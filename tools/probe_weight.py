"""The VR settings' weight in the running game, without a headset: does the player fall at the gravity it sets?

    python tools/probe_weight.py

The weight (percent of real gravity) is the swing's gravity while webs fly the player: swinging, and after
letting go until landing. The probe starts the swing at the default weight, jumps with the virtual Xbox controller,
shoots a web up into the most open direction and reels, lets go, and in that released flight changes the weight
as the SPIDY VR tab does (SpidySwingSettings) a step at a time: 80%, 150%, 300%. For each it fits the player's
vertical acceleration to every native step in the air that Spidy commanded, and compares it with the gravity the
weight sets (9.81 m/s^2 x weight / 100). It needs a freshly started game in free roam whose save was loaded with
the virtual controller (`tools/probe_menu_pad.py start`, then `pad a --until-player`), the player perched or
standing in the open, and the game window in front. It writes reports/weight-probe.json; exits 1 when a weight's
fall is more than 5% off, or a step in the air went without Spidy's command.
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
from probe_game_grab import ENTRIES, HERO_LOCAL, HERO_MOVERS, registry, resolve
from probe_game_swing import snapshot as swing_snapshot
from probe_menu_pad import BUTTONS, XR_DLL, XR_EXPORTS, pad
from probe_native_motion import snapshot as motion_snapshot
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from vr_launcher import bring_to_front

# vr_settings.hpp: real gravity, and the weights the probe flies at (the default first).
REAL_GRAVITY = 9.81
WEIGHTS = ((80, .6), (150, .6), (300, .4))  # percent, seconds of flight
# A weight's first steps may still run the command made at the one before (the mover applies a command a step late).
SETTLE = .1


def gravity(weight):
    return REAL_GRAVITY*weight/100


def settings(gravity_mps2):
    """game_swing::Settings v3: no grabbing, 32 m/s, webs in open air, the gravity."""
    return struct.pack('<4IfIf', 0x53575354, 3, 28, 0, 32., 1, gravity_mps2)


def fall(steps, start, end):
    """The vertical acceleration the native steps between two probe times show, on game time (their dt)."""
    inside = [s for s in steps if start <= s['seconds'] <= end]
    points, clock, undriven, landed = [], 0., 0, False
    for a, b in zip(inside, inside[1:]):
        ran = b['steps']-a['steps']
        if ran <= 0:
            continue
        if b['contact'] != 2:
            landed = True
            break
        undriven += ran-(b['controlled']-a['controlled'])
        clock += ran*b['dt']
        points.append((clock, b['velocity'][1]))
    if len(points) < 5:
        return dict(steps=len(points), landed=landed, undriven=undriven, gravity=None)
    mean_t = sum(t for t, _ in points)/len(points)
    mean_v = sum(v for _, v in points)/len(points)
    slope = sum((t-mean_t)*(v-mean_v) for t, v in points)/sum((t-mean_t)**2 for t, _ in points)
    return dict(steps=len(points), game_seconds=round(clock, 3), landed=landed, undriven=undriven,
                vertical_speed=[round(points[0][1], 2), round(points[-1][1], 2)], gravity=round(-slope, 3))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/weight-probe.json')
    a = p.parse_args()
    game = Game(find_game())
    process = None
    rays = None
    rays_active = swing_active = False
    report = dict(events=[], weights=[])
    steps, swings = [], []
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
        # As a VR session starts it at the default weight: 32 m/s, no grabbing.
        invoke(rays['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record,
                                                    mover, motion_module, 30000, 32., gravity(WEIGHTS[0][0]), 0),
               'Native swing start')
        swing_active = True
        started = time.monotonic()
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

        def mark(name):
            report['events'].append(dict(name=name, seconds=round(time.monotonic()-started, 3)))
            print(f"{report['events'][-1]['seconds']:6.2f} s  {name}", flush=True)

        run(.3, 0, 0)
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
            run(time.monotonic()-started+.02, 0, 0)
        if heading is None:
            raise RuntimeError('The open-space rays found no direction')
        aim = (heading[0]*math.cos(1.05), math.sin(1.05), heading[2]*math.cos(1.05))
        report['heading'] = heading
        mark('jump (virtual controller A)')
        pad(process, xr, BUTTONS['a'], 150)
        run(time.monotonic()-started+.15, 0, 0)
        mark('web shot up the open direction, grip held')
        attach = time.monotonic()-started
        run(attach+.5, 1, 0)
        mark('reeling')
        run(attach+1.6, 1, 1)
        held = [s for s in swings if s['seconds'] >= attach]
        report['web_attached'] = any(any(w['attached'] for w in s['webs']) for s in held)
        mark('web released: flight')
        for weight, seconds in WEIGHTS:
            if weight != WEIGHTS[0][0]:
                invoke(rays['SpidySwingSettings'], settings(gravity(weight)), 'Swing settings')
            mark(f'weight {weight}%: gravity {gravity(weight):.3f} m/s^2')
            t0 = time.monotonic()-started
            run(t0+seconds, 0, 0)
            measured = fall(steps, t0+SETTLE, time.monotonic()-started)
            report['weights'].append(dict(weight=weight, expected=round(gravity(weight), 3), **measured))
            print(json.dumps(report['weights'][-1]), flush=True)
        # Then the flight goes on at the last weight until it lands, or for two seconds.
        t0 = time.monotonic()-started
        while time.monotonic()-started < t0+2 and not (steps and steps[-1]['contact'] != 2):
            run(time.monotonic()-started+.1, 0, 0)
        report['landed'] = bool(steps and steps[-1]['contact'] != 2)
        # A refused gravity: more than a swing takes.
        report['refused_settings'] = call_with_payload(process, rays['SpidySwingSettings'], settings(31.))
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
        report['steps'] = steps
        report['swing_samples'] = swings
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(report, indent=1)+'\n')
    summary = {k: report.get(k) for k in ('web_attached', 'landed', 'refused_settings', 'swing_stop', 'ray_stop',
                                          'entries_restored')}
    print(json.dumps(summary))
    ok = len(report['weights']) == len(WEIGHTS) and all(
        w['gravity'] is not None and not w['undriven'] and not w['landed'] and
        abs(w['gravity']-w['expected']) <= .05*w['expected'] for w in report['weights']) and \
        report.get('refused_settings') == 2001 and report.get('entries_restored') is True and \
        not report.get('swing_stop') and not report.get('ray_stop')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
