# Autonomous game checks

The user authorized launching the game, taking screenshots, and sending controls
on 2026-10-03. Use the installed Computer Use skill and its `@oai/sky` runtime.
`tools/game_driver.mjs` wraps the documented API with exact game-window selection,
one observed action at a time, and optional screenshot/event evidence in
`reports/automation/<session>/`.

Initialize `sky` in `node_repl` according to the Computer Use skill. Then import:

```js
globalThis.GameDriver = (await import('file:///C:/Users/lianm/OneDrive/Documents/GitHub/Spidy/tools/game_driver.mjs')).GameDriver;
globalThis.game = new GameDriver(sky);
await game.select();
nodeRepl.write(await game.observe({label: 'initial'}));
```

Inspect the displayed state, then perform one grounded action in the next cell:

```js
nodeRepl.write(await game.act({kind: 'key', key: 'Return'}, {label: 'after-enter'}));
```

Supported actions are key presses, screenshot-relative clicks, and drags. The
driver refreshes after each action and invalidates observations after errors.
It does not guess game menus, synthesize hidden inputs, or fabricate window IDs.
Use `game.launch()` only when the game is not already starting/running. If a
launcher or modal is present, inspect and handle that window before gameplay.

Use normal shell tools for build commands and the read-only memory capture,
outside UI automation. Do not type commands into a terminal UI. After loading a
scene, run `tools/capture_game_state.py`, followed by
`tools/analyze_camera_capture.py <capture.json> --output <summary.json>`.
The summary checks whether player and camera candidates share an actor record
and reports motion/stability measurements. It does not establish a render hook.

## What has worked in the actual game

- Launching the installed game, clicking Play, selecting the existing profile,
  and continuing into free roam.
- Capturing and saving screenshots, and rotating the camera with a mouse drag.
- Closing with `Alt_L+F4`, recovering the replacement launcher/game windows,
  and repeating the load sequence.
- Reading a matching HeroLocal / HeroCameraManager pair and the FollowCamera
  lens. After rotating the visible camera, its transform changed while the
  player's position remained fixed.

The issued `space` and `Escape` key presses did not produce a jump or pause in
free roam. The 30-second jump capture measured zero player displacement. A
successful input API call is therefore **not** evidence that gameplay accepted
the key. Sustained key/gamepad control is not exposed by this driver's current
Computer Use API. Validate any new control against screenshots and telemetry.

The save directory was copied to `reports/backups/` before loading the profile.
Screenshots and a timestamped action/error log are in `reports/automation/`.

## Camera callback diagnostic

The optional diagnostic DLL instruments the supported camera-update entry. It
calls the original function exactly once, then publishes camera/player state.
It does not implement head tracking, native stereo, motion controls, or swinging
inside the game.

```powershell
.\tools\bootstrap.ps1 -Observer
.\tools\build.ps1 -Observer
python tests\observer_protocol_tests.py
python tools\capture_game_state.py --output reports\live-current.json
python tools\observe_game.py --seed-capture reports\live-current.json --seconds 10 --output reports\native-camera.json
python tools\analyze_observer_capture.py reports\native-camera.json --output reports\native-camera-summary.json
```

The discovery must be from the **current game process**. A bounded scan can be
partial. The observer requires exactly one valid player/camera pair with a shared
actor record, checks the executable SHA-256, and refuses a changed function entry.
The function's `this` object is tracked separately from HeroCameraManager.
The successful capture identified it as `Hero::AimContextSwing`, at manager +
`0x6c0`. Do not assume this entry is the final rendering-camera update.

The DLL is loaded from an immutable copy under `reports/observer-modules/`. No
file is installed in the game directory. The command disables its hook at the
end and checks that the original function bytes were restored. On an interrupted
sampler, use `python tools\observe_game.py --stop`. Closing the game also removes
the diagnostic. The disabled DLL stays mapped until exit to keep any returning
calls valid; restart the game before trying a different DLL build.

Process inspection and DLL loading may require running outside the sandbox,
because the game runs under the desktop user's account. The external scanner
opens only read/query rights. The opt-in observer loader additionally needs
memory allocation/write and remote-thread rights to load/start its own DLL.

## Game VR launcher and timed tests

Connect Quest 3 in Virtual Desktop and run `Launch Spidy VR.cmd`. The launcher
starts Steam Spider-Man with `-nolauncher`, waits for the selected save to load,
then attaches VR. It uses the bundled Python runtime and saves timestamped
`reports/game-vr-*.json` reports. Defaults are an untimed session, runtime-recommended
eye dimensions, and a 32 m/s swing cap. Both thumbstick clicks toggle immersive
VR and the native game camera on a flat quad in the headset.

`Test Spidy Game VR.cmd` selects 20 seconds at 1536-square resolution. Direct
options: `tools/launch-game-vr.ps1 -Seconds 20 -Size 1536 -AttachOnly`, or omit
`-AttachOnly` to launch/wait automatically. Size zero uses recommended dimensions
(times `-RenderScale`, 100% by default);
seconds zero uses a heartbeat-controlled session until game exit or Ctrl+C.
For validation beyond the old timeout, `run_game_vr.py --auto-launch --stop-after 45`
exercises the untimed worker with a bounded external stop.

The launcher checks headset availability before starting the game or injecting.
It starts the game, waits until the game draws frames (30 frames counted by the
render memory module, then the direct queue the game submits on itself,
`probe_stereo_gpu.game_queue`), brings the game window to
the front (the game pauses while another window is), starts the input bridge
without a player and starts the VR worker, all within about ten seconds of the
launch. The worker finds the player in-process (`src/game_player.cpp`) and hands
every new one, or none, to the input bridge, swing, movement and web-line
modules; before the seventh build of October 5 the launcher found one, once, and
VR stayed on the game screen after any reload. The player selects the save with
the VR controllers. When Codex operates the game, use the observed UI driver
or `tools/probe_menu_pad.py` for those steps and close the game promptly after
the prepared test. Keep it closed during
builds and research. A sandboxed headset failure is inconclusive; the preflight
must run with access to the desktop runtime before declaring the headset absent.

The worker waits for an accepted native eye pair before enabling movement, stops
active controls after 500 ms without a new image, shows the last image for up
to a second while none arrives, and aborts after three seconds without one.
Without gameplay (intro, menus, loading, pause menu, hint cards, cutscenes,
finisher or death cameras) it shows the game's own presented frame on a virtual
screen instead, and the VR controllers are the game's Xbox controller there
(`src/game_pad.cpp`, XInput controller 0). In VR the menu button (Start) and
Y (Back) reach the game as buttons, and native walking and jumping (the takeoff
jump included) as its left stick and A, besides the input bridge's
W/A/S/D/Space: once that controller has pressed a button, the game ignores the
keyboard for the player. Samples carry `presentation` (`immersive`, `flat`,
`game_screen`, or `none` for a frame without an image), `screen_submitted`
(game-screen frames), `gate` (why gameplay was unavailable: `no_player`,
`bridge_stopped`, `no_camera_commit`, `other_camera`, `tracking`),
`camera_mover` (the class of the camera that last committed; a scene played on
the screen names the camera the gate is missing), `camera_commits` and
`player_commits` (the bridge's commits, and those the gate accepts), `players` and
`player_record` (each player the worker followed), and `pad_buttons`,
`pad_installed` and `pad_reads` (the virtual controller and the game's reads of
it). The native mover controls collision; requests expire if the swing
or XR worker stops renewing them. Owned movement synchronizes the native airborne
velocity and clears the mover's duplicate fall accumulator. This correction
still needs an in-game handoff test. Reports now include native movement samples
with gravity-correction and airborne-event counters.

A grip press shoots a hand's web and holding the grip keeps it; release the grip
to let go. The trigger reels while a web is attached (after a release, if it was
held when the web attached). That press takes up existing slack immediately;
reeling runs at 16 m/s and starts at that speed in one step. Physical yanks use
tracked sample timestamps. Left stick steers, right stick snap-turns, and A jumps. The landing
window for point launch is core-tested; the combined in-game headset controls
remain unverified. Stereo/head movement and visible hands have user confirmation.
Left Y resets the lab position in the lab; in the game it is Back (the game
menu), and the left menu button is Start (pause).

The test report rejects intermediate XR/swing faults, incomplete GPU work, failed
stop calls, and any checked game entry left patched. Its success flag validates
these recorded checks; it does not establish visual quality, comfort, or completion
of the milestone. `-CaptureImages` enables native eye PNGs captured before the
hand overlay; image readback is otherwise disabled to avoid its per-frame cost.

A session report is written however the session ends. A closing game refuses
remote calls while its process handle still reports it running; the 12:05
session on October 5 ended that way before this was handled, and left no report.
Beside the report the launcher keeps `<report>-game.log`, a copy of the game's
own log, and parses its once-a-minute `[Render] Working set: ...` lines into
`game_memory` (commit, video and texture usage against their budgets, demoted
textures, the game's frame rate). The game overwrites that log at every start,
so read it before starting the game again when a session has no copy.
`<report>-eyes/` holds the left eye as presented, copied without blocking every
5 seconds (every 1.5 seconds while a game web is held above 15 m/s), with the
pixel positions of the wrist and the web's first point in `eye_snapshots`.

The launcher loads `spidy_render_memory.dll` into the game as soon as its
process exists, about three seconds before the game creates its per-frame
render memory; see the rendering contract in [MILESTONE-1.md](MILESTONE-1.md).
The report's `render_memory` names the ring in use (`spidy_ring`, or
`game_ring` when the game was already running), the most one frame and two
consecutive frames used, and `overflow_frames`, the frames with a request that
did not fit. Samples carry `render_frame_mb` and `render_overflow_frames`. A
stretch in which `eye_jobs_reclaimed` climbs, new pairs fall, and the game's
frame rate rises is the game dropping eye views for lack of that memory.

The report also records `free_commit_mb`: what Windows could still promise to
programs at the start, with each sample, and at its lowest. The launcher warns
before starting when that is less than a session takes. The game's own log
gives the same figure as `Avail page file` at startup and in its crash record.
A crash record's address has lost its upper 32 bits. Restore them from the
module base (`0x1199cfbb` with the module at `7ff70fd40000` is `7ff71199cfbb`),
then subtract the base for the offset in `Spider-Man.exe`. Minidumps, when the
game writes one, are in the game's folder under Documents.

## Checks in the running game without a headset

`tools/probe_eye_frames.py` (main menu) and `tools/probe_web_frames.py` (free
roam) exercise the eye views, game webs, and the native swing in the real game
with no headset connected; see their `--help`. `tools/probe_game_screen.py`
steps through game states itself (Esc for the pause menu) and, for each, saves
the headset's game-screen source (the copied back buffer) and a left eye copying
the stock camera beside the game window's own image, with brightness and alpha
statistics. `tools/probe_menu_pad.py` does what a VR session does from the
game's start: `start` launches the game as the launcher does and times the
renderer, `pad a --until-player` plays the title and main menu with the virtual
Xbox controller until a player exists, `pad start`, `pad down` or `pad --left 0
-1` play any menu, `shot NAME` saves the window, and `player --follow` compares
the in-process player search with the outside one, hands the player to the
input bridge and samples the gameplay gate. What they need:

- A fresh game process for every run. Each Spidy module (rays, movement, swing,
  eye views, GPU capture) starts once per process, and a second start is
  rejected with code 1000.
- Start that process with `python tools\vr_launcher.py`. It uses the VR
  launcher's small window and loads the render memory module in time, and it
  returns once the game has created its ring (`--ring 0` keeps the game's own
  128 MB, to compare). `probe_web_frames.py --views 13` then renders three
  views as a VR session does and fails if any frame's render memory did not
  fit.
- Steam drops a launch requested while the previous game process is still
  leaving, without an error. Wait until the process is gone for a few seconds.
  After a crash the process can linger for minutes; Steam's
  `logs\console_log.txt` shows "Game process removed" when it is done.
- The game with three views takes 13-15 GB of what Windows can promise. On
  October 5 that left 1-3 GB on the test PC during runs, and once 0.7 GB. Check
  the free commit before a run and close the game as soon as the run ends.
- The game's main menu is a live 3D scene. Eye views and GPU capture work
  there, so rendering checks do not need a loaded save.
- The game pauses while another window is in front, and Windows keeps a game
  that Steam starts behind the window that started it: the intro then sits
  still, drawn black, and the game reads no controller. `vr_launcher.bring_to_front`
  fixes that (`probe_menu_pad.py start` calls it).
- Menus accept the virtual Xbox controller (`probe_menu_pad.py pad`) or real
  keyboard input: `SendInput` scancodes while the game window is in the
  foreground. Posted `WM_KEYDOWN` messages are ignored. From a fresh start,
  `probe_menu_pad.py pad a --until-player` reaches a loaded save in about 40 s.
  With the keyboard, wait for the profile menu to be drawn (about half a minute,
  after the intro logos; check a screenshot), press Enter, wait about 4 s for
  the main menu, and press Enter again on Continue. The player is ready
  10-30 s later.
- The device that last pressed a button plays the player. After the virtual
  controller has pressed one (`probe_menu_pad.py pad`), the game ignores the
  keyboard for the player: the input bridge's keys (it still reads them) and
  `SendInput` alike, and the next key press does not switch back. Probes that
  jump or walk with the bridge (`probe_web_frames.py`, `probe_game_swing.py`,
  `probe_native_motion.py`, `probe_movement.py`, `capture_movement.py`,
  `bridge_game.py --jump`) need a save loaded with
  the keyboard and no virtual controller press since. The first press on the
  controller after keyboard play is swallowed by the switch.
- Screenshots of the game window need `PrintWindow(hwnd, dc, 3)`; a `BitBlt`
  from the screen returns black.
- Controller samples sent by a probe need distinct timestamps. Python's
  `time.monotonic_ns()` repeats within a 15.6 ms tick; a repeated timestamp is
  an invalid input interval and cancels the swing, so probes use
  `time.perf_counter_ns()`.
- Copy the save folder to `reports/backups/` before loading a save, close the
  game promptly afterwards, and run `python tools\vr_display.py --restore` if
  the launcher's small-window settings are still pending.

### The player's body and fists

`python tools/probe_game_body.py` in a freshly started game in free roam (the
VR launcher's way: `probe_menu_pad.py start`, then `pad a --until-player`):

- `--phases rig,ik,eyes,fist` (default): the hero's rig with every joint's
  name, parent and rest position; the body toward a scripted headset and
  controllers, with the wrists' and head's distance from their targets and a
  screenshot; the left eye's image looking down at the body, and with the body
  off; a scripted fist through the nearest bot's chest through the swing
  module's input, with the punch, the request and the bot's health.
- `walk`: the hero walks about under the body with the virtual pad's stick (a
  perched hero ignores a light stick), for the turn between pose job and
  render. `punch`: one blow straight to the DamageSystem; `--punch-hero` also
  staggers the hero.
- The GPU capture starts once per game process: the eyes phase needs a fresh
  game each time (a second start returns 1000).
- Reports: `reports/body-probe*.json`, images in `reports/body-probe/`.

### Aim markers' previews

`python tools/probe_aim.py` in a freshly started game in free roam: a scripted
hand 1.2 m above the player's feet, grip open, aims at the sky, eight compass
points level and 30 degrees down, straight down and the nearest throwable prop
within 55 m. For each aim it renews the previews' lease (`SpidyAimSample`) and
reads `SpidyAimData`: anchor, air, blocked, prop or character, the point and its
distance from the hand. It fails if an aim got no preview, the swing faulted,
a module did not stop, or a hook entry was not restored. Report:
`reports/aim-probe.json`. The markers themselves are drawn only by the VR
worker; the GPU test draws every kind with the same renderer.

### The web shooter

`python tools/probe_shooter.py` in a freshly started game in free roam: the
swing and the web shooter started as a session starts them, a scripted left
hand 1.2 m above the player's feet pulls its trigger (open 0.15 s, pulled
0.25 s, open again) aimed level along the game camera, 30 degrees down, at the
sky and 60 degrees to either side. A thread watches `SpidyShooterData` and
follows the `ShotWebShooter` each shot spawned (its record's transform every
5 ms) the moment it is reported: a shot at the ground nearby lives only 50 ms.
Then a trigger held a second (one shot), three pulls 0.2 s apart (three), the
nearest bot within 40 m through `SpidyShooterTest` (aimed at his actor: the
game's target taken, `resolved`), and the shooter stopped and started again.
It fails if a pull shot other than once, a shot was not followed, the swing
faulted, a module did not stop, or a hook entry (the rays' and the shooter's
camera-update and muzzle hooks) was not restored. Report:
`reports/shooter-probe.json`, screenshots in `reports/grab-probe/`.

RB on the virtual pad fires the gadget the game's gadget wheel has selected,
which need not be the web shooter (the user's save had the Impact Web), and a
perched hero fires it too; Spidy's shots always use the web shooter.

### The SPIDY VR tab in the game's Settings

`python tools/probe_menu.py` after `tools/probe_menu_pad.py start` and
`pad a --until-player` (a loaded save, in free roam or anywhere the pause menu
has Settings fifth): it starts the tab's hooks with test values
(`SpidyMenuStart`: 33 m/s, the web shooter off), pauses with the virtual Xbox
controller, moving with its left stick as the Touch controllers do, opens
Settings, goes Up to SPIDY VR and opens it, then switches the aim markers off,
steps the swing speed to 40 m/s and the weight to 80%, switches the body off
and resets it with X, steps snap turn to 45 degrees, smooth turn to 60 degrees
a second, and resets the tab with Y and A (13 changes in all). After each step
it reads `SpidyMenuSample` (what the tab holds, the tabs built, the changes)
and captures the window. It fails if a step left other values than expected or
a hooked function does not start with the game's own bytes after
`SpidyMenuStop`. Report: `reports/menu-probe.json`, captures in
`reports/menu-probe/`. Settings must not have been opened in that game yet:
the game reopens them on the tab last used, and the probe counts from GAME.

### Midair handoffs

`python tools/probe_air_handoff.py` after `tools/probe_menu_pad.py start` and
`pad a --until-player` (a loaded save in free roam, perched or standing in the
open; it jumps with the virtual controller's A, so the save must not have been
loaded with the keyboard's Enter). It starts the movement and ray modules as a
session does (32 m/s, gravity 6, no grabbing), finds the most open of eight
directions, jumps, shoots a web 60 degrees up that way, reels, then in the
air: 0.3 s of unfocused input with the grip and trigger held (`hold`), the web
released, the game process suspended 0.25 s (`hitch`), 2 s of unfocused input
(`coast`), and unfocused input around a 3 s suspension (`pause`). It reads
`SpidyMotionData` every few milliseconds and, per test, counts airborne steps
the game ran without Spidy's command and the largest change of vertical speed
between steps. It fails if a test did not start airborne, a step ran without
the command, the vertical speed jumped by 5 m/s or more in a step, the web did
not survive `hold`, a module did not stop, or a hook entry was not restored.
`--modules DIR` takes `spidy_movement_bridge.dll` and `spidy_ray_bridge.dll`
from another folder (a play folder's build) for a before-and-after. Report:
`reports/air-handoff.json` (`--output`).

### Weight

`python tools/probe_weight.py` after `tools/probe_menu_pad.py start` and
`pad a --until-player` (the same loaded save as the midair probe). It starts
the swing at the default weight (60%, 5.886 m/s²), jumps, webs the most open
direction 60 degrees up, reels, lets go, and in that flight changes the weight
through `SpidySwingSettings` as the SPIDY VR tab does: 0.6 s at 60%, 0.6 s at
150%, 0.4 s at 300%. For each it fits the vertical acceleration of the
airborne steps on game time (their `dt`, the first 0.1 s left out: the mover
applies a command a step late). It fails if a weight's fall is more than 5%
off 9.81 m/s² × weight / 100, a step in the air ran without Spidy's command,
the player landed within a window, a gravity over 30 m/s² was not refused
(2001), a module did not stop, or a hook entry was not restored. Report:
`reports/weight-probe.json` (`--output`).

### Walls the game sticks the player to

`python tools/probe_wall_crawl.py` after `tools/probe_menu_pad.py start` and
`pad a --until-player` (the same loaded save; a building within 50 m). It
casts 48 world rays from the hand (16 headings, level and 12 degrees up and
down) and takes the nearest wall 6-50 m away, preferring one level or above,
jumps with the virtual controller's A, webs the wall and reels until the
player's actor tilts past 45 degrees (the game's wall crawl), lets go, and on
the wall casts rays along the actor's up from a metre out: the wall's distance
from the feet and from eyes 1.65 m upright from them, without and with the
stand-off `GameTrackingRig` gives (`wallClearance`). Then it jumps off. Every
few milliseconds it records the actor's feet, up and forward, the mover's
flags and contact, and whether the swing owns the flight; the summary has the
time the actor took to turn onto the wall and back, how far it tilted in the
air, and the mover flags on the wall. It fails unless the player was held on
the wall, the stood-off eyes were at least 0.35 m from it, the modules stopped
and the hook entries were restored. Report: `reports/wall-crawl.json`; the game
window on the wall and after the jump in `reports/wall-crawl/`. A run took
under 10 s after the save loaded (October 8).

## Performance comparison

The initial successful headset test was `game-vr-20261004-131244.json`: 509 pairs
at 512-square resolution and 65 missed pairs. The game log reports VSync On and
an OS display refresh of 100 Hz. Only the saved VSync value was changed to 0;
`reports/vsync-before-performance-test.json` records its previous value (1).
It can be restored by setting `VSync` to 1 in the recorded graphics registry key,
or by enabling VSync in the game's settings. Other graphics values were preserved.

The new worker records mean, maximum, and last CPU wall time for each stage after
30 submitted warmup frames. `display_period` is the runtime's requested period,
not a GPU timing. In the older synchronous build, `copy_wait` included waiting
for the engine to submit a matching native pair. It now measures copying the
latest staged pair without that wait. `hand_overlay` includes any wait before
reusing its upload/allocator. `frame_rates` distinguishes new native scene pairs
from total XR submissions and reused pairs; reuse does not increase native FPS.
These measurements help separate runtime pacing from engine and GPU dependencies.
They are not GPU timestamp measurements. A fresh game process is required after
rebuilding. Use `-Size 512` for a comparison at the original resolution, or the
`-Size 1536` to reproduce that comparison. Normal launches use the runtime's
recommended dimensions times the render scale (`-RenderScale`, 50-200%).
Native and OpenXR image checks allow up to 8192 pixels per dimension (4096
before October 8), subject to runtime limits. `tools/probe_vr_load.py
--width 4608 --height 4896 --views 29` measures larger eyes in the game
without a headset; its phases report video memory in use and the game's
commit, and `--headings 1` keeps a capture run's shots phase short.

The 1536-square test `game-vr-20261004-133248.json` confirmed VSync Off but still
spent 29.76 ms of its 31.92 ms mean frame in the matching-pair wait. The staged
handoff subsequently passed `game-vr-20261004-135733.json` at the same resolution:
62.38 fresh pairs/second, 70.93 submissions/second including reuse, and 0.089 ms
in the copy call. The user reported much better frame rate. Mean `xrEndFrame`
time is now 13.43 ms; further profiling must separate rendering and runtime costs.
Its native-view lease includes initialization margin so it will not expire
ahead of the worker. The worker publishes a stopping state before normal cleanup,
and rate analysis excludes frozen cleanup samples while retaining active drops.
