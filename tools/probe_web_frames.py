"""Check in free roam, without a headset, that a game web starts where the eye camera expects it at speed.

The probe swings with the VR launcher's settings and reels toward the anchor for
speed. A left eye camera travels with the player and looks sideways, across the
direction of travel. A game web is held from a fixed point 0.6 m in front of
that camera. With the camera placed for the frame being rendered, the web's
first point stays at that point at any speed. A camera placed a frame late (as
in builds before 2026-10-05, compared here in the first half of the swing) sees
it move in the direction of travel by one frame of player travel.

Reported for each placement: how far the rope's first point was from the
expected point, measured in the game for every rendered frame, with the player
speed and the travel per frame. Left-eye images are saved with a green cross at
the expected start of the web.

Also reported: the game's per-frame render memory. Two consecutive frames must
fit in its ring. The game's own ring is 128 MB, and three scene views overflow
it when they start and in heavy views, which drops eye frames. `--views 13`
moves the game's own view to the eyes as a VR session does. Start the game with
`python tools\vr_launcher.py` to give it Spidy's larger ring, or with
`--ring 0` there to see the overflow on the game's own.

With `--views 13` the report's `monitor_view` says whether that view, which the
monitor shows, and the HUD panel were placed from the same player position as
the eyes in every frame, and how far the head the HUD's markers were projected
from was from the frame's head. 0.2.8 and 0.2.9 placed the view and the panel a
frame of travel behind the eyes: the monitor showed the player's back in a
swing. `--modules DIR` takes spidy_stereo_probe.dll from another build (a
play folder's), for a comparison.

Load a save, stand or perch somewhere with open space, and run this with the
game window in the foreground. The game must be restarted before another run.
"""
import argparse
import ctypes as c
import json
import math
import pathlib
import struct
import sys
import time
from bridge_game import ROOT, HOOKS, prepare, snapshot as bridge_snapshot
from capture_game_state import Game, LIVE_VTABLES, find_game, open_process, close
from capture_movement import component
from inspect_game import PE
from observe_game import call_remote, call_with_payload, modules
from probe_game_swing import snapshot as swing_snapshot, flight_summary
from probe_native_motion import snapshot as motion_snapshot
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from probe_stereo import snapshot as stereo_snapshot, frame_snapshot, render_memory_snapshot
from probe_stereo_gpu import discover_queue, snapshot as gpu_snapshot
from run_game_vr import appearance_snapshot, hud_snapshot, rgb_rows, write_rgb_png
from vr_launcher import enlarge_render_memory, RENDER_MEMORY_HOOKS

ENTRIES = (*HOOKS, 0x2e67010, 0x1fbe360, 0x1fbda50, 0xa7b3a0, 0x1f9db60, 0x18a0bb0, 0x189bd30, 0x186cc00,
           0x1846c20, 0x19223e0, 0x189e310, 0x189e3a0, 0x1873470, 0x17991a0, 0x1920310, 0x1899ab0,
           0x1920240, 0x676dd0)
HEAD = 1.6          # eye height above the feet, metres
REACH, DROP = .6, .15   # web start: in front of and below the eye centre
HALF_IPD = .032
FOV = math.pi/4     # half angle of the square eye image


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def scale(a, s): return tuple(x*s for x in a)
def norm(a): return math.sqrt(sum(x*x for x in a))


def camera(heading):
    """Right, down, forward of a level camera that sees travel along `heading` as motion to the right."""
    right = (heading[0], 0., heading[2])
    return right, (0., -1., 0.), (right[2], 0., -right[0])


def rig(feet, heading):
    """Both eye matrices, the web start, a far web target, and the start relative to the left eye."""
    right, down, forward = camera(heading)
    centre = add(feet, (0, HEAD, 0))
    eyes = []
    for side in (-HALF_IPD, HALF_IPD):
        position = add(centre, scale(right, side))
        eyes.append((*right, 0, *down, 0, *forward, 0, *position, 1))
    wrist = add(add(centre, scale(forward, REACH)), scale(down, DROP))
    target = add(add(add(wrist, scale(forward, 40)), scale(down, -30)), scale(right, 8))
    expected = add(add(scale(forward, REACH), scale(down, DROP)), scale(right, HALF_IPD))
    return eyes, wrist, target, expected


def eye_command(serial, eyes, feet):
    """native_eyes::Command v2: both eyes with a 90-degree lens, anchored to the sampled player position."""
    raw = struct.pack('<4IQ2I', 0x53455043, 2, 208, 1, serial, 250, 0)
    for eye in eyes:
        raw += struct.pack('<20f', *eye, -FOV, FOV, -FOV, FOV)
    return raw+struct.pack('<3fI', *feet, 1)


def web_command(feet, wrist, target, attached=True):
    """native_webs::ProbeCommand: the left hand holds one game web."""
    raw = struct.pack('<4I3f4x', 0x5357424d, 1, 112, 200, *feet)
    raw += struct.pack('<2Iq6f', int(attached), 1, 1, *target, *wrist)
    return raw+struct.pack('<2Iq6f', 0, 0, 0, *([0.]*6))


def swing_command(serial, origin, direction, grip, trigger, sample_seconds=.01):
    """game_swing::Command v2 with the left hand aimed along `direction`."""
    raw = bytearray(168)
    struct.pack_into('<4IQ2I', raw, 0, 0x5357434d, 2, 168, 1, serial, 100, 0)
    dx, dy, dz = direction
    quaternion = (dy, -dx, 0, 1-dz)  # rotates -Z onto the direction
    magnitude = norm(quaternion)
    quaternion = tuple(x/magnitude for x in quaternion) if magnitude > 1e-6 else (0, 1, 0, 0)
    struct.pack_into('<10fI2f', raw, 32, *origin, *quaternion, 0, 0, 0, 1, float(trigger), float(grip))
    struct.pack_into('<10fI2f', raw, 84, *origin, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)
    struct.pack_into('<fI', raw, 152, sample_seconds, 0)
    struct.pack_into('<Q', raw, 160, time.perf_counter_ns())
    return bytes(raw)


def expected_pixel(size):
    """Where the web starts in the left eye image."""
    half = size/2
    return half+HALF_IPD/REACH*half/math.tan(FOV), half+DROP/REACH*half/math.tan(FOV)


def mark(rgb, size, x, y, colour=(0, 255, 0), arm=14, gap=5):
    """Draw a cross with an open centre, so the web's start stays visible under it."""
    image = bytearray(rgb)
    for d in list(range(-arm, -gap+1))+list(range(gap, arm+1)):
        for px, py in ((round(x)+d, round(y)), (round(x), round(y)+d)):
            if 0 <= px < size and 0 <= py < size:
                image[(py*size+px)*3:(py*size+px)*3+3] = bytes(colour)
    return bytes(image)


def summarize(samples, speed=10):
    """Mean and largest miss of the web start, in metres, over the samples faster than `speed` m/s."""
    fast = [s for s in samples if s['speed'] >= speed]
    if not fast:
        return dict(samples=0)
    return dict(samples=len(fast), mean_miss_m=sum(s['miss'] for s in fast)/len(fast),
                max_miss_m=max(s['miss'] for s in fast), mean_speed_mps=sum(s['speed'] for s in fast)/len(fast),
                mean_frame_travel_m=sum(s['speed']*s['dt'] for s in fast)/len(fast))


def assess(late, frame):
    """Late eyes miss by about one frame of travel; eyes placed for the frame do not miss."""
    measured = late.get('samples', 0) >= 10 and frame.get('samples', 0) >= 10
    late_missed = measured and late['mean_miss_m'] > .5*late['mean_frame_travel_m']
    frame_hits = measured and frame['mean_miss_m'] < .1*frame['mean_frame_travel_m'] and frame['max_miss_m'] < .05
    return dict(measured=measured, late_eyes_missed=late_missed, frame_eyes_hit=frame_hits,
                passed=bool(late_missed and frame_hits))


def monitor_view(appearance, hud, frame):
    """The game's own view (the monitor's) and the HUD panel against the eyes of the same frames: the frames
    that view was placed from another player position than the eyes and the furthest (m), the furthest the panel
    was moved from the eyes' travel (m), and the furthest the markers' head was from the frame's (m)."""
    if not appearance or not appearance['active_view_aligned']:
        return None
    result = dict(frames=appearance['active_view_aligned'], lag_frames=appearance['active_view_lag_frames'],
                  lag_max_m=round(appearance['active_view_lag_max_m'], 4),
                  frame_travel_m=round(frame.get('mean_frame_travel_m', 0), 4))
    if hud:
        result.update(panel_mismatch_m=hud['offset_mismatch_m'], marker_projections=hud['marker_projections'],
                      marker_mismatch_m=hud['marker_mismatch_m'])
    result['with_the_eyes'] = not result['lag_frames'] and not result.get('panel_mismatch_m')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--speed', type=float, default=32, help='swing speed cap in m/s')
    p.add_argument('--gravity', type=float, default=6, help='swing gravity in m/s^2')
    p.add_argument('--hold', type=float, default=6, help='seconds to hold the swing web (2..10)')
    p.add_argument('--size', type=int, default=512)
    p.add_argument('--views', type=int, default=5, choices=(5, 13),
                   help='13 also moves the game view to the eyes, as in a VR session')
    p.add_argument('--modules', type=pathlib.Path, default=ROOT/'build/windows-ninja',
                   help='folder with spidy_stereo_probe.dll')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/web-frames.json')
    a = p.parse_args()
    if not 1 <= a.speed <= 65 or not 0 <= a.gravity <= 30 or not 2 <= a.hold <= 10 or not 64 <= a.size <= 2048:
        p.error('Use 1..65 m/s, 0..30 m/s^2, a hold of 2..10 seconds, and 64..2048 pixels')
    game = Game(find_game())
    process = None
    stops = []  # (name, export) in the order to stop
    result = dict(speed=a.speed, gravity=a.gravity, hold=a.hold, size=a.size, modules=str(a.modules))
    try:
        pe = PE(game.path.read_bytes())
        entries = {rva: pe.bytes(rva, 16) for rva in (*ENTRIES, *RENDER_MEMORY_HOOKS)}
        if any(game.read(game.base+rva, 16) != entries[rva] for rva in ENTRIES):
            raise RuntimeError('A required entry is already patched. Start a fresh game process.')
        LIVE_VTABLES['hero_mover'] = 0x38b2c98
        objects = game.registered_candidates()
        heroes = [o for o in objects if o['kind'] == 'hero_local']
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required. Load a save first.')
        hero = heroes[0]
        record = int(hero['actor_record'], 16)
        managers = [o for o in objects if o['kind'] == 'hero_mover' and o['actor_record'] == hero['actor_record']]
        if len(managers) != 1:
            raise RuntimeError('Exactly one local movement manager is required')
        mover = component(game, struct.unpack('<I', game.read(int(managers[0]['object'], 16)+0xdb4, 4))[0])
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())

        def invoke(address, payload, what):
            code = call_with_payload(process, address, payload)
            if code:
                raise RuntimeError(f'{what}: {code}')
        queue = discover_queue(game, process)
        bridge, _ = prepare(game.pid, process)
        invoke(bridge['SpidyStart'], struct.pack('<4I3Q', 0x53424346, 1, 40, game.pid, game.base,
                                                 int(hero['object'], 16), record), 'Input bridge start')
        stops.append(('bridge', bridge['SpidyStop']))
        motion, _ = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
                            ROOT/'reports/motion-modules',
                            ('SpidyMotionStart', 'SpidyMotionSample', 'SpidyMotionData', 'SpidyMotionStop'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays, _ = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll', ROOT/'reports/ray-modules',
                          ('SpidyRayStart', 'SpidyRaySubmit', 'SpidyRayData', 'SpidyRayStop', 'SpidySwingStart',
                           'SpidySwingSubmit', 'SpidySwingStop', 'SpidySwingData'))
        eyes_dll, digest = prepare(game.pid, process, a.modules/'spidy_stereo_probe.dll',
                                   ROOT/'reports/stereo-modules',
                                   ('SpidyStart', 'SpidyStop', 'SpidyStereoData', 'SpidySetEyes', 'SpidyStereoFrames',
                                    'SpidyGpuStart', 'SpidyGpuStop', 'SpidyGpuData', 'SpidyGpuFreeze',
                                    'SpidyEyePlacement', 'SpidyWebsStart', 'SpidyWebsSubmit', 'SpidyWebsStop',
                                    'SpidyAppearanceData', 'SpidyHudData'))
        # Already loaded if the game was started by tools/vr_launcher.py; otherwise it only reports.
        render_module = enlarge_render_memory(game.pid)
        stops.append(('render_memory', render_module['SpidyRenderMemoryStop']))
        invoke(rays['SpidyRayStart'], struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 28000, 0x410),
               'World rays start')
        stops.insert(0, ('rays', rays['SpidyRayStop']))
        invoke(rays['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record, mover,
                                                    motion_module, 24000, a.speed, a.gravity, 0), 'Native swing start')
        stops.insert(0, ('swing', rays['SpidySwingStop']))
        invoke(eyes_dll['SpidyGpuStart'], struct.pack('<4I2Q4I', 0x53475043, 3, 48, game.pid, game.base, queue,
                                                      30000, 0, 1, 0), 'Eye capture start')
        stops.insert(0, ('gpu', eyes_dll['SpidyGpuStop']))
        invoke(eyes_dll['SpidyStart'], struct.pack('<4IQ4I', 0x53534346, 1, 40, game.pid, game.base, 26000, a.views,
                                                   a.size, a.size), 'Native eye views start')
        stops.insert(0, ('eyes', eyes_dll['SpidyStop']))
        invoke(eyes_dll['SpidyWebsStart'], struct.pack('<4I2Q', 0x53574243, 1, 32, game.pid, game.base, record),
               'Game webs start')
        stops.insert(0, ('webs', eyes_dll['SpidyWebsStop']))

        origin = game.transform(int(hero['actor_transform'], 16))['position']
        folder = a.output.with_name(a.output.stem+'-eyes')
        started = time.monotonic()
        jumped = queried = late = False
        heading = aim = attach_at = None
        swing_serial = eye_serial = bridge_serial = 0
        samples, motion_samples, swing_samples, captures = [], [], [], []
        rope_frames = 0
        release_at = None
        shots = []  # (time, label) still to capture
        print('Swinging with a game web held in front of a sideways eye camera.', flush=True)
        while True:
            elapsed = time.monotonic()-started
            if elapsed > 22 or (release_at is not None and elapsed > release_at+1.5):
                break
            m = motion_snapshot(game, motion['SpidyMotionData'])
            s = swing_snapshot(game, rays['SpidySwingData'])
            if s:
                swing_samples.append(dict(seconds=elapsed, **s))
                if s['error']:
                    raise RuntimeError(f"Native swing error {s['error']}")
            if not m or not m['steps']:
                time.sleep(.01)
                continue
            if not motion_samples or m['steps'] != motion_samples[-1]['steps']:
                motion_samples.append(dict(seconds=elapsed, **m))
            feet = tuple(m['position'])
            speed = norm(m['velocity'])
            hand = add(feet, (0, 1.2, 0))
            if not jumped and elapsed >= .25:
                state = bridge_snapshot(game, bridge['SpidyBridgeData'])
                if not state or state['state'] != 1 or not state['matched']:
                    raise RuntimeError('The game camera is not following the hero. Bring the game to the foreground.')
                bridge_serial += 1
                invoke(bridge['SpidySubmit'], struct.pack('<4IQ2I8f', 0x53424354, 1, 64, 1, bridge_serial, 350, 16,
                                                          *([0.]*7), 1.), 'Jump')
                jumped = True
            if jumped and not queried and elapsed >= .8 and feet[1] > origin[1]+1:
                # Eight directions, slightly upward: travel toward the most open one.
                candidates = [(hand, (math.cos(.35)*math.cos(i*math.pi/4), math.sin(.35),
                                      math.cos(.35)*math.sin(i*math.pi/4)), 100, i) for i in range(8)]
                invoke(rays['SpidyRaySubmit'], ray_command(1, candidates, 250), 'Open-space rays')
                queried = True
            if queried and heading is None:
                targets = ray_snapshot(game, rays['SpidyRayData'])
                if targets and targets['serial'] == 1 and targets['status'] == 2 and not targets['error']:
                    clear = max(targets['hits'][:8], key=lambda h: h['fraction'] if h['count'] else 2)
                    direction = clear['direction']
                    flat = norm((direction[0], 0, direction[2]))
                    heading = (direction[0]/flat, 0., direction[2]/flat)
                    aim = (heading[0]*math.cos(.7), math.sin(.7), heading[2]*math.cos(.7))
                    attach_at = elapsed
                    release_at = attach_at+a.hold
                    half = a.hold/2
                    shots = [(attach_at+half-.9, 'late-1'), (attach_at+half-.3, 'late-2'),
                             (attach_at+a.hold-.9, 'frame-1'), (attach_at+a.hold-.3, 'frame-2')]
                    late = True
                    call_remote(process, eyes_dll['SpidyEyePlacement'], 1)
                    result.update(heading=heading, clear_distance=clear['fraction']*100 if clear['count'] else None)
            holding = attach_at is not None and elapsed < release_at
            if late and elapsed >= attach_at+a.hold/2:
                late = False
                call_remote(process, eyes_dll['SpidyEyePlacement'], 0)
            # Grip shoots and holds; the trigger reels once it has been released after the attachment.
            reel = holding and elapsed >= attach_at+.4
            swing_serial += 1
            invoke(rays['SpidySwingSubmit'], swing_command(swing_serial, hand, aim or (0, 0, -1), holding, reel),
                   'Swing input')
            if heading:
                eyes, wrist, target, expected = rig(feet, heading)
                eye_serial += 1
                code = call_with_payload(process, eyes_dll['SpidySetEyes'], eye_command(eye_serial, eyes, feet))
                if code and code != 4003:
                    raise RuntimeError(f'Eye command: {code}')
                invoke(eyes_dll['SpidyWebsSubmit'], web_command(feet, wrist, target), 'Game web')
                frames = frame_snapshot(game, eyes_dll['SpidyStereoFrames'])
                if frames and frames['rope_frames'] != rope_frames:
                    rope_frames = frames['rope_frames']
                    miss = norm(tuple(x-y for x, y in zip(frames['rope_from_eye'], expected)))
                    samples.append(dict(seconds=elapsed, late=late, speed=speed, dt=m['dt'], miss=miss,
                                        settled=abs(elapsed-(attach_at+a.hold/2)) > .3 and elapsed > attach_at+.6))
                if shots and elapsed >= shots[0][0]:
                    _, label = shots.pop(0)
                    code = call_remote(process, eyes_dll['SpidyGpuFreeze'])
                    gpu = gpu_snapshot(game, eyes_dll['SpidyGpuData'])
                    if not code and gpu and gpu['width'] == a.size and gpu['left_pixels'] >= 0x10000:
                        pixels = game.read(gpu['left_pixels'], a.size*a.size*4)
                        if len(pixels) == a.size*a.size*4:
                            folder.mkdir(parents=True, exist_ok=True)
                            _, _, rgb = rgb_rows(pixels, a.size*4, 0, 0, a.size, a.size)
                            write_rgb_png(folder/f'{label}.png', a.size, a.size,
                                          mark(rgb, a.size, *expected_pixel(a.size)))
                            captures.append(dict(label=label, seconds=elapsed, speed=speed, dt=m['dt'],
                                                 late=late, file=f'{label}.png', generation=gpu['generation']))
                    else:
                        captures.append(dict(label=label, seconds=elapsed, error=code))
            time.sleep(.004)
        appearance = appearance_snapshot(game, eyes_dll['SpidyAppearanceData'])
        hud = hud_snapshot(game, eyes_dll['SpidyHudData'])
        frames = frame_snapshot(game, eyes_dll['SpidyStereoFrames'])
        render_memory = render_memory_snapshot(game, render_module['SpidyRenderMemoryData'])
        codes = {}
        while stops:
            name, export = stops.pop(0)
            codes[name] = call_remote(process, export)
        settled = [s for s in samples if s['settled']]
        late_summary = summarize([s for s in settled if s['late']])
        frame_summary = summarize([s for s in settled if not s['late']])
        result.update(pid=game.pid, dll_sha256=digest, origin=origin, attach_at=attach_at, stops=codes,
                      placed_in_maintenance=late_summary, placed_before_render=frame_summary,
                      assessment=assess(late_summary, frame_summary), captures=captures,
                      image_folder=str(folder), expected_pixel=expected_pixel(a.size),
                      flight=flight_summary(motion_samples), appearance=appearance, frames=frames,
                      monitor_view=monitor_view(appearance, hud, frame_summary), hud=hud,
                      render_memory=render_memory,
                      entries_restored=all(game.read(game.base+rva, 16) == code for rva, code in entries.items()),
                      samples=samples, swing_samples=swing_samples, motion_samples=motion_samples)
        # Every frame's render memory must fit, or the game drops eye jobs for lack of it.
        memory_fits = bool(render_memory and not render_memory['overflow_frames'])
        result['passed'] = bool(result['assessment']['passed'] and result['entries_restored'] and
                                not any(codes.values()) and memory_fits)
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps({k: result[k] for k in ('placed_in_maintenance', 'placed_before_render', 'assessment',
                                                 'captures', 'flight', 'monitor_view', 'render_memory', 'stops',
                                                 'entries_restored')}, indent=2))
        print('eye jobs', {k: frames[k] for k in ('left_copies', 'left_begins', 'reclaimed')} if frames else None)
        print(f"Web frame check {'passed' if result['passed'] else 'failed'}; report: {a.output.resolve()}")
        return 0 if result['passed'] else 1
    finally:
        for _, export in stops:
            try:
                call_remote(process, export)
            except OSError:
                pass
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Web frame check failed: {error}', file=sys.stderr)
        sys.exit(2)
