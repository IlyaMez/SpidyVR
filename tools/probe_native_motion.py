"""Five-second test of leased native velocity control during one normal jump."""
import ctypes as c
import argparse
import json
import math
import struct
import sys
import time
from capture_game_state import Game, LIVE_VTABLES, find_game, open_process, close
from capture_movement import component
from bridge_game import ROOT, prepare, snapshot as bridge_snapshot, HOOKS
from observe_game import call_remote, call_with_payload
from inspect_game import PE


def snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 176)
        if len(raw) != 176:
            return None
        if struct.unpack_from('<3I', raw) != (0x534d5644, 4, 176):
            raise RuntimeError('Native motion protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        data = dict(zip(('qpc', 'steps', 'controlled', 'serial', 'gravity_corrections'),
                        struct.unpack_from('<5Q', raw, 24)))
        data.update(status=struct.unpack_from('<I', raw, 12)[0],
                    position=struct.unpack_from('<3f', raw, 64),
                    requested=struct.unpack_from('<3f', raw, 76),
                    velocity=struct.unpack_from('<3f', raw, 88),
                    dt=struct.unpack_from('<f', raw, 100)[0])
        data.update(zip(('mover_flags', 'collision_flags', 'thread', 'error'),
                        struct.unpack_from('<4I', raw, 104)))
        data.update(zip(('air_events', 'air_overrides', 'air_state'), struct.unpack_from('<3Q', raw, 120)))
        data['air_velocity'] = struct.unpack_from('<3f', raw, 144)
        data.update(zip(('air_vertical', 'air_gravity', 'air_dt'), struct.unpack_from('<3f', raw, 156)))
        data['grounded'] = bool(struct.unpack_from('<I', raw, 168)[0])
        data['contact'] = struct.unpack_from('<I', raw, 172)[0]
        return data
    return None


def assess(samples):
    # Feedback describes the previous physics step. Exclude the first driven
    # sample and duplicate external reads before comparing against the command.
    unique = {s['steps']: s for s in samples if s['status'] == 2 and s['serial'] == 1}
    driven = [unique[k] for k in sorted(unique)]
    settled = driven[1:]
    errors = [math.dist(s['velocity'], (.5, 0, 0)) for s in settled]
    drift = max((s['position'][1] for s in driven), default=0)-min(
        (s['position'][1] for s in driven), default=0)
    peak = max(errors, default=float('inf'))
    return dict(observed_controlled_steps=len(driven), max_velocity_error=peak if math.isfinite(peak) else None,
                vertical_drift=drift, velocity_verified=len(settled) >= 8 and peak < .03 and drift < .01)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sync-air', action='store_true')
    args = parser.parse_args()
    game = Game(find_game())
    process = bridge = motion = None
    bridge_active = motion_active = False
    try:
        pe = PE(game.path.read_bytes())
        entries = (*HOOKS, 0x1fbe360, 0x1fbda50, 0x1fc2e10, 0x1fb88d0, 0x16769f0, 0xa7b3a0, 0x1f9db60)
        if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in entries):
            raise RuntimeError('A required native motion entry is already patched')
        LIVE_VTABLES['hero_mover'] = 0x38b2c98
        found = game.registered_candidates()
        heroes = [x for x in found if x['kind'] == 'hero_local']
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required')
        hero = heroes[0]
        movers = [x for x in found if x['kind'] == 'hero_mover' and x['actor_record'] == hero['actor_record']]
        if len(movers) != 1:
            raise RuntimeError('Exactly one local movement manager is required')
        handle = struct.unpack('<I', game.read(int(movers[0]['object'], 16)+0xdb4, 4))[0]
        mover = component(game, handle)
        if game.pointer(mover) != game.base+0x4f70168 or game.pointer(mover+8) != int(hero['actor_record'], 16):
            raise RuntimeError('Local movement ownership failed')
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        bridge, _ = prepare(game.pid, process)
        code = call_with_payload(process, bridge['SpidyStart'], struct.pack('<4I3Q',
            0x53424346, 1, 40, game.pid, game.base, int(hero['object'], 16), int(hero['actor_record'], 16)))
        if code:
            raise RuntimeError(f'Input bridge start: {code}')
        bridge_active = True
        motion, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
            ROOT/'reports/motion-modules', ('SpidyMotionStart', 'SpidyMotionSubmit', 'SpidyMotionStop', 'SpidyMotionData'))
        code = call_with_payload(process, motion['SpidyMotionStart'], struct.pack('<4I3QIf2I',
            0x534d5643, 2, 56, game.pid, game.base, int(hero['actor_record'], 16), mover, 5000, 2.0,
            int(args.sync_air), 0))
        if code:
            raise RuntimeError(f'Native motion start: {code}')
        motion_active = True
        started = time.monotonic()
        jumped = sent = False
        sent_at = 0
        samples = []
        origin = game.transform(int(hero['actor_transform'], 16))['position']
        print(f'Testing one 200 ms movement lease; air synchronization={args.sync_air}.', flush=True)
        while time.monotonic()-started < 5.1:
            elapsed = time.monotonic()-started
            state = snapshot(game, motion['SpidyMotionData'])
            if state:
                state['seconds'] = elapsed
                samples.append(state)
            if not jumped and elapsed >= 1:
                b = bridge_snapshot(game, bridge['SpidyBridgeData'])
                if not b or b['state'] != 1 or not b['matched']:
                    raise RuntimeError('Camera no longer follows the local hero')
                code = call_with_payload(process, bridge['SpidySubmit'],
                    struct.pack('<4IQ2I8f', 0x53424354, 1, 64, 1, 1, 350, 16, *([0.0]*7), 1.0))
                if code:
                    raise RuntimeError(f'Native jump rejected: {code}')
                jumped = True
            if jumped and not sent and elapsed >= 1.5 and state and state['steps'] >= 30 and (
                    state['position'][1] > origin[1]+1 and not state['mover_flags'] & 0x80000000):
                # Exactly one lease: 0.5 m/s sideways, zero vertical velocity, for 200 ms.
                payload = struct.pack('<4IQ2I3fI', 0x534d564d, 1, 48, 1, 1, 200, 0, .5, 0, 0, 0)
                code = call_with_payload(process, motion['SpidyMotionSubmit'], payload)
                if code:
                    raise RuntimeError(f'Native velocity command rejected: {code}')
                sent = True
                sent_at = elapsed
            time.sleep(.008)
        motion_stop = call_remote(process, motion['SpidyMotionStop'])
        motion_active = motion_stop != 0
        bridge_stop = call_remote(process, bridge['SpidyStop'])
        bridge_active = bridge_stop != 0
        final = snapshot(game, motion['SpidyMotionData'])
        restored = all(game.read(game.base+rva, 16) == pe.bytes(rva, 16) for rva in entries)
        expired = any(s['seconds'] > sent_at+.4 and s['status'] == 1 and s['serial'] == 0 for s in samples)
        assessment = assess(samples)
        unique = {s['steps']: s for s in samples}
        ordered = [unique[k] for k in sorted(unique)]
        last_driven = max((i for i, s in enumerate(ordered) if s['status'] == 2), default=-1)
        # The first uncontrolled sample still measures the last controlled step.
        release_steps = ordered[last_driven+2:last_driven+7] if last_driven >= 0 else []
        assessment['release_velocities'] = [s['velocity'] for s in release_steps]
        assessment['release_vertical_peak'] = max((abs(s['velocity'][1]) for s in release_steps), default=None)
        assessment['air_handoff_verified'] = bool(final and final['air_overrides'] >= 8 and
            len(release_steps) == 5 and assessment['release_vertical_peak'] < 3)
        result = dict(pid=game.pid, module_base=hex(game.base), dll_hash=digest, origin=origin,
            sent=sent, sent_at=sent_at, samples=samples, final=final, motion_stop=motion_stop,
            bridge_stop=bridge_stop, restored=restored, expired=expired, assessment=assessment,
            air_sync=args.sync_air)
        output = 'native-motion-handoff.json' if args.sync_air else 'native-motion-lease-v2.json'
        (ROOT/'reports'/output).write_text(json.dumps(result, indent=2)+'\n')
        print(dict(sent=sent, final=final, samples=len(samples), expired=expired, restored=restored,
                   motion_stop=motion_stop, bridge_stop=bridge_stop, assessment=assessment))
        return 0 if sent and final and final['controlled'] > 0 and expired and restored and (
            not motion_stop and not bridge_stop and assessment['velocity_verified'] and
            (not args.sync_air or assessment['air_handoff_verified'])) else 1
    finally:
        if motion_active:
            call_remote(process, motion['SpidyMotionStop'])
        if bridge_active:
            call_remote(process, bridge['SpidyStop'])
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    sys.exit(main())
