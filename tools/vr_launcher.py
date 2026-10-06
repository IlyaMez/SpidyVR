"""Start the supported Steam game and wait for a live player before attachment.

Run as a script, it starts the game the way the VR launcher does (small window, larger render
memory) and returns once the process is up, for checks without a headset.
"""
import ctypes as c
from ctypes import wintypes as w
import msvcrt
import os
import pathlib
import subprocess
import time
from bridge_game import prepare as load_module
from capture_game_state import Game, LIVE_VTABLES, find_game, open_process, close
from observe_game import call_remote

ROOT=pathlib.Path(__file__).resolve().parents[1]
RENDER_MEMORY_DLL=ROOT/'build/windows-ninja/spidy_render_memory.dll'
# Megabytes of per-frame render memory for the game; its own ring is 128 MB. Two eye views and the
# head-aligned game view used 50-60 MB a frame at Times Square, and two frames must fit.
RENDER_RING_MB=512
# The module's hooks: the ring's creation and its end-of-frame rollover. They are in place from the
# game's start, so they are no sign of another session's leftovers.
RENDER_MEMORY_HOOKS=(0x1872d90,0x1872b90)
STEAM_APP_ID='1817070'
_exit_code=c.WinDLL('kernel32',use_last_error=True).GetExitCodeProcess
_exit_code.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
_exit_code.restype=w.BOOL
_user32=c.WinDLL('user32',use_last_error=True)
for _function,_result,_arguments in (
        (_user32.GetForegroundWindow,w.HWND,()),(_user32.SetForegroundWindow,w.BOOL,(w.HWND,)),
        (_user32.ShowWindow,w.BOOL,(w.HWND,c.c_int)),(_user32.IsIconic,w.BOOL,(w.HWND,)),
        (_user32.IsWindowVisible,w.BOOL,(w.HWND,)),(_user32.GetWindowTextLengthW,c.c_int,(w.HWND,)),
        (_user32.GetWindowThreadProcessId,w.DWORD,(w.HWND,c.POINTER(w.DWORD)))):
    _function.restype,_function.argtypes=_result,_arguments


class _KeyInput(c.Structure):
    _fields_=[('vk',w.WORD),('scan',w.WORD),('flags',w.DWORD),('time',w.DWORD),('extra',c.c_size_t)]


class _Input(c.Structure):
    class _Union(c.Union):
        _fields_=[('ki',_KeyInput),('padding',c.c_byte*32)]  # INPUT is 40 bytes
    _anonymous_=('u',)
    _fields_=[('type',w.DWORD),('u',_Union)]


def alive(game):
    code=w.DWORD()
    return bool(game.handle and _exit_code(game.handle,c.byref(code)) and code.value==259)


def bring_to_front(pid,wait=.5):
    """Put the game's window in front of the others; True once it is.

    The game pauses while another window is in front: on October 5 its intro sat still for three
    minutes behind the window that launched it, which is where Windows keeps a game Steam starts.
    """
    found=[]

    def visit(hwnd,_):
        owner=w.DWORD()
        _user32.GetWindowThreadProcessId(hwnd,c.byref(owner))
        if owner.value==pid and _user32.IsWindowVisible(hwnd) and _user32.GetWindowTextLengthW(hwnd):
            found.append(hwnd)
        return True
    _user32.EnumWindows(c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)(visit),0)
    if not found: return False
    hwnd=found[0]
    if _user32.GetForegroundWindow()==hwnd: return True
    if _user32.IsIconic(hwnd): _user32.ShowWindow(hwnd,9)
    # Windows lets the process that sent the last input choose the foreground window: a tap of Alt.
    alt=(_Input*2)()
    for i,flags in enumerate((0,2)):
        alt[i].type,alt[i].ki.vk,alt[i].ki.flags=1,0x12,flags
    _user32.SendInput(2,alt,c.sizeof(_Input))
    _user32.SetForegroundWindow(hwnd)
    time.sleep(wait)
    return _user32.GetForegroundWindow()==hwnd


class LauncherLock:
    def __enter__(self):
        folder=ROOT/'reports'
        folder.mkdir(exist_ok=True)
        self.file=(folder/'vr-launcher.lock').open('a+b')
        self.file.seek(0,2)
        if not self.file.tell(): self.file.write(b'0'); self.file.flush()
        self.file.seek(0)
        try: msvcrt.locking(self.file.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:
            self.file.close()
            raise RuntimeError('Spidy VR is already running. Use its existing launcher window.')
        return self

    def __exit__(self,*_):
        self.file.seek(0)
        msvcrt.locking(self.file.fileno(),msvcrt.LK_UNLCK,1)
        self.file.close()


def _registry_value(hive,key,name):
    import winreg
    try:
        with winreg.OpenKey(getattr(winreg,hive),key) as opened: return winreg.QueryValueEx(opened,name)[0]
    except OSError: return None


def steam_executable(read=_registry_value):
    """steam.exe where Steam's registry entries put it (any drive), else in its default folder."""
    candidates=[]
    for hive,key,name in (('HKEY_CURRENT_USER',r'Software\Valve\Steam','SteamExe'),
                          ('HKEY_CURRENT_USER',r'Software\Valve\Steam','SteamPath'),
                          ('HKEY_LOCAL_MACHINE',r'SOFTWARE\WOW6432Node\Valve\Steam','InstallPath')):
        value=read(hive,key,name)
        if value:
            path=pathlib.Path(value)
            candidates.append(path if path.suffix.lower()=='.exe' else path/'steam.exe')
    candidates.append(pathlib.Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Steam/steam.exe')
    return next((path for path in candidates if path.is_file()),None)


def ready_player(game):
    LIVE_VTABLES['hero_mover']=0x38b2c98
    candidates=game.registered_candidates()
    heroes=[h for h in candidates if h['kind']=='hero_local']
    return len(heroes)==1 and sum(h['kind']=='hero_mover' and h['actor_record']==heroes[0]['actor_record']
                                 for h in candidates)==1


def enlarge_render_memory(pid,megabytes=RENDER_RING_MB,patience=2.5):
    """Load Spidy's render memory module into the game and ask for a larger frame ring.

    The game creates its ring about three seconds after its process starts, and the ring cannot
    change afterwards, so this only takes effect in a game that has just started. Later it still
    reports how much of the game's ring frames use. Returns the module's export addresses.

    A process that is still being set up refuses module listings and remote calls for a moment;
    those are retried for `patience` seconds. The module's own refusal is final.
    """
    started=time.monotonic()
    while True:
        process=open_process(0x0400|0x0010|0x0020|0x0008|0x0002,False,pid)
        try:
            if not process: raise c.WinError(c.get_last_error())
            exports,_=load_module(pid,process,RENDER_MEMORY_DLL,ROOT/'reports/render-memory-modules',
                                  ('SpidyRenderMemoryStart','SpidyRenderMemoryStop','SpidyRenderMemoryData'))
            code=call_remote(process,exports['SpidyRenderMemoryStart'],megabytes)
        except (OSError,RuntimeError,StopIteration) as error:
            if time.monotonic()-started>=patience:
                if isinstance(error,StopIteration): raise RuntimeError('The game process lists no modules yet.') from error
                raise
            time.sleep(.05)
            continue
        finally:
            if process: close(process)
        if code: raise RuntimeError(f'Render memory module: {code}')
        return exports


def wait_for_game(timeout=180,prepare=None,early=None,ready=None):
    """`prepare` runs only when this launcher starts the game, before the game reads its settings.

    `early(pid)` runs once, as soon as the game process exists. `ready(game)` decides when to
    return; by default, when a save is loaded and its player exists.
    """
    ready=ready or ready_player
    try: pid=find_game()
    except RuntimeError as error:
        if not str(error).startswith('Spider-Man is not running.'): raise
        pid=None
    if pid is None:
        steam=steam_executable()
        if not steam: raise RuntimeError('Steam was not found. Start Spider-Man and run this launcher again.')
        if prepare: prepare()
        # -nolauncher is present in the supported Spider-Man executable.
        subprocess.Popen([str(steam),'-applaunch',STEAM_APP_ID,'-nolauncher'])
        print('Starting Spider-Man. Select your save and Continue; VR will attach automatically.'
              if ready is ready_player else 'Starting Spider-Man.',flush=True)
    else:
        print('Using the running game. Load your save; VR will attach automatically.'
              if ready is ready_player else 'Using the running game.',flush=True)
    game=prepared=None
    started=time.monotonic()
    try:
        while time.monotonic()-started<timeout:
            try: current=find_game()
            except RuntimeError as error:
                if not str(error).startswith('Spider-Man is not running.'): raise
                if game: raise RuntimeError('The game closed before gameplay became ready.')
                # A new process has about three seconds before it creates its render memory.
                time.sleep(.05); continue
            if game is None or game.pid!=current:
                if game: game.close()
                game=None
                # First: checking the executable takes half a second of the three there are.
                if early and current!=prepared:
                    prepared=current
                    early(current)
                try: game=Game(current)
                except OSError:
                    time.sleep(.05); continue  # the process is still being set up
            if ready(game):
                if ready is ready_player: print('Player found. Starting VR.',flush=True)
                return game
            time.sleep(.5)
        raise RuntimeError('No loaded player within three minutes. Load a save, then run the launcher again.')
    except BaseException:
        if game: game.close()
        raise


def main():
    import argparse
    import vr_display
    from probe_stereo import render_memory_snapshot
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--full-desktop-view',action='store_true',help="keep the game's own window settings")
    p.add_argument('--ring',type=int,default=RENDER_RING_MB,
                   help="megabytes of render memory; 0 keeps the game's own 128 MB ring")
    a=p.parse_args()
    loaded={}

    def early(pid):
        try: loaded.update(enlarge_render_memory(pid,a.ring))
        except (OSError,RuntimeError) as error: print(f'Render memory module unavailable: {error}',flush=True)
    game=wait_for_game(prepare=lambda: vr_display.prepare_launch(small=not a.full_desktop_view),early=early,
                       ready=lambda game: True)
    try:
        memory=None
        waited=time.monotonic()
        # The game creates its render memory about three seconds after it starts.
        while loaded and time.monotonic()-waited<20:
            memory=render_memory_snapshot(game,loaded['SpidyRenderMemoryData'])
            if memory and memory['ring']!='not_created': break
            time.sleep(.25)
        print(f'Game process {game.pid}; render memory: '+
              (f"{memory['ring']}, {memory['ring_mb']:.0f} MB." if memory else 'module not loaded.'),flush=True)
        print(r'Close the game when done, then run tools\vr_display.py --restore.',flush=True)
    finally:
        game.close()


if __name__=='__main__':
    main()
