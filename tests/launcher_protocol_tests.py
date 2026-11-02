"""Launcher must wait for a matching player, and never start another game on ambiguous discovery."""
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import ANY, Mock, call, patch
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import vr_display as display
import vr_launcher as launcher

ELEVATED=launcher.elevated


def no_real_processes(test):
    """No test asks Windows about the processes running here (Steam, a game); nothing runs as administrator."""
    for name,answer in (('elevated',False),('process_ids',[])):
        patcher=patch.object(launcher,name,return_value=answer)
        setattr(test,name,patcher.start())
        test.addCleanup(patcher.stop)


def clock(test):
    """Time that passes only while the launcher sleeps; returns it as a one-item list."""
    now=[0.]
    for name,stand_in in (('monotonic',lambda: now[0]),('sleep',lambda seconds: now.__setitem__(0,now[0]+seconds))):
        patcher=patch.object(launcher.time,name,stand_in)
        patcher.start()
        test.addCleanup(patcher.stop)
    return now


class FakeRegistry:
    """The game's keys: `values` is the graphics key, `keys` every key by path."""
    def __init__(self,values,others=None,refused=()):
        self.values=dict(values)
        self.keys={display.KEY:self.values,**{key:dict(v) for key,v in (others or {}).items()}}
        self.writes=[]
        self.refused=set(refused) # keys whose writes Windows refuses

    def read(self,names=display.NAMES,key=display.KEY):
        return {name:value for name,value in self.keys.get(key,{}).items() if name in names}

    def write(self,values,key=display.KEY):
        if key in self.refused: raise PermissionError(13,'Access is denied')
        self.writes.append(dict(values))
        stored=self.keys.setdefault(key,{})
        for name,value in values.items():
            if value is None: stored.pop(name,None)
            else: stored[name]=value


USER=dict(Fullscreen=1,ExclusiveFullscreen=0,WindowMaximized=0,WindowLeft=0,WindowTop=0,
          WindowWidth=2736,WindowHeight=2736,UserWindowWidth=2736,UserWindowHeight=2736)


class DesktopViewTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory()
        self.backup=pathlib.Path(self.folder.name)/'reports'/'desktop-view-before-vr.json'
        no_real_processes(self)

    def tearDown(self):
        self.folder.cleanup()

    def test_small_window_is_16_by_9_centred_and_fits(self):
        # 1920 x 1080 for the HUD panel's texture (native_hud.cpp), whatever the desktop's shape.
        w=display.small_window(3440,1440)
        self.assertEqual((w['Fullscreen'],w['ExclusiveFullscreen'],w['WindowWidth'],w['WindowHeight'],
                          w['WindowLeft'],w['WindowTop']),(0,0,1920,1080,760,180))
        self.assertEqual((w['UserWindowWidth'],w['UserWindowHeight']),(1920,1080))
        self.assertEqual((display.small_window(2560,1440)['WindowWidth'],display.small_window(3840,2160)['WindowHeight']),
                         (1920,1080))
        # At most three quarters of the desktop's height, and its width.
        self.assertEqual((display.small_window(1920,1080)['WindowWidth'],display.small_window(1920,1080)['WindowHeight']),
                         (1440,810))
        tiny=display.small_window(800,450)
        self.assertEqual((tiny['WindowWidth'],tiny['WindowHeight'],tiny['WindowLeft'],tiny['WindowTop']),(599,337,100,56))
        narrow=display.small_window(1000,2000)
        self.assertEqual((narrow['WindowWidth'],narrow['WindowHeight']),(1000,562))

    def test_launch_saves_the_users_settings_once_and_restores_them(self):
        registry=FakeRegistry(USER)
        written=display.prepare_launch(registry=registry,backup=self.backup,screen=(3440,1440))
        self.assertEqual(written,display.small_window(3440,1440))
        self.assertEqual((registry.values['Fullscreen'],registry.values['WindowWidth']),(0,1920))
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
        # The window keeps the user's settings; the session's own still wait to be restored.
        self.assertEqual(json.loads(self.backup.read_text())['values'],{})
        self.assertTrue(display.restore(registry,self.backup))
        self.assertFalse(display.pending(self.backup))

    def test_every_session_turns_off_windows_gaming_input_and_frame_generation(self):
        # The October 7 Steam Link session: Windows.Gaming.Input found SteamVR's virtual gamepads, the game
        # turned XInput off, and Spidy's controller (XInput) was never read again.
        graphics={**USER,'DLSSG':1,'FrameGen':2}
        registry=FakeRegistry(graphics,{display.INPUT_KEY:dict(EnableWindowsGamingInput=1,EnableLibScePad=1)})
        display.prepare_launch(small=False,registry=registry,backup=self.backup)
        self.assertEqual(registry.keys[display.INPUT_KEY],dict(EnableWindowsGamingInput=0,EnableLibScePad=1))
        self.assertEqual((registry.values['DLSSG'],registry.values['FrameGen'],registry.values['Fullscreen']),(0,0,1))
        self.assertTrue(display.restore(registry,self.backup))
        self.assertEqual(registry.keys[display.INPUT_KEY],dict(EnableWindowsGamingInput=1,EnableLibScePad=1))
        self.assertEqual(registry.values,graphics)
        # A value the game never saved: created for the session, deleted afterwards. Frame generation the
        # game never saved stays unsaved.
        fresh=FakeRegistry(USER)
        self.assertEqual(display.prepare_launch(registry=fresh,backup=self.backup,screen=(1920,1080))['WindowWidth'],1440)
        self.assertEqual(fresh.keys[display.INPUT_KEY],dict(EnableWindowsGamingInput=0))
        self.assertNotIn('DLSSG',fresh.values)
        self.assertTrue(display.restore(fresh,self.backup))
        self.assertEqual((fresh.keys[display.INPUT_KEY],fresh.values),({},USER))

    def test_a_session_turns_off_depth_of_field_and_sharpening_unless_told_to_keep_them(self):
        # In the headset the game's depth of field blurs away from where you look, and its sharpening rings on
        # edges: off for the session (sharpening at 1, where the game's slider stores its lowest), back after it.
        graphics={**USER,'DOFQuality':2,'Sharpness':700}
        registry=FakeRegistry(graphics)
        display.prepare_launch(registry=registry,backup=self.backup,screen=(1920,1080))
        self.assertEqual((registry.values['DOFQuality'],registry.values['Sharpness']),(0,1))
        self.assertTrue(display.restore(registry,self.backup))
        self.assertEqual(registry.values,graphics)
        # Kept as the player has them: one, or both. What is kept is not in the backup either.
        for keep,expected in ((('depth_of_field',),(2,1)),(('sharpening',),(0,700)),
                              (('depth_of_field','sharpening'),(2,700))):
            with self.subTest(keep=keep):
                registry=FakeRegistry(graphics)
                display.prepare_launch(registry=registry,backup=self.backup,screen=(1920,1080),keep=keep)
                self.assertEqual((registry.values['DOFQuality'],registry.values['Sharpness']),expected)
                saved=json.loads(self.backup.read_text())['session'][0]['values']
                self.assertEqual('DOFQuality' in saved,'depth_of_field' not in keep)
                self.assertEqual('Sharpness' in saved,'sharpening' not in keep)
                self.assertTrue(display.restore(registry,self.backup))
                self.assertEqual(registry.values,graphics)
        # Values the game never saved stay unsaved, and a backup from before October 12 still restores.
        fresh=FakeRegistry(USER)
        display.prepare_launch(registry=fresh,backup=self.backup,screen=(1920,1080))
        self.assertNotIn('DOFQuality',fresh.values)
        self.assertNotIn('Sharpness',fresh.values)
        self.assertTrue(display.restore(fresh,self.backup))
        old=FakeRegistry({**graphics,'VSync':0})
        self.backup.write_text(json.dumps(dict(key=display.KEY,values={},session=[
            dict(key=display.KEY,values=dict(VSync=1)),dict(key=display.INPUT_KEY,values={})])))
        self.assertTrue(display.restore(old,self.backup))
        self.assertEqual(old.values,{**graphics,'VSync':1})

    def test_every_session_turns_off_ray_traced_reflections(self):
        # October 13: with them on, the game's own view, which rests in VR play, hung the graphics device on the
        # first frame it drew again (a menu, the map, a cutscene). Off for the session, back after it, and the
        # session can say what it changed.
        graphics={**USER,'RTReflections':1,'VSync':1}
        registry=FakeRegistry(graphics)
        self.assertEqual(display.session_changes(self.backup),{})
        display.prepare_launch(registry=registry,backup=self.backup,screen=(1920,1080))
        self.assertEqual(registry.values['RTReflections'],0)
        self.assertEqual(display.session_changes(self.backup),dict(RTReflections=1,VSync=1,EnableWindowsGamingInput=None))
        self.assertTrue(display.restore(registry,self.backup))
        self.assertEqual(registry.values,graphics)
        self.assertEqual(display.session_changes(self.backup),{})
        # Off already, or never saved by the game: nothing to say, and nothing made.
        off=FakeRegistry({**USER,'RTReflections':0})
        display.prepare_launch(registry=off,backup=self.backup,screen=(1920,1080))
        self.assertFalse(display.session_changes(self.backup).get('RTReflections'))
        self.assertTrue(display.restore(off,self.backup))
        fresh=FakeRegistry(USER)
        display.prepare_launch(registry=fresh,backup=self.backup,screen=(1920,1080))
        self.assertNotIn('RTReflections',fresh.values)
        self.assertNotIn('RTReflections',display.session_changes(self.backup))
        self.assertTrue(display.restore(fresh,self.backup))
        self.assertEqual(fresh.values,USER)

    def test_every_session_puts_the_aspect_ratio_at_auto(self):
        # October 11: two players' games ended as VR started with a fixed shape chosen in the game's
        # display settings (ForcedAspectRatio counts the choices from Auto). Auto for the session, back
        # after it, and the session can say what it changed.
        graphics={**USER,'ForcedAspectRatio':1,'AspectRatio':0}
        registry=FakeRegistry(graphics)
        display.prepare_launch(registry=registry,backup=self.backup,screen=(1920,1080))
        self.assertEqual((registry.values['ForcedAspectRatio'],registry.values['AspectRatio']),(0,0))
        self.assertEqual(display.session_changes(self.backup),dict(ForcedAspectRatio=1,EnableWindowsGamingInput=None))
        self.assertTrue(display.restore(registry,self.backup))
        self.assertEqual(registry.values,graphics)
        # At Auto already, or never saved by the game: nothing to say, and nothing made.
        auto=FakeRegistry({**USER,'ForcedAspectRatio':0})
        display.prepare_launch(registry=auto,backup=self.backup,screen=(1920,1080))
        self.assertFalse(display.session_changes(self.backup).get('ForcedAspectRatio'))
        self.assertTrue(display.restore(auto,self.backup))
        fresh=FakeRegistry(USER)
        display.prepare_launch(registry=fresh,backup=self.backup,screen=(1920,1080))
        self.assertNotIn('ForcedAspectRatio',fresh.values)
        self.assertTrue(display.restore(fresh,self.backup))
        self.assertEqual(fresh.values,USER)

    def test_every_session_turns_off_nvidia_reflex(self):
        # October 16: a player's game with Reflex On + Boost (ReflexMode counts the choices from Off) lost its
        # graphics device by the 19th eye picture, every session. Off for the session, back after it, and the
        # session can say what it changed.
        graphics={**USER,'ReflexMode':2}
        registry=FakeRegistry(graphics)
        display.prepare_launch(registry=registry,backup=self.backup,screen=(1920,1080))
        self.assertEqual(registry.values['ReflexMode'],0)
        self.assertEqual(display.session_changes(self.backup),dict(ReflexMode=2,EnableWindowsGamingInput=None))
        self.assertTrue(display.restore(registry,self.backup))
        self.assertEqual(registry.values,graphics)
        # Off already, or never saved by the game: nothing to say, and nothing made.
        off=FakeRegistry({**USER,'ReflexMode':0})
        display.prepare_launch(registry=off,backup=self.backup,screen=(1920,1080))
        self.assertFalse(display.session_changes(self.backup).get('ReflexMode'))
        self.assertTrue(display.restore(off,self.backup))
        fresh=FakeRegistry(USER)
        display.prepare_launch(registry=fresh,backup=self.backup,screen=(1920,1080))
        self.assertNotIn('ReflexMode',fresh.values)
        self.assertTrue(display.restore(fresh,self.backup))
        self.assertEqual(fresh.values,USER)
        # A backup made before the setting joined the session still restores.
        old=FakeRegistry({**USER,'ReflexMode':2,'VSync':0})
        self.backup.write_text(json.dumps(dict(key=display.KEY,values={},session=[
            dict(key=display.KEY,values=dict(VSync=1)),dict(key=display.INPUT_KEY,values={})])))
        self.assertTrue(display.restore(old,self.backup))
        self.assertEqual(old.values,{**USER,'ReflexMode':2,'VSync':1})

    def test_every_session_turns_off_vertical_sync(self):
        # The October 10 session started with the game's VSync on at 120 Hz: frames of 36-45 ms, whole
        # numbers of the monitor's 8.3 ms. It comes back afterwards; a value the game never saved stays unsaved.
        graphics={**USER,'VSync':1}
        registry=FakeRegistry(graphics)
        display.prepare_launch(registry=registry,backup=self.backup,screen=(1920,1080))
        self.assertEqual(registry.values['VSync'],0)
        self.assertTrue(display.restore(registry,self.backup))
        self.assertEqual(registry.values,graphics)
        fresh=FakeRegistry(USER)
        display.prepare_launch(registry=fresh,backup=self.backup,screen=(1920,1080))
        self.assertNotIn('VSync',fresh.values)
        self.assertTrue(display.restore(fresh,self.backup))
        self.assertEqual(fresh.values,USER)

    def test_a_backup_from_before_the_session_settings_still_restores(self):
        self.backup.parent.mkdir(parents=True)
        self.backup.write_text(json.dumps(dict(key=display.KEY,values=USER)))
        registry=FakeRegistry(dict(USER,Fullscreen=0,WindowWidth=960))
        self.assertTrue(display.restore(registry,self.backup))
        self.assertEqual(registry.values,USER)

    def test_settings_the_game_never_saved_are_not_created(self):
        partial=FakeRegistry(dict(Fullscreen=1,WindowWidth=800,WindowHeight=600))
        self.assertEqual(set(display.shrink(partial,self.backup,(3440,1440))),{'Fullscreen','WindowWidth','WindowHeight'})
        other=self.backup.with_name('other.json')
        self.assertIsNone(display.shrink(FakeRegistry({}),other,(3440,1440)))
        self.assertFalse(other.exists())

    def test_refused_changes_leave_the_users_settings(self):
        # A player's PC on October 9: Windows refused every value, the launcher's traceback ended the launch.
        graphics={**USER,'DLSSG':1}
        refused=FakeRegistry(graphics,refused={display.KEY,display.INPUT_KEY})
        with self.assertRaises(PermissionError): display.prepare_launch(registry=refused,backup=self.backup,screen=(3440,1440))
        self.assertEqual((refused.values,refused.writes),(graphics,[]))
        self.assertFalse(display.pending(self.backup))
        # Only the controllers' key refused: the window and frame generation are put back.
        partial=FakeRegistry(graphics,refused={display.INPUT_KEY})
        with self.assertRaises(PermissionError): display.prepare_launch(registry=partial,backup=self.backup,screen=(3440,1440))
        self.assertEqual(partial.values,graphics)
        self.assertFalse(display.pending(self.backup))

    def test_restore_writes_only_what_changed(self):
        registry=FakeRegistry(USER,refused={display.KEY,display.INPUT_KEY})
        self.backup.parent.mkdir(parents=True)
        self.backup.write_text(json.dumps(dict(key=display.KEY,values=USER,session=[
            dict(key=display.INPUT_KEY,values=dict(EnableWindowsGamingInput=None))])))
        self.assertTrue(display.restore(registry,self.backup))
        self.assertFalse(display.pending(self.backup))
        # A value that differs and cannot be written: the backup stays for the next try.
        self.backup.write_text(json.dumps(dict(key=display.KEY,values=dict(USER,WindowWidth=960))))
        with self.assertRaises(PermissionError): display.restore(registry,self.backup)
        self.assertTrue(display.pending(self.backup))

    def test_session_warns_instead_of_failing_when_restoring_is_refused(self):
        import io,run_game_vr
        self.addCleanup(run_game_vr.settle_display.__dict__.pop,'deferred',None)
        shown=io.StringIO()
        with patch.object(run_game_vr.vr_display,'pending',return_value=True),              patch.object(run_game_vr.vr_display,'restore',side_effect=PermissionError(13,'Access is denied')),              patch('sys.stdout',shown):
            run_game_vr.settle_display(False)
            run_game_vr.settle_display(False) # once
        self.assertEqual(shown.getvalue().count('WARNING: Windows refused restoring'),1)

    def test_unrecognized_backup_is_never_written(self):
        self.backup.parent.mkdir(parents=True)
        registry=FakeRegistry({})
        session=lambda key,values: dict(key=display.KEY,values={},session=[dict(key=key,values=values)])
        for saved in (dict(key='Other',values=dict(Fullscreen=1)),dict(key=display.KEY,values=dict(Fullscreen=-1)),
                      dict(key=display.KEY,values=dict(Monitor=1)),dict(key=display.KEY,values=[1]),
                      session('Other',dict(EnableWindowsGamingInput=1)),session(display.INPUT_KEY,dict(Button_A_1=1)),
                      session(display.KEY,dict(DLSSG=None)),session(display.INPUT_KEY,dict(EnableWindowsGamingInput=-1)),
                      dict(key=display.KEY,values={},session={})):
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
    def setUp(self):
        no_real_processes(self)

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

    def test_the_epic_games_stores_copy_is_started_when_steam_has_none(self):
        game=Mock(pid=42)
        missing=RuntimeError('Spider-Man is not running.')
        link='com.epicgames.launcher://apps/ns%3Aitem%3Aapp?action=launch&silent=true'
        prepare=Mock()
        with patch.object(launcher,'find_game',side_effect=[missing,42,42]), \
             patch.object(launcher,'Game',return_value=game),patch.object(launcher.time,'sleep'), \
             patch.object(launcher,'steam_executable',return_value=None), \
             patch.object(launcher,'epic_launch',return_value=link), \
             patch.object(launcher.os,'startfile',create=True) as started, \
             patch.object(launcher.subprocess,'Popen') as steam:
            self.assertIs(launcher.wait_for_game(prepare=prepare,ready=lambda game: True),game)
        started.assert_called_once_with(link)
        prepare.assert_called_once_with()
        steam.assert_not_called()
        # Steam with the game installed starts Steam's copy, Epic or not.
        with patch.object(launcher,'find_game',side_effect=[missing,42,42]), \
             patch.object(launcher,'Game',return_value=game),patch.object(launcher.time,'sleep'), \
             patch.object(launcher,'steam_executable',return_value=pathlib.Path('C:/Steam/steam.exe')), \
             patch.object(launcher,'steam_has_game',return_value=True), \
             patch.object(launcher,'epic_launch',return_value=link), \
             patch.object(launcher.os,'startfile',create=True) as started, \
             patch.object(launcher.subprocess,'Popen') as steam:
            launcher.wait_for_game(ready=lambda game: True)
        started.assert_not_called()
        steam.assert_called_once()

    def test_game_closed_before_load_releases_handle(self):
        game=Mock(pid=42)
        with patch.object(launcher,'find_game',side_effect=[42,42,RuntimeError('Spider-Man is not running.')]), \
             patch.object(launcher,'Game',return_value=game),patch.object(launcher,'ready_player',return_value=False), \
             patch.object(launcher.time,'sleep'):
            with self.assertRaisesRegex(RuntimeError,'game closed'): launcher.wait_for_game()
            game.close.assert_called_once()

    def test_new_process_is_prepared_once_before_the_game_is_opened(self):
        order=Mock()
        game=Mock(pid=42)
        order.game.side_effect=[OSError('still being set up'),game]
        order.ready.return_value=True
        missing=RuntimeError('Spider-Man is not running.')
        with patch.object(launcher,'find_game',side_effect=[missing,missing,missing,42,42]), \
             patch.object(launcher.pathlib.Path,'is_file',return_value=True), \
             patch.object(launcher.subprocess,'Popen',order.launch),patch.object(launcher,'Game',order.game), \
             patch.object(launcher.time,'sleep',order.sleep):
            self.assertIs(launcher.wait_for_game(early=order.early,ready=order.ready),game)
        # The game creates its render memory three seconds in: the process is looked for twenty times
        # a second, and prepared before the half second that opening it takes.
        self.assertEqual(order.mock_calls,[call.launch(ANY),call.sleep(.05),call.sleep(.05),call.early(42),
                                           call.game(42),call.sleep(.05),call.game(42),call.ready(game)])

    def test_replaced_process_is_prepared_again_and_the_old_handle_released(self):
        first,second=Mock(pid=42),Mock(pid=43)
        early=Mock()
        with patch.object(launcher,'find_game',side_effect=[42,42,42,43]), \
             patch.object(launcher,'Game',side_effect=[first,second]),patch.object(launcher.time,'sleep'), \
             patch.object(launcher.subprocess,'Popen') as launch:
            self.assertIs(launcher.wait_for_game(early=early,ready=Mock(side_effect=[False,False,True])),second)
            launch.assert_not_called()
        self.assertEqual(early.mock_calls,[call(42),call(43)])
        first.close.assert_called_once()
        second.close.assert_not_called()

    def test_an_administrators_game_ends_the_launch_at_once(self):
        # A player's Quest Link launch reported on October 10: "render memory module unavailable ([WinError 5])",
        # three minutes of asking, then "No loaded player within three minutes. Load a save".
        self.elevated.side_effect=lambda pid=None: pid==42
        early=Mock()
        with patch.object(launcher,'find_game',return_value=42),patch.object(launcher,'Game') as opened, \
             patch.object(launcher.time,'sleep') as slept:
            with self.assertRaisesRegex(launcher.NeedsAdministrator,'Spider-Man is running as administrator'):
                launcher.wait_for_game(early=early,ready=lambda game: True)
        self.assertEqual((early.mock_calls,opened.mock_calls,slept.mock_calls),([],[],[]))
        # A launcher that runs as administrator itself goes on.
        self.elevated.side_effect=lambda pid=None: True
        game=Mock(pid=42)
        with patch.object(launcher,'find_game',return_value=42),patch.object(launcher,'Game',return_value=game):
            self.assertIs(launcher.wait_for_game(early=early,ready=lambda game: True),game)
        early.assert_called_once_with(42)

    def test_an_administrators_steam_is_refused_before_anything_changes(self):
        missing=RuntimeError('Spider-Man is not running.')
        self.process_ids.return_value=[7]
        self.elevated.side_effect=lambda pid=None: pid==7
        prepare=Mock()
        with patch.object(launcher,'find_game',side_effect=missing), \
             patch.object(launcher.pathlib.Path,'is_file',return_value=True), \
             patch.object(launcher.subprocess,'Popen') as launch:
            with self.assertRaisesRegex(launcher.NeedsAdministrator,'Steam runs as administrator'):
                launcher.wait_for_game(prepare=prepare)
        self.process_ids.assert_called_once_with('steam.exe')
        self.assertEqual((prepare.mock_calls,launch.mock_calls),([],[]))
        # A Steam that is not running and is set to run as administrator: Windows refuses starting it.
        self.process_ids.return_value=[]
        with patch.object(launcher,'find_game',side_effect=missing), \
             patch.object(launcher.pathlib.Path,'is_file',return_value=True), \
             patch.object(launcher.subprocess,'Popen',side_effect=OSError(None,'requires elevation',None,740)):
            with self.assertRaisesRegex(launcher.NeedsAdministrator,'Steam runs as administrator'):
                launcher.wait_for_game()
        # Any other refusal to start Steam stays what it is.
        with patch.object(launcher,'find_game',side_effect=missing), \
             patch.object(launcher.pathlib.Path,'is_file',return_value=True), \
             patch.object(launcher.subprocess,'Popen',side_effect=FileNotFoundError(2,'No such file')):
            with self.assertRaises(FileNotFoundError): launcher.wait_for_game()

    def test_a_game_that_keeps_refusing_ends_the_launch_with_the_reason(self):
        now=clock(self)
        denied=PermissionError(13,'Access is denied')
        def refused(game=False,own=False):
            self.elevated.side_effect=lambda pid=None: own if pid is None else game
            now[0]=0.
            with patch.object(launcher,'find_game',return_value=42),patch.object(launcher,'Game',side_effect=denied):
                with self.assertRaises(RuntimeError) as raised: launcher.wait_for_game(ready=lambda game: True)
            # Within the patience a starting process is given, not the three minutes.
            self.assertAlmostEqual(now[0],launcher.REFUSAL_PATIENCE,delta=.1)
            return raised.exception
        # Nothing runs as administrator, or Spidy does too: something else on the PC guards the game.
        for own in (False,True):
            error=refused(own=own)
            self.assertNotIsInstance(error,launcher.NeedsAdministrator)
            self.assertRegex(str(error),'Access is denied.*security program')
        # Windows does not say how the game runs: administrator rights are the first thing to try.
        error=refused(game=None)
        self.assertIsInstance(error,launcher.NeedsAdministrator)
        self.assertRegex(str(error),'Access is denied.*may be running as administrator')
        # An administrator's game that had no answer yet when it was first seen.
        answers=iter([False,None])
        self.elevated.side_effect=lambda pid=None: next(answers,pid==42)
        now[0]=0.
        with patch.object(launcher,'find_game',return_value=42),patch.object(launcher,'Game',side_effect=denied):
            with self.assertRaisesRegex(launcher.NeedsAdministrator,'Spider-Man is running as administrator'):
                launcher.wait_for_game(ready=lambda game: True)

    def test_a_process_that_refuses_for_a_moment_is_opened(self):
        now=clock(self)
        game=Mock(pid=42)
        denied=PermissionError(13,'Access is denied')
        with patch.object(launcher,'find_game',return_value=42), \
             patch.object(launcher,'Game',side_effect=[denied,denied,game]):
            self.assertIs(launcher.wait_for_game(ready=lambda game: True),game)
        self.assertAlmostEqual(now[0],.1)

    def test_a_launch_that_runs_out_of_time_says_what_was_missing(self):
        clock(self)
        missing=RuntimeError('Spider-Man is not running.')
        with patch.object(launcher,'find_game',side_effect=missing), \
             patch.object(launcher.pathlib.Path,'is_file',return_value=True),patch.object(launcher.subprocess,'Popen'):
            with self.assertRaisesRegex(RuntimeError,'did not start within three minutes'):
                launcher.wait_for_game(ready=lambda game: True)
        with patch.object(launcher,'find_game',return_value=42), \
             patch.object(launcher,'Game',side_effect=OSError(None,'Only part of the request was completed',None,299)):
            with self.assertRaisesRegex(RuntimeError,'could not open the game.*Only part'):
                launcher.wait_for_game(ready=lambda game: True)
        with patch.object(launcher,'find_game',return_value=42),patch.object(launcher,'Game',return_value=Mock(pid=42)), \
             patch.object(launcher,'ready_player',return_value=False):
            with self.assertRaisesRegex(RuntimeError,'No loaded player'): launcher.wait_for_game()

    def test_windows_says_whether_this_process_runs_as_administrator(self):
        self.assertIn(ELEVATED(),(True,False))
        # A process that cannot be opened (the idle process): no answer, not a guess.
        self.assertIsNone(ELEVATED(0))

    def test_only_a_missing_administrator_right_offers_the_restart(self):
        import run_game_vr
        self.assertEqual(run_game_vr.exit_code(launcher.NeedsAdministrator('as administrator')),
                         launcher.NEEDS_ADMINISTRATOR_EXIT)
        self.assertEqual((launcher.NEEDS_ADMINISTRATOR_EXIT,run_game_vr.exit_code(RuntimeError('no headset')),
                          run_game_vr.exit_code(PermissionError(13,'Access is denied'))),(5,2,2))

    def test_a_game_that_holds_spidy_already_offers_to_close_it(self):
        import run_game_vr
        from bridge_game import GameNeedsRestart,OtherBuild
        self.assertEqual((launcher.CLOSE_GAME_EXIT,run_game_vr.exit_code(GameNeedsRestart('VR already ran')),
                          run_game_vr.exit_code(OtherBuild('older build'))),(6,6,6))
        # Another build's render memory module is a refusal no second ask changes.
        with patch.object(launcher,'open_process',return_value=5),patch.object(launcher,'close'), \
             patch.object(launcher,'load_module',side_effect=OtherBuild('older build')) as load, \
             patch.object(launcher.time,'sleep') as slept:
            with self.assertRaises(OtherBuild): launcher.enlarge_render_memory(42)
        load.assert_called_once()
        slept.assert_not_called()

    def test_closing_the_game_is_its_close_button_and_never_a_kill(self):
        posted=[]
        with patch.object(launcher,'open_process',return_value=9),patch.object(launcher,'close') as closed, \
             patch.object(launcher._user32,'PostMessageW',side_effect=lambda *args: posted.append(args) or 1), \
             patch.object(launcher,'_wait',return_value=0) as waited,patch.object(launcher.time,'sleep') as slept:
            self.assertTrue(launcher.close_game(42,windows_of=lambda pid: [0x111,0x222]))
        self.assertEqual(posted,[(0x111,launcher.WM_CLOSE,0,0),(0x222,launcher.WM_CLOSE,0,0)])
        waited.assert_called_once_with(9,30000)
        slept.assert_called_once_with(3)  # for Steam
        closed.assert_called_once_with(9)
        # Still running after the wait: False, and nothing forced.
        with patch.object(launcher,'open_process',return_value=9),patch.object(launcher,'close'), \
             patch.object(launcher._user32,'PostMessageW',return_value=1), \
             patch.object(launcher,'_wait',return_value=0x102),patch.object(launcher.time,'sleep') as slept:
            self.assertFalse(launcher.close_game(42,windows_of=lambda pid: [0x111]))
        slept.assert_not_called()
        # No window to close (still starting) is no close either; a process that is gone is closed.
        with patch.object(launcher,'open_process',return_value=9),patch.object(launcher,'close'), \
             patch.object(launcher,'_wait') as waited:
            self.assertFalse(launcher.close_game(42,windows_of=lambda pid: []))
        waited.assert_not_called()
        with patch.object(launcher,'open_process',return_value=0):
            self.assertTrue(launcher.close_game(42,windows_of=lambda pid: [0x111]))

    def test_render_memory_module_retries_a_starting_process_but_not_its_own_refusal(self):
        exports=dict(SpidyRenderMemoryStart=0x1000,SpidyRenderMemoryStop=0x2000,SpidyRenderMemoryData=0x3000,
                     SpidyScenePool=0x4000,SpidyScenePoolData=0x5000)
        with patch.object(launcher,'open_process',side_effect=[0,5,5,5]), \
             patch.object(launcher,'load_module',side_effect=[StopIteration(),OSError('refused'),(exports,'hash')]), \
             patch.object(launcher,'call_remote',return_value=0) as called,patch.object(launcher,'close') as closed, \
             patch.object(launcher.time,'sleep') as slept:
            self.assertEqual(launcher.enlarge_render_memory(42),exports)
        # The ring, then the larger slot pool; a pool of the game's own size asks for nothing.
        self.assertEqual(called.mock_calls,[call(5,0x1000,launcher.RENDER_RING_MB),
                                            call(5,0x4000,launcher.SCENE_POOL_PERCENT)])
        with patch.object(launcher,'open_process',return_value=5), \
             patch.object(launcher,'load_module',return_value=(exports,'hash')), \
             patch.object(launcher,'call_remote',return_value=0) as called,patch.object(launcher,'close'):
            launcher.enlarge_render_memory(42,scene_pool=0)
        called.assert_called_once_with(5,0x1000,launcher.RENDER_RING_MB)
        self.assertEqual(slept.mock_calls,[call(.05)]*3)
        self.assertEqual(closed.mock_calls,[call(5)]*3) # every handle that was opened
        # 7001: not the supported game build. Asking again cannot change the answer.
        with patch.object(launcher,'open_process',return_value=5), \
             patch.object(launcher,'load_module',return_value=(exports,'hash')) as load, \
             patch.object(launcher,'call_remote',return_value=7001),patch.object(launcher,'close') as closed, \
             patch.object(launcher.time,'sleep') as slept:
            with self.assertRaisesRegex(RuntimeError,'7001'): launcher.enlarge_render_memory(42)
        load.assert_called_once()
        slept.assert_not_called()
        closed.assert_called_once_with(5)
        # A process that stays unusable is given up as an error the session tools report.
        with patch.object(launcher,'open_process',return_value=5),patch.object(launcher,'close'), \
             patch.object(launcher,'load_module',side_effect=StopIteration()) as load, \
             patch.object(launcher.time,'sleep'),patch.object(launcher.time,'monotonic',side_effect=[0,1,3]):
            with self.assertRaisesRegex(RuntimeError,'no modules'): launcher.enlarge_render_memory(42)
        self.assertEqual(load.call_count,2)

    def test_module_snapshot_retries_a_process_that_is_loading_modules(self):
        import ctypes
        import capture_game_state as state
        def answers(*codes):
            # Each call fails with the next code, then a snapshot handle (77) comes back.
            remaining=list(codes)
            def snapshot(flags,pid):
                if not remaining: return 77
                ctypes.set_last_error(remaining.pop(0))
                return ctypes.c_void_p(-1).value
            return snapshot
        # ERROR_BAD_LENGTH while the game loads its DLLs (it ended a launch on 2026-10-06): asked again.
        with patch.object(state,'snapshot',side_effect=answers(24,24)) as taken,patch.object(state.time,'sleep') as slept:
            self.assertEqual(state.checked_snapshot(0x18,42),77)
        self.assertEqual((taken.call_count,slept.call_count),(3,2))
        # Any other failure is final at once.
        with patch.object(state,'snapshot',side_effect=answers(5)),patch.object(state.time,'sleep') as slept:
            with self.assertRaises(OSError) as raised: state.checked_snapshot(0x18,42)
        self.assertEqual(raised.exception.winerror,5)
        slept.assert_not_called()
        # A process whose modules keep changing is given up once the patience is spent.
        with patch.object(state,'snapshot',side_effect=answers(*[24]*9)) as taken,patch.object(state.time,'sleep'), \
             patch.object(state.time,'monotonic',side_effect=[0,1,3]):
            with self.assertRaises(OSError) as raised: state.checked_snapshot(0x18,42)
        self.assertEqual((raised.exception.winerror,taken.call_count),(24,2))


if __name__=='__main__': unittest.main()
