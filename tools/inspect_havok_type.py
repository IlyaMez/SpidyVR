"""Read a native Havok reflection type from the supported executable, offline."""
import struct
import sys
from research_game import Research
r = Research()
pe = r.pe
if len(sys.argv) > 1 and sys.argv[1] == 'members':
    count = struct.unpack('<I', pe.bytes(0x52849b0, 4))[0]
    for index in range(count):
        at = struct.unpack('<Q', pe.bytes(0x52849b8+index*8, 8))[0]-pe.image_base
        raw = pe.bytes(at, 40)
        name = struct.unpack_from('<Q', raw, 16)[0]-pe.image_base
        flags_offset = struct.unpack_from('<Q', raw, 24)[0]
        print(hex(at), pe.bytes(name, 128).split(b'\0', 1)[0].decode(), hex(flags_offset))
    sys.exit(0)
if len(sys.argv) > 1:
    address = int(sys.argv[1], 0)
    length = int(sys.argv[2], 0) if len(sys.argv) > 2 else 256
    for offset in range(0, length, 8):
        value = struct.unpack('<Q', pe.bytes(address+offset, 8))[0]
        label = ''
        if pe.image_base <= value < pe.image_base+pe.image_size:
            try:
                raw = pe.bytes(value-pe.image_base, 128).split(b'\0', 1)[0]
                if raw and all(32 <= b < 127 for b in raw): label = raw.decode()
            except ValueError:
                pass
        print(f'{address+offset:08x}: {value:016x} {label}')
    sys.exit(0)
target = struct.pack('<Q', pe.image_base+0x521bce8)  # exact hknpBody name
for section in pe.sections:
    data = pe.data[section['raw']:section['raw']+section['raw_size']]
    start = 0
    while (at := data.find(target, start)) >= 0:
        start = at+1
        address = section['rva']+at
        print('hknpBody reflection reference', hex(address))
        raw = pe.bytes(address, 96)
        for offset in range(0, 96, 8):
            value = struct.unpack_from('<Q', raw, offset)[0]
            print(f'  {offset:02x}: {value:016x}')
