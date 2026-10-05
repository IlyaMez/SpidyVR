"""Map camera/render RTTI and vtables from the supported game PE, without running it."""
import argparse
import json
import pathlib
import re
import struct
import sys
from inspect_game import PE, EXPECTED_SHA256
import hashlib

def rva_for_offset(pe, offset):
    for section in pe.sections:
        if section['raw'] <= offset < section['raw'] + section['raw_size']:
            return section['rva'] + offset - section['raw']
    raise ValueError('File offset is outside the section table')

def occurrences(data, pattern):
    start = 0
    while True:
        found = data.find(pattern, start)
        if found < 0: break
        yield found
        start = found + 1

def discover(pe, pattern):
    results = []
    for match in re.finditer(rb'\.\?A[UV][\x21-\x7e]{3,240}?@@\x00', pe.data):
        name = match.group()[:-1].decode('ascii')
        if not pattern.search(name): continue
        descriptor = rva_for_offset(pe, match.start() - 16)
        vtables = []
        # A PE64 CompleteObjectLocator names its type descriptor at +12 and
        # carries its own RVA at +20. Validate both before following a pointer.
        for offset in occurrences(pe.data, struct.pack('<I', descriptor)):
            col = offset - 12
            if col < 0 or col + 24 > len(pe.data): continue
            signature, _, _, td, _, self_rva = struct.unpack_from('<IIIIII', pe.data, col)
            if signature != 1 or td != descriptor: continue
            try:
                if pe.offset(self_rva, 24) != col: continue
            except ValueError: continue
            for pointer_offset in occurrences(pe.data, struct.pack('<Q', pe.image_base + self_rva)):
                if pointer_offset % 8: continue
                table_rva = rva_for_offset(pe, pointer_offset + 8)
                methods = []
                for slot in range(128):
                    try: method = pe.u64(pe.offset(table_rva + slot * 8, 8)) - pe.image_base
                    except (ValueError, struct.error): break
                    if not pe.executable(method): break
                    methods.append(hex(method))
                if methods: vtables.append(dict(rva=hex(table_rva), methods=methods))
        results.append(dict(type=name, descriptor_rva=hex(descriptor), vtables=vtables))
    return results

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--filter', default=r'Camera2@|CameraManager|RenderView|SceneView|RenderContext|RenderPass|GameRender|RenderWorld|ViewInfo|Renderer@')
    args = parser.parse_args()
    data = args.executable.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        print('Unknown executable: refusing to label its objects with the supported build.', file=sys.stderr)
        return 1
    results = discover(PE(data), re.compile(args.filter, re.I))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(types=results, runtime_verified=False), indent=2) + '\n', encoding='utf-8')
    for item in results:
        tables = ', '.join(v['rva'] for v in item['vtables']) or 'no vtable located'
        print(item['type'], tables)
    print(f'{len(results)} types mapped; these are research leads, not callable render APIs.')
    return 0

if __name__ == '__main__': sys.exit(main())
