"""Check, in the running game and without a headset, that VR's two eyes are exposed alike.

The game adapts exposure (how bright the image is drawn) on the GPU from what a view shows, one view at a
time. Until October 10's sixth build each eye had its own, so the eye with more sky in its lens was
drawn darker than the other one, and they swapped when the head turned round. Now both eyes take
the exposure of the game's own view (stereo_probe.cpp, shareExposure), which is on the head and covers
both lenses. This probe drives the eye views as a VR session does (views 29) from the player's position
and at each head pose saves both eyes twice, each exposed on its own (the old way, SpidyEyeExposure(1))
and shared:

  NAME              hold the head still, then measure and save both eyes both ways (NAME-own-left.png ...)
  yaw:DEG           turn the head this far from the game camera's heading (0 at start)
  pitch:DEG         look up (positive) or down
  roll:DEG          tilt the head to its right (90: the right eye straight below the left, as when you
                    stand on a wall and look along it)

Example, in free roam: level roll:90 right-down roll:-90 left-down roll:0 yaw:90 side

For each NAME the report has:

- measured: the adapted luminance each eye works out for itself and the game view's (the eyes go on
  measuring theirs). brightness_own is the game view's over the eye's: how bright that eye was drawn
  before, next to how it is drawn now (1.25 = a quarter brighter).
- images: how bright the part of the scene that both eyes see is in the left eye's image over the right
  eye's, each on its own and shared (linear light; the median over a grid of blocks). 1.00 = alike.

The check passes when the shared images are alike within --tolerance at every pose. `reproduced` says
whether any pose showed the old difference (it needs a view that is brighter on one side of the head).

Start the game with tools/probe_menu_pad.py start, load the save (pad a --until-player), keep the game
window in front, and run this once per game process.
"""
import argparse
import ctypes as c
import json
import math
import pathlib
import statistics
import struct
import sys
import time
from bridge_game import ROOT, prepare
from capture_game_state import Game, find_game, open_process, close
from observe_game import call_remote, call_with_payload
from probe_game_screen import game_window, user32
from probe_hud import HEAD, HALF_IPD, OUTER, INNER, VERTICAL, eye_command, hud_setting
from probe_stereo import snapshot as stereo_snapshot
from probe_stereo_gpu import MAX_EYE_SIZE, discover_queue, snapshot as gpu_snapshot
from run_game_vr import appearance_snapshot, rgb_rows, write_rgb_png

GRID = 24  # blocks across and down the part both eyes see


def head_eyes(feet, heading, pitch, roll):
    """Both eye poses (rows right, down, forward, position) and lenses (left, right, down, up)."""
    hx, hz = heading
    cp, sp = math.cos(pitch), math.sin(pitch)
    forward = (cp*hx, sp, cp*hz)
    level = (-hz, 0., hx)
    below = (forward[1]*level[2]-forward[2]*level[1], forward[2]*level[0]-forward[0]*level[2],
             forward[0]*level[1]-forward[1]*level[0])
    cr, sr = math.cos(roll), math.sin(roll)
    right = tuple(a*cr+b*sr for a, b in zip(level, below))
    down = tuple(b*cr-a*sr for a, b in zip(level, below))
    centre = (feet[0], feet[1]+HEAD, feet[2])
    eyes = []
    for side, lens in ((-HALF_IPD, (-OUTER, INNER)), (HALF_IPD, (-INNER, OUTER))):
        position = tuple(p+r*side for p, r in zip(centre, right))
        eyes.append(((*right, 0, *down, 0, *forward, 0, *position, 1), (*lens, -VERTICAL, VERTICAL)))
    return eyes


def shared_columns(width):
    """How many image columns both eyes see: the left eye's last ones are the right eye's first ones."""
    return int(width*2*math.tan(INNER)/(math.tan(INNER)+math.tan(OUTER)))


def block_light(pixels, width, height, left, columns):
    """Mean linear light of each block of a GRID x GRID grid over `columns` columns from `left` (RGBA rows)."""
    lut = [((v/255+.055)/1.055)**2.4 if v > 10 else v/255/12.92 for v in range(256)]
    step = max(1, min(columns, height)//(GRID*12))  # about a dozen samples across a block
    sums = [[0.]*GRID for _ in range(GRID)]
    counts = [[0]*GRID for _ in range(GRID)]
    for y in range(0, height, step):
        row = pixels[(y*width+left)*4:(y*width+left+columns)*4]
        by = min(GRID-1, y*GRID//height)
        reds, greens, blues = row[0::4*step], row[1::4*step], row[2::4*step]
        for i, (r, g, b) in enumerate(zip(reds, greens, blues)):
            bx = min(GRID-1, i*step*GRID//columns)
            sums[by][bx] += .2126*lut[r]+.7152*lut[g]+.0722*lut[b]
            counts[by][bx] += 1
    return [sums[y][x]/counts[y][x] for y in range(GRID) for x in range(GRID) if counts[y][x]]


def compare_light(left, right):
    """Left over right for the same blocks of two eyes: the median block ratio and the ratio of the totals.

    Blocks that are nearly black or white in either eye say nothing about exposure and are left out.
    """
    ratios = [a/b for a, b in zip(left, right) if .004 < a < .9 and .004 < b < .9]
    spread = statistics.quantiles(ratios, n=10) if len(ratios) >= 10 else []
    return dict(blocks=len(ratios), median=round(statistics.median(ratios), 4) if ratios else None,
                total=round(sum(left)/sum(right), 4) if sum(right) > 0 else None,
                # A tenth of the blocks are below the first and a tenth above the second.
                tenths=[round(spread[0], 4), round(spread[-1], 4)] if spread else None)


def assess(captures, tolerance):
    """Shared exposure must leave the eyes alike at every pose; `reproduced`: a pose where they were not before.

    shared_jobs counts the eye images that took the game view's exposure while each mode was on: none on
    their own (a frame or two around the switch aside), every one when shared.
    """
    shared = [x['images']['shared']['median'] for x in captures.values()]
    own = [x['images']['own']['median'] for x in captures.values()]
    measured = all(x is not None for x in shared+own) and bool(shared)
    switched = bool(captures) and all(x['shared_jobs']['own'] <= 4 and x['shared_jobs']['shared'] >= 20
                                      for x in captures.values())
    alike = measured and all(abs(math.log(x)) <= tolerance for x in shared)
    reproduced = measured and any(abs(math.log(x)) > 2*tolerance for x in own)
    return dict(measured=measured, switched=switched, shared_alike=alike, reproduced=reproduced,
                passed=alike and switched)


def view_light(game, view):
    """A view's processor copy of its luminance: in use (adapted, measured) and its own newest reading."""
    raw = game.read(view+0x1708, 32)
    if len(raw) != 32:
        return None
    adapted, measured = struct.unpack_from('<2f', raw)
    own_adapted, own_measured = struct.unpack_from('<2f', raw, 0x18)
    return dict(adapted=adapted, measured=measured, own_adapted=own_adapted, own_measured=own_measured)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('steps', nargs='+')
    p.add_argument('--width', type=int, default=1536, help='eye width in pixels (headset: 3072)')
    p.add_argument('--height', type=int, default=1632, help='eye height in pixels (headset: 3264)')
    p.add_argument('--hold', type=float, default=4.0, help='seconds the head holds still before measuring')
    p.add_argument('--tolerance', type=float, default=.03, help='largest left/right difference that passes')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/exposure-probe.json')
    a = p.parse_args()
    for step in a.steps:
        kind, _, value = step.partition(':')
        if kind in ('yaw', 'pitch', 'roll'):
            float(value)
        elif value or not kind.replace('-', '').replace('_', '').isalnum():
            p.error(f'Unsupported step {step}')
    if not all(64 <= x <= MAX_EYE_SIZE for x in (a.width, a.height)) or not 1 <= a.hold <= 30:
        p.error(f'Eye sizes are 64..{MAX_EYE_SIZE}, holds 1..30 seconds')
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
                              ('SpidyStart', 'SpidyStop', 'SpidyStereoData', 'SpidySetEyes', 'SpidyAppearanceData',
                               'SpidyGpuStart', 'SpidyGpuStop', 'SpidyGpuData', 'SpidyGpuFreeze', 'SpidyHudSet',
                               'SpidyEyeExposure'))
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
        # The HUD's panel hangs 2 m ahead, at another place in each eye's image: left out of the images.
        code = call_with_payload(process, dll['SpidyHudSet'], hud_setting(0, 0))
        if code:
            raise RuntimeError(f'HUD setting: {code}')
        serial = time.time_ns()//1000  # above any earlier run's in this game process
        yaw = pitch = roll = 0.

        def drive(seconds):
            """Send the head's pose for `seconds`."""
            nonlocal serial
            end = time.perf_counter()+seconds
            angle = base_yaw+math.radians(yaw)
            eyes = head_eyes(feet, (math.cos(angle), math.sin(angle)), math.radians(pitch), math.radians(roll))
            while True:
                serial += 1
                code = call_with_payload(process, dll['SpidySetEyes'], eye_command(serial, eyes, feet))
                if code and code != 4003:
                    raise RuntimeError(f'Eye command: {code}')
                if user32.GetForegroundWindow() != window:
                    raise RuntimeError('Another window came to the front (the game pauses). Stopped.')
                if time.perf_counter() >= end:
                    return
                time.sleep(.008)

        def shared_jobs():
            state = appearance_snapshot(game, dll['SpidyAppearanceData'])
            if not state:
                raise RuntimeError('Appearance telemetry unavailable')
            return state['exposure_shared']

        def grab():
            """Both eyes' images as they are now (RGBA rows)."""
            code = call_remote(process, dll['SpidyGpuFreeze'])
            state = gpu_snapshot(game, dll['SpidyGpuData'])
            if code or not state or state['width'] != a.width or state['height'] != a.height:
                raise RuntimeError(f'Eye capture: {code} {state}')
            images = [game.read(state[f'{side}_pixels'], a.width*a.height*4) for side in ('left', 'right')]
            if any(len(pixels) != a.width*a.height*4 for pixels in images):
                raise RuntimeError('Eye image unreadable')
            return images

        def compare(step, mode, images):
            """The light of the part both eyes see, left over right, and the saved images."""
            columns = shared_columns(a.width)
            light, files = [], {}
            for side, left, pixels in zip(('left', 'right'), (a.width-columns, 0), images):
                light.append(block_light(pixels, a.width, a.height, left, columns))
                across, down, rgb = rgb_rows(pixels, a.width*4, 0, 0, a.width, a.height, 2)
                path = folder/f'{step}-{mode}-{side}.png'
                write_rgb_png(path, across, down, rgb)
                files[side] = str(path)
            return dict(compare_light(*light), files=files)

        drive(4)  # views are created and the city streams around them
        views = stereo_snapshot(game, dll['SpidyStereoData'])
        if not views or not views['primary'] or not all(e['view'] for e in views['eyes']):
            raise RuntimeError('The eye views were not created')
        result['eye_views'] = [{k: e[k] for k in ('width', 'height', 'occlusion')} for e in views['eyes']]
        for step in a.steps:
            kind, _, value = step.partition(':')
            if kind == 'yaw':
                yaw = float(value)
                continue
            if kind == 'pitch':
                pitch = float(value)
                continue
            if kind == 'roll':
                roll = float(value)
                continue
            # Each eye on its own, as before. Its own buffer has adapted all along, so this is its steady state.
            if call_remote(process, dll['SpidyEyeExposure'], 1):
                raise RuntimeError('Exposure switch rejected')
            before = shared_jobs()
            drive(a.hold)
            own_jobs = shared_jobs()-before
            lights = [view_light(game, v) for v in (views['primary'], *(e['view'] for e in views['eyes']))]
            if not all(lights) or not all(x['own_adapted'] > 0 for x in lights):
                raise RuntimeError(f'Luminance unreadable: {lights}')
            own_images = grab()
            if call_remote(process, dll['SpidyEyeExposure'], 0):
                raise RuntimeError('Exposure switch rejected')
            before = shared_jobs()
            drive(1)
            jobs = shared_jobs()-before
            shared_images = grab()
            # Compared after both grabs: the head stays driven between them.
            own, shared = compare(step, 'own', own_images), compare(step, 'shared', shared_images)
            game_view, left, right = lights
            measured = dict(game_view=round(game_view['own_adapted'], 6), left=round(left['own_adapted'], 6),
                            right=round(right['own_adapted'], 6),
                            brightness_own=[round(game_view['own_adapted']/x['own_adapted'], 4)
                                            for x in (left, right)],
                            in_use=[round(x['adapted'], 6) for x in lights])
            result['captures'][step] = dict(yaw=yaw, pitch=pitch, roll=roll, measured=measured,
                                            images=dict(own=own, shared=shared),
                                            shared_jobs=dict(own=own_jobs, shared=jobs))
            print(step, json.dumps(dict(measured=measured, own=own['median'], shared=shared['median'],
                                        shared_jobs=[own_jobs, jobs])), flush=True)
        while stops:
            label, export = stops.pop(0)
            result[f'{label}_stop'] = call_remote(process, export)
        result['assessment'] = assess(result['captures'], a.tolerance)
    finally:
        while stops:
            label, export = stops.pop(0)
            try:
                result[f'{label}_stop'] = call_remote(process, export)
            except OSError:
                pass
        if process:
            try:
                call_remote(process, dll['SpidyEyeExposure'], 0)
            except (OSError, NameError):
                pass
            close(process)
        game.close()
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=2)+'\n')
        print(f'Report: {a.output.resolve()}', flush=True)
    passed = bool(result.get('assessment', {}).get('passed'))
    print(json.dumps(result.get('assessment')), flush=True)
    print(f"Exposure check {'passed' if passed else 'failed'}")
    return 0 if passed else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Exposure probe failed: {error}', file=sys.stderr)
        sys.exit(2)
