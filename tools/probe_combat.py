"""Spidy's web balls, fists and web pulls on the game's enemies, flying ones included, without a headset.

    python tools/probe_combat.py --list                 the bots near the player, as Spidy's scan sees them
    python tools/probe_combat.py [--target flyer] [--phases shoot,punch,pull] [--range 40]

--list only reads the game, so it can run again and again while the player travels; the probes load Spidy's ray
and movement modules, which start once per game process, so each needs a freshly started game in free roam with
enemies within --range of the player, and the game window in front (the probe brings it there).

Bots are what Spidy's target scan (src/game_targets.cpp) takes for them, by the same rules on the game's own type
information (RTTI): an actor with a component derived from BotMoverManager (BotMoverManagerGame walks bots on foot,
HoverMoverManager flies the ones that hover), with the same traits (thug, civilian, ally, webbable, hover, neutral)
from the classes its components are or derive from. For each it lists the bot's own class, its mover manager, its
state machine's two states, its health, its webbing against its threshold, its MoverStandard, and where it is. It
also lists any actor with a Bot component but no bot mover manager: one the scan would miss. --target picks the
enemies to try: the nearest (any), a flyer (hover) or a walker, or one actor record (0x...). Each phase takes the
nearest target not tried yet, the first one when all are.

  shoot  --shots web balls (SpidyShooterTest) from 6 m before the target's chest, at it, --every apart: the shooter's
         collisions, webbing blows and latest actor hit, and the target's states, health and webbing for 3 s.
  punch  a scripted fist through the target's chest at 6 m/s, as a controller's samples drive it through the swing's
         input: the punch module's punches and the target hit, its health and states before and after.
  pull   a scripted hand next to the player webs the target (grip), yanks it over, carries it, winds up and throws
         it: the grab's catches, knock-backs, steered flight steps, impacts, and the target's states and path.

Writes reports/combat-probe.json and window captures in reports/grab-probe/.
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
from probe_game_grab import HERO_LOCAL, HERO_MOVERS, grab_snapshot, hand_command, registry, resolve, shot
from probe_game_screen import user32
from run_game_vr import punch_snapshot, shooter_snapshot
from vr_launcher import bring_to_front

# Spidy's scan's rules (src/game_targets.cpp): a class, or one it derives from, marks a bot or gives a trait.
BOT_MARKER = 'BotMoverManager'
TRAITS = {'HoverMoverManager': 'hover', 'ThugBot': 'thug', 'CivilianBot': 'civilian', 'AllyBot': 'ally',
          'MissionFollowBot': 'ally', 'StatusEffectTrackerWebbed': 'webbable', 'BreakableSystemComponent': 'breakable',
          'BirdBot': 'neutral', 'Helicopter': 'neutral', 'SilverSableCraftBot': 'neutral'}
MACHINE, BOT_HEALTH, HEALTH, MOVER_STANDARD, MOVER_BODY_SIZE = 0x4f843d0, 0x3839e98, 0x3839e08, 0x4f70168, 0x500e870
# The hooks the ray module installs: castRay, mover prequery and gravity, preCollide, camera update, muzzle, shot event.
ENTRIES = (0x2e67010, 0x1fbe360, 0x1fbda50, 0x2e54300, 0x897d30, 0x2150c40, 0xd2a570)
# A street thug's chest above his feet (the shooter's aim), his centre (the grab's) and where a fist goes.
CHEST, CENTRE, FIST = 1.15, .95, 1.3


def height(bot, thug):
    """A height tuned on a street thug carried over to this bot's size, as game_targets::Size does."""
    low, high, _ = bot['size'] or (.4, 1.6, .45)
    return low+(thug-.4)*(high-low)/1.2


def add(a, b):
    return [x+y for x, y in zip(a, b)]


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def scale(a, k):
    return [x*k for x in a]


def norm(a):
    n = math.sqrt(sum(x*x for x in a)) or 1
    return [x/n for x in a]


def lineage(pe, vtable):
    """A class and every class it derives from, by name, from the RTTI before its vtable (an image offset)."""
    try:
        col = pe.u64(pe.offset(vtable-8, 8))-pe.image_base
        signature, offset, _, _, hierarchy, own = struct.unpack_from('<6I', pe.data, pe.offset(col, 24))
        if signature != 1 or offset or own != col:
            return []
        count, bases = struct.unpack_from('<2I', pe.data, pe.offset(hierarchy+8, 8))
        names = []
        for i in range(count):
            descriptor = struct.unpack_from('<I', pe.data, pe.offset(bases+4*i, 4))[0]
            at = pe.offset(struct.unpack_from('<I', pe.data, pe.offset(descriptor, 4))[0]+16)
            name = pe.data[at:pe.data.index(b'\0', at, at+512)].decode('ascii')
            names.append(name.removeprefix('.?AV').removeprefix('.?AU').removesuffix('@@'))
        return names
    except (ValueError, struct.error):
        return []


class World:
    """The player and the bots, read from outside the game."""

    def __init__(self, game, pe):
        self.game, self.pe, self.lineages = game, pe, {}

    def lineage(self, vtable):
        if vtable not in self.lineages:
            self.lineages[vtable] = lineage(self.pe, vtable)
        return self.lineages[vtable]

    def name(self, address):
        if not address:
            return None
        vt = self.game.pointer(address)-self.game.base
        return (self.lineage(vt) or [hex(vt)])[0]

    def position(self, record):
        t = self.game.transform(self.game.pointer(record))
        return t['position'] if t else None

    def scan(self):
        parts = {}
        for address, vt, record, _ in registry(self.game):
            parts.setdefault(record, {})[vt] = address
        hero = next((r for r, p in parts.items() if HERO_LOCAL in p), None)
        feet = self.position(hero) if hero else None
        bots, missed = [], []
        throwables = 0
        for record, p in parts.items():
            lineages = {vt: self.lineage(vt) for vt in p}
            throwables += sum('ThrowableHelper' in names for names in lineages.values())
            manager = next((vt for vt, names in lineages.items() if BOT_MARKER in names), None)
            # The bot's own class: the component derived from Bot (ThugBot, DroneBot, the bosses...).
            classes = sorted(names[0] for names in lineages.values() if 'Bot' in names)
            at = self.position(record)
            distance = math.dist(at, feet) if at and feet else None
            if manager is None:
                if classes:
                    missed.append(dict(record=hex(record), classes=classes,
                                       components=sorted(names[0] for names in lineages.values() if names),
                                       distance=round(distance, 1) if distance is not None else None))
                continue
            traits = sorted({TRAITS[n] for names in lineages.values() for n in names if n in TRAITS})
            mover = resolve(self.game, struct.unpack('<I', self.game.read(p[manager]+0xdb4, 4))[0])
            mover_ok = bool(mover) and self.game.pointer(mover) == self.game.base+MOVER_STANDARD and \
                self.game.pointer(mover+8) == record
            bots.append(dict(record=record, manager=lineages[manager][0], component=p[manager],
                             size=self.size(p[manager]),
                             machine=p.get(MACHINE), health=p.get(BOT_HEALTH) or p.get(HEALTH),
                             tracker=next((p[vt] for vt, names in lineages.items()
                                           if 'StatusEffectTrackerWebbed' in names), None),
                             traits=traits, enemy=not {'civilian', 'ally', 'neutral'} & set(traits), classes=classes,
                             mover=mover if mover_ok else 0, position=at, distance=distance,
                             above=at[1]-feet[1] if at and feet else None))
        bots.sort(key=lambda b: b['distance'] if b['distance'] is not None else 1e9)
        return dict(hero=hero, feet=feet, bots=bots, missed=missed, throwables=throwables, parts=parts)

    def size(self, manager):
        """The capsule a bot's mover collides with, above his actor's transform, as game_targets::sizeOf reads it:
        [low, high, radius], None without a body."""
        if self.game.pointer(manager+0xdb8) != self.game.base+MOVER_BODY_SIZE:
            return None
        low, high, radius = struct.unpack('<3f', self.game.read(manager+0xdc0, 12))
        scale = struct.unpack('<f', self.game.read(manager+0xdf8, 4))[0]
        return [round(low-radius, 3), round(high*scale+radius, 3), round(radius, 3)]

    def state(self, bot):
        out = dict(states=[], hp=None, webbing=None, position=None)
        machine = bot.get('machine')
        for offset, valid in ((0x70, 0x80), (0x98, 0xa8)):
            obj = self.game.pointer(machine+offset) if machine else 0
            ok = obj and self.game.read(machine+valid, 1) != bytes(1)
            out['states'].append(self.name(obj) if ok else None)
        if bot.get('health'):
            out['hp'] = round(struct.unpack('<f', self.game.read(bot['health']+0x88, 4))[0], 2)
        if bot.get('tracker'):
            current, threshold = struct.unpack('<2f', self.game.read(bot['tracker']+0x50, 8))
            out['webbing'] = [round(current, 2), round(threshold, 2)]
        at = self.position(bot['record'])
        out['position'] = [round(x, 2) for x in at] if at else None
        return out


def describe(bot):
    return dict(record=hex(bot['record']), manager=bot['manager'], classes=bot['classes'], traits=bot['traits'],
                enemy=bot['enemy'], mover=hex(bot['mover']), size=bot['size'],
                distance=round(bot['distance'], 1) if bot['distance'] is not None else None,
                above=round(bot['above'], 1) if bot['above'] is not None else None)


def states_seen(rows):
    seen = []
    for row in rows:
        for s in row['states']:
            if s and s not in seen:
                seen.append(s)
    return seen


def main():
    try:
        user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    except AttributeError:
        user32.SetProcessDPIAware()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--list', action='store_true', help='list the bots and stop (reads the game only)')
    parser.add_argument('--target', default='nearest', help='nearest, flyer, walker, or an actor record 0x...')
    parser.add_argument('--phases', default='shoot,punch,pull')
    parser.add_argument('--range', type=float, default=40)
    parser.add_argument('--shots', type=int, default=3)
    parser.add_argument('--every', type=float, default=.3)
    parser.add_argument('--watch', type=float, default=4, help='seconds to follow a thrown target')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/combat-probe.json')
    args = parser.parse_args()
    game = Game(find_game())
    pe = PE(game.path.read_bytes())
    world = World(game, pe)
    seen = world.scan()
    if args.list:
        print('player', [round(v, 1) for v in seen['feet']] if seen['feet'] else None, flush=True)
        for bot in seen['bots']:
            print(json.dumps(dict(**describe(bot), **world.state(bot))), flush=True)
        for bot in seen['missed']:
            print('missed', json.dumps(bot), flush=True)
        print(len(seen['bots']), 'bots,', sum(b['enemy'] for b in seen['bots']), 'enemies,',
              sum('hover' in b['traits'] for b in seen['bots']), 'flying,', len(seen['missed']),
              'with a Bot component but no bot mover manager', flush=True)
        return 0
    phases = [p for p in args.phases.split(',') if p]
    report = dict(phases={}, args=vars(args) | dict(output=str(args.output)))
    if any(game.read(game.base+rva, 16) != pe.bytes(rva, 16) for rva in ENTRIES):
        raise SystemExit('A hook entry is already patched: start the game afresh')
    feet = seen['feet']
    if not seen['hero'] or not feet:
        raise SystemExit('No player')

    def wanted(bot):
        if bot['distance'] is None or bot['distance'] > args.range or not bot['enemy']:
            return False
        if args.target == 'flyer':
            return 'hover' in bot['traits']
        if args.target == 'walker':
            return 'hover' not in bot['traits']
        if args.target.startswith('0x'):
            return bot['record'] == int(args.target, 16)
        return True
    targets = [b for b in seen['bots'] if wanted(b)]
    report['hero'] = dict(record=hex(seen['hero']), feet=feet)
    report['bots'] = [dict(**describe(b), **world.state(b)) for b in seen['bots'] if b['distance'] is not None and
                      b['distance'] <= max(args.range, 60)]
    report['missed'] = seen['missed']
    print('targets', [describe(b) for b in targets], flush=True)
    if not targets:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=1, default=str)+'\n')
        raise SystemExit(f'No {args.target} enemy within {args.range} m')
    report['front'] = bring_to_front(game.pid)
    process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
    rays = None
    started = []
    tried = set()

    def pick():
        fresh = [b for b in targets if b['record'] not in tried]
        bot = (fresh or targets)[0]
        tried.add(bot['record'])
        return bot
    try:
        managers = [a for a, v, r, _ in registry(game) if v == HERO_MOVERS and r == seen['hero']]
        mover = resolve(game, struct.unpack('<I', game.read(managers[0]+0xdb4, 4))[0])
        _, report['motion_hash'] = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
                                           ROOT/'reports/motion-modules',
                                           ('SpidyMotionStart', 'SpidyMotionStop', 'SpidyMotionDrive'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        rays, report['ray_hash'] = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll',
                                           ROOT/'reports/ray-modules',
                                           ('SpidyRayStart', 'SpidyRayStop', 'SpidySwingStart', 'SpidySwingSubmit',
                                            'SpidySwingStop', 'SpidyGrabData', 'SpidyShooterStart',
                                            'SpidyShooterStop', 'SpidyShooterTest', 'SpidyShooterData',
                                            'SpidyPunchStart', 'SpidyPunchStop', 'SpidyPunchData'))

        def invoke(name, payload, stop=None):
            code = call_with_payload(process, rays[name], payload)
            if code:
                raise RuntimeError(f'{name}: {code}')
            if stop:
                started.insert(0, stop)
        invoke('SpidyRayStart', struct.pack('<4IQ2I', 0x53525943, 1, 32, game.pid, game.base, 30000, 0x410),
               'SpidyRayStop')
        kinds = 1 << 1 | 1 << 2  # props and bots
        invoke('SpidySwingStart', struct.pack('<4I4QI2fI', 0x53574346, 1, 64, game.pid, game.base, seen['hero'],
                                              mover, motion_module, 30000, 32., 6., kinds), 'SpidySwingStop')
        invoke('SpidyShooterStart', struct.pack('<4IQ', 0x53484f43, 1, 24, game.pid, game.base), 'SpidyShooterStop')
        invoke('SpidyPunchStart', struct.pack('<4IQ', 0x53505543, 1, 24, game.pid, game.base), 'SpidyPunchStop')
        serial = [time.perf_counter_ns() // 1000000]
        hand = [feet[0], feet[1]+1.2, feet[2]]

        def submit(position, direction, relative=(0, 0, 0), trigger=0., grip=0.):
            serial[0] += 1
            invoke('SpidySwingSubmit', hand_command(serial[0], position, direction, tuple(relative), trigger, grip))

        def idle(seconds, rows=None, bot=None, start=None):
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                submit(hand, (0, 0, -1))
                if rows is not None:
                    rows.append(dict(t=round(time.monotonic()-start, 3), **world.state(bot)))
                time.sleep(1/60)
        # Idle input for a moment: the swing's callback runs, and the shooter learns the player's record.
        idle(.4)
        # The web grab's own scan in the game (props and bots) against the same rules read from outside.
        grab = grab_snapshot(game, rays['SpidyGrabData'])
        outside = world.scan()
        report['candidates'] = dict(inside=grab['candidates'] if grab else None,
                                    outside=outside['throwables']+len(outside['bots']))
        print('candidates', report['candidates'], flush=True)
        for phase in phases:
            bot = pick()
            start = time.monotonic()
            rows = [dict(t=0., **world.state(bot))]
            if phase == 'shoot':
                before = shooter_snapshot(game, rays['SpidyShooterData'])
                codes = []
                for _ in range(args.shots):
                    at = world.state(bot)['position'] or bot['position']
                    chest = [at[0], at[1]+height(bot, CHEST), at[2]]
                    away = norm([hand[0]-chest[0], 0, hand[2]-chest[2]])
                    origin = [chest[0]+away[0]*6, chest[1]+.3, chest[2]+away[2]*6]
                    codes.append(call_with_payload(process, rays['SpidyShooterTest'], struct.pack(
                        '<4I3f3fQ', 0x53484f54, 1, 48, 1, *origin, *chest, bot['record'])))
                    idle(args.every, rows, bot, start)
                idle(3, rows, bot, start)
                after = shooter_snapshot(game, rays['SpidyShooterData'])
                result = dict(codes=codes, fired=after['fired']-before['fired'],
                              collisions=after['collisions']-before['collisions'],
                              webbed=after['webbed']-before['webbed'], last_hit=after['last_hit'],
                              hit_target=int(after['last_hit'], 16) == bot['record'])
            elif phase == 'punch':
                before = punch_snapshot(game, rays['SpidyPunchData'])
                at = world.state(bot)['position'] or bot['position']
                chest = [at[0], at[1]+height(bot, FIST), at[2]]
                direction = norm(sub(chest, hand))
                begin, end = sub(chest, scale(direction, .9)), add(chest, scale(direction, .1))

                def fist(position, frames):
                    for _ in range(frames):
                        # Relative to the player as the arm moves it: the fist's own travel.
                        submit(position, direction, add([.25, -.35, -.3], sub(position, begin)))
                        rows.append(dict(t=round(time.monotonic()-start, 3), **world.state(bot)))
                        time.sleep(1/90)
                fist(begin, 30)
                steps = int(round(math.dist(begin, end)/6*90))
                for k in range(steps+1):
                    fist(add(begin, scale(sub(end, begin), k/steps)), 1)
                fist(end, 30)
                idle(2, rows, bot, start)
                after = punch_snapshot(game, rays['SpidyPunchData'])
                result = dict(punches=after['punches']-before['punches'],
                              issued=after['issued']-before['issued'], dropped=after['dropped']-before['dropped'],
                              target=after['hands'][0]['last_target'],
                              hit_target=int(after['hands'][0]['last_target'], 16) == bot['record'],
                              damage=after['last_damage'], speed=after['last_speed'])
            elif phase == 'pull':
                at = world.state(bot)['position'] or bot['position']
                centre = [at[0], at[1]+height(bot, CENTRE), at[2]]
                direction = norm(sub(centre, hand))
                flat = math.hypot(direction[0], direction[2]) or 1
                forward = [direction[0]/flat, .35, direction[2]/flat]
                step, until, since = 'arm', start+.4, start
                position, relative, grip = list(hand), [0, 0, 0], 0.
                shots = {}
                grabs = []
                while time.monotonic()-start < 18+args.watch:
                    now = time.monotonic()
                    g = grab_snapshot(game, rays['SpidyGrabData'])
                    held = g['hands'][0]['phase'] if g else 'none'
                    grabs.append(dict(t=round(now-start, 3), step=step, phase=held,
                                      target=g['hands'][0]['target'] if g else None))
                    rows.append(dict(t=round(now-start, 3), **world.state(bot)))
                    if step == 'arm' and now >= until:
                        step, until, grip = 'grab', now+1, 1.
                    elif step == 'grab' and (held != 'none' or now >= until):
                        if held == 'none':
                            print('pull: nothing caught', flush=True)
                            break
                        shots['caught'] = shot(game, 'combat-caught')
                        step, until, since = 'yank', now+8, now
                    elif step == 'yank':
                        k = min(1, (now-since)/.1)
                        relative = [-direction[i]*.3*k for i in range(3)]
                        if held == 'held' or now >= until:
                            shots['held'] = shot(game, 'combat-held')
                            step, until, since = 'carry', now+2.5, now
                    elif step == 'carry':
                        angle = (now-since)/1.25*2*math.pi
                        position = [hand[0]+.3*math.cos(angle), hand[1]+.3*math.sin(angle), hand[2]]
                        if now >= until:
                            back = [hand[0]-forward[0]*.4, hand[1]-.4, hand[2]-forward[2]*.4]
                            step, until, since = 'windup', now+.7, now
                            windup = (list(position), back)
                    elif step == 'windup':
                        k = min(1, (now-since)/.3)
                        k = k*k*(3-2*k)
                        position = [windup[0][i]+(windup[1][i]-windup[0][i])*k for i in range(3)]
                        if now >= until:
                            step, until, since = 'throw', now+.25, now
                            thrown_from = list(position)
                    elif step == 'throw':
                        k = min(1, (now-since)/.25)
                        k = k*k*(3-2*k)
                        to = [hand[0]+forward[0]*.6, hand[1]+.5, hand[2]+forward[2]*.6]
                        position = [thrown_from[i]+(to[i]-thrown_from[i])*k for i in range(3)]
                        if now >= until:
                            grip = 0.
                            step, until = 'watch', now+args.watch
                    elif step == 'watch':
                        if 'flight' not in shots and now-(until-args.watch) > .4:
                            shots['flight'] = shot(game, 'combat-flight')
                        if now >= until:
                            break
                    submit(position, direction, relative, 0., grip)
                    time.sleep(1/90)
                final = grab_snapshot(game, rays['SpidyGrabData'])
                path = [row['position'] for row in rows if row['position']]
                result = dict(final={k: final[k] for k in ('grabs', 'yanks', 'catches', 'throws', 'releases', 'lost',
                                                           'flings', 'steers', 'refused', 'launches', 'flown',
                                                           'impacts', 'struck', 'drive_failures')},
                              caught=next((g['target'] for g in grabs if g['phase'] != 'none'), None),
                              steps=grabs[::10], shots=shots,
                              moved=round(math.dist(path[0], path[-1]), 2) if len(path) > 1 else None,
                              lowest=min(p[1] for p in path) if path else None,
                              highest=max(p[1] for p in path) if path else None)
                result['hit_target'] = result['caught'] is not None and int(result['caught'], 16) == bot['record']
            else:
                raise SystemExit(f'Unknown phase {phase}')
            result.update(target=describe(bot), states=states_seen(rows), hp=[rows[0]['hp'], rows[-1]['hp']],
                          webbing=[rows[0]['webbing'], max((r['webbing'] for r in rows if r['webbing']),
                                                          default=None)],
                          rows=rows[::5], screenshot=shot(game, f'combat-{phase}'))
            report['phases'][phase] = result
            print(phase, json.dumps({k: v for k, v in result.items() if k not in ('rows', 'steps', 'shots')}),
                  flush=True)
    finally:
        for name in started:
            try:
                report.setdefault('stops', {})[name] = call_remote(process, rays[name])
            except OSError as error:
                report.setdefault('stops', {})[name] = str(error)
        close(process)
        report['restored'] = all(game.read(game.base+rva, 16) == pe.bytes(rva, 16) for rva in ENTRIES)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=1, default=str)+'\n')
        print('restored', report['restored'], 'stops', report.get('stops'), 'report', args.output, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
