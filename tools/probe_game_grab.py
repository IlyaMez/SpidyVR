"""Web a prop or a bot from where the player stands, reel it in, carry it, throw it: the web grab in the
running game, without a headset.

    python tools/probe_game_grab.py              the nearest throwable prop in sight
    python tools/probe_game_grab.py --bots       the nearest bot in sight (a thug, or a scripted civilian)
    python tools/probe_game_grab.py --yank       pull it over with the zip gesture instead of reeling

It needs a freshly started game in free roam (Spidy's ray and movement modules start once per process) and
the game window in front, which it brings there. It plays one grab with a scripted hand next to the player:
grip on the target, reel (or yank) until it is caught, carry it around for 2.5 s, flick and let go, then
watch it for 3 s (--watch). It writes reports/grab-probe.json and screenshots in reports/grab-probe/.
"""
import argparse
import ctypes as c
import json
import math
import pathlib
import struct
import sys
import time
from bridge_game import ROOT, prepare
from capture_game_state import Game, find_game, open_process, close
from inspect_game import PE
from observe_game import call_remote, call_with_payload, modules
from probe_game_screen import game_window, client_size, window_rgb, user32
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from run_game_vr import write_rgb_png
from vr_launcher import bring_to_front

OUTPUT = ROOT/'reports/grab-probe'
REGISTRY, REGISTRY_COUNT = 0x7a44320, 0x7a44340
HERO_LOCAL, HERO_MOVERS, BOT_MOVERS, THROWABLE = 0x38a93c8, 0x38b2c98, 0x38533e0, 0x38489c0
# Hooks the probe's modules install: castRay, mover prequery and gravity, hknpWorld::preCollide.
ENTRIES = (0x2e67010, 0x1fbe360, 0x1fbda50, 0x2e54300)
LIFT = {'throwable': .45, 'bot': .95}
PHASES = {0: 'none', 1: 'tethered', 2: 'yanked', 3: 'held'}
GRAB_SIZE = 320


def grab_snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, GRAB_SIZE)
        if len(raw) != GRAB_SIZE:
            return None
        if struct.unpack_from('<3I', raw) != (0x53475244, 1, GRAB_SIZE):
            raise RuntimeError('Grab protocol mismatch')
        if struct.unpack_from('<q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        names = ('steps', 'commands', 'grabs', 'yanks', 'catches', 'throws', 'releases', 'lost', 'flights', 'landed')
        d = dict(zip(names, struct.unpack_from('<10Q', raw, 24)))
        d['status'] = struct.unpack_from('<I', raw, 12)[0]
        d.update(zip(('candidates', 'error', 'kinds', 'drive_failures'), struct.unpack_from('<4I', raw, 104)))
        d['hands'] = []
        for i in range(2):
            o = 120+i*40
            phase, kind, target = struct.unpack_from('<2IQ', raw, o)
            d['hands'].append(dict(phase=PHASES.get(phase, phase), kind=kind, target=hex(target),
                                   end=struct.unpack_from('<3f', raw, o+16), length=struct.unpack_from('<f', raw, o+28)[0],
                                   taut=struct.unpack_from('<I', raw, o+32)[0]))
        d['last_throw'] = struct.unpack_from('<3f', raw, 200)
        d['time_scale'] = struct.unpack_from('<f', raw, 212)[0]
        d.update(zip(('body_steps', 'frees', 'rebuilds', 'writes', 'follows', 'flings', 'steers', 'refused', 'expired'),
                     struct.unpack_from('<9Q', raw, 216)))
        d['commanded'], d['observed'] = struct.unpack_from('<3f', raw, 288), struct.unpack_from('<3f', raw, 300)
        d['tick_dt'], d['step_dt'] = struct.unpack_from('<2f', raw, 312)
        return d
    return None


def aim(direction):
    """The quaternion turning -Z to `direction` (OpenXR aim convention)."""
    dx, dy, dz = direction
    q = (dy, -dx, 0, 1-dz)
    n = math.sqrt(sum(x*x for x in q))
    return tuple(x/n for x in q) if n > 1e-6 else (0, 1, 0, 0)


def hand_command(serial, position, direction, relative=(0, 0, 0), trigger=0., grip=0., seconds=1/90):
    raw = bytearray(168)
    struct.pack_into('<4IQ2I', raw, 0, 0x5357434d, 2, 168, 1, serial, 100, 0)
    struct.pack_into('<10fI2f', raw, 32, *position, *aim(direction), *relative, 1, trigger, grip)
    struct.pack_into('<10fI2f', raw, 84, *position, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)
    struct.pack_into('<fI', raw, 152, seconds, 0)
    struct.pack_into('<Q', raw, 160, time.perf_counter_ns())
    return bytes(raw)


def registry(game):
    table = game.pointer(game.base+REGISTRY)
    count = struct.unpack('<i', game.read(game.base+REGISTRY_COUNT, 4))[0]
    raw = game.read(table, count*16)
    for i in range(count):
        address, generation = struct.unpack_from('<QI', raw, i*16)
        if not address or not generation:
            continue
        head = game.read(address, 0x18)
        if len(head) == 0x18 and struct.unpack_from('<I', head, 0x14)[0] == (generation << 20 | i):
            yield address, struct.unpack_from('<Q', head)[0]-game.base, struct.unpack_from('<Q', head, 8)[0], i


def resolve(game, handle):
    table = game.pointer(game.base+REGISTRY)
    address, generation = struct.unpack('<QI', game.read(table+(handle & 0xfffff)*16, 12))
    return address if generation == handle >> 20 else 0


def camera(game):
    """The game camera's forward vector and position (HeroCameraManager +0x744: side, up, forward rows)."""
    for address, vtable, _, _ in registry(game):
        if vtable == 0x38b1dd0:
            m = struct.unpack('<16f', game.read(address+0x744, 64))
            return m[8:11], m[12:15]
    return None, None


def aim_camera(game, target, attempts=20):
    """Turn the game camera toward `target` with the virtual Xbox controller's right stick.

    Measured on October 5: right stick +X turns the view's yaw up, +Y its pitch up. The camera speeds up while
    a stick stays pushed (1.3 degrees in a 0.15 s tap), so holds grow with the error and shrink near it.
    """
    import probe_menu_pad  # here: it imports run_game_vr, which imports this module
    process, xr = None, None
    try:
        _, process, xr, _ = probe_menu_pad.attach()
        for _ in range(attempts):
            forward, position = camera(game)
            if not forward:
                return False
            want = [b-a for a, b in zip(position, target)]
            yaw = math.atan2(want[0], -want[2])-math.atan2(forward[0], -forward[2])
            yaw = (yaw+math.pi) % (2*math.pi)-math.pi
            pitch = math.atan2(want[1], math.hypot(want[0], want[2]))-math.asin(max(-1, min(1, forward[1])))
            # The camera's own pitch stops short of straight down: close enough is close enough.
            if abs(yaw) < .1 and (abs(pitch) < .1 or forward[1] < -.8):
                return True
            # One axis at a time: yaw first, then pitch.
            error = yaw if abs(yaw) >= .1 else pitch
            deflection = int(math.copysign(min(32000, 9000+abs(error)*20000), error))
            sticks = (0, 0, deflection, 0) if abs(yaw) >= .1 else (0, 0, 0, deflection)
            probe_menu_pad.pad(process, xr, hold_ms=int(min(400, 80+abs(math.degrees(error))*6)), sticks=sticks)
        return False
    finally:
        if process:
            close(process)


def shot(game, name):
    hwnd = game_window(game.pid)
    width, height = client_size(hwnd)
    rgb = window_rgb(hwnd, width, height)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = OUTPUT/f'{name}.png'
    write_rgb_png(path, width, height, rgb)
    return dict(path=str(path), front=user32.GetForegroundWindow() == hwnd)


def fling_test(game, process, rays, motion, output):
    """What the web does to a bot, on the nearest one at any distance: flung, steered, handed back."""
    pe = PE(game.path.read_bytes())
    components = list(registry(game))
    machines = {r: a for a, v, r, _ in components if v == 0x4f843d0}
    bots = []
    hero = next(r for a, v, r, _ in components if v == HERO_LOCAL)
    feet = game.transform(game.pointer(hero))['position']
    for a, v, r, _ in components:
        if v == BOT_MOVERS and r in machines:
            t = game.transform(game.pointer(r))
            mover = resolve(game, struct.unpack('<I', game.read(a+0xdb4, 4))[0])
            if t and mover:
                bots.append((math.dist(t['position'], feet), r, machines[r], mover))
    if not bots:
        raise RuntimeError('No bot in the game right now')
    distance, record, machine, mover = min(bots)
    print(f'Bot {record:#x}, {distance:.0f} m away', flush=True)
    names = {}

    def state():
        out = []
        for offset, valid in ((0x70, 0x80), (0x98, 0xa8)):
            obj = game.pointer(machine+offset)
            vt = game.pointer(obj)-game.base if obj and game.read(machine+valid, 1) != bytes(1) else 0
            if vt and vt not in names:
                try:
                    names[vt] = pe.vtable(vt)['type'].replace('.?AV', '').rstrip('@')
                except Exception:
                    names[vt] = hex(vt)
            out.append(names.get(vt))
        return out

    def fling(velocity):
        payload = struct.pack('<4I2Q3fI', 0x53475454, 1, 48, 0, machine, record, *velocity, 0)
        return call_with_payload(process, rays['SpidyGrabTest'], payload)

    serial = [0]

    def steer(velocity, enabled=True):
        serial[0] += 1
        payload = struct.pack('<4I3Q2I3fI', 0x534d5652, 1, 64, int(enabled), mover, record, serial[0],
                              100 if enabled else 0, 1, *velocity, 0)
        return call_with_payload(process, motion['SpidyMotionDrive'], payload)

    samples = []
    started = time.monotonic()

    def watch(seconds, label, each=None):
        until = time.monotonic()+seconds
        while time.monotonic() < until:
            if each:
                each()
            t = game.transform(game.pointer(record))
            samples.append(dict(t=round(time.monotonic()-started, 3), phase=label, state=state(),
                                position=t['position'] if t else None))
            time.sleep(.02)
    watch(.3, 'before')
    codes = dict(fling=fling((0, 9, 3)))
    watch(1.0, 'flung')
    watch(.6, 'steered', lambda: steer((4, 3, 0)))
    codes['release'] = steer((0, 0, 0), False)
    codes['hand_back'] = fling((2, 1, 0))
    watch(3.0, 'handed back')
    states = sorted({s for x in samples for s in x['state'] if s})
    path = [x['position'] for x in samples if x['position']]
    phases = {}
    for x in samples:
        if x['position']:
            phases.setdefault(x['phase'], []).append(x['position'])
    moved = {k: round(math.dist(v[0], v[-1]), 2) for k, v in phases.items()}
    highest = max(p[1] for p in path)-path[0][1] if path else None
    report = dict(bot=hex(record), distance=distance, codes=codes, states=states, moved=moved,
                  rose=highest, samples=samples)
    output.write_text(json.dumps(report, indent=1)+'\n')
    print(json.dumps({k: report[k] for k in ('codes', 'states', 'moved', 'rose')}, indent=1))
    return 0 if 'BotStateFlung' in states and moved.get('steered', 0) > .5 else 1


def main():
    try:
        user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    except AttributeError:
        user32.SetProcessDPIAware()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--bots', action='store_true', help='web a bot instead of a throwable prop')
    parser.add_argument('--yank', action='store_true', help='yank the target over instead of reeling it')
    parser.add_argument('--range', type=float, default=55, help='farthest target, metres')
    parser.add_argument('--fling-test', action='store_true',
                        help='on the nearest bot at any distance: fling it, steer it, hand it back (no web)')
    parser.add_argument('--watch', type=float, default=3,
                        help='seconds to watch the target after the throw (its landing, slide and rest)')
    parser.add_argument('--aim-camera', action='store_true',
                        help='first turn the game camera toward the target (best effort, through the virtual pad)')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/grab-probe.json')
    args = parser.parse_args()
    kind = 'bot' if args.bots else 'throwable'
    game = Game(find_game())
    process = None
    rays = swing = None
    rays_active = swing_active = False
    try:
        pe = PE(game.path.read_bytes())
        if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in ENTRIES):
            raise RuntimeError('A hook entry is already patched: start the game afresh')
        front = bring_to_front(game.pid)
        components = list(registry(game))
        heroes = [(a, r) for a, v, r, _ in components if v == HERO_LOCAL]
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required: load free roam first')
        record = heroes[0][1]
        managers = [a for a, v, r, _ in components if v == HERO_MOVERS and r == record]
        mover = resolve(game, struct.unpack('<I', game.read(managers[0]+0xdb4, 4))[0])
        feet = game.transform(game.pointer(record))['position']
        hand = (feet[0], feet[1]+1.2, feet[2])
        marker = BOT_MOVERS if args.bots else THROWABLE
        targets = []
        for a, v, r, _ in components:
            if v == marker:
                t = game.transform(game.pointer(r))
                if t:
                    centre = (t['position'][0], t['position'][1]+LIFT[kind], t['position'][2])
                    if 2 < math.dist(centre, hand) < args.range:
                        targets.append((math.dist(centre, hand), r, centre))
        targets.sort()
        if not targets:
            raise RuntimeError(f'No {kind} within {args.range:.0f} m of the player')
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        motion, motion_hash = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
                                      ROOT/'reports/motion-modules', ('SpidyMotionStart', 'SpidyMotionStop',
                                                                      'SpidyMotionDrive'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays, ray_hash = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll',
                                 ROOT/'reports/ray-modules',
                                 ('SpidyRayStart', 'SpidyRaySubmit', 'SpidyRayData', 'SpidyRayStop', 'SpidySwingStart',
                                  'SpidySwingSubmit', 'SpidySwingStop', 'SpidySwingData', 'SpidyGrabData',
                                  'SpidyGrabTest'))
        swing = rays

        def invoke(address, payload):
            code = call_with_payload(process, address, payload)
            if code:
                raise RuntimeError(f'Native call rejected: {code}')
        invoke(rays['SpidyRayStart'], struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 30000, 0x410))
        rays_active = True
        if args.fling_test:
            invoke(swing['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record,
                                                          mover, motion_module, 30000, 32., 6., 1 << 2))
            swing_active = True
            return fling_test(game, process, rays, motion, args.output.with_name('grab-fling-test.json'))
        # The nearest target with a clear line from the hand: the prop's own body is the first thing a ray
        # meets, so it stops short of the target's sphere.
        candidates = targets[:8]
        invoke(rays['SpidyRaySubmit'], ray_command(1, [
            (hand, tuple((b-a)/d for a, b in zip(hand, centre)), max(.5, d-1.2), i)
            for i, (d, _, centre) in enumerate(candidates)], 250))
        target = None
        for _ in range(100):
            hits = ray_snapshot(game, rays['SpidyRayData'])
            if hits and hits['serial'] == 1 and hits['status'] == 2:
                for (d, r, centre), hit in zip(candidates, hits['hits']):
                    if not hit['count'] or not hit['fixed']:
                        target = (d, r, centre)
                        break
                break
            time.sleep(.02)
        if not target:
            raise RuntimeError(f'No {kind} in sight from the player: every line is blocked or the rays did not run')
        distance, target_record, centre = target
        print(f'Target: {kind} {target_record:#x}, {distance:.1f} m away at {[round(x, 1) for x in centre]}', flush=True)
        if args.aim_camera:
            # Halfway between the player and the target: the reel, the catch and the throw all in view.
            print('Camera aimed:', aim_camera(game, tuple((a+b)/2 for a, b in zip(hand, centre))), flush=True)
            bring_to_front(game.pid)
        kinds = 1 << (2 if args.bots else 1)
        invoke(swing['SpidySwingStart'], struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, record,
                                                      mover, motion_module, 30000, 32., 6., kinds))
        swing_active = True
        serial = 0
        samples, shots = [], {}
        started = time.monotonic()
        direction = tuple((b-a)/distance for a, b in zip(hand, centre))
        flat = math.hypot(direction[0], direction[2]) or 1
        forward = (direction[0]/flat, .35, direction[2]/flat)
        state = dict(phase='arm', until=started+.4)

        def sample(label):
            g = grab_snapshot(game, rays['SpidyGrabData'])
            t = game.transform(game.pointer(target_record))
            samples.append(dict(t=round(time.monotonic()-started, 3), phase=label, grab=g,
                                target=t['position'] if t else None))
            return g

        relative = [0, 0, 0]
        position = list(hand)
        grip = trigger = 0.
        caught_at = throw_at = None
        print('Arm, grab, ' + ('yank' if args.yank else 'reel') + ', carry, throw, watch.', flush=True)
        while time.monotonic()-started < 20+args.watch:
            now = time.monotonic()
            phase = state['phase']
            g = sample(phase)
            hand_phase = g['hands'][0]['phase'] if g else 'none'
            if phase == 'arm' and now >= state['until']:
                shots['aim'] = shot(game, 'aim')
                state = dict(phase='grab', until=now+1)
                grip = 1.
            elif phase == 'grab' and (hand_phase != 'none' or now >= state['until']):
                if hand_phase == 'none':
                    break  # nothing caught
                shots['caught'] = shot(game, 'caught')
                state = dict(phase='pull', until=now+8, start=now)
            elif phase == 'pull':
                if args.yank:
                    # The zip gesture: the hand comes back toward the body 0.3 m in 0.1 s.
                    k = min(1, (now-state['start'])/.1)
                    relative = [-direction[i]*.3*k for i in range(3)]
                else:
                    trigger = 1. if now-state['start'] > .3 else 0.
                if hand_phase == 'held' or now >= state['until']:
                    caught_at = now if hand_phase == 'held' else None
                    shots['held'] = shot(game, 'held')
                    state = dict(phase='carry', until=now+2.5, start=now)
            elif phase == 'carry':
                angle = (now-state['start'])/1.25*2*math.pi
                position = [hand[0]+.3*math.cos(angle), hand[1]+.3*math.sin(angle), hand[2]]
                if now >= state['until']:
                    shots['carried'] = shot(game, 'carried')
                    state = dict(phase='flick', until=now+.15, start=now, from_=list(position))
            elif phase == 'flick':
                k = min(1, (now-state['start'])/.15)
                position = [state['from_'][i]+forward[i]*.7*k for i in range(3)]
                if now >= state['until']:
                    grip = trigger = 0.
                    throw_at = now
                    state = dict(phase='watch', until=now+args.watch)
            elif phase == 'watch':
                if 'flight' not in shots and now-throw_at > .5:
                    shots['flight'] = shot(game, 'flight')
                if now >= state['until']:
                    shots['after'] = shot(game, 'after')
                    break
            serial += 1
            invoke(swing['SpidySwingSubmit'], hand_command(serial, position, direction, tuple(relative), trigger,
                                                           grip))
            time.sleep(1/90)
        stop_swing = call_remote(process, swing['SpidySwingStop'])
        swing_active = stop_swing != 0
        stop_rays = call_remote(process, rays['SpidyRayStop'])
        rays_active = stop_rays != 0
        final = grab_snapshot(game, rays['SpidyGrabData'])
        restored = all(game.read(game.base+rva, 16) == pe.bytes(rva, 16) for rva in ENTRIES[:2]+(0x2e54300,))
        path = [s['target'] for s in samples if s['target']]
        start_pos = path[0] if path else None
        report = dict(pid=game.pid, ray_hash=ray_hash, motion_hash=motion_hash, kind=kind, yank=args.yank,
                      front_at_start=front, hand=hand, target=hex(target_record), distance=distance,
                      caught=caught_at is not None, final=final, restored=restored, stop_swing=stop_swing,
                      stop_rays=stop_rays, shots=shots,
                      moved=math.dist(start_pos, path[-1]) if path else None,
                      highest=max(p[1] for p in path) if path else None, samples=samples)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=1)+'\n')
        summary = {k: report[k] for k in ('kind', 'distance', 'caught', 'moved', 'highest', 'restored')}
        summary['counters'] = {k: final[k] for k in ('grabs', 'yanks', 'catches', 'throws', 'lost', 'frees', 'writes',
                                                     'follows', 'flings', 'steers', 'refused', 'drive_failures', 'flights',
                                                     'landed')} if final else None
        summary['last_throw'] = final['last_throw'] if final else None
        print(json.dumps(summary, indent=1))
        return 0 if report['caught'] and final and final['throws'] and restored else 1
    finally:
        if swing_active:
            call_remote(process, swing['SpidySwingStop'])
        if rays_active:
            call_remote(process, rays['SpidyRayStop'])
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    sys.exit(main())
