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


class FakeRegistry:
    """The game's keys: `values` is the graphics key, `keys` every key by path."""
    def __init__(self,values,others=None):
        self.values=dict(values)
        self.keys={display.KEY:self.values,**{key:dict(v) for key,v in (others or {}).items()}}
        self.writes=[]

    def read(self,names=display.NAMES,key=display.KEY):
        return {name:value for name,value in self.keys.get(key,{}).items() if name in names}

    def write(self,values,key=display.KEY):
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
        self.assertEqual(display.prepare_launch(registry=fresh,backup=self.backup,screen=(1920,1080))['WindowWidth'],960)
        self.assertEqual(fresh.keys[display.INPUT_KEY],dict(EnableWindowsGamingInput=0))
        self.assertNotIn('DLSSG',fresh.values)
        self.assertTrue(display.restore(fresh,self.backup))
        self.assertEqual((fresh.keys[display.INPUT_KEY],fresh.values),({},USER))

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

    def test_render_memory_module_retries_a_starting_process_but_not_its_own_refusal(self):
        exports=dict(SpidyRenderMemoryStart=0x1000,SpidyRenderMemoryStop=0x2000,SpidyRenderMemoryData=0x3000)
        with patch.object(launcher,'open_process',side_effect=[0,5,5,5]), \
             patch.object(launcher,'load_module',side_effect=[StopIteration(),OSError('refused'),(exports,'hash')]), \
             patch.object(launcher,'call_remote',return_value=0) as called,patch.object(launcher,'close') as closed, \
             patch.object(launcher.time,'sleep') as slept:
            self.assertEqual(launcher.enlarge_render_memory(42),exports)
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
