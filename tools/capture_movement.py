"""Observe the local mover around one leased native jump; no physics writes."""
import ctypes as c
import json
import struct
import sys
import time
from capture_game_state import Game, LIVE_VTABLES, find_game, open_process, close
from bridge_game import ROOT, prepare, snapshot as bridge_snapshot
from observe_game import call_remote, call_with_payload
from inspect_game import PE


def component(game, handle):
    index, generation = handle & 0xfffff, handle >> 20
    table = game.pointer(game.base+0x7a44320)
    raw_count = game.read(game.base+0x7a44340, 4)
    if not table or not generation or len(raw_count) != 4:
        return 0
    count = struct.unpack('<i', raw_count)[0]
    if not 0 <= index < count <= 0x100000:
        return 0
    slot = game.read(table+index*16, 12)
    if len(slot) != 12:
        return 0
    address, current = struct.unpack('<QI', slot)
    return address if current == generation and game.read(address+0x14, 4) == struct.pack('<I', handle) else 0


def main():
    game = Game(find_game())
    process = exports = None
    active = False
    try:
        pe = PE(game.path.read_bytes())
        LIVE_VTABLES['hero_mover'] = 0x38b2c98
        started = time.monotonic()
        found = game.registered_candidates()
        elapsed = time.monotonic()-started
        heroes = [x for x in found if x['kind'] == 'hero_local']
        if len(heroes) != 1:
            raise RuntimeError('Exactly one registered local hero is required')
        hero = heroes[0]
        movers = [x for x in found if x['kind'] == 'hero_mover' and x['actor_record'] == hero['actor_record']]
        if len(movers) != 1:
            raise RuntimeError('Exactly one registered mover must belong to the local hero')
        mover = movers[0]
        address = int(mover['object'], 16)
        raw = game.read(address+0xdb4, 4)
        if len(raw) != 4:
            raise RuntimeError('Movement component handle is unreadable')
        handle = struct.unpack('<I', raw)[0]
        body = component(game, handle)
        if not body or game.pointer(body+8) != int(hero['actor_record'], 16):
            raise RuntimeError('Movement component owner differs from the local hero')
        body_vtable = game.pointer(body)-game.base
        body_type = pe.vtable(body_vtable)
        print(dict(registry_seconds=elapsed, hero=hero['object'], mover=mover['object'],
                   movement=hex(body), movement_type=body_type), flush=True)
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        exports, digest = prepare(game.pid, process)
        config = struct.pack('<4I3Q', 0x53424346, 1, 40, game.pid, game.base,
                             int(hero['object'], 16), int(hero['actor_record'], 16))
        code = call_with_payload(process, exports['SpidyStart'], config)
        if code:
            raise RuntimeError(f'Input bridge start: {code}')
        active = True
        samples = []
        started = time.monotonic()
        jumped = False
        while time.monotonic()-started < 5:
            seconds = time.monotonic()-started
            if component(game, handle) != body or game.pointer(body+8) != int(hero['actor_record'], 16):
                raise RuntimeError('Movement component lifetime changed')
            if seconds >= 1 and not jumped:
                b = bridge_snapshot(game, exports['SpidyBridgeData'])
                if not b or b['state'] != 1 or not b['matched']:
                    raise RuntimeError('The current camera does not follow the validated hero')
                payload = struct.pack('<4IQ2I8f', 0x53424354, 1, 64, 1, 1, 350, 16, *([0.0]*7), 1.0)
                code = call_with_payload(process, exports['SpidySubmit'], payload)
                if code:
                    raise RuntimeError(f'Native jump rejected: {code}')
                jumped = True
            player = game.transform(int(hero['actor_transform'], 16))
            data = game.read(body, 0x440)
            if player and len(data) == 0x440:
                # Offsets are research observations, deliberately not named velocity/position yet.
                vectors = {hex(offset): struct.unpack_from('<3f', data, offset) for offset in
                    (0x4c, 0x8c, 0xbc, 0x104, 0x110, 0x11c, 0x148, 0x2bc, 0x2ec, 0x3ac)}
                samples.append(dict(seconds=seconds, player=player['position'], vectors=vectors,
                                    flags144=struct.unpack_from('<I', data, 0x144)[0],
                                    enabled2a0=data[0x2a0]))
            time.sleep(.01)
        stop = call_remote(process, exports['SpidyStop'])
        active = stop != 0
        restored = all(game.read(game.base+rva, 16) == pe.bytes(rva, 16)
                       for rva in (0x1e1d600, 0x1ce2f40, 0x1cdfa70, 0x1cdf840, 0x5c2630))
        result = dict(pid=game.pid, module_base=hex(game.base), registry_seconds=elapsed,
                      candidates=found, mover=mover, movement=hex(body), movement_vtable=hex(body_vtable),
                      movement_type=body_type, dll_hash=digest, samples=samples, stop=stop, restored=restored)
        path = ROOT/'reports/movement-jump.json'
        path.write_text(json.dumps(result, indent=2)+'\n')
        heights = [s['player'][1] for s in samples]
        print(dict(samples=len(samples), height_range=max(heights)-min(heights) if heights else 0,
                   stopped=stop == 0, restored=restored))
        return 0 if samples and jumped and not stop and restored else 1
    finally:
        if active:
            call_remote(process, exports['SpidyStop'])
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    sys.exit(main())
