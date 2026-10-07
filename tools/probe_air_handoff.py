"""Midair handoffs in the running game, without a headset: does the swing keep the player it flies when its
input stops for a moment, stops for longer, or the game stops stepping?

    python tools/probe_air_handoff.py

Letting go in midair hands the player to the game's own airborne state, which counts the time airborne through
Spidy's flight: the next step falls at 36-48 m/s at once (the October 6-7 headset reports had about one such drop
a session, during frame hitches and on resuming from the pause menu). The probe jumps with the virtual Xbox
controller, shoots a web up into the most open direction, reels, and while airborne:

    hold    the input turns unfocused for 0.3 s with the web held (a hitch closing the VR gameplay gate)
    hitch   the game process is suspended for 0.25 s in released flight (a long frame)
    coast   the input is unfocused for 2 s in released flight (a menu that does not pause, tracking lost)
    pause   unfocused input, then the game suspended for 3 s, then unfocused for 0.3 s more (a paused game)

For each, every native step of the player is checked: airborne steps without Spidy's command, and the largest
change of vertical speed from one step to the next (a snap to the game's fall is 30-50 m/s). It needs a freshly
started game in free roam whose save was loaded with the virtual controller (`tools/probe_menu_pad.py start`, then
`pad a --until-player`), the player perched or standing in the open. `--modules DIR` takes the movement and ray
modules from another build (the play folder's), for a comparison. It writes reports/air-handoff.json.
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
from probe_game_grab import ENTRIES, HERO_LOCAL, HERO_MOVERS, registry, resolve
from probe_game_swing import snapshot as swing_snapshot
from probe_menu_pad import BUTTONS, XR_DLL, XR_EXPORTS, pad
from probe_native_motion import snapshot as motion_snapshot
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from vr_launcher import bring_to_front

# The movement module's airborne-event hooks, besides the swing's own entries.
AIR_ENTRIES = (0xa7b3a0, 0x1f9db60)
SUSPEND_RESUME = 0x0800
ntdll = c.WinDLL('ntdll')
ntdll.NtSuspendProcess.argtypes = ntdll.NtResumeProcess.argtypes = [c.c_void_p]


def swing_command(serial, origin, direction, grip, trigger, focused=True):
    """game_swing::Command v2: the left hand at `origin` aimed along `direction`."""
    raw = bytearray(168)
    struct.pack_into('<4IQ2I', raw, 0, 0x5357434d, 2, 168, int(focused), serial, 100, 0)
    dx, dy, dz = direction
    quaternion = (dy, -dx, 0, 1-dz)  # rotates -Z onto the direction
    magnitude = math.sqrt(sum(x*x for x in quaternion))
    quaternion = tuple(x/magnitude for x in quaternion) if magnitude > 1e-6 else (0, 1, 0, 0)
    struct.pack_into('<10fI2f', raw, 32, *origin, *quaternion, 0, 0, 0, 1, float(trigger), float(grip))
    struct.pack_into('<10fI2f', raw, 84, *origin, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)
    struct.pack_into('<fI', raw, 152, 1/90, 0)
    # Sample times must rise with every command; monotonic_ns repeats within a timer tick.
    struct.pack_into('<Q', raw, 160, time.perf_counter_ns())
    return bytes(raw)


def window(steps, start, end):
    """What the player's native steps did between two probe times."""
    inside = [s for s in steps if start <= s['seconds'] <= end]
    before = [s for s in steps if s['seconds'] < start]
    if before:
        inside = before[-1:]+inside
    airborne_undriven = 0
    jumps = []
    for a, b in zip(inside, inside[1:]):
        ran = b['steps']-a['steps']
        # Only steps still in the air: a landing stops the body, and the game walks it.
        if ran <= 0 or b['contact'] != 2:
            continue
        airborne_undriven += ran-(b['controlled']-a['controlled'])
        jumps.append(abs(b['velocity'][1]-a['velocity'][1])/ran)
    return dict(steps=inside[-1]['steps']-inside[0]['steps'] if inside else 0,
                airborne_at_start=bool(inside and inside[0]['contact'] == 2),
                airborne_steps_without_command=airborne_undriven,
                largest_vertical_change_mps=round(max(jumps), 2) if jumps else None,
                vertical_speed=[round(inside[0]['velocity'][1], 2), round(inside[-1]['velocity'][1], 2)] if inside else None,
                lowest_vertical_speed=round(min(s['velocity'][1] for s in inside), 2) if inside else None,
                landed=any(s['contact'] != 2 for s in inside[1:]))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--modules', type=pathlib.Path, default=ROOT/'build/windows-ninja',
                   help='folder with spidy_movement_bridge.dll and spidy_ray_bridge.dll')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/air-handoff.json')
    a = p.parse_args()
    game = Game(find_game())
    process = None
    rays = None
    rays_active = swing_active = suspended = False
    report = dict(modules=str(a.modules), events=[], tests={})
    steps, swings = [], []
    try:
        pe = PE(game.path.read_bytes())
        entries = (*ENTRIES, *AIR_ENTRIES)
        if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in entries):
            raise RuntimeError('A hook entry is already patched: start the game afresh')
        bring_to_front(game.pid)
        components = list(registry(game))
        heroes = [(a_, r) for a_, v, r, _ in components if v == HERO_LOCAL]
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required: load free roam first')
        record = heroes[0][1]
        managers = [a_ for a_, v, r, _ in components if v == HERO_MOVERS and r == record]
        mover = resolve(game, struct.unpack('<I', game.read(managers[0]+0xdb4, 4))[0])
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002 | SUSPEND_RESUME, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        xr, _ = prepare(game.pid, process, XR_DLL, ROOT/'reports/stereo-modules', XR_EXPORTS)
        motion, report['motion_hash'] = prepare(
            game.pid, process, a.modules/'spidy_movement_bridge.dll', ROOT/'reports/motion-modules',
            ('SpidyMotionStart', 'SpidyMotionStop', 'SpidyMotionData'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays, report['ray_hash'] = prepare(
            game.pid, process, a.modules/'spidy_ray_bridge.dll', ROOT/'reports/ray-modules',
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

        def run(until, grip, trigger, focused=True):
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
                    invoke(rays['SpidySwingSubmit'], swing_command(serial, hand, aim or (0, 1, 0), grip, trigger,
                                                                   focused), 'Swing input')
                time.sleep(.004)

        def mark(name):
            report['events'].append(dict(name=name, seconds=round(time.monotonic()-started, 3)))
            print(f"{report['events'][-1]['seconds']:6.2f} s  {name}", flush=True)

        def suspend(seconds):
            nonlocal suspended
            mark(f'game suspended for {seconds} s')
            ntdll.NtSuspendProcess(process)
            suspended = True
            until = time.monotonic()+seconds
            while time.monotonic() < until:
                sample()
                time.sleep(.004)
            ntdll.NtResumeProcess(process)
            suspended = False
            mark('game resumed')

        run(.3, 0, 0)
        origin = steps[-1]['position'] if steps else None
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
        now = time.monotonic()-started
        run(now+.15, 0, 0)
        mark('web shot up the open direction, grip held')
        attach = time.monotonic()-started
        run(attach+.5, 1, 0)
        mark('reeling')
        run(attach+1.6, 1, 1)
        held = [s for s in swings if s['seconds'] >= attach]
        report['web_attached'] = any(any(w['attached'] for w in s['webs']) for s in held)
        report['owned_before_tests'] = bool(held and held[-1]['owned'])

        mark('hold: input unfocused for 0.3 s, web held')
        t = time.monotonic()-started
        run(t+.3, 1, 1, focused=False)
        mark('input focused again')
        t_hold = (t, time.monotonic()-started+.3)
        m, s = run(time.monotonic()-started+.3, 1, 1)
        report['web_after_hold'] = bool(s and any(w['attached'] for w in s['webs']))
        run(time.monotonic()-started+.5, 1, 1)
        mark('web released: flight')
        t = time.monotonic()-started
        run(t+.5, 0, 0)
        t0 = time.monotonic()-started
        suspend(.25)
        run(time.monotonic()-started+.4, 0, 0)
        t_hitch = (t0, time.monotonic()-started)
        mark('coast: input unfocused for 2 s')
        t0 = time.monotonic()-started
        run(t0+2, 0, 0, focused=False)
        mark('input focused again')
        run(time.monotonic()-started+.3, 0, 0)
        t_coast = (t0, time.monotonic()-started)
        mark('pause: input unfocused, then the game suspended for 3 s')
        t0 = time.monotonic()-started
        run(t0+.15, 0, 0, focused=False)
        suspend(3)
        run(time.monotonic()-started+.3, 0, 0, focused=False)
        mark('input focused again')
        run(time.monotonic()-started+.6, 0, 0)
        t_pause = (t0, time.monotonic()-started)
        for name, (start, end) in (('hold', t_hold), ('hitch', t_hitch), ('coast', t_coast), ('pause', t_pause)):
            report['tests'][name] = dict(start=round(start, 3), end=round(end, 3), **window(steps, start, end))
        report['origin'] = origin
    finally:
        if suspended:
            ntdll.NtResumeProcess(process)
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
    summary = {k: report.get(k) for k in ('web_attached', 'owned_before_tests', 'web_after_hold', 'swing_stop',
                                          'ray_stop', 'entries_restored')}
    print(json.dumps(summary))
    for name, test in report['tests'].items():
        print(f"{name:>6}: {json.dumps(test)}")
    tests = report['tests']
    ok = len(tests) == 4 and all(t['airborne_at_start'] and not t['airborne_steps_without_command'] and
                                 t['largest_vertical_change_mps'] is not None and t['largest_vertical_change_mps'] < 5
                                 for t in tests.values()) and \
        report.get('web_after_hold') and report.get('entries_restored') is True and \
        not report.get('swing_stop') and not report.get('ray_stop')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
