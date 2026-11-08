"""Smoke-test the VR category tabs in a running, loaded game (no headset needed).

    python tools/probe_menu.py

Requires Settings not yet opened this game session, and the game in front.
Checks Webs and Swinging navigation, individual reset, category reset isolation,
and hook cleanup. This script does not launch the game. Other category contents
and dependencies are covered by the offline core tests.
"""
import argparse
import ctypes as c
import json
import pathlib
import struct
import sys
import time
from bridge_game import ROOT, prepare
from capture_game_state import Game, find_game, open_process, close
from inspect_game import PE
from observe_game import call_remote, call_with_payload
from probe_game_screen import game_window, client_size, window_rgb, user32
from run_game_vr import write_rgb_png
import probe_menu_pad as pad

OUTPUT = ROOT/'reports/menu-probe'
EXPORTS = ('SpidyMenuStart', 'SpidyMenuSample', 'SpidyMenuStop')
# game_menu.cpp's hooks: the UI's Flash calls, the pause menu's Settings callbacks, the settings system's questions
# about a setting (kind, value, whether it can change, default, default choice), its set and reset, and text lookups.
HOOKS = (0x1d1cf30, 0x7dc400, 0x72d640, 0x72d650, 0x72e1d0, 0x72d5f0, 0x72d610, 0x730ee0, 0x730ae0, 0x1749970,
         0x80a220,
         0x1749ae0)
MAGIC = 0x554e4d53
# ProbeSettings / ProbeSample flags, as XrData's settings bits plus the aim markers and a body calibration asked for.
FLAGS = dict(web_grab=1, punch=2, body=4, air_webs=8, web_shooter=16, aim_markers=32, calibrate=64, flips=128,
             trigger_webs=256, stand_on_walls=512, arcade_webs=2048)
# The left stick pushed fully one way (x, y; up is +y).
STICK = dict(up=(0, 32767), down=(0, -32767), left=(-32767, 0), right=(32767, 0))


def settings_payload(flags, snap_turn, haptics, screen_size, swing_speed, smooth_turn=0, weight=80, hud=2,
                     flip_speed=180, body_haptics=100):
    return struct.pack('<7If6I', MAGIC, 6, 56, flags, snap_turn, haptics, screen_size, swing_speed, smooth_turn,
                       weight, hud, flip_speed, body_haptics, 0)


def sample(game, process, exports):
    remote = pad.call_with_output(game, process, exports['SpidyMenuSample'], b'', 80)
    code, raw = remote
    if code or len(raw) != 80:
        raise RuntimeError(f'SpidyMenuSample: {code}')
    magic, version, size, installed, tabs, changes, status, flags, snap, haptics, screen, speed, smooth, weight, \
        hud, flip_speed, body_haptics, _ = struct.unpack('<4I2Q5If6I', raw)
    if (magic, version, size) != (MAGIC, 6, 80):
        raise RuntimeError('Menu protocol mismatch: rebuild and restart the game')
    values = {name: bool(flags & bit) for name, bit in FLAGS.items()}
    values.update(snap_turn=snap, smooth_turn=smooth, haptics=haptics, screen_size=screen,
                  swing_speed=round(speed, 1), weight=weight, hud=hud, flip_speed=flip_speed,
                  body_haptics=body_haptics)
    return dict(installed=bool(installed), tabs=tabs, changes=changes, status=status, values=values)


def capture(game, name):
    hwnd = game_window(game.pid)
    width, height = client_size(hwnd)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = OUTPUT/f'{name}.png'
    write_rgb_png(path, width, height, window_rgb(hwnd, width, height))
    return str(path.relative_to(ROOT))


def hook_bytes(game):
    """Whether each hooked function starts with the game's own bytes (from the executable on disk)."""
    pe = PE(game.path.read_bytes())
    return {hex(rva): game.read(game.base+rva, 9) == pe.bytes(rva, 9) for rva in HOOKS}


def main():
    try:
        user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    except AttributeError:
        user32.SetProcessDPIAware()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/menu-probe.json')
    args = parser.parse_args()
    game, process, xr, _ = pad.attach()
    exports, _ = prepare(game.pid, process, pad.XR_DLL, ROOT/'reports/stereo-modules', EXPORTS)
    report = dict(pid=game.pid, steps=[])
    failures = []

    def press(*names, wait=.35):
        # Directions on the left stick, as the Touch controllers give them (they have no D-pad).
        for name in names:
            if name in STICK:
                pad.pad(process, xr, 0, sticks=(*STICK[name], 0, 0))
            else:
                pad.pad(process, xr, pad.BUTTONS[name])
            time.sleep(wait)

    def step(name, expected=None, **checks):
        now = sample(game, process, exports)
        entry = dict(step=name, capture=capture(game, name), **now)
        problems = [f'{key}={now["values"][key]!r}, expected {value!r}' for key, value in (expected or {}).items()
                    if now['values'][key] != value]
        problems += [f'{key}={now[key]!r}, expected {value!r}' for key, value in checks.items() if now[key] != value]
        if problems:
            entry['problems'] = problems
            failures.append(f'{name}: ' + '; '.join(problems))
        report['steps'].append(entry)
        print(json.dumps(entry), flush=True)
        return now

    try:
        report['hooks_before'] = hook_bytes(game)
        start_flags = FLAGS['web_grab'] | FLAGS['punch'] | FLAGS['body'] | FLAGS['air_webs'] | \
            FLAGS['aim_markers'] | FLAGS['stand_on_walls']
        code = call_with_payload(process, exports['SpidyMenuStart'], settings_payload(start_flags, 30, 100, 1, 33.0))
        if code:
            raise RuntimeError(f'SpidyMenuStart: {code}')
        report['hooked'] = {k: not v for k, v in hook_bytes(game).items()}
        before = sample(game, process, exports)
        # Pause; Settings is the line above the last (QUIT), which Up from RESUME wraps to: two presses,
        # however many lines the menu has (a hideout or a mission adds ABANDON MISSION above PHOTO MODE,
        # and four presses down opened Photo Mode there on October 10).
        press('start', wait=1.2)
        press('up', 'up')
        press('a', wait=1.2)
        step('settings', tabs=before['tabs']+1, status=0)
        # The seven VR tabs follow the game's tabs. Up from GAME wraps to
        # Display, then Feedback, Flips, Movement, Swinging and Webs.
        press('up', 'up', 'up', 'up', 'up', 'up', wait=.6)
        press('a', wait=1.2)
        step('vr_webs', dict(arcade_webs=False, trigger_webs=False, aim_markers=True,
                             swing_speed=33.0, web_shooter=False, weight=80))
        press('right', wait=.8)
        step('web_style_arcade', dict(arcade_webs=True))
        press('down')
        press('right', wait=.8)
        step('web_button_trigger', dict(trigger_webs=True))
        press('down')
        press('right', wait=.8)
        step('aim_markers_off', dict(aim_markers=False))
        press('down')
        press('right', wait=.8)
        step('air_webs_off', dict(air_webs=False))
        press('x', wait=.8)
        step('air_webs_reset', dict(air_webs=True))
        press('y', wait=1.2)
        press('a', wait=1.2)
        step('reset_webs_only', dict(arcade_webs=False, trigger_webs=False, aim_markers=True,
                                    air_webs=True, swing_speed=33.0, weight=80, web_shooter=False))
        # Back to the category list, one down to Swinging.
        press('b', wait=.8)
        press('down', wait=.6)
        press('a', wait=1.2)
        press('right', wait=.8)
        step('swing_speed_40', dict(swing_speed=40.0))
        press('down')
        press('right', wait=.8)
        step('weight_100', dict(weight=100))
        press('y', wait=1.2)
        press('a', wait=1.2)
        step('reset_swinging_only', dict(swing_speed=32.0, weight=100, web_shooter=False,
                                        arcade_webs=False, snap_turn=30, smooth_turn=0, hud=2))
        press('b', wait=.8)
        press('b', wait=.8)
        press('b', wait=1.5)
        step('resumed')
    finally:
        report['stop'] = call_remote(process, exports['SpidyMenuStop'])
        report['restored'] = hook_bytes(game)
        if not all(report['restored'].values()):
            failures.append('a hooked function does not start with the game\'s own bytes after SpidyMenuStop')
        report['failures'] = failures
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=1))
        close(process)
        game.close()
    print(json.dumps(dict(failures=failures, hooked=report.get('hooked'), restored=report['restored'],
                          report=str(args.output))), flush=True)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
