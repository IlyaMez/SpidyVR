"""Launch/attach native VR, with optional bounded tests and automatic eye resolution."""
import argparse
import ctypes as c
import json
import math
import os
import pathlib
import struct
import subprocess
import sys
import time
import signal
from collections import deque
from capture_game_state import Game, LIVE_VTABLES, find_game, open_process, close
from capture_movement import component
from bridge_game import ROOT, HOOKS, prepare, snapshot as bridge_snapshot
from inspect_game import PE
from observe_game import call_remote, call_with_payload, modules
from probe_stereo_gpu import discover_queue, snapshot as gpu_snapshot, save_eye_images
from probe_stereo import frame_snapshot
from probe_native_rays import snapshot as ray_snapshot
from probe_game_swing import snapshot as swing_snapshot
from probe_native_motion import snapshot as motion_snapshot
from vr_launcher import LauncherLock, alive, wait_for_game
import vr_display

GAME_HOOKS = (*HOOKS, 0x2e67010, 0x1fbe360, 0x1fbda50, 0xa7b3a0, 0x1f9db60,
              0x18a0bb0, 0x189bd30, 0x186cc00, 0x1846c20, 0x19223e0,
              0x189e310, 0x189e3a0, 0x1873470, 0x17991a0, 0x1920310, 0x676dd0)


def frame_rates(samples):
    """Measure active presentation; reused images are not new scene frames."""
    active = [s for s in samples if s.get('status') == 3 and s.get('submitted', 0) > 0]
    if len(active) < 2:
        return None
    # Older workers kept status=3 during cleanup. Ignore trailing samples of
    # the same stopped frame, while retaining dropped frames during active work.
    counter = 'frames' if all('frames' in s for s in active) else 'submitted'
    while len(active) > 1 and active[-1][counter] == active[-2][counter]:
        active.pop()
    if len(active) < 2:
        return None
    first, last = active[0], active[-1]
    seconds = last['seconds'] - first['seconds']
    if seconds <= 0:
        return None
    result = dict(sample_seconds=seconds, submitted_fps=(last['submitted']-first['submitted'])/seconds)
    for name, field in (('new_scene_fps', 'unique_submitted'), ('reused_fps', 'reused_submitted')):
        result[name] = (last[field]-first[field])/seconds if field in first and field in last else None
    return result


def accepted(result):
    """A stopped worker alone is not a successful presentation or clean test."""
    final, gpu, swing = (result.get(name) or {} for name in ('final', 'gpu', 'swing'))
    return bool(final.get('status') == 4 and not final.get('error', 1) and final.get('submitted', 0) > 0 and
                gpu.get('status') == 2 and not gpu.get('error', 1) and gpu.get('pairs', 0) > 0 and
                0 < gpu.get('fence', 0) <= gpu.get('completed', 0) < 0xffffffffffffffff and
                not swing.get('error', 0) and not any(s.get('error') for s in result.get('samples', [])) and
                not any(s.get('error') for s in result.get('swing_samples', [])) and
                not any(s.get('error') for s in result.get('motion_samples', [])) and
                result.get('game_entries_restored') and result.get('xr_stop') == 0 and result.get('bridge_stop') == 0)


def preflight():
    manifest = pathlib.Path(r'C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json')
    probe = ROOT/'build/windows-ninja/spidy_headset_probe.exe'
    if not manifest.is_file() or not probe.is_file():
        raise RuntimeError('Build the headset probe and install the Virtual Desktop OpenXR runtime first')
    result = subprocess.run([str(probe)], env={**os.environ, 'XR_RUNTIME_JSON': str(manifest)},
                            capture_output=True, text=True, timeout=10)
    if result.returncode:
        raise RuntimeError((result.stderr or result.stdout).strip() +
                           ' Connect Quest 3 in Virtual Desktop before starting the game VR test.')
    print(result.stdout.strip(), flush=True)


def snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 576)
        if len(raw) != 576:
            return None
        if struct.unpack_from('<3I', raw) != (0x53585244, 3, 576):
            raise RuntimeError('Game XR protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        result = dict(zip(('frames', 'tracked', 'submitted', 'dropped', 'left_hands', 'right_hands', 'serial', 'generation'),
                          struct.unpack_from('<8Q', raw, 24)))
        result.update(status=struct.unpack_from('<I', raw, 12)[0],
                      native_keys=struct.unpack_from('<I', raw, 88)[0],
                      error=struct.unpack_from('<I', raw, 92)[0],
                      head=struct.unpack_from('<16f', raw, 96),
                      hands=[struct.unpack_from('<16f', raw, 160+i*64) for i in range(2)],
                      message=raw[288:544].split(b'\0', 1)[0].decode('utf-8', 'replace'))
        result.update(zip(('unique_submitted','reused_submitted'),struct.unpack_from('<2Q',raw,544)))
        result.update(zip(('flat_screen','toggles','eye_width','eye_height'),struct.unpack_from('<4I',raw,560)))
        return result
    return None


HERO_STATES = {0: 'unknown', 1: 'valid', 2: 'no_instance', 3: 'unregistered', 4: 'bad_transform'}
WEB_STATES = {0: 'off', 1: 'waiting', 2: 'game_webs', 3: 'failed'}


def lens_degrees(left, right, top, bottom):
    """Horizontal and vertical field of view of native lens tangents."""
    return [math.degrees(math.atan(right) - math.atan(left)), math.degrees(math.atan(bottom) - math.atan(top))]


def appearance_snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 288)
        if len(raw) != 288:
            return None
        if struct.unpack_from('<3I', raw) != (0x53415044, 4, 288):
            raise RuntimeError('Native appearance protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        state = struct.unpack_from('<I', raw, 12)[0]
        left, right, srgb_left, srgb_right, actor = struct.unpack_from('<5Q', raw, 24)
        hidden_frames, hides, restores = struct.unpack_from('<3Q', raw, 64)
        handle, flags = struct.unpack_from('<2I', raw, 88)
        near_left, near_right, near, model = struct.unpack_from('<4Q', raw, 96)
        near_distance, near_radius = struct.unpack_from('<2f', raw, 128)
        near_handle, near_flags = struct.unpack_from('<2I', raw, 136)
        anchored, rejected = struct.unpack_from('<2Q', raw, 144)
        anchor_last, anchor_max = struct.unpack_from('<2f', raw, 160)
        anchor_sum = struct.unpack_from('<d', raw, 168)[0]
        web_state, web_live = struct.unpack_from('<2I', raw, 176)
        web_creates, web_releases, web_failures, web_updates = struct.unpack_from('<4Q', raw, 184)
        active_aligned, active_rejected = struct.unpack_from('<2Q', raw, 216)
        active_bounds = struct.unpack_from('<4f', raw, 232)
        active_shift = struct.unpack_from('<f', raw, 248)[0]
        shared_hero, lag_last, lag_max, lag_sum = struct.unpack_from('<Q2fd', raw, 256)
        web_start_last, web_start_max = struct.unpack_from('<2f', raw, 280)
        return dict(hidden_avatar=[left, right], srgb_overlay=[srgb_left, srgb_right], player_actor=hex(actor),
                    hero_state=HERO_STATES.get(state, state), hero_handle=hex(handle), hero_flags=hex(flags),
                    native_hidden_frames=hidden_frames, native_hides=hides, native_restores=restores,
                    near_instances=[near_left, near_right], near_instance=hex(near), near_model=hex(model),
                    near_distance=near_distance, near_radius=near_radius, near_handle=hex(near_handle),
                    near_flags=hex(near_flags), anchored_frames=anchored, anchor_rejected=rejected,
                    anchor_last_m=anchor_last, anchor_max_m=anchor_max,
                    anchor_mean_m=anchor_sum/anchored if anchored else 0.0,
                    web_state=WEB_STATES.get(web_state, web_state), web_live=[bool(web_live & 1), bool(web_live & 2)],
                    web_creates=web_creates, web_releases=web_releases, web_failures=web_failures,
                    web_updates=web_updates, active_view_aligned=active_aligned,
                    active_view_rejected=active_rejected,
                    active_view_fov_deg=lens_degrees(*active_bounds) if active_aligned else None,
                    active_view_shift_m=active_shift, shared_hero_frames=shared_hero,
                    hero_lag_last_m=lag_last, hero_lag_max_m=lag_max,
                    hero_lag_mean_m=lag_sum/shared_hero if shared_hero else 0.0,
                    web_start_error_m=web_start_last if web_start_last >= 0 else None,
                    web_start_error_max_m=web_start_max if web_start_max >= 0 else None)
    return None


def timing_snapshot(game, address):
    names=('frame','wait_frame','tracking','prepare','acquire','copy_wait','hand_overlay','release','end_frame','display_period')
    for _ in range(8):
        raw=game.read(address,344)
        if len(raw)!=344:
            return None
        if struct.unpack_from('<3I',raw)!=(0x5358544d,1,344):
            raise RuntimeError('XR timing protocol mismatch')
        if struct.unpack_from('<Q',raw,16)[0]&1 or raw[16:24]!=game.read(address+16,8):
            continue
        result={}
        for i,name in enumerate(names):
            count,total,maximum,last=struct.unpack_from('<Q3d',raw,24+32*i)
            result[name]=dict(count=count,mean_ms=total/count if count else 0,max_ms=maximum,last_ms=last)
        return result
    return None


def game_running():
    try:
        find_game()
        return True
    except RuntimeError as error:
        return not str(error).startswith('Spider-Man is not running.')


def settle_display(running):
    """Restore display values saved for a VR launch once the game has closed."""
    if not vr_display.pending() or getattr(settle_display, 'deferred', False):
        return
    if running:
        settle_display.deferred = True
        print('Your display settings are restored after the game closes, the next time Spidy VR starts it '
              r'(or run tools\vr_display.py --restore).', flush=True)
    elif vr_display.restore():
        print('Restored your desktop display settings.', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seconds', type=float, default=0,help='0 runs until game exit or Ctrl+C; 2..25 runs a timed test')
    p.add_argument('--size', type=int, default=0,help='0 uses the runtime recommendation; otherwise a square override')
    p.add_argument('--auto-launch',action='store_true')
    p.add_argument('--stop-after',type=float,default=0,help='End an untimed session after this many seconds for validation')
    p.add_argument('--capture-images', action='store_true',help='Enable diagnostic CPU eye readback (adds overhead)')
    p.add_argument('--overlay-webs', action='store_true',help="Draw Spidy's overlay webs instead of the game's web lines")
    p.add_argument('--stock-monitor-view', action='store_true',
                   help="Keep the stock camera as the game's active view; culling and shadows then follow it, not the head")
    p.add_argument('--swing-speed', type=float, default=32, help='Native swing speed cap in m/s (default 32)')
    p.add_argument('--full-desktop-view', action='store_true',
                   help="Start the game with its own display settings instead of a small VR window")
    p.add_argument('--seed-capture', type=pathlib.Path)
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/game-vr.json')
    a = p.parse_args()
    if a.stop_after and (a.seconds or not 5<=a.stop_after<=300):
        p.error('--stop-after requires --seconds 0 and a value from 5 to 300')
    if (a.seconds!=0 and not 2 <= a.seconds <= 25) or (a.size!=0 and not 64 <= a.size <= 4096) or not 1 <= a.swing_speed <= 65:
        p.error('Use 0 or 2..25 seconds, 0 or 64..4096 pixels, and 1..65 m/s')
    preflight()

    def prepare_display():
        values = vr_display.prepare_launch(small=not a.full_desktop_view)
        if values:
            print(f"Desktop view: {values['WindowWidth']} x {values['WindowHeight']} window to save GPU time; "
                  'your display settings return when the game closes.', flush=True)

    game = wait_for_game(prepare=prepare_display) if a.auto_launch else Game(find_game())
    process = bridge = xr = rays = None
    bridge_active = xr_active = False
    previous_signal=None
    try:
        pe = PE(game.path.read_bytes())
        original_entries = {rva:pe.bytes(rva,16) for rva in GAME_HOOKS}
        if any(game.read(game.base+rva,16) != code for rva,code in original_entries.items()):
            raise RuntimeError('A required game entry is already patched. Start a fresh game process.')
        LIVE_VTABLES['hero_mover'] = 0x38b2c98
        if a.seed_capture:
            seed = json.loads(a.seed_capture.read_text())
            if seed['pid'] != game.pid or int(seed['module_base'], 16) != game.base:
                raise RuntimeError('Player capture belongs to another game process')
            candidates = seed['candidates']
        else:
            print('Reading the current local player from the component registry.', flush=True)
            candidates = game.registered_candidates()
            scanned, complete = 0, False
            source = 'component_registry'
            if not any(x['kind'] == 'hero_local' for x in candidates):
                print('Using bounded heap discovery (up to 20 seconds).', flush=True)
                found, scanned, complete = game.discover(8192 << 20, 20)
                candidates = list(found.values())
                source = 'heap_scan'
            seed = dict(pid=game.pid, module_base=hex(game.base), candidates=candidates,
                        scanned_mib=scanned/(1 << 20), scan_complete=complete, discovery_source=source)
            a.output.parent.mkdir(parents=True, exist_ok=True)
            a.output.with_name(a.output.stem+'-player.json').write_text(json.dumps(seed, indent=2)+'\n')
        heroes = [game.candidate(int(x['object'], 16), 'hero_local') for x in candidates if x['kind'] == 'hero_local']
        heroes = [h for h in heroes if h]
        if len(heroes) != 1:
            raise RuntimeError('Exactly one independently validated local hero is required')
        hero = heroes[0]
        managers = [x for x in game.registered_candidates() if x['kind'] == 'hero_mover' and
                    x['actor_record'] == hero['actor_record']]
        if len(managers) != 1:
            raise RuntimeError('Exactly one local movement manager is required')
        handle = struct.unpack('<I',game.read(int(managers[0]['object'],16)+0xdb4,4))[0]
        mover = component(game,handle)
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        bridge, bridge_hash = prepare(game.pid, process,
            names=('SpidyStart', 'SpidyStop', 'SpidySubmit', 'SpidyBridgeData', 'SpidyBridgeSample'))
        config = struct.pack('<4I3Q', 0x53424346, 1, 40, game.pid, game.base,
                             int(hero['object'], 16), int(hero['actor_record'], 16))
        code = call_with_payload(process, bridge['SpidyStart'], config)
        if code:
            raise RuntimeError(f'Input bridge start: {code}')
        bridge_active = True
        queue = discover_queue(game, process)
        b = bridge_snapshot(game, bridge['SpidyBridgeData'])
        camera_deadline=time.monotonic()+10
        while a.auto_launch and (not b or not b['matched']) and time.monotonic()<camera_deadline and alive(game):
            time.sleep(.1)
            b=bridge_snapshot(game,bridge['SpidyBridgeData'])
        if not b or not b['matched']:
            raise RuntimeError('The active game camera does not follow the discovered player')
        bridge_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_bridge.dll')
        rays, ray_hash = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll',
            ROOT/'reports/ray-modules', ('SpidyRayStart', 'SpidyRaySubmit', 'SpidyRayStop',
                                       'SpidyRaySample', 'SpidyRayData', 'SpidySwingData'))
        ray_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_ray_bridge.dll')
        motion, motion_hash = prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
            ROOT/'reports/motion-modules',('SpidyMotionStart','SpidyMotionSubmit','SpidyMotionSample',
                                         'SpidyMotionStop','SpidyMotionData'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        xr, xr_hash = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
            ROOT/'reports/stereo-modules', ('SpidyXrStart', 'SpidyXrStop', 'SpidyXrKeepAlive', 'SpidyXrData',
                                           'SpidyGpuData', 'SpidyXrTimingData', 'SpidyAppearanceData',
                                           'SpidyStereoFrames'))
        config = struct.pack('<4I7Q2IfI', 0x53585243, 5, 88, game.pid, game.base, queue,
                             bridge_module, ray_module, motion_module, int(hero['actor_record'],16), mover,
                             int(a.seconds*1000), a.size, a.swing_speed,
                             int(a.capture_images) | (2 if a.overlay_webs else 0) |
                             (4 if a.stock_monitor_view else 0))
        code = call_with_payload(process, xr['SpidyXrStart'], config)
        if code:
            raise RuntimeError(f'Game XR start: {code}')
        xr_active = True
        started = time.monotonic()
        samples = deque(maxlen=12000)
        ray_samples = deque(maxlen=12000)
        swing_samples = deque(maxlen=12000)
        motion_samples = deque(maxlen=12000)
        appearance = None
        eye_jobs = None
        previous = None
        printed_dimensions=False
        renewed=0
        stop_requested=False
        def request_stop(_signal,_frame):
            nonlocal stop_requested
            stop_requested=True
            print('Stopping VR and restoring game hooks...',flush=True)
        previous_signal=signal.signal(signal.SIGINT,request_stop)
        while not stop_requested and (not a.seconds or time.monotonic()-started < a.seconds+40) and alive(game):
            if a.stop_after and time.monotonic()-started>=a.stop_after: break
            if not a.seconds and time.monotonic()-renewed>=1:
                if call_remote(process,xr['SpidyXrKeepAlive']): break
                renewed=time.monotonic()
            sample = snapshot(game, xr['SpidyXrData'])
            if sample:
                sample['seconds'] = time.monotonic()-started
                appearance = appearance_snapshot(game, xr['SpidyAppearanceData']) or appearance
                sample['appearance'] = appearance
                # Eye job copies the game dropped unrendered (reclaimed by age).
                eye_jobs = frame_snapshot(game, xr['SpidyStereoFrames']) or eye_jobs
                sample['eye_jobs_reclaimed'] = eye_jobs['reclaimed'] if eye_jobs else None
                samples.append(sample)
                if sample['eye_width'] and not printed_dimensions:
                    print(f"Rendering {sample['eye_width']} x {sample['eye_height']} pixels per eye.",flush=True)
                    printed_dimensions=True
                aim = ray_snapshot(game, rays['SpidyRayData'])
                if aim and aim['serial'] and (not ray_samples or aim['serial'] != ray_samples[-1]['serial']):
                    ray_samples.append(aim)
                swing = swing_snapshot(game,rays['SpidySwingData'])
                if swing and swing['steps'] and (not swing_samples or swing['steps'] != swing_samples[-1]['steps']):
                    swing_samples.append(swing)
                movement = motion_snapshot(game, motion['SpidyMotionData'])
                if movement and movement['steps'] and (not motion_samples or movement['steps'] != motion_samples[-1]['steps']):
                    motion_samples.append(movement)
                state = (sample['status'], sample['message'])
                if state != previous:
                    print(sample['message'], flush=True)
                    previous = state
                if sample['status'] in (4, 5):
                    break
            time.sleep(.05)
        if not alive(game):
            xr_active=bridge_active=False
            result=dict(pid=game.pid,game_exited=True,eye_size=a.size,samples=list(samples),
                        swing_speed=a.swing_speed, motion_hash=motion_hash, xr_hash=xr_hash, ray_hash=ray_hash,
                        motion_samples=list(motion_samples), appearance=appearance, eye_jobs=eye_jobs,
                        frame_rates=frame_rates(list(samples)),final=samples[-1] if samples else None,
                        swing_samples=list(swing_samples),ray_samples=list(ray_samples))
            a.output.parent.mkdir(parents=True,exist_ok=True)
            a.output.write_text(json.dumps(result,indent=2)+'\n')
            print(f'Game closed. Session report: {a.output.resolve()}',flush=True)
            return 0
        xr_stop = call_remote(process, xr['SpidyXrStop'])
        xr_active = xr_stop != 0
        bridge_stop = call_remote(process, bridge['SpidyStop'])
        bridge_active = bridge_stop != 0
        result = dict(pid=game.pid, module_base=hex(game.base), bridge_hash=bridge_hash, xr_hash=xr_hash,
            eye_size=a.size,capture_images=a.capture_images,timing=timing_snapshot(game,xr['SpidyXrTimingData']),
            ray_hash=ray_hash, ray_samples=list(ray_samples), rays=ray_snapshot(game, rays['SpidyRayData']),
            motion_hash=motion_hash, swing_speed=a.swing_speed, swing_samples=list(swing_samples),
            motion_samples=list(motion_samples), appearance=appearance_snapshot(game,xr['SpidyAppearanceData']),
            eye_jobs=frame_snapshot(game, xr['SpidyStereoFrames']) or eye_jobs,
            swing=swing_snapshot(game,rays['SpidySwingData']),
            samples=list(samples), final=snapshot(game, xr['SpidyXrData']), gpu=gpu_snapshot(game, xr['SpidyGpuData']),
            bridge=bridge_snapshot(game, bridge['SpidyBridgeData']), xr_stop=xr_stop, bridge_stop=bridge_stop)
        result['game_entries_restored'] = all(game.read(game.base+rva,16) == code
                                              for rva,code in original_entries.items())
        result['frame_rates'] = frame_rates(list(samples))
        a.output.parent.mkdir(parents=True, exist_ok=True)
        try:
            if a.capture_images:
                result.update(save_eye_images(game, result['gpu'], a.output))
        except (OSError, ValueError, RuntimeError) as error:
            result['image_capture_error'] = str(error)
        result['passed'] = accepted(result)
        a.output.write_text(json.dumps(result, indent=2)+'\n')
        final = result['final']
        print(json.dumps(dict(final=final, gpu=result['gpu'], appearance=result['appearance'],
                              xr_stop=xr_stop, bridge_stop=bridge_stop), indent=2))
        if result['timing']:
            print('Mean milliseconds after warmup: '+', '.join(f'{k}={v["mean_ms"]:.2f}' for k,v in result['timing'].items()),flush=True)
        if result['frame_rates']:
            rates=result['frame_rates']
            print(f"New native scene pairs: {rates['new_scene_fps']:.1f}/s; "
                  f"XR submissions including reuse: {rates['submitted_fps']:.1f}/s; "
                  f"reused pairs: {rates['reused_fps']:.1f}/s.",flush=True)
        print(f"Test {'passed' if result['passed'] else 'failed'}; report: {a.output.resolve()}", flush=True)
        return 0 if result['passed'] else 1
    finally:
        if previous_signal is not None: signal.signal(signal.SIGINT,previous_signal)
        if xr_active and alive(game):
            call_remote(process, xr['SpidyXrStop'])
        if bridge_active and alive(game):
            call_remote(process, bridge['SpidyStop'])
        if process:
            close(process)
        settle_display(alive(game))
        game.close()


if __name__ == '__main__':
    try:
        with LauncherLock(): sys.exit(main())
    except KeyboardInterrupt:
        print('VR launch cancelled.',file=sys.stderr)
        settle_display(game_running())
        sys.exit(130)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'Spidy VR unavailable: {error}', file=sys.stderr)
        settle_display(game_running())
        sys.exit(2)
