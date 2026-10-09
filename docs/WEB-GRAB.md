# Web grab: webbing props and NPCs and throwing them

A web that hits a prop or a thug, instead of a building, catches it. You can
tow it, reel it in, yank it over to your hand, carry it, swing it around and
throw it. This is the VR counterpart of the game's own web yank and web throw,
driven by your arm instead of by an animation. The web is a rope and what it
holds keeps its weight: it hangs from the hand, swings, lags when heavy, and
flies off with what the swing gave it.

The system has an engine-independent core (`include/spidy/web_grab.hpp`,
`src/web_grab.cpp`), a playable version in the VR lab (`src/lab_props.cpp`,
`apps/xr_lab.cpp`), and a game adapter (`src/game_grab.cpp`,
`src/native_bodies.cpp`, `src/game_targets.cpp`). In the game it works on
throwable props, measured without a headset; thugs fly the game's own flung
reaction, steered by the web, and what flies hurts what it strikes (the
game's side measured, Spidy's pulls on thugs not yet seen); pedestrians are
not offered (see [In the game](#in-the-game)).

## Controls

The controls are the swing controls. What the web hits decides what they do.

| Control | On a prop or thug |
|---|---|
| Aim a hand at it | In the lab a yellow marker shows what a squeeze would catch |
| Squeeze the grip | Shoot the web; it catches the target with 0.3 m of slack |
| Keep the grip squeezed | Keep the web. Moving the hand away tows the target behind it, as hard as the web can pull it |
| Pull the trigger | Reel it in at up to 12 m/s; near the hand it is caught and hangs from it |
| Pull the hand back sharply (the zip gesture) | Yank it: it is jerked onto an arc to your hand at 7-15 m/s by how hard you pulled (slower when heavy), lobbed over what stands in the way; the web reels in behind it and pulls it the rest of the way where the arc falls short |
| Move and swing your arm | The caught target hangs below your hand on a short web (its radius plus 0.25 m), swings as the hand moves and settles when it stops. Turning the wrist does not swing it |
| Release the grip | Holding it: throw it with the speed the swing gave it, times up to 1.6 for a light target, up to 25 m/s, aimed into a thug within 12 degrees. Towing or yanking it: let go; it keeps its motion. The web goes with it as it dissolves |
| Catch it with both hands | It hangs between them; letting go of both throws it once, of one leaves it in the other |

The controller hums while a web pulls, by how hard: a hanging trash can
faintly, one swung hard, or too heavy to lift, strongly.

A press aimed at nothing grabbable shoots a swing web as before, and a press
that catches something never also shoots a swing web. As with swinging, a grip
still held after its web is lost does nothing until it is released. In the game
`Launch Spidy VR.cmd -NoWebGrab` turns grabbing off.

## How it works

Every web is a rope from the wrist: it only pulls, and the target keeps its
weight. One web pulls with at most 2400 N, so it changes the target's
velocity by at most 2400 N over its mass per second: 80 m/s² for a 30 kg
trash can, 30 m/s² for an 80 kg thug, 8 m/s² for the lab's 300 kg dumpster,
which one web can drag but not lift (two can). Taut, a web takes up its
stretch at 30 per second, so it gives a little like elastic, and it steadies
the target's spin at 20 rad/s². Gravity always acts.

Each hand's web is in one of four states:

- **Tethered.** A web 0.3 m longer than the distance when it caught. Slack, it
  does nothing: a webbed thug stays standing until you pull. The trigger reels
  it in at 12 m/s, winding no more than 0.5 m ahead of a target too heavy to
  follow (the reel slips instead of snapping the web), and slows its swing
  across the web at 4 per second so it comes in along the web. A towed prop
  drags, tips and bounces along the ground as the game's physics has it.
- **Yanked.** The target is jerked onto a ballistic arc to the hand's hold
  point (its hold length along the hand's aim, carried on at the player's own
  velocity: not at the pulling hand's, which put the point metres away). The
  arc takes as long as a straight flight at the yank's speed would: the pull's
  speed times 5, 7-15 m/s, times 60 kg over the mass for a heavier target.
  Where its underside would strike the fixed world on the way (a railing, a
  parapet, a car), it is tried 1.35, 1.8 and 2.4 times as long, each higher,
  up to 2.5 s. The jerk is never faster than 1.5 times the yank's speed: an
  arc that needs more (up to a perch) falls short. It flies tumbling end over
  end at 2.5 rad/s, its near side first. In flight the web reels in what the
  flight gives it and more at 0.8 times the yank's speed, so it pulls only a
  target that falls behind, and then along the web: what the target strikes
  stops it as it would stop anything thrown. A target that comes no closer
  for 0.5 s has stopped short: it stays where physics left it, on a web of its
  current length.
- **Caught.** A target reeled or yanked in is braked, as a hand catches it,
  at up to 90 m/s² (or the web's strength), so it reaches the edge of reach,
  0.5 m beyond its hold length, at 0.75 m/s instead of flying past the hand;
  the web pays out while it brakes, never pulling against the catch. Within
  that reach and no faster than 1.5 m/s, it is caught.
- **Held.** The target hangs from the wrist on a web of its radius plus
  0.25 m (0.7 m for a trash can), taken in gently where it was caught farther
  out. It hangs below the hand wherever the hand points, swings when the hand
  moves (the swing across the web slows at 0.8 per second) and lags the hand
  when heavy. Turning the wrist alone does not move it.
- **Let go.** Held, this is a throw: the target's velocity relative to the
  player, as the arm swung it, times 1.6 for a target up to 60 kg and less for
  a heavier one (1 + 0.6 × 60 kg / mass), capped at 25 m/s, plus the player's
  own velocity, so a crate carried along at swing speed is not flung at 1.6
  times that. It keeps its own spin. A throw of at least 6 m/s whose direction
  passes within 12 degrees of a character is replaced by the flatter of the
  two arcs at the same speed that reach it. Towed or yanked, the web just goes
  and the target keeps its motion: a yank let go in flight flies on where it
  was going. One hand letting go of what both hold leaves it in the other.
  The web let go of stays on its target as it dissolves (in the game, for
  1.5 s or until it is gone).

A web is lost when its target disappears, a wall stays between hand and target
for 0.3 s, it is stretched 6 m past its length, or tracking or focus is lost.
A target's own body is never the wall: `TargetQueries::owner` names the target
a struck surface belongs to.

Each physics step the core hands the adapter one command per target the webs
act on. A command is a law, not a velocity (`TargetCommand`, evaluated with
`advance()`): a tension-only rope from one or two hands, a brake toward a
velocity (the catch), or a launch that sets its velocity and spin once.
Whoever simulates the target evaluates the law against the target's actual
state, after whatever the step before did to it: the lab in its own step, the
game on its physics thread. A wall, the ground or another prop stays in the
target's motion; the web adds its pull on top. Targets without a command move
freely. Hand samples (72-120 Hz) arrive at a different rate from physics
steps (30-60 Hz in VR, 120-240 Hz in the lab and headless): a web leaves the
latest sample, carried on at the hand's velocity smoothed over 8 ms. The web's
stiffness and damping are rates, so a rope behaves alike at 30 steps a second
and at 360.

Each step also gives each hand's `tension`: how hard its web pulled, as a
share of its strength. The game hums the controller by it between event
pulses (0.6 times the tension, from 0.05), as does the lab.

### Choices made from measurement

- **A rope, not a hold point.** Until October 6 a held target followed a point
  ahead of the hand, held up and turned with the wrist: "it feels like objects
  have no weight while webbed ... the web is a stick that holds the object (i
  can hold a bin in the air with a web)". In the 10:16 session of October 6 a
  held prop swept at 15-18 m/s as the wrist turned the point, and every throw
  reached the 40 m/s cap. On a rope, in a simulation with ground and friction
  (90 Hz hands, 30 Hz physics): a 30 kg can reeled from 12 m is caught after
  1.2 s and hangs 0.7 m below the hand (tension 0.12); a wrist flick throws it
  at 0.3 m/s; an underhand swing of 1.2 m in 0.25 s (hand peak 7.8 m/s) throws
  it at 13 m/s, an 80 kg body at 5.3 m/s, and a 300 kg one not at all (it
  cannot be lifted).
- **The catch brakes the whole motion, toward the edge of reach.** Wound on to
  the end, a reeled can slid under the hand at 12 m/s and swung up behind it.
  Braked only in its speed toward the wrist, it still slid under: passing below
  the wrist it hardly closes in. Braked toward the hold length itself, which a
  can on the ground cannot come as close to as a hand above it, it slid under
  at 5 m/s. Braked toward the edge of reach, it stops there and is caught.
- **Yanks reel in along the web.** Reeled toward a hand on a perch 32 m above
  and 30 m away, a can whose arc fell short swung under the hand and up past
  it like a pendulum whose rope shortens; with its swing across the web slowed
  while the web pulls, it comes up along the web at 8 m/s and is caught.
- **No extrapolation between hand samples.** Carrying the hand on at its
  measured velocity overshot wherever the hand stopped.
- **A yank gives up on lack of progress, not on time.** A fixed 1.5 s ended a
  43 m yank in the game 8.5 m short of the hand, and the prop flew past it.
- **A law, not a velocity.** Until October 6 a command was the velocity a
  target should have, and the game set it on the prop's bodies every step,
  from the web's own idea of the prop's motion rather than its actual one.
  Once let go, a prop was flown along Spidy's own ballistic path, taking the
  game's velocity back only when they differed by more than 1 m/s. Friction
  (a few tenths of a metre per second per step) never counted, nor did
  rotation. In the 09:22 session a trash can behind a subway railing, yanked
  in a straight line, stopped against the railing four times.
- **Arcs, not straight lines.** An arc lies above its chord, so ground clutter
  that blocks the straight path from a prop to the hand is cleared by an arc
  that bulges enough. Only higher arcs are tried: overhead obstacles in the way
  of a yank are rare.

## Tuning

All of it is `GrabConfig` in `include/spidy/web_grab.hpp`, validated on
construction like `SwingConfig`.

| Setting | Default | Meaning |
|---|---:|---|
| `maxRange`, `aimCone` | 60 m, 0.07 rad | How far a web catches, and how far off the aim a target may be |
| `maxMass` | 400 kg | Heavier targets cannot be caught |
| `holdDistance`, `slack` | 0.25 m, 0.3 m | A held target's web beyond its radius; slack of a new web |
| `webForce`, `tetherResponse`, `webSpin` | 2400 N, 30 /s, 20 rad/s² | One web's strength; how fast a taut web takes up its stretch; how it steadies spin |
| `reelSpeed`, `reelLead`, `reelSteer` | 12 m/s, 0.5 m, 4 /s | Trigger reel; how far it may wind ahead of a heavy target; swing slowed while bringing in |
| `swingDamping` | 0.8 /s | A held target's swing across its web slows |
| `yankSpeed`, `yankDistance`, `yankWindow` | 1.3 m/s, 0.14 m, 0.25 s | The pull gesture (same as the zip) |
| `yankMultiplier`, `minYankFlight`, `maxYankFlight`, `liftMass` | 5, 7, 15 m/s, 60 kg | Yank speed from the pull, scaled down above `liftMass` |
| `maxYankTime`, `yankLaunch`, `yankReel`, `yankTumble` | 2.5 s, 1.5, 0.8, 2.5 rad/s | Longest (highest) arc tried; fastest jerk and reel in flight, times the yank's speed; tumble |
| `arrivalDeceleration`, `catchDistance`, `catchSpeed`, `yankTimeout` | 90 m/s², 0.5 m, 1.5 m/s, 0.5 s | The catch's brake, reach and speed; no progress this long is a stop |
| `throwMultiplier`, `maxThrowSpeed` | 1.6, 25 m/s | Throwing (the multiplier falls toward 1 above `liftMass`) |
| `aimAssistCone`, `aimAssistRange`, `minAssistSpeed` | 0.21 rad, 45 m, 6 m/s | Aimed throws |
| `obstructionTime`, `breakStretch` | 0.3 s, 6 m | When a web is lost |

## In the lab

`Launch Spidy Lab.cmd` now has crates, barrels, a dumpster (300 kg: carried
slower) and thugs: two crates, a barrel and two thugs on the launch rooftop,
more of each on the street below. Webbed and pulled, or hit by something faster
than 4 m/s, a thug falls over and tumbles; a thug that has lain still for
2.5 s gets up where it lies. The console counts throws and knockdowns. **Y** resets the
props with the rooftop.

The lab's props are spheres for physics against the lab's boxes and each other,
with bounce on hard contacts, rolling friction on the ground, and cosmetic spin.

## In the game

The core runs inside the swing's world-query callback (`game_swing.cpp`), with
the same hand input and the game's own rays, but on the game's physics clock
rather than the player's mover, which does not step while the player perches or
stands. It takes every input sample as it comes, so the swing never sees a press
the grab took. Targets come from a thread that keeps a list of candidates from
the component registry (`game_targets.cpp`): a pick only reads transforms. A
target's actor is what its components' record points to: transform at +0,
handle +0x64, physics system +0xe0.

### Props (verified)

Throwable props (`ThrowableHelper` actors, the ones the game's combat lets
you web and throw) rest on static Havok bodies the game places itself; a web
aimed at one used to swing from it. What frees one, all on the main thread
inside `hknpWorld::preCollide` (2e54300), where the world may change
(`native_bodies.cpp`):

1. `PhysicsComponent::SetFreebody` (1810530) and `Physics::SetMode` debris
   (1835780, mode 3): the game's own pick-up-and-throw (6a1620) does these two.
2. A physics rebuild (1830930). A prop built static has no keyframe record, the
   only thing through which the game's PostStep (182d230) draws an actor where
   its body is: freed alone, its bodies flew while the prop stayed put.
3. Three still steps. The game takes the offset between body and instance once,
   at its first sync: moved before then, the prop was drawn 5 m from its body.
4. Then the web's law, every step, evaluated against the root body's actual
   velocity as that step begins, after the contacts of the step before. Every
   body of the prop gets the same change, with
   `hknpWorld::setBodyLinearVelocity` (2e48b80) and, where the law turns it,
   `setBodyAngularVelocity` (2e48830: world axes in; a motion keeps its
   angular velocity in body axes at +0x50 and its orientation at +0x10).

**Physics runs faster than real time.** The game steps Havok once a frame by a
fixed 1/30 s (the length at 609a560, which only its TimeScaleSystem changes,
during its time effects). At 240 frames a second a freed prop fell 32 m in
0.3 s: Havok ran 8 times real time, and at a VR frame rate it runs fps/30
times (1.2-1.6 in the 09:22 session). The web works in real time, so
`native_bodies` measures the ratio every step and keeps the props it moves in
real time: their velocities and spins are the real ones divided by the ratio,
and Havok's gravity for the step is taken out and real gravity for the real
step put in. Restitution and friction are ratios of velocities, so the game's
own contacts stay right in real time: a thrown prop flies, bounces, slides,
tumbles and comes to rest by the game's physics. Spidy keeps doing this for a
prop it let go of until it has lain still for 0.3 s (under 0.3 m/s, turning
under 1 rad/s), the game puts it to sleep, or 20 s pass; from then on the game
has it in its own time again, at rest.

Measured with `tools/probe_game_grab.py` on a throwable prop in the street, 43 m
from the player perched 32 m up at Times Square, no headset (reports `grab-probe.json`, `grab-probe-yank.json`):

| | Reel (trigger) | Yank (gesture) |
|---|---|---|
| Caught after | 4.25 s for 43 m (about 10 m/s) | 1.77 s for 43 m |
| Nearest to the hand | 0.79 m (hold point 1.35 m) | 1.09 m, no fly-past |
| Carried | 0.8-1.6 m from the hand, following a 0.3 m circle | as reel |
| Thrown by a 0.7 m flick in 0.15 s | 19 m/s, 44 m across in 2.4 s | 17 m/s |
| Web lost / instance poses Spidy set | 0 / 0 | 0 / 0 |

The visible prop sits a steady 1 m from the point the web holds, the offset
between its root body's centre of mass and its origin.

The physical build, same perch, yank (`reports/grab-probe-physics-yank.json`,
October 6, the game at 7.4 times real time):

| | Yank |
|---|---|
| Caught after | 1.70 s for 43 m, along an arc: launched at 26 m/s up, slowing under gravity on the way, braking into the hand at the end |
| Carried | 0.3-2.0 m from the hand (0.8-2.5 m before); the hold stopped the prop's tumble |
| Thrown by a 0.7 m flick in 0.15 s | 15.1 m/s; over the next 1.94 s it kept (5.98, -13.6) m/s across, exactly the launch, and fell at 10.0 m/s², real gravity |
| Web lost / hooks restored | 0 / yes |

The prop was freed spinning about 30 rad/s, in both builds: its drawn origin
circled its path every 0.21 s. The yank now launches it with a gentle tumble
instead. Its landing and slide were not yet watched (`--watch` lengthens the
probe's watch after the throw).

Both tables are of the builds that held a prop at a point ahead of the hand.
In the rope build's first headset session (`game-vr-20261006-133652.json`,
October 6, 13:36, physics at 2.5-3.6 times real time) yanks were caught in
0.5-0.6 s and throws left at 10-17 m/s, with no web lost; the user: "it worked
good, only issue is on release the bins fell into the floor (still visible but
partially in it)". See the next two sections.

### The web let go of

The game draws the webs (`native_webs.cpp`, the hero's rope manager). Released
(ReleaseRope 67b610, which 6795b0 marks released), a rope dissolves over a
moment, its hand end falling away, but its far end stays at its target
position: let go of a thrown prop, the web hung in the air where the prop had
been, "as if its still there instead of falling". A released rope's update
still reads its target position (679dc0 calls 6786c0, which returns the
position SetRopeTargetPosition 67d7c0 writes at +67c, into the anchor at
+720), and ReleaseRope keeps the handle when its fourth argument is set. So
for 1.5 s after a hand lets go (`game_grab::trailSeconds`), its grab
telemetry reports the target as `trailing` with its position, the XR worker
asks for that hand's web with `attached` 2, and `native_webs` releases the
rope keeping its handle and aims its far end at the target every update until
the rope has dissolved. A new web from that hand forgets it; it dissolves
where it is. In the game, no headset (`tools/probe_game_grab.py`, which draws
the hand's web as the XR worker does and reads the hero's rope slots): after a
prop was let go of, the released rope's drawn end followed it 15.5 m, a median
0.45 m from it, for the 2 s the rope took to dissolve. In the headset session's
eye snapshots the let-go web lies on the road, leading to the prop.

### Breakable props: only the top piece flies (open)

Some throwables are breakables (`Breakable::BreakableComponent`,
`BreakableSystemComponent` and `Health` beside `ThrowableHelper`) made of two
bodies. Measured on a trash can (`reports/grab-probe-rope*.json`, `--bodies`):
both bodies start at the can's origin; the primary (the can, centre 0.48 m above
its base) and a top piece (centre 0.95 m above the base, about 0.4 m tall). The
game draws the whole can from the top piece, its keyframe record's root, with
no offset. Freed as Spidy frees it, the top piece is a free body; the primary
keeps zero velocity whatever velocity it is given, every step: it stays where
the can stood, held as an unbroken breakable is. So only the top piece ever
flew, and the can was drawn hanging from it. Within 0.1 s of being freed the
top piece fell 0.74 m through the can to the pavement (the two do not collide),
taking the drawn can 0.74 m into the ground; thrown, it came to rest on the
road with its centre 0.16-0.36 m above it and the can drawn 0.8 m below it: the
"bins fell into the floor". The can's own body stays behind, invisible, where
the can stood. Drawing from the primary instead (root index +0x9c set to it
before the first sync) drew the can upright where it stood, and it never moved.
The game's own web yank busts breakables (`BustBreakablesWhileWebYanked`);
freeing a prop the way it does, base included, is the open fix.

### Bots (the game's flight measured; Spidy's pulls on thugs not yet seen)

Until October 8 the first pull asked the game to fling a bot (`BotStateFlung`
through `RequestState`, vtable slot 13, 20e51c0; parameters from 556040 with
the velocity at +0x44) and the web steered it through its own MoverStandard
with leased velocities. A fighting thug's AI overrides that request (it
returns accepted; the thug keeps aiming), so pulled thugs slid along on their
feet: "they get pulled but dont really react to the web". Measured at a
street crime, the game's own answer to a kinetic blow is the flight:

- The first pull on a thug (`ThugBot`) deals him kKinetic 2, kFlyBack,
  KnockbackAmount 10 and ImpactImpulse 30 from the player, through the
  damage system (native_bodies). The game flings him away from the player
  (BotStateFlung, flailing). kMelee and kExplosion knock-backs only stagger
  a thug from 15-20 m.
- While he flies, his driver BotStateFlungLocal (machine +0x98) carries him
  at its +0x94 velocity, colliding him with the world. The web writes its
  pull there every step from the one after the blow: in the game a thug
  steered so went 7 m toward the player and another hung 2.4 m up for 4 s.
- Thrown or let go of, his flight gets the velocity once and the game flies
  him on and lands him (BotStateGroundFlop, then BotStateStunned). Come down
  while still on the web, he lies there until the web may knock him again,
  1.5 s after the last blow.
- A thug not flung 0.25 s after the blow (a heavy, a scripted scene), and a
  civilian, is steered through his mover as before (`SpidyMotionDrive`, rays
  keeping him out of walls and the ground), and let go of, falls under
  Spidy's rays until he is down.

### Strikes

What flies hurts what it strikes, through the game's damage system, as its
own thrown bodies do (web_grab's `StrikeConfig`, decided by `impactLoss`,
`impactBlow`, `strikeBlow` and `touches`; game_grab deals the blows):

| Strike | When | Blow |
|---|---|---|
| A flying thug stopped short | his pace over two steps drops under 45% of the fastest of the three steps before, from 7 m/s, while the web asked no such stop | kKinetic, 2 + 0.7 per m/s lost over 7, up to 15; kStagger, kKnockdown from 14 m/s |
| A thrown thug landing | his flight ends at 8 m/s or faster | as above, by his landing speed |
| A flying thug or a thrown prop touching another thug | 6 m/s or faster; a capsule from his feet to 1.8 m, 0.35 m around (a street thug's; since October 9 another enemy's carried over to his size, `game_targets::Size`) | 2 + 0.7 per m/s over 6, kKnockdown; from 9 m/s kFlyBack with an impulse, knocked away from the flying thug (from the player, for a prop) |

The game doubled requested damage on its normal difficulty (a street thug has
60). A bot is not hurt again within 0.8 s. The two-step pace and the web's
own command keep a frame the game drew late, and a yank's launch, from
reading as a stop.

### Pedestrians (not offered)

The street crowd are 600 pooled `PlacedPedestrian` actors, moved kinematically
by `PedestrianMover` with no physics bodies and no flung or ragdoll state. The
game can promote a pedestrian to a bot (its PedestrianPromoteRegistry); that is
the way to make them catchable, and it is not done yet.

## Validation

`spidy_tests.exe` has 30 checks for this, all passing: a prop press never
reaches the swing; a prop behind a wall swings instead; a web striking a long
prop's near end takes it and its own body never cuts the web; slack does not
pull and a taut web tows; trigger reel and catch, the caught target hanging
below the hand; a can reeled along the ground is caught at the hand instead of
sliding past it (30 Hz physics); a yank arrives without passing the hand and
settles below it, a 45 m yank arrives, a snagged one gives up, its jerk is no
faster than its pull allows and slower for a heavy target, and one from a
perch whose arc falls short is reeled up along the web without swinging past
the hand; a yank arcs, lobbed over a railing the lower arc would strike; a
yank let go in flight flies on, never thrown; the laws themselves (a web pulls
only when taut and never pushes, two webs both pull, a web holds up only what
its strength can and two hold what one cannot, damping slows only the swing
across the web, the catch's brake, spin steadied gradually or set at a
launch); a held target hangs below the hand wherever it points, and a wrist
flick throws nothing; an arm's swing throws, a heavy target slower; a held
target swings alike at 30-90 Hz and settles below the hand; heavy targets lag;
tension reads the target's weight hanging and 1 when the hand snatches it;
throws multiply only the arm's part and are sent once; aimed throws hit the
character (simulated ballistically); a grip held after a lost grab never
swings; walls cut the web after 0.3 s; tracking and focus loss; two-handed
carrying, and one hand letting go leaves it in the other; configuration checks;
and in the lab, a thrown crate knocks a thug over and it gets up, props come to
rest without creeping, and the grab reels a lab crate in until it hangs from
the hand; when a flight struck something and how hard, and what a flying
body touches (two checks). `tests/game_vr_protocol_tests.py` checks the grab
telemetry's layout (version 3: tension and the trailing web per hand; thugs
knocked into a flight, steps steered, impacts, thugs struck). The user tried
the rope build in the headset and found it good but for the sinking bins
(open, see above); the web let go of was measured in the game; the game's
flight for thugs and its steering were measured with a research DLL, Spidy's
own pulls on thugs not yet.
