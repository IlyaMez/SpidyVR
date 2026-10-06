"""Bounded native web attachment, release, and collision-controlled flight test."""
import argparse
import ctypes as c
import json
import math
import pathlib
import struct
import sys
import time
from bridge_game import ROOT, HOOKS, prepare, snapshot as bridge_snapshot
from capture_game_state import Game, LIVE_VTABLES, find_game, open_process, close
from capture_movement import component
from observe_game import call_remote, call_with_payload, modules
from inspect_game import PE
from probe_native_motion import snapshot as motion_snapshot
from probe_native_rays import snapshot as ray_snapshot, command as ray_command


def snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 240)
        if len(raw) != 240:
            return None
        if struct.unpack_from('<3I', raw) != (0x53574441, 3, 240):
            raise RuntimeError('Native swing protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        result = dict(zip(('qpc', 'steps', 'controlled', 'serial', 'attaches', 'releases', 'zips', 'world', 'source_step'),
                          struct.unpack_from('<9Q', raw, 24)))
        result['status'] = struct.unpack_from('<I', raw, 12)[0]
        result['position'] = struct.unpack_from('<3f', raw, 96)
        result['velocity'] = struct.unpack_from('<3f', raw, 108)
        result['requested'] = struct.unpack_from('<3f', raw, 120)
        result['dt'], result['owned'], result['error'] = struct.unpack_from('<f2I', raw, 132)
        result['webs'] = []
        for index in range(2):
            attached, body, x, y, z, length, tension = struct.unpack_from('<2I5f', raw, 144+index*28)
            result['webs'].append(dict(attached=bool(attached), body_id=body, anchor=(x,y,z), length=length, tension=tension))
        result.update(zip(('grounded','collision_flags','takeoff','misses','obstructed','tracking_lost'), struct.unpack_from('<6I',raw,200)))
        result.update(zip(('takeoff_phase','takeoff_attempts','takeoff_timeouts','native_contact'), struct.unpack_from('<4I',raw,224)))
        return result
    return None


def command(serial, origin, anchor=None, held=False, focused=True, sample_seconds=.01):
    raw = bytearray(168)
    struct.pack_into('<4IQ2I', raw, 0, 0x5357434d, 2, 168, int(focused), serial, 100, 0)
    quaternion = (0,0,0,1)
    if anchor:
        delta = [a-o for a,o in zip(anchor, origin)]
        distance = math.sqrt(sum(x*x for x in delta))
        dx,dy,dz = [x/distance for x in delta]
        quaternion = (dy,-dx,0,1-dz)
        magnitude = math.sqrt(sum(x*x for x in quaternion))
        quaternion = tuple(x/magnitude for x in quaternion) if magnitude > 1e-6 else (0,1,0,0)
    struct.pack_into('<10fI2f', raw, 32, *origin, *quaternion, 0,0,0, 1, float(held), float(held))
    struct.pack_into('<10fI2f', raw, 84, *origin, 0,0,0,1, 0,0,0, 0,0,0)
    struct.pack_into('<fI', raw, 152, sample_seconds, 0)
    # Sample times must increase with every command; time.monotonic_ns() repeats within a timer tick.
    struct.pack_into('<Q', raw, 160, time.perf_counter_ns())
    return bytes(raw)


def flight_summary(motion_samples):
    """How steadily the game followed the swing's velocity commands.

    Consecutive controlled airborne steps should differ only by gravity, steering, and rope
    tension. Steering from one-step-old state made even and odd steps two separate trajectories,
    whose velocities differed by metres per second; `alternation_mps` measures that zigzag as the
    mean size of the second difference of the achieved velocity.

    `steps_without_command` counts native steps in the middle of controlled flight that ran
    without the command because its lease had expired. The game moves the body at its own fall
    speed in such a step: a drop of about a metre, then a snap back.
    """
    controlled = [m for m in motion_samples if m['status'] == 2 and m['contact'] == 2]
    dropped = sum(max(0, (b['steps']-a['steps'])-(b['controlled']-a['controlled']))
                  for a, b in zip(controlled, controlled[1:]) if 0 < b['steps']-a['steps'] <= 12)
    runs, run = [], []
    for sample in controlled:
        if run and sample['steps'] != run[-1]['steps']+1:
            runs.append(run)
            run = []
        run.append(sample)
    if run:
        runs.append(run)
    zigzag, steps, fastest = 0., 0, 0.
    for run in runs:
        for a, b, d in zip(run, run[1:], run[2:]):
            second = [x-2*y+z for x, y, z in zip(a['velocity'], b['velocity'], d['velocity'])]
            zigzag += math.sqrt(sum(x*x for x in second))
            steps += 1
        fastest = max([fastest]+[math.sqrt(sum(x*x for x in m['velocity'])) for m in run])
    return dict(controlled_air_steps=len(controlled), consecutive_triples=steps,
                alternation_mps=zigzag/steps if steps else None, fastest_mps=fastest,
                steps_without_command=dropped)


def assess(samples, final, restored, stops):
    unique = {s['source_step']:s for s in samples}
    flight = [s for s in unique.values() if s['owned'] and not any(w['attached'] for w in s['webs'])]
    passed = bool(final and final['attaches'] and final['releases'] and final['controlled'] >= 20 and
        len(flight) >= 5 and not final['error'] and not any(s['error'] for s in samples) and
        restored and not any(stops))
    return dict(passed=passed,release_flight_steps=len(flight))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--from-ground', action='store_true')
    parser.add_argument('--speed', type=float, default=8, help='swing speed cap in m/s (the VR launcher uses 32)')
    parser.add_argument('--gravity', type=float, default=9.81, help='swing gravity in m/s^2 (VR uses 6)')
    parser.add_argument('--hold', type=float, help='seconds to hold the web (default 1, or 2.5 from the ground)')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    if not 1 <= args.speed <= 65 or not 0 <= args.gravity <= 30 or (args.hold is not None and not .2 <= args.hold <= 4.5):
        parser.error('Use 1..65 m/s, 0..30 m/s^2, and a hold of 0.2..4.5 seconds')
    hold = args.hold if args.hold is not None else (2.5 if args.from_ground else 1.0)
    game = Game(find_game())
    process = None
    bridge = rays = swing = motion = None
    bridge_active = rays_active = swing_active = False
    try:
        pe = PE(game.path.read_bytes())
        entries = (*HOOKS, 0x2e67010, 0x1fbe360, 0x1fbda50)
        if any(game.read(game.base+rva, 16) != pe.bytes(rva,16) for rva in entries):
            raise RuntimeError('A required entry is already patched')
        LIVE_VTABLES['hero_mover'] = 0x38b2c98
        objects = game.registered_candidates()
        heroes = [o for o in objects if o['kind'] == 'hero_local']
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required')
        hero = heroes[0]
        managers = [o for o in objects if o['kind'] == 'hero_mover' and o['actor_record'] == hero['actor_record']]
        if len(managers) != 1:
            raise RuntimeError('Exactly one local movement manager is required')
        handle = struct.unpack('<I', game.read(int(managers[0]['object'],16)+0xdb4,4))[0]
        mover = component(game,handle)
        process = open_process(0x0400|0x0010|0x0020|0x0008|0x0002,False,game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        bridge,_ = prepare(game.pid,process)
        def invoke(address,payload):
            code = call_with_payload(process,address,payload)
            if code:
                raise RuntimeError(f'Native call rejected: {code}')
        invoke(bridge['SpidyStart'],struct.pack('<4I3Q',0x53424346,1,40,game.pid,
            game.base,int(hero['object'],16),int(hero['actor_record'],16)))
        bridge_active = True
        motion,motion_hash = prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
            ROOT/'reports/motion-modules',('SpidyMotionStart','SpidyMotionSample','SpidyMotionData','SpidyMotionStop'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays,ray_hash = prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_ray_bridge.dll',
            ROOT/'reports/ray-modules',('SpidyRayStart','SpidyRaySubmit','SpidyRayData','SpidyRayStop',
                'SpidySwingStart','SpidySwingSubmit','SpidySwingStop','SpidySwingData'))
        swing = rays
        invoke(rays['SpidyRayStart'],struct.pack('<4IQ2I',0x53525943,1,32,game.pid,game.base,8000,0x410))
        rays_active = True
        invoke(swing['SpidySwingStart'],struct.pack('<4I4QI2fI',0x53574346,1,64,game.pid,
            game.base,int(hero['actor_record'],16),mover,motion_module,6000,args.speed,args.gravity,0))
        swing_active = True
        origin = game.transform(int(hero['actor_transform'],16))['position']
        started = time.monotonic()
        jumped = queried = False
        anchor = None
        serial = 0
        attach_at = None
        samples = []
        motion_samples = []
        takeoff_sent = False
        bridge_serial = 0
        print('Testing native web attachment, release, and flight for six seconds.',flush=True)
        while (elapsed := time.monotonic()-started) < 5.8:
            m = motion_snapshot(game,motion['SpidyMotionData'])
            s = snapshot(game,swing['SpidySwingData'])
            if m and m['steps'] and (not motion_samples or m['steps'] != motion_samples[-1]['steps']):
                motion_samples.append(dict(seconds=elapsed, **m))
            if args.from_ground:
                # Follow both the press and release phases, just like the XR worker.
                bridge_serial += 1
                keys = 16 if s and s['takeoff'] else 0
                invoke(bridge['SpidySubmit'],struct.pack('<4IQ2I8f',0x53424354,1,64,1,bridge_serial,100,keys,*([0.]*7),1.))
                takeoff_sent |= bool(keys)
            if s:
                s['seconds'] = elapsed
                samples.append(s)
                if s['error']:
                    break
            if not args.from_ground and not jumped and elapsed >= .25:
                state = bridge_snapshot(game,bridge['SpidyBridgeData'])
                if not state or state['state'] != 1 or not state['matched']:
                    raise RuntimeError('Camera is not following the local hero')
                invoke(bridge['SpidySubmit'],struct.pack('<4IQ2I8f',0x53424354,1,64,1,1,350,16,*([0.]*7),1.))
                jumped = True
            if m and m['steps']:
                hand = (m['position'][0],m['position'][1]+1.2,m['position'][2])
                if not queried and elapsed >= .8 and (args.from_ground or (jumped and m['position'][1] > origin[1]+1)):
                    elevation = .6 if args.from_ground else 0
                    horizontal = math.sqrt(1-elevation*elevation)
                    candidates = [(hand,(horizontal*math.cos(i*math.pi/4),elevation,horizontal*math.sin(i*math.pi/4)),80,i) for i in range(8)]
                    invoke(rays['SpidyRaySubmit'],ray_command(1,candidates,250))
                    queried = True
                if queried and anchor is None:
                    targets = ray_snapshot(game,rays['SpidyRayData'])
                    if targets and targets['serial'] == 1 and targets['status'] == 2 and not targets['error']:
                        valid = [h for h in targets['hits'] if h['fixed'] and 3 < h['fraction']*h['distance'] < 70]
                        if valid:
                            anchor = max(valid,key=lambda h:h['fraction'])['position']
                            attach_at = elapsed
                held = attach_at is not None and elapsed-attach_at < hold
                serial += 1
                invoke(swing['SpidySwingSubmit'],command(serial,hand,anchor,held))
            time.sleep(.01)
        stop_swing = call_remote(process,swing['SpidySwingStop'])
        swing_active = stop_swing != 0
        stop_rays = call_remote(process,rays['SpidyRayStop'])
        rays_active = stop_rays != 0
        stop_bridge = call_remote(process,bridge['SpidyStop'])
        bridge_active = stop_bridge != 0
        final = snapshot(game,swing['SpidySwingData'])
        restored = all(game.read(game.base+rva,16) == pe.bytes(rva,16) for rva in entries)
        assessment = assess(samples,final,restored,(stop_swing,stop_rays,stop_bridge))
        report = dict(pid=game.pid,ray_hash=ray_hash,motion_hash=motion_hash,origin=origin,anchor=anchor,
            attach_at=attach_at,samples=samples,motion_samples=motion_samples,takeoff_sent=takeoff_sent,from_ground=args.from_ground,final=final,restored=restored,
            speed=args.speed,gravity=args.gravity,hold=hold,flight=flight_summary(motion_samples),
            stop_swing=stop_swing,stop_rays=stop_rays,stop_bridge=stop_bridge,**assessment)
        output = args.output or ROOT/('reports/native-swing-ground-repair.json' if args.from_ground else 'reports/native-swing-flight.json')
        output.write_text(json.dumps(report,indent=2)+'\n')
        print(dict(flight=report['flight']))
        print(dict(final=final,restored=restored,**assessment))
        return 0 if assessment['passed'] else 1
    finally:
        if swing_active: call_remote(process,swing['SpidySwingStop'])
        if rays_active: call_remote(process,rays['SpidyRayStop'])
        if bridge_active: call_remote(process,bridge['SpidyStop'])
        if process: close(process)
        game.close()


if __name__ == '__main__':
    sys.exit(main())
