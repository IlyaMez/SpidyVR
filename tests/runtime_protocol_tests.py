"""Other PCs: Steam anywhere, any OpenXR runtime, and the launcher window's stop signal."""
import ctypes as c
import pathlib
import signal
import struct
import sys
import tempfile
import time
import unittest
import unittest.mock
import uuid
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_game_vr
import vr_launcher
import xr_runtime


class SteamTests(unittest.TestCase):
    def test_steam_is_found_where_its_registry_entry_points_first(self):
        with tempfile.TemporaryDirectory() as folder:
            steam=pathlib.Path(folder)/'Games/Steam/steam.exe'
            steam.parent.mkdir(parents=True)
            steam.write_bytes(b'')
            values={('HKEY_CURRENT_USER','SteamExe'):str(steam).replace('\\','/').lower()}
            found=vr_launcher.steam_executable(lambda hive,key,name: values.get((hive,name)))
            self.assertEqual(found,pathlib.Path(values[('HKEY_CURRENT_USER','SteamExe')]))
            # A folder entry (SteamPath, InstallPath) means steam.exe inside it.
            values={('HKEY_LOCAL_MACHINE','InstallPath'):str(steam.parent)}
            self.assertEqual(vr_launcher.steam_executable(lambda hive,key,name: values.get((hive,name))),steam)

    def test_no_steam_anywhere_is_none(self):
        with unittest.mock.patch.dict('os.environ',{'ProgramFiles(x86)':r'Z:\nowhere'}):
            self.assertIsNone(vr_launcher.steam_executable(lambda *_: None))


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory()
        base=pathlib.Path(self.folder.name)
        self.vd,self.steamvr,self.oculus=(base/'VirtualDesktop/virtualdesktop-openxr.json',
                                          base/'SteamVR/steamxr_win64.json', base/'Oculus/oculus_openxr_64.json')
        for manifest in (self.vd,self.steamvr,self.oculus):
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{"runtime": {"name": "x"}}')

    def tearDown(self):
        self.folder.cleanup()

    def test_virtual_desktop_is_listed_first_and_chosen_when_installed(self):
        listing=lambda: (str(self.steamvr),[str(self.oculus),str(self.vd)])
        found=xr_runtime.runtimes(listing,self.vd)
        self.assertEqual([r['name'] for r in found],['Virtual Desktop','SteamVR','Meta Quest Link'])
        self.assertEqual([(r['active'],r['tested']) for r in found],[(False,True),(True,False),(False,False)])
        self.assertEqual(xr_runtime.choose(None,listing,self.vd),self.vd)

    def test_without_virtual_desktop_windows_active_runtime_is_used(self):
        missing=pathlib.Path(self.folder.name)/'none.json'
        listing=lambda: (str(self.steamvr),[str(self.oculus)])
        self.assertEqual(xr_runtime.choose(None,listing,missing),self.steamvr)

    def test_a_requested_runtime_must_exist_and_none_installed_is_explained(self):
        self.assertEqual(xr_runtime.choose(str(self.oculus)),self.oculus)
        with self.assertRaisesRegex(RuntimeError,'not installed'):
            xr_runtime.choose(r'Z:\nope\runtime.json')
        with self.assertRaisesRegex(RuntimeError,'Virtual Desktop'):
            xr_runtime.choose(None,lambda: (None,[]),pathlib.Path(self.folder.name)/'none.json')

    def test_xr_config_v7_carries_the_manifest_null_terminated(self):
        packed=run_game_vr.runtime_path(r'C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json')
        self.assertEqual(len(packed),520)
        self.assertEqual(packed.decode('utf-16-le').rstrip('\0'),
                         r'C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json')
        self.assertEqual(struct.calcsize('<4I7Q2IfI')+len(packed),608)  # game_xr.cpp's XrConfig
        self.assertEqual(run_game_vr.runtime_path('x'*259)[-2:],b'\0\0')
        with self.assertRaisesRegex(RuntimeError,'too long'):
            run_game_vr.runtime_path('x'*260)


class StopEventTests(unittest.TestCase):
    def test_the_named_event_reaches_the_sigint_handler(self):
        kernel=c.WinDLL('kernel32',use_last_error=True)
        kernel.CreateEventW.restype=c.c_void_p
        kernel.CreateEventW.argtypes=[c.c_void_p,c.c_int,c.c_int,c.c_wchar_p]
        kernel.SetEvent.argtypes=kernel.CloseHandle.argtypes=[c.c_void_p]
        name=f'Local\\SpidyTestStop-{uuid.uuid4()}'
        event=kernel.CreateEventW(None,True,False,name)
        stopped=[]
        previous=signal.signal(signal.SIGINT,lambda *_: stopped.append(True))
        try:
            run_game_vr.watch_stop_event(name)
            kernel.SetEvent(event)
            deadline=time.monotonic()+3
            while not stopped and time.monotonic()<deadline:
                time.sleep(.02)
            self.assertEqual(stopped,[True])
        finally:
            signal.signal(signal.SIGINT,previous)
            kernel.CloseHandle(event)

    def test_a_missing_event_fails_before_anything_starts(self):
        with self.assertRaises(OSError):
            run_game_vr.watch_stop_event(f'Local\\SpidyTestMissing-{uuid.uuid4()}')


if __name__=='__main__':
    unittest.main()
