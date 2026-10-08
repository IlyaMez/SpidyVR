"""Slow motion in the running game, without a headset: does the whole game slow down, and come back?

    python tools/probe_slow_motion.py

Slow motion (slow_motion.hpp) eases the game's time down to 30% of real time and back; game_time.hpp puts it into
the game's own TimeScaleSystem. The probe hooks it as a VR session does (SpidyTimeStart) and, in free roam:
  1. reads the game's clock at normal speed: its scale (1), each frame's game time against its real time, and
     Havok's step;
  2. slows it to 30% (SpidyTimeSet): the clock's scale and each frame's game time at 30%, Havok's step at 30% of
     its base;
  3. jumps, webs up, reels and lets go as the weight probe does, and in that flight slows the game to 30%: the
     player's mover then steps game time at 30% of real time, while the swing's gravity on game time stays what
     it was (the swing slows with the world); the web grab's step time follows the world's time too;
  4. puts the time back and unhooks (SpidyTimeStop): the clock, Havok's step and the hooked entry are as before.
It needs a freshly started game in free roam whose save was loaded with the virtual controller
(`tools/probe_menu_pad.py start`, then `pad a --until-player`), the player perched or standing in the open, and
the game window in front. It writes reports/slow-motion-probe.json; exits 1 when a check fails.
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
from inspect_game import PE
from observe_game import call_remote, call_with_payload, modules
from probe_air_handoff import AIR_ENTRIES, swing_command
from probe_game_grab import ENTRIES, HERO_LOCAL, HERO_MOVERS, grab_snapshot, registry, resolve
from probe_game_swing import snapshot as swing_snapshot
from probe_menu_pad import BUTTONS, XR_DLL, XR_EXPORTS, call_with_output, pad
from probe_native_motion import snapshot as motion_snapshot
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from probe_weight import fall, gravity
from vr_launcher import bring_to_front

# The game's time system's update (game_time.cpp), and the probe's exports beside the VR worker's.
TIME_UPDATE = 0x19bb430
TIME_EXPORTS = ('SpidyTimeStart', 'SpidyTimeSet', 'SpidyTimeSample', 'SpidyTimeStop')
MAGIC = 0x454d4954
SLOW = .3
# The swing's default weight (60%), and what the web grab may catch (game_grab::movableKinds): with it the grab
# steps, so its step time is reported.
WEIGHT = 60
GRAB_KINDS = (1 << 1) | (1 << 2)
# Seconds after a change before measuring: the hook applies it at the game's next update, the clock at the frame
# after, and the mover runs each command a step late.
SETTLE = .15


def time_sample(game, process, exports):
    """game_time's ProbeSample: the hook's counts and the game's clock."""
    code, raw = call_with_output(game, process, exports['SpidyTimeSample'], b'', 104)
    if code or len(raw) != 104:
        raise RuntimeError(f'SpidyTimeSample: {code}')
    if struct.unpack_from('<3I', raw) != (MAGIC, 1, 104):
        raise RuntimeError('Time protocol mismatch: rebuild and restart the game')
    installed, = struct.unpack_from('<I', raw, 12)
    updates, slowed, systems, system = struct.unpack_from('<4Q', raw, 16)
    wanted, own, own_physics, world, step, base = struct.unpack_from('<6f', raw, 48)
    physics_scaled, restored = struct.unpack_from('<2I', raw, 72)
    clock, game_frame, real_frame = struct.unpack_from('<3d', raw, 80)
    return dict(installed=bool(installed), updates=updates, slowed=slowed, systems=systems, system=hex(system),
                wanted=round(wanted, 4), own=round(own, 4), own_physics=round(own_physics, 4), world=round(world, 4),
                physics_step=step, physics_base=base, physics_scaled=bool(physics_scaled), restored=bool(restored),
                clock=clock, game_frame=game_frame, real_frame=real_frame)


def clock_stats(samples):
    """The clock over a stretch: its scale, each frame's game time per real time, Havok's step per its base."""
    frames = [s['game_frame']/s['real_frame'] for s in samples if s['real_frame'] > 1e-4]
    return dict(samples=len(samples), clock=round(statistics.median(s['clock'] for s in samples), 4),
                frame_ratio=round(statistics.median(frames), 4) if frames else None,
                physics=round(statistics.median(s['physics_step']/s['physics_base'] for s in samples
                                                if s['physics_base'] > 0), 4),
                updates=samples[-1]['updates']-samples[0]['updates'])


def game_rate(steps, start, end):
    """Game seconds the player's mover stepped per real second between two probe times."""
    inside = [s for s in steps if start <= s['seconds'] <= end]
    if len(inside) < 3:
        return None
    game_time = sum((b['steps']-a['steps'])*b['dt'] for a, b in zip(inside, inside[1:]))
    return round(game_time/(inside[-1]['seconds']-inside[0]['seconds']), 3)


def close_to(value, expected, share):
    return value is not None and abs(value-expected) <= share*abs(expected)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/slow-motion-probe.json')
    p.add_argument('--check', action='store_true', help="only score the report at --output again; no game")
    a = p.parse_args()
    if a.check:
        checks = check(json.loads(a.output.read_text()))
        print(json.dumps(checks))
        return 0 if all(checks.values()) else 1
    game = Game(find_game())
    process = None
    rays = xr = None
    rays_active = swing_active = time_active = False
    report = dict(events=[], clock={}, flight={}, grab={})
    steps, swings, grabs, clocks = [], [], [], []
    hooked = (*ENTRIES, *AIR_ENTRIES, TIME_UPDATE)
    try:
        pe = PE(game.path.read_bytes())
        if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in hooked):
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
        xr, report['xr_hash'] = prepare(game.pid, process, XR_DLL, ROOT/'reports/stereo-modules',
                                        (*XR_EXPORTS, *TIME_EXPORTS))
        motion, report['motion_hash'] = prepare(
            game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll', ROOT/'reports/motion-modules',
            ('SpidyMotionStart', 'SpidyMotionStop', 'SpidyMotionData'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays, report['ray_hash'] = prepare(
            game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll', ROOT/'reports/ray-modules',
            ('SpidyRayStart', 'SpidyRayStop', 'SpidyRaySubmit', 'SpidyRayData', 'SpidySwingStart',
             'SpidySwingSubmit', 'SpidySwingStop', 'SpidySwingData', 'SpidyGrabData'))

        def invoke(address, payload, what):
            code = call_with_payload(process, address, payload)
            if code:
                raise RuntimeError(f'{what}: {code}')

        def slow(scale):
            invoke(xr['SpidyTimeSet'], struct.pack('<3If', MAGIC, 1, 16, scale), f'Time scale {scale}')

        started = time.monotonic()

        def mark(name):
            report['events'].append(dict(name=name, seconds=round(time.monotonic()-started, 3)))
            print(f"{report['events'][-1]['seconds']:6.2f} s  {name}", flush=True)

        def watch_clock(seconds):
            """The game's clock every 20 ms for a while."""
            out = []
            until = time.monotonic()+seconds
            while time.monotonic() < until:
                out.append(dict(seconds=round(time.monotonic()-started, 3), **time_sample(game, process, xr)))
                time.sleep(.02)
            clocks.extend(out)
            return out

        code = call_remote(process, xr['SpidyTimeStart'])
        if code:
            raise RuntimeError(f'SpidyTimeStart: {code}')
        time_active = True
        mark('time hooked: normal speed')
        watch_clock(SETTLE)
        report['clock']['normal'] = clock_stats(watch_clock(1))
        mark(f'slowed to {SLOW:.0%}')
        slow(SLOW)
        watch_clock(SETTLE)
        report['clock']['slow'] = clock_stats(watch_clock(1.5))
        mark('back to normal speed')
        slow(1)
        watch_clock(SETTLE)
        report['clock']['back'] = clock_stats(watch_clock(1))
        print(json.dumps(report['clock']), flush=True)

        # The flight: the swing as a VR session starts it (default weight, the web grab offered).
        invoke(rays['SpidyRayStart'], struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 30000, 0x410),
               'World rays start')
        rays_active = True
        invoke(rays['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record,
                                                    mover, motion_module, 30000, 32., gravity(WEIGHT), GRAB_KINDS),
               'Native swing start')
        swing_active = True
        serial = 0
        aim = None
        hand = None

        def sample():
            elapsed = time.monotonic()-started
            m = motion_snapshot(game, motion['SpidyMotionData'])
            if m and m['steps'] and (not steps or m['steps'] != steps[-1]['steps']):
                steps.append(dict(seconds=elapsed, **m))
            s = swing_snapshot(game, rays['SpidySwingData'])
            if s and (not swings or s['steps'] != swings[-1]['steps'] or s['owned'] != swings[-1]['owned']):
                swings.append(dict(seconds=elapsed, **s))
            if s and s['error']:
                raise RuntimeError(f"Native swing error {s['error']}")
            g = grab_snapshot(game, rays['SpidyGrabData'])
            if g and g['steps'] and (not grabs or g['steps'] != grabs[-1]['steps']):
                grabs.append(dict(seconds=elapsed, steps=g['steps'], tick_dt=g['tick_dt'], step_dt=g['step_dt'],
                                  time_scale=g['time_scale']))
            return elapsed, m, s

        def run(until, grip, trigger):
            """Submit the hand for a while, sampling the game every few milliseconds."""
            nonlocal serial, hand
            while True:
                elapsed, m, s = sample()
                if elapsed >= until:
                    return m, s
                if m and m['steps']:
                    hand = (m['position'][0], m['position'][1]+1.2, m['position'][2])
                if hand:
                    serial += 1
                    invoke(rays['SpidySwingSubmit'], swing_command(serial, hand, aim or (0, 1, 0), grip, trigger),
                           'Swing input')
                time.sleep(.004)

        def now():
            return time.monotonic()-started

        run(now()+.3, 0, 0)
        # Which way is open: eight directions from the hand, slightly up.
        if hand:
            candidates = [(hand, (math.cos(.35)*math.cos(i*math.pi/4), math.sin(.35),
                                  math.cos(.35)*math.sin(i*math.pi/4)), 100, i) for i in range(8)]
            invoke(rays['SpidyRaySubmit'], ray_command(1, candidates, 250), 'Open-space rays')
        heading = None
        for _ in range(100):
            targets = ray_snapshot(game, rays['SpidyRayData'])
            if targets and targets['serial'] == 1 and targets['status'] == 2 and not targets['error']:
                clear = max(targets['hits'][:8], key=lambda h: h['fraction'] if h['count'] else 2)
                flat = math.hypot(clear['direction'][0], clear['direction'][2])
                heading = (clear['direction'][0]/flat, 0., clear['direction'][2]/flat)
                break
            run(now()+.02, 0, 0)
        if heading is None:
            raise RuntimeError('The open-space rays found no direction')
        aim = (heading[0]*math.cos(1.05), math.sin(1.05), heading[2]*math.cos(1.05))
        mark('jump (virtual controller A)')
        pad(process, xr, BUTTONS['a'], 150)
        run(now()+.15, 0, 0)
        mark('web shot up the open direction, grip held')
        attach = now()
        run(attach+.5, 1, 0)
        mark('reeling')
        run(attach+1.6, 1, 1)
        report['flight']['web_attached'] = any(any(w['attached'] for w in s['webs'])
                                               for s in swings if s['seconds'] >= attach)
        mark('web released: flight at normal speed')
        t0 = now()
        run(t0+.5, 0, 0)
        t1 = now()
        report['flight']['normal'] = dict(rate=game_rate(steps, t0+SETTLE, t1), **fall(steps, t0+SETTLE, t1))
        mark(f'slowed to {SLOW:.0%} in the air')
        slow(SLOW)
        t2 = now()
        run(t2+1.4, 0, 0)
        t3 = now()
        report['flight']['slow'] = dict(rate=game_rate(steps, t2+SETTLE, t3), **fall(steps, t2+SETTLE, t3))
        grab_slow = [g for g in grabs if t2+SETTLE <= g['seconds'] <= t3]
        grab_normal = [g for g in grabs if g['seconds'] < t2]
        mark('back to normal speed')
        slow(1)
        t4 = now()
        run(t4+.4, 0, 0)
        report['flight']['back'] = dict(rate=game_rate(steps, t4+SETTLE, now()), **fall(steps, t4+SETTLE, now()))
        for name, picked in (('normal', grab_normal), ('slow', grab_slow)):
            if picked:
                report['grab'][name] = dict(
                    steps=len(picked), tick_dt=round(statistics.median(g['tick_dt'] for g in picked), 5),
                    step_dt=round(statistics.median(g['step_dt'] for g in picked), 5),
                    time_scale=round(statistics.median(g['time_scale'] for g in picked), 3))
        print(json.dumps(dict(flight=report['flight'], grab=report['grab'])), flush=True)
    finally:
        if swing_active:
            report['swing_stop'] = call_remote(process, rays['SpidySwingStop'])
        if rays_active:
            report['ray_stop'] = call_remote(process, rays['SpidyRayStop'])
        if time_active:
            report['time_stop'] = call_remote(process, xr['SpidyTimeStop'])
            try:
                report['clock']['after_stop'] = clock_stats(watch_clock(.5))
            except (OSError, RuntimeError) as error:
                report['clock']['after_stop'] = str(error)
        if process:
            close(process)
        try:
            pe = PE(game.path.read_bytes())
            report['entries_restored'] = all(game.read(game.base+rva, 16) == pe.bytes(rva, 16) for rva in hooked)
        except Exception as error:  # the game may have closed
            report['entries_restored'] = str(error)
        report['steps'] = steps
        report['swing_samples'] = swings
        report['grab_samples'] = grabs
        report['clock_samples'] = clocks
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(report, indent=1)+'\n')
    checks = check(report)
    print(json.dumps(dict(checks=checks, **{k: report.get(k) for k in ('time_stop', 'swing_stop', 'ray_stop',
                                                                        'entries_restored')})))
    return 0 if all(checks.values()) else 1


def check(report):
    clock, flight, grab = report['clock'], report['flight'], report['grab']
    normal, slowed, back = clock.get('normal', {}), clock.get('slow', {}), clock.get('back', {})
    after = clock.get('after_stop') if isinstance(clock.get('after_stop'), dict) else {}
    # A frame's game time has a floor near 1/240 s: in the small window's 250 frames a second it runs ~4% over
    # the real time at any speed (October 8: 1.043 normal, 0.311 slowed), so the speeds are compared.
    ratio = (slowed.get('frame_ratio') or 0)/(normal.get('frame_ratio') or 1)
    out = dict(
        clock_normal=close_to(normal.get('clock'), 1, .01) and close_to(normal.get('frame_ratio'), 1, .08),
        clock_slowed=close_to(slowed.get('clock'), SLOW, .01) and close_to(ratio, SLOW, .03),
        physics_slowed=close_to(slowed.get('physics'), SLOW, .03),
        clock_back=close_to(back.get('clock'), 1, .01) and close_to(back.get('physics'), normal.get('physics'), .01),
        updates_ran=bool(normal.get('updates')) and bool(slowed.get('updates')),
        web_attached=bool(flight.get('web_attached')),
        mover_slowed=close_to(flight.get('slow', {}).get('rate'), SLOW*(flight.get('normal', {}).get('rate') or 1),
                              .1),
        swing_gravity_kept=close_to(flight.get('slow', {}).get('gravity'), gravity(WEIGHT), .06) and
        close_to(flight.get('normal', {}).get('gravity'), gravity(WEIGHT), .06),
        stayed_airborne=not flight.get('slow', {}).get('landed') and not flight.get('slow', {}).get('undriven'),
        grab_slowed=bool(grab.get('normal')) and bool(grab.get('slow')) and
        close_to(grab['slow']['tick_dt'], SLOW*grab['normal']['tick_dt'], .15),
        restored=report.get('entries_restored') is True and not report.get('time_stop') and
        close_to(after.get('clock'), 1, .01) and close_to(after.get('physics'), normal.get('physics'), .01),
    )
    return out


if __name__ == '__main__':
    sys.exit(main())
