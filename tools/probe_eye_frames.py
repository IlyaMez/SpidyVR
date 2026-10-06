"""Check, in the running game and without a headset, when eye poses reach the renderer.

The game's frame shifts each view's camera history, runs gameplay, sets up the
eye views' render jobs, and only then runs view maintenance. Spidy places the
eyes before the render jobs. This probe drives two native eye views with a
moving pose, first placed in maintenance (as builds before 2026-10-05 did), then
in the current position, and reports for each phase:

- same/late frame poses: whether a rendered pose was placed in that frame;
- history moved/still: whether the eye's previous camera differed from it.

A rendering game is enough (the main menu works); no save has to be loaded. Both
eyes are captured to PNG at the end, so the images can be inspected.
"""
import argparse
import ctypes as c
import json
import pathlib
import struct
import sys
import time
from capture_game_state import Game, find_game, open_process, close
from bridge_game import ROOT, prepare
from inspect_game import PE
from observe_game import call_remote, call_with_payload
from probe_stereo import snapshot as stereo_snapshot, frame_snapshot, motion_command
from probe_stereo_gpu import discover_queue, snapshot as gpu_snapshot, save_eye_images

HOOKS = (0x18a0bb0, 0x189bd30, 0x186cc00, 0x1846c20, 0x19223e0, 0x189e310, 0x189e3a0, 0x1873470,
         0x17991a0, 0x1920310, 0x1899ab0, 0x1920240)
COUNTERS = ('copies', 'same_frame_poses', 'late_frame_poses', 'history_moved', 'history_still')


def phase_counts(before, after, commands):
    """Left-eye counter changes over one phase, with the eye commands sent during it."""
    result = {name: after[f'left_{name}']-before[f'left_{name}'] for name in COUNTERS}
    result['commands'] = commands
    return result


def assess(late, frame):
    """The current placement renders each pose in its own frame with a moving history; the old one never did.

    A couple of frames around the switch belong to the other phase. A new pose arrives with each
    command, so the history can only move in frames that latched one.
    """
    enough = late['copies'] >= 30 and frame['copies'] >= 30 and frame['commands'] >= 10
    old_was_late = late['same_frame_poses'] <= 3 and late['history_moved'] <= 3
    now_in_frame = frame['late_frame_poses'] <= 3 and frame['history_moved'] >= frame['commands']//2
    return dict(enough_frames=enough, old_placement_was_late=old_was_late, placement_in_frame=now_in_frame,
                passed=enough and old_was_late and now_in_frame)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--seconds', type=float, default=4, help='length of each of the two phases (2..10)')
    p.add_argument('--size', type=int, default=512)
    p.add_argument('--speed', type=float, default=2, help='sideways eye travel in m/s')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/eye-frames.json')
    a = p.parse_args()
    if not 2 <= a.seconds <= 10 or not 64 <= a.size <= 2048 or not 0 <= a.speed <= 40:
        p.error('Use 2..10 seconds per phase, 64..2048 pixels, and 0..40 m/s')
    game = Game(find_game())
    process = exports = None
    views_active = gpu_active = False
    try:
        pe = PE(pathlib.Path(game.path).read_bytes())
        entries = {rva: pe.bytes(rva, 16) for rva in HOOKS}
        if any(game.read(game.base+rva, 16) != code for rva, code in entries.items()):
            raise RuntimeError('A view entry is already patched. Start a fresh game process.')
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        queue = discover_queue(game, process)
        exports, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
                                  ROOT/'reports/stereo-modules',
                                  ('SpidyStart', 'SpidyStop', 'SpidyStereoData', 'SpidySetEyes', 'SpidyStereoFrames',
                                   'SpidyGpuStart', 'SpidyGpuStop', 'SpidyGpuData', 'SpidyEyePlacement'))
        total_ms = int((2*a.seconds+3)*1000)
        code = call_with_payload(process, exports['SpidyGpuStart'],
                                 struct.pack('<4I2Q4I', 0x53475043, 3, 48, game.pid, game.base, queue, total_ms+3000, 0, 1, 0))
        if code:
            raise RuntimeError(f'GPU marker start: {code}')
        gpu_active = True
        code = call_with_payload(process, exports['SpidyStart'],
                                 struct.pack('<4IQ4I', 0x53534346, 1, 40, game.pid, game.base, total_ms, 5, a.size, a.size))
        if code:
            raise RuntimeError(f'Native eye views start: {code}')
        views_active = True
        # Wait for both views and the first rendered copies.
        started = time.monotonic()
        main_pose = None
        while time.monotonic()-started < 3:
            view = stereo_snapshot(game, exports['SpidyStereoData'])
            if view and view['primary'] and view['created'] >= 2 and main_pose is None:
                main_pose = struct.unpack('<16f', game.read(view['primary'], 64))
            frames = frame_snapshot(game, exports['SpidyStereoFrames'])
            if main_pose and frames and frames['left_copies'] and frames['right_copies']:
                break
            time.sleep(.05)
        else:
            raise RuntimeError('The game did not render the eye views. It must be showing a 3D scene.')
        serial = 0
        phases = {}
        for name, late in (('placed_in_maintenance', 1), ('placed_before_render', 0)):
            if call_remote(process, exports['SpidyEyePlacement'], late):
                raise RuntimeError('Eye placement switch rejected')
            time.sleep(.2)  # frames around the switch belong to neither phase
            before = frame_snapshot(game, exports['SpidyStereoFrames'])
            begun = time.monotonic()
            commands = 0
            while (elapsed := time.monotonic()-begun) < a.seconds:
                serial += 1
                code = call_with_payload(process, exports['SpidySetEyes'],
                                         motion_command(main_pose, serial, elapsed, a.speed*elapsed))
                if code:
                    raise RuntimeError(f'Eye command: {code}')
                commands += 1
                time.sleep(.02)
            after = frame_snapshot(game, exports['SpidyStereoFrames'])
            if not before or not after:
                raise RuntimeError('Eye frame telemetry unavailable')
            phases[name] = phase_counts(before, after, commands)
            print(name, json.dumps(phases[name]), flush=True)
        call_remote(process, exports['SpidyEyePlacement'], 0)
        code = call_remote(process, exports['SpidyStop'])
        views_active = code != 0
        if code:
            raise RuntimeError(f'Native eye views stop: {code}')
        gpu_stop = call_remote(process, exports['SpidyGpuStop'])
        gpu_active = False
        final = gpu_snapshot(game, exports['SpidyGpuData'])
        result = dict(pid=game.pid, module_base=hex(game.base), dll_sha256=digest, phases=phases,
                      assessment=assess(phases['placed_in_maintenance'], phases['placed_before_render']),
                      gpu=final, gpu_stop=gpu_stop, frames=frame_snapshot(game, exports['SpidyStereoFrames']),
                      stereo=stereo_snapshot(game, exports['SpidyStereoData']),
                      entries_restored=all(game.read(game.base+rva, 16) == code for rva, code in entries.items()))
        a.output.parent.mkdir(parents=True, exist_ok=True)
        try:
            result.update(save_eye_images(game, final, a.output))
        except (OSError, ValueError, RuntimeError) as error:
            result['image_capture_error'] = str(error)
        result['passed'] = bool(result['assessment']['passed'] and result['entries_restored'] and not gpu_stop)
        a.output.write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(dict(assessment=result['assessment'], entries_restored=result['entries_restored'],
                              gpu_pairs=final and final['pairs'], gpu_error=final and final['error'],
                              images=[result.get('left_image'), result.get('right_image')]), indent=2))
        print(f"Eye frame check {'passed' if result['passed'] else 'failed'}; report: {a.output.resolve()}")
        return 0 if result['passed'] else 1
    finally:
        try:
            if views_active:
                call_remote(process, exports['SpidyStop'])
            if gpu_active:
                call_remote(process, exports['SpidyGpuStop'])
        except OSError:
            pass
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Eye frame check failed: {error}', file=sys.stderr)
        sys.exit(2)
