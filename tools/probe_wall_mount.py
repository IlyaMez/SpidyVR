"""Walking up onto a wall in the running game, without a headset: a player who walks at a wall goes up onto the
swing's own wall, and the game's wall crawl hands its wall over when it does start.

    python tools/probe_wall_mount.py

A player who walked into a wall was the game's: after 0.15 s against it the game turned him onto the wall in its own
crawl (all five crawls of the October 10 headset report began so), with the view upright and only A to leave. The
swing now sees the stick walk him at a wall (Swing::mounting, include/spidy/swing.hpp), has the game jump, and takes
him onto its own wall as he leaves the ground. Where the game's crawl begins all the same, the swing has the game
jump out of it and takes that wall.

The probe gets the player onto a wall as tools/probe_wall_run.py does (a jump, a web, the reel), walks him down it
to the ground with the swing input's stick, and then:

  mount      pushes the swing's stick at the wall, and presses A on the virtual Xbox controller while the swing
             asks for the jump (the VR worker does both in a session): the swing's wall has him, he climbs
  hand-over  walks him down again and into the wall with the controller's stick alone, as the game walks a player
             (the swing sees no stick, so nothing mounts): the game's crawl begins, the swing asks for the jump,
             and its own wall has him

Throughout it reads every native step of the player's mover, the swing's state and the player's actor.

It needs a freshly started game in free roam whose save was loaded with the virtual controller
(`tools/probe_menu_pad.py start`, then `pad a --until-player`), the player perched or standing with a building
within 50 m. It writes reports/wall-mount.json and reports/wall-mount/*.png.
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
from probe_wall_run import settings, swing_command
from run_game_vr import write_rgb_png
from vr_launcher import bring_to_front

OUTPUT = ROOT/'reports/wall-mount'
# SwingConfig (include/spidy/swing.hpp): the body's centre from its wall, and the stick's speed along it.
WALL_CLEARANCE, WALK_SPEED = .9, 6.


def window_shot(game, name):
    hwnd = game_window(game.pid)
    width, height = client_size(hwnd)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = OUTPUT/f'{name}.png'
    write_rgb_png(path, width, height, window_rgb(hwnd, width, height))
    return str(path)


def crawling(entry):
    """The game's own wall crawl: its actor turned onto the surface, or its crawl's collision mode."""
    return (entry.get('tilt') or 0) > 30 or (entry.get('collision_flags') is not None and
                                             int(entry['collision_flags'], 16) & 0x5 == 0x5)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/wall-mount.json')
    p.add_argument('--reel-seconds', type=float, default=4, help='longest reel toward the wall')
    p.add_argument('--gravity', type=float, default=7.848, help="the swing's gravity, m/s^2 (weight 80%%)")
    p.add_argument('--skip-hand-over', action='store_true', help="the mount alone, without the game's crawl")
    a = p.parse_args()
    game = Game(find_game())
    process = None
    rays_active = swing_active = False
    report = dict(events=[], samples=[], measurements=[], phases={}, shots={})
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
        invoke(rays['SpidySwingSettings'], settings(True, a.gravity), 'Swing settings')
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
                             grounded=m['grounded'], mover_flags=hex(m['mover_flags']),
                             collision_flags=hex(m['collision_flags']), velocity=[round(v, 2) for v in m['velocity']])
            if s:
                entry.update(owned=s['owned'], web=any(w['attached'] for w in s['webs']), wall=s['wall'],
                             wall_normal=[round(v, 3) for v in s['wall_normal']],
                             wall_distance=round(s['wall_distance'], 3), walls=s['walls'], mount=s['mount'],
                             takeoff=s['takeoff'], takeoff_phase=s['takeoff_phase'],
                             takeoff_timeouts=s['takeoff_timeouts'])
            last = samples[-1] if samples else None
            if not last or elapsed-last['seconds'] >= .02 or entry.get('steps') != last.get('steps'):
                samples.append(entry)
            return entry

        def controller(buttons=0, stick=(0., 0.), milliseconds=60):
            """The virtual Xbox controller's state for a moment, as the VR worker holds it (no wait here)."""
            state = struct.pack('<H2B4h', buttons, 0, 0, int(stick[0]*32767), int(stick[1]*32767), 0, 0)
            invoke(xr['SpidyPadSubmit'], state+struct.pack('<I', milliseconds), 'Virtual controller')

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

        def run(seconds, until=None, walk=None, **inputs):
            """Sample and send the swing its input for a while, or until `until(sample)` holds. The game gets A
            while the swing asks for its jump, and `walk` on the controller's stick otherwise, except while the
            swing mounts a wall: what the VR worker does with the player's own stick."""
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                entry = sample()
                submit(**inputs)
                if entry.get('takeoff'):
                    controller(BUTTONS['a'])
                elif walk and not entry.get('mount'):
                    controller(0, walk)
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
        at_wall = scale(normal, -1)
        print(f"wall {wall['distance']} m away at {wall['elevation']} degrees, normal "
              f"{[round(v, 2) for v in wall['normal']]}", flush=True)

        def on_wall(entry):
            return entry.get('wall') == 1

        def stands(entry):
            return bool(entry.get('grounded')) and not entry.get('owned')

        def down_to_the_ground(name):
            """Down the swing's wall with its stick (away from the wall is down it) until the game stands him."""
            t = mark(name)
            landed = run(40, move=normal, until=stands)
            if not landed:
                raise RuntimeError('The player did not reach the ground down the wall')
            run(.7)
            entry = measure(f'standing ({name})', normal)
            report['phases'][name] = (t, round(time.monotonic()-started, 3))
            return entry

        mark('jump (virtual controller A)')
        pad(process, xr, BUTTONS['a'], 150)
        run(.1)
        mark('web shot at the wall, grip held')
        run(.35, grip=1.)
        mark('reeling into the wall')
        arrived = run(a.reel_seconds, grip=1., trigger=1., until=lambda e: on_wall(e) or crawling(e))
        if not arrived or not on_wall(arrived):
            raise RuntimeError("The swing's wall did not take the player from the reel")
        mark("on the swing's wall: web let go")
        run(.6)
        stood = down_to_the_ground('down the wall to the ground')
        report['shots']['standing'] = window_shot(game, 'standing')
        print(f"standing {stood['centre_from_wall']} m from the wall", flush=True)

        # The mount: the swing's stick at the wall; A for the game while the swing asks for its jump.
        t = mark('mount: stick at the wall')
        mounted = run(3, move=at_wall, until=on_wall)
        report['phases']['mount'] = (t, round(time.monotonic()-started, 3))
        if mounted:
            mark("on the swing's wall from the ground")
            t = time.monotonic()-started
            run(1.5, move=at_wall)
            measure('climbing', normal, move=at_wall)
            report['phases']['climb'] = (round(t, 3), round(time.monotonic()-started, 3))
            run(.8)
            measure('stopped after the mount', normal)
            report['shots']['mounted'] = window_shot(game, 'mounted')
        else:
            mark('no mount within 3 s')
            report['shots']['mounted'] = window_shot(game, 'not-mounted')

        def met(entry):
            return crawling(entry) or on_wall(entry) or entry.get('mount')

        if not a.skip_hand_over and mounted:
            began = None
            for attempt in range(1, 4):
                down_to_the_ground(f'down the wall again ({attempt})')
                # Which way the controller's stick walks him: forward on it for a moment, by where he goes.
                t = mark("hand-over: the controller's stick forward, to find its heading")
                before = actor(game, record)['feet']
                began = run(.35, walk=(0., 1.), until=met)
                after = actor(game, record)['feet']
                went = (after[0]-before[0], 0., after[2]-before[2])
                if not began and length(went) > .15:
                    forward = norm(went)
                    right = (-forward[2], 0., forward[0])
                    stick = (dot(at_wall, right), dot(at_wall, forward))
                    report['stick'] = dict(forward=forward, stick=stick, travelled=round(length(went), 2))
                    mark('hand-over: walking into the wall with the controller alone')
                    began = run(2.5, walk=stick, until=met) or run(2.5, walk=(-stick[0], stick[1]), until=met)
                # Off a ledge beside the wall he falls at it and the swing's wall has him before the game's crawl:
                # from the ground below, again.
                if not began or crawling(began) or began.get('mount') == 2:
                    break
                mark("the swing's wall took him as he fell at it")
                run(.5)
            if began and (crawling(began) or began.get('mount') == 2):
                mark("the game's crawl began")
                taken = run(3, until=on_wall)
                if taken:
                    mark("on the swing's wall out of the game's crawl")
                    last = run(1.5) or sample()
                    # Whichever wall the controller walked him into: by the swing's own normal for it.
                    measure('after the hand-over', norm(last['wall_normal']) if on_wall(last) else normal)
                else:
                    mark('no hand-over within 3 s')
                    run(1.)
                report['shots']['hand_over'] = window_shot(game, 'hand-over')
            else:
                mark("the controller did not walk him into the game's crawl")
            report['phases']['hand_over'] = (t, round(time.monotonic()-started, 3))
        # Off the wall and down, so the game is left with a standing player.
        mark("jump off the wall (the swing input's jump)")
        run(.12, jump=True)
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
    return 0 if clean and summary.get('passed') else 1


def summarize(report):
    """How long the mount took and what the game's mover did meanwhile; the same for the hand-over."""
    samples = report['samples']
    phases = report.get('phases', {})
    out = dict(wall=report.get('wall'), events=report['events'], measurements=report.get('measurements'),
               shots=report.get('shots'), stick=report.get('stick'), entries_restored=report.get('entries_restored'),
               swing_stop=report.get('swing_stop'), ray_stop=report.get('ray_stop'),
               takeoff_timeouts=max((s.get('takeoff_timeouts') or 0 for s in samples), default=0))

    def within(phase, extra=0.):
        if phase not in phases:
            return []
        start, end = phases[phase]
        return [s for s in samples if start <= s['seconds'] <= end+extra]

    def first(entries, test):
        return next((s['seconds'] for s in entries if test(s)), None)
    checks = {}
    mount = within('mount')
    if mount:
        start = phases['mount'][0]
        asked, jumped = first(mount, lambda s: s.get('mount')), first(mount, lambda s: s.get('takeoff'))
        left, taken = first(mount, lambda s: s.get('grounded') is False), first(mount, lambda s: s.get('wall') == 1)
        crawl = [s for s in mount if crawling(s)]
        out['mount'] = dict(
            asked_after_s=round(asked-start, 3) if asked else None,
            jump_after_s=round(jumped-start, 3) if jumped else None,
            off_the_ground_after_s=round(left-start, 3) if left else None,
            on_the_wall_after_s=round(taken-start, 3) if taken else None,
            mount_flags=sorted({s.get('mount') for s in mount if s.get('mount') is not None}),
            game_crawl_samples=len(crawl), largest_tilt=max((s.get('tilt') or 0 for s in mount), default=None),
            movers=sorted({(s.get('mover_flags'), s.get('collision_flags'), s.get('contact')) for s in mount}, key=str))
        climb = [s for s in within('climb') if s['seconds'] >= phases['climb'][0]+.6 and s.get('velocity')]
        ups = sorted(s['velocity'][1] for s in climb)
        out['mount']['climb_m_s'] = round(ups[len(ups)//2], 2) if ups else None
        out['mount']['undriven_climb_steps'] = sum(1 for s in climb if s.get('status') != 2)
        held = [m for m in report.get('measurements', []) if m['name'] in ('climbing', 'stopped after the mount')]
        checks.update(
            mounted=taken is not None and taken-start < 1.5,
            no_game_crawl=not crawl,
            climbed=(out['mount']['climb_m_s'] or 0) > WALK_SPEED-1.5 and not out['mount']['undriven_climb_steps'],
            clearance=bool(held) and all(m.get('centre_from_wall') is not None and
                                         WALL_CLEARANCE-.25 <= m['centre_from_wall'] <= WALL_CLEARANCE+.35
                                         for m in held))
    hand = within('hand_over')
    if hand:
        crawl = [s for s in hand if crawling(s)]
        began = crawl[0]['seconds'] if crawl else None
        after = [s for s in hand if began is not None and s['seconds'] >= began]
        jumped, taken = first(after, lambda s: s.get('takeoff')), first(after, lambda s: s.get('wall') == 1)
        out['hand_over'] = dict(
            crawl_began=began, crawl_samples=len(crawl),
            crawl_seconds=round(crawl[-1]['seconds']-began, 3) if crawl else 0,
            jump_after_s=round(jumped-began, 3) if jumped else None,
            on_the_wall_after_s=round(taken-began, 3) if taken else None,
            mount_flags=sorted({s.get('mount') for s in hand if s.get('mount') is not None}),
            swing_wall_samples=sum(1 for s in after if s.get('wall') == 1),
            stood_again_samples=sum(1 for s in after if s.get('grounded') and not crawling(s)),
            largest_tilt=max((s.get('tilt') or 0 for s in hand), default=None),
            movers=sorted({(s.get('mover_flags'), s.get('collision_flags'), s.get('contact')) for s in after}, key=str))
        last = [m for m in report.get('measurements', []) if m['name'] == 'after the hand-over']
        # Met only if the game's crawl began: the mount's own checks do not wait on it.
        if crawl:
            # Still on it when the probe measured him there, a second and a half on.
            end = [s for s in hand if last and s['seconds'] <= last[0]['seconds'] and s.get('wall') is not None][-1:]
            out['hand_over']['on_the_wall_at_the_end'] = bool(end) and end[0].get('wall') == 1
            held = [s['seconds'] for s in after if s.get('wall') == 1]
            settled = next((t for t in held if not any(s.get('wall') != 1 and s.get('wall') is not None
                                                      for s in after if t <= s['seconds'] <= t+.5)), None)
            out['hand_over']['settled_on_the_wall_after_s'] = round(settled-began, 3) if settled else None
            checks.update(
                handed_over=taken is not None and taken-began < 2 and out['hand_over']['on_the_wall_at_the_end'],
                hand_over_clearance=bool(last) and last[0].get('centre_from_wall') is not None and
                WALL_CLEARANCE-.25 <= last[0]['centre_from_wall'] <= WALL_CLEARANCE+.35)
    out['checks'] = {k: bool(v) for k, v in checks.items()}
    out['passed'] = bool(checks) and all(out['checks'].values())
    return out


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Wall mount probe failed: {error}', file=sys.stderr)
        sys.exit(2)
