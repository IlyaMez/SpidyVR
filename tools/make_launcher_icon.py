"""Draw apps/launcher/spidy.ico: the launcher's mark, a red disc with a white web.

The same picture as App::badge in apps/launcher/ui.cpp. Standard library only; run it again
after changing the design.
"""
import math
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
RED = (227, 38, 47)
SIZES = (256, 64, 48, 32, 24, 16)


def coverage(size, samples=4):
    """RGBA rows for one icon size."""
    c = (size - 1) / 2
    radius = size * 0.47
    spokes, rings = (8, (0.3, 0.52, 0.74)) if size > 24 else (6, (0.38, 0.72))
    half = max(0.55, radius * 0.055 / 2) if size > 24 else 0.6
    offset = 0.39
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            disc = web = 0
            for sy in range(samples):
                for sx in range(samples):
                    px = x + (sx + .5) / samples - .5 - c
                    py = y + (sy + .5) / samples - .5 - c
                    rho = math.hypot(px, py)
                    if rho > radius:
                        continue
                    disc += 1
                    theta = (math.atan2(py, px) - offset) % (2*math.pi)
                    step = 2*math.pi/spokes
                    within = theta % step
                    if rho <= radius*0.8 and rho*math.sin(min(within, step-within)) <= half:
                        web += 1
                        continue
                    # Threads sag toward the centre between spokes, as App::badge draws them.
                    sag = math.sin(math.pi*within/step)
                    if any(abs(rho - radius*ring*(1 - 0.12*sag)) <= half for ring in rings):
                        web += 1
            total = samples*samples
            alpha = disc/total
            white = web/max(disc, 1)
            rgb = [round(channel*(1-white) + 255*white) for channel in RED]
            row += bytes((*rgb, round(alpha*255)))
        rows.append(bytes(row))
    return rows


def png(size, rows):
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data))
    raw = b''.join(b'\0' + row for row in rows)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    images = [png(size, coverage(size)) for size in SIZES]
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16*len(images)
    entries = b''
    for size, image in zip(SIZES, images):
        entries += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
    target = ROOT/'apps/launcher/spidy.ico'
    target.write_bytes(header + entries + b''.join(images))
    print(f'Wrote {target} ({target.stat().st_size} bytes).')


if __name__ == '__main__':
    main()
