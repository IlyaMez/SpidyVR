"""The web shooter in the running game, without a headset: scripted trigger pulls and the shots they fire.

    python tools/probe_shooter.py

It needs a freshly started game in free roam (Spidy's ray and movement modules start once per process) and the game
window in front, which it brings there. A scripted left hand 1.2 m above the player's feet pulls its trigger, through
the swing's input commands as the headset's controllers do, aimed level along the game camera, 30 degrees down, at
the sky and to either side. For each pull it reads the web shooter's telemetry (SpidyShooterData: a shot fired, where
it left and where it was aimed) and follows the ShotWebShooter actor the game spawned: where it started, which way and
how fast it flew, where it ended. Then a trigger held for a second (one shot), three pulls 0.2 s apart (three shots),
the nearest enemy within 40 m if there is one (SpidyShooterTest at its actor: the shot goes to him and the game takes
him as its target), and the shooter stopped and started again. It checks that nothing faulted and every hook entry is
restored, and writes reports/shooter-probe.json with a screenshot after each pull.
"""
import argparse
import ctypes as c
import json
import math
import pathlib
import struct
import sys
import threading
import time
from bridge_game import ROOT, prepare
from capture_game_state import Game, find_game, open_process, close
from inspect_game import PE
from observe_game import call_remote, call_with_payload, modules
from probe_aim import swing_health
from probe_game_grab import (BOT_MOVERS, ENTRIES, FRIENDLY, HERO_LOCAL, HERO_MOVERS, camera, hand_command, registry,
                             resolve, shot)
from probe_game_screen import user32
from run_game_vr import shooter_snapshot
from vr_launcher import bring_to_front

# The shooter's hooks: the camera's update (shots go out on the main thread) and the weapons' muzzle.
SHOOTER_ENTRIES = (0x897d30, 0x2150c40)
SHOT_WEB_SHOOTER = 0x3907d30


def follow(game, component, seconds=1.6):
    """The flight of the shot whose ShotWebShooter component is `component`: (seconds, position) samples."""
    path = []
    record = game.pointer(component+8) if component else 0
    start = time.monotonic()
    while record and time.monotonic()-start < seconds:
        if game.pointer(component) != game.base+SHOT_WEB_SHOOTER or game.pointer(component+8) != record:
            break  # the shot ended and its component went back to the game's pool
        t = game.transform(game.pointer(record))
        if t:
            point = [round(v, 3) for v in t['position']]
            if not path or point != path[-1][1:]:
                path.append([round(time.monotonic()-start, 4), *point])
        time.sleep(.005)
    return path


def flight(path, origin, aim_point):
    """Where a followed shot started and ended, its direction and speed, and how far it ended from its aim."""
    if len(path) < 2:
        return dict(samples=len(path))
    (t0, *a), (t1, *b) = path[0], path[-1]
    travel = [y-x for x, y in zip(a, b)]
    metres = math.sqrt(sum(v*v for v in travel))
    aim = [y-x for x, y in zip(origin, aim_point)]
    aim_length = math.sqrt(sum(v*v for v in aim)) or 1
    direction = [v/metres for v in travel] if metres > 1e-3 else None
    return dict(samples=len(path), start=a, end=b, metres=round(metres, 2),
                speed=round(metres/(t1-t0), 1) if t1 > t0 else None,
                direction=[round(v, 3) for v in direction] if direction else None,
                off_aim_degrees=round(math.degrees(math.acos(max(-1, min(1, sum(
                    d*v/aim_length for d, v in zip(direction, aim)))))), 2) if direction else None,
                start_from_origin=round(math.dist(a, origin), 2), end_from_aim=round(math.dist(b, aim_point), 2))


def main():
    try:
        user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    except AttributeError:
        user32.SetProcessDPIAware()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/shooter-probe.json')
    args = parser.parse_args()
    game = Game(find_game())
    process = None
    rays = None
    rays_active = swing_active = False
    report = dict(pulls=[])
    entries = (*ENTRIES, *SHOOTER_ENTRIES)
    try:
        pe = PE(game.path.read_bytes())
        if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in entries):
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
        bots = []
        # Enemies only: the shots web up no civilian or police officer.
        friendly = {r for a, v, r, _ in components if v in FRIENDLY}
        for a, v, r, _ in components:
            if v == BOT_MOVERS and r not in friendly:
                t = game.transform(game.pointer(r))
                if t and 3 < math.dist(t['position'], hand) < 40:
                    bots.append((math.dist(t['position'], hand), r, t['position']))
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll', ROOT/'reports/motion-modules',
                ('SpidyMotionStart', 'SpidyMotionStop'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays, report['ray_hash'] = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll',
                                           ROOT/'reports/ray-modules',
                                           ('SpidyRayStart', 'SpidyRayStop', 'SpidySwingStart', 'SpidySwingSubmit',
                                            'SpidySwingStop', 'SpidySwingData', 'SpidyShooterStart',
                                            'SpidyShooterStop', 'SpidyShooterTest', 'SpidyShooterData'))

        def invoke(address, payload):
            code = call_with_payload(process, address, payload)
            if code:
                raise RuntimeError(f'Native call rejected: {code}')
        invoke(rays['SpidyRayStart'], struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 30000, 0x410))
        rays_active = True
        invoke(rays['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record, mover,
                                                    motion_module, 30000, 32., 6., 0))
        swing_active = True
        start_shooter = struct.pack('<4IQ', 0x53484f43, 1, 24, game.pid, game.base)
        report['shooter_start'] = call_with_payload(process, rays['SpidyShooterStart'], start_shooter)
        if report['shooter_start']:
            raise RuntimeError(f"Shooter start: {report['shooter_start']}")
        serial = 10

        def hold(direction, trigger, seconds):
            """The hand aimed `direction` with its trigger at `trigger`, a command every 30 ms."""
            nonlocal serial
            until = time.monotonic()+seconds
            while time.monotonic() < until:
                serial += 1
                invoke(rays['SpidySwingSubmit'], hand_command(serial, hand, direction, trigger=trigger))
                time.sleep(.03)

        def sample():
            for _ in range(8):
                shots = shooter_snapshot(game, rays['SpidyShooterData'])
                if shots:
                    return shots
            return None

        def pull(name, direction, held=.25):
            """One pull: open, pulled for `held` seconds, open again; the shot it fired and its flight, followed
            from the moment the module reports it (a shot at the ground nearby lives 50 ms)."""
            hold(direction, 0, .15)
            before = sample()
            seen = {}

            def watch():
                until = time.monotonic()+held+1
                while time.monotonic() < until:
                    now = sample()
                    if now and now['fired'] > before['fired']:
                        seen['shot'] = now
                        seen['path'] = follow(game, int(now['last_shot'], 16))
                        return
                    time.sleep(.002)
            watcher = threading.Thread(target=watch, daemon=True)
            watcher.start()
            hold(direction, 1, held)
            hold(direction, 0, .06)
            watcher.join(3)
            after = sample()
            entry = dict(pull=name, direction=[round(x, 3) for x in direction], fired=after['fired']-before['fired'],
                         dropped=after['dropped']-before['dropped'], requested=after['requested']-before['requested'])
            if 'shot' in seen:
                first, path = seen['shot'], seen['path']
                entry.update(origin=first['last_origin'], aim_point=first['last_aim_point'], shot=first['last_shot'],
                             flight=flight(path, first['last_origin'], first['last_aim_point']), path=path)
            entry['swing'] = swing_health(game, rays['SpidySwingData'])
            entry['screenshot'] = shot(game, f"shooter-{name.replace(' ', '-')}")
            report['pulls'].append(entry)
            f = entry.get('flight', {})
            print(f"{name:>14}: fired {entry['fired']} dropped {entry['dropped']}  "
                  f"{f.get('metres', '-')} m at {f.get('speed', '-')} m/s, {f.get('off_aim_degrees', '-')} deg off "
                  f"the aim, started {f.get('start_from_origin', '-')} m from the hand, ended "
                  f"{f.get('end_from_aim', '-')} m from the aim point", flush=True)
            return entry
        forward, _ = camera(game)
        yaw = math.atan2(forward[0], forward[2]) if forward else 0.
        level = (math.sin(yaw), 0., math.cos(yaw))
        aims = [('level', level),
                ('down 30', (math.sin(yaw)*.866, -.5, math.cos(yaw)*.866)),
                ('sky', (math.sin(yaw)*.5, .866, math.cos(yaw)*.5)),
                ('left 60', (math.sin(yaw+1.047), 0., math.cos(yaw+1.047))),
                ('right 60', (math.sin(yaw-1.047), 0., math.cos(yaw-1.047)))]
        for name, direction in aims:
            pull(name, direction)
        # A trigger held for a second shoots once.
        report['held'] = pull('held 1 s', level, held=1.)['fired']
        # Three pulls 0.2 s apart shoot three times.
        before = sample()
        for _ in range(3):
            hold(level, 0, .1)
            hold(level, 1, .1)
        hold(level, 0, .06)
        after = sample()
        report['quick_pulls'] = after['fired']-before['fired']
        print(f"three quick pulls: {report['quick_pulls']} shots", flush=True)
        # A bot, through the probe's test shot: aimed at his actor, as the aim assist aims.
        if bots:
            distance, bot, at = min(bots)
            test = struct.pack('<4I6fQ', 0x53484f54, 1, 48, 1, *hand, at[0], at[1]+1.15, at[2], bot)
            before = sample()
            code = call_with_payload(process, rays['SpidyShooterTest'], test)
            after = sample()
            path = follow(game, int(after['last_shot'], 16)) if code == 0 else []
            report['bot'] = dict(distance=round(distance, 1), record=hex(bot), code=code,
                                 targeted=after['targeted']-before['targeted'],
                                 resolved=after['resolved']-before['resolved'],
                                 flight=flight(path, hand, (at[0], at[1]+1.15, at[2])), path=path,
                                 screenshot=shot(game, 'shooter-bot'))
            print(f"bot {distance:.1f} m away: code {code}, target taken {report['bot']['resolved']}, "
                  f"{report['bot']['flight']}", flush=True)
        else:
            report['bot'] = None
            print('no bot within 40 m: the targeted shot is untested', flush=True)
        # Switched off and on again during play, as the VR settings do.
        report['restart_codes'] = [call_remote(process, rays['SpidyShooterStop']),
                                   call_with_payload(process, rays['SpidyShooterStart'], start_shooter)]
        report['after_restart'] = pull('after restart', level)['fired']
        report['final'] = sample()
        report['faults'] = [p['pull'] for p in report['pulls'] if p['swing']['status'] == 4 or p['swing']['error']]
    finally:
        if swing_active:
            report['swing_stop'] = call_remote(process, rays['SpidySwingStop'])
        if rays_active:
            report['ray_stop'] = call_remote(process, rays['SpidyRayStop'])
        if process:
            close(process)
        try:
            pe = PE(game.path.read_bytes())
            report['entries_restored'] = all(game.read(game.base+rva, 16) == pe.bytes(rva, 16) for rva in entries)
        except Exception as error:  # the game may have closed
            report['entries_restored'] = str(error)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=1)+'\n')
    print(json.dumps({k: report.get(k) for k in ('held', 'quick_pulls', 'after_restart', 'restart_codes', 'faults',
                                                 'swing_stop', 'ray_stop', 'entries_restored')}))
    pulls = report['pulls'][:5]
    ok = all(p['fired'] == 1 and p.get('flight', {}).get('samples', 0) >= 2 for p in pulls) and \
        report.get('held') == 1 and report.get('quick_pulls') == 3 and report.get('after_restart') == 1 and \
        report.get('restart_codes') == [0, 0] and not report.get('faults') and \
        report.get('entries_restored') is True and not report.get('swing_stop') and not report.get('ray_stop')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
