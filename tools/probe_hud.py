"""Check, in the running game and without a headset, where VR's eyes show the game's HUD.

The game's HUD panel (health, gadgets, minimap, prompts) is a model the game hangs in front of its
own camera; while immersive Spidy places it in front of the head instead (native_hud.hpp). This
probe drives the eye views as a VR session does (views 29: the game's own view on the head, eye
occlusion) from the player's position and saves both eyes at each step:

  NAME              hold the head still for a moment, then save the eyes (NAME.png)
  yaw:DEG           turn the head this far from the game camera's heading (0 at start)
  pitch:DEG         look up (positive) or down
  turn:DEG          turn the head this many degrees a second from now on (0 stops); a NAME step
                    taken while turning saves the eyes mid-turn
  flat:1, flat:0    switch the eyes to VR's flat screen (they copy the game's camera) and back
  screen:NAME       send no eye commands for a moment, as while the headset shows the game screen
                    (menus, cutscenes), and save the game's window (NAME-window.png)
  hud:N             the HUD setting: 0 off, 1 small, 2 medium (the default), 3 large
  panel:DEG         the panel turned DEG degrees left of the head (negative: right), as the XR
                    worker's lazy follow leaves it while it glides after a turn

Example, in free roam: ahead yaw:40 turned yaw:0 pitch:-25 down pitch:0 turn:90 turning turn:0

The HUD should sit in the same spot of each image whatever the head does (with panel:0). SpidyHudData's
counters (game placements, panels placed in front of the head, rejected frames, frames and eye draws that
left the panel out while the HUD is off) go in the report.

Start the game with tools/probe_menu_pad.py start, load the save (pad a --until-player), keep the game
window in front, and run this once per game process.
"""
import argparse
import ctypes as c
import json
import math
import pathlib
import struct
import sys
import time
from bridge_game import ROOT, prepare
from capture_game_state import Game, find_game, open_process, close
from observe_game import call_remote, call_with_payload
from probe_game_screen import game_window, user32, client_size, window_rgb
from probe_stereo import snapshot as stereo_snapshot
from probe_stereo_gpu import MAX_EYE_SIZE, discover_queue, snapshot as gpu_snapshot
from run_game_vr import hud_snapshot, rgb_rows, write_rgb_png

HEAD = 1.6        # eye height above the feet, metres
HALF_IPD = .032
# Quest 3 through Virtual Desktop, as tools/probe_vr_load.py uses it.
OUTER, INNER, VERTICAL = math.radians(54), math.radians(44), math.radians(48)


def head_eyes(feet, heading, pitch):
    """Both eye poses (rows right, down, forward, position) and lenses (left, right, down, up)."""
    hx, hz = heading
    cp, sp = math.cos(pitch), math.sin(pitch)
    forward = (cp*hx, sp, cp*hz)
    right = (-hz, 0., hx)
    down = (forward[1]*right[2]-forward[2]*right[1], forward[2]*right[0]-forward[0]*right[2],
            forward[0]*right[1]-forward[1]*right[0])
    centre = (feet[0], feet[1]+HEAD, feet[2])
    eyes = []
    for side, lens in ((-HALF_IPD, (-OUTER, INNER)), (HALF_IPD, (-INNER, OUTER))):
        position = tuple(p+r*side for p, r in zip(centre, right))
        eyes.append(((*right, 0, *down, 0, *forward, 0, *position, 1), (*lens, -VERTICAL, VERTICAL)))
    return eyes


def eye_command(serial, eyes, feet, mode=1):
    """native_eyes::Command v2, anchored to the player's position (mode 2: the flat screen)."""
    raw = struct.pack('<4IQ2I', 0x53455043, 2, 208, mode, serial, 250, 0)
    for matrix, fov in eyes:
        raw += struct.pack('<20f', *matrix, *fov)
    return raw+struct.pack('<3fI', *feet, 1)


def hud_setting(size, panel_degrees):
    """SpidyHudSet: the HUD row, and the panel turned about the head's up axis (an OpenXR quaternion)."""
    half = math.radians(panel_degrees)/2
    return struct.pack('<4I4f', 0x53554853, 1, 32, size, 0, math.sin(half), 0, math.cos(half))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('steps', nargs='+')
    p.add_argument('--width', type=int, default=1536, help='eye width in pixels (headset: 3072)')
    p.add_argument('--height', type=int, default=1632, help='eye height in pixels (headset: 3264)')
    p.add_argument('--hold', type=float, default=2.0, help='seconds the head holds still before a capture')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/hud-probe.json')
    a = p.parse_args()
    for step in a.steps:
        kind, _, value = step.partition(':')
        if kind in ('yaw', 'pitch', 'turn', 'flat', 'panel'):
            float(value)
        elif kind == 'hud' and value not in ('0', '1', '2', '3'):
            p.error(f'Unsupported step {step}')
        elif kind == 'screen' and not value.replace('-', '').replace('_', '').isalnum():
            p.error(f'Unsupported step {step}')
        elif kind != 'screen' and not kind.replace('-', '').replace('_', '').isalnum():
            p.error(f'Unsupported step {step}')
    if not all(64 <= x <= MAX_EYE_SIZE for x in (a.width, a.height)):
        p.error(f'Eye sizes are 64..{MAX_EYE_SIZE}')
    try:
        user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    except AttributeError:
        user32.SetProcessDPIAware()
    game = Game(find_game())
    process = None
    stops = []
    result = dict(width=a.width, height=a.height, steps=a.steps, captures={})
    folder = a.output.with_name(a.output.stem+'-eyes')
    folder.mkdir(parents=True, exist_ok=True)
    try:
        window = game_window(game.pid)
        if user32.GetForegroundWindow() != window:
            raise RuntimeError('The game window is not in front, so the game is paused. Bring it to the front.')
        objects = game.registered_candidates()
        heroes = [o for o in objects if o['kind'] == 'hero_local']
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required. Load a save first.')
        feet = tuple(heroes[0]['player']['position'])
        cameras = [o for o in objects if o['kind'] == 'hero_camera_manager' and 'camera' in o]
        base_yaw = math.atan2(1., 0.)
        if cameras:
            camera = cameras[0]['camera']['position']
            dx, dz = feet[0]-camera[0], feet[2]-camera[2]
            if math.hypot(dx, dz) > .1:
                base_yaw = math.atan2(dz, dx)
        result.update(feet=feet, base_heading_deg=round(math.degrees(base_yaw), 2))
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        queue = discover_queue(game, process)
        dll, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
                              ROOT/'reports/stereo-modules',
                              ('SpidyStart', 'SpidyStop', 'SpidyStereoData', 'SpidySetEyes', 'SpidyStereoFrames',
                               'SpidyGpuStart', 'SpidyGpuStop', 'SpidyGpuData', 'SpidyGpuFreeze', 'SpidyHudData',
                               'SpidyHudSet'))
        result['dll_sha256'] = digest
        # Capture mode: every eye pair is read back, so a freeze can save it.
        code = call_with_payload(process, dll['SpidyGpuStart'],
                                 struct.pack('<4I2Q4I', 0x53475043, 3, 48, game.pid, game.base, queue, 0, 0, 1, 0))
        if code:
            raise RuntimeError(f'GPU bridge start: {code}')
        stops.append(('gpu', dll['SpidyGpuStop']))
        code = call_with_payload(process, dll['SpidyStart'],
                                 struct.pack('<4IQ4I', 0x53534346, 1, 40, game.pid, game.base, 0, 29, a.width,
                                             a.height))
        if code:
            raise RuntimeError(f'Eye views start: {code}')
        stops.insert(0, ('eyes', dll['SpidyStop']))
        serial = time.time_ns()//1000  # above any earlier run's in this game process
        yaw = pitch = rate = panel = 0.
        mode, size = 1, 2
        code = call_with_payload(process, dll['SpidyHudSet'], hud_setting(size, panel))
        if code:
            raise RuntimeError(f'HUD setting: {code}')
        clock = time.perf_counter()

        def drive(seconds):
            """Send head commands for `seconds`, turning at `rate` degrees a second."""
            nonlocal serial, yaw, clock
            end = time.perf_counter()+seconds
            while True:
                now = time.perf_counter()
                yaw += rate*(now-clock)
                clock = now
                angle = base_yaw+math.radians(yaw)
                serial += 1
                code = call_with_payload(process, dll['SpidySetEyes'],
                                         eye_command(serial, head_eyes(feet, (math.cos(angle), math.sin(angle)),
                                                                       math.radians(pitch)), feet, mode))
                if code and code != 4003:
                    raise RuntimeError(f'Eye command: {code}')
                if user32.GetForegroundWindow() != window:
                    raise RuntimeError('Another window came to the front (the game pauses). Stopped.')
                if now >= end:
                    return
                time.sleep(.008)

        drive(4)  # views are created and the city streams around them
        views = stereo_snapshot(game, dll['SpidyStereoData'])
        result['eye_views'] = [{k: e[k] for k in ('width', 'height', 'occlusion')} for e in views['eyes']] \
            if views else None
        for step in a.steps:
            kind, _, value = step.partition(':')
            if kind == 'yaw':
                yaw = float(value)
                continue
            if kind == 'pitch':
                pitch = float(value)
                continue
            if kind == 'turn':
                rate = float(value)
                continue
            if kind == 'flat':
                mode = 2 if float(value) else 1
                continue
            if kind in ('hud', 'panel'):
                if kind == 'hud':
                    size = int(value)
                else:
                    panel = float(value)
                code = call_with_payload(process, dll['SpidyHudSet'], hud_setting(size, panel))
                if code:
                    raise RuntimeError(f'HUD setting: {code}')
                continue
            if kind == 'screen':
                # The eye views' lease (250 ms) lapses: the game's view shows its HUD as the game screen would.
                time.sleep(1.2)
                width, height = client_size(window)
                path = folder/f'{value}-window.png'
                write_rgb_png(path, width, height, window_rgb(window, width, height))
                result['captures'][step] = dict(window=str(path), hud=hud_snapshot(game, dll['SpidyHudData']))
                print(step, json.dumps(result['captures'][step]), flush=True)
                clock = time.perf_counter()
                drive(2)
                continue
            drive(a.hold if rate == 0 else .6)
            code = call_remote(process, dll['SpidyGpuFreeze'])
            state = gpu_snapshot(game, dll['SpidyGpuData'])
            if code or not state or state['width'] != a.width or state['height'] != a.height:
                raise RuntimeError(f'Eye capture: {code} {state}')
            files = {}
            for side in ('left', 'right'):
                pixels = game.read(state[f'{side}_pixels'], a.width*a.height*4)
                columns, rows, rgb = rgb_rows(pixels, a.width*4, 0, 0, a.width, a.height, 2)
                path = folder/f'{step}-{side}.png'
                write_rgb_png(path, columns, rows, rgb)
                files[side] = str(path)
            capture = dict(yaw=round(yaw, 2), pitch=pitch, turning=rate, flat=mode == 2, size=size, panel=panel,
                           serial=state['captured_serial'], files=files, hud=hud_snapshot(game, dll['SpidyHudData']))
            result['captures'][step] = capture
            print(step, json.dumps(capture), flush=True)
            drive(.2)
        result['hud'] = hud_snapshot(game, dll['SpidyHudData'])
        # Stopping VR puts the game's own HUD back (texture size, second movie in its view).
        while stops:
            label, export = stops.pop(0)
            result[f'{label}_stop'] = call_remote(process, export)
        time.sleep(1)
        result['hud_after_stop'] = hud_snapshot(game, dll['SpidyHudData'])
        print('after stop', json.dumps(result['hud_after_stop']), flush=True)
    finally:
        while stops:
            label, export = stops.pop(0)
            try:
                result[f'{label}_stop'] = call_remote(process, export)
            except OSError:
                pass
        if process:
            close(process)
        game.close()
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=2)+'\n')
        print(f'Report: {a.output.resolve()}', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'HUD probe failed: {error}', file=sys.stderr)
        sys.exit(2)
