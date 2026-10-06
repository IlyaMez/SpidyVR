# Web grab: webbing props and NPCs and throwing them

A web that hits a prop or a thug, instead of a building, catches it. You can
tow it, reel it in, yank it over to your hand, carry it, swing it around and
throw it. This is the VR counterpart of the game's own web yank and web throw,
driven by your arm instead of by an animation.

The system has an engine-independent core (`include/spidy/web_grab.hpp`,
`src/web_grab.cpp`), a playable version in the VR lab (`src/lab_props.cpp`,
`apps/xr_lab.cpp`), and a game adapter (`src/game_grab.cpp`,
`src/native_bodies.cpp`, `src/game_targets.cpp`). In the game it works on
throwable props, measured without a headset; bots are built but not yet
verified there, and pedestrians are not offered (see
[In the game](#in-the-game)).

## Controls

The controls are the swing controls. What the web hits decides what they do.

| Control | On a prop or thug |
|---|---|
| Aim a hand at it | In the lab a yellow marker shows what a squeeze would catch |
| Squeeze the grip | Shoot the web; it catches the target with 0.3 m of slack |
| Keep the grip squeezed | Keep the web. Moving the hand away tows the target behind it |
| Pull the trigger | Reel it in at 12 m/s until it hangs at your hand |
| Pull the hand back sharply (the zip gesture) | Yank it: it flies an arc to your hand at 10-24 m/s, by how hard you pulled, lobbed over what stands in the way |
| Move and swing your arm | The caught target hangs 0.9 m beyond your hand, along its aim, follows it and turns with it |
| Release the grip | Holding it: throw it with what your arm gave it, times 2.2, up to 40 m/s, spinning as your hand turned, aimed into a thug within 12 degrees. Towing or yanking it: let go; it keeps its motion |
| Catch it with both hands | It hangs between them; letting go of both throws it once, of one leaves it in the other |

A press aimed at nothing grabbable shoots a swing web as before, and a press
that catches something never also shoots a swing web. As with swinging, a grip
still held after its web is lost does nothing until it is released. In the game
`Launch Spidy VR.cmd -NoWebGrab` turns grabbing off.

## How it works

Each hand's web is in one of four states:

- **Tethered.** A tension-only web from the hand to the target, 0.3 m longer
  than the distance when it caught. Slack, it does nothing: a webbed thug stays
  standing until you pull. Taut, it takes the stretch out of the target's
  velocity, 60% per step, so the web gives a little like elastic, picking the
  target up at no more than 240 m/s². It only ever pulls. The trigger shortens
  it. A towed prop drags, tips and bounces along the ground as the game's
  physics has it.
- **Yanked.** The target is launched on a ballistic arc to the hand's hold
  point (where the hand will be when it arrives, if the hand moves). The arc
  takes as long as a straight flight at the pull's speed times six would.
  Where its underside would strike the fixed world on the way (a railing, a
  parapet, a car), it is tried 1.35, 1.8 and 2.4 times as long, each higher,
  up to 2.5 s. It flies tumbling end over end at 2.5 rad/s, its near side
  first. In flight the web steers it to the moving hand at no more than
  40 m/s², so what it strikes stops it as it would stop anything thrown, and
  near the hand it brakes at 90 m/s² so it arrives with the hand instead of
  past it. A target that comes no closer for 0.5 s has stopped short: it stays
  where physics left it, on a web of its current length.
- **Held.** The target moves with its hold point, 0.9 m plus its radius ahead of
  the hand, and closes the remaining gap at 20 per second, with at most
  420 m/s² of acceleration. Heavier targets get less: the limits scale by
  120 kg over the target's mass, down to 15%. It takes up the hand's turning at
  up to 60 rad/s², so it turns with the wrist and stops tumbling when the hand
  is still.
- **Let go.** Held, this is a throw: the target's velocity relative to the
  player times 2.2, capped at 40 m/s, plus the player's own velocity, so a
  crate carried along at swing speed is not flung at 2.2 times that, spinning
  as the hand turned (up to 25 rad/s). A throw of at least 6 m/s whose
  direction passes within 12 degrees of a character is replaced by the flatter
  of the two arcs at the same speed that reach it. Towed or yanked, the web
  just goes and the target keeps its motion: a yank let go in flight flies on
  where it was going. One hand letting go of what both hold leaves it in the
  other.

A web is lost when its target disappears, a wall stays between hand and target
for 0.3 s, it is stretched 6 m past its length, or tracking or focus is lost.
A target's own body is never the wall: `TargetQueries::owner` names the target
a struck surface belongs to.

Each physics step the core hands the adapter one command per target the webs
act on. A command is a law, not a velocity (`TargetCommand`, evaluated with
`advance()`): a tension-only rope from one or two hands, a follower that steers
the target toward a velocity and a point, or a launch that sets its velocity
and spin once. Whoever simulates the target evaluates the law against the
target's actual state, after whatever the step before did to it: the lab in its
own step, the game on its physics thread. A wall, the ground or another prop
stays in the target's motion; the web adds its pull on top. Targets without a
command move freely. Hand samples (72-120 Hz) arrive slower than physics steps
(120-240 Hz). A held target follows the latest sample, and the hand's velocity
is smoothed over 8 ms.

### Choices made from measurement

- **First-order hold, not a spring.** A critically damped spring to the hold
  point carried a target 12-18 cm past a hand that moved a metre in 0.2 s and
  stopped (simulated at 60-360 Hz, 0-25 ms of smoothing). Moving with the hold
  point and closing the gap first-order overshoots 4-6 cm with 3-5 cm of lag.
- **No extrapolation between hand samples.** Carrying the hold point on at the
  hand's measured velocity overshot wherever the hand stopped.
- **Two hands average their hold points.** Applied one after the other, the
  second hand's follower overrode the first.
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
| `holdDistance`, `slack` | 0.9 m, 0.3 m | Hold point beyond the hand; slack of a new web |
| `reelSpeed` | 12 m/s | Trigger reel |
| `yankSpeed`, `yankDistance`, `yankWindow` | 1.3 m/s, 0.14 m, 0.25 s | The pull gesture (same as the zip) |
| `yankMultiplier`, `minYankFlight`, `maxYankFlight` | 6, 10, 24 m/s | Flight speed from the pull |
| `maxYankTime`, `yankGuidance`, `yankTumble` | 2.5 s, 40 m/s², 2.5 rad/s | Longest (highest) arc tried; steering in flight; tumble |
| `arrivalDeceleration`, `catchDistance`, `yankTimeout` | 90 m/s², 0.5 m, 0.5 s | Braking into the hand; catch; no progress this long is a stop |
| `webAcceleration` | 240 m/s² | Hardest pickup, tow or arrival braking at `liftMass` or below |
| `holdResponse`, `maxHoldAcceleration`, `liftMass`, `holdSpin` | 20 /s, 420 m/s², 120 kg, 60 rad/s² | Carrying |
| `throwMultiplier`, `maxThrowSpeed`, `maxThrowSpin` | 2.2, 40 m/s, 25 rad/s | Throwing |
| `aimAssistCone`, `aimAssistRange`, `minAssistSpeed` | 0.21 rad, 45 m, 6 m/s | Aimed throws |
| `tetherStiffness`, `obstructionTime`, `breakStretch` | 0.6, 0.3 s, 6 m | Web give and when it is lost |

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

### Bots (built, not yet verified in the game)

A bot moves with its own MoverStandard, which its `BotMoverManagerGame` names at
+0xdb4, as the player's manager does. The first pull asks the game to fling it:
`BotStateFlung`, its launched reaction, requested through the bot's
`SyncStaticStateMachine` (`RequestState`, vtable slot 13, 20e51c0; parameters
from 556040 with the velocity at +0x44, state type from 300440). While the web
holds it, Spidy steers it through its mover with leased velocities, as it
steers the player (`SpidyMotionDrive`; a bot standing about has a mover that
does not sweep, so rays keep it out of walls and the ground). When the web stops
steering (thrown, let go, a slack web), the flight takes the bot's velocity
(BotStateFlungLocal +0x94) and the game flies and lands it. A bot the game would
not fling falls under Spidy's rays until it is down.

Measured so far: the bot classes and the mover link in a live game, the
RequestState slot and the entry points offline. No bot came within reach in
this session's free roam (bots spawn with crimes; the one present was 160 m away
and its mover did not step). `tools/probe_game_grab.py --bots` and
`--fling-test` check them when a crime is near.

### Pedestrians (not offered)

The street crowd are 600 pooled `PlacedPedestrian` actors, moved kinematically
by `PedestrianMover` with no physics bodies and no flung or ragdoll state. The
game can promote a pedestrian to a bot (its PedestrianPromoteRegistry); that is
the way to make them catchable, and it is not done yet.

## Validation

`spidy_tests.exe` has 25 checks for this, all passing: a prop press never
reaches the swing; a prop behind a wall swings instead; a web striking a long
prop's near end takes it and its own body never cuts the web; slack does not
pull and a taut web tows; trigger reel and catch; a yank arrives at the hand
without passing it, a 45 m yank arrives, and a snagged one gives up; a yank
arcs, lobbed over a railing the lower arc would strike; a yank let go in flight
flies on, never thrown; the laws themselves (a web pulls only when taut and
never pushes, two webs both pull, a hold holds up, spin is taken up gradually
or at a launch); carrying settles alike at 60-360 Hz and overshoots under 8 cm;
heavy targets lag; a held target turns with the hand and leaves spinning as it
turned; throws multiply only the arm's part and are sent once; aimed throws hit
the character (simulated ballistically); a grip held after a lost grab never
swings; walls cut the web after 0.3 s; tracking and focus loss; two-handed
carrying, and one hand letting go leaves it in the other; configuration checks;
and in the lab, a thrown crate knocks a thug over and it gets up, props come to
rest without creeping, and the grab carries a lab crate end to end.
`tests/game_vr_protocol_tests.py` checks the grab telemetry's layout. The feel in
the headset, and bots in the game, are unconfirmed.
