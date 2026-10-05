"""Launcher must wait for a matching player, and never start another game on ambiguous discovery."""
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import vr_display as display
import vr_launcher as launcher


class FakeRegistry:
    def __init__(self,values):
        self.values=dict(values)
        self.writes=[]

    def read(self):
        return dict(self.values)

    def write(self,values):
        self.writes.append(dict(values))
        self.values.update(values)


USER=dict(Fullscreen=1,ExclusiveFullscreen=0,WindowMaximized=0,WindowLeft=0,WindowTop=0,
          WindowWidth=2736,WindowHeight=2736,UserWindowWidth=2736,UserWindowHeight=2736)


class DesktopViewTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory()
        self.backup=pathlib.Path(self.folder.name)/'reports'/'desktop-view-before-vr.json'

    def tearDown(self):
        self.folder.cleanup()

    def test_small_window_keeps_the_desktop_shape_centred_and_fits(self):
        w=display.small_window(3440,1440)
        self.assertEqual((w['Fullscreen'],w['ExclusiveFullscreen'],w['WindowWidth'],w['WindowHeight'],
                          w['WindowLeft'],w['WindowTop']),(0,0,1290,540,1075,450))
        self.assertEqual((w['UserWindowWidth'],w['UserWindowHeight']),(1290,540))
        self.assertEqual(display.small_window(1920,1080)['WindowWidth'],960)
        tiny=display.small_window(800,450)
        self.assertEqual((tiny['WindowWidth'],tiny['WindowHeight'],tiny['WindowLeft'],tiny['WindowTop']),(800,450,0,0))

    def test_launch_saves_the_users_settings_once_and_restores_them(self):
        registry=FakeRegistry(USER)
        written=display.prepare_launch(registry=registry,backup=self.backup,screen=(3440,1440))
        self.assertEqual(written,display.small_window(3440,1440))
        self.assertEqual((registry.values['Fullscreen'],registry.values['WindowWidth']),(0,1290))
        display.shrink(registry,self.backup,(3440,1440)) # a second launch must not save the VR window
        self.assertEqual(json.loads(self.backup.read_text())['values'],USER)
        self.assertTrue(display.restore(registry,self.backup))
        self.assertEqual(registry.values,USER)
        self.assertFalse(display.pending(self.backup))
        self.assertFalse(display.restore(registry,self.backup))

    def test_next_launch_restores_an_interrupted_session_first(self):
        registry=FakeRegistry(USER)
        display.shrink(registry,self.backup,(3440,1440))
        self.assertIsNone(display.prepare_launch(small=False,registry=registry,backup=self.backup))
        self.assertEqual(registry.values,USER)
        self.assertFalse(display.pending(self.backup))

    def test_settings_the_game_never_saved_are_not_created(self):
        partial=FakeRegistry(dict(Fullscreen=1,WindowWidth=800,WindowHeight=600))
        self.assertEqual(set(display.shrink(partial,self.backup,(3440,1440))),{'Fullscreen','WindowWidth','WindowHeight'})
        other=self.backup.with_name('other.json')
        self.assertIsNone(display.shrink(FakeRegistry({}),other,(3440,1440)))
        self.assertFalse(other.exists())

    def test_unrecognized_backup_is_never_written(self):
        self.backup.parent.mkdir(parents=True)
        registry=FakeRegistry({})
        for saved in (dict(key='Other',values=dict(Fullscreen=1)),dict(key=display.KEY,values=dict(Fullscreen=-1)),
                      dict(key=display.KEY,values=dict(Monitor=1)),dict(key=display.KEY,values=[1])):
            self.backup.write_text(json.dumps(saved))
            with self.assertRaisesRegex(RuntimeError,'Unrecognized'): display.restore(registry,self.backup)
        self.assertEqual(registry.writes,[])
        self.assertTrue(self.backup.exists())

    def test_display_is_prepared_only_when_the_launcher_starts_the_game(self):
        order=Mock()
        game=Mock(pid=42)
        with patch.object(launcher,'find_game',side_effect=[RuntimeError('Spider-Man is not running.'),42]), \
             patch.object(launcher.pathlib.Path,'is_file',return_value=True), \
             patch.object(launcher.subprocess,'Popen',order.launch),patch.object(launcher,'Game',return_value=game), \
             patch.object(launcher,'ready_player',return_value=True):
            self.assertIs(launcher.wait_for_game(prepare=order.prepare),game)
        self.assertEqual([call[0] for call in order.mock_calls],['prepare','launch'])
        prepare=Mock()
        with patch.object(launcher,'find_game',return_value=42),patch.object(launcher,'Game',return_value=game), \
             patch.object(launcher,'ready_player',return_value=True),patch.object(launcher.subprocess,'Popen') as launch:
            launcher.wait_for_game(prepare=prepare)
            prepare.assert_not_called()
            launch.assert_not_called()


class LauncherTests(unittest.TestCase):
    def test_player_requires_one_hero_and_matching_movement_record(self):
        game=Mock()
        hero=dict(kind='hero_local',actor_record='0x123')
        mover=dict(kind='hero_mover',actor_record='0x123')
        for records,expected in (([],False),([hero],False),([hero,mover],True),
                                 ([hero,{**mover,'actor_record':'0x456'}],False),([hero,hero,mover],False),
                                 ([hero,mover,mover],False)):
            game.registered_candidates.return_value=records
            self.assertEqual(launcher.ready_player(game),expected)

    def test_ambiguous_process_does_not_launch_another_game(self):
        with patch.object(launcher,'find_game',side_effect=RuntimeError('More than one Spider-Man process exists.')), \
             patch.object(launcher.subprocess,'Popen') as launch:
            with self.assertRaisesRegex(RuntimeError,'More than one'): launcher.wait_for_game()
            launch.assert_not_called()

    def test_existing_game_waits_for_gameplay_without_relaunch(self):
        game=Mock(pid=42)
        with patch.object(launcher,'find_game',return_value=42), patch.object(launcher,'Game',return_value=game), \
             patch.object(launcher,'ready_player',side_effect=[False,True]),patch.object(launcher.time,'sleep'), \
             patch.object(launcher.subprocess,'Popen') as launch:
            self.assertIs(launcher.wait_for_game(),game)
            game.close.assert_not_called()
            launch.assert_not_called()

    def test_game_closed_before_load_releases_handle(self):
        game=Mock(pid=42)
        with patch.object(launcher,'find_game',side_effect=[42,42,RuntimeError('Spider-Man is not running.')]), \
             patch.object(launcher,'Game',return_value=game),patch.object(launcher,'ready_player',return_value=False), \
             patch.object(launcher.time,'sleep'):
            with self.assertRaisesRegex(RuntimeError,'game closed'): launcher.wait_for_game()
            game.close.assert_called_once()


if __name__=='__main__': unittest.main()
