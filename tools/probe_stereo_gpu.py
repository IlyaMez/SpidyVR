"""Bounded native stereo capture synchronized by submitted GPU view markers."""
import argparse
import ctypes as c
import json
import pathlib
import struct
import sys
import time
from capture_game_state import Game, find_game, open_process, close
from bridge_game import ROOT, prepare
from observe_game import call_remote, call_with_payload
from probe_render import snapshot as render_snapshot
from probe_stereo import snapshot as stereo_snapshot, frame_snapshot, motion_command
from capture_stereo import write_png


def snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 208)
        seq = game.read(address+16, 8)
        if len(raw) != 208 or len(seq) != 8:
            return None
        if struct.unpack_from('<3I', raw) != (0x53475044, 2, 208):
            raise RuntimeError('GPU stereo protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != seq:
            continue
        names = ('executes', 'markers', 'matched_markers', 'pairs', 'serial', 'generation', 'fence', 'completed',
                 'left_begins', 'right_begins', 'left_ends', 'right_ends', 'left_resource', 'right_resource',
                 'left_pixels', 'right_pixels')
        result = dict(zip(names, struct.unpack_from('<16Q', raw, 32)))
        result.update(zip(('left_state', 'right_state', 'left_known', 'right_known', 'width', 'height'),
                          struct.unpack_from('<6I', raw, 160)))
        result.update(status=struct.unpack_from('<I', raw, 12)[0], error=struct.unpack_from('<I', raw, 24)[0])
        result.update(zip(('captured','reused','captured_serial'),struct.unpack_from('<3Q',raw,184)))
        return result
    return None


def discover_queue(game, process):
    exports, _ = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_render_probe.dll',
                         ROOT/'reports/render-modules', ('SpidyStart', 'SpidyStop', 'SpidyRenderData'))
    result = call_remote(process, exports['SpidyStart'])
    if result:
        raise RuntimeError(f'Render discovery start: {result}')
    try:
        time.sleep(1)
        data = render_snapshot(game, exports['SpidyRenderData'])
    finally:
        result = call_remote(process, exports['SpidyStop'])
    if result or not data:
        raise RuntimeError('Render discovery did not stop or returned no sample')
    queues = {int(s['object'], 16) for s in data['samples'] if s['kind'] == 2}
    if len(queues) != 1:
        raise RuntimeError('Exactly one direct game queue required')
    return next(iter(queues))


def save_eye_images(game, final, output):
    """Read only completed, stopped GPU captures; publish their actual dimensions."""
    if not final or final['status'] != 2 or final['error'] or not final['pairs']:
        return {}
    if not final['fence'] or final['completed'] < final['fence'] or final['completed'] == 0xffffffffffffffff:
        raise RuntimeError('Eye image GPU work did not complete')
    width, height = final['width'], final['height']
    if not 64 <= width <= 4096 or not 64 <= height <= 4096:
        raise RuntimeError('Unexpected eye capture dimensions')
    count = width*height*4
    result = {}
    for i, hand in enumerate(('left', 'right')):
        address = final[f'{hand}_pixels']
        if address < 0x10000:
            raise RuntimeError('Eye capture has no pixel buffer')
        pixels = game.read(address, count)
        if len(pixels) != count:
            raise RuntimeError('Incomplete eye capture pixels')
        path = output.with_name(output.stem+f'-{i}.png')
        write_png(path, width, height, pixels)
        result[f'{hand}_image'] = str(path.resolve())
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seconds', type=float, default=8)
    p.add_argument('--size', type=int, default=512)
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/stereo-gpu-pairs.json')
    a = p.parse_args()
    if not 2 <= a.seconds <= 10 or not 64 <= a.size <= 4096:
        p.error('Use 2..10 seconds and 64..4096 pixels')
    game = Game(find_game())
    process = None
    exports = None
    views_active = gpu_active = False
    result = {}
    try:
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        queue = discover_queue(game, process)
        exports, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
            ROOT/'reports/stereo-modules', ('SpidyStart', 'SpidyStop', 'SpidyStereoData', 'SpidySetEyes',
                'SpidyStereoFrames', 'SpidyGpuStart', 'SpidyGpuStop', 'SpidyGpuData'))
        config = struct.pack('<4I2Q4I', 0x53475043, 3, 48, game.pid, game.base, queue, 15000, 0, 1, 0)
        code = call_with_payload(process, exports['SpidyGpuStart'], config)
        if code:
            raise RuntimeError(f'GPU marker start: {code}')
        gpu_active = True
        config = struct.pack('<4IQ4I', 0x53534346, 1, 40, game.pid, game.base,
                             int(a.seconds*1000), 5, a.size, a.size)
        code = call_with_payload(process, exports['SpidyStart'], config)
        if code:
            raise RuntimeError(f'Native stereo start: {code}')
        views_active = True
        print('Capturing native eye pairs after their submitted GPU markers.', flush=True)
        started = time.monotonic()
        samples = []
        serial = 0
        main_pose = None
        while (elapsed := time.monotonic()-started) < a.seconds+1:
            view = stereo_snapshot(game, exports['SpidyStereoData'])
            if view and not view['retired'] and view['primary'] and elapsed < a.seconds-.3:
                if main_pose is None:
                    main_pose = struct.unpack('<16f', game.read(view['primary'], 64))
                serial += 1
                code = call_with_payload(process, exports['SpidySetEyes'], motion_command(main_pose, serial, elapsed))
                if code and code != 4003:
                    raise RuntimeError(f'Eye command: {code}')
            sample = snapshot(game, exports['SpidyGpuData'])
            if sample:
                samples.append(dict(seconds=elapsed, **sample))
                if sample['error']:
                    break
            time.sleep(.04)
        code = call_remote(process, exports['SpidyStop'])
        views_active = code != 0
        if code:
            raise RuntimeError(f'Native stereo stop: {code}')
        code = call_remote(process, exports['SpidyGpuStop'])
        gpu_active = False
        result.update(pid=game.pid, module_base=hex(game.base), dll_sha256=digest, stop_result=code,
            final=snapshot(game, exports['SpidyGpuData']), frames=frame_snapshot(game, exports['SpidyStereoFrames']),
            stereo=stereo_snapshot(game, exports['SpidyStereoData']), samples=samples)
        final = result['final']
        result.update(save_eye_images(game, final, a.output))
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(dict(stop_result=code, final=final, frames=result['frames']), indent=2))
        return 0 if final and not code and final['pairs'] else 1
    finally:
        if views_active:
            call_remote(process, exports['SpidyStop'])
        if gpu_active:
            call_remote(process, exports['SpidyGpuStop'])
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    sys.exit(main())

