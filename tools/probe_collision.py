"""Run six native world ray queries under the game's existing query lock."""
import ctypes as c
import json
import struct
import sys
import time
from capture_game_state import Game, find_game, open_process, close
from bridge_game import ROOT, prepare
from observe_game import call_remote, call_with_payload
from inspect_game import PE


def snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 2160)
        if len(raw) != 2160:
            return None
        if struct.unpack_from('<3I', raw) != (0x53435044, 2, 2160):
            raise RuntimeError('Collision probe protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        data = dict(zip(('calls', 'eligible', 'queries', 'world', 'error', 'thread'),
                        struct.unpack_from('<4Q2I', raw, 24)))
        data['samples'] = []
        data['callers'] = []
        for i in range(16):
            caller, world, count, mask, thread = struct.unpack_from('<3Q2I', raw, 1648+i*32)
            if caller:
                data['callers'].append(dict(caller_rva=hex(caller-game.base), world=hex(world),
                    count=count, filter=mask, thread=thread))
        for i in range(6):
            offset = 64+i*264
            values = struct.unpack_from('<13f5I', raw, offset)
            data['samples'].append(dict(origin=values[:3], direction=values[3:6], fraction=values[6],
                normal=values[7:10], position=values[10:13], count=values[13], body_id=values[14],
                filter=values[15], query_filter=values[16], body_bytes=raw[offset+72:offset+264].hex()))
        return data
    return None


def main():
    game = Game(find_game())
    process = exports = None
    stopped = False
    try:
        original = PE(game.path.read_bytes()).bytes(0x2e67010, 16)
        if game.read(game.base+0x2e67010, 16) != original:
            raise RuntimeError('Native raycast already patched')
        primary = game.pointer(game.base+0x7a34dd0)
        camera = game.transform(primary)
        if not camera:
            raise RuntimeError('No stable primary game camera')
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        exports, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_collision_probe.dll',
            ROOT/'reports/collision-modules', ('SpidyStart', 'SpidyStop', 'SpidyCollisionData'))
        config = struct.pack('<4IQ4f2I', 0x53435043, 1, 48, game.pid, game.base,
                             *camera['position'], 100.0, 0x410, 5000)
        code = call_with_payload(process, exports['SpidyStart'], config)
        if code:
            raise RuntimeError(f'Collision probe start: {code}')
        started = time.monotonic()
        final = None
        while time.monotonic()-started < 6:
            final = snapshot(game, exports['SpidyCollisionData'])
            if final and (final['queries'] or final['error']):
                break
            time.sleep(.05)
        code = call_remote(process, exports['SpidyStop'])
        stopped = code == 0
        final = snapshot(game, exports['SpidyCollisionData'])
        restored = game.read(game.base+0x2e67010, 16) == original
        result = dict(pid=game.pid, module_base=hex(game.base), dll_sha256=digest,
                      final=final, stopped=stopped, entry_restored=restored)
        path = ROOT/'reports/collision-rays.json'
        path.write_text(json.dumps(result, indent=2)+'\n')
        if final and final['queries']:
            for s in final['samples']:
                print({k: v for k, v in s.items() if k != 'body_bytes'})
        print(dict(queries=final['queries'] if final else 0, error=final['error'] if final else None,
                   stopped=stopped, restored=restored))
        print(final['callers'] if final else [])
        return 0 if final and final['queries'] == 6 and not final['error'] and stopped and restored else 1
    finally:
        if exports and not stopped:
            call_remote(process, exports['SpidyStop'])
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    sys.exit(main())
