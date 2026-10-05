"""Observe native movement requests, optionally add one bounded 4 cm upward request."""
import argparse
import ctypes as c
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
        raw = game.read(address, 57408)
        if len(raw) != 57408:
            return None
        if struct.unpack_from('<3I', raw) != (0x534d5044, 2, 57408):
            raise RuntimeError('Movement probe protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        data = dict(zip(('calls', 'matched', 'applied', 'last_serial', 'count', 'error'),
                        struct.unpack_from('<4Q2I', raw, 24)))
        if data['count'] > 512:
            raise RuntimeError('Movement probe sample overflow')
        data['samples'] = []
        for i in range(data['count']):
            values = struct.unpack_from('<3Q12f2I3f3IQ', raw, 64+i*112)
            data['samples'].append(dict(qpc=values[0], caller_rva=hex(values[1]-game.base),
                serial=values[2], position=values[3:6], before=values[6:9], after=values[9:12],
                forward=values[12:15], thread=values[15], flags=values[16],
                requested=values[17:20], mover_flags=values[20], ground_flags=values[21],
                ready=values[22], result=hex(values[23])))
        return data
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nudge', action='store_true')
    parser.add_argument('--jump', action='store_true', help='One native jump, with any nudge gated on airborne height')
    parser.add_argument('--axis', choices=('x', 'y', 'z'), default='y')
    args = parser.parse_args()
    game = Game(find_game())
    process = exports = None
    stopped = False
    bridge = None
    bridge_stopped = True
    try:
        pe = PE(game.path.read_bytes())
        original = pe.bytes(0x1fc2e10, 16)
        if game.read(game.base+0x1fc2e10, 16) != original:
            raise RuntimeError('Movement request entry already patched')
        LIVE_VTABLES['hero_mover'] = 0x38b2c98
        found = game.registered_candidates()
        heroes = [x for x in found if x['kind'] == 'hero_local']
        if len(heroes) != 1:
            raise RuntimeError('Exactly one registered local hero is required')
        hero = heroes[0]
        movers = [x for x in found if x['kind'] == 'hero_mover' and x['actor_record'] == hero['actor_record']]
        if len(movers) != 1:
            raise RuntimeError('Exactly one local hero mover is required')
        handle = struct.unpack('<I', game.read(int(movers[0]['object'], 16)+0xdb4, 4))[0]
        mover = component(game, handle)
        if game.pointer(mover) != game.base+0x4f70168 or game.pointer(mover+8) != int(hero['actor_record'], 16):
            raise RuntimeError('Local MoverStandard ownership failed validation')
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        if args.jump:
            if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in HOOKS):
                raise RuntimeError('Input bridge entry already patched')
            bridge, _ = prepare(game.pid, process)
            payload = struct.pack('<4I3Q', 0x53424346, 1, 40, game.pid, game.base,
                                  int(hero['object'], 16), int(hero['actor_record'], 16))
            code = call_with_payload(process, bridge['SpidyStart'], payload)
            if code:
                raise RuntimeError(f'Input bridge start: {code}')
            bridge_stopped = False
        exports, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_probe.dll',
            ROOT/'reports/movement-modules', ('SpidyStart', 'SpidyStop', 'SpidySubmitMovement', 'SpidyMovementData'))
        config = struct.pack('<4I3Q2I', 0x534d5043, 1, 48, game.pid, game.base,
                             int(hero['actor_record'], 16), mover, 5000, 0)
        code = call_with_payload(process, exports['SpidyStart'], config)
        if code:
            raise RuntimeError(f'Movement probe start: {code}')
        started = time.monotonic()
        sent = False
        jumped = False
        positions = []
        while time.monotonic()-started < 5.1:
            state = snapshot(game, exports['SpidyMovementData'])
            transform = game.transform(int(hero['actor_transform'], 16))
            if transform:
                raw = game.read(mover+0x750, 4)
                positions.append(dict(seconds=time.monotonic()-started, position=transform['position'],
                    mover_flags=struct.unpack('<I', raw)[0] if len(raw) == 4 else None))
            if args.jump and not jumped and time.monotonic()-started >= 1:
                b = bridge_snapshot(game, bridge['SpidyBridgeData'])
                if not b or b['state'] != 1 or not b['matched']:
                    raise RuntimeError('The camera does not follow the validated hero; jump skipped')
                payload = struct.pack('<4IQ2I8f', 0x53424354, 1, 64, 1, 1, 350, 16, *([0.0]*7), 1.0)
                code = call_with_payload(process, bridge['SpidySubmit'], payload)
                if code:
                    raise RuntimeError(f'Native jump rejected: {code}')
                jumped = True
            airborne = (jumped and transform and positions and
                        transform['position'][1] > positions[0]['position'][1]+1)
            if args.nudge and not sent and time.monotonic()-started >= 1.5 and state and state['matched'] >= 30 and (
                    not args.jump or airborne):
                recent = state['samples'][-30:]
                # Idle probes require stationary requests. Jump probes require small, enabled requests.
                bound = .25 if args.jump else .01
                safe = len({s['thread'] for s in recent}) == 1 and all(
                    not s['mover_flags'] & 0x80000000 and all(math.isfinite(v) for v in s['before']) and
                    sum(v*v for v in s['before']) < bound*bound
                    for s in recent)
                if not safe:
                    time.sleep(.02)
                    continue
                delta = [0.0]*3
                delta['xyz'.index(args.axis)] = .04
                command = struct.pack('<4IQ3fI', 0x534d4344, 1, 40, 250, 1, *delta, 0)
                code = call_with_payload(process, exports['SpidySubmitMovement'], command)
                if code:
                    raise RuntimeError(f'Movement nudge rejected: {code}')
                sent = True
            time.sleep(.02)
        code = call_remote(process, exports['SpidyStop'])
        stopped = code == 0
        state = snapshot(game, exports['SpidyMovementData'])
        restored = game.read(game.base+0x1fc2e10, 16) == original
        if bridge:
            bridge_stopped = call_remote(process, bridge['SpidyStop']) == 0
            restored &= all(game.read(game.base+rva, 16) == pe.bytes(rva, 16) for rva in HOOKS)
        result = dict(pid=game.pid, module_base=hex(game.base), dll_hash=digest, mover=hex(mover),
                      nudge_requested=args.nudge, sent=sent, final=state, positions=positions,
                      stopped=stopped, entry_restored=restored, jump_requested=args.jump,
                      jumped=jumped, bridge_stopped=bridge_stopped, axis=args.axis)
        name = 'movement-'+('jump-' if args.jump else '')+('nudge' if args.nudge else 'requests')+'.json'
        path = ROOT/'reports'/name
        path.write_text(json.dumps(result, indent=2)+'\n')
        summary = {k: v for k, v in (state or {}).items() if k != 'samples'}
        summary.update(sent=sent, stopped=stopped, restored=restored,
            callers=sorted({s['caller_rva'] for s in state['samples']}) if state else [])
        if positions:
            summary['height_range'] = max(s['position'][1] for s in positions)-min(s['position'][1] for s in positions)
            summary['axis_range'] = max(s['position']['xyz'.index(args.axis)] for s in positions)-min(
                s['position']['xyz'.index(args.axis)] for s in positions)
            summary['mover_flags'] = sorted({hex(s['mover_flags']) for s in positions if s['mover_flags'] is not None})
        summary['bridge_stopped'] = bridge_stopped
        print(summary)
        return 0 if state and state['matched'] and not state['error'] and stopped and restored and bridge_stopped and (
            not args.nudge or sent and state['applied'] == 1) else 1
    finally:
        if exports and not stopped:
            call_remote(process, exports['SpidyStop'])
        if bridge and not bridge_stopped:
            call_remote(process, bridge['SpidyStop'])
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    sys.exit(main())
