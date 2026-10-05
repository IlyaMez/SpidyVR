"""Start the game with a small desktop view for VR, and restore the display settings afterwards.

In immersive VR the game's own view must keep running: the engine does occlusion,
key-light shadow setup, and auto-exposure for it, and Spidy places it at the head
so that work fits the eyes. Its pixels are not needed. A small window keeps that
work and drops most of its rendering cost (a 3440 x 1440 desktop renders about a
fifth of the pixels of two 3072 x 3264 eyes). The game reads these values once at
startup. The previous values are saved in reports/ and written back once the game
has closed.
"""
import argparse
import ctypes
import json
import pathlib
import sys
import winreg

ROOT = pathlib.Path(__file__).resolve().parents[1]
KEY = r"Software\Insomniac Games\Marvel's Spider-Man Remastered\Graphics"
BACKUP = ROOT/'reports'/'desktop-view-before-vr.json'
NAMES = ('Fullscreen', 'ExclusiveFullscreen', 'WindowMaximized', 'WindowLeft', 'WindowTop',
         'WindowWidth', 'WindowHeight', 'UserWindowWidth', 'UserWindowHeight')
HEIGHT = 540


class Registry:
    """The game's graphics key; only the REG_DWORD values in NAMES are read or written."""

    def read(self):
        values = {}
        try:
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY)
        except FileNotFoundError:
            return values
        with key:
            for name in NAMES:
                try:
                    value, kind = winreg.QueryValueEx(key, name)
                except FileNotFoundError:
                    continue
                if kind == winreg.REG_DWORD:
                    values[name] = value
        return values

    def write(self, values):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY, 0, winreg.KEY_SET_VALUE) as key:
            for name, value in values.items():
                winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, value)


def desktop():
    """Primary display size in physical pixels."""
    user32 = ctypes.windll.user32
    user32.SetProcessDPIAware()
    return user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)


def small_window(screen_width, screen_height, height=HEIGHT):
    """Centred window with the desktop's shape, `height` rows tall (never larger than the desktop)."""
    height = min(height, screen_height)
    width = min(round(screen_width*height/screen_height), screen_width)
    return dict(Fullscreen=0, ExclusiveFullscreen=0, WindowMaximized=0,
                WindowLeft=(screen_width-width)//2, WindowTop=(screen_height-height)//2,
                WindowWidth=width, WindowHeight=height, UserWindowWidth=width, UserWindowHeight=height)


def pending(backup=BACKUP):
    return backup.exists()


def restore(registry=None, backup=BACKUP):
    """Write back the values saved before VR. Returns False when nothing was saved."""
    if not backup.exists():
        return False
    saved = json.loads(backup.read_text())
    values = saved.get('values') if saved.get('key') == KEY else None
    if not isinstance(values, dict) or any(name not in NAMES or not isinstance(value, int) or
                                           not 0 <= value < 1 << 32 for name, value in values.items()):
        raise RuntimeError(f'Unrecognized display backup: {backup}')
    (registry or Registry()).write(values)
    backup.unlink()
    return True


def shrink(registry=None, backup=BACKUP, screen=None):
    """Save the current values (once) and select the small window. Returns the values written."""
    registry = registry or Registry()
    current = registry.read()
    if not current:
        return None  # the game has not saved display settings yet
    if not backup.exists():
        backup.parent.mkdir(parents=True, exist_ok=True)
        backup.write_text(json.dumps(dict(key=KEY, values=current), indent=2)+'\n')
    wanted = {name: value for name, value in small_window(*(screen or desktop())).items() if name in current}
    registry.write(wanted)
    return wanted


def prepare_launch(small=True, registry=None, backup=BACKUP, screen=None):
    """Called with the game closed, just before it starts. A previous session's values are restored
    first, so a backup always holds the user's own settings."""
    restore(registry, backup)
    return shrink(registry, backup, screen) if small else None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--restore', action='store_true', help='write back the display values saved before VR')
    a = p.parse_args()
    if not a.restore:
        print('Restore pending.' if pending() else 'No display values are waiting to be restored.')
        return 0
    from capture_game_state import find_game
    try:
        find_game()
        print('Close Spider-Man first; it may save its window settings when it exits.', file=sys.stderr)
        return 1
    except RuntimeError as error:
        if not str(error).startswith('Spider-Man is not running.'):
            raise
    print('Restored your display settings.' if restore() else 'No display values are waiting to be restored.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
