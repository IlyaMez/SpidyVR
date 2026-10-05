"""Find RTTI vtables containing a native method, without starting the game."""
import hashlib
import pathlib
import struct
import sys
from inspect_game import PE, EXPECTED_SHA256
from discover_render_types import occurrences, rva_for_offset


def main():
    path = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered\Spider-Man.exe")
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        raise ValueError('Unsupported executable hash')
    pe = PE(data)
    for value in sys.argv[1:]:
        target = int(value, 0)
        for offset in occurrences(data, struct.pack('<Q', pe.image_base + target)):
            if offset % 8:
                continue
            at = rva_for_offset(pe, offset)
            for slot in range(256):
                table = at-slot*8
                try:
                    owner = pe.vtable(table)
                except (ValueError, struct.error):
                    continue
                print(dict(method=hex(target), table=hex(table), slot=hex(slot*8), **owner))
                for member in (0x138,):
                    address = struct.unpack('<Q', pe.bytes(table+member, 8))[0]-pe.image_base
                    print(f'  method[{member:#x}] = {address:#x}')
                break


if __name__ == '__main__':
    main()
