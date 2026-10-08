"""Spider-Man's own body and fists in the running game, without a headset.

    python tools/probe_game_body.py                       every phase
    python tools/probe_game_body.py --phases rig          list the hero's rig only

Phases:
  rig    the hero's rig as its pose job names it: every joint's name, parent and rest position, the joints the
         game starts its web lines from, and the joints the body takes for hips, spine, head, arms and legs
  ik     the body turns the hero's joints toward a scripted headset and controllers (the left hand raised, the
         right one punching ahead): how far each wrist and the head land from their targets, and a screenshot
  eyes   two eye views at the hero's head, as in VR, looking down at the body: the left eye's image
  walk   the hero walks about under the body (the virtual pad's stick): how far he turns between his pose job
         and the render, and the eyes' image meanwhile
  fist   a scripted fist driven through the nearest bot's chest at 6 m/s, through the swing module's input as a
         controller's: the punch the game adapter lands, the damage request it issues, the bot's health
  punch  one blow straight to the game's own DamageSystem on the nearest bot (and, with --punch-hero, the hero):
         its state machine and health before and after

It needs a freshly started game in free roam (Spidy's modules start once per process) with the window in front,
which it brings there. Reports go to reports/body-probe.json and reports/body-probe/.
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
from probe_game_grab import registry, HERO_LOCAL, BOT_MOVERS, THROWABLE, ROPE_MANAGER
from probe_game_screen import game_window, client_size, window_rgb
from run_game_vr import write_rgb_png
from vr_launcher import bring_to_front

OUTPUT = ROOT/'reports/body-probe'
STEREO_DLL = ROOT/'build/windows-ninja/spidy_stereo_probe.dll'
RAY_DLL = ROOT/'build/windows-ninja/spidy_ray_bridge.dll'
MAX_JOINTS = 256
STATUS_SIZE, POSES_SIZE = 136, 56 + 64 + 3*MAX_JOINTS*64
PROBLEMS = {0: 'none', 1: 'no hero', 2: 'no rig yet', 3: 'unknown rig', 4: 'bad rest pose', 5: 'bad instance'}
STATES = {0: 'off', 1: 'waiting', 2: 'active', 3: 'failed'}
SYNC_MACHINE, HEALTH, BOT_HEALTH = 0x4f843d0, 0x3839e08, 0x3839e98
HERO_HEALTH = 0x38a6df0
# Bits of native_body::Flags.
BODY_ON, HIDE_HEAD, HAND_TURN, AIRBORNE = 1, 2, 4, 8


def crc(name, table=[]):
    """The game's name hash (1bb88d0): CRC-32 steps seeded with 0xedb88320, no final inversion."""
    if not table:
        for i in range(256):
            v = i
            for _ in range(8):
                v = (v >> 1) ^ 0xedb88320 if v & 1 else v >> 1
            table.append(v)
    value = 0xedb88320
    for byte in name.encode():
        value = table[(value ^ byte) & 0xff] ^ (value >> 8)
    return value


def seqlock(game, address, size):
    for _ in range(16):
        raw = game.read(address, size)
        again = game.read(address+16, 8)
        if len(raw) == size and len(again) == 8 and not raw[16] & 1 and raw[16:24] == again:
            return raw
        time.sleep(.002)
    return None


def body_status(game, address):
    raw = seqlock(game, address, STATUS_SIZE)
    if not raw:
        return None
    if struct.unpack_from('<3I', raw) != (0x53424453, 1, STATUS_SIZE):
        raise RuntimeError('Body status protocol mismatch')
    v = struct.unpack('<4Iq3Q2I2Q3fI2ff2fIQ2Id', raw)
    return dict(state=STATES.get(v[3], v[3]), jobs=v[5], hero_jobs=v[6], solved=v[7],
                problem=PROBLEMS.get(v[8], v[8]), joints=v[9], rig=v[10], instance=v[11], weight=round(v[12], 3),
                scale=round(v[13], 4), yaw=round(v[14], 4), grounded=v[15],
                hand_error=[round(v[16], 4), round(v[17], 4)], head_error=round(v[18], 4),
                turn_last=round(v[19], 5), turn_max=round(v[20], 5), rig_switches=v[21], renders=v[22],
                hero_jobs_last_frame=v[23], hero_jobs_max=v[24], solve_ms=round(v[25], 3))


def body_poses(game, address):
    raw = seqlock(game, address, POSES_SIZE)
    if not raw:
        return None
    if struct.unpack_from('<3I', raw) != (0x53424450, 1, POSES_SIZE):
        raise RuntimeError('Body poses protocol mismatch')
    joints = struct.unpack_from('<I', raw, 12)[0]
    rig, instance, out, captured = struct.unpack_from('<4Q', raw, 24)

    def matrices(offset):
        values = struct.unpack_from(f'<{joints*16}f', raw, offset)
        return [values[j*16:j*16+16] for j in range(joints)]
    return dict(joints=joints, rig=rig, instance=instance, out=out, captured=captured,
                transform=struct.unpack_from('<16f', raw, 56), game=matrices(120),
                body=matrices(120+MAX_JOINTS*64), rest=matrices(120+2*MAX_JOINTS*64))


def capture(game, process, exports, last=0):
    """The next hero pose job's joints, as the game wrote them and as the body left them."""
    call_remote(process, exports['SpidyBodyCapture'])
    for _ in range(100):
        poses = body_poses(game, exports['SpidyBodyPoses'])
        if poses and poses['captured'] > last:
            return poses
        time.sleep(.02)
    return None


def rig_table(game, rig):
    count = struct.unpack('<H', game.read(rig+2, 2))[0]
    joints = game.pointer(rig+8)
    raw = game.read(joints, count*16)
    if len(raw) != count*16:
        raise RuntimeError('The rig\'s joints could not be read')
    out = []
    for j in range(count):
        parent, = struct.unpack_from('<h', raw, j*16)
        flags, = struct.unpack_from('<H', raw, j*16+6)
        name_hash, name_offset = struct.unpack_from('<2I', raw, j*16+8)
        out.append(dict(index=j, parent=parent, flags=flags, hash=name_hash, offset=name_offset))
    return joints, out


def c_string(game, address, limit=64):
    raw = game.read(address, limit)
    end = raw.find(b'\0')
    if end <= 0:
        return None
    try:
        text = raw[:end].decode('ascii')
    except UnicodeDecodeError:
        return None
    return text if text.isprintable() else None


def find_names(game, rig, joints, table):
    """Joint names: the offsets (+0xc) from a string block, found where the hashes come true."""
    def names_from(base):
        names = []
        for j in table:
            text = c_string(game, base+j['offset'])
            if text is None or crc(text) != j['hash']:
                return None
            names.append(text)
        return names
    candidates = [rig, joints, game.pointer(rig+0x10), game.pointer(rig+0x18), game.pointer(rig+0x20),
                  game.pointer(rig+0x28), game.pointer(rig+0x30)]
    for base in candidates:
        if base and (names := names_from(base)):
            return names, base
    # A string block near the rig: anchor on a known name.
    anchors = [(crc(n), n.encode()) for n in ('pelvis', 'spine_B', 'LF_clavicle', 'RT_clavicle', 'LF_upleg')]
    by_hash = {j['hash']: j for j in table}
    for centre in (joints, rig):
        for chunk in range(-64, 64):
            start = centre + chunk*0x10000
            raw = game.read(start, 0x10000+64)
            if not raw:
                continue
            for h, text in anchors:
                if h not in by_hash:
                    continue
                at = raw.find(b'\0'+text+b'\0')
                while at >= 0:
                    base = start+at+1-by_hash[h]['offset']
                    if names := names_from(base):
                        return names, base
                    at = raw.find(b'\0'+text+b'\0', at+1)
    return None, None


def column(m, row):
    return m[row*4:row*4+3]


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def add(a, b):
    return [x+y for x, y in zip(a, b)]


def scale(a, s):
    return [x*s for x in a]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def norm(a):
    n = math.sqrt(dot(a, a))
    return [x/n for x in a] if n > 1e-9 else [0, 0, 0]


def quat_from_axes(x, y, z):
    """The rotation whose columns are the orthonormal axes x, y, z: (x, y, z, w)."""
    m00, m01, m02, m10, m11, m12, m20, m21, m22 = x[0], y[0], z[0], x[1], y[1], z[1], x[2], y[2], z[2]
    trace = m00+m11+m22
    if trace > 0:
        s = math.sqrt(trace+1)*2
        q = ((m21-m12)/s, (m02-m20)/s, (m10-m01)/s, s/4)
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1+m00-m11-m22)*2
        q = (s/4, (m01+m10)/s, (m02+m20)/s, (m21-m12)/s)
    elif m11 > m22:
        s = math.sqrt(1+m11-m00-m22)*2
        q = ((m01+m10)/s, s/4, (m12+m21)/s, (m02-m20)/s)
    else:
        s = math.sqrt(1+m22-m00-m11)*2
        q = ((m02+m20)/s, (m12+m21)/s, s/4, (m10-m01)/s)
    n = math.sqrt(sum(v*v for v in q))
    return tuple(v/n for v in q)


def rotate(q, v):
    x, y, z, w = q
    t = [2*(y*v[2]-z*v[1]), 2*(z*v[0]-x*v[2]), 2*(x*v[1]-y*v[0])]
    return [v[0]+w*t[0]+y*t[2]-z*t[1], v[1]+w*t[1]+z*t[0]-x*t[2], v[2]+w*t[2]+x*t[1]-y*t[0]]


def chain(parents, top, bottom):
    """Joints from below `top` down to `bottom`, top first; None when bottom is not under top."""
    out = []
    j = bottom
    while j != top:
        if j < 0 or len(out) > len(parents):
            return None
        out.append(j)
        j = parents[j]
    return out[::-1]


def roles_from(names, parents, rest):
    """Which joints are which, by the rig's own names; positions break ties."""
    index = {n: i for i, n in enumerate(names)}
    lower = {n.lower(): i for i, n in enumerate(names)}
    children = [[] for _ in names]
    for j, p in enumerate(parents):
        if 0 <= p < len(names) and p != j:
            children[p].append(j)

    def under(j):
        out, todo = [], list(children[j])
        while todo:
            k = todo.pop()
            out.append(k)
            todo.extend(children[k])
        return out

    def pos(j):
        return column(rest[j], 3)

    def first(*options):
        for o in options:
            if o in index:
                return index[o]
            if o.lower() in lower:
                return lower[o.lower()]
        return -1
    pelvis = first('pelvis')
    head = first('head', 'Head', 'head_jnt')
    if head < 0:
        heads = [i for i, n in enumerate(names) if 'head' in n.lower() and not n.lower().startswith('head_')]
        head = min(heads, key=lambda i: len(under(i)) * -1) if heads else -1
    roles = dict(pelvis=pelvis, head=head, arms=[], legs=[], eyes=[-1, -1])
    for side in ('LF_', 'RT_'):
        clavicle = first(side+'clavicle')
        hand = first(side+'hand', side+'wrist', side+'Hand')
        arm = dict(clavicle=clavicle, hand=hand, upper=-1, lower=-1, finger=-1, thumb=-1)
        if clavicle >= 0 and hand >= 0 and (path := chain(parents, clavicle, hand)):
            # The shoulder is the clavicle's child on the way; the elbow the joint on it furthest from both ends.
            named_upper = first(side+'arm', side+'upperarm', side+'upArm')
            arm['upper'] = named_upper if named_upper in path else path[0]
            middle = [j for j in path if j not in (arm['upper'], hand)]
            named = [j for j in middle if any(k in names[j].lower() for k in ('forearm', 'elbow', 'lowarm'))
                     and 'twist' not in names[j].lower() and 'roll' not in names[j].lower()]
            arm['lower'] = named[0] if named else max(middle, key=lambda j: min(
                math.dist(pos(j), pos(arm['upper'])), math.dist(pos(j), pos(hand))), default=-1)
            fingers = [j for j in under(hand) if 'middle' in names[j].lower()]
            thumbs = [j for j in under(hand) if 'thumb' in names[j].lower()]
            arm['finger'] = min(fingers, key=lambda j: math.dist(pos(j), pos(hand)), default=-1)
            arm['thumb'] = min(thumbs, key=lambda j: math.dist(pos(j), pos(hand)), default=-1)
            if arm['finger'] < 0:
                # The child of the hand furthest out along the forearm.
                forearm = norm(sub(pos(hand), pos(arm['lower'])))
                arm['finger'] = max(children[hand], key=lambda j: dot(sub(pos(j), pos(hand)), forearm), default=-1)
        roles['arms'].append(arm)
        upleg = first(side+'upleg', side+'thigh', side+'upLeg')
        foot = first(side+'foot', side+'ankle', side+'Foot')
        leg = dict(upper=upleg, lower=-1, foot=foot)
        if upleg >= 0 and foot >= 0 and (path := chain(parents, upleg, foot)):
            middle = [j for j in path if j != foot]
            named = [j for j in middle if any(k in names[j].lower() for k in ('leg', 'knee', 'calf', 'shin'))
                     and 'up' not in names[j].lower() and 'twist' not in names[j].lower()]
            leg['lower'] = named[0] if named else max(middle, key=lambda j: min(
                math.dist(pos(j), pos(upleg)), math.dist(pos(j), pos(foot))), default=-1)
        roles['legs'].append(leg)
        roles['eyes'][0 if side == 'LF_' else 1] = first(side+'eye', side+'Eye', side+'eyeball')
    chest = -1
    if roles['arms'][0]['clavicle'] >= 0:
        chest = parents[roles['arms'][0]['clavicle']]
    roles['spine'] = chain(parents, pelvis, chest) if pelvis >= 0 and chest >= 0 else None
    roles['neck'] = chain(parents, chest, parents[head]) if chest >= 0 and head >= 0 and parents[head] != chest \
        else ([] if head >= 0 and parents[head] == chest else None)
    roles['chest'] = chest
    return roles


def roles_payload(roles, joints):
    spine, neck = roles['spine'][:8], roles['neck'][:8]
    values = [roles['pelvis'], roles['head'], *roles['eyes'], len(spine), len(neck), *(spine+[-1]*(8-len(spine))),
              *(neck+[-1]*(8-len(neck)))]
    for arm in roles['arms']:
        values += [arm[k] for k in ('clavicle', 'upper', 'lower', 'hand', 'finger', 'thumb')]
    for leg in roles['legs']:
        values += [leg[k] for k in ('upper', 'lower', 'foot')]
    return struct.pack('<4I4h2h8h8h12h6h', 0x53424452, 1, 96, joints, *values)


def command(serial, flags, eyes, facing, hands, height=0., lease=250):
    raw = struct.pack('<4IQ2I3f4f', 0x53424443, 1, 144, flags, serial, lease, 0, *eyes, *facing)
    for grip, orientation, tracked, fist in hands:
        raw += struct.pack('<3f4fIf', *grip, *orientation, tracked, fist)
    return raw+struct.pack('<2fI', height, 0, 0)


def hero_frame(game, record):
    """The hero's feet, forward, left (model +x) and up in the world."""
    m = struct.unpack('<16f', game.read(game.pointer(record), 64))
    left, upward, forward = column(m, 0), column(m, 1), column(m, 2)
    return list(m[12:15]), norm([forward[0], 0, forward[2]]), norm(left), norm(upward)


def to_world(transform, local):
    return [transform[12]+local[0]*transform[0]+local[1]*transform[4]+local[2]*transform[8],
            transform[13]+local[0]*transform[1]+local[1]*transform[5]+local[2]*transform[9],
            transform[14]+local[0]*transform[2]+local[1]*transform[6]+local[2]*transform[10]]


def grip_pose(knuckles, user_right):
    """An OpenXR grip orientation: +x the user's right, -y out past the knuckles."""
    y = scale(norm(knuckles), -1)
    x = norm(sub(user_right, scale(y, dot(user_right, y))))
    return quat_from_axes(x, y, cross(x, y))


def wrist_of(grip, orientation, side):
    return add(grip, rotate(orientation, [-.02 if side == 0 else .02, .09, 0]))


def hand_shape(names, body, transform, side, orientation):
    """How a solved hand came out: how far its palm faces from its controller's (degrees; a left palm faces the
    grip's +x, a right one its -x), each finger joint's bend about the finger's hinge (degrees; knuckle, middle, tip;
    negative is bent back), and the thumb: its two joints' bends, how far apart their hinges are (a twist), and its
    tip's height out of the fist over the index and middle fingers' middle bones (cm)."""
    index = {n: i for i, n in enumerate(names)}
    prefix = ('LF_', 'RT_')[side]
    try:
        def at(name):
            return column(body[index[prefix+name]], 3)
        chains = [[at(f'finger_{f}_{j}') for j in ('A', 'B', 'C', 'D', 'D_end')] for f in 'ABCD']
        thumb = [at(f'thumb_{j}') for j in ('A', 'B', 'C', 'C_end')]
        along = norm(sub(at('finger_B_B'), at('wrist')))
    except KeyError:
        return None
    bones = [0, 0, 0]
    for c in chains:
        bones = add(bones, norm(sub(c[1], c[0])))
    # From the index finger's knuckle to the little one's, a left hand's palm is on the right.
    n = cross(norm(sub(chains[3][1], chains[0][1])), norm(bones))
    n = scale(n, -1) if side else n
    palm = norm(sub(n, scale(along, dot(n, along))))
    palm_world = norm(sub(to_world(transform, palm), to_world(transform, [0, 0, 0])))
    want = rotate(orientation, [-1 if side else 1, 0, 0])

    def degrees(a, b, c, hinge):
        x, y = norm(sub(b, a)), norm(sub(c, b))
        x, y = sub(x, scale(hinge, dot(x, hinge))), sub(y, scale(hinge, dot(y, hinge)))
        return round(math.degrees(math.atan2(dot(cross(x, y), hinge), dot(x, y))), 1)
    fingers = {}
    for name, c in zip(('index', 'middle', 'ring', 'little'), chains):
        hinge = norm(cross(norm(sub(c[1], c[0])), palm))
        fingers[name] = [degrees(c[k-1], c[k], c[k+1], hinge) for k in (1, 2, 3)]
    segments = [sub(thumb[k+1], thumb[k]) for k in range(3)]
    def angle(u, v):
        return math.degrees(math.atan2(math.sqrt(dot(cross(u, v), cross(u, v))), dot(u, v)))
    hinges = [norm(cross(segments[0], segments[1])), norm(cross(segments[1], segments[2]))]
    middles = scale(add(add(chains[0][2], chains[0][3]), add(chains[1][2], chains[1][3])), .25)
    return dict(palm_off_deg=round(math.degrees(math.acos(max(-1., min(1., dot(palm_world, want))))), 1),
                fingers=fingers,
                thumb=dict(bends=[round(angle(segments[0], segments[1]), 1), round(angle(segments[1], segments[2]), 1)],
                           twist_deg=round(math.degrees(math.acos(max(-1., min(1., dot(*hinges))))), 1),
                           tip_out_cm=round(dot(sub(thumb[3], middles), palm)*100, 1)))


def screenshot(game, name):
    hwnd = game_window(game.pid)
    width, height = client_size(hwnd)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = OUTPUT/f'{name}.png'
    write_rgb_png(path, width, height, window_rgb(hwnd, width, height))
    return str(path)


def state_names(game, pe, machine, names={}):
    out = []
    for offset, valid in ((0x70, 0x80), (0x98, 0xa8)):
        state = game.pointer(machine+offset)
        vt = game.pointer(state)-game.base if state and game.read(machine+valid, 1) != bytes(1) else 0
        if vt and vt not in names:
            try:
                names[vt] = pe.vtable(vt)['type'].replace('.?AV', '').rstrip('@')
            except Exception:
                names[vt] = hex(vt)
        out.append(names.get(vt))
    return out


def fist_phase(game, process, pe, hero, feet):
    """A fist through the nearest bot's chest, as a controller's samples drive it: the swing module's input."""
    from probe_game_grab import hand_command, resolve, HERO_MOVERS
    from run_game_vr import punch_snapshot
    components = list(registry(game))
    bots = []
    for a, v, r, _ in components:
        if v == BOT_MOVERS:
            t = game.transform(game.pointer(r))
            if t:
                bots.append((math.dist(t['position'], feet), r, t['position']))
    if not bots:
        return dict(error='no bot in the game')
    distance, bot, at = min(bots)
    machines = {r: a for a, v, r, _ in components if v == SYNC_MACHINE}
    healths = {r: a for a, v, r, _ in components if v in (HEALTH, BOT_HEALTH)}
    managers = [a for a, v, r, _ in components if v == HERO_MOVERS and r == hero]
    mover = resolve(game, struct.unpack('<I', game.read(managers[0]+0xdb4, 4))[0])
    motion, _ = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
                        ROOT/'reports/motion-modules', ('SpidyMotionStart', 'SpidyMotionStop', 'SpidyMotionDrive'))
    motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
    rays, _ = prepare(game.pid, process, RAY_DLL, ROOT/'reports/ray-modules',
                      ('SpidyRayStart', 'SpidyRayStop', 'SpidySwingStart', 'SpidySwingSubmit', 'SpidySwingStop',
                       'SpidyPunchStart', 'SpidyPunchStop', 'SpidyPunchData'))
    started = []

    def invoke(name, payload):
        code = call_with_payload(process, rays[name], payload)
        if code:
            raise RuntimeError(f'{name}: {code}')
    try:
        invoke('SpidyRayStart', struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 30000, 0x410))
        started.append('SpidyRayStop')
        # No web catches anything (grab kinds 0): the fists alone.
        invoke('SpidySwingStart', struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, hero, mover,
                                              motion_module, 30000, 32., 6., 0))
        started.insert(0, 'SpidySwingStop')
        invoke('SpidyPunchStart', struct.pack('<4IQ', 0x53505543, 1, 24, game.pid, game.base))
        started.insert(0, 'SpidyPunchStop')
        chest = add(at, [0, 1.3, 0])
        direction = norm(sub(chest, add(feet, [0, 1.3, 0])))
        start, end = sub(chest, scale(direction, .9)), add(chest, scale(direction, .1))
        health = healths.get(bot)

        def hit_points():
            raw = game.read(health+0x88, 4) if health else b''
            return round(struct.unpack('<f', raw)[0], 3) if len(raw) == 4 else None
        before = dict(hp=hit_points(), states=state_names(game, pe, machines[bot]) if bot in machines else None)
        serial = int(time.time()*1000)
        samples = []

        def submit(position, frames):
            nonlocal serial
            for _ in range(frames):
                serial += 1
                # Relative to the player as the arm moves it: the fist's own travel.
                relative = add([.25, -.35, -.3], sub(position, start))
                invoke('SpidySwingSubmit', hand_command(serial, position, direction, tuple(relative)))
                time.sleep(1/90)
        submit(start, 30)
        steps = int(round(math.dist(start, end)/6*90))
        for k in range(steps+1):
            position = add(start, scale(sub(end, start), k/steps))
            submit(position, 1)
            punch = punch_snapshot(game, rays['SpidyPunchData'])
            samples.append(dict(k=k, punch=punch['punches'] if punch else None,
                                speed=punch['hands'][0]['speed'] if punch else None))
        submit(end, 60)
        punch = punch_snapshot(game, rays['SpidyPunchData'])
        after = dict(hp=hit_points(), states=state_names(game, pe, machines[bot]) if bot in machines else None)
        result = dict(bot=hex(bot), distance=round(distance, 1), before=before, after=after, punch=punch,
                      samples=samples)
        print('Fist:', json.dumps({k: v for k, v in result.items() if k != 'samples'}), flush=True)
        return result
    finally:
        for name in started:
            try:
                call_remote(process, rays[name])
            except OSError:
                pass


def main():
    try:
        c.windll.user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    except AttributeError:
        c.windll.user32.SetProcessDPIAware()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--phases', default='rig,ik,eyes,fist')
    parser.add_argument('--eye-size', type=int, default=1024)
    parser.add_argument('--fists', default='0,1',
                        help='how far the left and right hands close into fists, 0 to 1 (default: left open, '
                             'right closed)')
    parser.add_argument('--punch-hero', action='store_true',
                        help='also deal the hero a light stagger, to check the damage pipeline without a bot')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/body-probe.json')
    args = parser.parse_args()
    phases = args.phases.split(',')
    fists = [min(1., max(0., float(v))) for v in args.fists.split(',')]
    if len(fists) != 2:
        parser.error('--fists takes two values, left and right')
    game = Game(find_game())
    process = None
    report = dict(phases=phases)
    stereo = rays = None
    body_active = views_active = gpu_active = punch_active = False
    try:
        pe = PE(game.path.read_bytes())
        front = bring_to_front(game.pid)
        components = list(registry(game))
        heroes = [(a, r) for a, v, r, _ in components if v == HERO_LOCAL]
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required: load free roam first')
        hero = heroes[0][1]
        report.update(pid=game.pid, base=hex(game.base), hero=hex(hero), front=front)
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        stereo, digest = prepare(game.pid, process, STEREO_DLL, ROOT/'reports/stereo-modules',
                                 ('SpidyBodyStart', 'SpidyBodySubmit', 'SpidyBodyRoles', 'SpidyBodyCapture',
                                  'SpidyBodyStop', 'SpidyBodyData', 'SpidyBodyPoses', 'SpidyStart', 'SpidyStop',
                                  'SpidySetEyes', 'SpidyGpuStart', 'SpidyGpuStop', 'SpidyGpuData',
                                  'SpidyGpuFreeze', 'SpidyAppearanceData'))
        report['stereo_dll'] = digest
        code = call_with_payload(process, stereo['SpidyBodyStart'],
                                 struct.pack('<4I2Q', 0x53424346, 1, 32, game.pid, game.base, hero))
        if code:
            raise RuntimeError(f'Body start: {code}')
        body_active = True
        status = None
        for _ in range(150):
            status = body_status(game, stereo['SpidyBodyData'])
            if status and status['hero_jobs'] and status['rig']:
                break
            time.sleep(.02)
        report['status_start'] = status
        print('Body:', json.dumps(status), flush=True)
        if not status or not status['rig']:
            raise RuntimeError('The hero\'s pose job never ran through the body')
        rig = status['rig']
        joints_table, table = rig_table(game, rig)
        names, names_base = find_names(game, rig, joints_table, table)
        poses = capture(game, process, stereo)
        if not poses:
            raise RuntimeError('No pose was captured')
        parents = [j['parent'] for j in table]
        rest = poses['rest'] if poses['joints'] == len(table) else None
        report['rig'] = dict(address=hex(rig), joints=len(table), joint_table=hex(joints_table),
                             names_base=hex(names_base) if names_base else None, transform=poses['transform'])
        listing = []
        for j in table:
            entry = dict(index=j['index'], parent=j['parent'], flags=j['flags'], hash=hex(j['hash']),
                         name=names[j['index']] if names else None)
            if rest:
                entry['rest'] = [round(v, 4) for v in column(rest[j['index']], 3)]
                if j['parent'] >= 0:
                    entry['bone'] = round(math.dist(column(rest[j['index']], 3), column(rest[j['parent']], 3)), 4)
            entry['game'] = [round(v, 4) for v in column(poses['game'][j['index']], 3)] if poses['joints'] else None
            listing.append(entry)
        report['joints'] = listing
        # The joints the game starts its web lines from (hero rope manager +7d40: Start, Grip, Grip2, End).
        managers = [a for a, v, r, _ in components if v == ROPE_MANAGER and r == hero]
        if managers:
            hashes = struct.unpack('<8I', game.read(managers[0]+0x7d40, 32))
            by_hash = {j['hash']: j['index'] for j in table}
            report['rope_joints'] = [dict(hash=hex(h), joint=by_hash.get(h),
                                          name=names[by_hash[h]] if names and h in by_hash else None)
                                     for h in hashes]
        print(f"Rig {rig:#x}: {len(table)} joints, names {'found' if names else 'NOT found'}", flush=True)
        if names:
            for e in listing:
                print(f"  {e['index']:3} {e['name']:28} parent {e['parent']:4} rest {e.get('rest')}")
        roles = roles_from(names, parents, rest) if names and rest else None
        report['roles'] = roles
        print('Roles:', json.dumps(roles), flush=True)
        if 'ik' in phases or 'eyes' in phases:
            status = body_status(game, stereo['SpidyBodyData'])
            report['builtin_identification'] = status['problem']
            if roles and roles['spine'] is not None and roles['neck'] is not None and status['problem'] != 'none':
                code = call_with_payload(process, stereo['SpidyBodyRoles'], roles_payload(roles, len(table)))
                report['roles_override'] = code
                print('Roles override:', code, flush=True)
        serial = int(time.time()*1000)
        feet, forward, left, up = hero_frame(game, hero)
        user_right = scale(left, -1)
        facing = quat_from_axes(user_right, up, scale(forward, -1))
        eyes_rel = add(scale(up, 1.66), scale(forward, .06))
        targets = [
            # The left hand raised above the head, knuckles up; the right one punching ahead at the chin.
            (add(add(scale(up, 2.0), scale(left, .25)), scale(forward, .15)), grip_pose(up, user_right)),
            (add(add(scale(up, 1.45), scale(left, -.1)), scale(forward, .5)), grip_pose(forward, user_right)),
        ]

        def drive(seconds, flags, look=None):
            nonlocal serial
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                serial += 1
                # By default the right hand punching ahead is a closed fist.
                hands = [(grip, orientation, 1, fists[side]) for side, (grip, orientation) in enumerate(targets)]
                code = call_with_payload(process, stereo['SpidyBodySubmit'],
                                         command(serial, flags, eyes_rel, look or facing, hands, 1.75))
                if code:
                    raise RuntimeError(f'Body command: {code}')
                if eye_driver:
                    eye_driver()
                time.sleep(.02)
        eye_driver = None
        if 'ik' in phases:
            drive(1.5, BODY_ON | HIDE_HEAD | HAND_TURN)
            status = body_status(game, stereo['SpidyBodyData'])
            # The capture is taken by the next pose job: the body stays commanded meanwhile.
            call_remote(process, stereo['SpidyBodyCapture'])
            drive(.25, BODY_ON | HIDE_HEAD | HAND_TURN)
            solved = capture(game, process, stereo, poses['captured'])
            result = dict(status=status)
            if solved and roles and status['problem'] == 'none':
                transform = solved['transform']
                for side, arm in enumerate(roles['arms']):
                    wrist = to_world(transform, column(solved['body'][arm['hand']], 3))
                    want = wrist_of(add(feet, targets[side][0]), targets[side][1], side)
                    result[f'wrist_{side}'] = dict(world=[round(v, 3) for v in wrist],
                                                   target=[round(v, 3) for v in want],
                                                   error=round(math.dist(wrist, want), 4))
                head = to_world(transform, column(solved['body'][roles['head']], 3))
                result['head'] = [round(v, 3) for v in head]
                result['eyes_target'] = [round(v, 3) for v in add(feet, eyes_rel)]
                result['head_scale'] = round(math.sqrt(dot(column(solved['body'][roles['head']], 0),
                                                           column(solved['body'][roles['head']], 0))), 4)
                if names:
                    result['hands'] = [dict(fist=fists[side], **(hand_shape(names, solved['body'], transform, side,
                                                                            targets[side][1]) or {}))
                                       for side in (0, 1)]
            result['screenshot'] = screenshot(game, 'ik-third-person')
            report['ik'] = result
            print('IK:', json.dumps(result), flush=True)
        if 'eyes' in phases:
            from probe_stereo_gpu import discover_queue, snapshot as gpu_snapshot
            queue = discover_queue(game, process)
            size = args.eye_size
            code = call_with_payload(process, stereo['SpidyGpuStart'],
                                     struct.pack('<4I2Q4I', 0x53475043, 3, 48, game.pid, game.base, queue, 0, 0, 1, 0))
            if code:
                raise RuntimeError(f'GPU capture start: {code}')
            gpu_active = True
            # Eye views, the display path, the active view at the head, each eye's occlusion: a VR session's.
            code = call_with_payload(process, stereo['SpidyStart'],
                                     struct.pack('<4IQ4I', 0x53534346, 1, 40, game.pid, game.base, 0, 29, size, size))
            if code:
                raise RuntimeError(f'Native eye views start: {code}')
            views_active = True
            # Looking 50 degrees down at the body.
            pitch = math.radians(50)
            look = norm(sub(scale(forward, math.cos(pitch)), scale(up, math.sin(pitch))))
            down = cross(look, user_right)
            look_facing = quat_from_axes(user_right, scale(down, -1), scale(look, -1))
            eye_serial = [serial]

            def eyes_now():
                eye_serial[0] += 1
                raw = struct.pack('<4IQ2I', 0x53455043, 2, 208, 1, eye_serial[0], 250, 0)
                centre = add(feet, eyes_rel)
                for side in (-1, 1):
                    p = add(centre, scale(user_right, .032*side))
                    matrix = [*user_right, 0, *down, 0, *look, 0, *p, 1]
                    raw += struct.pack('<16f4f', *matrix, -.9, .9, -.9, .9)
                raw += struct.pack('<3fI', *feet, 1)
                code = call_with_payload(process, stereo['SpidySetEyes'], raw)
                if code and code != 4003:
                    raise RuntimeError(f'Eye command: {code}')
            eye_driver = eyes_now
            images = {}
            for label, flags in (('body', BODY_ON | HIDE_HEAD | HAND_TURN), ('hidden', 0)):
                drive(2.0, flags, look_facing)
                code = call_remote(process, stereo['SpidyGpuFreeze'])
                gpu = gpu_snapshot(game, stereo['SpidyGpuData'])
                status = body_status(game, stereo['SpidyBodyData'])
                if code or not gpu or not gpu['left_pixels']:
                    images[label] = dict(error=f'no eye image ({code})', status=status)
                    continue
                width, height = gpu['width'], gpu['height']
                rgba = game.read(gpu['left_pixels'], width*height*4)
                rgb = bytearray(width*height*3)
                rgb[0::3], rgb[1::3], rgb[2::3] = rgba[0::4], rgba[1::4], rgba[2::4]
                OUTPUT.mkdir(parents=True, exist_ok=True)
                path = OUTPUT/f'eye-{label}.png'
                write_rgb_png(path, width, height, bytes(rgb))
                images[label] = dict(image=str(path), status=status)
                print(label, json.dumps(images[label]), flush=True)
            if 'walk' in phases:
                # Walking about: the stick swings round four ways, so the hero turns every 0.4 s. How far he
                # turns between his pose job and the render shows how far the body is off while he turns.
                pad = prepare(game.pid, process, STEREO_DLL, ROOT/'reports/stereo-modules', ('SpidyPadSubmit',))[0]
                directions = [(0, .45), (.45, 0), (0, -.45), (-.45, 0)]
                walk = []
                end = time.monotonic()+4
                shot_taken = None
                started = time.monotonic()
                while time.monotonic() < end:
                    phase = int((time.monotonic()-started)/.4) % 4
                    x, y = directions[phase]
                    state = struct.pack('<H2B4h', 0, 0, 0, int(x*32767), int(y*32767), 0, 0)
                    call_with_payload(process, pad['SpidyPadSubmit'], state+struct.pack('<I', 80))
                    drive(.04, BODY_ON | HIDE_HEAD | HAND_TURN, look_facing)
                    status = body_status(game, stereo['SpidyBodyData'])
                    t = game.transform(game.pointer(hero))
                    walk.append(dict(t=round(time.monotonic()-started, 3), turn=status['turn_last'],
                                     hands=status['hand_error'], head=status['head_error'],
                                     grounded=status['grounded'], position=t['position'] if t else None))
                    if shot_taken is None and time.monotonic()-started > 2.1:
                        code = call_remote(process, stereo['SpidyGpuFreeze'])
                        gpu = gpu_snapshot(game, stereo['SpidyGpuData'])
                        if not code and gpu and gpu['left_pixels']:
                            width, height = gpu['width'], gpu['height']
                            rgba = game.read(gpu['left_pixels'], width*height*4)
                            rgb = bytearray(width*height*3)
                            rgb[0::3], rgb[1::3], rgb[2::3] = rgba[0::4], rgba[1::4], rgba[2::4]
                            path = OUTPUT/'eye-walk.png'
                            write_rgb_png(path, width, height, bytes(rgb))
                            shot_taken = str(path)
                status = body_status(game, stereo['SpidyBodyData'])
                turns = [w['turn'] for w in walk]
                travelled = math.dist(walk[0]['position'], walk[-1]['position']) if walk[0]['position'] else None
                report['walk'] = dict(image=shot_taken, turn_max=status['turn_max'],
                                      turn_mean=round(sum(turns)/len(turns), 5) if turns else None,
                                      hand_error_max=max(max(w['hands']) for w in walk),
                                      head_error_max=max(w['head'] for w in walk),
                                      travelled=round(travelled, 2) if travelled is not None else None,
                                      samples=walk)
                print('Walk:', json.dumps({k: v for k, v in report['walk'].items() if k != 'samples'}), flush=True)
            eye_driver = None
            report['eyes'] = images
            call_remote(process, stereo['SpidyStop'])
            views_active = False
            call_remote(process, stereo['SpidyGpuStop'])
            gpu_active = False
        report['status_end'] = body_status(game, stereo['SpidyBodyData'])
        if 'fist' in phases:
            report['fist'] = fist_phase(game, process, pe, hero, feet)
        if 'punch' in phases:
            rays, _ = prepare(game.pid, process, RAY_DLL, ROOT/'reports/ray-modules',
                              ('SpidyPunchStart', 'SpidyPunchStop', 'SpidyPunchTest', 'SpidyPunchData'))
            code = call_with_payload(process, rays['SpidyPunchStart'],
                                     struct.pack('<4IQ', 0x53505543, 1, 24, game.pid, game.base))
            if code:
                raise RuntimeError(f'Punch start: {code}')
            punch_active = True
            components = list(registry(game))
            machines = {r: a for a, v, r, _ in components if v == SYNC_MACHINE}
            healths = {r: a for a, v, r, _ in components if v in (HEALTH, BOT_HEALTH, HERO_HEALTH)}
            feet, forward, left, up = hero_frame(game, hero)
            others = []
            for a, v, r, _ in components:
                if v in (BOT_MOVERS, THROWABLE):
                    t = game.transform(game.pointer(r))
                    if t:
                        others.append((v != BOT_MOVERS, math.dist(t['position'], feet), r,
                                       'bot' if v == BOT_MOVERS else 'prop'))
            others.sort()
            blows = []
            bots = [x for x in others if x[3] == 'bot']
            if bots:
                # A thug: a real punch's blow, from the hero, sending him flying.
                _, distance, victim, _ = bots[0]
                at = game.transform(game.pointer(victim))['position']
                point = add(at, [0, 1.3, 0])
                blows.append(dict(kind='bot', victim=victim, damager=hero, distance=distance, point=point,
                                  direction=norm(sub(point, add(feet, [0, 1.3, 0]))), amount=30., knockback=5,
                                  knockback_amount=6.))
            if args.punch_hero and others:
                # The hero himself, from the nearest other actor: a light stagger backward, toward the roof.
                blows.append(dict(kind='hero', victim=hero, damager=others[0][2], distance=0.,
                                  point=add(feet, [0, 1.3, 0]), direction=scale(forward, -1), amount=5.,
                                  knockback=2, knockback_amount=2.))
            results = []
            for blow in blows:
                victim = blow['victim']
                health = healths.get(victim)

                def hit_points():
                    raw = game.read(health+0x88, 4) if health else b''
                    return round(struct.unpack('<f', raw)[0], 3) if len(raw) == 4 else None
                samples = []
                t0 = time.monotonic()

                def sample(label):
                    t = game.transform(game.pointer(victim))
                    samples.append(dict(t=round(time.monotonic()-t0, 3), phase=label,
                                        position=[round(v, 3) for v in t['position']] if t else None,
                                        states=state_names(game, pe, machines[victim]) if victim in machines else None,
                                        hp=hit_points()))
                for _ in range(10):
                    sample('before')
                    time.sleep(.03)
                payload = struct.pack('<4I2Q3f3f3fiII', 0x53505554, 1, 80, 1, victim, blow['damager'], *blow['point'],
                                      *blow['direction'], blow['amount'], blow['knockback_amount'], -1.,
                                      blow['knockback'], 0, 0)
                code = call_with_payload(process, rays['SpidyPunchTest'], payload)
                for _ in range(80):
                    sample('after')
                    time.sleep(.03)
                result = dict(kind=blow['kind'], victim=hex(victim), damager=hex(blow['damager']),
                              distance=round(blow['distance'], 1), code=code,
                              health_component=hex(health) if health else None,
                              hp=[samples[0]['hp'], samples[-1]['hp']],
                              states=sorted({s for x in samples for s in (x['states'] or []) if s}),
                              moved=round(math.dist(samples[0]['position'], samples[-1]['position']), 3)
                              if samples[0]['position'] and samples[-1]['position'] else None,
                              samples=samples)
                results.append(result)
                print('Blow:', json.dumps({k: result[k] for k in ('kind', 'distance', 'code', 'hp', 'states', 'moved')}),
                      flush=True)
            report['punch'] = dict(blows=results, bots=len(bots), others=len(others),
                                   screenshot=screenshot(game, 'after-blows'))
        return 0
    finally:
        try:
            if views_active:
                call_remote(process, stereo['SpidyStop'])
            if gpu_active:
                call_remote(process, stereo['SpidyGpuStop'])
            if body_active:
                report['body_stop'] = call_remote(process, stereo['SpidyBodyStop'])
            if punch_active:
                call_remote(process, rays['SpidyPunchStop'])
        except OSError:
            pass
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=1, default=str)+'\n')
        print(f'Report: {args.output}', flush=True)
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Body probe failed: {error}', file=sys.stderr)
        sys.exit(2)
