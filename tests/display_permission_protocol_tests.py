"""Settings access failures must not abort VR startup or lose the user's backup."""
import argparse
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_game_vr
import vr_display as display
from launcher_protocol_tests import FakeRegistry, USER


class DisplayPermissionTests(unittest.TestCase):
    def test_partial_window_write_rolls_back_or_keeps_backup_for_retry(self):
        for refuse_restore in (False, True):
            with self.subTest(refuse_restore=refuse_restore), tempfile.TemporaryDirectory() as folder:
                registry = FakeRegistry(USER)
                write = registry.write
                denied = PermissionError(13, 'Window change refused')

                def partial_write(values, key=display.KEY):
                    # SetValueEx can succeed for several values before refusing the next one.
                    write(dict(list(values.items())[:4]), key)
                    registry.write = Mock(side_effect=PermissionError(13, 'Restore refused')) if refuse_restore else write
                    raise denied

                registry.write = partial_write
                backup = pathlib.Path(folder)/'before.json'
                with self.assertRaises(PermissionError) as raised:
                    display.prepare_launch(registry=registry, backup=backup, screen=(3440, 1440))
                self.assertIs(raised.exception, denied)
                self.assertEqual(backup.exists(), refuse_restore)
                if refuse_restore:
                    self.assertEqual(json.loads(backup.read_text())['values'], USER)
                    self.assertEqual(registry.values['Fullscreen'], 0)
                    registry.write = write
                    self.assertTrue(display.restore(registry, backup))
                self.assertEqual(registry.values, USER)
                self.assertFalse(backup.exists())

    def test_startup_continues_after_settings_failure_and_records_it(self):
        class Prepared(Exception):
            pass

        def wait_for_game(**callbacks):
            callbacks['prepare']()
            raise Prepared()  # Stop before launching or attaching to a real game.

        for full_desktop, pending in ((False, False), (False, True), (True, False), (True, True)):
            with self.subTest(full_desktop=full_desktop, pending=pending):
                args = argparse.Namespace(stop_event=None, xr_runtime='auto', render_scale=100, size=0,
                                          auto_launch=True, full_desktop_view=full_desktop)
                startup = run_game_vr.Startup(pathlib.Path('unused.json'))
                shown = io.StringIO()
                with patch.object(run_game_vr, 'game_running', return_value=False), \
                     patch.object(run_game_vr, 'preflight', return_value=('runtime.json', {'recommended_eye': None})), \
                     patch.object(run_game_vr, 'wait_for_game', side_effect=wait_for_game), \
                     patch.object(display, 'prepare_launch', side_effect=PermissionError(13, 'Access is denied')) as prepare, \
                     patch.object(display, 'pending', return_value=pending), patch('sys.stdout', shown):
                    with self.assertRaises(Prepared):
                        run_game_vr.session(args, startup)
                prepare.assert_called_once_with(small=not full_desktop)
                self.assertIn('Access is denied', startup.fields['game_settings_refused'])
                self.assertIn('WARNING:', shown.getvalue())
                self.assertIn('frame generation off', shown.getvalue())
                self.assertEqual('still need restoring' in shown.getvalue(), pending)
                self.assertNotIn('Game settings for VR:', shown.getvalue())


if __name__ == '__main__':
    unittest.main()
