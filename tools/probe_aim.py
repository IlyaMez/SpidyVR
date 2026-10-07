"""What a grip press would do with a hand aimed around the player: the aim markers' previews in the running game,
without a headset.

    python tools/probe_aim.py

It needs a freshly started game in free roam (Spidy's ray and movement modules start once per process) and the game
window in front, which it brings there. A scripted hand 1.2 m above the player's feet aims, grip open, at the sky,
eight compass points level and below, straight down, and the nearest throwable prop in reach. For each aim it reads
what the swing module's previews say a press would do (SpidyAimData: anchor, air, blocked, prop or character, and
where), checks that the swing module and the rays never faulted, and restores every hook entry. It writes
reports/aim-probe.json.

    python tools/probe_aim.py --settings

also switches web grabbing and the speed limit as the headset's VR settings panel does (SpidySwingSettings): the
swing starts without the grab, as a -NoWebGrab session does, and the prop aim is held again with the grab started
during play, switched off and on again; punching is started and stopped twice (SpidyPunchStart, SpidyPunchStop).
The sky aim is held again with webs in open air switched off (nothing to preview: a press would miss) and on.
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
from probe_game_grab import ENTRIES, HERO_LOCAL, HERO_MOVERS, LIFT, THROWABLE, hand_command, registry, resolve
from probe_game_screen import user32
from probe_game_swing import snapshot as swing_snapshot
from vr_launcher import bring_to_front

KINDS = {0: 'none', 1: 'anchor', 2: 'air', 3: 'blocked', 4: 'prop', 5: 'character'}
AIM_SIZE = 96


def aim_snapshot(game, address):
    """game_swing::AimData: the latest previews, or None while it changes."""
    for _ in range(8):
        raw = game.read(address, AIM_SIZE)
        if len(raw) != AIM_SIZE:
            return None
        if struct.unpack_from('<3I', raw) != (0x5357414d, 1, AIM_SIZE):
            raise RuntimeError('Aim protocol mismatch')
        if struct.unpack_from('<q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        hands = []
        for i in range(2):
            kind, radius = struct.unpack_from('<If', raw, 32+i*32)
            hands.append(dict(kind=KINDS.get(kind, kind), radius=round(radius, 3),
                              point=struct.unpack_from('<3f', raw, 40+i*32),
                              normal=struct.unpack_from('<3f', raw, 52+i*32)))
        return dict(status=struct.unpack_from('<I', raw, 12)[0], serial=struct.unpack_from('<Q', raw, 24)[0],
                    hands=hands)
    return None


def swing_health(game, address):
    """SpidySwingData status and error (status 4 or an error code: the swing faulted)."""
    for _ in range(8):
        swing = swing_snapshot(game, address)
        if swing:
            return dict(status=swing['status'], error=swing['error'])
    return dict(status=None, error=None)


def main():
    try:
        user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    except AttributeError:
        user32.SetProcessDPIAware()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--seconds', type=float, default=.6, help='how long to hold each aim')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/aim-probe.json')
    parser.add_argument('--settings', action='store_true',
                        help="switch the grab, the speed limit and punching during play, as the VR settings panel does")
    args = parser.parse_args()
    game = Game(find_game())
    process = None
    rays = None
    rays_active = swing_active = False
    report = dict(aims=[])
    try:
        pe = PE(game.path.read_bytes())
        if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in ENTRIES):
            raise RuntimeError('A hook entry is already patched: start the game afresh')
        bring_to_front(game.pid)
        components = list(registry(game))
        heroes = [(a, r) for a, v, r, _ in components if v == HERO_LOCAL]
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required: load free roam first')
        record = heroes[0][1]
        managers = [a for a, v, r, _ in components if v == HERO_MOVERS and r == record]
        mover = resolve(game, struct.unpack('<I', game.read(managers[0]+0xdb4, 4))[0])
        feet = game.transform(game.pointer(record))['position']
        hand = (feet[0], feet[1]+1.2, feet[2])
        props = []
        for a, v, r, _ in components:
            if v == THROWABLE:
                t = game.transform(game.pointer(r))
                if t:
                    centre = (t['position'][0], t['position'][1]+LIFT['throwable'], t['position'][2])
                    if 2 < math.dist(centre, hand) < 55:
                        props.append((math.dist(centre, hand), centre))
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        motion, _ = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
                            ROOT/'reports/motion-modules', ('SpidyMotionStart', 'SpidyMotionStop'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays, report['ray_hash'] = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll',
                                           ROOT/'reports/ray-modules',
                                           ('SpidyRayStart', 'SpidyRayStop', 'SpidySwingStart', 'SpidySwingSubmit',
                                            'SpidySwingStop', 'SpidySwingData', 'SpidyAimSample', 'SpidyAimData',
                                            'SpidySwingSettings', 'SpidyPunchStart', 'SpidyPunchStop'))

        def invoke(address, payload):
            code = call_with_payload(process, address, payload)
            if code:
                raise RuntimeError(f'Native call rejected: {code}')
        invoke(rays['SpidyRayStart'], struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 30000, 0x410))
        rays_active = True
        # Props and bots on offer, as a VR session offers them (game_grab::movableKinds); with --settings none
        # yet, as -NoWebGrab starts a session, until the settings switch the grab on.
        invoke(rays['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record, mover,
                                                    motion_module, 30000, 32., 6.,
                                                    0 if args.settings else (1 << 1) | (1 << 2)))
        swing_active = True
        aims = [('sky', (0, 1, 0))]
        for step in range(8):
            yaw = step*math.pi/4
            aims.append((f'level {step*45}', (math.sin(yaw), 0, -math.cos(yaw))))
        for step in range(0, 8, 2):
            yaw = step*math.pi/4
            aims.append((f'down 30 at {step*45}', (math.sin(yaw)*.866, -.5, -math.cos(yaw)*.866)))
        aims.append(('straight down', (0, -1, 0)))
        if props:
            distance, centre = min(props)
            aims.append((f'prop {distance:.1f} m', tuple((b-a)/distance for a, b in zip(hand, centre))))
        serial = 10
        sample_buffer = bytes(AIM_SIZE)

        def hold(direction):
            """The latest previews while the hand holds this aim."""
            nonlocal serial
            until = time.monotonic()+args.seconds
            last = None
            first = serial
            while time.monotonic() < until:
                serial += 1
                invoke(rays['SpidySwingSubmit'], hand_command(serial, hand, direction))
                # Sampling keeps the previews coming; the module's own copy is read directly.
                invoke(rays['SpidyAimSample'], sample_buffer)
                time.sleep(.03)
                snapshot = aim_snapshot(game, rays['SpidyAimData'])
                if snapshot and snapshot['status'] == 2 and snapshot['serial'] > first:
                    last = snapshot
            return last
        for name, direction in aims:
            last = hold(direction)
            entry = dict(aim=name, direction=[round(x, 3) for x in direction])
            if last:
                first = last['hands'][0]
                entry.update(kind=first['kind'], point=[round(x, 2) for x in first['point']],
                             distance=round(math.dist(first['point'], hand), 2), radius=first['radius'],
                             normal=[round(x, 2) for x in first['normal']], right_hand=last['hands'][1]['kind'])
            else:
                entry['kind'] = None
            entry['swing'] = swing_health(game, rays['SpidySwingData'])
            report['aims'].append(entry)
            print(f"{name:>16}: {entry['kind']!s:>9} {entry.get('distance', '')!s:>7} m  {entry.get('point', '')}",
                  flush=True)
        if args.settings:
            # The VR settings panel during play: the prop aim with the grab off (as started), switched on (the
            # grab starts now, its hooks under the swing's lock), off (presses swing), on again; each with
            # another speed limit. A limit above 65 m/s is refused.
            def settings(grab, speed, air=1):
                return struct.pack('<4IfI', 0x53575354, 2, 24, grab, speed, air)
            report['refused_settings'] = call_with_payload(process, rays['SpidySwingSettings'], settings(1, 70.))
            report['settings'] = []
            if props:
                distance, centre = min(props)
                direction = tuple((b-a)/distance for a, b in zip(hand, centre))
                for grab, speed in ((None, None), (1, 48.), (0, 10.), (1, 32.)):
                    code = None if grab is None else \
                        call_with_payload(process, rays['SpidySwingSettings'], settings(grab, speed))
                    last = hold(direction)
                    entry = dict(grab=grab, speed=speed, code=code, kind=last['hands'][0]['kind'] if last else None,
                                 swing=swing_health(game, rays['SpidySwingData']))
                    report['settings'].append(entry)
                    print(f"grab {grab!s:>4} at {speed!s:>4} m/s: code {code!s:>4}, the prop aim previews "
                          f"{entry['kind']}", flush=True)
            punch = struct.pack('<4IQ', 0x53505543, 1, 24, game.pid, game.base)
            report['punch_codes'] = [call_with_payload(process, rays['SpidyPunchStart'], punch),
                                     call_remote(process, rays['SpidyPunchStop']),
                                     call_with_payload(process, rays['SpidyPunchStart'], punch),
                                     call_remote(process, rays['SpidyPunchStop'])]
            print(f"punching started, stopped, started, stopped: codes {report['punch_codes']}", flush=True)
            # Webs in open air: the sky aim with them off (no web and nothing met: no preview), then on again.
            report['air_webs'] = []
            for air in (0, 1):
                code = call_with_payload(process, rays['SpidySwingSettings'], settings(1, 32., air))
                last = hold((0, 1, 0))
                entry = dict(air=air, code=code, kind=last['hands'][0]['kind'] if last else None,
                             swing=swing_health(game, rays['SpidySwingData']))
                report['air_webs'].append(entry)
                print(f"webs in open air {'on' if air else 'off'}: code {code}, the sky aim previews {entry['kind']}",
                      flush=True)
            report['aims'] += [dict(aim='settings', kind='-', swing=e['swing'])
                               for e in report['settings'] + report['air_webs']]
        report['faults'] = [a for a in report['aims'] if a['swing']['status'] == 4 or a['swing']['error']]
        report['previewed'] = sum(1 for a in report['aims'] if a['kind'])
    finally:
        if swing_active:
            report['swing_stop'] = call_remote(process, rays['SpidySwingStop'])
        if rays_active:
            report['ray_stop'] = call_remote(process, rays['SpidyRayStop'])
        if process:
            close(process)
        try:
            pe = PE(game.path.read_bytes())
            report['entries_restored'] = all(game.read(game.base+rva, 16) == pe.bytes(rva, 16) for rva in ENTRIES)
        except Exception as error:  # the game may have closed
            report['entries_restored'] = str(error)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=1)+'\n')
    print(json.dumps({k: report.get(k) for k in ('previewed', 'faults', 'swing_stop', 'ray_stop', 'entries_restored')}))
    ok = report.get('previewed') == len(report['aims']) and not report.get('faults') and \
        report.get('entries_restored') is True and not report.get('swing_stop') and not report.get('ray_stop')
    if args.settings:
        kinds = [e['kind'] for e in report.get('settings', [])]
        print(json.dumps({k: report.get(k) for k in ('settings', 'refused_settings', 'punch_codes', 'air_webs')}))
        ok = ok and report.get('refused_settings') == 2001 and report.get('punch_codes') == [0, 0, 0, 0] and \
            len(kinds) == 4 and kinds[1] == kinds[3] == 'prop' and 'prop' not in (kinds[0], kinds[2]) and \
            all(e['code'] in (None, 0) for e in report['settings']) and \
            [(e['code'], e['kind']) for e in report.get('air_webs', [])] == [(0, 'none'), (0, 'air')]
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
