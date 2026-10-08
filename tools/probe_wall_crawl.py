"""The player stuck to a wall in the running game, without a headset: how the game holds it there, and where the
VR eyes go.

    python tools/probe_wall_crawl.py

A player who flies into a wall sticks to it (the game's wall crawl, HeroStateWallCrawl*). In VR that looked like
clipping through the wall. The probe finds the nearest wall around the player with world rays, jumps with the
virtual Xbox controller, shoots a web at the wall and reels in until the game holds the player on it, lets go and
watches. Throughout it reads every native step of the player's mover, the player's actor (feet, up, forward) and
its state machine's states. On the wall it measures with rays where the wall is from the feet along the actor's
up, and how far from it the eyes are: placed upright from the feet, as before, and stood off it as GameTrackingRig
does now. Then it jumps off and watches the actor come upright. The game window is saved on the wall and after
the jump.

It needs a freshly started game in free roam whose save was loaded with the virtual controller
(`tools/probe_menu_pad.py start`, then `pad a --until-player`), the player perched or standing with a building
within 50 m. It writes reports/wall-crawl.json and reports/wall-crawl/*.png.
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
from inspect_game import PE
from observe_game import call_remote, call_with_payload, modules
from probe_air_handoff import AIR_ENTRIES, swing_command
from probe_game_body import SYNC_MACHINE, state_names
from probe_game_grab import ENTRIES, HERO_LOCAL, HERO_MOVERS, registry, resolve
from probe_game_screen import game_window, client_size, window_rgb
from probe_game_swing import snapshot as swing_snapshot
from probe_menu_pad import BUTTONS, XR_DLL, XR_EXPORTS, pad
from probe_native_motion import snapshot as motion_snapshot
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from run_game_vr import write_rgb_png
from vr_launcher import bring_to_front

OUTPUT = ROOT/'reports/wall-crawl'
# GameTrackingRig (include/spidy/game_tracking.hpp): how far the eyes stand off a wall the game holds the player
# on, and when the actor counts as on one; the eye height of a player standing at the middle of the play space.
WALL_CLEARANCE, SURFACE_ENTER_COS, EYE_HEIGHT = .5, .7071068, 1.65


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def scale(a, s): return tuple(x*s for x in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def length(a): return math.sqrt(dot(a, a))


def norm(a):
    n = length(a)
    return scale(a, 1/n) if n > 1e-6 else (0., 0., 0.)


def actor(game, record):
    """The player's actor: its feet and its model axes in the world (the rows of its transform)."""
    raw = game.read(game.pointer(record), 64)
    if len(raw) != 64:
        return None
    m = struct.unpack('<16f', raw)
    if not all(math.isfinite(v) for v in m):
        return None
    return dict(feet=m[12:15], right=m[0:3], up=m[4:7], forward=m[8:11])


def tilt(up):
    """Degrees between the actor's up and the world's."""
    n = norm(up)
    return round(math.degrees(math.acos(max(-1., min(1., n[1])))), 2) if length(n) > .5 else None


def placed_eyes(feet, up):
    """The eyes of a player standing at the middle of the play space: placed upright from the feet (the build
    before this one), and stood off the surface along the actor's up as GameTrackingRig does now."""
    n = norm(up)
    before = add(feet, (0., EYE_HEIGHT, 0.))
    depth = max(0., WALL_CLEARANCE-dot(sub(before, feet), n)) if n[1] < SURFACE_ENTER_COS else 0.
    return before, add(before, scale(n, depth)), depth


def window_shot(game, name):
    hwnd = game_window(game.pid)
    width, height = client_size(hwnd)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = OUTPUT/f'{name}.png'
    write_rgb_png(path, width, height, window_rgb(hwnd, width, height))
    return str(path)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/wall-crawl.json')
    p.add_argument('--reel-seconds', type=float, default=4, help='longest reel toward the wall')
    p.add_argument('--wall-seconds', type=float, default=3, help='how long to watch the player on the wall')
    a = p.parse_args()
    game = Game(find_game())
    process = None
    rays_active = swing_active = False
    report = dict(events=[], samples=[], measurements=[], shots={})
    samples = report['samples']
    try:
        pe = PE(game.path.read_bytes())
        if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in (*ENTRIES, *AIR_ENTRIES)):
            raise RuntimeError('A hook entry is already patched: start the game afresh')
        bring_to_front(game.pid)
        components = list(registry(game))
        heroes = [(a_, r) for a_, v, r, _ in components if v == HERO_LOCAL]
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required: load free roam first')
        record = heroes[0][1]
        managers = [a_ for a_, v, r, _ in components if v == HERO_MOVERS and r == record]
        mover = resolve(game, struct.unpack('<I', game.read(managers[0]+0xdb4, 4))[0])
        machine = next((a_ for a_, v, r, _ in components if v == SYNC_MACHINE and r == record), 0)
        report['state_machine'] = hex(machine) if machine else None
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        xr, _ = prepare(game.pid, process, XR_DLL, ROOT/'reports/stereo-modules', XR_EXPORTS)
        motion, report['motion_hash'] = prepare(
            game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll', ROOT/'reports/motion-modules',
            ('SpidyMotionStart', 'SpidyMotionStop', 'SpidyMotionData'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays, report['ray_hash'] = prepare(
            game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll', ROOT/'reports/ray-modules',
            ('SpidyRayStart', 'SpidyRayStop', 'SpidyRaySubmit', 'SpidyRayData', 'SpidySwingStart',
             'SpidySwingSubmit', 'SpidySwingStop', 'SpidySwingData'))

        def invoke(address, payload, what):
            code = call_with_payload(process, address, payload)
            if code:
                raise RuntimeError(f'{what}: {code}')
        invoke(rays['SpidyRayStart'], struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 30000, 0x410),
               'World rays start')
        rays_active = True
        # As a VR session starts it: 32 m/s, the swing's gravity 6 m/s^2; no grabbing.
        invoke(rays['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record,
                                                    mover, motion_module, 30000, 32., 6., 0), 'Native swing start')
        swing_active = True
        started = time.monotonic()
        serial = ray_serial = 0
        target = None

        def sample():
            """The player's actor, mover and states now; kept when something changed."""
            elapsed = time.monotonic()-started
            body = actor(game, record)
            m = motion_snapshot(game, motion['SpidyMotionData'])
            s = swing_snapshot(game, rays['SpidySwingData'])
            if s and s['error']:
                raise RuntimeError(f"Native swing error {s['error']}")
            entry = dict(seconds=round(elapsed, 4))
            if body:
                entry.update(feet=[round(v, 3) for v in body['feet']], up=[round(v, 4) for v in body['up']],
                             forward=[round(v, 4) for v in body['forward']], tilt=tilt(body['up']))
            if m:
                entry.update(steps=m['steps'], controlled=m['controlled'], contact=m['contact'],
                             mover_flags=hex(m['mover_flags']), collision_flags=hex(m['collision_flags']),
                             velocity=[round(v, 2) for v in m['velocity']])
            if s:
                entry.update(owned=s['owned'], web=any(w['attached'] for w in s['webs']))
            entry['states'] = state_names(game, pe, machine) if machine else None
            last = samples[-1] if samples else None
            if not last or elapsed-last['seconds'] >= .02 or entry.get('states') != last.get('states') or \
                    entry.get('steps') != last.get('steps'):
                samples.append(entry)
            return entry

        def submit(grip, trigger, focused=True):
            """The left hand at the player's chest, aimed at the target, for the native swing."""
            nonlocal serial
            body = actor(game, record)
            if not body:
                return
            hand = add(body['feet'], (0., 1.2, 0.))
            aim = norm(sub(target, hand)) if target else (0., 1., 0.)
            serial += 1
            invoke(rays['SpidySwingSubmit'], swing_command(serial, hand, aim, grip, trigger, focused), 'Swing input')

        def run(seconds, grip=None, trigger=0., until=None):
            """Sample (and with a grip, hold the hand) for a while, or until `until(sample)` holds."""
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                entry = sample()
                if grip is not None:
                    submit(grip, trigger)
                if until and until(entry):
                    return entry
                time.sleep(.004)
            return None

        def mark(name):
            report['events'].append(dict(name=name, seconds=round(time.monotonic()-started, 3)))
            print(f"{report['events'][-1]['seconds']:6.2f} s  {name}", flush=True)

        def cast(batch):
            """World rays [(origin, direction, distance, tag)], at most eight; their hits."""
            nonlocal ray_serial
            ray_serial += 1
            invoke(rays['SpidyRaySubmit'], ray_command(ray_serial, batch, 250), 'World rays')
            deadline = time.monotonic()+2
            while time.monotonic() < deadline:
                result = ray_snapshot(game, rays['SpidyRayData'])
                if result and result['serial'] == ray_serial and result['status'] == 2:
                    if result['error']:
                        raise RuntimeError(f"World rays error {result['error']}")
                    return result['hits']
                sample()
                time.sleep(.004)
            raise RuntimeError('The world rays did not answer')

        run(.3, 0.)
        start = actor(game, record)
        if not start:
            raise RuntimeError("The player's actor could not be read")
        report['start'] = dict(feet=start['feet'], up=start['up'], tilt=tilt(start['up']))
        # The nearest wall around the hand: 16 headings, level and 12 degrees up and down.
        hand = add(start['feet'], (0., 1.2, 0.))
        walls = []
        for elevation in (0., 12., -12.):
            for half in (0, 1):
                batch = []
                for i in range(8):
                    heading = math.radians((2*i+half)*22.5)
                    e = math.radians(elevation)
                    batch.append((hand, (math.cos(e)*math.cos(heading), math.sin(e), math.cos(e)*math.sin(heading)),
                                  60., i))
                for (_, direction, distance, _), hit in zip(batch, cast(batch)):
                    reach = hit['fraction']*distance
                    if hit['count'] and abs(hit['normal'][1]) < .35 and dot(hit['normal'], direction) < -.3 and \
                            6 <= reach <= 50:
                        walls.append(dict(distance=round(reach, 2), elevation=elevation, point=hit['position'],
                                          normal=hit['normal'], direction=direction))
        if not walls:
            raise RuntimeError('No wall within 6-50 m of the player: move the save beside a building')
        wall = min(walls, key=lambda w: (w['elevation'] < 0, w['distance']))
        report['wall'] = wall
        report['walls_found'] = len(walls)
        target = wall['point']
        print(f"wall {wall['distance']} m away at {wall['elevation']} degrees, normal "
              f"{[round(v, 2) for v in wall['normal']]}", flush=True)

        def on_surface(entry):
            names = ' '.join(n for n in entry.get('states') or [] if n)
            return (entry.get('tilt') or 0) > 45 or 'WallCrawl' in names or 'WallRun' in names

        mark('jump (virtual controller A)')
        pad(process, xr, BUTTONS['a'], 150)
        run(.1, 0.)
        mark('web shot at the wall, grip held')
        run(.35, 1.)
        mark('reeling into the wall')
        held = run(a.reel_seconds, 1., 1., until=on_surface)
        if not held:
            mark('no wall hold while reeling: grip released, falling')
            held = run(2.5, 0., until=on_surface)
        report['held'] = bool(held)
        mark('on the wall: hand let go' if held else 'never held on a wall')
        run(.3, 0.)
        if held:
            run(.7)
            body = actor(game, record)
            n = norm(body['up'])
            before, after, depth = placed_eyes(body['feet'], body['up'])
            # Along the actor's up, from a metre out: where the wall is under the feet and under each eye
            # (negative: behind its surface); and straight at it, level, from each eye.
            level = norm((-n[0], 0., -n[2])) if math.hypot(n[0], n[2]) > .2 else scale(n, -1)
            batch = [(add(body['feet'], n), scale(n, -1), 3., 0),
                     (add(before, n), scale(n, -1), 3., 1),
                     (add(after, n), scale(n, -1), 3., 2),
                     (before, level, 2., 3),
                     (after, level, 2., 4)]
            hits = cast(batch)
            ahead = [round(h['fraction']*r[2], 3) if h['count'] else None for r, h in zip(batch, hits)]
            report['measurements'].append(dict(
                seconds=round(time.monotonic()-started, 3), feet=body['feet'], up=body['up'], tilt=tilt(body['up']),
                stand_off=round(depth, 3), eyes_before=before, eyes_after=after,
                feet_over_wall=None if ahead[0] is None else round(ahead[0]-1, 3),
                eyes_before_over_wall=None if ahead[1] is None else round(ahead[1]-1, 3),
                eyes_after_over_wall=None if ahead[2] is None else round(ahead[2]-1, 3),
                wall_ahead_of_eyes_before=ahead[3], wall_ahead_of_eyes_after=ahead[4],
                normals=[h['normal'] if h['count'] else None for h in hits]))
            print('on the wall:', json.dumps({k: v for k, v in report['measurements'][-1].items()
                                              if k not in ('normals', 'eyes_before', 'eyes_after')}), flush=True)
            report['shots']['on_wall'] = window_shot(game, 'on-wall')
            run(max(0., a.wall_seconds-1.))
            mark('jump off the wall (virtual controller A)')
            pad(process, xr, BUTTONS['a'], 150)
            run(.5)
            report['shots']['after_jump'] = window_shot(game, 'after-jump')
            run(2.)
        else:
            report['shots']['no_hold'] = window_shot(game, 'no-hold')
    finally:
        if swing_active:
            report['swing_stop'] = call_remote(process, rays['SpidySwingStop'])
        if rays_active:
            report['ray_stop'] = call_remote(process, rays['SpidyRayStop'])
        if process:
            close(process)
        try:
            pe = PE(game.path.read_bytes())
            report['entries_restored'] = all(game.read(game.base+rva, 16) == pe.bytes(rva, 16)
                                             for rva in (*ENTRIES, *AIR_ENTRIES))
        except Exception as error:  # the game may have closed
            report['entries_restored'] = str(error)
        report['summary'] = summarize(report)
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(report, indent=1)+'\n')
    print(json.dumps(report['summary'], indent=1))
    measured = report['measurements'][-1] if report['measurements'] else {}
    ok = report.get('held') and measured.get('eyes_after_over_wall') is not None and \
        measured['eyes_after_over_wall'] >= WALL_CLEARANCE-.15 and report.get('entries_restored') is True and \
        not report.get('swing_stop') and not report.get('ray_stop')
    return 0 if ok else 1


def summarize(report):
    """The stretches the actor was tilted, what held it, and how far it tilted elsewhere."""
    samples = [s for s in report['samples'] if s.get('tilt') is not None]
    tilted = [s for s in samples if s['tilt'] > 45]
    events = {e['name']: e['seconds'] for e in report['events']}
    off = next((t for name, t in events.items() if name.startswith('jump off')), None)
    states = {}
    for s in tilted:
        for name in s.get('states') or []:
            if name:
                states[name] = states.get(name, 0)+1
    turn = None
    if tilted:
        first = tilted[0]['seconds']
        leaving = [s for s in samples if s['seconds'] < first and s['tilt'] < 5]
        full = next((s for s in samples if s['seconds'] >= first and s['tilt'] > 80), None)
        if leaving and full:
            turn = round(full['seconds']-leaving[-1]['seconds'], 3)
    upright_after = None
    if off is not None:
        upright_after = next((round(s['seconds']-off, 3) for s in samples if s['seconds'] > off and s['tilt'] < 10),
                             None)
    airborne = [s for s in samples if s.get('contact') == 2]
    return dict(
        held=report.get('held'), wall=report.get('wall'),
        tilted_seconds=round(tilted[-1]['seconds']-tilted[0]['seconds'], 3) if tilted else 0,
        tilt_on_wall=[min(s['tilt'] for s in tilted), max(s['tilt'] for s in tilted)] if tilted else None,
        seconds_to_turn_onto_wall=turn, seconds_upright_after_jump_off=upright_after,
        states_on_wall=states,
        mover_on_wall=sorted({(s.get('mover_flags'), s.get('collision_flags'), s.get('contact')) for s in tilted},
                             key=str),
        largest_tilt_airborne_owned=max((s['tilt'] for s in airborne if s.get('owned')), default=None),
        largest_tilt_airborne_game=max((s['tilt'] for s in airborne if not s.get('owned')), default=None),
        largest_tilt_supported_upright=max((s['tilt'] for s in samples
                                            if s.get('contact') == 0 and s['tilt'] <= 45), default=None),
        measurement=report['measurements'][-1] if report['measurements'] else None,
        shots=report.get('shots'), entries_restored=report.get('entries_restored'),
        swing_stop=report.get('swing_stop'), ray_stop=report.get('ray_stop'))


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Wall crawl probe failed: {error}', file=sys.stderr)
        sys.exit(2)
