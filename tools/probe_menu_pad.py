"""Play the game's menus with Spidy's virtual Xbox controller and follow the player across reloads, no headset.

What a VR session does from the game's start, step by step in the running game:

    python tools/probe_menu_pad.py start             launch as the VR launcher does; time to the renderer
    python tools/probe_menu_pad.py pad a --until-player   press A every 2 s (title, Continue) until a player exists
    python tools/probe_menu_pad.py pad start          one press: Start, A, B, X, Y, up, down, left, right, lb, rb
    python tools/probe_menu_pad.py shot NAME          the game window as reports/menu-pad/NAME.png
    python tools/probe_menu_pad.py player --follow    find the player in-process, compare with an outside
                                                      search, hand it to the input bridge and sample the gate

The controller is the one the VR worker serves while the headset shows the game screen (src/game_pad.cpp);
the player search is the worker's own (src/game_player.cpp). The bridge starts without a player, as in VR.
"""
import argparse
import ctypes as c
import json
import math
import pathlib
import struct
import sys
import time
from capture_game_state import Game, LIVE_VTABLES, find_game, open_process, close
from bridge_game import ROOT, prepare, snapshot as bridge_snapshot
from observe_game import allocate, call_remote, call_with_payload, release, write
from probe_game_screen import game_window, client_size, window_rgb, user32
from run_game_vr import write_rgb_png, wait_for_renderer
from vr_launcher import alive, bring_to_front, wait_for_game, enlarge_render_memory
import vr_display

OUTPUT = ROOT/'reports/menu-pad'
XR_DLL = ROOT/'build/windows-ninja/spidy_stereo_probe.dll'
XR_EXPORTS = ('SpidyPadSubmit', 'SpidyPadSample', 'SpidyPlayerFind')
BRIDGE_EXPORTS = ('SpidyStart', 'SpidyStop', 'SpidySubmit', 'SpidyRetarget', 'SpidyBridgeData')
BUTTONS = {'up': 0x1, 'down': 0x2, 'left': 0x4, 'right': 0x8, 'start': 0x10, 'back': 0x20, 'ls': 0x40, 'rs': 0x80,
           'lb': 0x100, 'rb': 0x200, 'a': 0x1000, 'b': 0x2000, 'x': 0x4000, 'y': 0x8000}
# The cameras the VR gate accepts (game_bridge.cpp): follow and combat.
GAMEPLAY_MOVERS = {0x3871fd8: 'follow', 0x38720d0: 'combat'}


def attach():
    game = Game(find_game())
    process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
    if not process:
        raise c.WinError(c.get_last_error())
    xr, _ = prepare(game.pid, process, XR_DLL, ROOT/'reports/stereo-modules', XR_EXPORTS)
    bridge, _ = prepare(game.pid, process, names=BRIDGE_EXPORTS)
    return game, process, xr, bridge


def call_with_output(game, process, address, payload, size):
    """Remote call whose argument buffer is also its output; returns (code, bytes)."""
    remote = allocate(process, None, max(size, len(payload), 8), 0x3000, 4)
    if not remote:
        raise c.WinError(c.get_last_error())
    try:
        count = c.c_size_t()
        if payload and (not write(process, remote, payload, len(payload), c.byref(count)) or count.value != len(payload)):
            raise c.WinError(c.get_last_error())
        code = call_remote(process, address, remote)
        return code, game.read(remote, size)
    finally:
        release(process, remote, 0, 0x8000)


def pad(process, xr, buttons=0, hold_ms=150, sticks=(0, 0, 0, 0), triggers=(0, 0)):
    state = struct.pack('<H2B4h', buttons, *triggers, *sticks)
    code = call_with_payload(process, xr['SpidyPadSubmit'], state+struct.pack('<I', hold_ms))
    if code:
        raise RuntimeError(f'Virtual controller: {code}' + (' (the game has not loaded XInput yet)' if code == 8001 else ''))
    time.sleep(hold_ms/1000+.15)  # the press and its release, each seen by several game frames


def pad_telemetry(game, process, xr):
    code, raw = call_with_output(game, process, xr['SpidyPadSample'], b'', 24)
    if code or len(raw) != 24:
        return None
    reads, active, installed, buttons = struct.unpack('<2Q2I', raw)
    return dict(reads=reads, active=active, installed=bool(installed), buttons=hex(buttons))


def find_player(game, process, xr):
    code, raw = call_with_output(game, process, xr['SpidyPlayerFind'], b'', 32)
    hero, record, mover, microseconds = struct.unpack('<4Q', raw) if len(raw) == 32 else (0, 0, 0, 0)
    return (dict(hero=hex(hero), record=hex(record), mover=hex(mover)) if code == 0 else None), microseconds


def outside_player(game):
    """The launcher's search before the seventh build of October 5, from outside the process."""
    LIVE_VTABLES['hero_mover'] = 0x38b2c98
    found = game.registered_candidates()
    heroes = [x for x in found if x['kind'] == 'hero_local']
    return dict(hero=heroes[0]['object'], record=heroes[0]['actor_record']) if len(heroes) == 1 else None


def valid_matrix(m):
    return all(math.isfinite(v) and abs(v) < 1e6 for v in m) and \
        all(abs(sum(m[r*4+i]**2 for i in range(3))-1) < .05 for r in range(3))


def sample_gate(game, bridge, seconds=1.0):
    """How often the newest camera commit was fresh and on the player, as the VR gate reads it."""
    frequency = c.c_int64()
    c.windll.kernel32.QueryPerformanceFrequency(c.byref(frequency))
    first = bridge_snapshot(game, bridge['SpidyBridgeData'])
    open_samples = samples = 0
    movers = {}
    end = time.monotonic()+seconds
    while time.monotonic() < end:
        b = bridge_snapshot(game, bridge['SpidyBridgeData'])
        now = c.c_int64()
        c.windll.kernel32.QueryPerformanceCounter(c.byref(now))
        if b:
            samples += 1
            before = [*b['before']['basis'][0], 0, *b['before']['basis'][1], 0, *b['before']['basis'][2], 0,
                      *b['before']['position'], 1]
            player = [*b['player']['basis'][0], 0, *b['player']['basis'][1], 0, *b['player']['basis'][2], 0,
                      *b['player']['position'], 1]
            fresh = b['qpc'] and 0 <= now.value-b['qpc'] < frequency.value/10
            open_samples += bool(b['state'] == 1 and fresh and valid_matrix(before) and valid_matrix(player))
            table = game.pointer(b['mover'])
            name = GAMEPLAY_MOVERS.get(table-game.base, hex(table-game.base) if table > game.base else None)
            movers[name] = movers.get(name, 0)+1
        time.sleep(.01)
    last = bridge_snapshot(game, bridge['SpidyBridgeData'])
    return dict(open=open_samples/samples if samples else 0, samples=samples,
                matched=last['matched']-first['matched'] if first and last else None,
                camera_calls=last['camera_calls']-first['camera_calls'] if first and last else None, movers=movers)


def shot(game, name):
    hwnd = game_window(game.pid)
    width, height = client_size(hwnd)
    rgb = window_rgb(hwnd, width, height)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = OUTPUT/f'{name}.png'
    write_rgb_png(path, width, height, rgb)
    return path, sum(rgb)/len(rgb), user32.GetForegroundWindow() == hwnd


def main():
    try:
        user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    except AttributeError:
        user32.SetProcessDPIAware()
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('command', choices=('start', 'pad', 'shot', 'player', 'stop'))
    p.add_argument('arguments', nargs='*')
    p.add_argument('--hold', type=int, default=150, help='milliseconds a press lasts')
    p.add_argument('--left', type=float, nargs=2, default=(0, 0), metavar=('X', 'Y'),
                   help='left stick, -1..1 (Quest controllers have no D-pad: menus are played with this)')
    p.add_argument('--right', type=float, nargs=2, default=(0, 0), metavar=('X', 'Y'))
    p.add_argument('--until-player', action='store_true', help='repeat the press every 2 s until a player exists')
    p.add_argument('--follow', action='store_true', help='hand the player to the input bridge and sample the gate')
    a = p.parse_args()
    if a.command == 'start':
        started = time.monotonic()
        loaded = {}

        def early(pid):
            loaded.update(enlarge_render_memory(pid))
        game = wait_for_game(prepare=lambda: vr_display.prepare_launch(small=True), early=early, ready=lambda g: True)
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        try:
            queue = wait_for_renderer(game, process, loaded.get('SpidyRenderMemoryData'))
            renderer = time.monotonic()-started
            # The game pauses while another window is in front.
            front = bring_to_front(game.pid)
            bridge, _ = prepare(game.pid, process, names=BRIDGE_EXPORTS)
            code = call_with_payload(process, bridge['SpidyStart'],
                                     struct.pack('<4I3Q', 0x53424346, 1, 40, game.pid, game.base, 0, 0))
            prepare(game.pid, process, XR_DLL, ROOT/'reports/stereo-modules', XR_EXPORTS)
            print(json.dumps(dict(pid=game.pid, queue=hex(queue), renderer_seconds=round(renderer, 1),
                                  game_in_front=front, bridge_start=code, player=outside_player(game))), flush=True)
        finally:
            close(process)
        return 0
    game, process, xr, bridge = attach()
    try:
        if a.command == 'pad':
            buttons = 0
            for name in a.arguments:
                buttons |= BUTTONS[name]
            started = time.monotonic()
            sticks = [round(max(-1, min(1, v))*32767) for v in (*a.left, *a.right)]
            while True:
                pad(process, xr, buttons, a.hold, sticks)
                if not a.until_player:
                    break
                player, _ = find_player(game, process, xr)
                if player:
                    print(f'player after {time.monotonic()-started:.1f} s: {player}', flush=True)
                    break
                if time.monotonic()-started > 180 or not alive(game):
                    raise RuntimeError('No player within three minutes')
                time.sleep(1.85)
            print(json.dumps(dict(pressed=a.arguments, controller=pad_telemetry(game, process, xr))), flush=True)
        elif a.command == 'shot':
            path, mean, front = shot(game, a.arguments[0] if a.arguments else 'shot')
            print(f'{path} mean {mean:.1f}; game window in front: {front}', flush=True)
        elif a.command == 'player':
            inside, microseconds = find_player(game, process, xr)
            result = dict(inside=inside, search_us=microseconds, outside=outside_player(game))
            if inside and a.follow:
                hero, record = int(inside['hero'], 16), int(inside['record'], 16)
                result['before_follow'] = sample_gate(game, bridge, .5)
                result['retarget'] = call_with_payload(process, bridge['SpidyRetarget'],
                                                       struct.pack('<4I3Q', 0x53424346, 1, 40, game.pid, game.base,
                                                                   hero, record))
                result['after_follow'] = sample_gate(game, bridge, 1)
            print(json.dumps(result, indent=1), flush=True)
        elif a.command == 'stop':
            print(json.dumps(dict(bridge_stop=call_remote(process, bridge['SpidyStop']))), flush=True)
    finally:
        close(process)
        game.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
