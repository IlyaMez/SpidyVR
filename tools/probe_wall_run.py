"""The swing's own walls in the running game, without a headset: a player who flies into a wall stays on it, walks
it and jumps off it, and the game's own wall crawl never starts.

    python tools/probe_wall_run.py
    python tools/probe_wall_run.py --walls off      the game's own wall crawl, as before, for comparison

A player in the air who comes into a wall used to be the game's: it held him still against the wall for a second,
turned him onto it in its crawl, and only a jump left it. The swing now keeps him on the wall itself (Swing's
walls, include/spidy/swing.hpp): his centre 0.9 m off it, where the game's collision never meets the wall, so the
game goes on in its air state. The probe finds the nearest wall around the player with world rays, jumps with the
virtual Xbox controller, shoots a web at the wall and reels in until the swing has him on it, lets go and stays
there, then walks along the wall and up it with the swing input's stick, stops, and jumps off with its jump.
Throughout it reads every native step of the player's mover, the swing's state and the player's actor, and with
world rays how far his centre is from the wall.

It needs a freshly started game in free roam whose save was loaded with the virtual controller
(`tools/probe_menu_pad.py start`, then `pad a --until-player`), the player perched or standing with a building
within 50 m. It writes reports/wall-run.json and reports/wall-run/*.png.
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
from probe_air_handoff import AIR_ENTRIES
from probe_game_grab import ENTRIES, HERO_LOCAL, HERO_MOVERS, registry, resolve
from probe_game_screen import game_window, client_size, window_rgb
from probe_game_swing import snapshot as swing_snapshot
from probe_menu_pad import BUTTONS, XR_DLL, XR_EXPORTS, pad
from probe_native_motion import snapshot as motion_snapshot
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from probe_wall_crawl import actor, add, dot, length, norm, scale, sub, tilt
from run_game_vr import write_rgb_png
from vr_launcher import bring_to_front

OUTPUT = ROOT/'reports/wall-run'
# SwingConfig (include/spidy/swing.hpp): the body's centre from its wall, and the stick's speed along it.
WALL_CLEARANCE, WALK_SPEED = .9, 6.


def swing_command(serial, origin, direction, grip=0., trigger=0., move=(0., 0., 0.), jump=False):
    """game_swing::Command v2: the left hand at `origin` aimed along `direction`, the stick's `move` (a direction
    in the world, at most 1 long) and the jump button."""
    raw = bytearray(168)
    struct.pack_into('<4IQ2I', raw, 0, 0x5357434d, 2, 168, 1, serial, 100, int(jump))
    dx, dy, dz = direction
    quaternion = (dy, -dx, 0, 1-dz)  # rotates -Z onto the direction
    magnitude = math.sqrt(sum(x*x for x in quaternion))
    quaternion = tuple(x/magnitude for x in quaternion) if magnitude > 1e-6 else (0, 1, 0, 0)
    struct.pack_into('<10fI2f', raw, 32, *origin, *quaternion, 0, 0, 0, 1, float(trigger), float(grip))
    struct.pack_into('<10fI2f', raw, 84, *origin, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)
    struct.pack_into('<3f', raw, 136, *move)
    struct.pack_into('<fI', raw, 152, 1/90, 0)
    # Sample times must rise with every command; monotonic_ns repeats within a timer tick.
    struct.pack_into('<Q', raw, 160, time.perf_counter_ns())
    return bytes(raw)


def settings(walls, gravity):
    """game_swing::Settings v4: no web grab, 32 m/s, webs hold in open air."""
    return struct.pack('<4IfIfI', 0x53575354, 4, 32, 0, 32., 1, gravity, int(walls))


def air_mode(sample):
    """The mover as the game's air state runs it and the swing drives it: sweeping, unsupported, in the air."""
    return sample.get('collision_flags') is not None and int(sample['collision_flags'], 16) & 0x10 and \
        not int(sample['collision_flags'], 16) & 0x800003 and sample.get('contact') == 2


def window_shot(game, name):
    hwnd = game_window(game.pid)
    width, height = client_size(hwnd)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = OUTPUT/f'{name}.png'
    write_rgb_png(path, width, height, window_rgb(hwnd, width, height))
    return str(path)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/wall-run.json')
    p.add_argument('--walls', choices=('on', 'off'), default='on',
                   help="off: the game's own wall crawl takes the player, as before")
    p.add_argument('--reel-seconds', type=float, default=4, help='longest reel toward the wall')
    p.add_argument('--gravity', type=float, default=7.848, help="the swing's gravity, m/s^2 (weight 80%%)")
    a = p.parse_args()
    game = Game(find_game())
    process = None
    rays_active = swing_active = False
    report = dict(walls=a.walls, events=[], samples=[], measurements=[], phases={}, shots={})
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
             'SpidySwingSubmit', 'SpidySwingStop', 'SpidySwingData', 'SpidySwingSettings'))

        def invoke(address, payload, what):
            code = call_with_payload(process, address, payload)
            if code:
                raise RuntimeError(f'{what}: {code}')
        invoke(rays['SpidyRayStart'], struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 30000, 0x410),
               'World rays start')
        rays_active = True
        invoke(rays['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record,
                                                    mover, motion_module, 30000, 32., a.gravity, 0),
               'Native swing start')
        swing_active = True
        invoke(rays['SpidySwingSettings'], settings(a.walls == 'on', a.gravity), 'Swing settings')
        started = time.monotonic()
        serial = ray_serial = 0
        target = None

        def sample():
            """The player's actor, mover and swing now; kept when something changed."""
            elapsed = time.monotonic()-started
            body = actor(game, record)
            m = motion_snapshot(game, motion['SpidyMotionData'])
            s = swing_snapshot(game, rays['SpidySwingData'])
            if s and s['error']:
                raise RuntimeError(f"Native swing error {s['error']}")
            entry = dict(seconds=round(elapsed, 4))
            if body:
                entry.update(feet=[round(v, 3) for v in body['feet']], tilt=tilt(body['up']))
            if m:
                entry.update(steps=m['steps'], controlled=m['controlled'], status=m['status'], contact=m['contact'],
                             mover_flags=hex(m['mover_flags']), collision_flags=hex(m['collision_flags']),
                             velocity=[round(v, 2) for v in m['velocity']], air_events=m['air_events'])
            if s:
                entry.update(owned=s['owned'], web=any(w['attached'] for w in s['webs']), wall=s['wall'],
                             wall_normal=[round(v, 3) for v in s['wall_normal']],
                             wall_distance=round(s['wall_distance'], 3), walls=s['walls'],
                             wall_jumps=s['wall_jumps'])
            last = samples[-1] if samples else None
            if not last or elapsed-last['seconds'] >= .02 or entry.get('steps') != last.get('steps'):
                samples.append(entry)
            return entry

        def submit(grip=0., trigger=0., move=(0., 0., 0.), jump=False):
            """The left hand at the player's chest, aimed at the target, and the stick, for the native swing."""
            nonlocal serial
            body = actor(game, record)
            if not body:
                return
            hand = add(body['feet'], (0., 1.2, 0.))
            aim = norm(sub(target, hand)) if target else (0., 1., 0.)
            serial += 1
            invoke(rays['SpidySwingSubmit'], swing_command(serial, hand, aim, grip, trigger, move, jump),
                   'Swing input')

        def run(seconds, until=None, **inputs):
            """Sample and send the swing its input for a while, or until `until(sample)` holds."""
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                entry = sample()
                submit(**inputs)
                if until and until(entry):
                    return entry
                time.sleep(.004)
            return None

        def mark(name):
            report['events'].append(dict(name=name, seconds=round(time.monotonic()-started, 3)))
            print(f"{report['events'][-1]['seconds']:6.2f} s  {name}", flush=True)
            return report['events'][-1]['seconds']

        def cast(batch, **inputs):
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
                submit(**inputs)
                time.sleep(.004)
            raise RuntimeError('The world rays did not answer')

        def measure(name, normal, **inputs):
            """How far the player's centre is from the wall, straight at it."""
            body = actor(game, record)
            centre = add(body['feet'], (0., 1., 0.))
            hit = cast([(centre, scale(normal, -1), 4., 0)], **inputs)[0]
            entry = dict(name=name, seconds=round(time.monotonic()-started, 3), centre=centre, tilt=tilt(body['up']),
                         centre_from_wall=round(hit['fraction']*4., 3) if hit['count'] else None,
                         normal=hit['normal'] if hit['count'] else None)
            report['measurements'].append(entry)
            return entry

        run(.3)
        start = actor(game, record)
        if not start:
            raise RuntimeError("The player's actor could not be read")
        report['start'] = dict(feet=start['feet'], tilt=tilt(start['up']))
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
        target = wall['point']
        normal = norm((wall['normal'][0], 0., wall['normal'][2]))
        along = (-normal[2], 0., normal[0])
        print(f"wall {wall['distance']} m away at {wall['elevation']} degrees, normal "
              f"{[round(v, 2) for v in wall['normal']]}", flush=True)

        def on_wall(entry):
            return entry.get('wall') == 1

        def game_took(entry):
            """The game's own wall states: its mover out of the air mode, or its actor turned."""
            return (entry.get('tilt') or 0) > 45 or (entry.get('collision_flags') is not None and
                                                     not int(entry['collision_flags'], 16) & 0x10 and
                                                     entry.get('contact') == 2)

        mark('jump (virtual controller A)')
        pad(process, xr, BUTTONS['a'], 150)
        run(.1)
        mark('web shot at the wall, grip held')
        run(.35, grip=1.)
        t_reel = mark('reeling into the wall')
        arrived = run(a.reel_seconds, grip=1., trigger=1., until=lambda e: on_wall(e) or game_took(e))
        if arrived and on_wall(arrived):
            t_on = mark("on the swing's wall: web let go")
            report['phases']['reel_to_wall_seconds'] = round(t_on-t_reel, 3)
            run(.5)
            report['shots']['on_wall'] = window_shot(game, f'on-wall-{a.walls}')
            measure('holding', normal)
            run(.7)
            t = mark('stick along the wall')
            run(1.5, move=along)
            measure('walking along', normal, move=along)
            report['phases']['along'] = (t, round(time.monotonic()-started, 3))
            t = mark('stick at the wall: up it')
            run(1.5, move=scale(normal, -1))
            measure('walking up', normal, move=scale(normal, -1))
            report['phases']['up'] = (t, round(time.monotonic()-started, 3))
            t = mark('stick let go')
            run(.8)
            measure('stopped', normal)
            report['phases']['stopped'] = (t, round(time.monotonic()-started, 3))
            t = mark("jump off the wall (the swing input's jump)")
            run(.12, jump=True)
            report['phases']['jump'] = (t, round(time.monotonic()-started, 3))
            run(.5)
            report['shots']['after_jump'] = window_shot(game, f'after-jump-{a.walls}')
            run(2.)
        else:
            mark("the game's own wall state took the player" if arrived else 'no wall reached while reeling')
            # As before: held against the wall, then turned onto it; a jump of the game's leaves it.
            run(3., grip=0.)
            report['shots']['on_wall'] = window_shot(game, f'on-wall-{a.walls}')
            measure('held by the game', normal)
            mark('jump off the wall (virtual controller A)')
            pad(process, xr, BUTTONS['a'], 150)
            run(2.5)
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
    summary = report['summary']
    print(json.dumps(summary, indent=1))
    clean = report.get('entries_restored') is True and not report.get('swing_stop') and not report.get('ray_stop')
    if a.walls == 'off':
        return 0 if clean else 1
    return 0 if clean and summary.get('passed') else 1


def summarize(report):
    """What held the player on the wall, what the game's mover did meanwhile, and how the wall was walked."""
    samples = report['samples']
    events = {e['name']: e['seconds'] for e in report['events']}
    phases = report.get('phases', {})
    on = [s for s in samples if s.get('wall') == 1]
    out = dict(walls=report.get('walls'), wall=report.get('wall'), events=report['events'],
               reel_to_wall_seconds=phases.get('reel_to_wall_seconds'),
               seconds_on_swing_wall=round(on[-1]['seconds']-on[0]['seconds'], 3) if on else 0,
               walls_taken=max((s.get('walls') or 0 for s in samples), default=0),
               wall_jumps=max((s.get('wall_jumps') or 0 for s in samples), default=0),
               measurements=report.get('measurements'), shots=report.get('shots'),
               entries_restored=report.get('entries_restored'), swing_stop=report.get('swing_stop'),
               ray_stop=report.get('ray_stop'))
    # The game's own wall states from the reel to the jump off: its mover sweeping but out of the air mode while
    # unsupported (it holds the player against the wall), its crawl's turn (0x2060005), or its actor turned
    # onto a surface.
    begin = next((t for name, t in events.items() if name.startswith('reeling')), 0)
    end = next((t+.3 for name, t in events.items() if name.startswith('jump off')), float('inf'))
    game_wall = [s for s in samples if begin <= s['seconds'] <= end and s.get('collision_flags') is not None and
                 ((s.get('contact') == 2 and not int(s['collision_flags'], 16) & 0x800013) or
                  int(s['collision_flags'], 16) & 0x5 == 0x5 or (s.get('tilt') or 0) > 45)]
    out['game_wall_state_samples'] = len(game_wall)
    out['game_wall_state_seconds'] = round(game_wall[-1]['seconds']-game_wall[0]['seconds'], 3) if game_wall else 0
    out['game_wall_states'] = sorted({(s.get('mover_flags'), s.get('collision_flags'), s.get('contact'))
                                      for s in game_wall}, key=str)
    out['largest_tilt'] = max((s.get('tilt') or 0 for s in samples), default=None)
    if on:
        out['mover_on_wall'] = sorted({(s.get('mover_flags'), s.get('collision_flags'), s.get('contact'),
                                        s.get('status')) for s in on}, key=str)
        out['undriven_steps_on_wall'] = sum(1 for s in on if s.get('status') != 2)
        out['not_air_mode_on_wall'] = sum(1 for s in on if not air_mode(s))
        distances = [s['wall_distance'] for s in on if s.get('wall_distance')]
        out['swing_wall_distance'] = [min(distances), max(distances)] if distances else None
        first, last = on[0], on[-1]
        if first.get('air_events') is not None and last.get('air_events') is not None:
            # The game's air state handles one event a step while it runs.
            steps = (last.get('steps') or 0)-(first.get('steps') or 0)
            out['air_events_per_step_on_wall'] = round((last['air_events']-first['air_events'])/steps, 2) \
                if steps else None

    def speeds(phase, direction):
        if phase not in phases:
            return None
        start, end = phases[phase]
        inside = [s for s in samples if start+.5 <= s['seconds'] <= end and s.get('velocity')]
        values = [dot(s['velocity'], direction) for s in inside]
        return round(sorted(values)[len(values)//2], 2) if values else None
    if report.get('wall'):
        normal = norm((report['wall']['normal'][0], 0., report['wall']['normal'][2]))
        along = (-normal[2], 0., normal[0])
        out['speed_along_m_s'] = speeds('along', along)
        out['speed_up_m_s'] = speeds('up', (0., 1., 0.))
        out['speed_stopped_m_s'] = speeds('stopped', (0., 1., 0.))
        if 'jump' in phases:
            start = phases['jump'][0]
            after = [s for s in samples if start+.1 <= s['seconds'] <= start+.35 and s.get('velocity')]
            if after:
                out['jump_out_m_s'] = round(max(dot(s['velocity'], normal) for s in after), 2)
                out['jump_up_m_s'] = round(max(s['velocity'][1] for s in after), 2)
            out['on_wall_after_jump'] = any(s.get('wall') == 1 for s in samples if s['seconds'] > start+.3)
    if report.get('walls') == 'on':
        held = [m for m in report.get('measurements', []) if m.get('centre_from_wall') is not None]
        checks = dict(
            taken=bool(on),
            game_never_took=not game_wall,
            driven_in_air_mode=bool(on) and not out.get('undriven_steps_on_wall') and
            not out.get('not_air_mode_on_wall'),
            clearance=bool(held) and all(WALL_CLEARANCE-.25 <= m['centre_from_wall'] <= WALL_CLEARANCE+.35
                                         for m in held),
            walked_along=(out.get('speed_along_m_s') or 0) and abs(out['speed_along_m_s']) > WALK_SPEED-1.5,
            walked_up=(out.get('speed_up_m_s') or 0) > WALK_SPEED-1.5,
            stopped=out.get('speed_stopped_m_s') is not None and abs(out['speed_stopped_m_s']) < .3,
            jumped=(out.get('jump_out_m_s') or 0) > 3 and (out.get('jump_up_m_s') or 0) > 2 and
            not out.get('on_wall_after_jump'))
        out['checks'] = {k: bool(v) for k, v in checks.items()}
        out['passed'] = all(out['checks'].values())
    return out


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Wall run probe failed: {error}', file=sys.stderr)
        sys.exit(2)
