# Spidy

An in-development VR mod project for **Marvel's Spider-Man Remastered**, targeting
**Quest 3 + Virtual Desktop**. First milestone: 6DoF head/controller tracking,
native stereo, and physically controlled web swinging.

**Latest build (October 6, second build): walking, jumping and web pulls work
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
[docs/WEB-GRAB.md](docs/WEB-GRAB.md). 114 core checks, the GPU test and 56
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
  [Memory for a VR session](#memory-for-a-vr-session).

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
[Graphics settings in VR](#graphics-settings-in-vr).

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

## What's runnable

- `spidy_xr_lab.exe`: an original block city in OpenXR/D3D12 with tracked hands,
  separate eye renders, two webs, gravity, reeling, physical zips, point launches,
  and crates, barrels and thugs to web, yank, carry and throw.
- `spidy_tests.exe`: deterministic physics, input, and camera checks.
- `spidy_graphics_test.exe`: actual D3D12 stereo rendering and pixel readback,
  usable without a headset.
- `spidy_headset_probe.exe`: checks for a connected OpenXR headset without
  creating a session. The game VR tool runs this before any game injection.
- `spidy_sim.exe`: a reproducible pendulum/release simulation written as CSV to stdout.
- `tools/inspect_game.py`: read-only executable identity, signature, and RTTI checks.
- `tools/capture_game_state.py`: bounded live camera/player discovery in a
  free-roam session. It reads the supported game process and saves candidate
  transforms; it does not install anything or write game memory.
- `tools/game_driver.mjs`: launch, window recovery, screenshots, and observed
  mouse/keyboard actions through the Computer Use runtime. See
  [automation and its verified controls](docs/AUTOMATION.md).
- Optional `spidy_observer.dll` and `tools/observe_game.py`: camera-update timing
  and transform diagnostics in the supported game process, with automatic hook
  disable/restore after capture. This is a diagnostic, not the VR game adapter.
- Experimental `spidy_bridge.dll` and `spidy_view.dll`: bounded native input,
  final camera pose, and lens tests. Commands expire without renewal.
- `Launch Spidy VR.cmd`: checks the headset, starts Steam Spider-Man with its
  launcher skipped, and attaches VR when your save has loaded. It stays running
  until game exit or Ctrl+C in its console. Eye resolution follows the active
  runtime recommendation. Click both thumbsticks to switch between VR and a
  flat game screen inside the headset. These new launch/toggle features are
  built and locally tested; headset validation is pending.
- `spidy_render_memory.dll`: replaces the game's 128 MB per-frame render memory
  with a 512 MB ring where the game creates it, and reports how much of it
  frames use. The VR launcher loads it while the game starts.
- `tools/vr_launcher.py`: run as a script, starts the game as the VR launcher
  does (small window, larger render memory) for checks without a headset.
- `Launch Spidy Game VR.cmd`: the same launcher with a console that remains
  open after exit. `Test Spidy Game VR.cmd` preserves the 20-second, 1536-square
  diagnostic. Add `-CaptureImages` to save eye images with readback overhead.
- `tools/run_game_vr.py`: the same integration test with command-line settings.
- `tools/probe_collision.py`: six native city raycasts during a verified collision
  worker callback, with hit telemetry and automatic hook restoration.
- `tools/probe_native_motion.py`: a five-second native jump/velocity test with a
  single 200 ms command, measured movement feedback, and automatic hook restoration.
- `tools/probe_native_rays.py`: repeated world queries, fixed-body classification,
  command expiry, and hook restoration checks.
- `tools/probe_game_swing.py`: a six-second native web attachment/release/landing
  test with automatic hook restoration. No headset is required.
- `tools/probe_game_grab.py`: webs the nearest throwable prop in sight from where
  the player stands, reels (or with `--yank` yanks) it in, carries it and throws
  it, in a freshly started game; `--bots` does the same with a bot, and
  `--fling-test` checks the game's flung reaction on the nearest bot.
- `spidy_stereo_probe.dll`, `spidy_eye_capture.dll`, and `spidy_scene_trace.dll`:
  offscreen-view lifetime, synchronized GPU readback, and render scheduling
  diagnostics. A texture allocation alone is not evidence of a rendered eye.

## Build on Windows

Requires Visual Studio 2022 C++ Build Tools, its CMake/Ninja tools, and a Windows
SDK. The detected installation on this PC already has these components.

```powershell
.\tools\bootstrap.ps1
.\tools\build.ps1
```

Executables are in `build\windows-ninja`. Build the dependency-free core with
`tools\build.ps1 -CoreOnly`; its output is in `build\core-ninja`.

For other C++20 environments, configure CMake with `-DSPIDY_BUILD_XR=OFF`.
No game files or reference-mod assets are needed for the lab.

## Start Spider-Man in VR

1. Connect Quest 3 to this PC in Virtual Desktop.
2. With the game closed, double-click **Launch Spidy VR.cmd**. It starts the
   game itself, which is what lets it enlarge the game's render memory; see
   [Memory for a VR session](#memory-for-a-vr-session).
3. VR starts with the game, about ten seconds after launch: its intro and menus
   appear on a screen in the headset. Select your save and Continue with the
   VR controllers. Immersive VR starts with gameplay and follows every load,
   respawn and character switch; there is no second attach command.

The game pauses while another window is in front of it on the desktop, so the
launcher brings the game window to the front. If the headset shows a still
game screen, click the game window once (in Virtual Desktop's desktop view).

Squeeze a **grip** to shoot that hand's web where the controller points, and
keep squeezing to swing; release it to let go. Pull the **trigger** while a web
is attached to reel in; if it was already held when the web attached, release it
first. Pull a hand sharply away from its anchor to zip. A web that breaks
mid-swing stays released until you squeeze that grip again.

Webs are drawn by the game's own web-line system. If they look wrong in the
eyes, start with `Launch Spidy VR.cmd -OverlayWebs` for Spidy's overlay strands.

Aim the web at a throwable prop (one the game's combat lets you web and
throw) or a thug, and it catches it instead. Pull the trigger to reel it in, or
pull the hand back sharply to yank it to you; it then hangs just beyond your
hand and follows your arm. Let go of the grip to throw it: the harder you
swing, the faster it flies, and a throw close to a thug is aimed to hit them.
`Launch Spidy VR.cmd -NoWebGrab` keeps webs for swinging only. See
[docs/WEB-GRAB.md](docs/WEB-GRAB.md).

The intro, menus, loading, the map, hint cards, cutscenes, finishers and death
appear on a screen in front of you, as the game shows them on the monitor; VR
resumes when play does. On that screen the VR controllers are the game's Xbox
controller: the left thumbstick moves through menus, A selects, B goes back,
X and Y are the game's X and Y, the grips are the bumpers (menu tabs), the
triggers are the triggers, the right thumbstick and the stick clicks are the
same on both, and the menu button is Start. The game shows Xbox prompts, which
sit where the Touch controller buttons are. In immersive VR the menu button
pauses the game and Y opens the game menu (map, suits, skills); every other
control stays with VR, which walks and jumps through the same virtual
controller. A held from the screen (Resume, Continue) does not jump. A real
gamepad keeps working. The keyboard does not: once the virtual controller has
pressed a button, the game ignores the keyboard for the player. The screen has the size of the
game's desktop window, small when the launcher starts the game;
`-FullDesktopView` makes it sharper.

Click **both thumbsticks** to switch between immersive VR and a flat screen
inside the headset. Release both before clicking again. Flat mode uses the
stock game camera and the game's own controls on a real gamepad (not the
keyboard, as above; the Touch controllers pass only the menu button and Y);
VR movement and web input are released. In immersive VR the monitor shows your head view, horizontally
stretched; in flat mode it shows the normal game view. Add `-StockMonitorView`
to keep the normal game view on the monitor in immersive VR too, at the cost of
culling and shading that follow the stock camera instead of your head.

When the launcher starts the game, the game opens in a small window with your
desktop's shape, 540 pixels tall, centred. The engine still runs culling, shadow
setup, and auto-exposure for that view; a small window keeps that work and skips
most of its rendering. The launcher saves the game's window settings in
`reports\desktop-view-before-vr.json` and writes them back after the game
closes. If the launcher stops while the game is still running, they are written
back the next time it starts the game, or run `python tools\vr_display.py
--restore` after closing the game. A game that is already running keeps its
size. Add `-FullDesktopView` to start the game with your own settings.

The session has no 20-second cutoff. Close the game normally, or press Ctrl+C
in the launcher console to stop VR and restore the hooks. Keep that console
running during play. A second launcher is rejected instead of attaching twice.

Every session leaves `reports\game-vr-<date>-<time>.json`, however it ends: the
game closing, Ctrl+C, or the console window being closed. Beside it are
`...-game.log`, a copy of the game's own log with its once-a-minute memory and
frame rate lines (the game overwrites that log at its next start), and
`...-eyes\`, the left eye as it was sent to the headset. One reduced image is
saved every 5 seconds, or every 1.5 seconds while a web is held above 15 m/s,
with a full-resolution crop around the hand holding it; the newest 72 are kept.
If something looked wrong in the headset, these show whether the game's image
was wrong too.

Resolution defaults to the runtime's recommended width and height for each eye,
which can change with your Virtual Desktop quality preset. These are the
[OpenXR recommended render dimensions](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrViewConfigurationView.html),
not a fixed 1536-square image. The launcher prints the selected dimensions.
For an explicit square override, use `tools/launch-game-vr.ps1 -Size 2048`.

The new launcher, automatic dimensions, and thumbstick toggle are built. Their
combined headset check was deferred at the user's request. Returning to VR
after stopping the launcher is not yet seamless. Restart the game before
reattaching after a stopped session or a rebuilt DLL.

## Memory for a VR session

Two memory limits matter, and neither of them is RAM running out.

**Render memory.** The game keeps two frames of draw lists and render commands
in a 128 MB ring. Three scene views need more, so the launcher gives the game a
512 MB ring while the game starts (see the latest build above). A few seconds
into VR the launcher prints either `Render memory: 512 MB for two frames (the
game's own: 128 MB).` or a warning that the game is on its own ring. After the
warning, close the game and start it with `Launch Spidy VR.cmd`. When the
session ends the launcher prints the most two frames used and how many frames
did not fit; the report has the same under `render_memory`.

**Windows commit.** Windows promises each program the memory it asks for, out
of RAM plus page file, whether or not the program then uses it. The game's GPU
allocations count as well. In VR at 3072 x 3264 per eye the game takes about
17 GB of promises. On October 5 this PC had 61.6 GB of RAM with half of it
free, but other programs held about 50 GB of promises and the page file was
4 GB, which left 14-15 GB. The game went past that. Windows enlarges a system-managed page file when this happens, and while
it does, requests for memory can stall or fail. When less than about 19 GB is
left, the launcher prints a warning and waits for Enter before it starts the
game, and the report records `free_commit_mb` at the start and at its lowest.
Either of these makes room:

- Close large programs before a session: browsers, chat apps, game launchers.
- Give Windows a larger page file. System Properties > Advanced > Performance
  Settings > Advanced > Virtual memory > Change: clear "Automatically manage",
  select the system drive, choose Custom size with initial 32768 MB and maximum
  49152 MB, press Set, and restart Windows. While RAM is free the larger file is
  not read or written; it only raises what Windows can promise.

## Graphics settings in VR

Spidy does not change in-game graphics options at runtime. One persistent change
was made on October 4 for a performance comparison: the game's `VSync` registry
value went from 1 to 0 (backup in `reports/vsync-before-performance-test.json`).
When the VR launcher starts the game, it changes only the window settings
(windowed, size, position) for that session and writes them back after the game
closes; see [Start Spider-Man in VR](#start-spider-man-in-vr).

The two eyes are extra native scene views. They inherit the game's own quality
settings, including textures, shadows, level of detail, crowds, ray-traced
reflections, ambient occlusion, anti-aliasing, and motion blur strength. For
each eye, Spidy sets only:

- the runtime-recommended resolution (3072 x 3264 in the latest session), with
  private scene buffers at that size;
- the tracked eye pose and asymmetric headset lens, with zero lens jitter;
- the active post-processing profile and the main camera's display path;
- the main camera's current exposure, so both eyes share auto-exposure;
- the HUD's sRGB overlay setup pass, which the game skips for secondary views;
- avatar visibility while immersive VR is active (this also hides the avatar
  and its shadow on the monitor).

While immersive, Spidy also moves the game's own camera view, the one on the
monitor, to your tracked head with a lens covering both eyes. The game does some
per-frame work only for that view: occlusion, "drawn in any view" checks, and the
shading inputs shared by every view of a frame, which carry the key-light shadow
setup and a camera position available to every shader. Moving the view lines
that work up with the headset. That view now covers a wider field of view than
the stock camera, which may cost some frame rate; compare with `-StockMonitorView`.

The desktop view renders at the game window's size. Before the small VR window,
it rendered the full 3440 x 1440 desktop, about a fifth of the frame's pixels;
at 1290 x 540 it is about 3%. The October 5 morning session averaged 44 new
stereo pairs per second at
3072 x 3264 per eye with the headset at 120 Hz; about 40% of the frames sent to
the headset repeated an earlier pair, which doubles fast-moving scenery. The
largest cost is eye resolution: lower Virtual Desktop's quality preset, or
use `tools\launch-game-vr.ps1 -Size 2048` for a square override. Ray-traced
reflections are computed for each eye. On this PC, motion blur, film grain, and
sharpening are at their lowest stored value (1), and depth of field, vignette,
chromatic aberration, and lens flares are off. The game also has a traversal
motion blur bonus while swinging; whether that low setting suppresses it in the
eyes is not verified. When frames arrive slower than the refresh rate, Virtual
Desktop repeats or synthesizes frames (Synchronous Spacewarp), which can double
fast-moving edges. A refresh rate the frame rate divides evenly, such as 72 or
90 Hz, gives a steadier cadence than 120 Hz. With SSW on, Virtual Desktop renders
the game at half the refresh rate and synthesizes the frames in between; at 90 Hz
that is 45 frames per second, close to the rate measured above, so most frames it
receives are new rather than repeats.

## Run the lab

Connect Quest 3 to Virtual Desktop on this PC, then double-click
`Launch Spidy Lab.cmd`, or run:

```powershell
.\tools\launch-lab.ps1
```

Use `-Probe` to check the runtime without starting a headset session. The default
launcher uses VDXR for its own process. `-SystemRuntime` uses the registered
OpenXR runtime instead. Neither option changes the system registration.

| Quest Touch control | Action |
|---|---|
| Aim hand, squeeze grip | Shoot that hand's web |
| Keep grip held | Keep swinging |
| Release grip | Release web and retain momentum |
| Hold trigger while attached | Reel the web in (release it first if it was held when the web attached) |
| Pull hand sharply away from the anchor | Zip once per attachment |
| Aim at a crate, barrel or thug (a yellow marker shows it), squeeze grip | Web it |
| Trigger on a webbed prop or thug | Reel it in to your hand |
| Pull hand sharply back from it | Yank it to your hand |
| Release grip while it is at your hand | Throw it |
| Left stick | Move / steer |
| Right stick left/right | 30-degree snap turn |
| Right A | Jump; shortly after a zip landing, point launch |
| Left Y | Reset to the lab rooftop |
| Escape on PC | Exit |

Clear shots that reach no surface create a fixed anchor at maximum reach
(100 metres by default). Hold grip and pull the trigger to reel toward either a
real surface or an open-space anchor.

The lab starts on a rooftop. Controller markers and colored aim markers indicate
each hand. Tracking/focus loss releases webs and requires releasing grip before
reattaching. The lab has no desktop mirror or in-headset menu yet. Its collision
world uses static boxes and a swept sphere; room-scale head/body collision is
still pending. The user has confirmed the lab works on Quest 3 through Virtual
Desktop. Detailed haptic, recentering, comfort, and performance checks remain open.

## Verify without a headset

```powershell
.\build\windows-ninja\spidy_tests.exe
.\build\windows-ninja\spidy_graphics_test.exe reports\graphics
.\tools\launch-lab.ps1 -Probe
python tools\inspect_game.py "C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered\Spider-Man.exe" --output reports\game-inspection.json
python tools\discover_render_types.py "C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered\Spider-Man.exe" --output reports\render-types.json
```

The graphics check saves left/right BMP images and checks near-object parallax,
typed and typeless image copies, and tracked hand overlays on the same device/queue.
It does not verify an OpenXR headset session or game rendering. Spidy loads its
modules from this workspace; there is no game-folder installer in this build.

Two probes check eye rendering in the running game itself, with no headset.
Each needs a freshly started game, because a Spidy module starts only once per
game process. `tools\vr_launcher.py` starts the game the way the VR launcher
does, in the small window and with Spidy's render memory (`--ring 0` keeps the
game's own 128 MB ring, for comparison).

```powershell
python tools\vr_launcher.py                    # start the game, then load a save
python tools\probe_eye_frames.py               # at the main menu
python tools\probe_web_frames.py --views 13    # in free roam, with open space ahead
```

`probe_eye_frames.py` drives two eye views with a moving pose and counts, for
every eye render job, whether its pose was placed in that frame and whether its
previous camera differs from its current one. `probe_web_frames.py` swings and
reels at the VR launcher's settings with a game web held in front of a sideways
eye camera, and measures how far the web's first point is from where that
camera expects it. It runs the first half with the eyes placed a frame late, as
before October 5, for comparison, and saves left-eye images with a marker at
the expected start. Its report also lists every physics step of the swing.
`--views 13` moves the game's own view to the eyes as a VR session does, so the
frame carries three scene views; the report's `render_memory` shows what they
used, and the check fails if any frame's render memory did not fit.

For the next integration session, load free roam and run
`python tools\capture_game_state.py`. It checks the running executable's SHA-256,
scans up to 8 GiB of cacheable private writable memory within a 45-second budget, and samples
plausible player/camera objects for three seconds. `reports/live-camera.json`
records partial scans explicitly; absence of a candidate does not mean the
object does not exist. Reuse current-process candidates with `--seed-capture` to
avoid another heap scan. These observations do not verify a render or physics ABI.

The optional callback diagnostic is built with `tools/bootstrap.ps1 -Observer`
and `tools/build.ps1 -Observer`. Its launch/capture/stop workflow is documented
in [AUTOMATION.md](docs/AUTOMATION.md). A 30-second free-roam run captured 3,362
valid camera/player observations; its hook was disabled and original entry
bytes restored afterward. Native eye rendering is now demonstrated in a bounded
diagnostic; the playable VR game adapter is still in development.

## Development map

See [validation results](docs/VALIDATION.md) for what passed locally and what
still requires a running game or headset.

| Path | Role |
|---|---|
| `include/spidy`, `src/swing.cpp` | Engine-independent physics and control logic |
| `src/xr_session.cpp` | OpenXR timing, tracking, actions, stereo submission |
| `src/d3d12_renderer.cpp` | Lab renderer, tracked hand overlay, diagnostic readback |
| `src/native_webs.cpp` | The game's own web lines, started at the tracked wrists |
| `src/web_grab.cpp`, `src/lab_props.cpp` | Web grab: catching, yanking, carrying and throwing props and characters; the lab's props |
| `src/game_grab.cpp`, `src/native_bodies.cpp`, `src/game_targets.cpp` | Web grab in the game: candidates, freed Havok props, flung bots |
| `src/native_render_memory.cpp`, `tools/vr_launcher.py` | A larger per-frame render memory ring, installed while the game starts |
| `src/web_visual.cpp` | Fallback game-style web strands for the headset overlay and lab |
| `src/game_xr.cpp`, `src/game_tracking.cpp` | Native eye presentation and Quest controls |
| `src/native_rays.cpp`, `src/native_movement.cpp`, `src/game_swing.cpp` | Native world queries and collision-controlled swing requests |
| `src/lab_world.cpp`, `apps/xr_lab.cpp` | Synthetic collision world and VR lab |
| `tools/inspect_game.py`, `tools/discover_render_types.py` | Offline game research |
| `src/game_observer.cpp`, `tools/observe_game.py` | Opt-in native camera callback diagnostic |
| `tools/game_driver.mjs` | Observed game launch, screenshots, and input workflow |
| `tools/vr_display.py` | Small desktop window for VR launches; restores the game's window settings |
| `docs/MILESTONE-1.md` | In-game acceptance criteria and remaining integration work |
| `docs/REFERENCE.md` | Reference mechanics, executable evidence, source attribution |

The bounded adapter connects `spidy_openxr` to the game's D3D12 device and direct
queue, then copies matching native eyes after their submitted GPU markers.
See [milestone details](docs/MILESTONE-1.md) for its remaining validation.
