"""The SPIDY VR tab in the game's own Settings, without a headset: opened, changed and reset as a player would.

    python tools/probe_menu.py

It needs a started game with a loaded save (tools/probe_menu_pad.py start, then pad a --until-player), in which
Settings have not been opened yet (the game reopens them on the tab last used), and the game window in front. It starts the tab's hooks with test values (SpidyMenuStart: swing speed 33 m/s, which the tab shows
as its nearest step, 32; the web shooter off), pauses with Spidy's virtual Xbox controller, moving with its left stick
as the Touch controllers do, opens Settings, goes Up to SPIDY VR (the list wraps) and opens it. Then it puts the
web button on the trigger (WEB BUTTON: TRIGGER), switches the aim markers off, switches webs in open air off and puts
them back with X (RESET), steps the swing speed up (from 32 one step is 40), steps the weight up (from 80% one step is
100%), asks for a body calibration (CALIBRATE BODY: ON RESUME; without a headset nothing calibrates), steps snap turn
up, switches smooth turning on (its first speed, 60 degrees a second), steps the HUD down from MEDIUM to SMALL and to
OFF, switches the experimental flips on, and resets the whole tab with Y (RESET ALL, confirmed with A): Spidy's
defaults, the grip webbing again, the calibration no longer asked for, the HUD back to MEDIUM, while the web shooter,
which the tab does not offer, stays off. After each step it reads what the tab holds
(SpidyMenuSample) and saves a window capture in reports/menu-probe/. Last it backs out to the game, stops the hooks
(SpidyMenuStop) and checks that each hooked function starts with the game's own bytes again. Writes
reports/menu-probe.json; exits 1 when a step did not do what it should.
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
         0x1749ae0)
MAGIC = 0x554e4d53
# ProbeSettings / ProbeSample flags, as XrData's settings bits plus the aim markers and a body calibration asked for.
FLAGS = dict(web_grab=1, punch=2, body=4, air_webs=8, web_shooter=16, aim_markers=32, calibrate=64, flips=128,
             trigger_webs=256)
# The left stick pushed fully one way (x, y; up is +y).
STICK = dict(up=(0, 32767), down=(0, -32767), left=(-32767, 0), right=(32767, 0))


def settings_payload(flags, snap_turn, haptics, screen_size, swing_speed, smooth_turn=0, weight=80, hud=2):
    return struct.pack('<7If4I', MAGIC, 4, 48, flags, snap_turn, haptics, screen_size, swing_speed, smooth_turn,
                       weight, hud, 0)


def sample(game, process, exports):
    remote = pad.call_with_output(game, process, exports['SpidyMenuSample'], b'', 72)
    code, raw = remote
    if code or len(raw) != 72:
        raise RuntimeError(f'SpidyMenuSample: {code}')
    magic, version, size, installed, tabs, changes, status, flags, snap, haptics, screen, speed, smooth, weight, \
        hud, _ = struct.unpack('<4I2Q5If4I', raw)
    if (magic, version, size) != (MAGIC, 4, 72):
        raise RuntimeError('Menu protocol mismatch: rebuild and restart the game')
    values = {name: bool(flags & bit) for name, bit in FLAGS.items()}
    values.update(snap_turn=snap, smooth_turn=smooth, haptics=haptics, screen_size=screen,
                  swing_speed=round(speed, 1), weight=weight, hud=hud)
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
        start_flags = FLAGS['web_grab'] | FLAGS['punch'] | FLAGS['body'] | FLAGS['air_webs'] | FLAGS['aim_markers']
        code = call_with_payload(process, exports['SpidyMenuStart'], settings_payload(start_flags, 30, 100, 1, 33.0))
        if code:
            raise RuntimeError(f'SpidyMenuStart: {code}')
        report['hooked'] = {k: not v for k, v in hook_bytes(game).items()}
        before = sample(game, process, exports)
        # Pause; Settings is the fifth line of the free-roam pause menu.
        press('start', wait=1.2)
        press('down', 'down', 'down', 'down')
        press('a', wait=1.2)
        step('settings', tabs=before['tabs']+1, status=0)
        # Up from GAME wraps to the last tab, SPIDY VR.
        press('up', wait=.6)
        step('settings_last_tab')
        press('a', wait=1.2)
        step('spidy_vr', dict(trigger_webs=False, aim_markers=True, swing_speed=33.0, web_shooter=False, weight=80, hud=2))
        # The first row: WEB BUTTON, GRIP until stepped to TRIGGER.
        press('right', wait=.8)
        step('web_button_trigger', dict(trigger_webs=True, aim_markers=True), changes=1)
        press('down')
        press('right', wait=.8)
        step('aim_markers_off', dict(aim_markers=False, trigger_webs=True), changes=2)
        press('down')
        press('right', wait=.8)
        step('air_webs_off', dict(air_webs=False), changes=3)
        press('x', wait=.8)
        step('air_webs_reset', dict(air_webs=True), changes=4)
        press('down')
        press('right', wait=.8)
        step('swing_speed_40', dict(swing_speed=40.0), changes=5)
        press('down')
        press('right', wait=.8)
        step('weight_100', dict(weight=100, swing_speed=40.0), changes=6)
        # Down past the BODY heading: a calibration at the next gameplay.
        press('down')
        press('right', wait=.8)
        step('calibrate_on_resume', dict(calibrate=True, weight=100), changes=7)
        # Down past the COMFORT heading.
        press('down')
        press('right', wait=.8)
        step('snap_turn_45', dict(snap_turn=45, calibrate=True), changes=8)
        press('down')
        press('right', wait=.8)
        step('smooth_turn_60', dict(smooth_turn=60, snap_turn=45), changes=9)
        # Down past vibration and the screen size: the HUD, MEDIUM until stepped down to SMALL and OFF.
        press('down', 'down', 'down')
        press('left', wait=.8)
        step('hud_small', dict(hud=1, smooth_turn=60), changes=10)
        press('left', wait=.8)
        step('hud_off', dict(hud=0), changes=11)
        # Down past the EXPERIMENTAL heading: the flips, off until switched on.
        press('down')
        press('right', wait=.8)
        step('flips_on', dict(flips=True, hud=0), changes=12)
        press('y', wait=1.2)
        step('reset_all_asks')
        press('a', wait=1.2)
        # Spidy's defaults: nine settings changed back (the web button, aim markers, swing speed, weight, the
        # calibration asked for, snap turn, smooth turn, the HUD, the flips). The web shooter, which the tab does not
        # offer, stays as the probe started it.
        step('reset_all', dict(trigger_webs=False, aim_markers=True, air_webs=True, swing_speed=32.0, weight=80,
                               calibrate=False, snap_turn=30, smooth_turn=0, hud=2, flips=False, web_shooter=False,
                               body=True), changes=21)
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
