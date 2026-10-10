# Spidy changelog

Every build, newest first, with what changed, why, and what was measured.
Per-check results are in [VALIDATION.md](VALIDATION.md).

**Latest build (October 10, sixth build): both eyes get one brightness.** You
reported that brightness and shadows differ between your eyes: standing on
the side of a building, the eye that sees more sky and the eye nearer the
city come out differently, and they swap when you turn round; you suspected
an auto brightness worked out per eye. It is exactly that.
The game sets its exposure (how bright it draws the picture) on the graphics
card, one view at a time: it averages what the view shows, lets a small
buffer of the view's follow that average, and its last pass multiplies the
picture by that buffer. Each of your eyes is a view of its own with a buffer
of its own, so each eye was exposed for what it alone saw. A headset's lenses
see further outward than inward, so the left eye's picture holds more of what
is on your left: put the sky there, and that eye is drawn darker. Spidy has
copied "the main camera's exposure" to the eyes since its first VR builds,
but those were the numbers the game reads back to the processor, which its
last pass does not use.
Now each eye's render job reads the buffer of the game's own view, the one on
the monitor, which sits on your head and covers both lenses while you are in
VR: one exposure for both eyes, worked out from everything you see.
Measured in the game without the headset (`tools/probe_exposure.py`, new),
from your save on the construction site at sunset, both eyes drawn the old
way and the new way at each head pose. The brightness of the part of the
scene both eyes see, left eye over right eye:

| Head | Each eye on its own | One exposure |
| --- | --- | --- |
| level, four directions | 1.28, 0.63, 1.42, 1.26 | 1.000, 1.000, 1.001, 1.000 |
| on its right side (right eye below) | 0.43 | 1.001 |
| on its left side | 2.22 | 1.000 |

So with a level head one eye was a quarter to a half brighter than the other,
and with your head on its side, as when you stand on a wall and look along
it, more than twice. In every pose the eye with more sky was the darker one.
`spidy_stereo_probe.dll` changed, and `run_game_vr.py` reads one new counter,
`appearance.exposure_shared` in the session report: the eye images that took
the game view's exposure, two a frame. No protocol version changed (the
counter takes an unused field). 227 core checks pass; 103 Python checks, 1
new (the probe's comparison of what both eyes see, and that it fails when the
eyes still differ). Your saves: all identical but `slot0-s.save`, which the
game saved itself after Continue, as in every probe.
In your play folder (`dist\Spidy-0.2.9`) since 13:19: the module and
`run_game_vr.py` (the folder's own, with the new counters only), the earlier
files in its `reports\backups\play-folder-before-exposure-20261010-131936`.
The module carries the fourth build below as well.
Not tried: the headset. The numbers say the two pictures now match; whether
anything else still differs between your eyes (shadows were part of your
report, and with one eye up to twice as bright they could not match) is for
you to see. If it does, say where you were looking.

**Preceding build (October 10, fifth build): the launcher finds SteamVR beside
Virtual Desktop, and the memory row is gone.** A player on Steam Link, with
Virtual Desktop's streamer running on the PC as well, could not start VR
(the chat, October 10): START VR said "No headset found (Virtual Desktop:
Headset unavailable ... (-35); Meta Quest Link: Headset unavailable ...
(-35))", and the VR runtime list held Automatic, Virtual Desktop and Meta
Quest Link, so there was no SteamVR to choose either. A player the night
before, without Virtual Desktop, had the same ("its only showing up as quest
link not steamvr").
The launcher and the session took the runtimes from Windows' OpenXR registry:
the active runtime and its `AvailableRuntimes` list. SteamVR is in that
registry only while it is the active runtime; it is not on the list on this
PC or in either player's launcher. With Virtual Desktop or the Meta Quest
Link app active, SteamVR was unknown: not listed, and never asked by
Automatic although `vrserver.exe` ran. This PC has it the same way today:
SteamVR installed, Virtual Desktop active, the list holding Virtual Desktop
and Meta's, and 0.2.9 showing those two. (Steam Link worked here on October 7
because SteamVR was the active runtime then.)
Now SteamVR is also found where OpenVR programs find it:
`%LOCALAPPDATA%\openvr\openvrpaths.vrpath` names its folder, and
`steamxr_win64.json` in that folder is its runtime. The launcher
(`openXrRuntimes`) and the session (`xr_runtime.steamvr`) both read it, so
the list shows SteamVR, and Automatic asks it after Virtual Desktop whenever
SteamVR runs. The launcher's JSON reader moved from `launcher_update.hpp` to
`launcher_text.hpp` for this, unchanged.
The Memory row, the "Low on memory" question before a start and the
session's memory warning are removed, as you asked: a player with 32 GB of
RAM never saw the row green and asked how to get it so. With everything in
order the last row now reads "Windows and Spidy's files". Session reports
still record `free_commit_mb` at the start and at its lowest.
The launcher, `tools/xr_runtime.py`, `tools/run_game_vr.py` and the players'
README changed; no module and no protocol version did. 23 launcher checks
pass, 1 new (the path file as SteamVR writes it, folders in another alphabet
written plainly and as escapes, a byte order mark, files that are not it);
103 Python checks, 1 new (SteamVR found by the file, listed, and asked by
Automatic when it runs). On this PC the launcher built from this tree lists
Automatic, Virtual Desktop, Meta Quest Link and SteamVR and has no memory
row; 0.2.9's code lists Virtual Desktop and Meta Quest Link.
Not run: a headset over Steam Link with this build (SteamVR was not started
here), so a session through the SteamVR found this way is untried; the core
checks and the game, which this build does not touch.
For players on 0.2.9 and older, SteamVR's Settings > OpenXR > "Set SteamVR
as OpenXR runtime" puts SteamVR in the launcher's list (in PLAYERS.md).

**Preceding build (October 10, fourth build): the monitor shows your head's view
again in a swing, and the HUD keeps its distance.** Players reported that
since 0.2.8 the game window shows Spider-Man from behind, without his head,
whenever they swing, so first-person footage recorded from the monitor is
spoiled (#spidy-bugs "Camera", October 9: "tested in v0.2.7 and it's
working"; a 0.2.9 clip in the chat on October 10).
The monitor shows the game's own view, which Spidy moves to your head every
frame, and, like the eyes, on with you to where the frame draws you. Since
the HUD build (October 9, fifth) the eyes, that view and the HUD panel take
one head per frame, fixed at its first use. The first use turned out to be
the HUD placing its world markers, which the game does before it moves you:
that head still had the last frame's position. The eyes work out their own
travel later and were right, so nothing showed in the headset's view of the
world. The game's view and the HUD panel were one frame of travel behind
them: nothing while you stand, most of a metre in a swing. From there the
view looks at your own back, and your body has no head, because the head is
hidden for the eyes.
Your own session reports have it. In the 0.2.9 session (October 10, 11:20)
the game's view was placed from another position than the eyes in 41,727 of
53,825 frames, up to 0.90 m behind, and the HUD panel with it
(`offset_mismatch_m` 0.90, which should be 0); in the 0.2.8 session (October
9, 19:51) 18,788 of 24,543 frames, up to 1.35 m. The sessions of October 5
and 6, before the HUD build, have none in 42,614 frames. The panel hangs 2 m
ahead, so 0.9 m of trail in a swing straight ahead put it 1.1 m from your
eyes, nearly twice as large, moving in and out with your speed.
Now the frame's head is fixed after the game has moved you, at the game
camera's submit, as before the HUD build: the game's view, the panel and the
eyes are placed from one position again. The markers, placed earlier, get a
head of their own: the newest head pose, moved to where the frame will draw
you, which is where the last frame drew you plus the travel that frame made.
A step longer than 5 m (fast travel, a respawn) is not carried on.
`spidy_stereo_probe.dll` changed, and `run_game_vr.py` reads one new counter:
`hud.marker_mismatch_m` in the session report, the furthest the markers'
head was from the frame's. No protocol version changed (the counter takes
SpidyHudData's unused field). 227 core checks pass, 1 new (the position
foreseen for the next frame: standing, moving, after a teleport or a gap, and
that a head moved there sits where the eyes are); the launcher's checks; 101
Python checks. `tools/probe_web_frames.py --views 13` now reports
`monitor_view` for a swing in the game without a headset, and `--modules`
loads another build's module for a before and after.
Not run: the game. I asked and you chose to look in the headset yourself, so
neither the probe's new report nor the fix has been in the game. To check:
swing and watch the game window, which should stay inside your head; in the
session report `active_view_lag_frames` and `hud.offset_mismatch_m` should
stay 0, and `hud.marker_mismatch_m` should be a few centimetres. Not tried
either: whether the HUD's markers sit steadier over their targets at speed.

**Preceding build (October 10, third build): walk at a wall and you are on it,
and the wall is your floor as soon as you walk it.** After your first session
with the walls you said that wall walking often does not start and the game's
own wall crawl takes over, and that walking towards a wall and up it, the
view turns only once you let go of the stick. Your session report
(`dist\Spidy-0.2.9`, 11:20, 627 s) shows both. The swing's walls took you 46
times and the game's crawl five times, 36 s in all with the view upright: all
five began on the ground, when you walked into a wall. The swing's walls
only took a body in the air; a walk into a wall was left to the game, which
turned you onto the wall 0.15 s after you met it. And the view turned only
below 1.5 m/s: at 73.5 s you walked a wall at 6 m/s for 29 s upright, until
you stopped.
Now the swing sees the left stick walk you at a wall: within 1.3 m of it
(from your middle), the stick within 45 degrees of straight at it, the wall
there above your head too, so a kerb or a car is none. From that moment the
game no longer gets your stick, which had walked you on into the wall and
into its crawl; a tenth of a second on, the game jumps for you, and the
swing's wall has you as your feet leave the ground, going up it at the walk's
6 m/s while you hold the stick. In the air the stick takes a wall beside you
the same way, with no speed into it: walk off a roof, push at the facade as
you fall, and you are on it and climbing.
Where the game's crawl begins all the same (a wall the mount does not see:
lower than your head, met at a glance), the swing has the game jump you out
of it and takes that wall, at rest.
**STAND ON WALLS** now turns your view as soon as you walk the wall (the
left stick tilted, at up to 7.5 m/s) as well as when you have stopped; a run
the stick steers stays upright as before. So a walk at a wall goes on as one
motion: a short hop, the view pitches back in 0.15 s, and the stick you were
holding forward walks you on up the wall.
In the game without a headset (`tools/probe_wall_mount.py`, new; your save,
the same brick wall; three runs, the third with this build): the probe gets
the player onto the wall as before, walks him down it to a ledge 0.79 m from
the wall, and pushes the swing's stick at the wall. The game stopped getting
the stick at once; its jump was asked for 0.16 s in; at 0.22 s he was off the
ground and on the swing's wall, the game's crawl never began, and he climbed
at 6.0 m/s, his centre 0.90 m from the wall by a world ray, every step the
swing's. Then the hand-over: from the ground the probe walks him into the
wall with the controller's stick alone, as the game walks a player. The
game's crawl began 0.06 s after he met the wall; the swing asked for the jump
0.23 s later, the game put him back on his feet at the wall's foot and jumped,
and 0.66 s after the crawl began he was on the swing's wall at rest, 0.90 m
off it, and stayed (0.42 s in the crawl, where a crawl lasted until you
pressed A). The first run found what the checks could not: as its jump begins
the game reports the player standing again for one step, and the wall that
had just taken him let go, after which the push out to the wall's clearance
came back as speed off the wall and he drifted away. The wall now takes him
again within half a second of the mount, and the stick takes a wall with no
speed off it.
The swing's data stays version 4 (its last, unused field is now `mount`);
XrConfig and XrData stay version 20. `spidy_ray_bridge.dll`,
`spidy_stereo_probe.dll` and the launcher changed (its "Stand on walls" text).
226 core checks pass, 5 new (the mount and the step in which the game stands
the body again, what mounts nothing, the crawl's hand-over, the stick taking
a wall in a fall, the view while walking); 22 launcher checks; 101 Python
checks.
Not tried: the headset. Whether the hop onto the wall and the view turning
as you go up feel right is yours to say, and whether walls take you when you
do not mean it: in a fight beside a wall, or walking up to something on one
(you now stop about a metre short of a wall you walk straight at, and hop
on). In the game the crawl's hand-over was met once, a metre above the
ground; higher up, your 11:20 session has it by your own A press (253.3 s:
out of the crawl and on the swing's wall within 0.06 s).

**Preceding build (October 10, second build): walls are yours. Fly into one and
you run along it, stop on it, walk it and jump off; stopped, your view turns so
the wall is your floor.** You said that sticking to walls, walking on them and
wall running felt bad: the stick is clunky to get on and off, only A leaves a
wall, and running along a wall in a swing should feel better; and you
suggested turning the camera to the new floor. Your two October 9 sessions
show what the game did. A swing that met a wall put the game into a wall
state of its own, in which any movement with a part into the wall moves
nothing: you hung still against the wall for a second, the game then turned
you onto it in its crawl (0.6 to 0.7 s more), and from there only A, or a web
above you, got you off, through the game's jump. A wall touch cost 2 to 3
seconds and all your speed (28 m/s to 0 in one of them).
The swing now keeps you on the wall itself. Short rays find a wall you are
coming into, and you stay 0.9 m off it, where the game's own collision never
meets it, so the game goes on as if you were in the air and none of its wall
states start. On the wall nothing pulls you down along it. What you had of
speed along the wall goes on, a little more for a hit at an angle: a wall
run. Without the left stick or a web the run slows (3 m/s a second) until you
stop and stay; head on, you stop at once. The left stick walks the wall at
6 m/s where you look along it; pushed at the wall, up it. A faster run keeps
its speed while the stick points its way, and the stick steers it. **A** jumps
off, out from the wall and up, with the speed you had along it; a web pulls
you off it or along it. Up over the wall's top edge you hop onto the roof. At
a walk you step round a corner onto the next face; a run flies off the
wall's end. A jump of the game's own into a wall lands you on it the same
way. Both hands buzz when a wall takes you and when you jump off.
**STAND ON WALLS** (on; Settings > SPIDY VR under COMFORT, or "Stand on walls"
in the launcher): once you have come to rest on a wall, your view turns in
0.15 s so that the wall is your floor, your feet on it, and stays so while
you walk it, round its corners too; when the wall lets you go it turns back as
fast and keeps the way you face. A run along a wall never turns it. Players
on Discord asked for the same, near instant, because looking up a wall
strains the neck. Off, you stay upright beside the wall.
Walking into a wall from the ground is still the game's own crawl, with the
view upright and stood off it as before: how the game takes the stick there
is not measured yet, and a turned view wants the stick to go where you look.
In the game without a headset (`tools/probe_wall_run.py`, your save, a brick
wall 17.7 m off, webbed and reeled into at 31 m/s): the swing had the player
on the wall 0.84 s after the reel began, his centre 0.90 m from it by a world
ray, at 23 m/s along the wall where the game used to stop him; for the 5.1 s
on the wall the game's mover stayed in its air mode under the swing's command
on every step and its wall states never appeared. He slowed by 3 m/s a
second, turned to the stick, walked along at 6.0 m/s and up at 6.0 m/s,
stopped and stayed (0.00 m/s), and the jump left at 6.0 m/s out and up. With
the swing's walls off, the same reel ended in the game's wall state at
2.6 m/s and a 63 m fall to the street. `tools/probe_menu.py` switched STAND ON
WALLS off in the game's own Settings among its 20 steps, all as expected (it
now reaches Settings with Up twice from RESUME: your save is in a Fisk
hideout, whose pause menu has one line more, and four presses down opened
Photo Mode).
The swing's data and settings are version 4, XrConfig and XrData version 20,
so the whole package goes together. `--no-stand-on-walls` and `--no-wall-run`
(the game's crawl as before, for comparison) on `run_game_vr.py`.
221 core checks pass, 9 new (the run, the stop, the stick, the jump, a web's
pull, the top edge, the corner, what walls leave alone; the view's turn, a
corner's, and the game's crawl); 22 launcher checks; 101 Python checks.
Not tried: the headset. How the run, the stop and above all the turning view
feel is yours to say; the view's turn has only its unit checks, since it runs
in the headset's worker. In the game the corner, the top edge and facades with
ledges were not met. Not in the play folder.

**Preceding build (October 10, first build): a game that runs as administrator
is named at once, and the launcher offers to start again as administrator.**
A player's launch (Quest 3 on a cable, Meta Quest Link, a French Windows)
printed "WARNING: render memory module unavailable ([WinError 5] Accès
refusé.)" when the game started, said nothing for three minutes, and ended
with "No loaded player within three minutes. Load a save, then run the
launcher again." Windows had refused the launcher the game's process. The
render memory module's 2.5 s of asking ended in the warning; the wait for
the game then took every refusal for a process still being set up until its
three minutes were over, and its last message is about a save this launch
never came to. The log does not say why Windows refused. The usual reason
is a game that runs as administrator (Steam started as administrator, or
Spider-Man.exe set to) with a launcher that does not; the other is a
security program guarding the game.
The session now asks Windows whether Steam, and then the game, runs as
administrator, which Windows answers for a process it otherwise keeps
closed. An administrator's Steam ends the launch before anything is changed
or started, and an administrator's game the moment its process appears. A
Steam that is not running and is set to run as administrator, which Windows
refuses to start from the launcher, ends it the same way. The message names
what runs as administrator and what to do, and the launcher's window asks "Administrator
rights needed" with *Restart as administrator*: Windows asks to allow it,
and the launcher opens again with those rights. After "Not now" the offer
stays under START VR. A game that refuses for 5 s without that explanation
ends the launch naming a security program (allow the Spidy folder in it), or
administrator rights as the first thing to try where Windows does not say
how the game runs. A launch that runs out of its three minutes says what was
missing: the game never started (look at Steam), or it could not be opened
(with Windows' error); "No loaded player" is left for a game that was open.
A second launcher started while one runs as administrator no longer opens
beside it. The startup report has `administrator` (whether the session
itself ran as administrator).
7 new launcher protocol checks: an administrator's game, before the render
memory module is tried; an administrator's Steam, before the game's settings
change; a refusal that lasts, by who runs as administrator; one that passes;
the three ways to run out of time; the question itself; the exit code. All
101 checks in the 6 Python suites pass, and 22 launcher checks (1 new: the
error line and the exit code). In the launcher's window, a stand-in session
that ran the real wait with a Steam "running as administrator" ended with
exit code 5 before starting anything, and the question, "Not now" and the
offer under START VR were as described. Not run: the restart itself
(Windows' prompt needs a click), a game that does run as administrator, and
the player's PC. The core checks were not rerun (nothing in the modules
changed). Not in the play folder.

**Preceding build (October 9, tenth build): VR starts where Windows refuses
changes to the game's settings.** A player's 0.2.8 launch (Virtual Desktop,
Quest 3) ended before the game started with "Spidy VR unavailable: [WinError
5] Access is denied" and a traceback. Before it starts the game, the launcher
sets a small window and turns off frame generation and Windows.Gaming.Input
in the game's registry key. On that PC Windows opened the key for writing,
then refused writing a value; the traceback does not establish why.
The launch now goes on with the current settings and prints a warning
(turn frame generation off in the game, and a smaller window leaves more of
the GPU for the headset). Whatever the launch had already changed is put
back where possible; a failed rollback keeps the backup for a later retry.
Restoring writes only values that differ from the registry, so a
launch whose changes were all refused has nothing to restore. A refused
restore after the game closes is a warning, and the next launch tries again.
The session report and the startup report have `game_settings_refused` (the
error, or null). 3 new launcher checks: everything refused, or only the
controllers' key; a restore that writes nothing, or keeps its backup when it
cannot write; the warning shown once. Two additional checks cover a partial
write within a key, failed rollback and retry, and continued startup in both
window modes with the error recorded. All 94 checks in the 6 Python suites
pass. Not run on the player's PC; not in the play folder.

**Preceding build (October 9, ninth build): a flip speed setting, slower by
default.** You asked for flip speeds to choose from, like the turning speed
and the weight, and for a lower default. FLIP SPEED is a new row under FLIPS
in the SPIDY VR tab of the game's Settings, and "Flip speed" in the
launcher's options under "Flips (experimental)": how fast a flip turns you at
full tilt of the left stick, 90, 120, 150, 180, 240, 300, 360 or 480 degrees
a second. The default is 180, a quarter slower than the 240 the flips had so
far; 240 is still a choice. A tap's whole flip and the turn back level go as
much faster or slower (at 180: a tap's flip about 1.4 s instead of 1.1 s,
the way back 1.33 s a turn instead of 1). Changed in the headset, it carries
to the next session as the other settings do (launcher.ini `flip_speed=`;
`--flip-speed`, `Launch Spidy VR.cmd -FlipSpeed`). RESET in the tab puts 180
back.
XrConfig and XrData went to version 19 (XrConfig's spare is now `flipSpeed`;
XrData has `flipSpeed` at 792, 800 bytes), the menu probe's structs to 5: the
launcher, the Python tools and the modules must come from the same build;
install the whole package. The session report's `vr_settings` has
`flip_speed`. 212 core checks (1 new: the stick's speed, a tap and the way
back at 180 against 240, kept through a reset, the clamps, the rig passing it
on; the tab's checks now count 17 rows), 21 launcher checks (the
`--flip-speed` argument and its range, `flip_speed=` from the headset) and the
5 Python suites pass. `tools/probe_menu.py` now steps FLIP SPEED to 240 and
expects RESET ALL to put it back (23 changes); it has not run in the game.
Not tried in the headset; not in the play folder.

**Preceding build (October 9, eighth build): with FLIPS on, the left stick flips
you without A.** You asked for the stick to flip you at all times once flips
are on, without holding A. With the experimental FLIPS switch on, the left
stick in the air now turns you head over heels by itself, as fast as you tilt
it (ahead a front flip, back a backflip, to a side a cartwheel, a whole turn
in 1.5 s at full tilt). Let go and you turn back level the short way; push
again on the way and the stick takes the turn over where it is. While it
flips you the stick no longer steers you in the air, as with A held before.
- A stick already held when you jump (a running jump) keeps steering you
  until you let it go once in the air, so running off a roof does not throw
  you into a flip. Landing, or the game sticking you to a wall or ceiling,
  levels you as before, and from the ground the stick walks again.
- A works as before: a tap is one whole flip toward the stick, and held in
  the air it keeps you turned while the stick rests (let go of A and the
  stick to come back level). A tap while the stick is turning you is no
  extra flip.
- FLIPS off: nothing changed; the stick steers you in the air.
The SPIDY VR tab's and the launcher's FLIPS help now read "In the air the
left stick turns you over; tap A for one flip, hold A to stay." No protocol
change: the stereo DLL (the flip and the tab's text) and the launcher (its
text) change in code. 211 core checks (2 new: the stick alone in the
air flips, a stick held from a jump or on a wall walks, release levels,
landing levels; its speed, A holding the angle, a flick or a tap no whole
flip), 21 launcher checks and the 5 Python suites pass. Not run in the game
or the headset; not in the play folder. (Its flips turned at 240 degrees a
second; the ninth build makes that a setting, 180 by default.)

**Preceding build (October 9, seventh build): a steadier, sharper HUD, and a HUD
setting.** You played the fifth build's HUD on a Quest 3 (Virtual Desktop,
eyes of 3840 x 4080 at 125%), found it still a bit blurry and a tad
jittery, asked for it to follow the head smoothly, and for a way to switch it
off. Your 14:37 session placed the panel in every immersive frame. Its jitter:
a quarter of the images the headset showed were earlier ones turned to your
newer head (53.6 new images a second on a 72 Hz display), and turning an
image turns a panel locked to your head with it. Its blur: the panel's
texture had 1720 pixels across its 60 degrees, fewer than your eye images
have there, so the HUD was stretched, and the game made its text for a
720-row window.
- The HUD now stays put in your room while your head looks within 2 degrees
  of it, and once you look further it glides back in front of you (63% of the
  way every 0.15 s), level with the horizon. Fixed in the room between those
  glides, it stays put on reused images too. Markers still sit on their
  targets: the game places them where the line from your head to the target
  crosses the panel, whichever way it faces.
- The VR window is 1920 x 1080 (16:9) instead of the desktop's shape (1720 x
  720 on 3440 x 1440), and at most three quarters of the desktop's height.
  The game sizes its HUD by the window's height and lays it out for 16:9, as
  on a console: the HUD has half again the pixels, and the same 60-degree
  panel shows all of it a third larger. Menus and cutscenes on the game screen
  are 1080p too. Next to your eyes it is about 3% more pixels to render.
- HUD (Settings > SPIDY VR, under COMFORT after GAME SCREEN SIZE; "HUD" in
  the launcher; `--hud`, `-Hud`): OFF, SMALL (50 degrees across), MEDIUM (60,
  the default) or LARGE (70). OFF leaves the panel and everything on it
  (markers, subtitles, prompts) out of the headset, immersive and on the flat
  screen; the game screen keeps the game's own HUD.
Measured, in the game without the headset (`tools/probe_hud.py` at the
headset's eye size, your save at the Fisk construction site):
- The window came up at 1920 x 1080, and so did the panel's texture (the game
  makes it 1932 x 1080 for that window; Spidy makes it once at the window's
  size, as before).
- At MEDIUM the panel stayed in place with the head ahead, turned 40 degrees
  and turning at 90 degrees a second. The run placed it 1,621 times, none
  rejected, with 0 m between the panel's and the eyes' travel. SMALL and LARGE
  spanned 50 and 70 degrees, with a world marker on the same spot.
- A panel turned 15 degrees from the head, as the follow leaves it mid-glide,
  showed 15 degrees to the side with its subtitles and minimap.
- OFF: no panel and no subtitles in either eye, immersive and on the flat
  screen (848 eye draws of the panel left out in 424 frames); at MEDIUM again
  it came back, and stopping VR gave the game its own texture back.
- At half the headset's resolution, subtitles that were hard to read in the
  fifth build's captures now read cleanly, and the health bar is a third
  larger.
- The follow itself (still within 2 degrees, the glide, level when the head
  tilts, straight down, a reset) is a new core test: only the XR worker drives
  it, which needs the headset.
Not tested: the headset, so neither how the follow feels nor how sharp it is
through Virtual Desktop. In your next session's report, `hud` gains
`followed_frames` (placements the follow turned) and `follow_angle`, and
`vr_settings` the `hud` value. XrConfig and XrData are v18, SpidyHudData v3
and the menu probe's structs v4: install the whole package.

**Preceding build (October 9, sixth build): heavier by default, and longer
slow motion.** You asked for the default weight to be 80 and for slow
motion to last longer.
- WEIGHT (Settings > SPIDY VR, "Weight" in the launcher) now starts at 80%
  of real gravity (7.85 m/s² while webs fly you) instead of 60%. RESET ALL
  goes back to 80%. Until now the launcher saved the weight whether or not
  you had chosen it, so every player's saved settings say 60%: the launcher
  now takes a saved 60% as the old default and starts at 80%. Any other
  saved weight is kept, and the launcher now saves it under a new name. A
  60% you chose yourself needs choosing once more.
- A full focus meter lasts 12 seconds of slow motion instead of 7.
  Refilling still takes 12 seconds from empty, starting a moment after slow
  motion ends.
Measured: the core tests (208), the launcher's tests, five Python suites and
the GPU test pass. The tests show the new default as the tab's 80% step,
falling at 7.848 m/s², and the meter running out at its 12 s. Not run in the
game; the swing's gravity code itself did not change. `probe_menu.py` now
steps the weight from 80% to 100%, and `probe_weight.py` and
`probe_slow_motion.py` fly at 80%.

**Preceding build (October 9, fifth build): the HUD stays in view, sharper, with
the game's markers and subtitles.** You asked for the HUD to stay in view, be
sharper (it was very blurry), and show what the flat game shows, like
trackers. The game draws its HUD (health, gadgets, minimap, prompts) on a
panel it hangs 20 m in front of its own third-person camera, larger than that
camera's view. In VR that camera is still behind you and does not turn with
your head, so the panel hung far ahead, mostly out of view and small. The
rest of the HUD (world markers such as objective trackers and interaction
rings, subtitles, QTE and other prompts) the game draws only into its own
view on the monitor, which the eyes never get.
Now, while you play in VR:
- The panel sits 2 m in front of your head and turns with it, centred, the
  part the flat game shows spanning 60 degrees (30 to each side). Both eyes
  see it at 2 m, so it fuses cleanly. Like the flat HUD it is drawn over
  everything, walls included.
- The rest of the HUD is drawn onto the same panel, laid out as on the
  monitor: subtitles at its bottom, prompts where the game puts them. The game
  places its world markers from your head onto the panel, so a marker sits
  over its target while the target is inside the panel; outside it, the game's
  edge indicator shows the way.
- The flat-screen view (both sticks) now shows markers and subtitles too.
- Sharper: the panel's texture is drawn at the window's size, and the VR
  window is 720 rows instead of 540 (1720 x 720 on a 3440 x 1440 desktop),
  about the headset's own detail across the panel's 60 degrees. That window is
  also what the game screen shows, so menus and cutscenes are sharper. It adds
  about 3% to the pixels the game renders for the two eyes.
How: [DEVELOPMENT.md](DEVELOPMENT.md#the-hud-in-vr).
Measured, in the game without the headset (`tools/probe_hud.py`, your save at
the Fisk construction site):
- The panel stayed at the same place in both eye images while the head looked
  ahead, turned 40 degrees, looked 25 and 70 degrees down, and turned at 90
  degrees a second, which also shows no frame of lag. Every immersive frame
  placed it (2,349 placements, none rejected), and the panel and the eyes always
  moved with the player alike (0 m apart).
- The rest of the HUD was on the panel in every immersive frame; two markers
  a frame were placed from the head. Looking down at the crates, the game's
  interaction ring sat on the crate; subtitles showed at the panel's bottom.
- With the eye views paused, as when the headset shows the game screen, the
  game's own view had the whole HUD back within about 0.35 s: the window
  showed the HUD, the ring and the subtitles as the flat game does.
- Stopping VR made the panel's texture at the game's size again and bound it.
- Text is readable in captures at half the headset's resolution.
Not tested: the headset. In your next session the report's `hud` block shows
it working (`placed_frames`, `layer_frames`, `marker_projections`; `restored`
after the session). Worth checking in the headset: whether 2 m and 60 degrees
are comfortable (both could become settings), subtitles and prompts in a
fight or a QTE, and objective markers while swinging. A new export
(`SpidyHudData`) ties this build's modules and Python together: install the
whole package.

**Preceding build (October 9, fourth build): fists, web balls and webs take
each enemy's own size.** Spidy treated every enemy as a street thug: aim at
a chest 1.15 m above his feet, catch him 0.95 m up, a fist hits within 1.8 m
of his feet, all 0.45 m wide or less. For a drone, a flyer or a heavy like
Rhino that was off. Now Spidy reads each bot's size from the game: the
capsule his mover collides with, which the game keeps in his mover manager
(a street thug's spans 0.4 m to 1.6 m above his feet, 0.45 m around).
Every height and width tuned on a street thug carries over in proportion to
that capsule. Heights stretch between the capsule's ends and widths follow
its radius, so a street thug keeps exactly today's values while a drone's
shrink and a heavy's grow. The web balls' aim point and aim assist, where a
web catches and holds an enemy, a fist's target, and what a flying enemy or
thrown prop strikes all use it. Mass, which sets how hard the web pulls,
stays as it was.
Measured: the game builds each mover's capsule (`0x1fbba90`) from fields
Spidy now reads. In the game, all 12 Fisk thugs at your save read 0.85,
1.15 and 0.45 (0.4 m to 1.6 m), and the hero 0.86, 1.3 and 0.4. A new core
check reads a body laid out like the game's, and checks that a street
thug's measures come back unchanged and a drone's and a heavy's scale. Not
seen yet: an enemy of another size in the game (none near your save).
`tools/probe_combat.py --list` now prints each bot's capsule. No protocol
change: only the ray module changed.

**Preceding build (October 9, third build): the launcher updates itself.** You
asked for an auto-updater for the launcher. When it starts, the launcher asks
GitHub for the newest release. If that is newer, a banner above the Play tab
offers it with *What's new* (the release's changes on hover, its page on
click), *Update now* and *Later*. *Update now* downloads the zip (progress on
the banner, *Cancel* stops it), checks it against the SHA-256 GitHub gives for
it, unpacks it, puts its files in place of the folder's and starts the new
launcher, which says "Spidy is updated: 0.2.6 to 0.2.7." Settings
(`%APPDATA%`), `reports\` and files beside the launcher stay. *Later* hides the
banner until the next start. About has a new UPDATES card: the last check's
result, *Check now*, *All releases*, and a switch for the check at start. The
switch is on by default, and updating always waits for a click.
How: the files are swapped by renames within the Spidy folder, which Windows
allows even for the running launcher. Old files go to a hidden
`.spidy-update\previous\`; if any file cannot move, every move so far is undone
("Nothing was changed"). It refuses while VR runs, while a program runs from
the folder (a session's Python, the headset check), or while the game has
Spidy's modules loaded. The old launcher closes and then starts the new one,
which waits for it to exit and deletes `.spidy-update\`. A checkout's launcher
(`build\windows-ninja`) neither checks at start nor installs. Details:
[DEVELOPMENT.md](DEVELOPMENT.md#updates). No protocol change; `update_check=`
is new in launcher.ini. Players on 0.2.6 or older download the first release
with the updater by hand once.
Measured: 21 launcher checks (5 new: JSON; the release read from GitHub's real
answer for v0.2.6; the notes' SHA-256 when the digest is missing, and refusals
without one, without the zip or without a version; versions; the update plan).
End to end, with this launcher in a copy of the 0.2.5 release, against the
real v0.2.6 release:
- The banner offered 0.2.6. *Update now* downloaded, checked and installed it,
  then the 0.2.6 launcher started. The folder's 68 package files were
  hash-identical to the 0.2.6 zip; a report and a file beside the launcher
  stayed, and a stray tool was retired.
- With the folder's Python running, it refused ("Close python.exe first").
- With `tools\xr_runtime.py` held open, it rolled back: all 69 files were
  hash-identical to before, the running launcher's own exe included. *Try
  again* after the lock was released succeeded.
- Started with `--updated-from` and `--wait-for`, it waited for that process,
  deleted `.spidy-update\`, and showed the updated banner.
A whole update took a few seconds in two runs; in a third, GitHub served the
12 MB zip in about 110 s.
Not tested: *Cancel* mid-download, a SHA-256 mismatch, a proxy, and updating
into a release that has the updater, since none is published yet.

**Preceding build (October 9, second build): choose the button that shoots
webs.** You asked for an option to switch the grip and trigger actions.
WEB BUTTON, the first row under WEBS in Settings > SPIDY VR, is GRIP (the
default, as before) or TRIGGER. On TRIGGER the trigger shoots and holds a
hand's web, and the grip reels it in and, on a hand without a web, shoots web
balls. The launcher's options have the same choice ("Web button"), and
`Launch Spidy VR.cmd -TriggerWebs` starts with it. A change made in the
headset carries over to your next session, like the other settings.
How: the swap happens in one place, where the controllers' buttons become the
swing's input. Everything that reads the web and reel buttons follows it:
swinging, reeling, zips, the web grab and its throw, the web shooter, and
the aim markers (the ring tightens as you press the web button). Fists still
close with the grip, the T-pose calibration still asks for both triggers,
and the game's menus keep the grips as bumpers. Changing it lets go of both
webs, so a button held at that moment fires nothing until you release it.
On TRIGGER, a fist made with the grip fires a web ball from a free hand (on
GRIP it shoots a web). A fast punch closes the fist by itself, so you can
punch with the grip open.
Protocol: XrConfig and XrData are version 17 (options bit 12, settings bit
64, `trigger_webs` in reports and in the launcher's settings line), so the
runner and the DLLs must come from the same build.
Measured: 206 core checks (1 new: the swap per hand; a change in play lets go
of the webs once; a change in a menu lets go when play resumes; reset keeps
the button). The tab test checks the new row and that RESET ALL puts GRIP
back. 16 launcher checks, 88 Python checks and the GPU test pass. Not run in
the game yet: `tools/probe_menu.py` now sets WEB BUTTON to TRIGGER first and
expects 18 changes. Tell me how webbing on the trigger feels in the headset.

**Preceding build (October 9, first build): fists, webs and web balls reach
every enemy, flying ones included.** A player reported that flying Sable
agents ignore punches and that webs pass through them. Spidy recognised a
bot by one game class, the mover manager that walks bots on foot
(`BotMoverManagerGame`), matched exactly. Any enemy moved some other way was
invisible to the fists, the web grab and the web balls' webbing. That covers
every flyer the game moves with its `HoverMoverManager`, plus Doc Ock's,
Hammerhead's and Mecha-Hammerhead's movers.
You asked for something systemic rather than a fix per enemy type. Spidy
now reads the game's own type information (RTTI) for every component it
scans, so a class counts together with everything derived from it. An actor
with a component derived from `BotMoverManager` is a bot, whatever moves it.
Every bot is an enemy except the friendly and neutral families: civilians,
the police and mission companions, birds, helicopters and Silver Sable's
aircraft. No enemy type is named, so flyers, bosses and DLC enemies are
covered without a save for each. What a blow does is the game's own
reaction, since fists, webbing and knock-backs go through its damage system.
Fists now also skip the neutral bots.
Measured: a new core check builds stand-in classes named like the game's and
runs the same walk over them. A class two levels below `HoverMoverManager`
is a flying bot, a subclass of `ThugBot` is a thug, and an unrelated class
or plain data is nothing. In the game, headless, at your save's Fisk hideout
(`tools/probe_combat.py`):
- Spidy's scan inside the game found the same 74 props and bots as the same
  rules read from outside, and no actor had a bot component without a bot
  mover manager.
- Three web balls webbed a thug up.
- A scripted fist at 5.4 m/s landed: 27.9 damage, his health from 50 to 19.
  This is the first time the fists' own path was seen hitting a thug.
- A pull flung a thug into a wall: his health from 50 to 26.

No flying enemy was loaded anywhere near your save, so a flyer's reactions
were not seen. Fists and webbing are the game's damage, as for any bot. A
pull asks the game for its flung reaction; if a flyer doesn't take it, Spidy
steers its mover, as it already does for bots the game won't fling.
No protocol change: only the ray module changed. 205 core checks pass.
Tell me, or the player, how flying agents react to a punch, three web balls
and a pull.

**Preceding build (October 8, fifteenth build): slow motion, as in Blade &
Sorcery.** You asked for a Blade & Sorcery-like slow-motion button with a
smooth transition in and out, an effect while it is on, and a "mana" that
runs down and recovers. Click the left thumbstick on its own (both together
still switch to the flat screen) and the game's time eases down to 30% in
0.4 s; click again and it eases back in 0.55 s. The ease is an S-curve in
log time, so the world decelerates evenly and never jolts. Your head and
hands keep real time. Focus, the mana, drains in real time while slow
motion lasts: a full meter lasts 7 s, and when it runs out slow motion ends
by itself. It starts refilling 1.2 s after slow motion ends and takes 12 s
from empty; a click with under 15% left is refused. The meter is a cyan
ring around an hourglass floating over the back of your left wrist. It shows
while focus is spent, empties clockwise from the top, glows and breathes in
slow motion, flashes red when it runs empty or refuses a click, and fades
1.5 s after it is full again. In slow motion the headset's image is drained
toward grey, cooler, and darker toward the rim (round about each lens), and
a ring of light bends the image slightly as it sweeps out across your view
at the start and at the end. Both controllers pulse at the start and end;
the left one buzzes when the meter runs empty or refuses a click. A menu,
the game screen or the flat screen ends it.
How: Spidy hooks the game's own `TimeScaleSystem`, the one its dodges and
gadget wheel slow the world with (update `0x19bb430`, game_time.cpp). After
each of its updates the system's scale, the clock's scale (`0x7a7fb90`) and
Havok's step (`0x609a560`) become the smaller of the game's own and Spidy's.
Before the next update the game's own values go back, so its own slow-motion
moments, channels and events work as before. Everything on the game's clock
slows: thugs, traffic, physics, effects, and Spidy's own swing, which steps
with the player's mover. Props a web holds or throws used to be kept in real
time; `native_bodies` now keeps them in the world's time (real time at the
game's time scale), so they slow too, and they also follow the game's own
slow-motion moments.
Measured in the game, headless (`tools/probe_slow_motion.py`, reports
`slow-motion-probe.json` and `-first.json`): at 30% the clock read 0.30,
each frame's game time came to 0.298 of its normal share of real time, and
Havok's step to 0.30 of its base. In a released swing flight the player's
mover stepped 0.301 game seconds per real second (1.022 at normal speed),
while the swing's gravity on game time stayed 5.89 m/s² (5.886 asked): the
swing slowed with the world. The web grab's step time fell to 0.32 of
normal. Everything came back to 1.0, every module stopped with code 0, the
hooked code was restored, and the save files stayed byte-identical.
No XrConfig/XrData change (both stay version 16), but the session runner
needs the stereo module's new `SpidySlowMotionData` export and checks the
new hook's entry, so the runner and the modules must come from the same
build. The session report has `slow_motion` (presses, starts, refusals,
empties, seconds in it, focus, the game's scale) and `slow_motion_samples`.
204 core checks (5 new), the GPU test (new: both eyes recoloured, typed and
typeless, the ring and the meter; debug layer clean), 16 launcher checks and
the 5 Python suites pass. Not tried in the headset: how the look and the
meter feel, the haptics, and the game's sound, which Spidy leaves alone (it
may or may not slow with the game's time).

**Preceding build (October 8, fourteenth build): the launcher says why a game
version is not supported.** You passed on a player's report: Steam says their
game is up to date, yet the launcher called it a different version. Steam's
current version is still the one Spidy supports (build 23986256, version
4.630.0.0, the July 8 update; Steam's app info checked today), so their
`Spider-Man.exe` is not Steam's current file, and the launcher's "a game
update needs a Spidy update" pointed the wrong way. The game line now names
the file's version against 4.630.0.0 and the fix, from Steam's app manifest
beside the game. If Steam is set to a beta (the game has two rollback betas,
`previous_version` from October 2023 and `previous_version2`, v1.1212.0.0),
it says to choose None under Properties > Betas. If an update is waiting, it
says to let Steam install it. Otherwise, for an older or changed
`Spider-Man.exe`, it says to verify the game's files in Steam, and for a copy
Steam did not install, to choose the one in the Steam library. Only a newer
version still says a game update needs a Spidy update.
`tools/inspect_game.py` keeps the version as `EXPECTED_VERSION` beside the
hash. No protocol change. 16 launcher checks pass (3 new: a block of Steam's
text, the beta and the update in an app manifest, every message). The
launcher's own scan, run on copies of the game's file set to 1.1212.0.0 on
that beta, 3.618.0.0 with an update waiting, 4.630.0.0 with one byte changed,
and that copy outside a Steam library, gave each message; the real file still
reads as supported. Not in a release yet: players see it from the next one.

**Preceding build (October 8, thirteenth build): flips are an experimental
setting, off by default.** You asked to put the flips in the settings as an
experimental option, off by default. The SPIDY VR tab in the game's Settings
has a new last section, EXPERIMENTAL, with one switch: FLIPS, OFF until you
switch it on. The launcher's options have the same switch, "Flips
(experimental)", and the next session starts with whichever you left it at
(launcher.ini `flips=`; `Launch Spidy VR.cmd -Flips`, run_game_vr.py
`--flips`). Off, A in the air does nothing, as before the flips; switched off
mid-flip, you are level at once. The flips themselves are the twelfth
build's (a tap flips you, held the left stick turns you). XrConfig and
XrData went to version 16 (options bit 11, settings bit 32: flips), so the
launcher, the Python tools and the modules must come from the same build;
the session report's `vr_settings` has `flips`. 199 core checks (1 new: off
by default, switched off mid-flip, kept through a reset; the tab's checks
now count four sections), 13 launcher checks (the `--flips` switch, `flips=`
from the headset) and the 5 Python suites pass. `tools/probe_menu.py` now
also switches FLIPS on and expects RESET ALL to turn it off (16 changes); it
has not run in the game with this build. Not tried in the headset.

**Preceding build (October 8, twelfth build): holding A turns you only with the
stick.** You said holding A just started you spinning nonstop, super fast,
the way the stick pointed. Your 17:12 session shows it: held, the flip went
round once every 0.7 s for as long as you held it, eight turns in a row at
one point. Now, held in the air, A hands the left stick the turning, the
way the Spider-Lair's hold mode does: the stick turns you head over heels as
fast as you tilt it (a whole turn in 1.5 s at full tilt, slower tilted
less), and at rest it keeps you at the angle you reached, so you can stay
upside down or lie flat. Let go of A and you turn back level the short way
(a turn's worth in a second). A quick tap (under a quarter second) is still
one flip toward the stick, now a whole turn in about 1.2 s instead of 0.8
s; tap again during it and the flip carries on to level. A long press
without the stick does nothing. Landing or a wall levels you within about a
third of a second, as before. A web catching no longer ends a held flip:
the stick is yours until you let go. Only the stereo module's turning
changed: no protocol change. 198 core checks pass (the five flip checks
rewritten for holding: the stick's speed and its resting angle, letting go,
taps with and without the stick, a tap during a flip), as do the graphics
test and the 5 Python suites. Not tried in the headset yet. Tell me whether
1.5 s a turn at full tilt is the right top speed, and whether coming back
level on letting go feels right.

**Preceding build (October 8, eleventh build): A in the air flips you.** You
asked for a flip like the Ultimate Spider-Lair's (in VRChat), just the
rotation, without changing any control you already have. Since the ninth
build A in the air did nothing, so the flip lives there. Press A in the air
and you turn head over heels toward where the left stick points: ahead (or
with the stick at rest) a front flip, back a backflip, to a side a
cartwheel. The world turns about your head, so your eyes stay where they
are. A tap is one whole turn in 0.8 s. Keep A held and you keep turning,
the left stick steering the flip instead of moving you; let go and you
finish the turn you are in and come out level (let go just past level after
a whole turn and you go back instead of round again). A web that catches
meanwhile, or one already holding you, ends it the same way, so A pressed
while swinging is one flip. Landing, or a wall or ceiling the game sticks
you to, brings you back level the short way within 0.3 s; a menu, a
cutscene or a recenter puts you level at once.
What stays the same: A on the ground, a wall or a perch is the jump, and a
jump held into the air never turns into a flip. The right stick, grips,
triggers, B, X, Y, the menu button and the stick clicks do what they did.
The left stick moves you as before, except while A is held in the air.
There is no setting for flips (you asked for only the rotation): leave A
alone in the air and you never flip.
While you are turned, your hands, aim markers and web balls follow the
turned view. A sharp pull still zips toward the anchor upside down, and a
punch is still measured from your arm: both used only your turn left and
right before, so upside down a pull would have counted backwards. The spin
itself is not a punch or a pull. Spider-Man's body turns with you about
your eyes, his legs keeping the game's pose.
The swing input and the body command carry the flip's tilt now (swing
command version 3, body command version 2). Both modules still take the
probes' older, shorter commands, so the Python tools are unchanged; the ray
and stereo modules must come from the same build. 198 core checks pass (11
new: a tap, hold and release, the stick's directions, landing and menus,
the rig's math, an upside-down pull, a flip that is no punch, the aim
markers, the swing command, the body turning), as do the graphics test, the
launcher's checks and the 5 Python suites. The rebuilt modules took the
probes' old commands and the new ones and refused a bad tilt. Not tried yet:
the game and the headset. Tell me whether 0.8 s a flip feels right, whether
flipping makes you uneasy, and whether steering a held flip with the stick
is useful.

**Preceding build (October 8, tenth build): web balls web thugs up, pulled
thugs fly and get hurt.** You said web balls at enemies and objects did
nothing, and that pulled enemies came along without reacting to the web or
taking collision damage. Your 13:50 session and a headless game at a street
crime showed why. The web balls did hit: 30 of your 58 shots went at thugs,
and in the game every one of Spidy's shots at a thug struck him. But a web
ball carries nothing of its own: its damage is empty, and the game webs an
enemy only through its own firing code, which Spidy's shots go around. Pulls
asked the game for its "flung" reaction, which it accepts and then ignores
for a thug who is fighting (his AI keeps him aiming), so the web slid him
along on his feet.
Now a web ball that strikes a thug deals him the webbing the game's own web
hits deal (a kWebImpact blow carrying 11 of webbing). A street thug is webbed
up at 30, so three quick hits web him up and he struggles in his webs, the
game's own reaction; the webbing wears off within seconds, so slow single
shots do not add up. The first pull on a thug deals him the game's kinetic
knock-back, which throws him into the flailing flight it uses for thrown
enemies, and from the next frame the web steers that flight: yanked, he
flies flailing to your hand; held, he hangs and swings on the web; thrown,
the game flies him on and lands him (a flop, then dazed on the ground). If
he comes down while still on your web, he lies there until the next pull
knocks him up again (1.5 s at the soonest). What flies now hurts what it
strikes: a thug whose flight is stopped short by a wall or a car (flying 7
m/s or faster, then keeping under 45% of it, and not because the web braked
him), or who lands at 8 m/s or faster, takes a blow by the speed he lost
(14 m/s and up knocks him down); a flung thug or a prop you throw that
strikes another thug at 6 m/s or more knocks him down, or from 9 m/s flings
him. Blows are 2 to 15 of the game's damage, which it doubles on its normal
difficulty (a street thug has 60). Civilians, and thugs the game will not
fling (scripted scenes, heavies), are moved on their feet as before.
Objects: still no. All 147 throwable props near your save are breakables
(trash cans and the like): the game's damage does not reach them (a kinetic
blow of 30 was dropped), and pushing one shows the sinking-bin glitch. A web
ball knocks only a throwable that is not breakable, and none was found.
Measured with a research DLL at a crime with 7 thugs (each tried several
ways, see VALIDATION.md): the webbing webbed thugs up; the kinetic
knock-back flung a fighting thug every time while melee or explosion
knock-backs only staggered him; steering the flight pulled a thug 7 m toward
the hero and held another 2.4 m up for 4 s. Then with Spidy's own modules:
its web balls struck a thug and a trash can, said so in their telemetry and
named what they hit. The reports gained the shots' collisions, webbing
blows, props knocked and the latest actor hit, and the grab's knock-backs,
steered flight steps, impacts and thugs struck (the shooter's and the grab's
telemetry changed: the ray and XR modules and the Python tools must come
from the same build). 185 core checks (2 new: when a flight struck
something and how hard, and what a flying body touches) and the 5 Python
suites pass. Not tried yet: Spidy's own pulls on thugs in the game (no thugs
came near on the second run) and the headset. Tell me how webbed thugs look,
whether three hits feels right, and whether pulled thugs fly, land and get
hurt the way you expect.

**Preceding build (October 8, ninth build): A in the air no longer shoots the
game's web zip.** You asked to turn off the game's own web shooting when you
press A in the air or mid-jump. That is the game's web zip: in the air its
jump button shoots a web from Spider-Man's wrist and zips him forward,
whatever your hands are doing. Spidy gave the game your A wherever you were,
even while its own webs flew you. Now a press of A that starts in the air
(swinging, flying after letting go, or in the game's own jump or fall) never
reaches the game, until you let go of it, a landing on the way included. A
press that starts on the ground, on a wall or on a perch is the game's jump
as before, for as long as you hold it. Telling the air from a perch: the
game's mover says "airborne" on some perches too, so Spidy counts you in the
air while its webs fly you, or while the mover is unsupported and in the mode
the game's air state moves it in. In the reports of your October 6-8
sessions that matched the game's own air state on 98.6% of 61,657 steps; the
rest were a jump's first steps (no web zip there now either) and single
landing steps (A jumps there). Perches and wall crawls never counted as air.
183 core checks (2 new) and the 5 Python suites pass. Not tried in the game
or the headset yet.

**Preceding build (October 8, eighth build): steadier aim markers, a colour per
hand, a new target ring.** You said the aim markers felt jerky and jittery,
asked for a different colour shade per hand, and called the square target
marker ugly. The jitter came from the controller: a hand held still still
trembles and the tracking is a little noisy, which swings the aim ray by
about 0.4 degrees peak to peak with a typical tremor, about as much as the
ring is wide in the headset. On top of that the markers' hard pixel edges
crawled as they moved, a marker swapped shape the moment the game's preview
changed, and a target's corners moved in the game's own steps.
Now the hand's aim ray is steadied before the marker goes on it, by a
speed-adaptive filter (the "1 euro filter"): it smooths hard while you hold
still and less the faster you move. Simulated with that tremor, the wobble
drops from 0.4 to 0.1 degree; aiming slowly the marker trails by about 0.1
degree, sweeping fast by about 0.3, and it settles within 30 ms of stopping.
It works in your tracking space, so snap and smooth turns do not make it
swim. A marker eases to a new distance in about 30 ms, a target's ring
slides to the next target, and a marker of another kind fades in (it shows
at once and is whole within 25 ms) while the old one fades out (80 ms), so
an edge where the aim flickers between two kinds no longer pops.
The markers have soft edges over a soft dark shadow (the overlay gained a
translucent pass), the left hand's in sky blue and the right hand's in
orange; a miss stays a red cross. The amber corners are gone: a prop or thug
you would catch gets a ring of three arcs with claws pointing in, turning
slowly (faster as you squeeze) and closing in from wider as it locks on. The
right hand's ring is a little wider and turned between the left's, so both
hands on one thug stay apart.
181 core checks (2 new: the steadying against tremor, sweeps, turns and
gaps; the fades and eases; and the marker check reworked for shadows first,
soft edges, each hand's colour, fading, and a round target ring that closes
in), the launcher's checks, 87 Python checks and the GPU test (both hands'
markers on the dark scene, a bright sky and a lit wall, each in its colour,
shadowed on the bright panels; D3D12 debug layer clean) pass. Not seen in
the headset yet: tell me if they still wobble when you hold still, lag when
you sweep, or if the colours or sizes want changing.

**Preceding build (October 8, seventh build): calibrate your body in a T-pose.**
You asked for an in-game calibration on first start, and as an option in the
VR menu, where the player stands in a T-pose and holds the triggers so the
avatar is scaled to their measurements. Until now Spider-Man's body took its
size from the highest your headset had been and kept his own arms: with
longer arms than his, your controllers ran ahead of his hands; with shorter
ones, his elbows stayed bent.
Now, the first time VR shows the game, a panel appears ahead of you: BODY
CALIBRATION, stand tall and look ahead, arms straight out to the sides, hold
both triggers. A line under it says what to fix (an arm not out to the side
or bent, the head turned, a trigger let go, moving), a figure's arms and a
ring around each controller turn green when that arm is right, and a bar
fills while you hold the pose. After a second and a half it says CALIBRATED
with your eye height and arm length, both controllers buzz, and Spider-Man
is resized at once: his height from your eye height, then each arm about its
shoulder, so his wrists reach yours when your arms are straight. B skips it.
While it shows, the triggers, grips, A and B do nothing in the game, so
holding the triggers shoots no web balls, until you let go after it.
Settings > SPIDY VR has a BODY section again, with one row, **CALIBRATE
BODY**: set it to ON RESUME and resume to calibrate again. The launcher keeps
your measurements and starts every next session with them, so the panel comes
once; its options show them, and Redo forgets them so the next session asks
again. A skip is remembered as well. `Launch Spidy VR.cmd` takes
`-EyeHeight 1630 -ArmLength 590` (the console's last settings line has your
numbers) or `-NoCalibrationPrompt`.
The panel's text is a new stroke font in Spidy's overlay; I checked how the
panel reads on the GPU test's eye image. 179 core checks (7 new: arms scaled
about their shoulders reaching farther controllers, the hero's proportions
and the scales' ranges, a T-pose measured, every panel line, the hold pausing
and starting over, the panel and the font), 13 launcher checks, 87 Python
checks and the GPU test (the panel and the result drawn) pass. Checked in
the game without a headset, on your save: the SPIDY VR tab showed the BODY
section and CALIBRATE BODY, and `tools/probe_menu.py` switched it to ON RESUME
and RESET ALL back to NO (its help then wrapped "T-pose" at the hyphen, so I
reworded it). A new T-pose phase of `tools/probe_game_body.py` posed
Spider-Man's real body as the calibration would for eyes 1.66 m up and arms
0.62 m long: with his own arms his wrists stopped 7-8.5 cm short of the
controllers, with your arm length 0.8-2.7 cm from them (his standing pose
holds the shoulders a little higher than his rest pose, which the measurement
assumes). Your save files were unchanged except `slot0-s.save`, which the
game saved itself. This build changes the VR protocol (the XR config and data
are version 15, the body's status version 2), so it can only go into your
play folder as the whole package; you said not yet. The panel itself needs
the headset. Details: [docs/VALIDATION.md](VALIDATION.md),
[docs/BODY.md](BODY.md).

**Preceding build (October 8, sixth build): five options removed.** You asked to
remove "Webs catch props and thugs", "Web shooter", "Your own body", "Punch
thugs" and "Webs drawn by" from the options. All five are gone from the
launcher's options, and the four that Settings > SPIDY VR had are gone from
there too, with its BODY heading. The tab now has two sections: WEBS (aim
markers, webs hold in open air, swing speed limit, weight) and COMFORT (snap
turn, smooth turn, controller vibration, game screen size). Catching props
and thugs, the web shooter, your body and punching are always on, and the
game draws the webs. If you had switched one of them off, the launcher
forgets that: it no longer reads, saves or passes them on, and ignores them
in the settings a session ends with. `Launch Spidy VR.cmd` keeps `-NoWebGrab`,
`-NoWebShooter`, `-NoBody`, `-NoPunch` and `-OverlayWebs` for testing, and
RESET ALL in the tab leaves those as the session started. The VR protocol is
unchanged (version 14), so only the launcher and the XR module change.
172 core checks, 12 launcher checks and 86 Python checks pass. Checked in
the game without a headset, on your save: the SPIDY VR tab showed the new
rows, and `tools/probe_menu.py` passed every step through them (12 changes;
RESET ALL left the web shooter as the probe started it). Your save files
were unchanged except `slot0-s.save`, which the game saved itself when the
probe left the pause menu. It is in your play folder (`dist\Spidy-0.2.2`):
the launcher, the XR module and README.txt. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 8, fifth build): a WEIGHT setting for swinging.** You
said moving through the air feels too floaty and asked for a weight setting
in the game's settings. While webs fly you (swinging, and after you let go,
until you land) Spidy decides how fast you fall, not the game, and since
October 5 it has pulled at 6 m/s², about 60% of real gravity. That is the
floatiness. The number kept what the October 4 builds applied in practice
(they were set to 18 but applied a third of it), the swing feel you approved
then. Settings > SPIDY VR has a new row under SWING SPEED LIMIT, **WEIGHT**:
40%, 60% (the default: the feel you have now), 80%, 100% (real gravity), 125%,
150%, 200%, 250% or 300% (about the game's own falls, which pull at 30 m/s²).
A change takes hold at once, mid-swing too. Heavier, you drop faster after
letting go, and swings run faster and shorter. Jumps and falls without a web
stay the game's own. The launcher's options have a matching "Weight" list,
`Launch Spidy VR.cmd` takes `-Weight 150`, and the next session starts with
whatever you left it at in the headset. A heavier weight reaches the swing
speed limit sooner (at 300%, a fall from rest reaches 32 m/s in about a
second), so raise that limit too if you feel it cap your falls.
Checked in the game without a headset, on your save: the SPIDY VR tab showed
the row, the left stick stepped it from 60% to 80%, and RESET ALL put it back
(`tools/probe_menu.py`). A new probe (`tools/probe_weight.py`) jumped off your
perch, swung up on a web, let go, and changed the weight mid-flight as the
tab does: Spider-Man fell at 5.88 m/s² at 60%, 14.72 at 150% and 29.53 at
300% (asked: 5.89, 14.72 and 29.43), with every physics step in the air under
Spidy's control. Your save files were unchanged except `slot0-s.save`, which
the game saved itself when the probe left the pause menu, before the jump.
172 core checks (new: the swing taking a new gravity during play, the row and
its steps), the launcher checks, 86 Python checks and the GPU test pass. This
build changes the VR protocol (the XR config and the XR data are version 14),
so it went into your play folder as the whole package, built from the
repository as it is now (the fist fix below included). How each weight feels
needs the headset. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 8, fourth build): fingers no longer twist in a
fist.** You reported that your fingers in VR look twisted and tangled in
most poses. They did whenever a hand made a fist: a squeezed grip (every
web) or a hand moving faster than 1.2 m/s, so most of play. The hand
snapshots of your October 7 23:11 session show it at 64-68 s: the right fist
a claw with its fingertips bent up out of it, the left hand's fingers bent
back out and crossing. Two faults. Each finger joint bent toward the palm
about an axis taken from the bone before it, and that axis turns over once a
bone has curled past the palm's normal: in a full fist every fingertip bent
60° backward, and a fist the game had already closed folded its middle
joints back through themselves. And the solver assumed the rig's rest pose
holds its palms down; Spider-Man's face 30° from down, toward the thumb
(measured from his knuckles, and the game's own finger bends agree), so his
fingers closed diagonally across the palm into each other, and each hand sat
rolled 30° on its controller. Now the palm comes from the knuckles, and each
finger joint turns about its finger's own hinge to a set bend (83°, 95° and
63°, part of the way for a hand closed part of the way); the thumb bends at
its two joints in one plane and swings its tip onto the index and middle
fingers. Spider-Man's own hands in the solver, before and after: the palm
29.9° off the controller's, now 0.0°; a full fist's knuckle, middle and tip
joints 82°, 96° and −60° on every finger, now 83°, 95° and 63°; no joint
bends back any more, from straight fingers, fingers bent back or the game's
own fists. Your hands now sit 30° differently in your grip: tell me how that
feels. 171 core checks (3 new, with his real hands) and 86 Python checks
pass. Not yet run in the game or the headset (`tools/probe_game_body.py
--fists 0.5,1` reports each hand's palm and joints), and not in your play
folder: the fix is in the XR module, so it needs the whole package there,
or a release. Details: [BODY.md](BODY.md#twisted-fingers-october-8).

**Preceding build (October 8, third build): render resolution above the
headset's.** You asked for a way to render above your headset's resolution:
the game looks blurry and jagged, and the launcher's "Eye resolution" went
no higher than "Headset default". Two Discord reports point the same way. A
Quest 3 player over Virtual Desktop rendered 2048 x 2048, a choice that
looked larger than "Headset default" but was below the 2496 x 2688 their
headset asked for. Another player's Virtual Desktop asks for 4032 x 3648,
just under the 4096 a side Spidy allowed. The launcher's option is now
"Render resolution": a percentage of the headset's own resolution per side,
50% to 200% in steps of 5, 100% by default. After Check in the Headset row it
shows the pixels per eye (125% of 3072 x 3264 is 3840 x 4080). The fixed
square sizes are gone, and one saved by an older launcher is dropped, so
that player starts at their headset's resolution again. Eyes may now be up
to 8192 pixels a side; a size the VR runtime does not take shrinks to its
largest with the same shape. Aim markers keep the size they have at 100%:
they are drawn so many pixels wide and would have shrunk with the scale. The
console says "Rendering 4608 x 4896 pixels per eye (150% of the headset's
3072 x 3264)", the report keeps `render_scale` and the headset's
`recommended_eye`, and the launcher's memory figure and the console's
memory warning grow with the scale.
Measured in the game without a headset (`tools/probe_vr_load.py`, your save,
eye occlusion on as in VR, a fresh game for each size): at 100% (3072 x 3264)
90 frames a second looking ahead and 103 turning, GPU 85% busy; at 150%
(4608 x 4896) 66 and 71, GPU 92%. That is about 30% fewer frames, with the
GPU setting the rate. 150% took 2.5-2.6 GB more video memory and as much more
Windows commit, about 108 bytes per extra eye pixel. The game made both
eyes' buffers and viewports 4608 x 4896, and the left eye's image is whole:
no black or cut-off part. Your October 7 session got 77 new eye pairs a
second at 100%; 30% fewer would be well under 72 Hz, so start at 120-130%.
The game screen's copy into the headset image now samples at each pixel's own
centre: at 4608 x 4896 its interpolated coordinate drifted and changed copied
values by 2 levels. XrConfig version 13 (632 bytes: `renderScale` and a spare
word); `-RenderScale 125` for `tools\launch-game-vr.ps1`, `--render-scale` for
`run_game_vr.py`. New checks: the scale's eye sizes (core, and the same cases
in Python), the launcher's arguments and headset size, the scale's limits; 86
Python checks and the GPU test pass, the GPU test now at every size from 1536
to 8192 x 8192. Not in your play folder yet: version 13 needs the whole
package there.

**Preceding build (October 8, second build): a wall the game sticks you to no
longer cuts through your view.** You reported that the player sometimes gets
attached to walls in VR, which looks and feels like clipping through the
wall, until you jump. That is the game's wall crawl: a player who flies into
a wall sticks to it. The game turns the hero onto the wall (his up along the
wall's normal) with his feet on its surface, and VR placed your eyes upright
from those feet: on the wall's plane, or inside it by as far as your head
was toward it in your room. The left eye of your October 7 22:08 session at
180 s shows it; your October 6-7 reports hold six such stretches, 1-2.4 s
each, 20 to 200 m up. Now, while the game holds you on a wall, your head
stands off it: 0.5 m out, reached in about 0.2 s. From there you can lean in
to 0.25 m; lean back and the wall stays where it is. Under a ceiling the head
goes 0.5 m below it. Back on your feet, or jumping off, the view returns over
the feet. Hands, webs and the body go with the head; crawling and the jump off
are the game's, as before (`GameTrackingRig`, `wallClearance`).
Measured in the game without a headset (`tools/probe_wall_crawl.py`, your
save, a wall 6 m from the player, reeled into on a web): the game took the
player at the wall and turned him from upright to 90° in 0.25 s, feet 0.00 m
from the wall's surface; eyes placed as before were 0.00 m from it, as now
0.50 m. On the wall the hero rocks up to 14° about the wall's normal and
snaps back several times a second, so the stand-off is level (straight down
under a ceiling) and does not bob with him. He was upright 0.3 s after the
jump off; Spidy's own flight never tilted him. Session reports carry
`surface` per sample (stretches and frames on a wall, the hero's up, the
stand-off, the head's distance from the wall without and with it; XrData
version 13). 167 core checks (5 new), 84 Python checks and the GPU test pass.
Not in your play folder yet: version 13 needs the whole package there.

**Preceding build (October 8, first build): "Game XR start: 1000" says what to do.** A
player on a Quest 3 through Virtual Desktop got "VR could not start: Game XR
start: 1000". Their game had started at 09:14:46 and the launcher attached
to it at 09:18:21 ("Using the running game."), so VR had already run once in
that game: 1000 is the XR module refusing a second session in one game
process, as every Spidy module does (AUTOMATION.md). STOP VR, or VR that
ends by itself, leaves the game running, and the next START VR attached to
it. The launcher now checks that first, before the headset: a running game
that already holds Spidy's XR module gets "VR already ran in this game, and
Spidy VR starts once per game launch. Close Spider-Man, then press START VR:
the launcher starts the game again.", and a 1000 from the XR start says the
same. PLAYERS.md says it under PLAY and IF SOMETHING IS WRONG. Starting VR
again in the same game would need every module to start more than once; not
done. 84 Python checks pass (new: the check, its message, and no headset
check before it). Only the Python tools and PLAYERS.md changed.

**Preceding build (October 7, seventh build): SteamVR headsets, and the VR
runtime found automatically.** You reported that players with SteamVR
headsets (a Steam Frame, a PSVR2) get the flat game instead of VR, and that
on your Quest 3 over Steam Link VR works but the game's menus can't be
played. Two different causes.

The menus. In your 23:11 Steam Link session the game found two Steam
virtual gamepads (Valve 28de:11ff) through Windows.Gaming.Input six seconds
after it started, logged "XInput disabled, disconnecting controller", and
never read Spidy's controller again: 0 reads in the whole session while
your presses reached Spidy, against 52,644 in your 22:08 Virtual Desktop
session. Spidy gives the game the VR controllers as an XInput controller.
The game reads a setting at startup, EnableWindowsGamingInput in its
registry key, and its code skips Windows.Gaming.Input altogether when that
is 0. The launcher now sets it to 0 for every game it starts and puts your
value back when the game closes, as it does with the small desktop window;
XInput and DualSense controllers work as before. It also turns frame
generation off for VR sessions (yours was already off) and puts it back
afterwards. If the game screen still ignores the controllers (a game started
outside the launcher, or one behind another window), the launcher says so.

The flat start. The Steam Frame player's reports folder (release 0.2.1)
holds only the launcher's first steps: the saved desktop view and two
modules, the second being the probe that finds the game's graphics queue.
No session report and no other module, so the launcher stopped while
looking for that queue and the game stayed flat. On your PC the same step
under SteamVR took about a second (VR started 7 s after the game, as with
Virtual Desktop), so something on their PC differs, and only the launcher's
window said what. The launcher used to give up after three minutes unless
it saw exactly one graphics queue; an overlay or capture tool drawing on a
queue of its own from inside the game's present, or frame generation, makes
that fail every time. Now it takes the queue the game submits on itself
(the others sit deeper in the call stack, or take a quarter of its work or
less) and gives up only with a list of the queues it saw; a call the game
never answers ends the wait at once with that reason; and the search says
what it sees every 20 seconds. Whether this was their cause is not known
yet. So every session now also writes what the launcher's window shows to
`reports\game-vr-<time>-console.log`, and a session that ends before VR
starts writes a small report saying where and why, with the game's own log
and the modules loaded into the game (overlays among them). The next
reports folder from a Steam Frame or PSVR2 player will say what stopped it.

The VR runtime. The launcher's list starts with Automatic, now the default
for everyone. Each session asks Virtual Desktop first (asking it starts
nothing), then SteamVR and Meta Quest Link if their service is running,
then Windows' active runtime, and uses the first that has a headset; a
runtime that is not running and not Windows' active one is named instead of
started. Check shows which runtime has the headset ("Connected: ... via
SteamVR"). Choosing a runtime from the list works as before, and session
reports now record the runtime and headset (`xr_runtime`).

162 core checks, 11 launcher checks (new: the headset check's line), 82
Python checks (new: Windows.Gaming.Input and frame generation off and back,
older backups restoring, the runtime order and what is never started, the
game's queue among several, the queue wait's failures, the warning for an
unread controller, the report of a session that ends before VR, the console
log) and the GPU test pass. Run without a headset, a session that found no
headset in Virtual Desktop wrote its console log and its report. Not yet
seen: the menus over Steam Link with this build (your headset), and a Steam
Frame or PSVR2 session. No VR protocol change: only the launcher and the
Python tools changed.

**Preceding build (October 7, sixth build): smooth turning.** You asked for a
smooth turning option. Settings > SPIDY VR has a new row under COMFORT,
**SMOOTH TURN**: OFF (the default, so the right stick snap turns as before),
or 60, 90, 120, 180 or 240 degrees a second. With it on, holding the right
stick left or right turns you steadily instead of in steps: the further you
tilt it, the faster, up to the chosen speed from nine tenths of the way, and
nothing inside the stick's first fifth. You turn about your head, as with a
snap turn, and Spider-Man's body turns along with you. A game hitch does not
stop a turn you are making, but a stick still held when you come back from a
menu turns nothing until you let it go. The launcher's options have a
matching "Smooth turn" list, `Launch Spidy VR.cmd` takes `-SmoothTurn 120`,
and the next session starts with whatever you left it at in the headset.
Turning also no longer counts as moving your arms for punches: a fist held
out while you turn sweeps through the world at up to 2.5 m/s at 240°/s,
faster than a punch, and it could have knocked a thug over; snap turns had
the same flaw for one frame each. Now the punch measure turns with you and
counts only what your arm does. 162 core checks (new: smooth turning's speed
by tilt and its pivot at the head, a stutter going on turning and a held stick
waiting after a break, turning with a still arm being no punch while a punch
during a turn still is, the new row and its steps), 10 launcher checks, 72
Python checks and the GPU test pass. How smooth turning feels, and the speeds
being right in a headset, need the headset. This build changes the VR
protocol (the XR config and the XR data each have a new version), so it runs
only as a whole package. Details: [docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, fifth build): no more sudden dives in midair.** You
reported being pulled down fast in the air, out of nowhere, as if diving. It
was the game's own fall: while Spidy flies you, the game's airborne state
keeps counting the time you have been in the air, and whenever Spidy let go of
you in midair the game took over at the fall speed that time gives, 36 to 48
m/s down from one frame to the next, and your webs were gone. Your last six
session reports have 14 such handoffs in about 45 minutes of retained play,
half of them straight to 36-48 m/s down. In the 14:13 session there were
three: two during game frames of about a tenth of a second (a frame over 100
ms closes the gate that tells Spidy you are playing, and Spidy took it for a
menu and let go), and one when you resumed from the pause menu in midair.
Now Spidy keeps the player it flies. A hitch keeps your input and your webs
for up to half a second, as if the frame were just late. A longer break
without input (a menu that does not pause the game, lost tracking, the switch
to the flat screen) lets go of the webs, and you glide on under Spidy's own
gravity until you land, where the game takes over as it always did. A
physics step that comes late (a long frame, the end of a pause) still gets
Spidy's last command. And nothing else hands you over in midair any more: a
physics step Spidy did not see, or a gap in the controller samples, used to.
Measured in the game without the headset, on your save, by a new probe that
jumps off your perch, swings up on a web, reels, lets go and then forces each
case. With the build in your play folder, 0.3 s without input while reeling
upward at 11.6 m/s ended the swing: 138 physics steps ran on the game's fall,
Spider-Man went down at 46.9 m/s, and the web was gone. With this build he
went on reeling with the web held, then flew on through a 0.25 s freeze of
the game, 2 s without input and a 3 s freeze like a pause: every physics step
in the air under Spidy's control, the vertical speed changing by at most 0.09
m/s from one step to the next, falling under Spidy's 6 m/s² while gliding.
Your save files were unchanged. 159 core checks (new: a stutter keeps the
input and a longer loss does not, the webs survive a stutter but not a longer
break, the first controller sample after a gap is no yank and keeps the web,
a movement command reaches a late step), 10 launcher checks, 72 Python checks
and the GPU test pass. How gliding after a long break feels, and the drops
being gone in a real session, need the headset; the session report shows them
as physics steps under Spidy's control while the input is unfocused. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, fourth build): VR settings in the game's own
Settings.** You asked for the in-game VR settings as real items in the game's
settings menu, not a separately drawn drawer. They are now a tab of the game's
own Settings: pause with the menu button, choose Settings, and **SPIDY VR** is
the last tab, after KEY MAPPING (Up from GAME reaches it in one press, since
the list wraps; the game reopens Settings on the tab you last used). The game
draws the tab with its own code, so it looks and works like its other tabs:
the sections WEBS, BODY and COMFORT, switches showing the game's ON and OFF,
the swing speed limit, snap turn, controller vibration and game screen size
as lists you step with left and right, each setting's explanation beside the
rows, X to reset the selected setting and Y to reset the whole tab (after the
game's own "Are you sure?") to Spidy's defaults. Changes apply at once, and
the launcher starts your next session with them, as before. A swing speed the
launcher set between two steps (say 33 m/s) shows as the nearest step and
stays as set until you change it. The panel beside the game screen, its tab
and pointing at it are gone, so both triggers always reach the game's menus.
The title screen's Options have no SPIDY VR tab: set things in the launcher,
or pause once your save is loaded. When the pause menu's Settings hand their
tabs to the game's Flash interface, Spidy adds one more, built by the game's
own tab builder from items copied from its settings config but numbered past
the game's 123 settings; Spidy answers the game's questions about those
numbers itself, so the game's own settings and its settings file never see
them. Measured in the game without the headset, on your save, steered with
the virtual controller's left stick as the Touch controllers do: SPIDY VR
appeared after KEY MAPPING and showed the session's values (33 m/s as 32 M/S,
the web shooter off); right on AIM MARKERS switched them off, on SWING SPEED
LIMIT stepped it to 40 M/S, on YOUR OWN BODY switched it off and X put it back,
on SNAP TURN stepped it to 45°; Y then A put back Spidy's defaults, the web
shooter on again; each change reached Spidy's values at once; backing out
resumed play. With Spidy's hooks taken out, Settings showed the game's eight
tabs only; put back in, SPIDY VR returned. Every hook was restored and your
settings file was unchanged (the game re-saved its autosave on Continue, as
it does on every load). 155 core checks (new: the tab's rows and choices,
launcher values between steps, RESET to the defaults), 10 launcher checks, 72
Python checks and the GPU test pass. How the tab reads on the headset's game
screen, and the XR worker taking its changes during a session, need the
headset. This build changes the VR protocol (the XR config and the XR data
each have a new version), so it runs only as a whole package. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, third build): the web shooter.** You asked for the
web projectiles Spider-Man shoots in the game, the web balls of his gadget
button, not the swinging webs. Pull the **trigger** of a hand whose web is not
attached and that hand shoots one: the game's own web-shooter shot, leaving
from your wrist where the controller points. Aimed close to a thug (within 7
degrees, or within his own width when he is near) with nothing in between,
the ball goes to him and the game takes him as its target; otherwise it flies
to the first thing on the hand's line and splats there, or on through open air
until it ends, about 60 m out. One ball per pull: a held trigger shoots once,
and pulls can follow each other every 0.12 s. A short tick in that hand tells
you it left. The trigger still reels in whenever that hand's web is attached,
so the other hand can shoot while one hand swings; a trigger held from a reel,
a menu or a tracking loss shoots nothing until you let go. It is **Web
shooter** under WEBS in the headset's VR settings and in the launcher's
options, on by default; `Launch Spidy VR.cmd -NoWebShooter` starts without
it. Spidy hands each pull to the hero's own web-shooter gadget on the game's
main thread, with the hand's position and the aim it worked out, the way the
gamepad's gadget button hands it a fire event: the game makes the shot, its
look and trail, and what it does to whatever it hits. Spidy's shots take no
gadget ammo and play no arm animation, since the arm is yours. Measured in the
game without a headset, on your save (perched on a lamp post in Times
Square): a scripted hand pulled its trigger 12 times through the same input
as your controllers; each pull fired exactly one shot, from exactly the hand
(0.00 m), within 0.01 degrees of its aim, at 52-60 m/s; aimed 30 degrees down
it struck the pavement 6.5 m away, sideways a wall 10 m away, level and at the
sky it flew 60 m in one second and ended; a trigger held for a second shot
once, three pulls 0.2 s apart three times; switched off and on again during
play it went on shooting; no fault, every hook restored, your save files
unchanged. Captured from the game window, the ball crosses the street as a
white streak. No thug was in free roam, so a ball hitting a thug, and the game
taking the thug you aimed at as its target, await a fight in the headset (the
session report counts both). 158 core checks (new: one shot per pull, none
from a hand whose web is busy or from a trigger held through it, the
interval, the aim assist taking the thug nearest the line but none behind a
wall, surface and open-air aims), 10 launcher checks, 72 Python checks and the
GPU test (the panel one row taller again: 840 x 1410 pixels) pass. This build
changes the VR protocol (the XR config and the XR data each have a new
version), so it runs only as a whole package. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, second build): webs in open air are a setting.**
You asked to make hooking a web at maximum distance without hitting anything
(webs on nowhere) an optional setting, on by default. It is **Webs hold in
open air**, under WEBS in the headset's VR settings and in the launcher's
options, and it starts on. On, webs work as before: a squeeze whose ray meets
nothing within 100 m holds the web in open air there, and the aim marker is
the faint dashed ring. Off, that squeeze misses, as one aimed at a car does,
and a hand aimed into open air shows no marker; webs on buildings, the ground
and props are unchanged. Switched in the headset it applies from the next
squeeze (a web already holding keeps its anchor), and the launcher starts
your next session as you left it; `Launch Spidy VR.cmd -NoAirWebs` starts
with it off. The panel is one row taller (0.9 x 1.4 m). Measured without the
headset: 153 core checks (new: switched off during play, a squeeze into open
air misses and its preview shows nothing, a web already holding keeps its
anchor, a surface still holds, and switched on again open air holds), 10
launcher checks, 71 Python checks, and the GPU test (the panel painted at
840 x 1311 pixels, the new switch showing its own value). In the game, on
your save: a hand aimed at the sky would hold in open air at 100 m; with the
switch off the same aim had nothing to hold (a squeeze would miss), and
switched on again open air at 100 m; no fault, every hook restored, your save
files unchanged. How the new row looks and clicks in the headset is pending.
This build changes the VR protocol (the settings call, the XR config and the
XR data each have a new version), so it runs only as a whole package.
Details: [docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, first build): VR settings beside the game's menus.** You asked
for a VR settings section in the in-game menu. The game draws its menus itself
and Spidy draws nothing into them, so the section is Spidy's own panel,
hanging beside them in the headset. Pause with the menu button and **VR
SETTINGS** opens to the right of the pause menu, turned toward you; on every
other game screen (the game menu, the main menu, loading, cutscenes) a small
VR SETTINGS tab hangs there instead, and opens it. Point a controller at a
setting and pull the trigger: switch the aim markers, webs catching props and
thugs, your own body and punching on or off, or step the swing speed limit
(10 to 65 m/s), the snap turn (off, or 15 to 90 degrees; until now always 30),
controller vibration (off, or 25 to 100%) and the size of the game screen
itself (small, medium as before, or large; the flat-mode screen too). Every
change applies at once, in the same session: switching web grabbing off lets
go of what a web holds, switching your body off hides the hero and draws
gloves, a new speed limit holds from the next swing. A dot shows where each
controller points, the line under it lights up, and a click ticks in that
hand. While a hand points at the panel its trigger goes to the panel, not the
game; every other control keeps working the game's menu. The X in the
panel's corner folds it to its tab for the rest of the session, until you open
it again. When the session ends, the launcher keeps what you set for the next
one (X for the markers counts too); its options card has the three new
settings, and `Launch Spidy VR.cmd` takes `-SnapTurn`, `-Haptics` and
`-ScreenSize`. Measured without the headset: 152 core checks (the panel's
layout, hit testing, placement and aim rays, one click per pull, a held or
off-panel pull ignored, the tab, snap turn angles), 10 launcher checks, 71
Python checks, and the GPU test, which paints the panel at your headset's
3072 x 3264 eye size (840 x 1212 pixels for a panel 0.9 m wide) and draws it
into the corner of the right eye's image with every pixel unchanged and the
rest of the image untouched. In the game, on your save: with web grabbing
started off, then switched on, off and on again during the session, a hand
aimed at a prop 51 m away would anchor its web there, catch the prop, anchor,
catch it again; punching switched on and off twice; no fault, every hook
restored, your save files unchanged. How the panel looks and handles in
the headset is pending. Details: [docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 6, evening): an interact button, and markers that
show where each web will land.** You asked for an interaction button in VR
and an optional crosshair for better aim. The game puts its context actions
(picking up a backpack, opening a door, working a console, the prompts it
shows) and, in a fight, its web strike on one button: Triangle on
PlayStation, Y on Xbox. In VR that button was out of reach, because the
controllers' Y opens the game menu. Now **B** gives the game its Y while you
stand, walk, climb or fight; not while a web carries you. A press keeps the
meaning it started with until you let go: B still held from a menu, where it
means Back, does not interact, and an interact held into a menu or scene it
opened does not go Back there. Each free hand also shows an
aim marker where a squeeze of its grip would send its web, worked out in the
game with the same rays and target picks the squeeze itself uses: a white
ring with a dot where the web would hold; a faint dashed ring where nothing
is in reach and the web would hold in open air at 100 m; a red cross where it
would miss (a car, or a wall between you and that point); amber corners
around a prop or thug it would catch instead. A marker faces you, keeps its
size on screen at any distance, has a dark outline so it reads against the
sky and lit walls, and tightens as you squeeze the grip. A hand whose web is
attached shows none, nor does a hand pointing at the floor by your feet.
**X** hides or shows the markers in VR; the launcher's "Aim markers" switch
(or `Launch Spidy VR.cmd -NoAimMarkers`) sets how a session starts. Webs
still shoot exactly as before: the squeeze and the marker now run the same
code. Measured in the game without a headset, on your save (perched on a lamp
post beside a building): a scripted hand aimed 15 ways got the building
beside you at 11-16 m, facades across the street at 36 and 60 m, the ground
below, open air at the sky and down the open street, a miss at the lamp
under your feet, and a catch on a throwable prop 51 m away, with no fault
and every hook restored. 146 core checks, the GPU test (which now draws
every marker) and 69 Python checks pass; the headset session is pending.
Details: [docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 6, sixth build): Spider-Man's body is yours, and
your fists hit.** You asked for Spider-Man's actual avatar as a full-body
presence you control, and for physical punching. In VR you are now the game's
own hero model: look down and you see his torso, legs and arms; his hands are
where your controllers are and turn as you turn them; his head is gone,
because you are inside it. The body stands upright under your head, faces
where you look once you turn more than 34 degrees (a snap turn turns it at
once), bends its knees when you crouch with its feet on the ground, and keeps
the game's leg pose in the air. A squeezed grip or a fast-moving hand closes
into a fist. Spidy turns the hero's joints right after the game's animation
writes them (the pose writer 1601290 and the rig's own joint names), so the
game skins and lights the body itself. A fist moving 2.2 m/s or faster into a
thug is a punch: Spidy hands it to the game's own damage system as a melee
blow from the hero, 10-40 damage by the fist's speed, with the game's
reaction for that strength (a flinch, a stagger, a knockdown, a flight; an
uppercut pops him up), and the controller kicks. Measured in the game without
a headset, on your rooftop save: both wrists landed 0.0 mm from scripted
controller targets and the head joint 0.0 mm from its place behind the eyes,
in 0.03 ms a frame; from the eyes, looking down, you see his arm reaching to
the controller with a closed fist, the web shooter at the wrist, and his feet
on the vent below. A scripted fist driven through a thug's chest at 6 m/s,
through the same input path as your controllers, landed one punch (5.6 m/s, a
knockdown, 29 damage) and took the thug's health from 100 to 68. The thugs in
reach were in a scripted scene and lost health without reacting; a thug in a
fight reacting to a punch still needs a crime or the headset. With the body
off, or whenever it is not on the hero, the hero stays hidden and the overlay
draws gloves, as before: `Launch Spidy VR.cmd -NoBody`; `-NoPunch` turns
punching off; Spidy Launcher has a switch for each. 143 core checks, the GPU
test and 68 Python checks pass; the headset session is pending. Details:
[docs/BODY.md](BODY.md).

**Preceding build (October 6, fifth build): a web is a rope, and what it holds
has weight.** You said webbed objects felt weightless, as if the web were a
stick (you could hold a bin up in the air with it), that they felt weightless
once let go too, and that the web you let go of stayed in the air where the
object had been. In the 10:16 session a held prop followed a point 1.35 m ahead
of your hand, held up, so a turn of the wrist swung it fast, and a throw
multiplied its speed by 2.2: two of four throws left at the 40 m/s cap. Yanks
launched props at 17-45 m/s, mostly upward, and four you let go of in flight
kept rising at 12-13 m/s. Now every web is a rope that only pulls, with at most
2400 N: 80 m/s² on a 30 kg trash can, less on anything heavier, and one web can
drag but not lift anything over 245 kg (two can). A caught prop hangs below
your hand on a short web, swings when you move and settles when you stop;
pointing the hand ahead or turning the wrist neither lifts nor swings it. A
throw keeps the speed your swing gave the prop, times 1.6 for a light prop and
less for a heavy one, up to 25 m/s: in a simulation a hard underhand swing
throws a can at 13 m/s and a flick of the wrist at 0.3 m/s. Yanks fly at 7-15
m/s by how hard you pull; where the arc falls short (up to a rooftop) the web
reels the prop in along itself, and a prop coming in is caught near your hand
instead of flying past it. The controller hums with the web's pull: faintly
for a hanging can, strongly when you swing it hard or it is too heavy to lift.
The web you let go of now stays on the prop as it dissolves, going with it.
You tried it and found it good, except that thrown bins sank into the floor.
They are breakable props of two bodies: the game draws the whole bin from its
top piece, and that is the only part Spidy's freeing lets move; the bin's own
body stays held where it stood, invisible. Measured in the game: the top piece
falls through the bin when freed and lies on the road after a throw, with the
bin drawn 0.8 m below it. Freeing a bin the way the game's own yank breaks it
off is the open fix. 125 core checks, the GPU test and 66 Python checks pass.
Details: [docs/WEB-GRAB.md](WEB-GRAB.md) and
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 6, fourth build): the VR frame rate about doubles.**
You reported that VR felt slow although the CPU and GPU showed about 20% load
and the disk 44%. In the 10:16 session the headset ran at 90 Hz, and the game
made 38 new eye images a second (41-64 frames a second in its own log); a third
of the headset's frames repeated an older image. Measured in the game without a
headset, both eyes at the headset's 3072 x 3264: one game thread, the one that
turns each frame's render commands into GPU commands, was busy 95% of the time
while the main thread waited on it and the GPU was 58-77% busy. Task Manager
averages the CPU over 16 threads, so one saturated thread shows as 6%; and the
game pauses while another window is in front, so what Task Manager shows after
an alt-tab is a paused game. That thread's work grows with what the views draw,
and Spidy's eyes drew everything in their field of view: the engine creates
offscreen views without the occlusion culling its own view has, so buildings
hidden behind nearer ones were drawn anyway: a frame carried 2.3 to 3.8 times
the render commands it carries with culled eyes. Each eye now gets the engine's
own occlusion culling when it is created. On the rooftop of your save, looking
ahead: 65 frames a second before, 125 now (render commands 31 MB a frame
before, 13 MB now); turning a full circle: 48 before, 105 now. Nothing visible changed: at four headings the eye
images with and without the culling differ only where two captures differ
anyway (moving cars and people, birds, leaves), and their average brightness
within 0.4%. The GPU, 80-88% busy now, is close to being the limit at this
resolution. `-NoEyeOcclusion` turns the culling off for comparison. The disk
load is the game streaming textures (in VR its texture budget is full, 3,868 of
3,874 MB); it is not what held the frame rate down. At 120 Hz the earlier
sessions sent the headset only 71 frames a second (Virtual Desktop's end of
frame took 13.4 ms each time), while at 90 Hz it kept up; whether 120 Hz keeps
up now needs the headset. Also fixed: a launch could end with `WinError 24`
when the launcher listed the game's modules while the game was still loading
its own; it now asks again, as Windows documents. 120 core checks, the GPU test
and 58 Python checks pass; the headset session is pending. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 6, second build): walking, jumping and web pulls work
in VR again.** In the 08:39 session you could shoot webs but not pull yourself,
walk or jump, while the menus worked. The player stood on the Times Square
perch for the whole session: 25 webs attached, and every pull's takeoff jump
timed out, so the swing never took over. Spidy gave the game walking and
jumping, the takeoff jump included, as keyboard keys (W/A/S/D and Space)
through the input bridge. Since the seventh build the VR controllers play the
menus as a virtual Xbox controller, and once that controller has pressed a
button, the game plays the player with it and ignores the keyboard. Measured
in the game without a headset, on the same perch: after menus played with the
virtual controller, the bridge's W and Space moved the player 0 m though the
game read them 1,131 times, and real key presses did nothing either, while the
controller's A jumped him 2.9 m and its stick ran him 7.4 m in 1.5 s. With the
menus played on the keyboard instead, the bridge's Space jumped him 3.0 m. In
VR, walking and jumping now go to the game on the virtual controller too: the
left stick turned into the game camera's axes, as for the keys (now analog,
so half a push walks), and A for the jump, released and pressed by the takeoff
as Space was (a 160 ms press jumped him 1.95 m). 115 core checks, the GPU test
and 56 Python checks pass; the headset session is pending.

**Preceding build (October 6, first build): webs catch props and thugs.** You asked to web
objects and NPCs and throw them around, as the game's web yank and web throw
do. A grip press aimed at a throwable prop or a thug, nearer than the wall
behind it, now catches it instead of swinging. The trigger reels it in; the zip
gesture (a sharp pull of the hand) yanks it over; it hangs 0.9 m beyond your
hand and follows your arm; letting go throws it with 2.2 times the speed your
arm gave it, aimed into a thug within 12 degrees. Measured in the game without
a headset, on a throwable prop in the street 43 m from the player perched at
Times Square: reeled in within 4.25 s or yanked in 1.77 s, carried, and thrown
at 17-19 m/s, with no web lost. A prop is freed the way the game's own pick-up-and-throw frees it,
then rebuilt, so the game draws it where its body is. The game steps its
physics by a fixed 1/30 s each frame: at 240 frames a second Havok ran 8 times
real time, and a freed prop fell 32 m in 0.3 s. The web therefore flies grabbed
and thrown props in real time until they come to rest. Thugs are flung with the
game's own flung reaction and steered through their movers. No bot came within
reach in free roam, so that part awaits a crime nearby or the headset.
Pedestrians are not offered: the game moves them kinematically, with no
physics. The lab has crates, barrels, a dumpster and thugs who fall over and
get up. `-NoWebGrab` turns grabbing off. Design and measurements are in
[docs/WEB-GRAB.md](WEB-GRAB.md). 114 core checks, the GPU test and 56
Python checks pass; the headset session is pending.

**Preceding build (October 5, seventh build):** for the 21:12 session, where
"the game always stays flat": the headset showed the game screen for the whole
session and VR never started. The launcher found the player once, before VR
started, and the input bridge accepted camera commits only for that player. The
session began in a Peter Parker mission; the user then left it through the
pause menu, and the load into free roam created a new player, so no camera
commit ever matched again. Measured in the game without a headset: after a
checkpoint restart the follow camera committed 470 times a second and none
matched; handed the new player, 496 of 496 did. The VR worker now finds the
player itself, inside the game, and hands each new one (a loaded save, a
respawn, a character switch) to the input bridge, the swing, movement and
web-line modules and the avatar hiding; its search finds what the outside
search finds, in 7-9 ms on its own thread. The Peter section itself may use a
camera the gate does not accept yet; reports now name the camera of every
scene that stays on the screen. Also new: VR starts with the game, as soon as it
draws frames (8.8-10.3 s after launch), and shows its intro and menus on the
screen. There the VR controllers are an Xbox controller for the game (XInput):
in the game, from the intro to a loaded save with A presses alone, the pause
menu with Start, D-pad and thumbstick moving the selection one item per flick,
the game menu with Back, Xbox prompts throughout. In VR the menu button pauses
and Y opens the game menu. The game pauses while another window is in front
(its intro sat still for three minutes behind the launcher console), so the
launcher brings the game window to the front. 94 core checks and 55 Python
checks pass; the headset session is pending.

**Preceding build (October 5, sixth build):** for the 19:36 and 19:40
sessions, where menus, cutscenes and what looked like spider-sense and damage
overlays turned the headset black. VR keeps a picture only while the game's
follow or combat camera drives the view. The pause menu, the opening mission's
hint cards (they pause the game), cutscenes, finishers and death use other
cameras or none, and the headset then got frames with no image. Now, a quarter
of a second after gameplay stops, the headset shows the game's own frame,
exactly what the monitor shows (menus, hint cards, subtitles included), on a
screen in front of you, and VR resumes with play. Measured in the game without
a headset: that frame was copied at every present in play, in the pause menu
and after it, identical to the window to the byte. The eye views show the HUD
but never the pause menu or subtitles, which is why the screen uses the game's
frame. On brightness: the eye images match the monitor image of the same
camera within about 2%, with opaque alpha, and the session snapshots are the
exact bytes sent to the headset, so the darker look arises after Spidy hands
the image over. 93 core checks, the GPU test, and 53 Python checks pass;
headset confirmation is pending.

**Preceding build (October 5, fifth build):** for the user's 14:39 session.
The webs were confirmed fixed, but the picture still broke up ("black flickering
and quads all across my vision, objects, building and ground dissapearing,
geometry randomly exploding into impossible polygons"), and the game crashed in
what the user saw as the second bout. The cause is the game's per-frame render
memory. The draw lists
and render commands of every view come out of one 128 MB ring each frame, and
two consecutive frames must fit in it. The stock game uses 6-8 MB a frame. With
the two eye views and the game's own view moved to the head, a frame used
50-60 MB at Times Square, at any resolution, so two frames barely fit and a
heavier view did not. When a request does not fit, the game leaves out that
view's work for the frame. The session's report shows three stretches of
20-40 s in which the game dropped 60-87 eye render jobs a second and new eye
images arrived 11-20 times a second. In the first and the last, Spidy also sent
the headset up to 19 frames a second with no image at all, which is the black
flicker; the user stood still through the middle one. Three changes:

- Spidy gives the game a 512 MB ring. The ring cannot change once frames exist,
  so the launcher loads a small module (`spidy_render_memory.dll`) as soon as
  the game process appears, and the module replaces the ring where the game
  creates it, about three seconds in. **This needs `Launch Spidy VR.cmd` to
  start the game.** Attached to a game that is already running, VR works as
  before on the game's own ring, and the launcher prints a warning. Measured in
  the game without a headset, three head-aligned views, swinging and reeling: on
  the game's ring the widest test overflowed in 13 frames and lost 24 eye jobs
  in 10 seconds; on Spidy's ring the same views used at most 137 MB for two
  frames, more than the game's ring holds, and lost none.
- A headset frame for which the game delivered no new image now shows the last
  one again, for up to a second (the runtime reprojects it), instead of nothing.
  Movement and web control need a new image within the last half second.
- That session also ran Windows out of memory it can promise to programs.
  14.0 GB was available when the game started and the game took 17.1 GB in VR;
  Windows enlarged its page file while the game ran, and 0.6 GB was left at the
  crash. The launcher now warns and waits for Enter before starting when less
  than about 19 GB is available; see
  [Memory for a VR session](DEVELOPMENT.md#memory-for-a-vr-session).

The crash was inside the game's own heap allocator, reading a free-list entry
that was no longer valid. Either problem above can lead there, and which one did
is not established. It passes 91 core checks, the GPU test, and 53 Python
checks; headset confirmation is pending. Fully close the game and start it with
`Launch Spidy VR.cmd`.

The preceding build (October 5, fourth build) made fixes for the user's 12:05
session, where web starts were still offset at speed. The remaining cause was in
Spidy's eye cameras. Each frame they were placed after the game had already
copied that frame's views for rendering, so every eye image showed the scene from
the previous frame's head and player position, while the game drew the web from
the current frame's hand. One frame of travel is 0.4-0.7 m at 20-32 m/s, and it
changes with every frame time, so the web started ahead of the hand and flickered
between positions. Spidy now places the eyes just before the game copies its
views. Measured in the game without a headset, swinging and reeling: the web's
first point was 0.45 m from where the eye expected it with the old placement at
20 m/s, and 0.00002 m with the new one at 32 m/s; captured eye images show the
web ending on the marker. The same change gives each eye a real previous-frame
camera. Until now it always equalled the current one, so the previous-camera
values the game hands its shaders (`m_PrevCamWorldToClipMat`, `m_PrevCameraPos`)
described a camera that never moved. Reeling had two faults, both found in that
in-game run: letting go of a web while reeling threw away the reel speed (a
16 m/s jolt in one step), and reeling jolted by 1-3 m/s whenever the frame time
changed. A reeled rope now carries the body's velocity, and the rope is wound for
the time each step really took; step-to-step velocity spikes fell from a median
0.11 m/s to 0.005 m/s in the same swing. A third fault was put forward as the
explanation for "the game began breaking up after a few minutes". It was real,
but it was not that: the break-up was render memory, fixed in the build above.
Spidy's movement command was valid for 50 ms.
Whenever one frame took longer than that in mid-swing, the game ran that physics
step on its own, at the fall speed it keeps for the time spent airborne
(21-42 m/s downward in the day's reports): the world dropped about a metre and
snapped back at the speed limit. Both morning headset sessions contain such
steps, and a slower or hitching game produces more of them. The command now
stays valid for 150 ms; in the final in-game run a 62 ms frame passed without a
missed step. The 12:05 session left no report, because the launcher stopped when
the closing game refused a call. It now always writes the report, and keeps
beside it the game's own log (which the game overwrites at every start) and a
left-eye image every 5 seconds; the 14:39 session's report is what identified
the render memory. It passed 90 core checks, the GPU test, and 48 Python checks.
The user confirmed the webs fixed.

The preceding build (October 5, third build) made fixes for the user's 10:25
session. Webs now shoot when you squeeze a grip; the trigger only reels. A web
that was lost mid-swing re-fired every 0.15 s while trigger and grip stayed held;
now the next web needs a new grip press. Webs were lost that often because the
line from the body to the anchor grazed ledges and sills beside the anchor while
swinging along a facade. Hits within 1.5 m of the anchor (or a tenth of the
rope) no longer count, and any other wall must stay in the way for 0.15 s. One
cause of the offset web starts was the game's swing rope, which adds a hand-held
segment and an eight-point tail that hangs from the hand and trails opposite the
hand's world velocity. At swing speed the tail streamed about 0.45 m behind the
wrist as a second strand; the session measured the rope's first point 0.42-0.45 m
from the wrist. Spidy now gives the rope a single hand point, so it runs straight
from the tracked wrist to the anchor. (The other cause, eye cameras a frame late,
is fixed in the build above.) The shake after a yank came from the swing solver.
The game's mover applies each velocity command in the physics step after the one
Spidy observes, and the solver steered from that one-step-old state. Even and odd
physics steps became two separate trajectories, each receiving gravity and
steering only every other step. A yank kicked one of them, so the body alternated
between two velocities (11 m/s apart in a simulated yank) until landing handed
control back to the game. The solver now starts from where the step in flight
will leave the body. Because of that bug, gravity and air steering acted at about
a third of their settings (18 and 12 m/s²; the session measured 5-6 m/s² of
gravity), so they are now set to 6 and 4 to keep the swing feel you approved.
Yanks now apply their full strength. It passed 83 core checks, the GPU test, and
39 Python checks.

The preceding build (October 5, second build) fixed VR dropping to the flat
screen. Spidy stopped VR after 3 seconds without new eye images, because eye
render jobs the game copied but never rendered filled Spidy's 128-entry job
table after about 10,500 frames. Entries older than 16 frames are now reclaimed;
the 10:25 session ran 874 s and 41,467 frames with 1,026 reclaimed and no stall.
That build also started the game's web lines and the eyes from one hero sample;
the session measured no gap between the two samples, so the web offset had
another cause (the tail above). It opened the game in a 1290 x 540 window during
VR. Double images at speed also come from frame rate: about 44 new stereo pairs
per second arrived, and about 36% of the frames sent to the headset repeated an
earlier image, which shows the city twice at 30 m/s; see
[Graphics settings in VR](DEVELOPMENT.md#graphics-settings-in-vr).

The preceding build (October 5, morning) followed the user's report that culling
of some content, such as the fake apartment interiors behind windows, did not
line up with the headset view and seemed tied to the body. The game does part of each
frame's work for its *active view* only, and that is always the stock
third-person camera; the two VR eyes are extra views it never treats as active.
That work includes occlusion, "drawn in any view" checks, and the shading inputs
shared by every view of a frame. On this PC the stock camera sits behind the hero
and covers 81 x 39 degrees of the 21:9 monitor, far less than the headset shows
vertically. While immersive VR is active, this build moves that camera's render
view to your tracked head and widens its lens to cover both eyes. The gameplay
camera itself, and with it movement direction, is unchanged. The monitor now
shows your head view, stretched to the monitor's shape; flat mode and menus keep
the stock camera. `Launch Spidy VR.cmd -StockMonitorView` brings back the old
monitor view and the old culling, for comparison. It passed 78 core checks, the
GPU test, and 32 Python checks.

The preceding build (October 4, evening) hid the hero with the game's own actor
visibility switch while immersive, moved both eyes with the player to the
simulation frame being rendered, and switched to the game's own web lines started
at your tracked wrists (`-OverlayWebs` forces Spidy's strands). Its 666-second
session report shows all three active: 35,031 frames with the hero hidden, eyes
re-anchored every frame, and 184 game web shots without a failure.

The preceding build restored the native `PrepareSrgbOverlay` pass omitted for
secondary views; the user confirmed this fixed the glowing HUD.

The user confirmed the preceding swing update feels much better. It raises the
native speed cap from 8 to 32 m/s, increases steering and gravity, and starts
reeling immediately on another trigger press. Ground takeoff and 100-metre
open-space anchors were also confirmed working.

R.E.A.L. VR's local installation was removed from the game folder at the user's
request. Its 21 files are backed up under `backups/realvr-removed-20261004`, with
a SHA-256 manifest. The game executable is unchanged.

**The in-game VR milestone is unfinished.** The standalone OpenXR swinging lab
works on the user's Quest. In-game tests now verify native input, camera position,
camera rotation, and asymmetric projection. Two native offscreen eye views can
be created and retained in the tested idle scene. Captured eye images now show
native stereo parallax with a corrected square projection and active scene
lighting. A GPU marker test captured 338 matching pairs with completed fences.
The corrected headset test submitted 509 eye pairs. The user confirmed stereo,
head movement, and visible hands, but reported low resolution and poor performance.
That run used 512-square eyes. A second run at 1536-square with VSync off delivered
about 26.8 new pairs/second over the active session (28.3 before native rendering
expired early). Its native-copy wait took 29.8 ms of a 31.9 ms frame.
The latest build stages complete native pairs and submits the latest available
pair with its original render poses, removing that blocking wait. The user-run
comparison reached 62.4 fresh stereo pairs/second and 70.9 XR submissions/second
at the same resolution, with a 0.09 ms copy call and clean shutdown. The user
reported much better frame rate. Reused pairs are counted separately; this is
still below the headset's requested 120 Hz.

Continuous native queries now identify fixed city surfaces. A six-second game
test attached a web, held it, released it, and continued collision-controlled
flight until landing: 61 controlled physics steps, no faults, all checked hooks
restored. Quest input is connected to this native swing path. Articulated hand
and web overlays, independent input timing, and a point-launch landing window
also build and pass local checks. Physical web controls in the headset, full-speed
collisions, skeletal IK, world occlusion, and lifecycle transitions remain open.
