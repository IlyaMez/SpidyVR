"""Exercise torn-read rejection and the diagnostic DLL's wrong-process gate."""
import ctypes as c
from ctypes import wintypes as w
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from observe_game import BUILD_DLL, read_snapshot


def payload(sequence=2, magic=0x5350594f):
    value = bytearray(256)
    struct.pack_into('<4Iq8Q', value, 0, magic, 2, 256, 123, 10000000,
                     sequence, 4, 0, 0, 5000, 0x10000, 0x20000, 0x30000)
    struct.pack_into('<IIfI', value, 88, 456, 1, .01, 0x0100)
    identity = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 1., 2., 3., 1.)
    struct.pack_into('<16f', value, 120, *identity)
    struct.pack_into('<16f', value, 184, *identity)
    return bytes(value)


class FakeReader:
    def __init__(self, values):
        self.values = iter(values)

    def read(self, address, size):
        return next(self.values)


class ObserverTests(unittest.TestCase):
    def test_overlapping_write_is_retried(self):
        frame = read_snapshot(FakeReader([payload(2), struct.pack('<Q', 4),
                                          payload(4), struct.pack('<Q', 4)]), 0)
        self.assertEqual(frame['sequence'], 4)
        self.assertEqual(frame['thread'], 456)
        self.assertEqual(frame['camera']['position'], (1., 2., 3.))

    def test_writer_in_progress_never_publishes(self):
        self.assertIsNone(read_snapshot(FakeReader([payload(3), struct.pack('<Q', 3)] * 4), 0))

    def test_protocol_mismatch_fails(self):
        with self.assertRaisesRegex(RuntimeError, 'protocol mismatch'):
            read_snapshot(FakeReader([payload(magic=0), struct.pack('<Q', 2)]), 0)

    def test_unreadable_process_memory_is_not_a_frame(self):
        self.assertIsNone(read_snapshot(FakeReader([b'', b'']), 0))

    def test_dll_rejects_non_game_process(self):
        dll = c.WinDLL(str(BUILD_DLL))
        dll.SpidyStart.argtypes = [c.c_void_p]
        dll.SpidyStart.restype = w.DWORD
        dll.SpidyStop.argtypes = [c.c_void_p]
        dll.SpidyStop.restype = w.DWORD
        self.assertEqual(dll.SpidyStart(None), 1001)
        self.assertEqual(dll.SpidyStop(None), 0)


if __name__ == '__main__':
    unittest.main()
