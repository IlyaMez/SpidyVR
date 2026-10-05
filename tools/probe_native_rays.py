"""Exercise changing, expiring ray batches without moving the player or using VR."""
import ctypes as c
import json
import math
import struct
import sys
import time
from capture_game_state import Game, find_game, open_process, close
from bridge_game import ROOT, prepare
from observe_game import call_remote, call_with_payload, allocate, release
from inspect_game import PE


def snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 784)
        if len(raw) != 784:
            return None
        if struct.unpack_from('<3I', raw) != (0x53525944, 2, 784):
            raise RuntimeError('Native ray protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        result = dict(zip(('serial', 'qpc', 'calls', 'batches', 'world', 'count', 'error', 'thread'),
                          struct.unpack_from('<5Q3I', raw, 24)))
        result['status'] = struct.unpack_from('<I', raw, 12)[0]
        if result['count'] > 8:
            raise RuntimeError('Native ray sample overflow')
        result['hits'] = []
        for i in range(result['count']):
            offset = 80+i*88
            origin_distance_direction = struct.unpack_from('<7fI', raw, offset)
            position_fraction_normal = struct.unpack_from('<7f7I', raw, offset+32)
            v, h = origin_distance_direction, position_fraction_normal
            result['hits'].append(dict(origin=v[:3], distance=v[3], direction=v[4:7], tag=v[7],
                position=h[:3], fraction=h[3], normal=h[4:7], count=h[7], body_id=h[8],
                filter=h[9], body_flags=h[10], body_matches=bool(h[11]), motion_id=h[12], broad_phase_id=h[13],
                fixed=bool(h[7] and h[11] and h[10] & 3 == 1 and h[12] == 0 and h[13] != 0xffffffff)))
        return result
    return None


def command(serial, rays, lease_ms=150):
    if len(rays) > 8:
        raise ValueError('At most eight rays per batch')
    payload = bytearray(288)
    struct.pack_into('<4IQ2I', payload, 0, 0x5352594d, 1, 288, len(rays), serial, lease_ms, 0)
    for i, (origin, direction, distance, tag) in enumerate(rays):
        struct.pack_into('<7fI', payload, 32+i*32, *origin, distance, *direction, tag)
    return bytes(payload)


def main():
    game = Game(find_game())
    process = exports = destination = None
    active = False
    try:
        pe = PE(game.path.read_bytes())
        original = pe.bytes(0x2e67010, 16)
        if game.read(game.base+0x2e67010, 16) != original:
            raise RuntimeError('Raycast entry is already patched')
        camera = game.transform(game.pointer(game.base+0x7a34dd0))
        if not camera:
            raise RuntimeError('No active native camera')
        origin = camera['position']
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        exports, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll',
            ROOT/'reports/ray-modules', ('SpidyRayStart', 'SpidyRaySubmit', 'SpidyRayStop',
                                       'SpidyRaySample', 'SpidyRayData'))
        code = call_with_payload(process, exports['SpidyRayStart'], struct.pack('<4IQ2I',
            0x53525943, 1, 32, game.pid, game.base, 6000, 0x410))
        if code:
            raise RuntimeError(f'Native rays start: {code}')
        active = True
        destination = allocate(process, None, 784, 0x3000, 4)
        if not destination:
            raise c.WinError(c.get_last_error())
        def sample():
            nonlocal destination
            try:
                code = call_remote(process, exports['SpidyRaySample'], destination)
            except TimeoutError:
                # The remote call may still own this output buffer.
                destination = None
                raise
            if code:
                raise RuntimeError(f'Native rays sample: {code}')
            return snapshot(game, destination)
        samples = []
        latencies = []
        print('Testing 40 changing ray batches and command expiry (no movement).', flush=True)
        for serial in range(1, 41):
            # A different second ray on every batch catches old serial/direction reuse.
            angle = serial*.1
            directions = [(0, -1, 0), (math.cos(angle), 0, math.sin(angle))]
            rays = [(origin, d, 100, serial*10+i) for i, d in enumerate(directions)]
            started = time.monotonic()
            code = call_with_payload(process, exports['SpidyRaySubmit'], command(serial, rays))
            if code:
                raise RuntimeError(f'Native ray command: {code}')
            while time.monotonic()-started < .2:
                result = sample()
                if result and result['error']:
                    raise RuntimeError(f'Native ray result: {result["error"]}')
                if result and result['status'] == 2 and result['serial'] == serial:
                    break
                time.sleep(.002)
            else:
                raise RuntimeError('Native ray batch missed its 150 ms lease')
            latencies.append((time.monotonic()-started)*1000)
            if result['count'] != 2 or [h['tag'] for h in result['hits']] != [serial*10, serial*10+1]:
                raise RuntimeError('Native ray batch mixed requests')
            for h in result['hits']:
                if h['count']:
                    expected = [o+d*h['fraction']*h['distance'] for o, d in zip(h['origin'], h['direction'])]
                    if not h['body_matches'] or math.dist(h['position'], expected) > .002:
                        raise RuntimeError('Native ray geometry or body generation mismatch')
            samples.append(result)
            time.sleep(.02)
        time.sleep(.18)
        expired = sample()
        code = call_with_payload(process, exports['SpidyRaySubmit'], command(41, []))
        cancelled = sample()
        stop = call_remote(process, exports['SpidyRayStop'])
        active = stop != 0
        restored = game.read(game.base+0x2e67010, 16) == original
        final = snapshot(game, exports['SpidyRayData'])
        passed = not code and not stop and restored and expired['status'] == 0 and expired['count'] == 0 and (
            cancelled['status'] == 0 and cancelled['count'] == 0)
        result = dict(pid=game.pid, dll_sha256=digest, samples=samples, latency_ms=latencies,
            expired=expired, cancelled=cancelled, stop_result=stop, restored=restored, final=final, passed=passed)
        (ROOT/'reports/native-ray-batches-v2.json').write_text(json.dumps(result, indent=2)+'\n')
        print(dict(passed=passed, batches=len(samples), hits=sum(h['count'] > 0 for s in samples for h in s['hits']),
            maximum_latency_ms=max(latencies), expiry_rejected=expired['status'] == 0, restored=restored, stop=stop))
        return 0 if passed else 1
    finally:
        if active:
            call_remote(process, exports['SpidyRayStop'])
        if destination:
            release(process, destination, 0, 0x8000)
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    sys.exit(main())
