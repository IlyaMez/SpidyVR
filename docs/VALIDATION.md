# Validation — 2026-10-08

## A pressed in the air is no web zip — current build

The user, October 8: "lets disable the regular og game webshooting mechanic
that happens when pressing a in the air or mid jump"

**Cause.** The controllers' A (`XrFrame::jump`) reached the game as the
virtual Xbox controller's A and the input bridge's Space whenever VR
gameplay steered, in the air too: `swingNativeKeys` passed the jump during
Spidy's owned flight ("manual jump during a web-controlled landing"). In
the air the game's A is its web zip (`HeroStateWebZip`).

**What changed.**
- `AirJumpFilter` (include/spidy/swing_takeoff.hpp) decides at the press:
  a jump key pressed while airborne is withheld until released; one pressed
  on support passes for as long as it is held.
- `game_swing::airborne(Data, nowQpc, frequency)` (include/spidy/game_swing.hpp):
  status 1 or 2, a step within 100 ms (a perch or ledge the game animates
  without steps leaves an old airborne sample), and owned flight or the
  mover not grounded with collision flags bit `0x10`.
- The XR worker (src/game_xr.cpp) filters `motion.nativeKeys` before
  `swingNativeKeys`, so the pad's A and the bridge's Space both follow it.
  Spidy's own jump input (`motion.swing.jump`: takeoff, point launch, both
  on support only) and the takeoff's own jump press are unchanged. No
  protocol change.

**Why that rule.** Contact 2 alone is not the air: the October 8 11:54
session began on a perch at contact 2 (`0x80a0` / `0x2060001`, standing
still), and a wall-run-like stretch ran at contact 2 with `0x2060000`.
Swing samples of the 11 October 6-8 session reports, each matched to the
movement sample of the step it observed (`source_step`), against the game's
air-state handler (0xa7b3a0, `air_events`) in that step: rule air and
handler ran 37,612; neither 23,168; air without the handler 734 (a jump's
first steps at `0x80a0` / `0x2060010`, and owned flight along walls);
handler without air 143 (landing steps at contact 0): 98.6% agree. Perch
(`0x2060001`, 1,434) and wall-crawl steps (`0x2060005`, and `0x8001` /
`0x2060000`, 313) never counted as air, nor ran the handler.

**Checks.** 183 core checks (2 new: the filter's press decisions, a landing
during a withheld press, other keys kept; `airborne` on owned flight, the
game's air state, standing, landing, perch, wall crawl, wall run, an old
sample, a stopped or faulted swing), the 5 Python protocol suites. Not run
in the game: a peer's research session had it open, and the XR worker
needs the headset.

## Steadier aim markers, a colour per hand, a new target ring — preceding build

The user, October 8: "lets make the aim markers nicer (diffirent color shade
per hand maybe) and they feel very jerky\jittery rn can we smooth them a bit
while staying snappy. also the square target marker is ugly as heck"

**Why they jittered.** The marker sat on the raw aim ray of the image's
input sample. A held hand's tremor (about 8-12 Hz) and the controller's
tracking noise swing that ray by about 0.4 degrees peak to peak (a typical
tremor, simulated: 0.1 + 0.05 degree sines at 9.5 and 7.1 Hz plus 0.03
degree noise per frame at 72 Hz), as wide as the ring at 20-25 pixels a
degree, at any distance. The shapes were aliased (no multisampling, opaque),
so their edges crawled by whole pixels as they moved by fractions of one; a
change in the preview's kind swapped the shape at once (at an edge, every
few frames); and a target's centre moved in the game's steps.

**What changed.** `AimMarkerMotion` (src/web_visual.cpp, spidy_core), one per
hand in the XR worker's overlay callback (src/game_xr.cpp):
- `begin(t)` per eye image, `t` its predicted display time: the same image
  again changes nothing; a gap over 0.25 s starts afresh.
- `aim(direction, yaw)` turns the aim direction into the tracking space
  (`Quat::yaw(-trackingYaw)`, so snap and smooth turns are no motion) and
  filters it: a 1 euro filter, cutoff 1 Hz + 60 x max(speed - 0.06 rad/s, 0),
  the speed taken from the step off the last filtered direction and
  low-passed at 4 Hz. Simulated (the tremor above, 5 seeds): at rest 0.40 to
  0.10 degree peak to peak; at a constant 2.3-8.6 deg/s, 0.06-0.10 degree
  behind; sweeps at 0.3, 1 and 3 rad/s at most 0.28, 0.31 and 0.35 behind;
  within 0.05 degree 19-31 ms after stopping. (Without the dead band rest
  kept about twice the wobble; textbook settings, 10 per rad/s and a 1 Hz
  speed low-pass, trailed sweeps by up to 1-1.5 degrees.)
- `markers(wanted, origin, direction, out)`: per kind a weight (new: 0.35 at
  once, +1 per 40 ms; unwanted: -1 per 80 ms), for anchor, air and blocked a
  distance along the steadied ray (eased in log space, 30 ms), for a target
  its centre and radius (eased, 35 ms) and a lock (0 to 1 in 120 ms; the ring
  starts 1.8 times wider, and widens again as it fades). Each kind still
  fading is drawn too, at its weight's opacity, where it was last wanted (a
  fired web's ring fades where the web went, not along the moving hand).
- The placement rules (onAimLine for surfaces, the preview's reach for air,
  the 2 / 3 / 0.5 m hide distances) are unchanged, on the steadied ray. Off
  (X), untracked, or with the hand's web attached, the marker fades out; the
  flat screen resets it.

`appendAimMarker` draws soft-edged shapes (`softArc`, `softBar`,
`softTriangle`: edges fade over 0.8 px), every shadow first (60% dark, 1.3 px
past the shape outside and 0.5 px inside, fading over 1.8 px; a dot's half
that), then the colours. Linear RGB: left (0.13, 0.55, 1), right (1, 0.40,
0.05), miss (0.9, 0.07, 0.05), centre dots 60% toward white. At 100%: anchor
ring 10.5 px radius, 2.3 px wide, dot 2 px (the ring shrinks 40% as the grip
closes); air 8.5 px, eight dashes of 45%, 75% opaque; the cross's arms 5.5
px; the target ring max(1.15 x radius, 13 px), the right hand's 15% wider and
turned 60 degrees, three 75-degree arcs with claws 20% of the radius (4.5-10
px) and a dot, turning 0.8 rad/s plus 2.4 x grip.

Renderer: `Vertex::alpha` (default 1, so every `{position, color}` stays
opaque); the overlay blends (source alpha / inverse source alpha; at 1 the
pixels are as before); `renderViews(..., translucent)` draws a second span
after the first with a pipeline that tests depth but never writes it, so
soft edges and shadows never cut holes in each other. The worker sorts the
markers farthest first.

**Checks.** `spidy_tests`: 181/181. New: "aim marker motion steadies hand
tremor, keeps up with a sweep and ignores turns" (a 0.15-degree 9 Hz tremor
to under half; a 2 rad/s sweep under 0.6 degree behind; a 30-degree snap
turn no motion; the same image no change; a gap restarts; a bad direction
passes) and "... fades markers in and out and eases them to new places".
Reworked: "aim markers keep their size on screen and stand out on any
background" (shadows painted before colours, a quarter of the vertices at
opacity 0, each hand's colour, opacity, the target ring round (its outer edge
in at least 18 of 36 directions), closing in, both hands' rings apart).
`spidy_graphics_test` 1536x1536: both hands' anchors, open air, a miss and two
rings on one target over the dark scene, a bright sky and a lit wall drawn
169 m out: each marker's colour, and on the panels pixels darkened at least
15% by its shadow; D3D12 debug layer clean. ctest (core, launcher) and the
five Python suites (87) pass. The saved `aim-markers.bmp` was looked at.

**Not checked.** The headset: how the steadying feels (the tremor is a
model, not the user's controllers), the colours on the game's own scenes,
the ring's turning. The game was not run: the markers are drawn only in the
XR worker's eye images, through the same renderer the GPU test runs.

## Body calibration in a T-pose — preceding build

The user, October 8: "lets add an in game calibration section on first start
(and as an option in the vr menu) where we ask the players to stand in a t
pose and hold triggers to get correct measurements to scale the avatar."

**What changed.** `src/body_calibration.cpp` (spidy_core, engine
independent): `Calibration` takes the headset and controllers in the
tracking space each frame and, after 1.5 s of a held T-pose (both
controllers tracked, eyes at least 1 m up, each arm within 41 degrees of
straight out to its side from the hero's shoulder at the player's size and at
least 72% of his arm, the two within 12%, the head within 26 degrees of level
and 34 of square to the arms, both triggers past 0.6 then 0.35, head under 0.3
and wrists under 0.35 m/s; a lapse under 0.25 s pauses), measures the eye
height (mean head height) and each arm's reach from that shoulder to the wrist
the solver places for the grip; the arm length is the longer. `Proportions`
(the hero's rest pose: eyes 1.697 m up, shoulder joints 0.111 m behind, 0.266
below and 0.170 to the side of them, arms 0.559 m) come from
`body::Rig::shoulders` (new, from `prepare`) through
`native_body::proportions`, with Spider-Man's numbers until the body has run.
`bodyScale` (0.7-1.3 calibrated) and `armScale` (0.8-1.25) size the body;
`body::solve` takes `armScale` and scales each arm's subtree about its
shoulder after the body's scale. `appendView` draws the panel (1.2 x 0.6 m,
1.4 m ahead, 12 cm under the eyes, where the player faced; it stays in the
tracking space) and a ring at each controller; `src/overlay_text.cpp` is the
stroke font. The XR worker starts it at the first 1.5 s of immersive play
without a calibration (not while Spidy's swing owns the player, and only with
the body on) or when `vr_settings::Values::calibrate` is set by the tab's
CALIBRATE BODY (`Item::calibrate`, a switch named NO / ON RESUME, row 6 under
a BODY heading: 12 rows, 0x200-0x20b), and clears that flag as it starts.
From its start until each is let go afterwards, the swing's triggers and grips
and the jump are zeroed and B is no interact; B skips. Done, the body command
carries the eye height (with the new `calibrated` flag for the wider range)
and the arm length (`Command::armLength`, the old spare word).

**Protocol.** XrConfig version 15, 640 bytes: `eyeHeightMm`, `armLengthMm`
(both 0, or 1000-2500 and 250-1200; else 1001) and options bit 10 (no prompt
at the first gameplay). XrData version 15, 792 bytes: from 752 the
calibration's phase, hint, progress, eye height and arm length (mm), done and
skipped counts, flags (bit 0: no prompt) and each arm's reach. The body's
status is version 2, 144 bytes (`armScale`, `calibrated`). The menu probe's
flags gain 64 (a calibration asked for). Python: `--eye-height`,
`--arm-length`, `--no-calibration-prompt`; samples' `calibration` and
`vr_settings` `eye_height_mm`, `arm_length_mm`, `calibration_prompt`, which the
settings line carries; `body.arm_scale`. Launcher: `SessionOptions`
`eyeHeightMm`, `armLengthMm`, `calibrationPrompt`; launcher.ini
`eye_height_mm`, `arm_length_mm`, `calibration_prompt`; a "Body calibration"
row with Redo / Ask again; "Reset options" keeps the measurements.
`launch-game-vr.ps1 -EyeHeight -ArmLength -NoCalibrationPrompt`.
`probe_game_body.py --phases tpose` drives the body with a T-pose and the arm
length; `probe_menu.py` steps CALIBRATE BODY (14 changes in all).

**Checks.** 179 core checks pass (new: arms 0.65 m from the shoulders
reached only with the arm scale, every bone 1.16 times longer and the hips
unmoved; the test rig's proportions, the scales and their clamps; a T-pose
measured at 134/135 frames, the eye height and 0.6 m arms exact, the longer
of two arms taken; each check's panel line, crossed and hanging arms
included; a 0.2 s lapse pausing and a 0.3 s one restarting, the trigger's
hysteresis; the panel's place and drawing; the font's glyphs for every panel
text, its widths and bars), 13 launcher checks (the calibration round trip,
half or out-of-range values refused, the skip) and 87 Python checks (XrData
v15's calibration, the body's status v2, the settings line). The GPU test
draws the panel holding at 60% and the result over the left eye
(`calibration-panel.bmp`, `calibration-done.bmp`): every line legible at 1536
pixels for 92 degrees, the rings filling clockwise from the top.

In the game without a headset (the user's save, `probe_menu_pad.py start` +
`pad a --until-player`, two fresh games, with the user's OK):

- `tools/probe_menu.py` (player at 28.5 s) passed every step: BODY and
  CALIBRATE BODY between WEIGHT and COMFORT, NO stepped to ON RESUME
  (`calibrate` true, 6 changes), RESET ALL back to NO (14 changes), every
  hook restored. The capture (`reports/menu-probe/calibrate_on_resume.png`)
  shows the row like the game's own; its help wrapped "T-pose" at the
  hyphen, so the help was reworded afterwards ("Fit Spider-Man to you: pick
  ON RESUME, then hold both triggers in a T-pose.").
- `tools/probe_game_body.py --phases rig,tpose` (player at 35 s; report
  `reports/body-probe-tpose.json`): the hero's rig gave the proportions the
  defaults hold (eyes 1.6969 m up, arms 0.5586 m). Eyes 1.66 m up as a
  calibrated height (body 0.9782), both wrists 0.62 m straight out from the
  rest pose's shoulders: with the hero's own arms they ended 8.5 and 7.1 cm
  short of their controllers (left, right); with the arm length the body's
  arms scaled 1.1346 (0.62 / (0.5586 x 0.9782)) and ended 2.7 and 0.8 cm from
  them. What is left is the game's standing pose holding the shoulders 3-7 cm
  higher than the rest pose, with the far reach lifting the clavicles
  (shoulders at 1.47 m against 1.40 estimated). The screenshot
  (`reports/body-probe/tpose-calibrated.png`) shows him in a T-pose with the
  longer arms.

Save files unchanged except `slot0-s.save` (the game's own save when the menu
probe left the pause menu). The calibration itself runs only in the XR
worker, so the panel in the headset, the pose's thresholds against real
players, and how the scaled arms feel need the headset. Not in the play
folder (the user said not yet).

## Five options removed — preceding build

The user, October 8: "lets remove web catch props, web shooter, your own body,
punch thugst, webs drawn by from options".

**What changed.** The launcher's OPTIONS card lost "Webs catch props and
thugs", "Web shooter", "Your own body", "Punch thugs" and "Webs drawn by"
(`App::segmented`, used only by the last, is gone). `SessionOptions` lost
`webGrab`, `overlayWebs`, `body`, `punch` and `webShooter`:
`sessionArguments` never passes `--no-web-grab`, `--overlay-webs`,
`--no-body`, `--no-punch` or `--no-web-shooter`; launcher.ini's `web_grab`,
`overlay_webs`, `body`, `punch` and `web_shooter` are no longer read or
written; `headsetSettings` ignores those keys on the session's settings line.
The SPIDY VR tab lost WEBS CATCH PROPS AND THUGS, WEB SHOOTER, the BODY
heading, YOUR OWN BODY and PUNCH THUGS: 10 rows, setting numbers 0x200-0x209
(WEBS: aim markers, webs hold in open air, swing speed limit, weight; COMFORT:
snap turn, smooth turn, controller vibration, game screen size).
`vr_settings::Item` lost `webGrab`, `webShooter`, `body` and `punch`; `Values`
keeps their fields, which only run_game_vr.py's switches set, so RESET ALL
leaves them as the session started.

**Protocol.** Unchanged: XrConfig and XrData version 14, the menu probe's
structs version 3; run_game_vr.py and its `--no-*` switches as before.

**Checks.** 172 core checks pass (the tab's two sections and ten rows, a
switch's round trip on WEBS HOLD IN OPEN AIR, RESET ALL leaving web grab,
punching, the body and the web shooter off when a session started them off),
12 launcher checks (the arguments without the removed flags, the headset line
ignoring their keys) and 86 Python checks. `tools/probe_menu.py` follows the
new rows (aim markers off; webs in open air off and back with X; swing speed
40 m/s; weight 80%; past the COMFORT heading to snap turn 45 degrees and
smooth turn 60 degrees a second; RESET ALL: 12 changes, the web shooter still
off).

In the game without a headset (fresh game, the user's save,
`probe_menu_pad.py start` + `pad a --until-player`, player at 31 s):
`tools/probe_menu.py` passed every step, 12 changes, every hook restored; the
captures show WEBS with its four rows, then COMFORT. Save files unchanged
except `slot0-s.save` (the game's own `[Save] Request save type 0` when the
probe left the pause menu). Deployed in place to `dist\Spidy-0.2.2`: Spidy
Launcher.exe, spidy_stereo_probe.dll, spidy_ray_bridge.dll (relinked) and
README.txt replaced, 29 files hash-verified, import check 42; previous files
in `reports\backups\play-folder-before-options-20261008-130334`.

## Weight — earlier build

The user, October 8: "the locomotion in the air feels too floaty, lets maybe
add controllable weight setting to ingame settings?"

**Cause.** Flight Spidy owns (a web attached, then released flight until the
player lands) falls under the swing solver's gravity, `game_swing::Config`
6 m/s² since October 5 (`physicsConfig`): 61% of real gravity, a fifth of the
game's own airborne gravity (the movement module's `air_gravity` reads 30.0
m/s² in `reports/weight-probe.json`). 6 was what the October 4 builds applied
in practice (configured 18, a third reaching the body through the one-step-old
steering that `InFlightStep` fixed).

**What it does.** `vr_settings::Values::weight`, percent of real gravity
(9.81 m/s², `vr_settings::gravity`), 60 by default (5.886 m/s², 2% under the
old 6). The SPIDY VR tab has a WEIGHT row under SWING SPEED LIMIT in WEBS (row
6, setting number 0x206; the tab now runs 0x200-0x20e, BODY and COMFORT one
lower) with 40, 60, 80, 100, 125, 150, 200, 250 and 300% (nine, the most a row
takes); `Item::weight` is the last item. `Swing::setGravity` changes the
solver's gravity from its next step; `SpidySwingSettings` takes it
(`game_swing::Settings` version 3, 28 bytes, `gravity` up to `maxGravity`,
30 m/s², else 2001), and the XR worker starts the swing with it and sends it
with every settings change. Coasting without input after a break falls at it
too.

**Protocol.** XrConfig version 14, still 632 bytes: the spare word after the
render scale is now `weight` (40-300; anything else is 1001). XrData version
14, 752 bytes: `weight` at 744, then a spare word; Python
`vr_settings.weight`, and the settings line for the launcher carries
`weight=`. The menu probe's `ProbeSettings`/`ProbeSample` are version 3 (40
and 64 bytes) with `weight`. Launcher: `SessionOptions::weight`, `--weight`
(left out at 60), `weight=` in launcher.ini (40-300), a "Weight" list;
`launch-game-vr.ps1 -Weight`; `run_game_vr.py --weight` (40-300).

**Checks.** 172 core checks pass, new: the swing's fall over 20 ms at the
default weight and at 150% after `setGravity` (velocity within 0.001 of
gravity × time), NaN, negative and infinite gravities refused with the old one
kept; the row under the swing speed, its nine steps, a launcher weight between
two steps (70 shows 60%, 71 shows 80%), the 40-300 range, the heaviest weight
within `maxGravity`. The launcher checks (the argument, its 300 cap, the
headset line and its 40 floor), 86 Python checks (XrData v14 with the weight,
the settings line) and the GPU test pass.

In the game without a headset (fresh game, the user's save, perched where it
loads; `probe_menu_pad.py start` + `pad a --until-player`, player at 33 s):
`tools/probe_menu.py` passed every step, WEIGHT stepping from 60% to 80% with
the left stick (change 3) and back to 60% with RESET ALL (13 changes), hooks
restored. `tools/probe_weight.py` jumped, webbed the open direction, reeled
1.1 s, let go and in that flight fitted the vertical acceleration of every
airborne step on game time (about 200 steps a second in the small window):

| Weight | Asked (m/s²) | Measured (m/s²) | Steps | Vertical speed |
|---|---|---|---|---|
| 60% | 5.886 | 5.884 | 99 in 0.505 s | 11.13 to 8.18 m/s |
| 150% | 14.715 | 14.716 | 100 in 0.501 s | 6.65 to -0.67 m/s |
| 300% | 29.430 | 29.530 | 60 in 0.301 s | -3.72 to -12.47 m/s |

Every airborne step ran Spidy's command; the player landed after the last
window; a gravity of 31 m/s² was refused (2001); the modules stopped and the
hook entries were restored. Save files byte-identical but `slot0-s.save`,
which the game saved itself at 12:26:29 (`[Save] Request save type 0`, the
probe leaving the pause menu, before the jump); backup in
`reports/backups/save-before-weight-probe-20261008-122456`.

How each weight feels, and the XR worker taking the tab's change during a
headset session, need the headset. In the next report: `vr_settings.weight`.

## Fingers in a fist — earlier build

The user, October 8: "my fingers in vr appear twisted and tangled in most
poses". The hand crops of their October 7 23:11 session
(`game-vr-20261007-231152-eyes/0015-right-hand.png`, `0017-left-hand.png`,
64-68 s, swinging with the grips held): the right fist a claw, its fingertips
bent up out of it; the left hand's fingers bent back out and crossing.

**Cause.** A hand closes into a fist by its grip (every web) or by moving
faster than 1.2 m/s against the head. The fist bent each finger joint toward
the palm about cross(bone before it, palm's normal). That axis turns over
once the bone before has curled past the normal: at a full fist the middle
bones point back at 178° from the bones in the palm, so each tip joint bent
60° backward; from a hand the game had already closed, middle joints folded
back through themselves. And `prepare` took the rest pose's palms as facing
down (the thumb overruling it only when clearly elsewhere). The hero's rest
pose holds them 30° from down, toward the thumb, measured from the knuckles:
the fingers closed diagonally across the palm, and each hand sat rolled 30°
on its controller. The game's own bends agree with the knuckles: in the
perch pose of `reports/body-probe-fist.json` it bends each finger's middle
and tip joints about axes 3-9° from the knuckles' hinge, 24-32° from the
assumed one.

**What it does.** `body::prepare` takes the palm's normal across the line
from the first finger's knuckle to the last's and along the bones in the
palm, when an arm names two fingers or more and it lies within 60° of the
old guess. Each finger joint after the palm's gets a hinge, across its
finger's bone in the palm and the palm's normal at rest, kept in the frame of
the joint before it (`Rig::fingerHinges`); the thumb's two joints get one
across its first bone and the way to the little finger (`Rig::thumbHinges`).
`body::solve` turns each joint about its hinge from the game's bend to
`fistBend` (83°, 95°, 63°), the fist's share of the way. The thumb's last
joint bends to `thumbBend` (1.1 rad), the joint before it as far as puts the
tip as far from the thumb's base as its place (at most `thumbBendMax`,
1.4 rad), and the base swings the tip there: over the middle bones of the
index and middle fingers, `thumbRest` (0.016) out of the fist. `thumbFold` is
gone. `tools/probe_game_body.py` gains `--fists LEFT,RIGHT` and `ik.hands`
(each palm against its controller's, every finger joint's bend about its
hinge, the thumb's bends, twist and tip height).

**Measured without the game**: a scratch program with the old and the new
solver on Spider-Man's own left hand (the rig's rest positions), a controller
held thumb up:

| | Before | Now |
|---|---|---|
| Palm against the controller's palm | 29.9° | 0.0° |
| Full fist, every finger's knuckle, middle and tip joint | 82°, 96°, −60° | 83°, 95°, 63° |
| Half fist from straight fingers | tips −35° on three fingers | 42-50°, 47°, 31-32° |
| Half fist from the game's own fist (101-118°, 109°, 68-69°) | ring middle joint 172°, little −175° | 92-100°, 102°, 66° |
| Joints bent backward, any start, half or full fist | 1 to 8 | 0 |
| Fingertips off their finger's own plane | up to 7.1 cm | 2.6 cm, the rig's own spread |
| Thumb | no hinge; tip at the index finger's middle joint | 59° and 63° about one hinge; tip 1.6 cm out of the fist |

**Checks.** 171 core checks (3 new, with both of Spider-Man's hands and with
turned and mirrored joint frames: each palm faces its controller's within
0.6° for three grips; every finger joint goes a quarter, half or all of the
way from the game's bend to the fist's within 0.6°, from straight fingers,
fingers bent back 15° and the game's own fist, with the fingertips tucked in
side by side; the thumb's tip within 3 mm of its place, its joints bending
the same way about one hinge, the last at `thumbBend`). The scratch program's
numbers fail the new checks for the old solver. 86 Python checks.

**Not yet seen:** the game and the headset. The user declined the game check
for now; `probe_game_body.py --phases rig,ik,eyes --fists 0.5,1` is ready for
it. In the next report read the hand crops in `<report>-eyes/`, and ask how
the hands sit on the controllers (the palm turned 30°).

## Render resolution above the headset's — earlier build

The user, October 8: "right now we can't change resolution to be above our
current headset making the game look blurry\aliased, maybe add an option to
go above it?" Two Discord reports the same day: a Quest 3 player over
Virtual Desktop whose log shows "Recommended eye 0: 2496x2688" but
"Rendering 2048 x 2048 pixels per eye" (the launcher's 2048 x 2048, below
their headset's), and a player whose Virtual Desktop recommends 4032 x 3648.

**Cause.** The eyes were the runtime's recommended size (`XrRuntime`), or a
square override (`--size`, the launcher's 2048 down to 1280). Every layer
refused more than 4096 a side (`maximumEyeSize`: OpenXR setup, the game's eye
views, the GPU bridge, the session's snapshots, the probes).

**What it does.** `spidy::scaledEyeSize` (`eye_resolution.hpp`): the
recommendation itself at 100%, otherwise each side times the percentage in
multiples of 8, shrunk with its shape to the runtime's `maxImageRect` and to
`maximumEyeSize`, now 8192. XrConfig version 13 (632 bytes) carries
`renderScale` (50-200; with `eyeSize` set only 100) and a spare word that
must be 0, else 1001. The launcher's "Render resolution" is a slider in steps
of 5 (`render_scale=` in launcher.ini; `eye_size=` is no longer read); after
Check its line shows the eye size from the probe's "Recommended eye 0", and
its memory figure is `vrCommitGb`. `run_game_vr.py --render-scale`,
`launch-game-vr.ps1 -RenderScale`; the report has `render_scale` and
`xr_runtime.recommended_eye`. Aim markers are drawn at `pixelAngle` times the
eye width over the recommended width (at least 1), so they keep their size at
100%. The blit that puts the game's presented frame into the headset image
samples at `SV_Position` times one over the target size instead of an
interpolated coordinate.

**In the game, no headset** (`tools/probe_vr_load.py`, the user's save,
phases stock, vr and turn, a fresh game per size, `spidy_stereo_probe.dll`
38f9ca8e pinned because another session rebuilt meanwhile):

| Eyes, views | vr fps | turn fps | GPU busy | video memory, vr | commit, vr |
|---|---|---|---|---|---|
| 3072 x 3264, 29 (occlusion) | 90.3 | 102.7 | 85-87% | 11,594 MB | 16,010 MB |
| 4608 x 4896, 29 (occlusion) | 66.1 | 70.6 | 92-93% | 14,166 MB | 18,620 MB |
| 4608 x 4896, 13 (none) | 32.2 | 42.0 | 67-74% | 14,617 MB | 19,276 MB |

The game alone (stock): 221-235 fps, video memory 8.6-10.0 GB, commit
12.7-14.2 GB. 150% against 100%: 2,532-2,572 MB more video memory and
2,524-2,610 MB more commit (vr and turn phases), 105-109 bytes per extra eye
pixel, so `EYE_COMMIT_BYTES` is 110. The engine reported both eyes' size,
render size and viewport as 4608 x 4896. `reports/vr-load-4608-shots-eyes/`
(capture mode, one heading, at half size) is a whole street view with no
black or flat rows or columns; a full-size crop from the bottom-right corner
past 4096 shows the pavement's texture. The first 150% run had no eye
occlusion (the probe's default views 13); its render commands were 67 MB a
frame against 17 and its frame rate half. Each game was closed with WM_CLOSE
and the display settings restored.

**Checks.** 168 core checks before another session's body tests were added
(new: the scale's sizes, the 8192 limit), 12 launcher checks (new: the
headset check's eye size and `vrCommitGb`; the session arguments without
`--size` and with `--render-scale`, capped at 50 and 200), 86 Python checks
(new: `scaled_eye_size` with the core cases, `vr_commit_mb`,
`rendering_line`, the argument checks; the probe's video memory and commit),
and the GPU test at 1536, 3072 x 3264, 4608 x 4896, 6144 x 6528 and
8192 x 8192. At 4608 x 4896 it first failed twice: the aim marker check's
window was too small for the catch target, which has a size in metres
(widened), and the game-screen copy changed values by 2 levels where 1 is
allowed (fixed by the blit change above). 4096 x 4096, 4608 x 1536 and
1536 x 4896 had passed it before.

**Not yet seen:** the headset. In the next report check `render_scale`,
`xr_runtime.recommended_eye` and the eye size samples; ask how sharp it
looks, and compare new eye pairs a second with the 77 of October 7.

## Walls the game sticks the player to — earlier build

The user, October 8: "sometimes the player gets attached to the walls in vr
(crawling mode i guess) which looks and feels like im clipping through the
wall. jumping from it fixes it."

**Cause.** The game's wall crawl (`HeroStateWallCrawl*`): a player who flies
into a wall sticks to it. In the session reports it is mover flags `0x80a6`
with collision flags `0x2060005` (the game moves the player without sweeping),
then `0x8001` / `0x2060000`, contact 0, Spidy not driving, body not grounded:
six stretches in the October 6-7 reports (16:36 four, 10:43 one, 22:08 one at
81 s), 1-2.4 s each, 20-200 m up, each ended by a jump. The eyes were the
tracked head placed upright from the player's feet (`GameTrackingRig`), and
the feet are on the wall: the 22:08 session's left eye at 180 s is on the
brick wall's plane.

**In the game, no headset** (`tools/probe_wall_crawl.py`, the user's save,
October 8; `reports/wall-crawl.json`, `reports/wall-crawl/on-wall.png`): the
nearest wall was 6.07 m from the hand, 12° up. Jump, web at it, reel: the game
took the player at 2.45 s (`0x80a6` / `0x2060005`, contact 0) and turned the
actor's up from 0° to 90° from the world's in 0.25 s, ending at (1, 0, 0), the
wall's normal by rays to 2e-7. Rays along that normal: feet 0.00 m from the
wall's surface; eyes 1.65 m upright from the feet (as before) 0.00 m from it,
stood off as now 0.50 m. Idle on the wall (`0x8001` / `0x2060000`) the actor's
up leaned away from the normal by up to 14° over 30-80 ms and snapped back,
again and again (clean rotations, not torn reads), so the stand-off goes level
from a wall and straight down under a ceiling, not along the actor's up.
The jump off made the actor upright in 0.3 s (airborne from 81° down); Spidy's
own flight (owned) never tilted it. Hook entries restored, modules stopped
with 0, the save files byte-identical afterwards. The first attempt that
morning stopped at the title: another window came in front, the game paused,
and it was closed.

**What it does.** `GameTrackingRig::update` takes the player's up (its actor's
second row; the world's up while Spidy's swing owns the flight). Past 45° from
upright (back within 35°) the player is on a surface: the head, eyes, hands
and swing aims are placed `standOff` away from it, level from a wall, straight
down from a ceiling, the feet staying the render anchor. The stand-off puts
the head `wallClearance` (0.5 m) from the surface's plane for the first
0.5 s (the actor turning onto it), then only keeps it `minWallClearance`
(0.25 m) away; it never shrinks while on the surface, so leaning back moves
away from the wall. It approaches its target by 63% in 0.06 s; off the
surface it returns to zero the same way. A snap or smooth turn pivots at the
head and a recenter keeps the head, stand-off apart.

**Checks.** 167 core checks (5 new: stand-off on a wall with eyes, hands and
web hands moved and the feet kept as anchor; the actor rocking 14° does not
move the head; leaning in to the minimum and back; turning and recentering
on a wall; a ceiling, a 30° slope, the 35-45° hysteresis), 84 Python checks
(XrData version 13, 744 bytes: `surfaceEntries`, `surfaceFrames`, `heroUp`,
`standOff`, `surfaceHeight`, `surfaceClearance`; report `surface`), the GPU
test.

**Not yet seen:** the headset. In the next report, `surface.entries` counts
the stretches; `head_height_m` near 0 or below with `head_clearance_m` near
0.5 is the fix at work. Ask how leaning on a wall feels.

## Smooth turning — earlier build

The user, October 7: "add smooth turning option".

**What it does.** `vr_settings::Values::smoothTurn`, degrees a second (0, the
default: the right stick snap turns as before). The SPIDY VR tab has a SMOOTH
TURN row after SNAP TURN in COMFORT (row 11, setting number 0x20b; GAME
SCREEN SIZE moves to 0x20d) with OFF, 60, 90, 120, 180 and 240°/S;
`Item::smoothTurn` is the last item (`vr_settings::lastItem`, which
`game_menu.cpp`'s `copyItems` now loops to). `GameTrackingRig::smoothTurn`
takes radians a second. While it is on, the right stick's X past a 0.2 dead
zone turns the tracking space about the head by `speed × tilt × seconds`
each headset frame, tilt rising linearly to full at 0.9; snap turning is off
meanwhile. A stick held into play waits for its release (below 0.3) after a
break over `controlHoldMs` (500 ms, a menu), as a snap turn always did, but
not after a stutter, so a turn in progress goes on through a hitch. The body
follows each turn through the tracking yaw it already takes
(`native_body`, `body::turnState`).

**Punches.** `game_punch` measured the fist's motion as the grip relative to
the head turned to the world by the tracking yaw, so any change of that yaw
counted as arm motion: a still fist 0.6 m out at 240°/s measured 2.5 m/s, over
the 2.2 m/s punch speed; a 30° snap turn put 15-20 m/s into one sample.
`Punches::turn` now turns each hand's last relative position and smoothed
velocity by the yaw change since the previous sample (`std::remainder` of the
tracking yaws), so only the arm's own motion counts.

**Protocol.** XrConfig version 12, 624 bytes: the reserved word after the
screen size is now `smoothTurn` (0-360; anything else is 1001). XrData
version 12, 712 bytes: `smoothTurn` at 704 (then a spare word); Python
`vr_settings.smooth_turn`, and the settings line for the launcher carries
`smooth_turn=`. The menu probe's `ProbeSettings`/`ProbeSample` are version 2
(36 and 64 bytes) with `smoothTurn`. Launcher: `SessionOptions::smoothTurn`,
`--smooth-turn` (left out at 0), `smooth_turn=` in launcher.ini, a "Smooth
turn" list; `launch-game-vr.ps1 -SmoothTurn`.

**Checks.** 162 core checks pass, new: smooth turning at full, half and
dead-zone tilt and to the left for a second at 90°/s (the head's forward
within 0.002 of 90°, 45°, 0 and -90°, its position unchanged with the head
0.4 m off the tracking origin); a 20 ms gap goes on turning, a 610 ms one with
the stick held turns nothing until it is let go and held again; a still arm at
240°/s measures over 2.2 m/s without `turn` and under 0.01 with it, and a
5 m/s punch during the turn measures 5 ± 0.05; the tab's rows, the new steps,
a launcher speed between steps (100 shows 90), the 0-360 range. 10 launcher
checks (the argument, the headset line, the range), 72 Python checks (XrData
v12 with smooth turning, the settings line) and the GPU test pass.
`tools/probe_menu.py` steps SMOOTH TURN to 60°/S before RESET ALL and expects
11 changes. How smooth turning feels, and the XR worker applying the tab's
change at once, need the headset.

## Midair drops — earlier build

The user, October 7: "sometimes while in the air i get pulled down fast as im
diving out of no where I think its a game mechanic lets stop it".

**What the reports show.** Every place in the recent session reports where a
step of the player's mover driven by Spidy in the air was followed by one the
game ran itself (`motion_samples`, status 2 then 1, contact 2): 14:13 session
(play folder `dist\Spidy-0.1.1`), 3 in 11 minutes of retained samples; 14:09,
1; 10:43, 2; October 6 19:22, 2; 16:36, 2; 14:50, 4. In 7 of those 14 the
game's airborne vertical speed (`air_vertical`) went in one step from Spidy's
(+6 to -8 m/s) to -36 to -48 m/s, and on from there toward about -50; in 3 to
about -20 m/s. The 14:13 session's three, by the XR samples' `seconds`: 508 s
(-1.2 to -42.5 m/s, both webs held, two steps in 128 ms), 583 s (+3.0 to
-36.3, released flight, one step in 107 ms) and 786 s (-8.2 to -42.4 on the
first step after 13.3 s in the pause menu, which the menu button had opened in
midair). The game's air state (event handler
`0xa7b3a0`, vtables `0x38c1340`, `0x38c9090`) keeps its own fall going through
Spidy's flight, from the time airborne; the displacement override Spidy gives
its airborne event while driving does not reach that, so any step it runs
itself in midair starts at that fall speed. Each place was a let-go by the
swing: `relinquishMotion` (enabled 0) or a lease that ran out.

**Why the swing let go.** In `game_swing.cpp`, any of these in midair cancelled
the swing (webs released, flight handed to the game): the input unfocused or
its 100 ms lease lapsed (the XR worker's gameplay gate closes when the game
camera has not committed for 100 ms, so a frame of over 100 ms closes it, and
the first active headset frame after any break marked both hands untracked);
no step of the mover for 50 ms (a long frame); a step the visits missed; a
controller sample more than 0.1 s after the previous one. And a step that came
more than 150 ms after Spidy's command ran without it (the pause).

**Changes.**

- `game_swing::InputHold` (`include/spidy/game_swing.hpp`): a focused input
  sample stays in use for `controlHoldMs` (500 ms, the XR worker's existing
  stutter allowance) after input stops or turns unfocused, as a sample that
  is simply not new (same serial: no new presses, yanks, shots or punches).
- `game_swing.cpp`: past the hold, flight the swing owns coasts (`coast()`,
  once per stretch): webs, grabs and shots end, the input clocks start over,
  and the body flies on with no input under the swing's gravity until it
  lands; the game has it on landing, as before. Without owned flight, the
  swing cancels as before. No step for 50 ms: wait for the next one (the swing
  still lets go when input is live and no step came for 500 ms, as on a
  perch). A step the visits missed: carry on from the observation. The world
  identity, the mover's 0x80000000 flag, the movement module stopping and an
  invalid prediction still cancel.
- `GameTrackingRig` (`src/game_tracking.cpp`): the first active frame after a
  break shorter than `controlHoldMs` keeps the hands tracked; after a longer
  one, a recenter or the first activation, each hand must squeeze again, as
  before.
- `Swing::predictNativeStep` (`src/swing.cpp`): a controller sample more than
  0.1 s after the previous one starts each hand's motion afresh (no yank from
  the gap) instead of releasing the webs.
- `native_movement` (`commandApplies`): a command governs the first step of
  the player's mover after it arrives, however late that step comes, then
  every step within its 150 ms lease as before. No protocol changed.

**Measured in the game without a headset** (`tools/probe_air_handoff.py`, the
user's save perched above a rooftop, loaded with the virtual controller; the
same probe and save for both builds, a fresh game each): it jumps with A,
shoots a web 60 degrees up the most open direction, reels from 0.5 s, and at
1.6 s turns the input unfocused for 0.3 s with the grip and trigger held
(`hold`), releases at 2.7 s, suspends the game process for 0.25 s (`hitch`),
turns the input unfocused for 2 s (`coast`), then unfocused 0.15 s, the game
suspended 3 s and unfocused 0.3 s more (`pause`), reading every step of the
player's mover. Reports: `reports/air-handoff-before.json` (the play folder's
movement and ray modules, `dist\Spidy-0.1.1`) and `reports/air-handoff-after.json`.

| | play folder's build | this build |
|---|---|---|
| `hold`: airborne steps without Spidy's command | 138 | 0 |
| `hold`: vertical speed, start → end | +11.6 → -46.9 m/s | +11.6 → +11.3 m/s (reeling) |
| `hold`: largest change between two steps | 26.8 m/s | 0.01 m/s |
| web still held after `hold` | no | yes |
| `hitch`, `coast`, `pause` | not reached: the player had landed | 0 steps without command; at most 0.09 m/s a step |
| `coast`: vertical speed over 2.3 s | | +5.9 → -8.1 m/s (6 m/s²) |
| `pause`: first steps after the 3 s freeze | | driven; -8.0 → -15.0 m/s over the window |

Swing samples showed the flight owned throughout `coast` (398 samples, webs
released) and the web held through `hold` (63 samples). Both modules stopped
with 0, every hook entry was restored, and the save files were byte-identical
to the copy taken before the runs. 159 core checks (new: the input hold, the
rig keeping webs through a stutter, the first sample after a gap, a late step
taking its command), 10 launcher checks, 72 Python checks and the GPU test
pass.

Not checked: a real session's hitches and pause menu (the probe stands in
with process suspension and unfocused input commands), and how gliding under
Spidy's gravity after a long break feels. In the next headset report, a drop
would show as an airborne `motion_samples` entry with status 1 right after one
with status 2; gliding shows as status 2 steps while the XR samples' input is
unfocused.

## VR settings in the game's own Settings — earlier build

The user, October 7: "can we put the ingame vr settings as actual new items in
the ingame settings menu (not a seperately rendered drawer)?", then "build it".

**How the game builds its Settings.** From the executable's strings, RTTI,
reflection tables and disassembly, then a research DLL in the running game
that logged the Settings' Flash calls and callbacks (addresses in
[REFERENCE.md](REFERENCE.md)). The Settings are Scaleform (AS3) screens. The
pause menu's Settings (`PopupSystemMenuUI`) build every tab of the settings
config (`configs/uiconfig/uisystemmenu.config`: 9 tabs, ids 2-10) with one
builder (`0x807740`) and hand the list to Flash (`OpenOptions`, through the
UI's call wrapper `0x1d1cf30`): 8 of them, since CONNECTIONS (id 10) has no
row to show on PC and its builder returns nothing. A tab's items name the game's settings by number (0-122); the settings
system answers a number's kind, value and default from unchecked 40-byte
records. Flash calls back `UpdateOption` (setting, value), `ResetOption` and
`ResetAllOptionsForCurrentOptionsMenu` (tab id); set and reset skip numbers
above 0x7a, RESET ALL tab ids above 10. A setting with no choices of its own
is the game's OFF/ON list.

**What Spidy adds** (`src/game_menu.cpp`). When the Settings call
`OpenOptions`, one more tab is built by the game's builder and pushed onto the
list: a copy of the GAME tab (id 93) holding 13 items copied from the config's
first heading and its first setting with choices, numbered 0x200-0x20c, their
texts and choices Spidy's (`vr_settings::rows`: WEBS, BODY, COMFORT; 6
switches, 4 lists). Hooks answer the getters for those numbers (kind, value =
the choice shown, changeable, default), take set and reset, answer the texts'
hashes only while Spidy's tab is being built, and take RESET ALL for tab 93
(Spidy's defaults, then the tabs built again as the game does). 11 hooks; each
function's first bytes are compared with the supported game's first, and all
are enabled and disabled together. The XR worker installs it when it starts,
takes the tab's changes each headset frame (`take`, then `applySettings` as
before) and publishes its values (`publish`: what X switched shows too). A
launcher value between steps shows as the nearest step and stays until changed.
The panel is gone: `vr_settings::Panel`, its canvas, `D3D12Renderer::blitPixels`
and `XrRuntime::panel`; no trigger is kept from the virtual controller.

**Protocol.** XrConfig v11 (layout unchanged) and XrData v11: the panel's
frame counts became `menuTabs` (the game built its Settings with the tab),
`menuInstalled` and `menuStatus` (93xx-94xx not hooked, 95xx not built);
`run_game_vr.py` reports `menu_tabs`, `menu_installed`, `menu_status`. Headless
probes: `SpidyMenuStart`, `SpidyMenuSample`, `SpidyMenuStop`.

**Measured in the game** (`tools/probe_menu.py`, the user's save, free roam,
steered with the virtual controller's left stick): Settings listed SPIDY VR
after KEY MAPPING (Up from GAME); the tab showed the start values (33 m/s as
32 M/S, the web shooter OFF); right on AIM MARKERS gave OFF, on SWING SPEED
LIMIT 40 M/S, on YOUR OWN BODY OFF, X put it back ON, right on SNAP TURN 45°; Y
asked "Are you sure?" and A put Spidy's defaults back (4 more changes, the web
shooter ON); each step's values as expected, 9 changes, the tab built 7 times
(each opening and refresh), status 0; B three times resumed play. All 11 hook
entries patched while in and the game's own bytes after `SpidyMenuStop`. In an
earlier run of the same code (texts not yet gated to the tab's build), Settings
with the hooks out listed the game's 8 tabs only, and with them in again SPIDY
VR (opened from the remembered position: KEY MAPPING, then down). The game's
settings file (`-userprefs.save`) stayed byte-identical; `slot0-s.save` was
rewritten by the game's own save after Continue, as on every probe's load. 155
core checks (the panel's 5 replaced by 2: the tab's rows, sections and choices;
switches and lists round-trip, launcher values show the nearest step, every
default together is `Values{}`), 10 launcher checks, 72 Python checks
(`menu_tabs`, `menu_installed`, `menu_status`) and the GPU test (its panel part
removed) pass.

Not checked: the XR worker's part (install at session start, changes taken and
applied, XrData telemetry) runs only in an OpenXR session; how the tab reads on
the headset's game screen. The title screen's Options keep their own lists and
show no SPIDY VR tab.

## The web shooter — earlier build

The user, October 7: "Lets add the ability to shoot the web projectiles
spiderman has normally (not the regular webs, the web bullets)".

**What a pull does.** The trigger of a hand whose web is neither attached nor
holding a target shoots one web ball (`Shooter`, engine independent). A pull is
the trigger past 0.65 after it was below 0.35, the swing's reel thresholds; one
hand shoots at most every 0.12 s. A trigger already pulled when the shooter
starts, when its hand was busy, or through a tracking or focus loss (a menu,
the flat screen) is no pull until released. The ball leaves 8 cm ahead of the
aim pose. A bot whose chest (feet + 1.15 m) is within 0.12 rad of the hand's
line, or within his 0.45 m radius of it, nearer than 45 m and with no surface
between, takes it (the one nearest the line, as a share of what he may be off
it); otherwise the aim point is 0.4 m past the first surface the line meets
within 60 m, else open air at 60 m.

**How the game fires it.** Traced on October 7 with a research DLL hooking the
weapon path while the virtual pad's RB fired the equipped gadget (the Impact
Web: RB fires the gadget wheel's selection, and the web shooter was not it):
the hero's weapon state hands the weapon a fire event on the main thread
(`WeaponGame` vtable +0x108, 0xe3bd80), which stores the event's aim and fires
(0x2150460); `SpawnShot` (+0x110, 0x2150830) asks the muzzle (+0x160,
0x2150c40, the emitter component of that index) and spawns the shot actor
(+0x2f8, 0x215c190). The hero's `WeaponWebShooter` (vtable 391c950) is a
component of its own weapon actor, 0.5 m from the hero, not of the hero's
record. Fire events injected from the camera manager's update (0x897d30) with
an overridden muzzle spawned a `ShotWebShooter` at the scripted point that
flew straight to the event's aim point (+0x20) at 53-61 m/s and ended there,
or at the first surface; its matrix rows did not change its course. The event
layout, the shot ids (0x215f040 allocates from the game's own counter; 0x215f840
registers one, despawning what an older shot left in its ring slot) and the
target reference (0x1f7b8e0) are in `game_shooter.hpp`. A pedestrian has no
reference (0x1f7b8e0 gives 0), so the aim assist offers bots only.

**The module.** `game_shooter` in the ray bridge: the swing's input callback
updates it with each sample (after the swing's step, beside the aim previews,
so its rays never fault the swing); pulls are queued with the player's actor
and position, and the camera manager's update fires them on the main thread:
the web shooter fills its own part of the event (+0x100), Spidy sets the
emitter (right wrist 0, left 1), a new shot id, the target's reference (with
+0x39, so the game's own target point replaces the aim point when it
resolves), the aim point and the level facing, and the weapon's fire event
runs with the muzzle hook returning the hand (moved with the player since the
sample). The gadget is found by a registry scan on its own thread (nearest the
player, within 10 m); a fire checks that it is still registered and that the
two things the game reads without a check exist (its setup at +0x48, the
actor at +0x5b0). Signatures of all seven functions and four vtable slots are
checked before the hooks go in. Shots take no gadget ammo.

Protocol: XrConfig version 10 (options bit 512: no web shooter, up to 1023),
XrData version 10 (settings bit 16), both sizes unchanged; ray module exports
`SpidyShooterStart`/`Stop`/`Sample`/`Test` and `SpidyShooterData`
(`game_shooter::Data`, 160 bytes). The runner's report has `shooter` and
`shooter_samples`, and each sample `shooter` (fired, dropped, bots on offer).
The VR settings panel has "Web shooter" under WEBS: 13 lines, 560 x 940
points, 840 x 1410 pixels.

Checks without the game: 158 core checks (new: one shot per pull, a held or
half-released trigger never again, repeated samples nothing, both hands
together; pulls closer than the interval shoot once; a busy hand, a trigger
held after its web let go, through a tracking loss or a reset shoot nothing;
the aim assist takes the thug 3 degrees off the line, not one 14 degrees off,
behind a wall, behind the hand or 50 m away, one beside the hand within his
width, not one clear of it, and none behind a pillar, whose surface the shot
goes to instead; sky and wall aims; invalid tuning refused), 10 launcher checks
(`--no-web-shooter`; `web_shooter=0` from the headset's line), 72 Python checks
(the shooter's telemetry and torn reads, XrData v10 settings bit 16, the start
values and the line with `web_shooter`), and the GPU test at 1536 x 1536 and
3072 x 3264: the panel painted at 840 x 1410 pixels with the new switch on.

In the game, without the headset (`tools/probe_shooter.py` in a fresh game on
the user's save, perched on the Times Square lamp post, the player 35 s after
launch), the scripted left hand 1.2 m above the feet pulling its trigger
through `SpidySwingSubmit`:

| Aim | Shots | Flight | Off the aim | Start from the hand | End |
|---|---:|---|---:|---:|---|
| Level, along the camera | 1 | 60.2 m at 60.2 m/s | 0.00° | 0.00 m | 60 m out (1 s), surface 73 m |
| 30° down | 1 | 6.5 m at 51.7 m/s | 0.01° | 0.00 m | the pavement (0.4 m from the aim point) |
| Sky | 1 | 60.2 m at 60.2 m/s | 0.00° | 0.00 m | 60 m out, open air |
| 60° left | 1 | 10.4 m at 55.7 m/s | 0.01° | 0.00 m | a wall (0.4 m from the aim point) |
| 60° right | 1 | 60.0 m at 59.1 m/s | 0.00° | 0.00 m | 60 m out |

A trigger held for a second shot once; three pulls 0.2 s apart shot three
times; the shooter stopped and started again during play (0, 0) and shot
again. 10 pulls requested, 10 fired, none dropped; the main-thread hook ran
1,692 times; no swing fault; swing and rays stopped with 0, every hook entry
restored, and the save files byte-identical to the backup. Window captures at
the game's frame rate while a test shot crossed the street 5 m ahead show the
ball as a white motion-blurred streak, frame by frame. That run used a range of
80 m; a shot ends after one second (about 60 m) anyway, so the build sets 60 m
and nothing else changed.

Not checked: a ball reaching a thug and the game taking the thug as its target
(no bot was within 40 m in free roam; the report's `shooter.targeted` and
`resolved` count them in a fight), the sound, how the tick and the pull feel in
the headset, and the XR worker's start, stop and haptics, which run only in an
OpenXR session.

## Webs in open air as a setting — earlier build

The user, October 7: "lets make the ability to hook webs on max distance
without hitting an object (webs on nowhere) as an optional setting (on by
default)".

The swing already had the switch: `SwingConfig::airAnchors`, on, read by
`Swing::shot`, which both the grip press and the aim previews call. A ray that
meets nothing within reach (`maxRange`, 100 m) attaches the web in the air
there when it is on, and misses when it is off. Nothing in the game turned it
off until now.

- `Swing::allowAirAnchors` switches it during play. Shots from then on follow
  it; a web already attached keeps its anchor. (The panel shows only with the
  game screen, when the swing has let go of every web anyway.)
- `SpidySwingSettings` passes it: Settings version 2, still 24 bytes, the
  reserved word is now `airWebs`; version 1 and values above 1 are refused
  (2001). `previewAims` reads the solver's configuration too, so with it off a
  hand aimed into open air previews `none` (no marker, as a press would miss),
  and the red cross for an air point the body has no line to goes with it.
- VR settings: "Webs hold in open air" under WEBS, after "Webs catch props
  and thugs". One more 66-point row: the panel is 560 x 874 points, 0.9 x
  1.40 m in the headset, 840 x 1311 pixels at 1.5 pixels a point.
- A session starts with it off from XrConfig options bit 256
  (`run_game_vr.py --no-air-webs`, `Launch Spidy VR.cmd -NoAirWebs`, the
  launcher's "Webs hold in open air"). XrData settings bit 8 reports it; the
  headset's line and the launcher's settings file carry `air_webs`.

Protocol: XrConfig version 9 and XrData version 9 (sizes unchanged, 624 and
704 bytes; options up to 511), Settings version 2. As before, the runner and
the DLLs must come from one build: an older runner against these DLLs fails
with "Game XR start: 1001", this runner against older DLLs with "Game XR
protocol mismatch".

Checks without the game: 153 core checks (new: switched off during play, a
press into open air misses and its preview has neither web nor hit, a web
already attached keeps its anchor, a surface still holds, and switched on
again open air holds; the panel's switch switches back), 10 launcher checks
(`--no-air-webs`; `air_webs=0` from the headset's line), 71 Python checks
(XrData v9: settings bit 8 on and off; the start values and the line with
`air_webs`), and the GPU test: the panel painted at 840 x 1311 pixels, the
new switch showing its own value (on) beside punching off, drawn into the
eye image within 0 levels, the D3D12 debug layer clean.

In the game, without the headset (`tools/probe_aim.py --settings` in a fresh
game on the user's save, the player 26 s after launch): the sky aim
previewed open air at 100.0 m, as before. `SpidySwingSettings` with `airWebs`
0 returned 0, and the same aim previewed none (no web, nothing met: a press
would miss); with 1, 0 again and open air at 100 m. The grab switched as in
the preceding build (a prop 6.3 m away: anchor, prop, anchor, prop), a 70 m/s
limit was refused (2001), punching started and stopped twice (all 0). No
swing fault in 21 aims, both modules stopped with 0, every hook entry
restored, and the save files byte-identical to the backup taken before.

Not checked: what only the headset shows: the new row in the Quest, a click
on it, and the XR worker handing the launch option and the panel's switch to
the swing (`applySettings`), which runs only in an OpenXR session.

## VR settings beside the game's menus — earlier build

The user, October 7: "can you add a vr settings section to ingame menu?"

The game draws its menus itself; what the headset shows of them is the
game's presented frame on a virtual screen (October 5, sixth build), and Spidy
draws nothing into the game's UI. The section is therefore Spidy's own panel,
hung beside that screen whenever the headset shows it.

**Where and when.** The panel is a second OpenXR quad layer, 0.9 m wide,
starting 8 cm past the game screen's right edge and square to the viewer's
line of sight there, centred on the screen's height
(`vr_settings::placement`). It opens when the game screen comes up within 2 s
of the menu button pressed in play (the pause menu); on any other screen (the
game menu, the main menu, loading, cutscenes) it is folded to a 0.42 m tab
level with the panel's top. Its X folds it for the rest of the session; the
tab opens it again.

**Input.** Each controller's OpenXR aim pose (the space the screen is placed
in) is intersected with the panel's quad (`aimAt`; a ray along the face or
from behind misses). A pull past 0.75, released below 0.35, acts on what the
ray points at: a switch's whole line switches it, a stepper's left or right
half steps it. A pull already held when the screen came up, or begun off the
panel, does nothing. The virtual Xbox controller gets a copy of the frame in
which the trigger of a hand pointing at the panel, or holding a pull the
panel took, is 0; every other control stays the game's. A click ticks that
hand (0.3, times the vibration setting).

**Drawing.** `vr_settings::Canvas` paints text with GDI (Segoe UI, grayscale
antialiasing) into a 32-bit DIB and the switches, buttons and pointer dots as
signed-distance shapes, 1.5 pixels a point (840 x 1212 for the panel), less
when the eye images are smaller. It repaints only when what it shows changes
(hover, a pointer's whole-point position, a value, open or folded).
`D3D12Renderer::blitPixels` uploads the pixels (B8G8R8A8, one copy) and draws
them with the game screen's blit into the top-left of the right eye's
swapchain image, which the game screen leaves unused; `XrRuntime::panel`
submits that rectangle as the second quad. Every frame's swapchain image gets
it drawn; only a repaint uploads. A panel that fails to paint or upload is
dropped for the session with a status message, and the game screen goes on.

**The settings, live.**
- Aim markers: the worker's switch, as X switches it.
- Webs catch props and thugs: `SpidySwingSettings` (new ray-module export,
  Settings version 1, 24 bytes) calls `game_grab::allow`. Off, the grab lets
  go of what it holds or trails at the next input sample (`cancel()`: props
  back to their physics, bots to the game's flight), and previews and presses
  go to the swing. A swing started without the grab (`-NoWebGrab`) starts it
  there, its hooks installed under the swing's simulation lock.
- Swing speed limit: the same export sets the swing's cap and the solver's
  (`Swing::limitSpeed`). The movement module is now always started with
  65 m/s, the panel's highest step: it only rejects faster requests.
- Punch thugs: `SpidyPunchStart` and `SpidyPunchStop` from the worker.
- Your own body: the body command's `bodyOn` flag. Off, `native_body::drawn()`
  is false at once, so the eyes hide the hero and the overlay draws gloves; a
  session started with `-NoBody` starts the body module when switched on.
- Snap turn: `GameTrackingRig::snapTurn`, off or 15-90 degrees (30 until now,
  always); `reset()` keeps it.
- Controller vibration: `XrRuntime::hapticStrength` scales every pulse; off
  sends none.
- Game screen size: 2.4, 3.2 (as before) or 4.2 m wide at 2.5 m, the game
  screen and the flat-mode screen alike.

**The next session.** XrData reports the settings. When a session ends with
other values than it began with (the panel, or X), `run_game_vr.py` prints
"VR settings from the headset: aim_markers=... screen_size=..." and the
launcher saves them as its options (`headsetSettings`). The launcher's
options card, `run_game_vr.py` (`--snap-turn`, `--haptics`, `--screen-size`)
and `Launch Spidy VR.cmd` (`-SnapTurn`, `-Haptics`, `-ScreenSize`) start a
session with the three new ones.

Protocol: XrConfig version 8, 624 bytes (snapTurn, haptics, screenSize and a
reserved word after the runtime path); XrData version 8, 704 bytes (settings
bits 1 web grab, 2 punch, 4 body; snapTurn, haptics, screenSize, swingSpeed,
settingChanges, panelFrames, tabFrames). `run_game_vr.py` and the DLLs must
come from one build: an older runner against these DLLs fails with "Game XR
start: 1001", this runner against older DLLs with "Game XR protocol
mismatch".

Checks without the game: 152 core checks (new: the layout fits and each
control hits its own setting; steppers stop at their ends and step a value
between steps to the next one; the panel hangs past the screen's edge, square
to the viewer, for all three sizes, and aim rays meet it at its centre but
not from behind or from the screen; one click per pull, a held pull, a pull
begun off the panel and a trigger held as the screen came up click nothing,
a press keeps its trigger from the game until released; the tab opens it,
close folds it for the session; snap turn off and 90 degrees), 10 launcher
checks (the new arguments; the headset's line applied, clamped, ignored when
unchanged), 71 Python checks (XrData v8 fields; the line printed only for
changes), and the GPU test: the panel painted at the headset's 3072 x 3264
eye size (840 x 1212 pixels), drawn into the corner of a typeless sRGB eye
image within 0 levels with the rest of the image untouched, drawn again
without new pixels, the tab uploaded at its new size, and the D3D12 debug
layer clean (`vr-settings.bmp`, `vr-settings-tab.bmp`).

In the game, without the headset (`tools/probe_aim.py --settings`, the user's
save, perched on the lamp post): the swing started without the grab, as a
`-NoWebGrab` session does, and a hand aimed at the throwable prop 51.3 m away
previewed an anchor at 51.0 m, on the prop's surface; `SpidySwingSettings` with the grab on and 48 m/s
returned 0 and the same aim previewed the prop; off at 10 m/s, the anchor
again; on at 32 m/s, the prop. A 70 m/s limit was refused (2001). Punching
started, stopped, started and stopped (all 0). No swing fault in 19 aims,
both modules stopped with 0, every hook entry restored, and the save files
byte-identical to the backup taken before.

Not checked: anything only the headset shows. The panel is drawn by the XR
worker alone, which needs an OpenXR session: its size and sharpness in the
Quest, pointing and clicking with real controllers, the pause detection with
the game's real pause timing, and the quad layer on Virtual Desktop.

## Interact button and aim markers — preceding build

The user, October 6 evening: "lets add an interaction button in vr and maybe
some nice optional indicator\crosshairs for better aim".

**Interact.** The game's context actions and its web strike share one button:
Triangle, Xbox Y, keyboard F (the game's registry binds `Button_Y_1 = 33`,
scancode F, and lists no separate interact key). A GameFAQs walkthrough:
"Press Triangle to open the elevator doors", "press Triangle to interact with
the computer", backpacks likewise. In immersive VR the virtual Xbox controller
passed only Start (menu button), Back (Touch Y), A (the swing's jump) and the
left stick, so nothing could press the game's Y. Now Touch B gives the game's
Y (`game_pad::Walk::interact`) while VR steers and the swing does not own the
player. A press keeps the meaning it started with until it is let go: B held
from the game screen, where it is Back, is no interact (as A from the screen
is no jump), and an interact still held when it opens a menu or a scene (the
screen comes up 250 ms after play stops) is no Back there. XR telemetry
counts the presses (`interacts`).

**Aim markers.** What a grip press would do is worked out in the swing
module, inside the game's world-query callback, with the rays and picks the
press itself uses, for each free hand: first the grab's pick
(`WebGrab::preview`, against a read-only view of the targets: the press's own
pick starts following its target, the preview's does not), then the swing's
shot (`Swing::shot`, which the press now calls too, so the two cannot
disagree). The result per hand is anchor, air (no surface within 100 m: an air
anchor), blocked (what the ray meets cannot hold a web, the anchor is nearer
than the shortest rope, or a wall stands between body and anchor), prop or
character, with its point and normal. Previews run once per input command,
only while sampled (`SpidyAimSample`, 250 ms lease: with the markers off they
cost nothing), and after the swing's step in each callback: an error from a
preview ray is never read, so it cannot fault the swing. The headset overlay
draws one marker per free hand: a white ring and dot (anchor), a faint dashed
ring (air), a red cross (blocked), amber corners around the target (prop,
character); facing the viewer, a fixed size on screen (13 pixels of radius at
the eye image's pixel angle), a dark outline behind the colour, tightening as
the grip closes. A surface marker is placed where the image's own aim line
crosses the surface's plane (`onAimLine`), so it stays on the line the
hand points along while the next preview arrives. None for a hand whose web
is attached or holds something, none nearer than 2 m to the hand (a blocked
one 3 m: a lowered hand points at the floor by the feet). X switches them in
immersive VR (a short pulse on the left hand); `-NoAimMarkers`, the launcher's
"Aim markers" switch, or XrConfig option bit 128 starts with them hidden.

Protocol: XrData version 7, 664 bytes (`interacts`, `aimMarkers`, `markers`
drawn); XrConfig options up to 255; the ray module exports `SpidyAimSample`
and `SpidyAimData` (AimData version 1, 96 bytes). An older ray module without
them leaves the markers off.

Checks without the game: 146 core checks (new: a shot preview attaches where
the press attaches, and names what it met when it would miss; marker size on
screen at 5 and 50 m within 2%, outline behind the colour, squeeze tightens,
invalid input draws nothing; the aim-line crossing and its fallbacks; B is the
game's Y only through the swing's leave in VR and stays B on the screen), 9
launcher checks (`--no-aim-markers`), 69 Python checks (XrData v7 fields), and
the GPU test, which now draws every marker kind with the overlay renderer over
the lab scene in a 1536 x 1536 sRGB eye image and finds each one's colour and
outline (`aim-markers.bmp` beside its other images).

In the game without a headset (`tools/probe_aim.py` after `probe_menu_pad.py
start` and `pad a --until-player`, the user's save: free roam, Spider-Man
perched on a lamp post beside a building; `reports/aim-probe.json`): a
scripted hand 1.2 m above his feet got a preview for each of its 15 aims, the
swing never faulted, both modules stopped with 0 and every hook entry was
restored. Sky and the open street: air at 100 m. The building beside him:
anchors at 11.4-16.1 m; facades across the street at 36 and 60 m. Thirty
degrees down: the ground and the building at 5.3-12.4 m. Straight down:
blocked at 1.2 m, the lamp under his feet nearer than the shortest rope.
Aimed at the nearest throwable prop, 51.3 m away: prop. The save files were
byte for byte unchanged afterwards.

Not yet seen: the headset session (how the markers look and feel; B on a real
interact prompt). Whether the game's own interact prompt appears in the eye
images is unknown: its world markers, such as enemy reticles, do not (see
"Game screen for menus, hint cards and cutscenes" below), so a prompt may
show only on the monitor and the game screen.

## VR stays immersive in fights — checked in the game without a headset

The user, after the 16:36 session
(`dist/Spidy-0.1.0-win64/Spidy-0.1.0/reports/game-vr-20261006-163630.json`):
"in combat game goes to flatscreen mode for some reason".

What the report shows. Its samples cover the last 622 s. Between 946 and
1002 s the headset showed the game screen six times: 3.1, 4.3, 4.1, 17.9 and
17.5 s, then 0.4 s without a player. Every stretch had gate `no_camera_commit`
with the commit counters standing still, and the last committing camera was
still `follow`. The game was not paused: 207 physics steps a second in the
17.9 s stretch (the game's own rate once the eyes stop), and the hero moved
76 m during it. Each stretch began 0.1-1.2 s after the user's web caught a thug
(bot grabs at 944.7, 945.5, 950.5 and 962.9 s; no fling was asked, `flings`
stayed 0). The last one ended with the game replacing the player (a respawn,
then an autosave at 16:53:22). Prop grabs in the same session, and two bot
flings at 13:36, left VR immersive. In none of the October 6 reports did
`camera_mover` ever read `combat`.

Cause, from the executable. A camera mover's per-frame update is vtable
slot 21 (+0xa8). The camera pipeline calls it at 1e1b7c6 with the mover, its
CameraTarget, the frame's seconds in xmm2 and two flags. FollowCameraMover,
LookCameraMover, LookCameraMoverGame, TurretCameraMover, PerfTestCameraMover
and PhotomodeLookCameraMover share one update (1e1cf80), and it ends by calling
slot 24: the commit (1e1d600) that the input bridge hooks. CombatCameraMover's
update (5c2630, used by no other class) never calls the commit. It places its
camera itself, through SetTranslation (191c590) on the same camera
(`[[mover+8]]`), and reads its target's +8 as the actor record, as the commit
check does. The gate accepted the combat camera's vtable, but no commit ever
came from it. So every fight read as no camera commit for 100 ms, the
condition meant for pauses, loading and cutscenes, and after 250 ms the headset
put the game's frame on the screen. The Miles Morales first-person camera
research (`.research/miles-camera`, same engine) hooks the combat mover's
slot 21 separately for the same reason.

Change: `game_bridge.cpp` hooks the combat camera's update. At start it checks
the update's entry bytes and that the combat vtable's slot 21 points at it
(else 1003). After the game's own update it records the camera exactly as a
commit, with the same validation (the player's CameraTarget, the follow or
combat vtable, finite orthonormal transforms) and the same telemetry. Only the
follow camera's commit takes the camera-offset experiment's write. No protocol
change: bridge Data and XR telemetry are as before, and in a fight
`camera_mover` should now read `combat`. `bridge_game.py` and
`capture_movement.py` check the fifth hook entry too. The fixed bridge is copied
into `dist/Spidy-0.1.0-win64/Spidy-0.1.0` (the old one is staged under its hash
in that folder's `reports/bridge-modules/caea951a…`).

In the game without a headset (`tools/probe_menu_pad.py` start, `pad a
--until-player`, `player --follow`, `stop`), on the user's save (the checkpoint
written after the 16:36 respawn):

- The bridge started (0) with the combat update hooked (its entry a jump).
- Free roam with the player handed over: gate open in 98.9% of 95 samples,
  486 of 486 camera calls on the player, all from the follow camera. Outside a
  fight the combat camera did not update.
- SpidyStop returned 0, and both camera entries were restored byte for byte.
- No bot was in the game (the crime was over), so a fight was not measured
  under the new hook. The save files were unchanged afterwards
  (`reports/backups/saves-20261006-combat-camera/`).

143 core checks, the GPU test and 68 Python checks pass. There are no new
tests: the hook needs the game.

Headset check: start a fight (a crime with thugs). VR should stay immersive,
with webs and punches working and no flat screen. In the report during the
fight, `gate` should be empty, `camera_mover` `combat` and `presentation`
immersive. If a fight still goes flat, its `gate` and `camera_mover` say
which camera it was. Finishers and death use movers that place their camera
from an animation (MeleeRelAnimCameraMover, DeathCameraMover); those still go
to the game screen.

## The body through the game's own moves — current build, not yet run in the game

The same 16:36 session was the body's first in the headset (details:
[BODY.md](BODY.md#the-first-headset-session-october-6-1636)). With the eye
views on, the body was on the hero in all 10,761 samples; at full blend the
head joint sat 0.0 m from its place in every one and each wrist within 1 cm of
its target in 89-96% of them (at most 0.20 m, beyond the arm's reach); the
hero turned between his pose job and the render in 21 samples, at most
0.099 rad.

The defect: for 302 samples (about 16 s, in stretches of up to 8 s) the
body's blend fell from 1 to under 0.1 within a frame and kept starting over
every frame or two, while the game itself moved the hero (a landing after a
web release, ledge climbs, a jump off a roof): the hero showed mostly the
game's animation, arms up to 1.9 m from the controllers. Only a change of rig
in the hero's pose job started the blend over, so his jobs alternated between
rigs. Now each rig keeps its own cache entry and the blend and yaw stay across
them; the eyes hide the hero when his latest job is one the body could not
turn. New telemetry: `body.rig_switches`, `body.hero_jobs_max`, the rig in each
sample, and per-sample `punch` (bots within reach, punches, fist speeds).

No punch landed in that session: its fights were on the flat game screen
(above), where the fists get no tracked input.

143 core checks, the GPU test and the Python checks pass; the fix has not run
in the game yet.

## Your own body and punching — preceding build, measured without a headset

The user: "lets add spiderman actual avatar as a full body presense we control
and allow physical punching". Design, numbers and open items:
[BODY.md](BODY.md).

Measured with `tools/probe_game_body.py` in five fresh game processes on the
user's rooftop save (the hero perched on a vent cap), October 6:

- The hero's rig as its pose job names it (1601290, rig at job +0x28): 237
  joints, every name resolved through its hash (`reports/body-probe.json`).
  `a_body` is the body's root with `pelvis` and the spine under it; the
  identification by name is built in, and found the same rig in every run.
  One hero pose job per render frame.
- Body on, a scripted headset at 1.75 m eye height looking ahead, the left
  controller raised above the head, the right one at the chin 0.5 m ahead:
  both wrists 0.0 mm from their targets in the world, the head joint 0.0 mm
  from its place, the head shrunk to 0.001, body scale 1.031, 0.03 ms of
  solving a frame (`reports/body-probe/ik-third-person.png`: the hero standing
  on the vent with his left hand raised and no head).
- The left eye's image, looking 50 degrees down
  (`reports/body-probe/eye-body.png`): his right arm reaching ahead, the hand a
  closed fist with the thumb across the fingers, the web shooter at the wrist,
  his feet on the vent below, nothing of the head. With the body off the eyes
  hide the hero, as before (`eye-hidden.png`).
- The game's damage system: a blow of 30 (kMelee, kFlyBack) straight to a bot
  126 m away took its health from 100 to 67; 5 (kStagger) to the hero, 110 to
  105, and he was shoved 7.9 m. A scripted fist driven through that bot's chest
  at 6 m/s, as controller samples through the swing module's input, landed one
  punch (5.6 m/s, strength 0.64, kKnockdown, 29.1 damage), issued one request,
  and took the bot's health from 100 to 68 (`reports/body-probe-fist.json`).
  The bot was in `BotStatePlayCinematicGame` and played no reaction.
- Not measured: a free-roaming thug's reaction (none came within reach), and
  the hero's turn between pose job and render (he did not walk off the perch;
  `body.turn_max` in the session report measures it in VR).

143 core checks (18 new), the GPU test and 68 Python checks (2 new) pass.

## Webs as ropes: weight, swinging, the web let go of follows its prop — preceding build, tried in the headset

The user, after trying it (`Spidy-0.1.0\reports\game-vr-20261006-133652.json`,
from the release folder): "i tested in vr and it worked good,only issue is on
release the bins fell into the floor (still visible but partially in it)". In
that session 11 yanks, 5 catches (0.5-0.6 s after the yank), 4 throws at
10.2-16.7 m/s, no web lost. Eye snapshot 0025 shows a thrown blue bin as a thin
sliver in the road; 0023 shows the let-go web lying on the road to it.

The bins, measured in the game without a headset (`reports/grab-probe-rope.json`,
`grab-probe-rope-yank.json`, new `--bodies` option): the thrown bins' bodies
rested on the road (the player stood on it at y 0.85; their centres at
1.20-1.22), so the bodies did not sink: the drawing did. A breakable trash can
is two bodies; the game draws it from the top piece, which is the only body
Spidy's freeing lets move: the can itself (the primary) keeps zero velocity
whatever it is given, held as an unbroken breakable. Within 0.1 s of being
freed the top piece fell 0.74 m through the can and the can was drawn with it,
0.74 m into the pavement; thrown, it landed with the can drawn 0.8 m into the
road. Drawn from the primary instead, the can stood upright and never moved;
that change was reverted. Breaking the prop off as the game's own yank does
(`BustBreakablesWhileWebYanked`) is the open fix; details in
[WEB-GRAB.md](WEB-GRAB.md#breakable-props-only-the-top-piece-flies-open). The
same probe measured the let-go web: its drawn end followed the prop 15.5 m, a
median 0.45 m from it, until it dissolved after 2 s.

The first report of the session, as written before the headset check:

The user: "right now it feels like objects have no weight while webbed and dont
feel like they are being pulled by a web, they feel like the web is a stick
that holds the object (i can hold a bin in the air with a web). when releasing
the web the objects also seem like they barely have any weight. also when
releasing the object the web stays in place attached to last point of contact
with the object as if its still there instead of falling".

The grab samples of the 10:16 session (`reports/game-vr-20261006-101603.json`,
14 grabs, 10 yanks, 5 catches, 5 throws, physics at 0.9-2.2 times real time):

- Two of the four throws reached the 40 m/s cap, (3.2, -25.2, 30.9) and
  (-30.1, 16.2, 20.8) m/s: the held prop moved at 18 m/s or more and the throw
  multiplied that by 2.2. A held prop followed a point 1.35 m ahead of the
  wrist, held up, so a turn of the wrist alone swung it fast. The other two
  left at 5.4 and 1.5 m/s.
- Yanks launched props at 17-45 m/s, mostly upward: (2.3, 26.8, -12.0),
  (9.3, 32.8, 8.6), (-29.5, 32.1, 11.1). The arc aimed where the hold point
  would be after the whole flight, carried on at the hand's velocity, which
  during the pull gesture is the gesture itself. Let go of 0.14-0.41 s into
  the flight (5 times), props flew on at 12-33 m/s, four of them rising at
  12-13 m/s.
- The web line: the game draws the webs. A released rope dissolves with its
  far end at its target position, which Spidy stopped updating when the grab
  ended.

Changes (design and numbers in [WEB-GRAB.md](WEB-GRAB.md)):

- Every web is a tension-only rope with weight on its end: one web pulls with
  at most 2400 N (80 m/s² on the game's 30 kg props), its stiffness and
  damping are rates, gravity always acts. A held prop hangs from the wrist on a
  web 0.25 m longer than its radius and swings; turning the wrist does not move
  it. A target the web brings in is braked to the edge of reach as a hand
  catches it, then caught.
- Throws: the prop's own velocity from the swing, times 1.6 for targets up to
  60 kg (less above), capped at 25 m/s, keeping its own spin.
- Yanks: 7-15 m/s from the pull (5 times its speed), slower for heavier
  targets, aimed where the hand is carried with the player, the jerk no faster
  than 1.5 times that; in flight the web reels in at 0.8 times it and pulls
  only what falls behind, along the web.
- The web let go of: for 1.5 s the grab telemetry (version 2, 336 bytes)
  reports the target as trailing; the XR worker asks for that web with
  `attached` 2; `native_webs` releases the rope keeping its handle (ReleaseRope
  67b610's fourth argument) and aims its far end at the prop each update until
  it has dissolved. A released rope's update still reads that position (679dc0
  → 6786c0 → +67c into the anchor +720), from the disassembly.
- The controller hums by the web's tension (0.6 times it) between event pulses,
  in the game and the lab. The grab telemetry carries each hand's tension.

Offline: a simulation with ground and friction (90 Hz hands, 30 Hz physics):
a can reeled from 12 m is caught after 1.2 s and hangs 0.7 m below the hand;
yanked from 8, 20 and 40 m it launches at 15-20 m/s and is caught at under
1.5 m/s after 0.7-2.7 s, never more than 0.5 m behind the wrist; from a perch
32 m up it is reeled up along the web and caught; an underhand swing throws
a 30 kg can at 13 m/s, 80 kg at 5 m/s; a wrist flick at 0.3 m/s. 125 core
checks (30 for the grab, 10 of them new or rewritten), the GPU test and
66 Python checks pass. Not run in the game: the rope's behaviour on Havok
bodies at VR frame rates, and the released web following its prop.

Headset check: web a trash can and reel it in; it should hang below the hand,
swing, and not stay up when the hand points ahead. Swing it underhand and let
go; it should fly with the swing, not with a flick. Yank one and let go
mid-flight. Watch the web after a throw: it should go with the prop and fall
away. In the report, the grab samples' `tension` and `trailing` per hand.

## Eye occlusion culling — preceding build, awaiting headset check

The user, after the 10:16 session (`reports/game-vr-20261006-101603.json`):
"performance feels not to too good in vr considering that i alt tabbed and saw
that my cpu and gpu were barely hitting 20% load, with the hard drive at 44%
load. at these numbers i would expect 120 fps stable on headset".

The session: 619 s of samples at 3072 x 3264 per eye, the headset at 90 Hz (XR
frames 89.8 a second). New eye pairs 38.0 a second, submissions 83.1, of which
30.0 repeated an earlier pair; the game log's minute frame rate was 41-64 in VR
minutes. Physics step `dt`: median 20.2 ms, 5th percentile 13.1, 95th 32.4.
Frame cost followed the per-frame render memory: 24-37 MB at 57-71 new pairs a
second, 58-77 MB at 30-38. The render ring never overflowed (512 MB, worst two
frames 155 MB). The texture budget was full in every VR minute (3,837-3,871 of
3,874 MB), the source of the disk reads. The last 40 s have the gate at
`tracking` and `no_camera_commit`: the headset not viewing and the game paused,
as it is while its window is not in front, which fits the alt-tab. Earlier
sessions at 120 Hz (`timing` in the 09:22 Oct 6, 09:36 Oct 5 and 18:47 Oct 4
reports): mean XR frame 13.9-14.0 ms, `xrEndFrame` 13.4-13.5 ms of it, 71
submissions a second. The 10:16 report has no `timing`: the game closed first,
and that path does not read it.

In the game, no headset, `tools/probe_vr_load.py` at the user's save (a rooftop
at night), eyes 3072 x 3264 with Quest 3's lens (54 degrees outward, 44 inward,
48 up and down), views 13 as in VR (`reports/vr-load-3072.json`):

| Phase | Game fps | Busiest thread | Main thread | GPU busy | Render MB/frame |
|---|---|---|---|---|---|
| Game alone | 250 | 90% of a core | 65% | 43% | 3.9 |
| VR views, looking ahead | 65 | 95% | 47% | 66% | 31.1 |
| VR views, turning a full circle | 48 | 96% | 40% | 58% | 45.4 |
| VR views, 60-degree lens | 94 | 95% | 45% | 77% | 18.3 |

Across these, frame time is about 2.4 ms plus 0.41 ms per MB of render
commands. The busiest thread does not log; the main thread (the one logging
`[NxApp]`) does. 7,751 instruction-pointer samples of each
(`reports/vr-load-3072-profile.json`): the busiest was in the game 68% of the
time, the NVIDIA driver (`nvwgf2umx`) 19%, D3D12Core 4%, ntdll 7% (3% waiting),
with no function above 6.5%: per-draw work spread over the render code. The
main thread spent 57% of its samples in one wait (`ntdll+164a00`, from
`1bbf3fa`). The busiest is the render thread, and the main thread waits on it.
Spidy's GPU bridge made no difference (63.6 fps without it, 65.3 with).

Cause, from the executable:

- The offscreen view setup `186c1f0` (Spidy's buffer hook returns at its
  `186c325`) calls `18a0020` with 1 as the sixth argument, which becomes the
  pool initializer `189ce20`'s seventh: skip occlusion. `189ce20` sets the
  view's `+1208` to 1 unconditionally, but creates the occlusion object at
  `+1ba0` (0x8c0 bytes, "ViewContext::InitOcclBufferClasses" and
  "InitOcclBufferPointers"; depth target and coverage texture through
  `17f9310`) only when that argument is 0.
- ModelOcclJob (`17935a0`, created in `1793f5b`) tests each instance against
  the view's object (`17fd5b0` near, `17fdec0` far) only when `+1208` is set and
  `+1ba0` holds an object whose `+184` is clear. A dozen other culling functions
  read the same pair (`16b4990`, `16fad46`, `17164c0`, `173a940`, `173e890`,
  `1795110`, `1796d20`, `191f2d0`, `19f0bf0`, `1a072a1`, `1a0f120`). Without
  an object, every instance in the frustum within draw distance is drawn.
- `189fed0` builds the object for a view that has none, from the view's render
  buffers (`+1640`); it stores the pointer before constructing the object. The
  view's display setup (`1920310`) then queues its occlusion depth sample
  (command 0x9c), and its render job (`19206a0`) runs the occlusion update
  `17f97c0` (`OcclReprojJob`), both guarded by `+1208`; the per-view update
  `189bd30` calls `17f8f80` on the object every frame, on the main thread
  during view maintenance. The engine itself sets and clears `+184` each frame
  (`17fa880`, `17f97c0`), for example while the view's camera flags (`+438` bit
  1) mark a discontinuity.

Change: `stereo_probe.cpp` calls `189fed0` on each eye right after creating it
in view maintenance, before any render job has copied it (view option 16,
entry bytes checked). VR uses it unless started with `-NoEyeOcclusion` (XR
option bit 4). The eye sample's former reserved word reports the object, and
session reports carry `eye_occlusion` (eyes with it) in every sample.

With it (`reports/vr-load-occlusion.json`, no GPU bridge, same save and spot;
"before" from the run above):

| Phase | Before | With eye occlusion | Render MB/frame | GPU busy |
|---|---|---|---|---|
| Looking ahead | 65 fps | 125 fps | 31.1 to 12.8 | 66% to 88% |
| Turning a full circle | 48 | 105 | 45.4 to 14.4 | 58% to 82% |
| 60-degree lens | 94 | 138 | 18.3 to 8.2 | 77% to 79% |

In one process (`reports/vr-load-shots.json`, the GPU bridge reading every pair
back, which costs GPU time), the probe took each eye's object pointer away (to
null, which every reader tests) and put it back: looking ahead 88 fps with it,
56 without. Clearing `+1208` instead stops the eye from rendering at all (0
pairs a second): it is no occlusion switch.

Images, left eye at four headings with the culling, without it, and with it
again (`reports/vr-load-shots-eyes/`): render MB 13.2/30.8, 13.4/51.8,
15.4/59.2 and 12.4/33.9 with/without. Pixels whose largest channel moved by
over 48: with against without 0.003%, 0.34%, 0.17%, 0.18%; with against with
again 0.003%, 0.29%, 0.10%, 0.18%. Mean brightness is equal within 0.4%.
Difference maps put the changes on moving cars and people, birds, an animated
billboard and leaves; no building, prop or light is missing.

Not checked: when the eyes resume after a menu or cutscene, their first frame
tests against the depth of the last frame before they paused. Static geometry
still occludes correctly; something that moved meanwhile could hide an object
for that one frame.

Headset check: play as before. `eye_occlusion` should read 2 while immersive;
compare new pairs a second with the 10:16 session (38) in similar places, and
with `-NoEyeOcclusion`. At 120 Hz, see whether submissions reach 120 a second.

Launcher: the user's first launch of this build ended with `Spidy VR
unavailable: [WinError 24]`, before VR started and without a report.
`CreateToolhelp32Snapshot` fails with ERROR_BAD_LENGTH while the process it
lists is loading or unloading modules, and Windows documents retrying it; the
game loads dozens of DLLs in its first seconds, while the launcher lists its
modules to load Spidy's. Only finding the game and the render memory module
retried. `capture_game_state.checked_snapshot`, which every process and module
listing goes through, now retries that error for up to 2 s; any other error
stays final.

Offline: 120 core checks, the GPU test and 58 Python checks pass (new: the load
probe's eye command passes the native eye check, the eye sample reports the
object, the phase summary's arithmetic, and the module listing retrying only
ERROR_BAD_LENGTH).

## Rays through a city block, web-moved props on the game's physics — preceding build, awaiting headset check

The user, after the 09:22 session (`reports/game-vr-20261006-092258.json`):
"game exited to flatscreen mode mid session. also pulling objects with webs
doesnt work well, they get stuck, behave unrealistically".

Flat screen: the session ended with `Tracked swing input: 2002` after 138 s.
The swing module had faulted with 2103 (`swing.error`), a world ray whose hit
collector held 16 hits, and a faulted swing refuses input, which ends VR. The
controller aim rays (100 m) met 10-14 bodies in their last batches, aimed
across a street at a facade 15 m away; the batch at serial 34230 met 16. That
was not an error: past its capacity the game's collector (vtable 3d0a948, add
1804200) replaces its farthest hit with a nearer one (1803fd0 finds the
farthest), so the 16 it holds are the nearest. A full collector is now
accepted. The game log has no crash; the game was closed normally at 09:27.

Props: six grabs, all on two trash cans in the street. The first was yanked,
caught and thrown. The second, beside the subway entrance, was yanked four
times; the first straight flight at the hand stopped after half a metre
against the railing and lamp post, every yank after it gave up as snagged, and
the web went on commanding 3-5 m/s at a can that did not move. Eye snapshots
0015-0018 show it. Since the fifth build the web set each prop's velocity every step from its
own prediction, and flew a thrown prop along Spidy's path, adopting the game's
velocity only after a change of more than 1 m/s: friction and rotation never
counted. Releasing a yank in flight threw the prop at 2.2 times its speed
toward the player (55 m/s at 63.2 s).

The fix, in [WEB-GRAB.md](WEB-GRAB.md): a command is a law the game's physics
thread evaluates against the prop's actual state; Spidy keeps props it moves,
or let go of, in real time with the game's own contacts until they rest; a yank
is an arc, higher where the lower one is blocked; a hold turns the prop with
the hand; only a held prop is thrown.

Offline: 120 core checks (new: a yank lobbed over a railing, a yank let go in
flight, the laws, turning with the hand, one hand of two letting go), the GPU
test and the Python protocol tests pass.

In the game, no headset, Times Square perch, a throwable prop 43 m below
(`reports/grab-probe-physics-yank.json`): caught after 1.70 s along an arc,
carried 0.3-2.0 m from the hand, thrown at 15.1 m/s; in the following 1.94 s it
kept exactly the launch's horizontal velocity and fell at 10.0 m/s² with the
game at 7.4 times real time. No web lost; hooks restored. A second run, to watch
the landing and slide, stopped: the game cannot run in the background, and the
user was using the PC.

## Walking, jumping and web pulls through the virtual controller — October 6 second build, awaiting headset check

The user, after the 08:39 session (`reports/game-vr-20261006-083949.json`, the
first headset session with the seventh build's virtual controller): "i cant
move in game in vr (i can shoot webs but can pull myself or move or jump) menus
work fine".

The session: the player stood at (-305.31, 32.15, -153.30), the Times Square
perch of the save, for all 2,122 movement samples; the head moved only as far
as the user leaned (0.6 m). Webs attached 25 times. Pulls that wanted lift from
the perch made the takeoff press jump (`takeoff_phase` 1, 2, 3), and it gave up
after 1.4 s, 6 times; the collision flags stayed 0x2060001 and the swing
controlled no step. In the 14:39 session of October 5, at the same perch, the
same takeoff flipped them to 0x2060010 about 0.1 s after its press and the
player rose at 11 m/s. The worker did send the keys (W/A/S/D combinations and
Space in 209 samples). The game log has `XInput controller connected`,
`Gamepads connected: 0 -> 1` and `Gamepad for main player: 8` as VR started;
no earlier session's log has an XInput controller.

In the game, no headset, same build and perch; only the input bridge and the
virtual controller loaded:

| Menus played with | Bridge W 1.5 s / Space 0.35 s | Real Space / S (`SendInput`) | Controller A | Controller left stick |
|---|---|---|---|---|
| Virtual controller (as in VR) | 0 m / 0 m (game read the keys 769 and 362 times) | 0 m / 0 m | 2.87 m jump | full 7.42 m, half 1.55 m in 1.5 s |
| Keyboard (Enter), controller never pressed | Space 3.01 m jump | | | |
| Keyboard, then the controller connected at rest | Space 2.99 m jump | | | |
| Keyboard, then one A on the controller | Space 0 m | 0 m | first A 0 m (swallowed), second 2.55 m | |

The device that last pressed a button plays the player; a connected controller
at rest takes nothing over, and key presses after a controller press did not
switch back. VR menus are played with the virtual controller, so in VR the game
ignored every key the bridge served. Takeoff presses jump for 160 ms: a 160 ms A
jumped the player 1.95 m from the street.

The fix: in VR the virtual controller also carries walking and jumping as the
swing leaves them, besides the bridge's keys. Its left stick is the Touch stick
turned into the stock camera's axes, analog (the keys only have 8 directions at
full speed), zero while the swing owns the body; A is the bridge's Space bit,
with the takeoff's release and press. The worker submits the controller after
the swing's sample, as it does the keys.

Offline: 115 core checks (new: the rig's stick in the camera's axes; gameplay
controller with walking and jumping), the GPU test and 56 Python checks pass.

## Web grab: catching, yanking, carrying and throwing props and thugs — preceding build, awaiting headset check

The user asked: "design and implement a system like the one in the game where
i could web objects and npcs and throw them around". Design, tuning and the full
measurements are in [WEB-GRAB.md](WEB-GRAB.md).

Offline. `spidy_tests.exe` passes 114 checks, 20 of them for the grab (core and
lab); `spidy_graphics_test.exe` passes; the Python protocol tests pass 56,
including the grab telemetry's layout. Simulated tuning: a critically damped
hold spring carried a target 12-18 cm past a hand that moved 1 m in 0.2 s and
stopped; the first-order follower that replaced it, 4-6 cm.

In the game, no headset (`tools/probe_game_grab.py`, Times Square, the player
perched 32 m up; `reports/grab-probe.json`, `grab-probe-yank.json`):

- A throwable prop's bodies are static (flags 0x1, motion 0, category 0x43f
  including the web bit), so before this build a web aimed at one swung from it.
- Freed with SetFreebody and SetMode debris alone, its bodies moved (6.5 m up in
  0.5 s) while the prop was still drawn where it had stood: it had no keyframe
  record. A physics rebuild gives it one, and the game draws it after its body.
  Moved in the steps before the game's first sync, it was drawn 5 m off; three
  still steps after the rebuild fixed that.
- The game steps Havok by a fixed 1/30 s per frame (609a560; TimeScaleSystem
  owns it). At 240 frames a second a released prop fell 32 m in 0.3 s, about 64
  times real gravity. With the ratio measured each step and props flown in real
  time until they rest, a thrown prop crossed 44 m in 2.4 s.
- Reel: 43 m in 4.25 s; caught, carried 0.8-1.6 m from the hand around a 0.3 m
  circle; thrown at 19 m/s. Yank: 43 m in 1.77 s, nearest 1.09 m to the hand,
  thrown at 17 m/s. No web lost in either run; the hooks were restored.
- Earlier runs failed in ways now fixed: a prop's own body cut its web after
  0.3 s; ticking the grab twice per physics step gave half the pull; a slack web
  let the game's 8-times physics drop the prop and the web snapped it back (a
  yo-yo of +-45 m/s); a fixed 1.5 s yank timeout stopped 8.5 m short.

Bots: the components (BotMoverManagerGame +0xdb4 names the bot's MoverStandard)
were read in a live game, and the RequestState slot and the flung reaction's
entry points offline. Not verified in the game: the only bot in free roam was
160 m away and its mover did not step. Pedestrians have no physics and are not
offered.

## The player after a reload, VR from the game's start, menus with the VR controllers — preceding build, awaiting headset check

After the 21:12 session the user reported "game always stays flat", then asked:
"also add ability to navigate menus in vr and start the game in vr from the get
go".

What the report shows (`game-vr-20261005-211234`). 4,330 frames went to the
headset in 68 s, every one the game screen; `tracked` stayed 0, so the gameplay
gate never opened, and no swing or motion sample exists. The eye snapshots show
the session: Peter Parker at F.E.A.S.T. (a story mission), the pause menu, a
loading screen at 31-41 s, then Spider-Man in free roam at Times Square from 46
s, still on the screen. The game log has the load at 21:14:13.

Cause. The launcher found the player once, from outside the process, and
started the input bridge with that hero and actor record. The bridge accepts a
camera commit only when its target is that record. Loading a save, restarting a
checkpoint or switching characters replaces the player's actor and components,
so from the load on no commit could match. Measured in the game without a
headset (`tools/probe_menu_pad.py`, the user's save at Times Square):

- After a checkpoint restart the old hero, record and mover were gone and new
  ones appeared 6.1 s later. With the old target the follow camera committed
  470 times in a second and none matched: the 21:12 session from 46 s.
- Handed the new player (`SpidyRetarget`), 496 of 496 commits matched and the
  gate's conditions held in 99% of samples; after the first load, 500 of 500.
- The Peter section before the load had the player the launcher found, so it
  failed for another reason. The session has no camera data; Peter's camera is
  most likely one the gate does not accept (the commit method also belongs to
  LookCameraMover, LookCameraMoverGame and TurretCameraMover) or one that does
  not commit. XR telemetry now records it.

The game pauses while another window is in front: started from Steam behind the
launching window, its intro sat still for three minutes, drawing black, and it
read no controller. In front, it played on at once.

Changes:

- `game_player.cpp` finds the player in-process from the component registry, as
  `capture_game_state.py` does from outside: exactly one hero (vtable
  `38a93c8`) with a valid actor transform, exactly one HeroMoverManager on the
  same record, and its MoverStandard by handle. A thread keeps it: a live
  player is re-checked every 100 ms, a lost one searched for every 250 ms (7-9
  ms per search). Same hero and record as the outside search, both times.
- The VR worker hands each new player, or none, to the input bridge
  (`SpidyRetarget`, new), the swing module (`SpidySwingRetarget`, which resets
  its solver and retargets the movement module, `SpidyMotionRetarget`), the
  game's web lines (`native_webs::retarget`; rope handles reset with the rope
  manager) and the avatar hiding; the tracking rig and image history start
  over.
- VR starts with the game. The launcher waits for the renderer (30 frames
  through the render memory module, then exactly one direct queue), starts the
  input bridge without a player, and starts the VR worker with none (config v6).
  The GPU bridge, and with it the game screen, starts when the headset is
  focused; the eye views start with the first gameplay, as before the sixth
  build. No 30-second headset deadline: VR waits for the headset. Measured: the
  renderer was ready 8.8 and 10.3 s after launch.
- `game_pad.cpp` serves controller 0 through the game's XInput
  (`XInputGetState` and `XInputGetCapabilities` in whichever of xinput1_4,
  1_3 and 9_1_0 is loaded; the game loads the DLL itself). On the game screen
  the Touch controllers map to Xbox buttons, sticks, triggers and bumpers (the
  grips); in VR only the menu button (Start) and Y (Back) pass. A real
  controller in slot 0 is merged. Once served, the controller stays connected.
  The OpenXR session binds B, X and the left menu button. A held from the screen
  does not jump.
- The launcher brings the game window to the front (`bring_to_front`).
- XR telemetry v6 (648 bytes): `gate` (no_player, bridge_stopped,
  no_camera_commit, other_camera, tracking), `camera_mover` (the last committing
  camera's class), `camera_commits` and `player_commits` (both rising under
  other_camera: a second camera commits after the player's every frame),
  `players`, `player_record`, `pad_buttons`, `pad_installed`, `pad_reads`.

In the game, driven only through the virtual controller: A presses every 2 s
took the game from its intro logos through the title and main menu to a loaded
save (39 s from launch to player with the window brought forward by the
launcher's function; 13 s from the title when brought forward by hand). Start
opened the pause menu, with Xbox prompts; D-pad down and two flicks of the left
stick each moved the selection one item; A and the confirmation restarted the
checkpoint; Back opened the game menu (map, LB/RB tabs, LT/RT zoom); B closed
it. The game read controller 0 about 2,500 times a second while in front, and
not once while behind another window.

Validation: 94/94 core checks (new: the controller mapping in menus, in VR and
without the headset, and bad readings), 26 + 13 + 11 + 5 Python checks (new:
XR telemetry v6 with gate reasons, camera names and commit counts; VR waits for
frames and one queue). Saves: the game rewrote `slot0-s.save` with its own autosave on
Continue; the copy from before is in `reports/backups/saves-20261005-menu-pad/`.

Headset check: start with `Launch Spidy VR.cmd` with the game closed. The
headset should show the intro about ten seconds after launch; play the menus
with the controllers, Continue, and VR should start with gameplay. Pause with
the menu button and resume with A; open the map with Y. Restart a checkpoint or
die once: VR must come back after the load. In the report, `players` counts the
players found; a scene that stays on the screen while you play shows its reason
in `gate` and its camera in `camera_mover`.

## Game screen for menus, hint cards and cutscenes — preceding build

After the 19:36 and 19:40 sessions the user reported: "main menu, cutscenes and
i think stuff like damage indication overlay or spidey senses are causing the
screen to go black (in vr). also everything looks a bit to dark (or too bright)
like an extra ambient occlussion or something is active", and then that the
session's eye snapshots look brighter than the headset.

What the reports show. The VR loop had a picture only while its gameplay gate
was open: a camera commit (`1e1d600`) from the follow or combat camera mover,
less than 0.1 s old, with valid player and camera transforms. Closed, every
frame went out with no layer, which is black. In `game-vr-20261005-193658` it
was closed for 7.9, 12.2, 4.7, 5.8 and 16.2 s; in `-194057` for 4.4, 2.9 and
22.4 s. Aligned with the physics samples by time:

- Four stretches of the 19:36 session ran no physics step at all: the game was
  paused, while render memory shows it still drew about 5 MB a frame of its own
  view. The opening mission's hint cards (Spider-Sense, health) pause the game
  until dismissed, which fits what the user took for spider sense and damage
  overlays.
- The last stretch at 19:36 and the one from 21.5 s at 19:40 ran physics at 240
  steps a second with the player standing: the game's own rate once the eyes
  stopped, in a cutscene.
- 17-21 s at 19:40 ran physics at the usual rate with the player walking
  indoors at 5 m/s: a camera that does not commit.
- The commit method (vtable slot `0xc0`) belongs to FollowCameraMover,
  CombatCameraMover, LookCameraMover, LookCameraMoverGame, TurretCameraMover
  and PerfTestCameraMover; the gate accepts the first two.
  MeleeRelAnimCameraMover, RelativeAnimCameraMover, DeathCameraMover,
  ExteriorCameraMover, VehicleCameraMover, PhotomodeLookCameraMover and the
  cinematic cameras never call it.

In the game without a headset (`tools/probe_game_screen.py`, the user's save
at the Fisk construction site; reports `game-screen-2` to `-5`):

- The eye views kept rendering in the pause menu: 139 new pairs in 2 s, against
  117 in play.
- They carry the HUD but not the pause menu, subtitles, or world markers (enemy
  reticles); the game draws those into its presented frame only. An eye copying
  the stock camera showed the paused scene without the menu or its dimming
  (luma 81 of 255, the window 36).
- Brightness, same camera: eye 81.8, window 83.9 (linear light 0.170 against
  0.174); darks -0.5 to -1.6 levels, midtones -3. Alpha was 255 in every pixel.
  The session snapshots are the exact bytes copied into the
  `R8G8B8A8_UNORM_SRGB` swapchain, so a darker look in the headset arises after
  that handoff, which receives standard sRGB content with opaque alpha. This
  build does not change it.

Changes:

- Without gameplay the last immersive image stays up for 250 ms
  (`GameScreen`), then the headset shows the game's presented frame on a level
  virtual screen 2.5 m ahead (`screenAhead`, heading only), until gameplay
  returns. The eye views get no command meanwhile and stop rendering.
- `native_eye_gpu.cpp` hooks DXGI Present and Present1, whose addresses come
  from a throwaway 64-pixel swapchain on a hidden window. While the screen is
  wanted it copies the back buffer on the game's render queue before the
  present. The swapchain reports a Streamline-wrapped queue, not the one the
  game renders on (first attempt: no copies), so the game's swapchain is
  recognized by its back buffer's device.
- `D3D12Renderer::blit` draws the copy into the left eye image for the quad.
  An sRGB target gets the stored values unchanged; float frames are encoded.
- `game_xr.cpp`: native views start when the headset is focused, not only in
  gameplay; the first-image watchdog counts new images, from each start of
  gameplay; the pose history survives gameplay gaps (cleared on focus loss and
  recentering), and held images age by the frame's display time. XR telemetry
  v4 (592 bytes) adds `presentation` and `screen_submitted`. The flat-screen
  toggle places its screen level too.
- The GPU capture's readback range overran the buffer by the last row's pitch
  padding when a row was not a multiple of 256 bytes (1290 pixels here); every
  freeze failed with 3904. VR sizes were multiples and unaffected.

Validation: 93/93 core checks (new: the screen's hold, entry, return to
gameplay and clock reversal; level placement at five pitches, with roll), the
GPU test at 1536 x 1536 (new: 8-bit, 10-bit and float frames drawn into an
sRGB typeless target within 1 level; debug layer clean), and 53/53 Python
checks. In the game (`game-screen-5`): 118, 137 and 123 copies of the
presented frame in 2-second phases of play, pause menu and resumed play
(R8G8B8A8_UNORM, 1290 x 540), each identical to the window to the byte; all
entries restored. The draw into the headset's swapchain and the quad were not
run in the game: they need the headset. The game re-saved its autosave and
preferences on each Continue, as in the user's sessions;
`reports/backups/saves-20261005-game-screen/` holds the files from before.

Headset check: open the pause menu and the map, dismiss a hint card, watch a
cutscene, die once. Each should appear on a screen ahead, and VR resume with
play. The report's `presentation` shows `game_screen` for those stretches and
`screen_submitted` counts the frames. The screen has the desktop window's
resolution (1290 x 540 with the launcher's small window); `-FullDesktopView`
sharpens it at the cost of rendering that view at full size in VR.

## Render memory ring, held images, memory headroom — preceding build

After the 14:39 session with the fourth build the user reported: "webs are
fixed! still got the artifact i got before - black flickering and quads all
across my vision, objects, building and ground dissapearing, geometry randomly
exploding into impossible polygons", and then "the game crashed on the second
spike of those". The session's report is `reports/game-vr-20261005-143902.json`,
with the game's log and the left-eye images beside it.

What the report shows. In three stretches (220-260 s, 350-390 s, and 461-495 s
into the session, after which the game crashed) the game copied eye render jobs
and never rendered them: 60-87 a second, 3,047 of the session's 22,379 copies.
In those stretches new eye pairs arrived 11-20 times a second while the game's
own frame rate rose to 48-56, and Spidy ended 2-19 headset frames a second
without an image. The second stretch has none of those; the user was standing
still. In the session's 329 s of driven flight no physics step ran without its
movement command, so the lease described in the next section was not involved.

The cause is the game's frame allocator, "RenderAlloc" (the object at `7938880`):

- `1872d90` creates it (its only call is `188a19c`): a 128 MB ring, reserved and
  committed at once. `1872930` (316 call sites) and `1872860` (35) allocate from
  it with an interlocked add. `1872b90` (only call `187e623`, inside `187e320`)
  rolls it over at the end of each frame. The frame that just ended stays
  allocated while the render thread draws it, so a frame may use the ring minus
  what the frame before it used.
- A request that does not fit sets the overflow flag (`+0x41`). `1872930` then
  returns null. `1872860` falls back to the heap and queues the block for
  release two frames later (`1c5dab0`; flushed by `1c5e900` from `175a1a4`).
  With a null result `19206a0` skips the view and `1793be0` the actor's job.
- The stock game uses 6-8 MB a frame. With the two eye views and the game view
  moved to the head, frames used 47-64 MB swinging at Times Square with
  512-pixel eyes and the same with 1536-pixel eyes: the memory holds draw lists
  and commands, not pixels. Two such frames are 119-127 MB of the 128.
- Refused requests still advance the usage counter. One sampled overflow frame
  had 526 MB requested of the 128 MB ring, and the game's own two-frame record
  reached 1,223 MB.

Read from the code and not observed running: of the 316 call sites of
`1872930`, a heuristic pass found the result tested before use at 289; two of
the others pass it straight to `memset` (`1857a9e`, `185b158`). The rollover
takes the usage counters as the extent of the frame, so after a frame with that
much refused it leaves the next frame no region at all, and computes the region
after that from a null pointer (`first - ring` at `1872ca4`), which gives a size
unrelated to the ring.

Why the ring is replaced where it is created and nowhere else. Render commands
refer to frame memory by 32-bit offsets from the ring's base (encoded at
`179db08`, decoded at `177059c` and `1770a13`). A ring moved at a rollover
crashed the game in the decoder (`17706db`). The address space for 4-6 GB around
the ring had no free gap of 16 MB, so the ring cannot grow in place, and no
second arena fits within reach of an offset.

Changes:

- `spidy_render_memory.dll` (`src/native_render_memory.cpp`) hooks `1872d90`.
  After the game's own creation, and only if the allocator is exactly as
  creation leaves it, the hook allocates a 512 MB ring, points the allocator at
  it, and releases the game's. A hook on `1872b90` counts frames, frames with a
  refused request, and the largest frame and pair of frames. Signature checks
  cover both functions and their call sites. `Data.status` says which ring the
  game is on. The ring outlives the session that installed it, and a later
  session on the same game recognizes it.
- `tools/vr_launcher.py` looks for the game process every 50 ms and loads that
  module as soon as the process exists, before it opens the game for anything
  else. The game creates the ring 3.4 s after its process is first seen
  (kernel32 and user32 are loaded at that point, d3d12 0.4 s later, the GPU
  driver 1.5 s later).
  Loading is retried while the new process still refuses module listings. Run
  as a script, the launcher starts the game the same way for checks without a
  headset.
- `run_game_vr.py` records `render_memory` in the report, samples
  `render_frame_mb` and `render_overflow_frames`, prints which ring the game is
  on, and warns when it is the game's own (a game that was already running).
- Images. `XrRuntime::frameStereo` ends a frame with no layer when the draw
  callback has no image. `copyLatest` refused a staged pair older than 150 ms,
  and `NativeEyeHistory::find` refused its pose, so at 11-20 new pairs a second
  the headset went black between pairs. Both now hold the last pair for
  1000 ms (`imageHoldMs`). Controls need a new image within 500 ms
  (`controlHoldMs`; before, any submitted image within 250 ms), and the
  three-second stall watchdog counts from the last new image.
- Memory headroom. `run_game_vr.py` warns before it starts the game when
  Windows can promise less than a session takes (19 GB, less what a game that
  is already running holds), and records `free_commit_mb` at the start, with
  every sample, and at its lowest.

In-game checks without a headset, RTX 5090. Each run used a freshly started
game, which was closed afterwards; all hook entries were restored and the saves
stayed byte-identical to the copy taken before. "Release stress" is a scratch
variant of `probe_web_frames.py`, not in the repository, in which both hands
shoot and release game webs every 0.9 s during the swing. All runs have three
views (`--views 13`); the eye images are 90° wide unless noted. Frame counts
start when the module does, so they include menus and loading.

| Report | Ring | Test | Frames that did not fit | Eye jobs lost | Most used by two frames |
|---|---|---|---|---|---|
| `web-frames-6` | game's, 128 MB | `probe_web_frames.py` | 0 of 14,085 | 0 of 345 | 119.4 MB |
| `web-release-7` | game's | release stress, 110° eyes, 8 s swing | 0 of 14,183 | 0 | 127.2 MB |
| `web-release-8` | game's | release stress, 120° eyes, 8 s swing | 13 of 13,609 | 24 of 395 | over the ring |
| `web-frames-5` | Spidy's, 512 MB | `probe_web_frames.py` | 0 of 23,289 | 0 of 345 | 121.6 MB |
| `web-frames-7` | Spidy's, final build | `probe_web_frames.py` | 0 of 14,817 | 0 of 369 | 120.0 MB |
| `web-release-10` | Spidy's, final build | release stress, 120° eyes, 6 s swing | 0 of 22,666 | 0 of 305 | 136.6 MB |

Two earlier runs of the 110° stress on the game's ring, before the allocator
was instrumented, lost 15 and 8 eye jobs (`web-release-5`, and `-6` with
1536-pixel eyes); `web-release-7` is the same test fitting with under 1 MB to
spare. A first run of the 120° stress on Spidy's ring (`web-release-9`) was
stopped after 5.7 s by the test's own guard on Windows commit, with 138.3 MB as
the most for two frames and no job lost. In the final build, a second start of
the module in the same game reported Spidy's ring, 512 MB, with its counters
restarted, and stopping it restored both entries.

The crash. The game's log ends with an access violation at `0x1199cfbb`. The
log prints the low half of the address only; with the module at `7ff70fd40000`
that is `Spider-Man.exe+1c5cfbb`. No minidump was written. `1c5cf90` takes a
block from a pool's free list, and the faulting instruction reads the block's
link to the next (`mov rcx, [rax]`); its callers are the game's small-block heap
(`1c63770`). So the head of a free list was not a readable address: the heap's
bookkeeping had been overwritten, or memory it counted on was not there. Two
conditions of that session can lead there, and which one did is not established:

- Render memory overflow, above. The game is not built to run in that state.
- Windows had no memory left to promise. The game's log gives 14.0 GB available
  of a 65.5 GB commit limit when the game started. Its once-a-minute memory line
  shows the process holding 14,968 MB a minute in, 16,721 MB a minute later, and
  17,112 MB at the end. At the crash the limit was 67.5 GB (Windows had enlarged
  the page file) with 0.6 GB available. In a separate experiment a job limit on
  the game's commit made it log `CreateHeap ... failed` (`0x8007000e`) and
  freeze (`reports/memory-limit-1.json`); the user's log has no such line, so no
  GPU allocation failed outright, which does not rule out a failed allocation
  elsewhere. On the test PC other programs held about 51 GB of commit with half
  of the 61.6 GB of RAM free, and the page file was 4 GB.

Validation: 91/91 core checks (new: the allocator's fields as creation leaves
them, as replaced, and as a later session finds them; a recent new image as the
condition for control; the hold boundaries of the pose history), the GPU test
at 1536 x 1536, and 53/53 Python checks (new: the module is loaded once per
process before the game is opened, retried while the process starts, and not
retried after its own refusal; the render memory reader; the memory warning).

Not verified: everything in the headset. Whether the break-up is gone; how a
held image looks while the game is slow; whether 512 MB covers every scene
(the report's `render_memory` gives the most two frames used and the frames
that did not fit); what caused the crash. One left-eye image of the 14:39
session (`0168`) shows a black polygon 0.72 s after a web release; the release
stress did not reproduce it in 60 releases, and it is unexplained. A session
attached to a game that is already running stays on the game's ring: it only
warns.

## Eyes placed before the frame's render jobs, reel momentum — preceding build, webs confirmed by the user

After a session with the third build at 12:05 the user reported: "offsets still
happens and game began breaking up after a few minutes of gameplay". That session
left no report. `run_game_vr.py` called `SpidyXrKeepAlive` while the game was
closing; the call raised `OSError`, and the launcher exited before writing.

Why the web still started away from the hand. The frame function `175a060` runs
in this order (disassembly; call sites in parentheses):

1. `18a13c0` (`175a42f`): for every pool view and the eight offscreen slots,
   `189ee00` copies the view's descriptor `+0` to `+0x530` and the old `+0x530`
   to `+0xa60`. Previous camera = current camera.
2. The gameplay update phases: hero movement, the hero's rope update
   (`95f6c0` → `676dd0`), and the stock camera's submit (`1646e10` → `1899ab0`).
3. `1920240` (`175aaa9`): for each offscreen slot of the manager at `7a34dd0`,
   `186d050` → `19206a0` → `19223e0` copies the whole view (0x1f70 bytes) into
   its render job. The pool views follow.
4. `187e320` (`175acb4`) → view maintenance `18a0bb0`.

Spidy submitted the eye poses in its maintenance hook, step 4, after step 3 had
copied the views. Two consequences:

- Every eye image was rendered from the previous frame's head and player
  position. The game's web was built in step 2 from the current frame's hand,
  so in the eye it started one frame of travel ahead of the hand: 0.4-0.7 m at
  20-32 m/s and 15-33 ms frames, and different in every frame.
- At the next step 1 the pose became its own previous camera. `186e520` fills
  the per-view shader constants `GlobalViewportCBuffer` from that history
  (`m_PrevCamWorldToClipMat` at `+0x80`, `m_PrevCameraPos` at `+0xc0` from the
  previous descriptor at view `+0x530`), so for both eyes the previous camera
  always equalled the current one. What that did to the image (anti-aliasing
  history, motion blur, reflections) was not measured.

Changes:

- `stereo_probe.cpp`: a hook on `1920240` places the eyes (`placeEyes`: latch the
  pose command, anchor it to this frame's hero sample, set lens and pose, submit)
  before the original copies the views. Maintenance only creates and retires the
  views. `SpidyEyePlacement(1)` restores the old placement for comparison.
  Signature checks cover `1920240`, its call site `175aaa9`, and `189ee00`.
- Eye frame telemetry v4 counts, per eye job copy, whether the pose was placed in
  that frame (`same_frame_poses` / `late_frame_poses`) and whether the previous
  camera differs from the current one (`history_moved` / `history_still`).
  Appearance v5 adds `web_hand_gap_*`: the distance, in each presented image,
  between the wrist the overlay draws and the game rope's first point.
- `Swing`: a taut rope that is being reeled now carries the body's velocity at
  the winch rate; the pull used to move only the position. With two taut ropes
  the pass over both keeps what each has pulled and lets a rope hand back a
  surplus (accumulated impulses); kept, the surplus pushed the body sideways
  every step. The speed two ropes carry together is followed exactly up to a
  right angle between them and held at that value beyond it, because it grows
  without bound as they come to oppose each other. Winches that reach the span
  between their anchors now share the remaining length instead of the left
  hand's taking it. A rope within 5 mm of taut is treated as taut, so float
  rounding no longer decides which of two ropes acts on the velocity.
- `Swing::settleStep`, called by `game_swing.cpp`: a prediction covers a native
  step whose length is unknown until the next observation. The rope was wound for
  the previous step's length while the body travelled for the real one. The rope
  is now wound for the difference, at the rate the winch really ran (none at the
  shortest length).
- `game_swing.cpp`: the movement command's lease is 150 ms instead of 50 ms. The
  command is submitted in one frame and applied by MoverStandard's prequery in
  the next, so with 50 ms (47-62 ms in practice, the lease is measured with the
  16 ms tick count) any slower frame ran its step without the command. The game
  then moves the body at the fall speed it derives from the time spent airborne.
  In `reports/web-frames-3.json` two such steps occurred 0.9 and 1.1 s after
  takeoff, at 27.5 and 32.6 m/s downward: the body dropped 0.9 m and 1.4 m in
  one step, the solver pulled it back at the 32 m/s cap, and the velocity
  alternated for about five steps. The morning headset sessions each contain
  one such place in their retained samples (10:25: one step at 20.6 m/s down in
  321 s of driven flight; 09:36: two steps in 156 s, one sampled at 42 m/s
  down, speed 16 to 43 m/s). 150 ms covers two steps of the longest length the solver accepts
  (50 ms), because one missed observation is tolerated.
- `run_game_vr.py`: the report is written however the session ends (game closed,
  Ctrl+C, console closed, a failed stop). It copies the game's log to
  `<report>-game.log` and parses its memory lines into `game_memory`. The left
  eye as presented is copied without blocking (`D3D12Renderer::capture` /
  `captured`, `EyeSnapshotSchedule`) and saved to `<report>-eyes/`.
- `probe_game_swing.flight_summary` reports `steps_without_command`: native
  steps inside controlled flight that ran without the command.

In-game checks, no headset, RTX 5090, game closed afterwards and all hook entries
restored:

- `probe_eye_frames.py` at the main menu (`reports/eye-frames-menu.json`). Old
  placement: 658 eye job copies, 0 with a pose placed in that frame, previous
  camera equal to the current one in all 658. New placement: 657 of 657 placed in
  the frame, and the previous camera differed in 193 copies, exactly the 193 pose
  commands sent.
- `probe_web_frames.py` in free roam, final build (`reports/web-frames-4.json`):
  one 100 m web, reeled, speed cap 32 m/s, gravity 6, a game web held 0.6 m in
  front of a sideways eye camera. Old placement: the web's first point was a mean
  0.455 m (max 0.605 m) from the expected point over 90 frames at a mean
  19.6 m/s; mean frame travel was 0.453 m. New placement: mean 13 µm, max 23 µm
  over 218 frames at a mean 31.6 m/s (0.60 m of travel per frame). In the saved
  left-eye images the web ends on the marker with the new placement and misses it
  with the old one. `web_start_error` and `hero_lag` stayed 0.0 m.
- Flight in the same run, per native step: the second difference of achieved
  velocity (an isolated jolt) had a median of 0.006 m/s and a 90th percentile of
  0.029 m/s; the run before the reel changes (`reports/web-frames-1.json`) had
  0.111 and 0.524, with 1.3-2.8 m/s jolts where the frame time moved by about a
  millisecond, and lost 16.4 m/s in the step that released the web. The final
  build lost nothing at release. Starting the reel is still a 16 m/s step, as
  designed.
- The lease: one driven step of that run took 62 ms of wall time, longer than
  the old lease, and no step ran without the command. The run before the change
  (`web-frames-3.json`, the same build otherwise) had the two dropped steps
  described above.
- The saves were byte-identical to the copy taken before the probes.

Validation: 90/90 core checks (new: release while reeling keeps the winch speed;
two reeling webs carry the body at the geometric speed and keep it on release;
two winches never exceed 1.5 times the reel speed or teleport the body from any
of six starting heights and three reel combinations; a rope at its shortest
length ignores frame time corrections; reeling stays smooth with 16-28 ms frame
times in a simulated one-step-late mover; eye snapshot schedule and projection),
the GPU test including the nonblocking eye copy, and 48/48 Python checks.

Not verified: everything in the headset. In particular whether doubled webs are
gone (the repeated-frame doubling of the whole scene at 36-50 new pairs per
second remains), how the corrected previous camera changes the image, and how
reeling with momentum feels. "The game began breaking up" is not explained with
certainty. The expired movement lease fits it: a metre-sized jerk of the whole
world on every frame slower than about 50 ms in mid-swing, more often as the
game slows down or hitches. But no report exists for that session, so the user's
description has to confirm it. The 12:05 game log, read before the game was
started again, showed about six minutes of VR at 44-51 frames per second,
texture usage at its 3874 MB budget, a normal quit, and no crash, GPU, or
low-memory event in the Windows logs. Windows had about 15 GB of commit left
with the game closed, and the game commits 15-17 GB in a long session, which is
another possible cause but not an observed one. In the next report,
`flight_summary`-style counting of steps without the command (compare `steps`
with `controlled` between driven `motion_samples`), `game_memory`, and the eye
images are there to settle it.

Outcome, from the 14:39 session with this build: the user confirmed the webs
fixed. The break-up continued, and the movement lease was not its cause; that
session had no step without the command in 329 s of driven flight. The cause
was the game's render memory, in the section above.

## Grip-only webs, straight game webs, in-flight steering — preceding build, web offset remained

The user reported from `reports/game-vr-20261005-102512.json` (874 s, second
build): web starts still offset at high speed and seen double, more after pressing
the trigger mid-swing; webs sometimes shooting again and again mid-swing without
releasing buttons; and after a physical yank, the whole world shaking until
landing. They asked for webs to shoot on a grip press instead of trigger plus
grip.

The report's swing, motion, and XR sample lists are separate 12,000-entry queues
appended at different rates, so their indices do not line up. Align them by the
shared command serial (XR `serial`, swing `serial`), and convert swing/motion
`qpc` at 10 MHz.

Session findings:

- Shake after a yank. `native_movement.cpp` applies the leased command at each
  MoverStandard prequery, and `game_swing.cpp` observes that prequery's start
  position and the previous step's achieved velocity, then submits the next
  command. That command takes effect one step later. Steering from the observed
  state made each command a function of the command two steps earlier, so even
  and odd steps formed two independent trajectories. Every owned flight showed
  them: achieved velocity alternated between, for example, (10.0, y, 28.5) and
  (10.1, y + 1.9, 27.6) m/s, swapping whenever the sampler skipped a step. Each
  trajectory received gravity only every other step and through the step-average
  velocity, so vertical speed fell at 5-6 m/s² with gravity configured at 18. A
  yank adds its impulse in one solve, so it reached only one trajectory, and
  nothing re-coupled them in free flight until landing returned control to the
  game. Simulating that pipeline with a yank reproduces it: vertical speed
  alternates between about 11.5 and 0.5 m/s on consecutive steps, and gravity
  acts at a third of its setting.
- Web start offset. `hero_lag` stayed 0.0 m over 41,378 frames: the render
  transform did not move between the rope update and view maintenance, so the
  second build's shared hero sample changed nothing. `web_start_error` (rope `+1c`
  to the requested start) began near 0 at each rope creation and grew to
  0.42-0.45 m within about a second, also while standing. `679dc0` builds a point list at
  rope `+1c` (count `+6ac`). When Grip (`7d78`) is non-zero and finite, it passes
  two hand points to `HeroRopeManager` `vtable+0x78` (`95f9c0`), which for swing
  ropes (type 2) appends `lerp(Start, Grip2, t)` and `lerp(Start, Grip, t)` as
  points 8-9 and rebuilds points 0-7 as a tail from point 8 along minus the
  hand's world velocity (`7df8`, from `95f6c0`) and down, each segment
  length-limited. The tube runs through the tail, the hand, then the main line to
  the anchor (`+720`). At swing speed the tail streams about 0.45 m behind the
  wrist; in the headset it read as an offset start and a second strand.
- Repeated shots. With trigger and grip held, `Swing::inputs` re-shot every
  0.15 s after any automatic release or miss. The line from the body to the
  anchor releases a web at the first hit anywhere along it, and swinging along a
  facade it grazed ledges and sills next to the anchor. The swing samples hold
  144 attachments, 22 obstruction releases, and 3 misses; 16 of the 22 releases
  were followed by a new attachment within 0.5 s, 8 of them before the next
  20 Hz sample.
- The second build's eye job reclamation held: 41,467 generations, 1,026 dropped
  copies reclaimed, no presentation stall. New pairs still arrived at 44.4 per
  second, and 36.5% of the 70 frames per second sent to the headset were repeats.

Changes:

- `include/spidy/game_swing.hpp` `InFlightStep`, used by `game_swing.cpp`: the
  solver starts from the observed position plus the displacement of the command
  in flight, at the solver's own end velocity for that command. When the next
  observation shows a command lost speed to collision, the motion into that
  surface is projected out once, which cannot double-count one contact. Takeoff
  still follows measured native progress.
- `game_swing::physicsConfig`: gravity 6 and air acceleration 4 m/s², the values
  the earlier builds applied in practice, so the swing feel the user approved is
  kept. Yanks now apply their full configured strength.
- `Swing`: a grip press (above 0.65, released below 0.35) shoots; the trigger
  only reels, after a release if it was held at the attachment. There is no
  retry while held. A hit within max(1.5 m, a tenth of the rope) of the anchor,
  or within the body radius, is not an obstruction, for shooting or holding. A
  held web releases only after 0.15 s of continuous obstruction.
- `native_webs.cpp`: Grip is written as (0,0,0), so `679dc0` keeps one hand
  point, skips the hero override, and runs the rope straight from the tracked
  Start to the anchor. Grip2 and End are written as Start.
- OpenXR action names now read "Reel web" (trigger) and "Shoot and hold web"
  (grip); the lab's console help matches.

Validation: 83/83 core checks (the held-gesture retry test now asserts the
opposite; new checks cover grip-only shooting, no re-fire while held, a ledge
beside the anchor, a passing versus a lasting wall, a simulated one-step-late
mover with a yank followed by free flight at configured gravity with no
alternation, and a wall contact removed exactly once), the GPU test, and 39/39
Python checks. The tail removal follows from the disassembly; that the rope now
starts at the wrist, and how the new controls and corrected gravity feel, need
the headset. In the next report, `web_start_error` should stay near 0 m.

## Small desktop window for VR launches — previous build

The user asked to drop the flat-screen mirror if that saves performance. The
desktop view cannot simply stop: the engine runs occlusion (`OcclDepthBufferSample`,
`OcclDepthBufferReadback`), key-light shadow setup, and the luminance histogram
for its active view, and the eyes copy that view's exposure (`+1708`) every frame.
Its pixels are not needed. The game's settings (`HKCU\Software\Insomniac
Games\Marvel's Spider-Man Remastered\Graphics`) showed borderless fullscreen on a
3440 x 1440 desktop with no upscaler (`UpscaleMethod` 0), so that view rendered
4.95 MP per frame beside two 3072 x 3264 eyes (20.05 MP), on an RTX 5090. The
headset's `xrEndFrame` waits (13.4 ms mean) indicate a busy GPU queue. The eyes
already render into private buffers sized by Spidy (3264 rows on a 1440-row
desktop), so the desktop size does not limit them.

Changes:

- `tools/vr_display.py`: when the launcher itself starts the game, it saves the
  game's nine window values (`Fullscreen`, `ExclusiveFullscreen`,
  `WindowMaximized`, `WindowLeft/Top`, `WindowWidth/Height`,
  `UserWindowWidth/Height`) to `reports/desktop-view-before-vr.json` and selects a
  centred window with the desktop's shape, 540 rows tall (1290 x 540 here: 0.70 MP,
  about 3% of the frame's pixels instead of 20%). Values the game never saved are
  not created. Other graphics settings are untouched.
- After the game closes, the session tool writes the saved values back. If the
  launcher ends first, the next launch restores them before anything else, and
  `python tools\vr_display.py --restore` does so with the game closed. A game that
  is already running keeps its size. `-FullDesktopView` (`--full-desktop-view`)
  starts the game with the user's own settings.

Validation: 6 new Python checks (39/39) cover the window shape, centring and
clamping, a single backup across repeated launches, restoring an interrupted
session first, not creating missing values, rejecting an unrecognized backup, and
preparing only when the launcher starts the game, before Steam. The real registry
reader found all nine values as DWORDs and computed 1290 x 540 for this desktop;
nothing was written during development. Whether the game renders exactly at the
window size, and whether a smaller active view changes anything in the eyes (HUD
sharpness, level of detail, occlusion of distant objects), is unverified until
the next headset session; `-FullDesktopView` gives the comparison.

## Eye job reclamation, one hero sample for webs and eyes — same build

The user reported three things from `reports/game-vr-20261005-093614.json`
(242 s at 3072 x 3264 per eye, headset at 120 Hz): VR "crashed" back to the flat
screen after a while, high above the city; while swinging fast the web starts
separated from the hands, more and more; and webs and passing buildings doubled
at high speed, but not when webbing from the ground.

Session findings:

- Not a crash. The report ends with status 5, `Native eye presentation stalled
  for 3 seconds`, hooks restored. The last new pair (generation 10,614) arrived at
  235.3 s while the player stood still at about 250 m. GPU begin/end markers for
  both eyes had stopped at exactly 10,540, yet the game kept setting up both eye
  jobs every frame (sRGB overlay count still rising), the views stayed live
  (`active_view_aligned` still rising), and maintenance kept running. The
  watchdog fired at 238.4 s; cleanup took 4 s.
- 74 generations of eye jobs went unmarked before the last good frame, against 3
  (in flight) after 5,568 generations in `game-vr-20261004-184706`. The sRGB setup
  count matched the presented generation through the whole session, so those jobs
  were copied. `stereo_probe.cpp` kept each copied eye job (from `19223e0`) in a
  128-entry table until its end (`189e3a0`). `1886ea0` ends a job only when the
  next one becomes the current render view, so a copy the game drops is never
  ended. Once dropped copies and jobs in flight held all 128 entries, no later eye
  job could be marked, no pair completed, and VR stopped. Height did not cause it;
  heavier GPU load plausibly makes dropped jobs more frequent.
- Webs: `679dc0` reads each hand's Start and Grip (`7d60`/`7d78`) on every update
  and stores the start it built at rope `+1c`. Only a released rope (`+70c` bit 2,
  set by dissolve `6795b0`) blends away from it and falls. 67 rope creations for
  67 attaches show no Spidy rope was dissolved mid-swing. The wrists were moved by
  (hero position at the rope update − sampled feet), the eyes by (hero position at
  view maintenance − sampled feet). Any move of the hero's render transform between
  the two (`175a060` runs gameplay; `187e320` does scene work, then calls `18a0bb0`)
  separates web and wrist by that distance: zero when standing, growing with speed.
  The old telemetry did not measure it.
- Double images: new pairs arrived at 40-53 per second and 34-43% of the
  ~70 frames per second sent to the headset repeated an earlier pair, at every
  speed. A repeat is invisible when standing still; at 30 m/s (about 0.7 m per
  game frame) it shows the world in the same place twice.

Changes:

- `EyeJobTable` (`include/spidy/eye_job_table.hpp`) reclaims entries older than 16
  scene generations and, if every entry is still in flight, the oldest one. Eye
  frame telemetry v2 (160 bytes) adds `reclaimed`. The job record's unused
  immersive and actor fields were dropped.
- The hero rope update publishes the hero position it read, on every update while
  game webs run (attached or not, so the camera never switches source mid-swing).
  View maintenance and the active-view head camera use that sample when one
  arrived since their previous call, otherwise the render transform as before.
  `-OverlayWebs` has no rope updates and keeps the old behaviour.
- Appearance telemetry v4 (288 bytes) adds `shared_hero_frames`,
  `hero_lag_last_m` / `_max_m` / `_mean_m` (how far the render transform had moved
  from the rope sample by maintenance: the gap this build removes), and
  `web_start_error_m` / `_max_m` (start the game built each rope from, against the
  start Spidy wrote). Reports add `eye_jobs` and a per-sample
  `eye_jobs_reclaimed`.

Validation: release build succeeds; **80/80 core checks**, the D3D12 test, and
**33/33 Python checks** pass. New core checks run 20,000 frames with every 37th
frame's eye jobs dropped (each new job stays markable; dropped ones are reclaimed),
keep a job in flight for 16 generations, update a re-copied job in place, give up
the oldest entry of a full table, and use each rope sample in one frame only. No
new game function is hooked or called; `GetRope` `67b7b0` returning the rope base
and the `+1c` start were decoded offline. No game or headset test has run.

Headset check: play at least 5 minutes, including fast swings and standing on a
tall building. VR should stay up; `eye_jobs.reclaimed` counts dropped eye jobs that
would previously have accumulated. Swing fast and watch the web starts:
`hero_lag_mean_m` should be near zero when standing and larger in flight if the
order explained above was the cause, and `web_start_error_max_m` should stay near
zero. If webs still separate while `hero_lag` stays near zero, the cause is
elsewhere. For double images, compare Virtual Desktop at 120 Hz with 90 or 72 Hz
and SSW on.

## Game's active view follows the head — preceding build, session stalled after 235 s

The user reported that culling of some content, such as the fake apartment
interiors behind windows, did not line up with the headset view and seemed tied
to the body. Their session `reports/game-vr-20261004-185226.json` (666 s at
3072 x 3264 per eye) shows the previous build working as intended: 35,031 frames
with the hero hidden, eyes re-anchored every frame (none rejected), and 184 game
web shots with no failures.

Offline findings in the supported executable:

- `ViewContextManager` (`7a34dd0`) holds a pool of game views (stride `0x1f70`)
  and eight offscreen slots at `+30`; Spidy's eyes are offscreen views. `+f8` is
  the active view: the pool allocator `18a07c0` sets it to the first pool view,
  and `18a0fe0` returns it (or a default view). It is the stock camera's view and
  never an eye.
- `18a1890` (is an instance in any view's drawn array, `+1b80`) and `18a12a0` (is
  a sphere in any view) visit pool views only.
- Shader reflection embedded in the executable gives the frame-wide
  `GlobalWorldCBuffer` (`g_World`, 0x390 bytes) a member `m_ActiveViewCtxPos` at
  `+330`, beside the key-light shadow shells and cascade LOD settings. Per-view
  data is `GlobalViewportCBuffer` (`g_VP`), with each view's own camera matrix.
- The frame dispatch `175a060` submits the active view's occlusion object
  (`17f97c0`), reads it back (`17f9060`), and runs `1722b10`, which measures
  objects from the active view's position, before it calls view maintenance
  (`187e320` → `18a0bb0`). The offscreen initializer `186c4e0` does not call the
  pool initializer `189ce20` that creates the occlusion object at `+1ba0`.
- `1646e10` builds the stock camera's descriptor and submits it to its view
  through `1899ab0` (return `1647044`). On this PC that lens is 81 x 39 degrees
  (21:9, `reports/view-rebuild.json`), placed behind the hero.

Changes:

- `spidy_stereo_probe.dll` hooks view submit `1899ab0` and acts only on the stock
  camera's call from `1646e10` for the first pool view. While immersive, it
  replaces that descriptor with a head view: centred between the two eye poses,
  oriented as their average, moved with the player to the rendered frame like
  the eyes, with the smallest lens that contains both eye images plus 5%. The
  stock near/far planes and temporal jitter are kept. The gameplay camera object
  is untouched, so movement keys still follow the stock camera.
- Flat mode, lapsed commands, menus, and stop leave the stock descriptor as is.
  The flat-screen quad keeps the stock camera's aspect.
- The VR path creates its views with new flag 8 (13 instead of 5). Diagnostic
  tools keep flags 1-7 and their behaviour. `-StockMonitorView`
  (`--stock-monitor-view`, XR option bit 2) keeps flag 5.
- Telemetry: appearance v3 (256 bytes) adds aligned and rejected submits, the
  last head lens, and its distance from the stock camera. Reports show
  `active_view_aligned`, `active_view_rejected`, `active_view_fov_deg`, and
  `active_view_shift_m`.

Validation: release build succeeds; **78/78 core checks**, the D3D12 test, and
**32/32 Python checks** pass. The new core check covers the head pose between
parallel and outward-canted eyes, containment of every eye-image corner in its
lens, the 5% margin, and rejection of a backward-facing eye. All 19 entry
signatures in `stereo_probe.cpp`, including view submit, the camera's call to it,
and the pose setter, match the supported executable. No game or headset test has
run. Which shaders read `m_ActiveViewCtxPos`, and how the window interiors are
culled or shaded, could not be established offline: those materials ship in the
game's data, not the executable. The monitor view now covers a wider field of
view; its frame-rate cost is unmeasured.

Headset check: look up, down, and to the sides at nearby and distant apartment
windows, then compare with `Launch Spidy VR.cmd -StockMonitorView`. Watch shadows
at the edges of view and exposure when turning between bright and dark areas. In
the report, `active_view_aligned` should grow with immersive frames,
`active_view_rejected` should stay near zero, and `active_view_shift_m` gives the
distance between the stock camera and your head. Compare new stereo pairs per
second with the previous session.

## Native avatar hiding, render-frame eye anchoring, game-style webs — preceding build

The user confirmed the HUD glow fix, but still saw the avatar in VR, saw buildings
and the avatar double or jitter at high speed, and asked for the game's web look.
`reports/game-vr-20261004-170757.json` (468 s at 3072 x 3264 per eye) recorded
13,816 sRGB overlay setups per eye and **zero** hidden avatar submissions: the
previous hook never matched. It averaged 29.3 new stereo pairs/s and 46.1 XR
submissions/s, 16.8 of them reused images.

Offline findings in the supported executable:

- `17991a0` receives render instances from three loops (`1794430`, `179d660`).
  Context +8 is the persistent view that `1920e54` also passes to `19223e0`,
  so the eye check matched; the hero's instance pointer never arrived there.
- The `ActorDrawAction` script node (`15a3460`, vtable `3cebbf8`, "This turns
  the Actor visibility off") resolves actor records through `15a0560` and calls
  `191afb0` (DrawOff: sets `0x20` at +5c, stamps +6c, marks the instance index
  in the table at `7a43710`) or `191b880` (DrawOn). The record's first field is
  the object they receive. A live instance is its own entry in the handle table
  at `7a436e8`, as `191ba30` checks.
- `175a060` runs gameplay updates, then `187e320`, which calls view maintenance
  `18a0bb0`. Eye poses were placed on the XR thread from the last camera-commit
  sample, so the camera trailed the body by zero or one simulation step,
  varying frame to frame: roughly 0.5–1 m at 32 m/s.

Changes:

- Avatar: while immersive, maintenance (main thread) validates the hero instance
  and calls DrawOff. DrawOn restores it for flat mode, menus or lapsed commands,
  and stop, only for the same live instance and handle Spidy switched off; a
  hero hidden by the game stays the game's. Stop waits for that restoration and
  has a fallback. In eye views, `17991a0` also skips the hero instance and
  anything on its root transform (3 cm, basis dot 0.998). The monitor view and
  the shadow lose the avatar while immersive.
- Eye anchoring: eye command v2 (208 bytes) carries the player position used for
  tracking. Maintenance moves both eyes by the player's travel to the frame being
  rendered (up to 20 m; larger jumps keep the sampled pose and are counted). The
  offset is stored per scene generation, and the overlay applies it to hands and
  eyes so web anchors stay on their surfaces.
- Game webs: `Hero::HeroRopeManager` (vtable `38b3df8`, update slot `95f6c0`)
  owns 16 rope slots of 0x7c8 bytes. Swing states call `EnsureRope` `677d20`
  (type 2) and `SetRopeTargetPosition` `67d7c0`; `ReleaseRope` `67b610`
  dissolves; `GetRope` `67b7b0` validates `(generation << 16) | slot` handles.
  `RopeManager::Update` `676dd0` refreshes per-hand Start/Grip positions
  (`7d60`/`7d78`, +12 per hand) from joints whose hashes (`7d40..7d5f`) are
  non-zero, then builds each rope from Start and Grip to its target. The tube
  and end cone are separate instances, so hiding the hero leaves them visible.
  Spidy hooks `676dd0` for the hero's manager only. In that update it creates,
  aims, and dissolves one rope per attached hand, zeroes the joint hashes for
  that call, writes the tracked wrists (moved with the player like the eyes),
  and restores the hashes afterwards. Each owned rope's lifetime (+6c0) is
  refreshed to 0.3 s, so the game dissolves it if Spidy stops. The per-eye
  avatar filter never hides rope tubes or cones. The overlay skips a hand's web
  while the game draws it.
- Overlay webs (fallback, or `-OverlayWebs`): `appendWeb` draws a camera-facing
  strand from the palm-side wrist with a twisting sheen, a 1.6-pixel minimum
  width so distant webs stay continuous, parabolic sag from spare rope, a 90 ms
  shot with a travelling tip, a seven-strand splat at the anchor, and a 220 ms
  snap-back after release. The lab uses the same strand.
- Telemetry: appearance v2 (216 bytes) adds hero state/handle/flags, native
  hidden frames, hide/restore counts, small instances still drawn within the body,
  eye re-anchoring counts with last/maximum/mean correction in metres, and game
  web state with rope creations, releases, failures, and manager updates.

Validation: release build succeeds; **77/77 core checks**, the D3D12 test, and
**32/32 Python checks** pass. New checks cover anchored command validation,
re-anchored eyes equal to eyes placed from the rendered position, web endpoints,
pixel-width floor, partial shots, sag depth, splat size, and shot/release timing.
`reports/graphics-webs/web-overlay.bmp` was visually inspected. All 21 hooked or
called entry signatures match the supported executable, and `38b3df8` resolves to
`Hero::HeroRopeManager`. No game or headset test has run. DrawOff, the instance
table, the hero record mapping, and the rope calls were decoded offline; their
live effect is unverified. Rope creation runs in the hero rope manager's update,
the phase that also serves the game's own rope users; the thread that runs it
has not been observed. The web material samples the rendered scene, and its look
in the offscreen eye views is unknown. `-OverlayWebs` turns game webs off.

Headset check: look down in VR (avatar and shadow gone), toggle to the flat
screen (avatar back), swing fast past buildings, and inspect webs as they shoot,
hold, and dissolve. The report's `appearance` block shows `hero_state`,
`native_hidden_frames`, `near_instances`, `anchor_mean_m` / `anchor_max_m` (the
lag this build removes), and `web_state` / `web_creates` / `web_failures`.

## Avatar hiding and HUD brightness — preceding build, HUD confirmed, avatar not hidden

The user confirmed the preceding swinging update feels much better, then
requested avatar removal (or a tracked body) and a fix for the glowing HUD.
This build implements avatar hiding in immersive VR.

Native evidence and changes:

- Geometry submission at `17991a0` receives the actual actor pointer. Its
  context contains the scene view at `+8`, initialized by `1796d20`.
  The hook skips only the discovered local actor in the two immersive eye
  views and their registered render copies. The immersive state is captured
  with queued jobs. The normal desktop view and flatscreen eye mode retain
  their avatar. Actor transforms, animation, and gameplay visibility flags
  are not modified. See `reports/vr-actor-render-full.asm`,
  `reports/vr-gui-buckets.asm`, and `reports/vr-actor-scheduling.asm`.
- Display setup at `1920310` skips command `0x88` when the view ID at `+f90`
  is negative. The verified native command table identifies this as
  `PrepareSrgbOverlay`, handled by `18490d0`. The hook restores that command
  in each owned secondary eye's `+15b0` bucket, immediately before GUI
  geometry at `+15c0`. See `reports/render-passes-repair.asm`,
  `reports/vr-render-commands.json`, and `reports/vr-srgb-overlay.asm`.
  This targets incorrect HUD color/target setup in secondary renders.
- `SpidyAppearanceData` records per-eye skipped avatar submissions and
  queued sRGB overlay setup commands. Reports retain these counters in
  samples and final state, including when the game exits. Hook cleanup
  checks now include both new entry points. Counters show whether each
  path ran; they do not establish visual correctness.

Validation: release build succeeds; **73/73 core tests and 32/32 Python
checks pass**. The appearance protocol test covers both eye counters,
truncated buffers, concurrent writes, and incompatible versions. Native hooks
were checked against the supported executable offline. No game/headset test
has been run for this build. Full body tracking and HUD repositioning are not
implemented by this change.

Headset check: restart through `Launch Spidy VR.cmd`, verify the avatar ahead
is gone while hands/webs remain, and check that the minimap and other HUD
elements have normal brightness. Toggle both thumbsticks to flatscreen and
back, checking avatar visibility in both modes. Session reports will contain
the corresponding per-eye appearance counters.

## Swing response, fall handoff, and R.E.A.L. VR removal — preceding build

The user confirmed ground takeoff and open-space attachment now work, then
reported slow swinging, delayed reeling, and intermittent abrupt downward motion.
`reports/game-vr-20261004-151545.json` contains 10,574 swing samples. Median and
90th-percentile controlled speeds were both 8 m/s: the launcher still imposed
the earlier diagnostic cap. Five midair ownership losses were observed; two
were followed by downward movement of roughly 30 and 39 m/s. No takeoff timeout
was recorded. Reported input did not include a dive key.

Changes:

- Default native speed cap is 32 m/s; gravity is 18 m/s², airborne steering is
  12 m/s², reel speed is 16 m/s, and a deliberate yank can add up to 18 m/s.
  The native request remains speed bounded and collision handled.
- Starting a reel trims slack to current body-to-anchor distance. It then
  shortens at reel speed on that same physics step, avoiding the old delay
  while winding in slack after a yank. Anchor position stays fixed.
- Native airborne displacement events now follow the controlled velocity.
  The mover's independent fall accumulator at `+0x6f0` is cleared after each
  owned gravity call, scoped to the exact controlled mover invocation.
  `reports/mover-full-1fbda50.txt` shows this accumulator integrating even while
  its target displacement is overridden; `reports/mover-gravity-setters.asm`
  records its reset conditions. An earlier air-event-only test still dropped
  at about 8.78 m/s on handoff, so both native histories need attention.
- Session reports now retain native movement telemetry as well as swing input
  and XR data. Cleanup checks include the airborne-event hooks and private eye
  buffer initialization hook.

Validation: release build succeeds; **73/73 core tests and 31/31 Python checks
pass**. Added coverage checks immediate reeling with ten metres of slack,
preserved release momentum, fixed anchor position, and native swing/steering
at 45, 72, 90, and 120 physics steps per second. These checks do not execute the
native game hooks. The user subsequently reported that the update feels much
better. That feedback confirms improved swing feel; exhaustive fall handoff
and full-speed collision behavior have not been verified.

At the user's request, 21 R.E.A.L. VR files were moved out of the game folder:
`RealRepo`, `ShaderFixesGeo3D`, `dxgi.dll`, `openvr_api.dll`, both bundled CUDA
runtime DLLs, `RealConfig.bat`, `RealVR.ini`, `RealVR64.log`, and `Begun.txt`.
The backup and SHA-256 inventory are in `backups/realvr-removed-20261004`.
Every backup hash was checked and the game executable hash is unchanged.
Global NVIDIA profiles and game registry preferences were not restored because
their pre-mod values were not established. No bundled R.E.A.L. VR executable
or setup script was run.

## Ground takeoff and open-space webs — preceding build, user confirmed

The user reported sliding along surfaces and being pulled back down while
trying to reel upward. Their run in `reports/game-vr-20261004-145447.json`
contains 2,660 swing observations with no reported ground contact, including
long stationary-height stretches at Y=77 under continued velocity ownership.
The contact decoder used `1` as grounded. Offline inspection of the supported
executable establishes `0` as support, `1` as sliding, and `2` as airborne:
`1fbd460` classifies support, and `1fbeea3` stores that classification.
Positive support time at `+6b4` is accumulated only for contact `0`.
See `reports/ground-contact-research.asm` and `reports/mover-collision-repair.asm`.

The updated integration relinquishes velocity ownership on support while keeping
the held webs. Takeoff first releases jump for 60 ms, then presses for up to
160 ms. It waits for two unsupported, collidable samples at least 12 cm above
the starting position before restoring web movement. A missed jump retries once;
the transition times out after 1.4 seconds. Pending yank/point-launch velocity
survives that handoff. Manual jump remains available when web movement owns flight.
No collision flags or actor positions are written to force takeoff.

A clear hand ray now creates an anchor at its 100-metre endpoint. It remains at
that world position and supports reeling/release without a native body ID. Real
surface hits still win; moving/invalid hits and blocked body-to-anchor paths
remain rejected. Later geometry blocking a rope still releases it.

Validation: release build succeeds; **71/71 core tests and 31/31 Python checks
pass**. New coverage includes contact classification, delayed takeoff, a second
lift after landing with a held web, input-edge retry/timeout, retained yank and
point-launch intent, maximum-range sky anchors, reeling, and obstruction.
Movement telemetry is version 4 / 176 bytes; swing telemetry is version 3 /
240 bytes, including the raw native contact and takeoff phase/attempt/timeout
fields. No live game or headset test was run for this change. The user will test
ground lift-off, landing followed by another reel, and swinging over open space.

## Latest game integration evidence

- Earlier core baseline: **61 tests passed**, including native camera basis conversion, eye spacing,
  one predicted head/hand/eye sample, tracking loss, stale timestamps, recentering,
  snap-turn pivot, excluding artificial movement from physical hand yanks, controller
  timing across repeated/skipped samples, native point-launch prediction, and
  preservation/expiry/invalidation of delayed native image poses, the two-stick
  shortcut's edge/focus handling, and flat native-camera command validation.
- Native input: a 350 ms Space command produced a **2.986755 m jump**.
  `reports/bridge-jump.json` records 291 virtual input responses and restored hooks.
- Native camera: **5,006 position writes**, maximum **0.500009 m** requested offset,
  and no player displacement in `reports/bridge-camera-offset.json`.
- Final render descriptor: identity reconstruction produced **zero difference**
  in pose, projection, lens bounds, and jitter in `reports/view-rebuild.json`.
  A separate test applied 15-degree yaw, 32 mm translation, and an asymmetric
  lens for 14,460 writes; screenshots confirmed the changed view.
- Offscreen views: the first attempt queued native deletion and was followed by
  a worker-thread access violation. `reports/stereo-crash.json` preserves the
  exception and candidate stack addresses. Those are not an unwound call stack.
- Revised lifetime: views are excluded from new work and retained until game exit.
  `reports/stereo-two-views-retained.json` records two distinct 256 x 256 textures,
  64 mm eye spacing, 344 updates each, and restored hooks. The game remained alive
  and visibly animated several minutes after this test and survived reactivation.
  This establishes the tested idle case, not general lifecycle safety.
- Scene tracing: `reports/scene-copy-test.json` identifies copied render jobs by
  their retained native owner. Both eyes reached the scene and render-job paths
  147 times. Exact-pointer matching had previously missed those copied jobs.
- Native CPU readback: `reports/stereo-native-readback.json` contains two stable
  512 x 512 RGBA images from the engine. Both contain scene detail but are almost
  black; they do not establish correct lighting or stereo geometry. The native
  completion value is 2048 (consistent with row pitch), not a frame count.
- Direct GPU tracing: `reports/eye-display-capture.json` observed 230,045 barrier
  calls and 1,286 copies, with no matching eye-resource write. Capture refused
  with 2004 because resource states were unknown. Native display flag `0x400`
  alone did not produce an observed transition during that test. A later run,
  `reports/eye-display-inventory.json`, observed both resources transition to UAV
  state and successfully copied them on the game queue. Unknown state in the
  earlier run was not evidence of a different underlying resource.
- Lighting: passive tone mapping snapshots in `reports/scene-tone-trace.json`
  showed the eyes using default postprocessing despite matching exposure. Scene
  flag `0x80` enables the active profile; `reports/scene-active-profile.json`
  confirms matching tone settings apart from a changing frame field. Copying
  exposure alone had failed to resolve the dark images.
- Native stereo images: `reports/stereo-square-color.json` captures two 512 x 512
  views with a native 90-degree square projection and 64 mm eye spacing, with
  332 updates and restored hooks. Both PNGs were visually inspected. Searching
  horizontal offsets -12..12 and vertical offsets -3..3 using mean absolute RGB
  error found a five-pixel left shift in the right eye for the near character
  (ROI 175,260..238,323) and lamp (180,340..229,370), and zero shift for distant
  buildings (230,200..350,235). All three best matches had zero vertical shift.
  This verifies native viewpoint separation in this scene. Color/HUD effects,
  frame synchronization, and OpenXR presentation still need work.
- Stereo telemetry: five Python protocol checks pass for the version 3, 320-byte
  snapshot, including torn/odd reads, short memory, and version rejection.
- Native collision: `reports/collision-rays.json` records six read-only rays
  serviced inside `CastRayVsWorld::execute` (return address `0x18180fe`). Four
  directions found surfaces and two found open space. The downward ray hit
  Y=77.0, 6.443 m below the camera; horizontal hits include 27.064 m and 90.498 m.
  Error=0; the hook stopped and entry bytes restored. The initial synchronous
  caller restriction serviced no rays; diagnostics identified the active worker.
  Continuous targeting and fixed-body evidence were established in later tests below.
- Continuous native queries: `reports/native-ray-batches-v2.json` passed 40 changing
  two-ray batches, with 72 hits and a maximum measured response of 16 ms. Every
  hit passed full body-ID validation and had motion ID 0 and flags 1. Command
  expiry and cancellation invalidated old results; stop returned 0 and restored
  the entry. This verifies fixed surfaces in this scene, not moving anchors.
  Controller aims are wired into the XR worker; their live headset check is pending.
- Component discovery: the native generation-checked registry found the local
  hero and its mover in 0.953 seconds. `reports/movement-jump.json` records 447
  read-only samples over a five-second native jump, a 2.985 m height range, and
  restored input hooks. This replaces the slow heap scan on the normal path.
- Movement request: `reports/movement-nudge.json` observed 641 requests and
  changed one delta argument by 4 cm upward, but measured **zero player
  displacement** while perched. This is a failed movement experiment. It does
  not establish authoritative movement. The hook was restored.
- Airborne request: `reports/movement-jump-nudge.json` changed one native request
  by +4 cm X. The next request observed the actual player X position advance by
  0.039993 m. Native movement then carried horizontal momentum forward; Peter
  landed on the rooftop. There were 263 matched requests and restored hooks.
- Leased velocity: `reports/native-motion-lease.json` records 610 native physics
  steps, including 22 controlled steps under one 200 ms command. After the first
  controlled step, observed X velocity stayed in **0.499310..0.500677 m/s** for
  the requested 0.5 m/s, with zero Y/Z velocity and zero vertical drift. The lease
  expired, both modules stopped with code 0, and every checked entry restored.
  This verifies the tested airborne control path. The game's accumulated vertical
  velocity resumed at about -8.81 m/s after expiry; momentum handoff still needs
  work. Ground/perch control, wall collisions under control, and traversal remain
  unverified in this experiment. The later swing test below connects the solver.
- Air-event experiment: `reports/native-motion-handoff.json` records 21 native
  displacement-field overrides and 21 controlled steps. The state reported the
  requested (0.5, 0, 0) velocity, but expiry still resumed roughly -8.78 m/s
  vertical speed. This is a failed momentum handoff experiment, disabled by
  default. The stop operations returned 0 and all checked entries restored.
- Native swinging: `reports/native-swing-flight.json` passed a six-second test
  with **725 evaluated steps, 61 controlled steps, one attachment and one release**.
  The web remained held for the requested 350 ms. Twelve distinct observed steps
  continued controlled flight after release, then native ground contact ended
  ownership. No sampled or final faults occurred; all checked game entries restored
  and all three stop calls returned 0. This verifies one low-speed single-web
  sequence, not city traversal or controller gestures in the headset.
- The earlier `reports/native-swing-flight-first.json` is **failed**. It hit
  error 3002, but the original cleanup overwrote the error and falsely marked the
  run successful. The report is corrected and retained. Stop now preserves faults;
  the harness also rejects any fault in intermediate samples. The speed request
  is capped before native submission to avoid that floating-point/constraint limit.
- Newer than that live run: Quest buttons and physical hand poses feed the native
  swing service, and procedural gloves/web lines render into both XR images.
  Controller time is carried separately from physics time, so repeated samples
  do not interrupt a yank and skipped samples do not inflate its speed. The native
  point-launch window is connected and core-tested. These additions have not yet
  had a combined game/headset run.
- Game VR/collision telemetry: seventeen Python checks pass, covering GPU markers,
  eye states, torn poses, incompatible protocols, hand identity, and collision
  sample/caller boundaries, movement flags/results, velocity lease feedback,
  registry generations, swing input, and rejection of faults hidden by cleanup.
  Fresh native frames and reused presentations have separate rate checks.
  Together with observer, stereo, and four launcher checks, **31 Python tests pass**.
- Headset preflight: the sandbox returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE`.
  Repeating outside the sandbox detected Quest 3 with position and orientation
  tracking. A sandbox failure alone must not be reported as a disconnected headset.
- First game headset run: `reports/game-vr-first-headset.json` recorded 1,398
  tracked frames and 1,399 valid samples for each controller, but **zero submitted
  eye pairs**. The user reported black/distorted imagery. Both stop operations
  succeeded and the game was closed.
- Image rejection cause: the standalone swapchain probe found six 512 x 512,
  single-layer, single-mip images with native DXGI format 27 (`R8G8B8A8_TYPELESS`)
  backing the requested format 29 (`R8G8B8A8_UNORM_SRGB`). The adapter's target
  check rejected format 27. It now accepts that compatible family, reports
  permanent target failures, and stops after three seconds without a first image.
  The actual D3D12 test copies both typed sRGB and typeless targets byte-for-byte
  and rejects incompatible formats and arrays. An empty overlay preserves every
  scene pixel; hand geometry changes a bounded part of each eye and leaves both
  native source images unchanged. `reports/graphics-overlay/left-overlay.bmp`
  was visually inspected. The overlay uses its own depth buffer and does not
  establish occlusion against the game world or native skeletal animation.
- The corrected headset test was initially deferred after an unsandboxed preflight also
  reported no available headset. The subsequent user-run test is recorded below.
  The build releases
  input after a 250 ms image-submission lapse and ends a three-second presentation
  stall. Its launcher saves a separate report for each run.
- Successful headset smoke test: `reports/game-vr-20261004-131244.json` contains
  **509 submitted pairs**, 65 dropped attempts, 575 tracked frames, and 577 valid
  samples per hand. Both stops returned 0 and all checked game entries restored.
  The user confirmed stereo, head movement, and visible hands, while reporting
  extremely low resolution and poor performance. It used 512 x 512 eyes. Swing
  telemetry records zero attachments and zero controlled steps, so this run does
  not verify physical web controls. The sampled presentation interval measured
  about 26.85 new pairs/second; see the adjacent `-analysis.json` report.
- Performance investigation: the game log reports VSync On with a 100 Hz OS
  display. The registry agrees VSync was enabled. This makes a pacing limit
  plausible, but does not isolate the cause. The prototype also flushed the
  entire graphics queue four times per eye pair and copied both images to CPU
  readback buffers every frame. VSync is now disabled with a saved backup.
- New performance build: default 1536-square eyes, supported range 64..4096,
  optional CPU image capture, one asynchronous hand-overlay submission per pair,
  event-driven native-copy submission, and reduced locking in unrelated graphics
  calls. Exact queue ordering replaces CPU waits before OpenXR release; allocator
  reuse and cleanup retain fence checks. Ten stage metrics are recorded after
  30 warmup submissions. Its headset results are recorded below.
- The actual GPU test passed at 1536 x 1536: 155,509 differing stereo pixels,
  26-pixel near-object disparity, exact typed/typeless copies, and 16 asynchronous
  overlay batches matching independently rendered synchronous reference images.
  `reports/graphics-performance/left-overlay.bmp` was visually inspected.
- 1536-square headset run: `reports/game-vr-20261004-133248.json` passed with
  532 submitted pairs, 82 dropped attempts, and clean stops/restored entries.
  The full active sample interval measured **26.76 new pairs/second**, including
  1.1 seconds of drops at the end. Before that tail, it measured 28.34. The native
  view lease started before initialization finished and expired ahead of the XR
  worker; it now includes three seconds of cleanup margin. Mean frame time
  was 31.92 ms, including **29.76 ms waiting for a matching native pair** and
  0.12 ms of hand overlay work. The runtime requested 120 Hz (8.33 ms period),
  with a 0.04 ms `xrWaitFrame` mean. The game log confirms **VSync Off**. Disabling
  VSync did not resolve the low pair rate. These CPU timings identify the blocking
  handoff; they do not isolate all GPU/game-rendering costs. No swing attachment
  was recorded. See `reports/game-vr-20261004-133248-analysis.json`.
- Latest staged handoff: game rendering and XR presentation no longer wait for
  matching current-frame commands. Complete pairs are staged on the bound queue;
  reused pairs retain their original tracking-space projection poses and cached
  overlay state. Metadata expires at 150 ms and is invalidated on focus loss or
  recenter. Fresh/reused XR submissions and native captures have separate counters.
  GPU configuration is v3/48 bytes, GPU telemetry v2/208, XR telemetry v2/560.
- The staged build passes the Release build, 59 core checks, 27 Python checks,
  and real 1536-square GPU testing. Six changing stereo generations were read
  twice each through shared staging textures without intermediate CPU drains;
  every output matched its expected generation/eye. The D3D12 debug layer
  reported no errors. This validates copy ordering locally, not live game hooks.
  The subsequent headset comparison is recorded below.
- Staged headset comparison: `reports/game-vr-20261004-135733.json` passed at
  1536 x 1536. Over the 19.765-second sampled interval it delivered **62.38 new
  stereo pairs/second**, **70.93 total XR submissions/second**, and 8.55 reused
  pairs/second. Final counts: 1,405 accepted submissions (1,236 new, 169 reused),
  eight dropped attempts, and 1,415 valid samples per hand. The user reported
  much better frame rate. Both stops returned zero, GPU fence 2,741 completed,
  and every checked game entry restored. The scene-pair rate improved by about
  2.33x over the full previous active interval, or 2.20x against its productive
  interval before the expired-view tail. These are short runs, not a controlled
  benchmark across representative game scenes.
  Mean CPU frame time fell from 31.92 to 13.91 ms; the copy call fell from 29.76
  to 0.089 ms. `xrEndFrame` now accounts for 13.43 ms of the mean. This measures
  blocking in frame submission; GPU rendering and compositor costs are not yet
  separated. The runtime still requested 120 Hz, which the fresh scene rate did
  not reach. No web attachment or controlled swing step was recorded.

The tracking mapper has now received live head/controller samples in a
Spider-Man OpenXR session. Native headset stereo, head movement, and visible hands
now have user confirmation. Detailed stereo quality, tracked web targeting,
full-speed/two-hand swinging, point launches, skeletal animation, and lifecycle
tests remain open. The bounded native single-web sequence above has passed.

## Seamless launch build — local checks

- `Launch Spidy VR.cmd` selects automatic runtime resolution and an untimed
  session. Steam receives `-applaunch 1817070 -nolauncher`; the latter flag was
  verified in the supported executable's strings. The launcher waits for one
  registered hero and matching mover, then for an active camera sample. Actual
  automatic launch through the game menus has not yet been exercised.
- Both thumbstick clicks form one shortcut: a held chord fires once, and both
  buttons must be released after focus loss/startup before it can fire. VR and
  monoscopic quad presentation use the mode cached with the rendered image.
  Flat mode clones the primary native camera descriptor and uses its aspect
  ratio. It stops VR locomotion/web input and preserves normal stock controls.
- Duration zero keeps the modules active until explicit shutdown. The launcher
  renews a five-second worker heartbeat; the worker also restores the input
  bridge on shutdown. Individual input/eye commands still have short leases.
  Untimed native views retire when their eye commands expire and can resume
  when tracking returns. Shutdown on game exit writes the last known telemetry
  without claiming that final hooks/fences were sampled after process death.
- XR configuration is v5/88 bytes and telemetry v3/576, including mode, toggle
  count, and actual eye width/height. Timed tests remain available separately.
- Release build and all 61 core / 31 Python checks pass. The actual GPU test
  passed at **2688 x 2784**, including typed/typeless copies, six staged stereo
  generations with repeated presentation, and asynchronous overlay reuse.
  The D3D12 debug layer reported no errors. This resolution was a rectangular
  test case, not a reading from the currently disconnected headset.
- The user deferred the new combined headset test. Runtime-recommended resolution,
  live thumbstick switching, the quad image, focus/resume, and operation beyond
  the previous short test duration remain to be verified in the game.

## Earlier lab and observer evidence

## Passed locally

| Check | Evidence |
|---|---|
| Windows Release build | MSVC 19.43, Windows SDK 10.0.26100.0, CMake/Ninja |
| Physics/input/camera core | Originally 32/32; now 61/61 tests pass; CTest suite passes |
| D3D12 rendering | Actual GPU render, depth buffer, submission, fence, and readback complete |
| Stereo geometry | 45,865 pixels differ between eyes; near-object disparity is 16 pixels at 64 mm IPD in the test scene |
| Diagnostic images | `reports/graphics/left-eye.bmp` and `right-eye.bmp`; left image visually inspected |
| Runtime loading | `VirtualDesktopXR` loads and advertises the D3D12 extension |
| Quest 3 / Virtual Desktop lab smoke test | User launched the lab and reported "i did it works fine" after the display, head/hand tracking, and web attach/release checklist |
| Executable identity | SHA-256 and PE64 checks pass for the local Steam 4.0630.0.0 executable |
| Research signatures | Three function entry signatures match; two vtables resolve to expected RTTI |
| Camera/render discovery | 109 matching RTTI types mapped to candidate vtables |
| Game automation | Launched twice, loaded existing free roam, captured screenshots, rotated the camera, closed/restarted through the driver |
| Live objects | HeroLocal and HeroCameraManager share an actor record; all three discovered objects, including FollowCamera, survived the idle samples |
| Camera response | Mouse movement changed the visible view and camera transform while player position remained fixed |
| Native callback observation | 3,362/3,362 valid samples in a 30-second free-roam test; one callback thread; zero rejected or overlapping observations |
| Observer cleanup | Hook disabled; camera-update entry bytes match the original signature after capture |
| Observer reuse | A second enable/capture/disable cycle yielded 543 valid observations and restored the entry again |
| Observer protocol | 5/5 checks pass: overlapping writes, odd sequence, protocol mismatch, unreadable memory, and refusal to start in a non-game process |
| Python tools | Syntax checks pass; no-process and access-denied paths reported cleanly |

The core tests exercise pendulum length and energy, different display rates,
release momentum, slack lines, independent hands, two distinct anchors,
opposing winches, occlusion, missing/invalid tracking, loss of focus, long
stalls, anchor unloading, deliberate/accidental yanks, reeling, swept collision,
grounding, point launches, eye spacing, head translation, snap-turn pivot,
recenter continuity, and projection depth/frustum conventions.

## Headset feedback

On 2026-10-03 the user confirmed that the standalone lab works on Quest 3 through
Virtual Desktop. This is a successful user-reported smoke test. Individual
haptic, recenter, point-launch, frame-time, and reprojection checks were not
reported separately, so those remain open. The report applies to the lab;
Spider-Man integration still requires its own validation.

## Actual game experiment

The camera observer sampled the supported Steam executable with the existing
R.E.A.L. VR module still present and its `AutoStartVR=0` setting. This establishes
flat-screen diagnostic coexistence in this scene only.

`reports/native-camera-summary.json` records:

- 3,362 valid observations over 29.975 seconds between the first and last sample.
- About 112.1 camera-related callbacks per second; this is **not a VR frame-rate measurement**.
- `dt` between 7.71 and 12.75 ms, and one observed callback thread.
- Camera rotation up to 10.86 degrees and displacement up to 0.576 m during the
  mouse test; player displacement was zero.
- Camera basis determinants between 0.99999953 and 1.00000081.
- Hook disable and original entry-byte restoration both confirmed.

The function's `this` pointer is **not HeroCameraManager**. Its observed vtable
RVA is `0x38b1d08`, which resolves in the executable to `Hero::AimContextSwing`.
The object was at manager + `0x6c0` throughout this capture. The observer keeps
that object separate from the independently discovered camera manager. This is
a camera-related update point; it is not yet established as the final render
camera update for every traversal state.

The first diagnostic attempt rejected every transform because it assumed those
two objects were the same. That attempt is retained in `reports/native-camera-idle.json`
as a failed experiment. The corrected capture is `reports/native-camera-v2.json`.

The external read-only captures are in `reports/live-idle-v2.json`,
`reports/live-camera-rotated.json`, and `reports/live-restart.json`. Heap scans
were partial; the reports do not claim to enumerate every camera instance.
FollowCamera contained a 65-degree stored FOV, a 0.1 near plane, and a 1000 far
plane. Its projection axis/convention has not been verified at the renderer.

The UI driver issued `space` and `Escape`, but those OS key events did not jump
or pause the game. Native input queries subsequently solved the tested Space
action as recorded above. Mouse camera movement and the OS close shortcut work.

## Not verified

- Detailed controller/haptic behavior, recentering, and point launches in the headset.
- Quantitative reprojection/latency quality and sustained staged performance across game scenes.
- Camera and movement ownership through loading/cutscenes, respawn, and streaming.
- Detailed stereo/reprojection quality and performance beyond the confirmed smoke test.
- Sustained game-world swinging, animation/IK, streaming, loading, cutscenes, HUD, or performance.
- Global settings left by the removed R.E.A.L. VR installation.

The headset test was initially deferred and subsequently completed as a lab
smoke test. For the game experiment, a save backup was made first. The diagnostic
DLL was loaded from a project staging directory into the running game; its
temporary function hook changed process code and was then restored. No DLL was
installed in the game directory. The original observer wrote no transforms;
later bridge tests deliberately changed the camera with expiring commands.

## Next experiment

The bounded moving-eye test (`reports/stereo-pose-jobs.json`) recorded 90 copies,
begins, and ends for each eye, with matching final pose serial 71 and no adapter
errors. These native render-job events alone do not establish GPU completion.

The subsequent `reports/stereo-gpu-pairs.json` test identified 1,356 eye timestamp
commands in actual D3D12 command lists. It captured 338 pairs with matching pose
serials and render generations, tracked both source states as UAV, and completed
fence 338. Both saved 512-square images contain the native rendered scene. The
game remained responsive and was closed after the test. This is GPU capture
evidence, not headset presentation evidence.

The shared copy routine also passes the standalone D3D12 test: both UNORM eye
textures copy byte-for-byte into sRGB targets, preserving source images. The
bounded OpenXR worker, first-person tracking mapper, and native controller key
bridge have since reached the headset as recorded above. The staged handoff also
passed its short headset comparison. Physical web controls still need validation.

The next headset run should measure the new presentation timings and image
quality, followed by attachment/release, two hands, reeling/yanks, point launch,
and loading/traversal. Launching the stock game alone still starts the flat game;
`Launch Spidy Game VR.cmd` attaches this project's bounded prototype session.
