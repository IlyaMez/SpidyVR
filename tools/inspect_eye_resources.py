"""Read the supported game's retained diagnostic view resource wrappers."""
import argparse
import json
import pathlib
import struct
from capture_game_state import Game, find_game


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('report', type=pathlib.Path)
    p.add_argument('--output', type=pathlib.Path, required=True)
    args = p.parse_args()
    previous = json.loads(args.report.read_text())
    game = Game(find_game())
    try:
        if game.pid != previous['pid'] or game.base != int(previous['module_base'], 16):
            raise RuntimeError('Report belongs to another game process')
        result = dict(pid=game.pid, base=hex(game.base), eyes=[])
        for eye in previous['final']['eyes']:
            view = eye['view']
            slots = [game.pointer(game.base + 0x7a34e00 + i * 8) for i in range(8)]
            if view not in slots:
                raise RuntimeError('Native view no longer owned by the engine')
            obj = game.pointer(game.pointer(view + 0x1f70) + 0x40)
            if game.pointer(obj) != game.base + 0x4e56c80:
                raise RuntimeError('Unexpected texture wrapper type')
            alloc = game.pointer(obj + 0x38)
            resource = game.pointer(alloc)
            custom_device = game.pointer(obj + 8)
            native_device = game.pointer(custom_device + 0x68)
            result['eyes'].append(dict(view=hex(view), object=hex(obj),
                texture_desc=list(struct.unpack('<11I', game.read(obj + 0x88, 44))),
                allocation=hex(alloc), allocation_bytes=game.read(alloc, 256).hex(),
                resource=hex(resource), resource_vtable=hex(game.pointer(resource)),
                resource_bytes=game.read(resource, 128).hex(), device=hex(native_device)))
        args.output.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
    finally:
        game.close()


if __name__ == '__main__':
    main()
