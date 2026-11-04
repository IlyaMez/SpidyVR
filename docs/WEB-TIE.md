# Web tie: sticking a webbed-up enemy to a wall

An enemy webbed up can be stuck by a web to a wall, the floor, or anything a
web holds on (a lamp post, a crane, a crate), and he stays there until the
fight is over.

## Controls

**WEB TIE** switches it on: the SPIDY VR tab's EXPERIMENTAL section, or the
launcher's Webs card. It is off by default, and off none of this runs: no
gesture, no tick, and an enemy on a web comes to you as before. On, a webbed
enemy comes to you only when you yank him (WEB-GRAB.md).

1. **Web him up.** Hit him with the web shooter's balls (the trigger of a
   hand without a web) until the game has him in his cocoon: three quick hits
   for a street thug.
2. **One web on the surface, one on him.** Squeeze one grip aimed at the wall
   (or the floor, a lamp post...): a web sticks there. Squeeze the other grip
   aimed at him: the web catches him (keep the grip squeezed; don't pull).
3. **Hands together, click the right stick.** Bring both hands together
   (within about 30 cm). Both controllers tick when the tie is ready. Click
   the right thumbstick (up to 0.4 s after the hands part still counts).

Both webs leave your hands and become one: from where the first web stuck,
it draws him over to the surface (up to 16 m/s, slowing as he nears it, so he
lands on it rather than striking it) and holds him there, his centre his
radius plus 5 cm off the surface, whichever side faces it: his back or his
belly. A strong pulse in both hands says the tie is made.

## How long he stays

Until the fight is over: no enemy who is not tied has been in the fight within
60 m of the player for 5 s. A tie also lets go when:

- he is knocked out (punch him on the wall: the game takes him back),
- a hand's web catches him again (the grab takes over),
- he is gone (the game removed him), or the web grab is switched off or stops.

A pause or a menu keeps the ties; he is held again once play goes on.

## How it works

The core is engine independent (`include/spidy/web_tie.hpp`,
`src/web_tie.cpp`): `TieGesture` decides when the gesture is made,
`WebTies` holds the tied targets, one `TargetCommand` per tie each physics
step: a `Follow` law toward his place that cancels gravity, so he neither
sags nor slams (`TieConfig`: `handsNear` 0.3 m, `grace` 0.4 s,
`approachSpeed` 10 m/s, `approachGain` 3 /s, `gap` 0.05 m, `webForce`
6000 N, `fightOverSeconds` 5 s, `maxTies` 8).

In the game:

- **Webbed up?** `game_targets::cocooned`: his state machine's layer-0 state
  is `BotStateWebStruggle` (or a class derived from it), by RTTI name like
  the other state rules. Fallback, `game_shooter::webbedUp`: Spidy's own web
  balls brought his webbing to 30 (11 a hit, wearing off at 4 a second) within
  the last 12 s.
- **The click** is the right thumbstick alone, in VR play with WEB TIE on,
  with the pad layer off and no R3 the game asks for: `Command::asks` bit 1
  (no layout change). WEB TIE reaches the swing in its settings, version 23
  (`game_swing::Settings::webTie`); off, the gesture is not looked for.
- **The webs handed over:** `Swing::handOver` (no SOFT RELEASE pull) and
  `WebGrab::handOver` (no throw, no trailing web); both grips shoot nothing
  until let go.
- **Held** (`game_grab.cpp`, `hold`): in the game's own flight
  (BotStateFlung), as the web pulls a thug: a kinetic blow of 0.25 knocks him
  into it, and the tie's law steers it to his place and holds it there; when
  the game ends the flight he is knocked into it again, 0.8 s after the last
  blow. Until October 31 he was held on his mover: in that day's 09:44
  session three ties were steered every physics step, and the user: "it
  pulls very slowly", "it does not stick to walls" (a mover keeps a bot on the
  ground). A bot the game will not fling is still steered on his mover.
- **The way there:** an arc, not along the ground: far off the tie aims 15%
  of the distance left above his place (at most 1.5 m), coming down onto it
  over the last 2 m; the first pull starts him at 55% of the speed it wants
  (a yank), the tie speeds him up from there.
- **Stuck at once where he arrives** (a wall, a ceiling, anything): in
  November 2's session (08:13) 24 of 29 ties ended as their thug came to the
  wall and the game took him down; he stuck only once the player's web balls
  finished him, a little late. Now as he comes within 1.2 m of his place (or
  0.12 s of it at his speed, so the webbing lands as he touches), or the game
  takes him down there, the tie webs him as a web ball does (kWebImpact,
  0.1 damage) with his whole cocoon in one hit (40 of kWebEncase; a street
  thug's threshold is 30), and again every 0.25 s (6 at most) until the game
  sticks him (`BotStateWallStuck`, `game_targets::wallStuck`): the tie is
  done. Nothing webs him on the way (a first build that day did on any
  touch: not wanted). Down without being stuck 3 s after he arrived, he is
  the game's (knocked out). Once arrived, no more blows near his place (that
  day one tie knocked its thug 14 times at a place it could not reach: he
  twitched); his mover holds him there. Let go, he is never handed to a
  flight that would pull him off.
- **Ceilings and cranes:** the game has wall and ground sticks
  (`BotWallStuckEvent`, `BotGroundStuckEvent`) and nothing for a ceiling. In
  November 2's sessions (08:36, 08:58) walls stuck every time, but thugs tied
  under ceilings and cranes stopped 1.0 m under them, half a metre short of
  a place their radius put off it, were webbed 6 times (twitching at each
  hit) and hung pulled at a place they never reached. Now under a surface
  facing down his place is 1 m below it (his head touches it; between that
  and his radius as the surface tilts), one hit cocoons him there, and the
  tie holds him against it itself. Anywhere, once arrived, he is settled
  where he came to as soon as he comes no nearer for 0.2 s (by 3 cm): his
  place was a guess of how far his body keeps him off.
- **Second ceiling build (November 2):** "the ceiling still practically
  does not work, it sometimes sticks; a crane cannot be tied to". Ceilings
  stuck only now and then in that day's sessions, when a web hit came as he
  was pressed close: the game sticks a webbed thug a web hits near a surface,
  and from his centre a ceiling is a metre off, a wall half that. Up there the
  tie's webbing now hits him where his head meets it (0.85 m from his centre,
  along the line to the anchor), every 0.5 s, 10 times at most, until the
  game sticks him. Cranes: arcade webs (`SwingConfig::arcade`) go through
  thin things to what is behind them or into the air, so a web aimed at a
  crane's beam held on the building behind it or nowhere, and a web into the
  air never armed the tie. The tie's anchor is now the first thing the web's
  line meets from the wrist as the game's rays see it (thin things too,
  0.8 m out at least), with its normal; a web whose line meets nothing holds
  where it stuck, unless that is open air.
- **The arc** fades out under a ceiling (none for a surface facing down), and
  he slows only in the last 2 m (approach gain 8).
- **The surface's normal:** a ray from the wrist that shot the wall web to
  its end; with no surface there (a thin thing), toward the enemy.
- **The web** is the game's own rope (`native_webs::submitTies`): the hero
  rope manager starts a rope at one of its two hand points, so a tie's rope
  is made on a hand whose own web is not drawn, and that hand's points are
  the tie's anchor for the update (restored after it). Two ties at most are
  drawn so; while both hands have webs of their own (or for a third tie) the
  overlay draws a thin strand instead. Until October 31 the overlay drew
  every tie, with the swing strand: "a thick white line".
- **Seen:** `SpidyTieSample` / `SpidyTieData` (`game_grab::TieData`, version
  3: ties stuck by the game, the tie's webbing hits, and per tie whether he
  touched and is down): the ties' webs, their distance to their place and whether they fly,
  ties made and how they ended (fight over, taken, gone, down), the knocks,
  and whether the gesture is ready (the tick). The session report keeps
  them as `tie_samples`.

## Open

- Played once (October 31, the mover build: slow, not on the wall). The flight build and the game's rope are not yet played. To check there: that a cocooned
  thug's layer-0 state reads as `BotStateWebStruggle` (else only the
  fallback applies, for Spidy's own balls), and how the game's AI takes a
  thug held on a wall once his struggle ends.
- The game's own wall stick, `BotStateWallStuck`, would count him as down;
  Spidy cannot request it yet. While tied he still counts as up, so a fight
  the game ends only when he is down may wait for him: the tie lets go once
  the others are gone, and he can be finished then.
- Facing: he is not turned; he sticks with whichever side faces the surface.

## Validation

`spidy_tests.exe` has 10 checks for this: the gesture ties on the click with
the hands together, either hand holding the wall; no tie for an enemy not
webbed up, hands apart, a prop, a web in the air or an untracked hand; the
click's grace; the place off the surface; a thug drawn 8 m onto a wall no
faster than 10 m/s, arriving and then held still for 60 s without sagging;
the end of the fight letting every tie go and a short lull not; a gone
target or one taken again by a web ending its tie; tying again moves the
tie; configuration checks; and the grab's hand-over.

## Research: a real stick under ceilings (November 2)

The 09:14 and 09:25 sessions settled it: every tie to a wall ended with the
game sticking its thug, and no tie under a ceiling or a roof beam ever did -
each arrived 2-4 cm from its place and took 10 webbing hits, and the game
left it hanging in its flight. The game sticks to walls and the ground
(`BotWallStuckEvent`, `BotGroundStuckEvent`) and has nothing for a ceiling;
Spidy can request only the flight (`RequestState` with the flung state's
type and parameters, `native_bodies::fling`).

So the state log (`native_bodies::StateLog`, export `SpidyStateLog`, kept in
the session report as `state_log`): `RequestState` (20e51c0) is hooked from
a session's first tie on (`native_bodies::logStates`: a player who ties
nobody never has the hook), while bodies run, and each request the game
makes for a tied bot is kept - the
type it asks for (image offset and its class's RTTI name), the caller, the
result and the parameters' first 0x180 bytes - with a mark of what the tie
knew (where its web stuck, the surface's normal, where he was) when the game
stuck him, or when one under a ceiling arrived and was not stuck. From a
session with ties to walls the request that sticks a thug can be read off:
its type, and where the wall's point and normal sit in its parameters. The
next step is to make that request under a ceiling, the normal facing down.
The hook is optional: if it cannot be made, nothing else changes, and the
log's status says so (0 no tie yet, 1 hooked, 3 not).

### What the state log showed (09:44), and the stick Spidy asks

Six ties stuck by the game, each with one request of the type at image
offset 0x6c6dc90 (its vtable 0x3833948) right before: the game's stick.
Its parameters are 0x68 bytes: a header (0x9349bd43, 0x68), zeros to 0x30,
the surface's hknp body at +0x30 (the id Spidy's rays return), the point on
it at +0x34, its normal at +0x40 (out of it), a direction along it at +0x4c
(up a wall), ff ff 01 01 at +0x58, a pose at +0x5c (5 a tilted wall, 6 a
wall, 7 under a beam), an index at +0x60 (9, 42, 61) and 1.5 at +0x64; what
follows was the caller's stack. One of the six was stuck under a beam, its
normal straight down: the game can hold a thug under a surface, it only
never asks it under a ceiling. Its flight's type is at 0x6c6d810, as
`native_bodies::fling` makes it.

So the tie now asks the stick itself (`native_bodies::stick`, made at the
next physics step on the game's thread, as flights are): under a surface
facing down 0.15 s after he arrived, elsewhere after 1 s if the game has not
stuck him (it nearly always has), again every 0.6 s, 3 times at most, never
on a floor. The surface, its point and normal come from a ray from his
centre toward where the web stuck; the direction along it is up a wall, and
under a ceiling away from the player. The type must be the one seen (its
vtable checked): the game makes it the first time it sticks anyone, so
until then the ask is refused and counted (`stick_refused` in the report's
`state_log`). Under a ceiling the webbing is two hits now. The log keeps
Spidy's own asks too (marks with `stuck` 2 and the body the ray met), so
the next session shows whether the game took them.

### Defeated where they hang (November 2, 12:35)

That session the stick held under ceilings and cranes too: 26 of 30 ties
stuck, none of Spidy's asks refused. But stuck is only a state of the body;
a stuck thug keeps his hit points, and the game and its scanner still count
him in the fight ("the sensor still lights them red"). So 0.35 s after one
is stuck (by the game or by Spidy) he takes a blow of all his hit points
plus 5 (kWebImpact, knockback kNone, no impulse, no status): defeated the
game's own way, its events and counts with it. A second after the blow, if
it took him off his surface, the stick is asked again (twice at most). The
state log's marks say how it went: 3 the blow (his hit points before it in
the normal's x), 4 still stuck a second later, 5 not (asked again), 6 down
and stuck at the end. Not yet seen in the game: whether a thug the game
counts as defeated stays on his surface.

### Tying a thug in the air (November 2, 13:08)

That session every stuck thug was finished and stayed where he hung (marks
3, 4 and 6 for all 14). But of 20 yanks none ended in a tie: "you cannot
tie one in the air; you should be able to pull one and tie him to a wall".
A yank flings the thug out of his cocoon's state into the game's flight, so
he no longer read as webbed up, and as he comes to the hand the yank's grab
ends, leaving no web on him for the gesture. Now a thug seen webbed up stays
so for 15 s while he is up (`game_grab::cocooned`), and a hand's tie may take
the thug its web held within the last 2 s (`game_grab::heldLately`): yank
him, bring the hands together on a web on a wall, click, and the tie steers
his flight there.

### No wait before the stick (November 2, 13:30)

"It sticks, but late" (with a video at a crane). Where the game did not
stick him by itself, he came to his place 2-3 cm off it and hung there about
a second: Spidy waited 1 s off a surface not facing down for the game to do
it first. Now Spidy asks the stick the moment he is at his surface (within
0.4 m of his place, or settled against it), 0.3 s after he arrived at the
latest, on every surface but a floor.

### Tied in the air, stuck at once (November 2, 13:46)

"Only those pulled in the air stick late" (with a video). A yanked thug is
out of his cocoon's state, in the game's flight, his webbing worn off; tied
so, he arrived at his wall, took the tie's first webbing hit at his touch,
and stuck 0.5-0.9 s after arriving, while ties off the floor stuck within
0.1 s. Now one tied out of his cocoon's state is webbed at once on his way,
and the stick is asked every 0.2 s (not 0.6 s), a ray that met nothing tried
again at the next step. The state log's marks carry their time (`ms`) now.
