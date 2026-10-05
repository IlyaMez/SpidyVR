# Spidy

An in-development VR mod project for **Marvel's Spider-Man Remastered**, targeting
**Quest 3 + Virtual Desktop**. First milestone: 6DoF head/controller tracking,
native stereo, and physically controlled web swinging.

**Latest test build (October 5, second build):** fixes for the user's morning
session. VR dropping to the flat screen was not a game crash: Spidy stopped VR
after 3 seconds without new eye images. Spidy follows each eye render job from
the game's copy until it ends, in a 128-entry table. The game occasionally drops
a copied eye job without rendering it, and those entries were never freed; after
about 10,500 frames the table was full, no later eye frame could be matched, and
VR stopped. Height was not the cause. Entries older than 16 frames are now
reclaimed. The web starts drifted because the game's web lines were started from
the hero position read in its rope update, while the eyes used the hero position
read later in the frame; any movement in between separates web and hand, more at
higher speed and not at all when standing. Both now use the rope update's
position, and the session report measures the gap that removes. Double images at
speed come from frame rate: about 44 new stereo pairs per second arrived at every
speed, and about 40% of the frames sent to the headset repeated an earlier image.
A repeat is invisible when standing still but shows the city twice at 30 m/s;
see [Graphics settings in VR](#graphics-settings-in-vr). The desktop view no
longer renders at full size during VR: when the launcher starts the game, it
opens a 1290 x 540 window (on a 3440 x 1440 desktop; about a seventh of the
pixels) and your display settings return after the game closes. The game still
needs that view for culling, shadows, and exposure, but not its pixels. It passes
80 core checks, the GPU test, and 39 Python checks; headset confirmation is
pending. Fully close the game and restart `Launch Spidy VR.cmd` to load the new
modules and the small window.

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
  separate eye renders, two webs, gravity, reeling, physical zips, and point launches.
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
2. Double-click **Launch Spidy VR.cmd**.
3. Select your save and Continue in the game's normal menu. VR starts when the
   player is loaded; there is no second attach command.

Webs are drawn by the game's own web-line system. If they look wrong in the
eyes, start with `Launch Spidy VR.cmd -OverlayWebs` for Spidy's overlay strands.

Click **both thumbsticks** to switch between immersive VR and a flat screen
inside the headset. Release both before clicking again. Flat mode uses the
stock game camera and normal PC/gamepad controls; VR movement and web input are
released. In immersive VR the monitor shows your head view, horizontally
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

Resolution defaults to the runtime's recommended width and height for each eye,
which can change with your Virtual Desktop quality preset. These are the
[OpenXR recommended render dimensions](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrViewConfigurationView.html),
not a fixed 1536-square image. The launcher prints the selected dimensions.
For an explicit square override, use `tools/launch-game-vr.ps1 -Size 2048`.

The new launcher, automatic dimensions, and thumbstick toggle are built. Their
combined headset check was deferred at the user's request. Initial save menus
still use the normal desktop presentation; load/respawn transitions and returning
to VR after stopping the launcher are not yet seamless. Restart the game before
reattaching after a stopped session or a rebuilt DLL.

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
| Aim hand, hold trigger, squeeze grip | Attach that hand's web |
| Keep grip held | Keep swinging |
| Release grip | Release web and retain momentum |
| Release trigger, then hold it again | Reel the attached web in |
| Pull hand sharply away from the anchor | Zip once per attachment |
| Left stick | Move / steer |
| Right stick left/right | 30-degree snap turn |
| Right A | Jump; shortly after a zip landing, point launch |
| Left Y | Reset to the lab rooftop |
| Escape on PC | Exit |

Clear shots that reach no surface create a fixed anchor at maximum reach
(100 metres by default). Hold grip and release/repress trigger to reel toward
either a real surface or an open-space anchor.

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
