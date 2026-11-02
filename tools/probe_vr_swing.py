"""Swing through the city with VR's eye views and the headset's frame rate, in the running game without a headset.

What players see in a swing - buildings coming in late, uneven motion - happens while the player moves, which
probes that hold the eyes still at one spot never do. This one renders the eyes as a VR session does (views 61:
the game's own view on the head and resting, at --width x --height, the scene at --upscale percent), sends them
an eye command --rate times a second from a thread of its own as the XR worker does once a headset frame, and
has the game's own swing carry the hero (Spidy's virtual controller: right trigger and the left stick held
forward, turning a little now and then). In phases (--phases name:seconds:pacing[:cone[:reach[:blur]]], pacing
1 one pair a command, 0 the game's frames as fast as it draws them, cone the far buildings' cone margin in
degrees, reach how far they draw their full models in percent of the game's distance, blur 1 the eyes keep the
game's speed blur as before November 1, 0 they go without it) it reports what is below. A phase's name picks
what the hero does: stand... nothing, zip... the swing with a web zip (A) every 1.5 s, dive... the swing let go
4 s of every 9, point... the swing with the left trigger held 0.7 s of every 3 (a point launch where the game
offers one), launch... a jump, a climb and a 50 m/s dive every 8 s through the movement bridge, as fast as a
Spidy swing goes and the game's own never does, fall... a jump and a climb to some 100 m every 10 s and the
game's own fall from there, boost... the swing carried at 46 m/s 1.5 s of every 4; any other the swing.


    frames      the game's frame rate, and the spread of its frame intervals (tools/probe_frame_pacing.py)
    pairs       eye pairs a second, and how old the newest finished pair was at each command (the age's
                spread is the uneven motion a headset shows)
    pacing      SpidyPaceData: frames that waited for a command, and for how long
    streaming   SpidyStreamData: the scene's slot pool (size, in use, the most), the room checks it refused
                far buildings and nearby props, and the texture budget and the card's memory
    hero        how far and how fast the game's swing carried the player
    speed_blur  SpidyBlurData: how often the game asked its speed blur of its own view and of the eyes (a frame
                and a view at a time), and with --pictures the left eye's pictures taken while it did
    gpu         nvidia-smi: utilization, clock, power

Start the game with tools/probe_menu_pad.py start, load a save that leaves the hero outdoors with room to
swing (pad a --until-player), wait half a minute for its streaming to settle, and run this with the game
window in front. Every Spidy module starts once per game process: one run per game.
"""
import argparse
import ctypes as c
import json
import math
import pathlib
import struct
import subprocess
import sys
import threading
import time
from bridge_game import ROOT, prepare
from capture_game_state import Game, LIVE_VTABLES, find_game, open_process, close
from capture_movement import component
from observe_game import call_remote, call_with_payload
from probe_game_screen import game_window, user32
from probe_stereo import frame_snapshot, render_memory_snapshot, scene_pool_snapshot, snapshot as stereo_snapshot
from probe_stereo_gpu import discover_queue, snapshot as gpu_snapshot
from probe_vr_load import Gpu, eye_command, eye_rig
from run_game_vr import (GAME_NAME, blur_snapshot, documents_folder, game_memory, pace_snapshot, rgb_rows,
                         stream_snapshot, write_rgb_png)
from vr_launcher import enlarge_render_memory

EXPORTS = ('SpidyStart', 'SpidyStop', 'SpidyStereoData', 'SpidySetEyes', 'SpidyStereoFrames', 'SpidyGpuStart',
           'SpidyGpuStop', 'SpidyGpuData', 'SpidyEyeTemporal', 'SpidyEyeShadows', 'SpidyMonitorRate',
           'SpidyImpostorReach', 'SpidyBuildingCone', 'SpidyStreamData', 'SpidyPaceData', 'SpidyEyePacing',
           'SpidyPadSubmit', 'SpidyBlurData', 'SpidyEyeSpeedBlur', 'SpidyGpuFreeze')


def pad_state(right_trigger, x, y, hold_ms, buttons=0, left_trigger=0.):
    """game_pad's State and lease: buttons, triggers, sticks (left x, y, right x, y)."""
    return struct.pack('<H2B4hI', buttons, round(255*left_trigger), round(255*right_trigger), round(32767*x),
                       round(32767*y), 0, 0, hold_ms)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--width', type=int, default=2688, help='eye width in pixels (Quest 3 through Virtual Desktop)')
    p.add_argument('--height', type=int, default=2880, help='eye height in pixels')
    p.add_argument('--upscale', type=int, default=77, help="the eyes' scene in percent of their size per side")
    p.add_argument('--rate', type=float, default=90, help='eye commands a second (the headset frame rate)')
    p.add_argument('--phases', default='swing_paced:40:1,swing_free:40:0,swing_paced_again:30:1',
                   help='name:seconds:pacing[:cone degrees[:reach percent[:blur]]], in turn')
    p.add_argument('--pictures', type=int, default=0,
                   help="left eye pictures a phase saves while the game asks its speed blur, a second or more "
                        "apart; reads every pair back, which costs GPU time (compare no frame rates then)")
    p.add_argument('--turn-every', type=float, default=10, help='seconds between turns of the swing')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/vr-swing.json')
    a = p.parse_args()
    phases = []
    for text in a.phases.split(','):
        parts = text.split(':')
        if len(parts) not in (3, 4, 5, 6) or parts[2] not in ('0', '1') or parts[5:] not in ([], ['0'], ['1']):
            p.error('Phases are name:seconds:pacing[:cone[:reach[:blur]]]')
        phases.append(dict(name=parts[0], seconds=float(parts[1]), pacing=int(parts[2]),
                           cone=int(parts[3]) if len(parts) >= 4 else 30,
                           reach=int(parts[4]) if len(parts) >= 5 else 150,
                           blur=int(parts[5]) if len(parts) == 6 else 0))
    game = Game(find_game())
    process = None
    stops = []
    gpu = Gpu()
    result = dict(width=a.width, height=a.height, upscale=a.upscale, rate=a.rate, pictures=a.pictures, phases=[])
    stop = threading.Event()
    try:
        window = game_window(game.pid)
        if user32.GetForegroundWindow() != window:
            raise RuntimeError('The game window is not in front, so the game is paused. Bring it to the front.')
        objects = game.registered_candidates()
        heroes = [o for o in objects if o['kind'] == 'hero_local']
        cameras = [o for o in objects if o['kind'] == 'hero_camera_manager' and 'camera' in o]
        if len(heroes) != 1 or not cameras:
            raise RuntimeError('Exactly one local hero and its camera are required. Load a save first.')
        hero_transform = int(heroes[0]['actor_transform'], 16)
        camera_object = int(cameras[0]['object'], 16)
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        render = enlarge_render_memory(game.pid)
        result['scene_pool'] = scene_pool_snapshot(game, render['SpidyScenePoolData'])
        print('scene pool:', json.dumps(result['scene_pool']), flush=True)
        queue = discover_queue(game, process)
        dll, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
                              ROOT/'reports/stereo-modules', EXPORTS)
        result['dll_sha256'] = digest

        def where():
            """The hero's feet and the way the game's camera looks over him (horizontal)."""
            hero = game.transform(hero_transform)
            camera = game.transform(camera_object+0x744)
            if not hero:
                return None, None
            feet = tuple(hero['position'])
            if camera:
                dx, dz = feet[0]-camera['position'][0], feet[2]-camera['position'][2]
                if math.hypot(dx, dz) > .1:
                    return feet, (dx/math.hypot(dx, dz), 0., dz/math.hypot(dx, dz))
            return feet, None

        feet, heading = where()
        if not feet:
            raise RuntimeError('The hero has no transform')
        state = dict(feet=feet, heading=heading or (0., 0., 1.), serial=time.time_ns()//1000)
        sent, ages, call_ms = {}, [], []
        lock = threading.Lock()

        def commands():
            """An eye command every 1/--rate s on the clock's grid, as the XR worker sends one a headset frame,
            from where the hero is then: the eyes stay on him at a Spidy swing's speeds as a headset's do."""
            period = 1/a.rate
            due = time.perf_counter()
            while not stop.is_set():
                due += period
                hero = game.transform(hero_transform)
                with lock:
                    if hero:
                        state['feet'] = tuple(hero['position'])
                    state['serial'] += 1
                    serial, feet, heading = state['serial'], state['feet'], state['heading']
                began = time.perf_counter()
                code = call_with_payload(process, dll['SpidySetEyes'], eye_command(serial, eye_rig(feet, heading),
                                                                                    feet))
                now = time.perf_counter()
                if code and code != 4003:
                    state['error'] = f'Eye command: {code}'
                    return
                pair = gpu_snapshot(game, dll['SpidyGpuData'])
                with lock:
                    sent[serial] = began
                    call_ms.append(1000*(now-began))
                    if pair and pair['captured_serial'] in sent:
                        ages.append((now, 1000*(now-sent[pair['captured_serial']])))
                wait = due-time.perf_counter()
                if wait > 0:
                    time.sleep(wait)
                elif wait < -period:
                    due = time.perf_counter()

        steer = dict(x=0., trigger=1., jump=False, forward=False, left=0.)

        def pad():
            """The game's swing: right trigger and the left stick forward, re-sent before the lease ends; A
            once when the hero has stopped against something or zips (`jump`)."""
            while not stop.is_set():
                jump, steer['jump'] = steer['jump'], False
                forward = 1. if steer['trigger'] or steer['forward'] else 0.
                code = call_with_payload(process, dll['SpidyPadSubmit'],
                                         pad_state(steer['trigger'], steer['x'], forward, 600,
                                                   0x1000 if jump else 0, steer['left']))
                if code:
                    state['error'] = f'Virtual controller: {code}'
                    return
                time.sleep(.25)

        # The eyes as a VR session makes them: the GPU bridge staged (as the XR worker copies pairs; with
        # --pictures each pair read back instead), the scene upscaled with the game's temporal anti-aliasing,
        # shared sun shadows, the monitor at 5 a second.
        copy, read_back = (0, 1) if a.pictures else (2, 0)
        code = call_with_payload(process, dll['SpidyGpuStart'],
                                 struct.pack('<4I2Q4I', 0x53475043, 3, 48, game.pid, game.base, queue, 0, copy,
                                             read_back, 0))
        if code:
            raise RuntimeError(f'GPU bridge start: {code}')
        stops.append(('gpu', dll['SpidyGpuStop']))
        code = call_remote(process, dll['SpidyEyeTemporal'], a.upscale | 7 << 8) or \
            call_remote(process, dll['SpidyEyeShadows'], 1) or call_remote(process, dll['SpidyMonitorRate'], 5)
        if code:
            raise RuntimeError(f'Eye settings: {code}')
        code = call_with_payload(process, dll['SpidyStart'],
                                 struct.pack('<4IQ4I', 0x53534346, 1, 40, game.pid, game.base, 0, 61, a.width,
                                             a.height))
        if code:
            raise RuntimeError(f'Eye views start: {code}')
        stops.insert(0, ('eyes', dll['SpidyStop']))
        # launch... and fall... phases carry the hero through the movement bridge, as a Spidy swing does.
        motion = None
        if any(phase['name'].startswith(('launch', 'fall', 'boost')) for phase in phases):
            LIVE_VTABLES['hero_mover'] = 0x38b2c98
            movers = [o for o in game.registered_candidates() if o['kind'] == 'hero_mover' and
                      o['actor_record'] == heroes[0]['actor_record']]
            if len(movers) != 1:
                raise RuntimeError('Exactly one local movement manager is required')
            mover = component(game, struct.unpack('<I', game.read(int(movers[0]['object'], 16)+0xdb4, 4))[0])
            motion, _ = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
                                ROOT/'reports/motion-modules', ('SpidyMotionStart', 'SpidyMotionSubmit',
                                                                'SpidyMotionStop'))
            code = call_with_payload(process, motion['SpidyMotionStart'],
                                     struct.pack('<4I3QIf2I', 0x534d5643, 2, 56, game.pid, game.base,
                                                 int(heroes[0]['actor_record'], 16), mover, 0, 70., 0, 0))
            if code:
                raise RuntimeError(f'Movement bridge start: {code}')
            stops.append(('motion', motion['SpidyMotionStop']))
        motion_serial = 0

        def drive(velocity):
            """The hero's velocity for the next 150 ms (the movement bridge's lease)."""
            nonlocal motion_serial
            motion_serial += 1
            code = call_with_payload(process, motion['SpidyMotionSubmit'],
                                     struct.pack('<4IQ2I3fI', 0x534d564d, 1, 48, 1, motion_serial, 150, 0,
                                                 *velocity, 0))
            if code:
                raise RuntimeError(f'Movement lease: {code}')

        workers = [threading.Thread(target=commands, daemon=True), threading.Thread(target=pad, daemon=True)]
        for worker in workers:
            worker.start()
        time.sleep(3)  # the eye views are made and render
        # The speed blur's hook at work: the game's blur scheduler (1841bf0) called with the left eye's view goes
        # to Spidy's hook, which counts it and returns before any of the game's code runs. Only where the entry
        # is the hook's jump, so the game's own scheduler never runs on this thread.
        views = stereo_snapshot(game, dll['SpidyStereoData'])
        eye_view = views['eyes'][0]['view'] if views else 0
        before = blur_snapshot(game, dll['SpidyBlurData'])
        if eye_view and before and before['on'] and not before['eyes_keep'] and \
                game.read(game.base+0x1841bf0, 1) == b'\xe9':
            call_remote(process, game.base+0x1841bf0, eye_view)
            after = blur_snapshot(game, dll['SpidyBlurData'])
            result['blur_hook_check'] = dict(eye_view=hex(eye_view),
                                             eyes_left_out=after['eyes_left_out']-before['eyes_left_out'],
                                             game_views=after['game_views']-before['game_views'],
                                             eyes_kept=after['eyes_kept']-before['eyes_kept'])
        else:
            result['blur_hook_check'] = dict(skipped=True, on=before and before['on'],
                                             entry=game.read(game.base+0x1841bf0, 1).hex())
        print('speed blur hook check:', json.dumps(result['blur_hook_check']), flush=True)

        def sample():
            memory = render_memory_snapshot(game, render['SpidyRenderMemoryData'])
            frames = frame_snapshot(game, dll['SpidyStereoFrames'])
            feet, heading = where()
            if feet:
                with lock:
                    state['feet'] = feet
                    if heading:
                        state['heading'] = heading
            return dict(t=time.perf_counter(), frames=memory['frames'] if memory else None,
                        pairs=frames['left_copies'] if frames else None, feet=feet,
                        stream=stream_snapshot(game, dll['SpidyStreamData']),
                        pace=pace_snapshot(game, dll['SpidyPaceData']),
                        blur=blur_snapshot(game, dll['SpidyBlurData']))

        def asked(blur):
            """Times the game asked its speed blur of an eye (left out or kept)."""
            return blur['eyes_left_out']+blur['eyes_kept'] if blur else 0

        def picture(path):
            """The newest left eye, read back, at half size."""
            code = call_remote(process, dll['SpidyGpuFreeze'])
            pair = gpu_snapshot(game, dll['SpidyGpuData'])
            if code or not pair or pair['width'] != a.width or pair['height'] != a.height:
                raise RuntimeError(f'Eye capture: {code} {pair}')
            pixels = game.read(pair['left_pixels'], a.width*a.height*4)
            columns, rows, rgb = rgb_rows(pixels, a.width*4, 0, 0, a.width, a.height, 2)
            write_rgb_png(path, columns, rows, rgb)

        pictures_folder = a.output.with_name(a.output.stem+'-eyes')
        for phase in phases:
            if call_remote(process, dll['SpidyEyePacing'], phase['pacing']) or \
                    call_remote(process, dll['SpidyBuildingCone'], phase['cone']) or \
                    call_remote(process, dll['SpidyImpostorReach'], phase['reach']) or \
                    call_remote(process, dll['SpidyEyeSpeedBlur'], phase['blur']):
                raise RuntimeError('Pacing, cone, reach or speed blur refused')
            pacing_report = a.output.with_name(f"{a.output.stem}-pacing-{phase['name']}.json")
            timing = subprocess.Popen([sys.executable, str(ROOT/'tools/probe_frame_pacing.py'), '--seconds',
                                       str(phase['seconds']-3), '--window', '5', '--output', str(pacing_report)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            began = time.perf_counter()
            with lock:
                ages.clear()
                call_ms.clear()
            samples = [sample()]
            next_turn = began+a.turn_every
            unstuck = 0.
            pictures, pictured = [], 0.
            # A phase named stand... leaves the controller alone: the hero stands where the last one left him.
            # zip...: A in the air every 1.5 s too (the game's web zip); dive...: the trigger let go 4 s of
            # every 9 with the stick still forward, so the hero falls from the swing; launch...: every 8 s a jump,
            # 35 m/s up for 2.2 s and a 50 m/s dive the way the game's camera looks for 2.8 s (movement bridge);
            # fall...: every 10 s a jump, 35 m/s up for 3 s, then the game's own fall from some 100 m; boost...:
            # the swing, carried at 46 m/s the way the camera looks for 1.5 s of every 4 (its rope still on).
            standing = phase['name'].startswith('stand')
            zipping, diving = phase['name'].startswith('zip'), phase['name'].startswith('dive')
            launching, pointing = phase['name'].startswith('launch'), phase['name'].startswith('point')
            falling, boosting = phase['name'].startswith('fall'), phase['name'].startswith('boost')
            cycle_seconds, climb_until = (10, 3.4) if falling else (8, 2.6)
            carried = launching or falling
            steer['trigger'], steer['x'] = (0., 0.) if standing or carried else (1., steer['x'])
            steer['forward'], steer['left'] = diving, 0.
            next_zip = began+1.5 if zipping else float('inf')
            if standing or carried:
                next_turn = unstuck = float('inf')
            jumped = -1
            emitters_seen = 0
            while time.perf_counter()-began < phase['seconds']:
                if state.get('error'):
                    raise RuntimeError(state['error'])
                if user32.GetForegroundWindow() != window:
                    raise RuntimeError('Another window came to the front (the game pauses). Stopped.')
                now = time.perf_counter()
                if now >= next_turn:
                    steer['x'] = .7 if steer['x'] <= 0 else -.7
                    next_turn = now+a.turn_every
                elif now >= next_turn-a.turn_every+2.5 and now >= unstuck:
                    steer['x'] = 0.
                if now >= next_zip:
                    steer['jump'] = True
                    next_zip = now+1.5
                if pointing:
                    steer['left'] = 1. if (now-began) % 3 < .7 else 0.
                if diving:
                    steer['trigger'] = 0. if (now-began) % 9 >= 5 else 1.
                # Half a second between samples; with --pictures, the left eye while the game asks its blur.
                until = time.perf_counter()+.5
                seen = asked(samples[-1]['blur'])
                while time.perf_counter() < until:
                    time.sleep(.05)
                    # The visual effects' radial blurs waiting for this frame's blur pass (7a81000 +2c4, which
                    # 19ca590 reads and empties): whether any played at all, near a view or not.
                    raw = game.read(game.base+0x7a812c4, 4)
                    if len(raw) == 4 and struct.unpack('<I', raw)[0]:
                        emitters_seen += 1
                    if boosting and (time.perf_counter()-began) % 4 < 1.5:
                        with lock:
                            heading = state['heading']
                        drive((45*heading[0], -10., 45*heading[2]))
                    if carried:
                        cycle = (time.perf_counter()-began) % cycle_seconds
                        if cycle < .4 and jumped != int((time.perf_counter()-began)//cycle_seconds):
                            jumped = int((time.perf_counter()-began)//cycle_seconds)
                            steer['jump'] = True
                        with lock:
                            heading = state['heading']
                        if .4 <= cycle < climb_until:
                            drive((0., 35., 0.))
                        elif launching and 2.6 <= cycle < 5.4:
                            drive((30*heading[0], -40., 30*heading[2]))
                        # The eyes keep up with the hero.
                        feet, _ = where()
                        if feet:
                            with lock:
                                state['feet'] = feet
                    if len(pictures) < a.pictures and time.perf_counter()-pictured >= 1:
                        blur = blur_snapshot(game, dll['SpidyBlurData'])
                        # A launch's dive and a point launch's zip are pictured whether the game asks its
                        # blur or not.
                        moment = time.perf_counter()-began
                        anyway = (launching and 3.4 <= moment % 8 < 4.4) or (pointing and .6 <= moment % 3 < 1.2) \
                            or (falling and 5.5 <= moment % 10 < 7) or (boosting and .8 <= moment % 4 < 1.4)
                        if asked(blur) > seen or anyway:
                            pictures_folder.mkdir(parents=True, exist_ok=True)
                            name = f"{phase['name']}-{len(pictures)}.png"
                            picture(pictures_folder/name)
                            pictured = time.perf_counter()
                            pictures.append(dict(file=name, t=round(pictured-began, 2),
                                                 blur_asked=asked(blur) > seen))
                        seen = asked(blur)
                samples.append(sample())
                # Stopped against something for a second: jump and turn hard for a second and a half.
                recent = [s['feet'] for s in samples[-3:] if s['feet']]
                if len(recent) == 3 and math.dist(recent[0], recent[-1]) < 2 and now >= unstuck:
                    steer['jump'] = True
                    steer['x'] = 1. if steer['x'] <= 0 else -1.
                    unstuck = now+1.5
            _, problem = timing.communicate(timeout=30)
            first, last = samples[1] if len(samples) > 2 else samples[0], samples[-1]
            seconds = last['t']-first['t']
            gpu_rows = gpu.window(first['t'], last['t'])
            with lock:
                age_values = sorted(age for _, age in ages)
                calls = sorted(call_ms)
            travelled = [math.dist(x['feet'], y['feet']) for x, y in zip(samples, samples[1:]) if x['feet'] and
                         y['feet']]
            stream, pace = last['stream'] or {}, last['pace'] or {}
            blur_first, blur_last = samples[0]['blur'] or {}, last['blur'] or {}
            pick = lambda values, q: round(values[min(len(values)-1, int(q*len(values)))], 2) if values else None
            summary = dict(
                phase=phase['name'], pacing=phase['pacing'], cone=phase['cone'], reach=phase['reach'],
                seconds=round(seconds, 1),
                frames_per_second=round((last['frames']-first['frames'])/seconds, 1) if last['frames'] else None,
                pairs_per_second=round((last['pairs']-first['pairs'])/seconds, 1) if last['pairs'] else None,
                newest_pair_age_ms=dict(p10=pick(age_values, .1), median=pick(age_values, .5),
                                        p90=pick(age_values, .9), spread=round(pick(age_values, .9) -
                                                                               pick(age_values, .1), 2)
                                        if age_values else None),
                command_call_ms=dict(median=pick(calls, .5), p90=pick(calls, .9)),
                pace={k: pace.get(k) for k in ('on', 'frames', 'waits', 'timeouts', 'wait_mean_ms', 'wait_most_ms',
                                               'headset_period_ms')},
                stream={k: stream.get(k) for k in ('pool_capacity', 'pool_used', 'pool_most', 'pool_full_frames',
                                                   'cone_margin_deg', 'building_checks', 'building_refusals',
                                                   'zone_checks', 'zone_refusals', 'pool_most_asked',
                                                   'texture_budget_mb', 'video_usage_mb', 'video_budget_mb',
                                                   'pool_checks_counted')},
                pool_used_most_in_phase=max((s['stream'] or {}).get('pool_used', 0) for s in samples),
                speed_blur=dict(blur=phase['blur'], on=blur_last.get('on'),
                                **{k: blur_last.get(k, 0)-blur_first.get(k, 0)
                                   for k in ('game_views', 'eyes_left_out', 'eyes_kept')},
                                emitters_seen=emitters_seen, pictures=pictures),
                hero_metres=round(sum(travelled), 1),
                hero_speed_mps=round(sum(travelled)/seconds, 1),
                gpu=dict(utilization=round(sum(r[1] for r in gpu_rows)/len(gpu_rows), 1),
                         clock_mhz=round(sum(r[2] for r in gpu_rows)/len(gpu_rows)),
                         power_w=round(sum(r[3] for r in gpu_rows)/len(gpu_rows)),
                         memory_mb=round(gpu_rows[-1][4])) if gpu_rows else None)
            if timing.returncode == 0:
                summary['frame_intervals'] = json.loads(pacing_report.read_text())['counters']
            else:
                summary['frame_intervals'] = dict(error=problem.strip()[-300:])
            summary['samples'] = [dict(t=round(s['t']-began, 2), frames=s['frames'], pairs=s['pairs'],
                                       feet=[round(v, 1) for v in s['feet']] if s['feet'] else None,
                                       pool_used=(s['stream'] or {}).get('pool_used'),
                                       building_refusals=(s['stream'] or {}).get('building_refusals'),
                                       zone_refusals=(s['stream'] or {}).get('zone_refusals'),
                                       video_usage_mb=(s['stream'] or {}).get('video_usage_mb'),
                                       cone=(s['stream'] or {}).get('cone_margin_deg'),
                                       waits=(s['pace'] or {}).get('waits'),
                                       blur_asked=asked(s['blur'])) for s in samples]
            result['phases'].append(summary)
            print(json.dumps({k: v for k, v in summary.items() if k not in ('samples', 'frame_intervals')}),
                  flush=True)
            intervals = summary['frame_intervals'].get('frames', {}).get('all') if 'error' not in \
                summary['frame_intervals'] else None
            if intervals:
                print('    frame intervals:', json.dumps(intervals), flush=True)
        steer['trigger'] = 0.
        try:
            log = (documents_folder()/GAME_NAME/(GAME_NAME+'.log')).read_text(encoding='utf-8', errors='replace')
            result['game_memory'] = game_memory(log)
        except OSError as error:
            result['game_memory'] = str(error)
    finally:
        stop.set()
        time.sleep(.3)
        while stops:
            label, export = stops.pop(0)
            try:
                result[f'{label}_stop'] = call_remote(process, export)
            except OSError:
                pass
        gpu.stop()
        if process:
            close(process)
        game.close()
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=1)+'\n')
        print(f'Report: {a.output.resolve()}', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'VR swing probe failed: {error}', file=sys.stderr)
        sys.exit(2)
