"""Start the supported Steam game and wait for a live player before attachment."""
import ctypes as c
from ctypes import wintypes as w
import msvcrt
import os
import pathlib
import subprocess
import time
from capture_game_state import Game, LIVE_VTABLES, find_game

ROOT=pathlib.Path(__file__).resolve().parents[1]
_exit_code=c.WinDLL('kernel32',use_last_error=True).GetExitCodeProcess
_exit_code.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
_exit_code.restype=w.BOOL


def alive(game):
    code=w.DWORD()
    return bool(game.handle and _exit_code(game.handle,c.byref(code)) and code.value==259)


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


def ready_player(game):
    LIVE_VTABLES['hero_mover']=0x38b2c98
    candidates=game.registered_candidates()
    heroes=[h for h in candidates if h['kind']=='hero_local']
    return len(heroes)==1 and sum(h['kind']=='hero_mover' and h['actor_record']==heroes[0]['actor_record']
                                 for h in candidates)==1


def wait_for_game(timeout=180,prepare=None):
    """`prepare` runs only when this launcher starts the game, before the game reads its settings."""
    try: pid=find_game()
    except RuntimeError as error:
        if not str(error).startswith('Spider-Man is not running.'): raise
        pid=None
    if pid is None:
        steam=pathlib.Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Steam/steam.exe'
        if not steam.is_file(): raise RuntimeError('Steam was not found. Start Spider-Man and run this launcher again.')
        if prepare: prepare()
        # -nolauncher is present in the supported Spider-Man executable.
        subprocess.Popen([str(steam),'-applaunch','1817070','-nolauncher'])
        print('Starting Spider-Man. Select your save and Continue; VR will attach automatically.',flush=True)
    else:
        print('Using the running game. Load your save; VR will attach automatically.',flush=True)
    game=None
    started=time.monotonic()
    try:
        while time.monotonic()-started<timeout:
            try: current=find_game()
            except RuntimeError as error:
                if not str(error).startswith('Spider-Man is not running.'): raise
                if game: raise RuntimeError('The game closed before gameplay became ready.')
                time.sleep(.5); continue
            if game is None or game.pid!=current:
                if game: game.close()
                game=Game(current)
            if ready_player(game):
                print('Player found. Starting VR.',flush=True)
                return game
            time.sleep(.5)
        raise RuntimeError('No loaded player within three minutes. Load a save, then run the launcher again.')
    except BaseException:
        if game: game.close()
        raise
