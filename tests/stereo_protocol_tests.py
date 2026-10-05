"""Check eye telemetry boundaries and reject torn process-memory snapshots."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from probe_stereo import snapshot, frame_snapshot


def payload(sequence=2, version=4):
    raw = bytearray(352)
    struct.pack_into('<4IQ', raw, 0, 0x53535042, version, 352, 2, sequence)
    for i in range(2):
        offset = 64 + i * 144
        struct.pack_into('<4Q4I', raw, offset, 0x10000+i*0x20000, 0, 0, 45, 17, 512, 512, 0)
        struct.pack_into('<Q2I', raw, offset+112, 0x90000+i*0x100000, 512*512*4, 12+i)
        struct.pack_into('<4I', raw, offset+128, 3072,3264,3072,3264)
    return bytes(raw)


class Reader:
    def __init__(self, values):
        self.values = iter(values)

    def read(self, address, size):
        return next(self.values)


class StereoProtocolTests(unittest.TestCase):
    def test_both_eye_readbacks_keep_their_identity(self):
        value = snapshot(Reader([payload(), struct.pack('<Q', 2)]), 0)
        self.assertEqual([e['readback'] for e in value['eyes']], [0x90000, 0x190000])
        self.assertEqual([e['readback_completed'] for e in value['eyes']], [12, 13])
        self.assertEqual([e['view'] for e in value['eyes']], [0x10000, 0x30000])
        self.assertEqual([(e['render_width'],e['render_height']) for e in value['eyes']], [(3072,3264)]*2)

    def test_eye_job_frames_report_reclaimed_copies_and_reject_old_layouts(self):
        raw = bytearray(160)
        struct.pack_into('<4IQ', raw, 0, 0x53455044, 2, 160, 0, 6)
        struct.pack_into('<17Q', raw, 24, *range(1, 17), 9)
        value = frame_snapshot(Reader([raw, struct.pack('<Q', 6)]), 0)
        self.assertEqual((value['error'], value['left_begins'], value['right_ends'], value['reclaimed']),
                         (0, 5, 8, 9))
        struct.pack_into('<Q', raw, 16, 7)
        self.assertIsNone(frame_snapshot(Reader([raw, struct.pack('<Q', 7)]*8), 0))
        old = bytearray(152)
        struct.pack_into('<4IQ', old, 0, 0x53455044, 1, 152, 0, 2)
        self.assertIsNone(frame_snapshot(Reader([old, struct.pack('<Q', 2)]), 0))
        struct.pack_into('<3I', raw, 0, 0x53455044, 1, 160)
        with self.assertRaisesRegex(RuntimeError, 'protocol mismatch'):
            frame_snapshot(Reader([raw, struct.pack('<Q', 6)]), 0)

    def test_torn_read_retries(self):
        value = snapshot(Reader([payload(2), struct.pack('<Q', 4), payload(4), struct.pack('<Q', 4)]), 0)
        self.assertEqual(value['sequence'], 4)

    def test_writer_in_progress_is_not_published(self):
        self.assertIsNone(snapshot(Reader([payload(3), struct.pack('<Q', 3)]*8), 0))

    def test_short_memory_read_is_not_published(self):
        self.assertIsNone(snapshot(Reader([payload()[:-1], struct.pack('<Q', 2)]), 0))

    def test_old_dll_protocol_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'protocol mismatch'):
            snapshot(Reader([payload(version=2), struct.pack('<Q', 2)]), 0)


if __name__ == '__main__':
    unittest.main()
