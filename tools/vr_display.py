"""Start the game with a small desktop view for VR, and restore the display settings afterwards.

In immersive VR the game's own view must keep running: the engine does occlusion,
key-light shadow setup, and auto-exposure for it, and Spidy places it at the head
so that work fits the eyes. Its pixels are not needed. A small window keeps that
work and drops most of its rendering cost (a 3440 x 1440 desktop renders about a
fifth of the pixels of two 3072 x 3264 eyes). The game reads these values once at
startup. The previous values are saved in reports/ and written back once the game
has closed.

Every VR session also turns off two game settings, whatever its window (SESSION):
frame generation, and the game's Windows.Gaming.Input, which under SteamVR takes
the controllers away from Spidy's XInput controller. They are saved and restored
with the window's.
"""
import argparse
import ctypes
import json
import pathlib
import sys
import winreg

ROOT = pathlib.Path(__file__).resolve().parents[1]
KEY = r"Software\Insomniac Games\Marvel's Spider-Man Remastered\Graphics"
INPUT_KEY = r"Software\Insomniac Games\Marvel's Spider-Man Remastered\Input"
BACKUP = ROOT/'reports'/'desktop-view-before-vr.json'
NAMES = ('Fullscreen', 'ExclusiveFullscreen', 'WindowMaximized', 'WindowLeft', 'WindowTop',
         'WindowWidth', 'WindowHeight', 'UserWindowWidth', 'UserWindowHeight')
HEIGHT = 540
# What every VR session changes, whatever its window: (key, values, create). A value the game never
# saved is created only where `create` is set, and deleted again afterwards.
# - Frame generation (DLSS or FSR) off: Spidy draws the eyes itself, and generated frames change how
#   the game presents its window (which the headset's game screen copies) on a queue of their own.
#   Every tested session had it off.
# - Windows.Gaming.Input off: in the Steam Link session of October 7 the game found two Steam virtual
#   gamepads (Valve 28de:11ff) there, then logged "XInput disabled, disconnecting controller" and never
#   read Spidy's XInput controller again (pad_reads 0): its menus did not move. At 0 the game skips
#   Windows.Gaming.Input altogether (Spider-Man.exe 1d14c89); XInput and DualSense (libScePad) stay.
SESSION = ((KEY, dict(DLSSG=0, FrameGen=0), False),
           (INPUT_KEY, dict(EnableWindowsGamingInput=0), True))


class Registry:
    """The game's keys; only REG_DWORD values are read or written (by default NAMES in the graphics key)."""

    def read(self, names=NAMES, key=KEY):
        values = {}
        try:
            opened = winreg.OpenKey(winreg.HKEY_CURRENT_USER, key)
        except FileNotFoundError:
            return values
        with opened:
            for name in names:
                try:
                    value, kind = winreg.QueryValueEx(opened, name)
                except FileNotFoundError:
                    continue
                if kind == winreg.REG_DWORD:
                    values[name] = value
        return values

    def write(self, values, key=KEY):
        """None deletes a value: the session created it."""
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, key, 0, winreg.KEY_SET_VALUE) as opened:
            for name, value in values.items():
                if value is None:
                    try:
                        winreg.DeleteValue(opened, name)
                    except FileNotFoundError:
                        pass
                else:
                    winreg.SetValueEx(opened, name, 0, winreg.REG_DWORD, value)


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


def _saved(values, names, created=False):
    """Values a backup may hold: known names with REG_DWORD numbers, and None for one the session created."""
    return isinstance(values, dict) and all(
        name in names and ((created and value is None) or (isinstance(value, int) and 0 <= value < 1 << 32))
        for name, value in values.items())


def restore(registry=None, backup=BACKUP):
    """Write back the values saved before VR. Returns False when nothing was saved."""
    if not backup.exists():
        return False
    saved = json.loads(backup.read_text())
    values = saved.get('values') if saved.get('key') == KEY else None
    # Backups before October 7's SteamVR fix hold the window's values only.
    session = saved.get('session', [])
    allowed = {key: (tuple(wanted), create) for key, wanted, create in SESSION}
    known = isinstance(session, list) and all(
        isinstance(entry, dict) and entry.get('key') in allowed and
        _saved(entry.get('values'), *allowed[entry['key']]) for entry in session)
    if not _saved(values, NAMES) or not known:
        raise RuntimeError(f'Unrecognized display backup: {backup}')
    registry = registry or Registry()
    if values:
        registry.write(values)
    for entry in session:
        if entry['values']:
            registry.write(entry['values'], entry['key'])
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


def prepare_session(registry=None, backup=BACKUP):
    """Turn off what SESSION lists, adding the user's values to the backup (made here if the window
    keeps the user's settings). Returns the values written."""
    registry = registry or Registry()
    saved = json.loads(backup.read_text()) if backup.exists() else dict(key=KEY, values={})
    if 'session' not in saved:
        saved['session'] = []
        for key, wanted, create in SESSION:
            current = registry.read(tuple(wanted), key)
            saved['session'].append(dict(key=key, values={name: current.get(name) for name in wanted
                                                          if name in current or create}))
        backup.parent.mkdir(parents=True, exist_ok=True)
        backup.write_text(json.dumps(saved, indent=2)+'\n')
    before = {entry['key']: entry['values'] for entry in saved['session']}
    written = {}
    for key, wanted, _ in SESSION:
        values = {name: value for name, value in wanted.items() if name in before.get(key, {})}
        if values:
            registry.write(values, key)
            written.update(values)
    return written


def prepare_launch(small=True, registry=None, backup=BACKUP, screen=None):
    """Called with the game closed, just before it starts. A previous session's values are restored
    first, so a backup always holds the user's own settings. Returns the window's values written
    (None when the window keeps the user's settings)."""
    restore(registry, backup)
    window = shrink(registry, backup, screen) if small else None
    prepare_session(registry, backup)
    return window


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
