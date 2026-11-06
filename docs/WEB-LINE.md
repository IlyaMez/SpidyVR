# Web lines

A straight, taut web between two surfaces, as Spider-Man 2's: made, drawn in
the game's own rope, zipped to, walked and jumped on, swung from, enemies
hung under it. **WEB LINES** in the SPIDY VR settings' EXPERIMENTAL section
(`web_lines`, swing settings version 31), off by default: off, nothing of it
runs, and switched off the lines there are go (a player on one with them).
Hanging enemies under a line is a web tie (WEB-TIE.md): it needs WEB TIE on
as well.

## Controls

1. Squeeze one grip aimed at where the line is to start (a wall, a post, a
   crane): a web sticks there.
2. Squeeze the other grip aimed at where it is to end (a wall beside you, a
   lamp post).
3. Bring both hands together (within about 30 cm; both controllers tick
   when it is ready) and click the right thumbstick.

Both webs leave your hands and become the line between where they stuck; a
strong pulse in both hands says it is made. The web tie's gesture with a
surface in the other hand instead of a webbed-up enemy (WEB-TIE.md): with
an enemy on a web, the click ties him; with both webs on surfaces, it makes a
line.

A line is 1.5 to 40 m long. Six at once: a seventh takes the oldest's place.
A line can end on another line; it goes when that line goes (and the lines
on it with it): one end in the air holds nothing (November 3, 18:30: a line
whose line the limit took hung from the air, with a video). The line the
player is on, and the lines it is on, never go to make room, by age or by
distance.
None goes by age; one goes only once the player is 500 m from it, when
the limit takes the oldest, or when the swing stops (a checkpoint's load).
Until November 3 (19:00) lines went after 3 minutes and 150 m away, the
thugs hung from them left in the air.

## The zip to a line, and walking on it

**Aim and zip.** Hold the right thumbstick's click down: after a quarter of a
second a turning ring marks the point of a line nearest where you look
(within about 10 degrees, 2 to 35 m away), with a tick in the right hand. Let
go of the click with the ring in view and you zip there: off the ground by
the game's jump, as a web strike takes off, then flown there (up to 24 m/s,
slowing over the last 2.5 m) onto the line, your feet on it. A short click
with your hands together is still the tie's or a line's gesture, and the
stick pushed up is the game's own zip to its points, as before.

**On the line.** The left stick walks you along it (up to 4 m/s; across it
the stick does nothing), and you stop 0.4 m short of its ends: no fall off.
The jump jumps (6.5 m/s up, carried along the stick); coming down with your
feet within about half a metre of the line you are on it again; jump off to
the side and the line lets you go as soon as you come down with no line
under you, falling at your weight's gravity (the VR settings'), as you do
off a swing. You leave it by shooting a web, by the game's own zip (the
stick up: the line lets go at once, so the game zips you from there), by
zipping to another line, or when the line goes.

November 3, 16:34: "zipping from the line pulls me back onto it, and
jumping off it pulls me to where I zipped" - held on the line, the game
could not begin its zip, and began it once a jump let go - and "off the line
I fall slowly, then fast, by my weight setting": the jump fell at its own
14 m/s^2 down to 0.8 m under the line, then at the weight's.

Until November 3 (16:14) the stick pushed up zipped to a line in view. Held
on, the same push went to the game as its own zip once the line's zip had
begun (its marker gone): the game zipped the player to its point, and the
line's perch pulled him back ("it zips me where it should not, then pulls
me back to the web"). One zip that day flew 14 s: now no zip lasts past 4 s
from its start.

A zip is as the game's web zip is (November 4: a far one "pulled me in
place to the other line, too arcade"): its web goes out from the hand with
none of its own (the right first) to where it holds on the line, in the
game's rope (`LineData::zipTo`, version 5), and stays till he is there; for
its first 0.12 s nothing pulls him; then it does, its top speed 10 m/s and
0.6 a metre more (24 at most), building at 45 m/s^2 over a far one (past
8 m), its path bowing up (0.15 of the distance, 4 m at most, fading as he
nears) and down onto the line, slowing in time to stop on it (40 m in 2.25 s,
1.9 m over the straight line at its height).

In the core (`LineZip`, `lineTarget`): flight, perch, walk, jump and catch.
In the game (`game_swing.cpp`): the aim from the right stick's click held
(`Command::asks` bit 1, as the tie's click) when no gesture takes it; the
marker in `game_lines::LineData` (version 4: the zip's phase, zips,
perches, jumps, landings); the swing flies and holds the body as it does a
web strike's.

## Standing on it (November 4)

On a line the game had the player in its fall state all along: his mover's
contact (+6ee) airborne (2), its air state at 38c9090, falling at 0.3 m/s
held still - its fall's sound and pose on ("the air effect is there the whole
time"; the 12:17 session's motion samples). Writing the contact around the
step did nothing (12:47: airborne at every step): the step itself asks the
mover's classification (its vtable's +0x90, 1fbd460, the mover alone in rcx)
and stores the answer at +6ee after (1fbeea3), with the time on or off the
ground (+6b4) and the landing's and leaving's sounds as it changes - read off
this build's code (12:57, from a dump of it taken for the purpose).

So standing or walking on a line (LineZip perched, the swing driving him;
not once the game's zip is asked from it) the swing asks the motion module
for support (`native_movement::Command::support`), and its hook on that
classification answers supported for the player's mover alone: the game may
take him as standing - its own stance, walk and dodge, its fall's sound off.
The hook goes in only on this build's code (its entry, the mover's vtable
pointing at it). The report keeps `support`: hooked, and the steps answered
supported.

November 4, 13:33: answering supported did take him as standing (contact 0,
grounded, 2360 steps) and dropped him off the line onto the floor. The
answer skips the probe inside the classification, so the step keeps the last
real surface (mover +1a0) and reels the body down onto it at a hard 7 m/s:
the line was asking 6 m/s up, the body went at exactly -7 until Y met that
floor (about 80 down to 67), XZ still on the line. Not a fall of his weight.
Before answering, the hook now plants that surface where he is (+294 copied
onto +1a0, an upward normal at +1b8, the sample marked valid at +238 when it
was not), so the separation the step stores at +6b8 is 0 and there is no
height to reel to. Not yet played.

**Walking on it** (November 4, 14:00: standing held, but "the legs do not
walk"): driven along the line, the body went with no step of the legs - the
game walks its legs by its own walk, which the drive took the place of. So
with the stick along the line the game walks him itself (`LineZip::gameWalk`:
standing on it, within 0.3 m of where the line holds him, not pushed past an
end): nothing driven, the ground kept under him (`Command::support` without
the drive), the headset's stick to the game laid along the line (none across
it), and the line following where he gets to (`LineZip::follow`). Off by
more than that, or at an end, the line holds him again. The headset sends the game
its walk on a line though the swing holds him (`game_xr.cpp`'s lineFoot):
until 19:07 it sent it only to a player the swing did not hold, and on a
line the game walked nowhere ("now I cannot walk at all").

November 4, 19:35 ("walking is not exact: sometimes on air, and it jolts;
after a dodge my feet are in the air"): the game walked him beside the line
within its 0.3 m slack, and past it the line took him back with a jolt; and
the ground the support planted was wherever he was, so a dodge that lifted
him left him standing on air. Now the headset steers the game's walk back to
the line's middle as he walks (his offset across it, 2.5 a metre, 0.6 of the
stick at most), the slack is 0.5 m, and the support's ground is at the
line's height (a version 3 command carries it: `native_movement::
supportHeight`), so the step brings his feet back onto the line. Its height is
his feet's, the actor's position (20:07: sent as his body's middle, it
stood him 0.75 m over the line). After a dodge on the line, till he walks,
jumps or leaves it, the ground was 0.1 m lower: the game's pose after its
dodge stood his feet a little over the line, the actor on it (20:14). That
did not set them down (20:37), so a dodge on a line is now the line's dash
alone: the game gets no B there, and for the dash its stick goes all the way
along the line, the dash's way, so its legs run. The walk's pull back to the
line's middle is 4 a metre, 0.8 of the stick at most (20:37: at 2.5 and 0.6
he still walked off its middle on some lines). The game's web strike (Y)
from a line leaves it as its zip does (it was held on the line: "no web
strike from the line, it bugs").

## On the line: webs, flips, ties, the dodge

The dodge's button (B, `Command::asks` bit 5) on a line dashes you along it
(11 m/s for 0.3 s, the way the stick goes along it, or the look), and the
game's own dodge answers the blows it warns of while you are on a line, as
it does for a player a web carries. A zip on a web (the solver's), or a jump
off the line with a web held, takes you off it: the web swings you
("jumping off the line and pulling myself to a building, the line held
me", November 4).


A web shot from the line does not take you off it: you stand on, the web
held, and swing on it once a jump takes you off (until November 3, 17:04,
any web let go of the line: "I should not sink"). FLIPS do not turn you
while on a line or jumping off it (they spun the player there without end).

**Webs on lines.** Webs hold on lines (`LineWorld`, the swing's world with
the lines in it): a web aimed at a line holds on it when the line is nearer
than what is behind it, and you can swing from one - not a line within 2 m
of the hand, so a web aimed down from the line you stand on goes on past it.
Until November 3 (17:30) webs went through lines.

**Ties to a line.** Web an enemy up, one web on him (on the ground or just
yanked into the air), the other on a line, hands together, the click. He is
carried under the line and hangs there in his cocoon, his centre 1.8 m under
it (`game_grab::lineHang`), upright and facing you, on a strand of web from
the line to his head drawn in the game's rope (`TieData::hangs`); the game's
stick holds him there as on a wall facing you, on the body at one of the
line's ends. Hung below it, he is out of the way of a player walking on the
line (until November 3, 17:30 he was stuck on the line itself, as under a
beam: in the way, and not hanging). When the line goes, he falls: the
game's flight and a light blow down (the flight alone left a stuck,
defeated thug in the air).

Each strand is its own rope in the game's material, with the lines' ropes
ten at most (`native_webs::maxLineRopes`): the hero rope manager has 16
ropes, the hands', their trailing ones and the ties' among them. When they
run short, two neighbours on a line share a rope (laid up the first strand,
along the line to the second's top, down to its head: three points), as many
pairs as it takes; past even that the overlay draws a strand as a thin web.
A thug is hung 1.1 m clear of those already on the line (two hung closer
pushed each other off it, November 4). One the game no longer holds stuck for 0.3 s
has fallen off: his strand goes, and he lies where he falls, finished if he
was not yet (his strand stayed on his falling body, and with several fallen
the strands tangled, November 4, 11:47). The launcher has the switch too,
under Web tie: Web lines (experimental). Laid in its line's own rope instead
(November 3, 19:00: down each strand and up again), the rope took more than
the 8 points a rope is laid and the game crashed, and the line was drawn
twice (November 4).

While on a line only the game's zip (the stick up, `Command::asks` bit 4,
held into the game's aim mode too) takes you off it, and the line holds you
still until the game's zip has you (AirYield; 0.8 s after the last ask at
most): let go at once, the player dropped for the 0.3-0.9 s the game takes
to begin its zip ("I sink for a moment", November 3, 18:04). Only it: the other buttons the game names - a webbed thug's throw
among them, which the grips press as they yank one - did too, and the
player fell off the line as he yanked a thug to tie to it.

**Only the game's rope.** Lines and the strands of thugs hung from them are
only ever drawn in the game's own rope; the overlay draws no stand-in for
them (until November 5 it drew a thin white strand for one the game had not
drawn yet: each new line's first frame or two, the first line's longest).
A strand past the ropes there are is not drawn at all.

## How it works

The core is engine independent (`include/spidy/web_line.hpp`,
`src/web_line.cpp`): `LineGesture` decides when the gesture is made,
`WebLines` keeps the lines and answers where a ray meets one (for swinging
and ties to come). Each web's end is the first thing its line meets from the
wrist, as for the tie (thin things too).

The swing module keeps the lines (`game_swing.cpp`) and publishes them
(`game_lines::LineData`: export `SpidyLineData`, sample `SpidyLineSample`).
The headset's module hands them to the game's rope system
(`native_webs::submitLines`).

**The rope.** The hero rope manager starts a rope at one of two hand points
(Start, Grip, Grip2, End at 7d60.., +12 per hand), and a line lasts while
the player uses both hands. Each line keeps its own rope (by its id). The
update's ropes are laid with their hand's points set for each in turn:
before the update for the first of Spidy's ropes on a hand (by slot, the
order the update lays them), and in the hand points the game asks for each
rope (95f9c0) for the next one on that hand. After each update a line's
rope is checked: its third point must lie within 0.3 m of the line, once it
is 30 updates old (until then, while the game shoots it out, it counts as
drawn: the overlay drew its strand over the shot for half a second, "for a
moment it is a white line"). One drawn from the hand 6 updates in a row is let go and
made again once a hand is free; the overlay draws it meanwhile, as a thin
strand.

The first build (November 3) set a hand's Start in the hand points for the
rope being laid: the game reads it before, and while both hands held webs
the lines' ropes came from the player's hand (with a video: "webs appear in
my hand"); 6345 of 13934 checks were off the line. Its ropes were kept by
their place in the list, so a new line past the limit moved every rope to
another line, and the newest showed the overlay's strand ("the fourth is a
white line").

**Seen:** the session report keeps `line_samples` (lines made and gone, the
gesture's readiness, each line) and `line_ropes` (the game's ropes for them:
made, released, `checks`, `good` and `bad` - the middle on the line or off
it, `shared` - the bad ones while the hand's own web or a tie's was drawn
too, `moves` - Starts set for a rope in the hand points, and how far off the
middle was).

## Validation

`spidy_tests` checks the gesture (a line on the click, none with a target on
a web, hands apart, a web in the air, too short or too long), three lines at
once with the oldest giving way, lines going when old or far, and rays
meeting lines (the nearest first, within the radius, not past an end).
Not yet played in the game.

November 3, 13:57: 31140 checks, all on the line (the worst 3 mm off), none
let go, with both hands holding webs; seven lines made, the first going.
