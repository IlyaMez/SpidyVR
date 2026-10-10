# Spidy development notes

The detailed reference behind the [README](../README.md): how the launcher and
the release zip work, every tool and module, building, running VR from a
checkout, memory and graphics in VR, the lab, and checks without a headset.
What each build changed is in [CHANGELOG.md](CHANGELOG.md).

## Share Spidy: the launcher and the release zip

`Spidy Launcher.exe` (source in `apps/launcher`, built as
`build\windows-ninja\spidy_launcher.exe`) lets other people play without the
development setup. It is credited to Ilya Mezerowsky in its header, its About
tab, its file properties and the zip's README, and carries a Ko-fi button. On
start it checks the PC and shows what it finds:

- **The game:** in every Steam library (Steam's registry entries, then
  `libraryfolders.vdf` and the app manifest), else where you point it with
  *Change...*. It compares `Spider-Man.exe` with the supported build's SHA-256
  (read from `tools/inspect_game.py`) and explains when the copy is the Epic
  Games Store version or another file: it names the file's version against
  `EXPECTED_VERSION` and, from Steam's app manifest beside the game, the fix
  (a beta chosen in *Properties > Betas*, an update waiting, else *Verify
  integrity of game files*); only a newer version needs a Spidy update.
- **The VR runtime:** every registered OpenXR runtime, after *Automatic* (the
  default since October 7; `xr_runtime=auto` in `launcher.ini`, which replaced
  `runtime=`, so every earlier install starts on Automatic). Automatic leaves
  the choice to each session: `xr_runtime.detect` asks Virtual Desktop (asking
  it starts nothing), then SteamVR if `vrserver.exe` runs and Meta Quest Link
  if `OVRServer_x64.exe` runs, then Windows' active runtime, and takes the first
  with a headset; a runtime that is not running and not active is named, never
  started (asking SteamVR starts it). A chosen runtime reaches both the headset
  check and the game's XR worker (`XrConfig` version 7 carries the manifest
  path); reports record the session's in `xr_runtime`.
- **The headset:** *Check* runs `spidy_headset_probe.exe` against the chosen
  runtime, or on Automatic `tools\xr_runtime.py --detect`, and shows the
  headset and the runtime that has it.
- **The Visual C++ runtime:** 14.40 or newer, which the modules need. *Install*
  downloads Microsoft's installer, runs it only if Microsoft signed it, and
  checks again. The launcher itself is linked statically and needs nothing.
- **Memory** Windows can still promise programs, against `VR_COMMIT_MB` in
  `tools/run_game_vr.py`; below that, START VR asks before it starts.
- **Spidy's own files:** the modules, Python, and a writable folder.

START VR runs `tools/run_game_vr.py` with the chosen options and shows its
output live; STOP VR (or closing the window, after a question) stops the
session through a named event that `run_game_vr.py --stop-event` treats as
Ctrl+C, so the hooks come out and the report is written as before. Options
and the game's hash are remembered in `%APPDATA%\Spidy\launcher.ini`. A session
that ends with other VR settings than it began with (the SPIDY VR tab in the
game's Settings, or X for the aim markers) prints them on its last line, "VR settings
from the headset: ...", and the launcher saves them as its options. The
first start offers desktop and Start menu shortcuts.

### Updates

A player's copy updates itself from the newest release on GitHub
(`apps/launcher/update.cpp`; the parts without Windows calls, tested in
`tests/launcher_tests.cpp`, are in `include/spidy/launcher_update.hpp`):

- **Check:** at start, unless `update_check=0` in `launcher.ini` (About >
  UPDATES), a worker asks `api.github.com/repos/IlyaMez/SpidyVR/releases/latest`
  over WinHTTP (Windows' proxy settings) and offers the release when its tag
  is a newer `vX.Y.Z` than the launcher's. Unauthenticated, GitHub allows 60
  such questions an hour from one address.
- **What a release needs:** the asset `Spidy-<version>-win64.zip` and its
  SHA-256: the asset's `digest` (GitHub's own), else the notes' line "SHA-256 of
  Spidy-<version>-win64.zip: `...`" that `release.yml` writes. Keep both names
  if the workflow changes: a release without them is not offered.
- **Install** (only on *Update now*): the zip goes to the hidden
  `.spidy-update\` in the Spidy folder, its size and SHA-256 are checked, and
  Windows' `tar.exe` unpacks it (the folder's Python's `zipfile` where tar is
  missing). Each package file then replaces the folder's by renames, which
  Windows allows for the running launcher and loaded modules too: the old file
  to `.spidy-update\previous\`, the new one into place. A file the package
  lacks goes the same way when its folder holds package files (`tools\`,
  `python\`, `build\windows-ninja\`, `docs\licenses\`), so a removed tool or an
  older Python's files do not linger; files beside the launcher, `reports\` and
  other folders stay (`updatePlan`). A rename that fails after 3 s of retries
  (antivirus) undoes every rename so far.
- **Refusals:** it waits for a stopped VR session and finished checks, and
  before the download and again before the renames refuses while a process
  runs from the folder or `Spider-Man.exe` has the folder's modules loaded.
- **Restart:** the old launcher closes its window, releases its one-at-a-time
  lock, then starts the new `Spidy Launcher.exe --updated-from <old> --wait-for
  <pid>`. The new one waits for the old process to exit, deletes
  `.spidy-update\`, and says what it replaced.
- **Where:** only where `Spidy Launcher.exe` sits beside `tools\run_game_vr.py`
  with no `.git` or `CMakeLists.txt` there. The launcher in a checkout's
  `build\windows-ninja` neither checks at start nor installs; git updates it.

Make the zip with:

```powershell
.\tools\bootstrap.ps1 -Observer   # also fetches Dear ImGui for the launcher
.\tools\build.ps1 -Observer
.\tools\package.ps1               # dist\Spidy-<version>-win64.zip
```

It holds the launcher, the seven game modules, the Python tools a session
imports, the Windows embeddable Python 3.12.10 (hash-pinned), the notices and
[docs/PLAYERS.md](PLAYERS.md) as `README.txt`. Players extract it anywhere
they can write and start `Spidy Launcher.exe`. The version comes from
`project(Spidy VERSION ...)` in `CMakeLists.txt`. The supported game build is
one Steam build: a game update needs new addresses in Spidy before the
launcher accepts it. Other runtimes than Virtual Desktop and other controllers
than Quest Touch (Index bindings are also suggested) are untested.

### Publish a release

GitHub Actions builds releases on a clean machine and puts them under the
repository's **Releases**, zip attached. Publish from GitHub: **Actions >
Release > Run workflow** on `main`, choose the part of the version to raise
(patch 0.1.1 -> 0.1.2, minor 0.2.0, major 1.0.0), and run it. From a terminal:

```powershell
gh workflow run release.yml -f bump=patch                    # or minor, major
gh workflow run release.yml -f bump=patch -f publish=false   # a test run that changes nothing
```

[.github/workflows/release.yml](../.github/workflows/release.yml) runs on GitHub's
Windows Server 2022 image (Visual Studio 2022). It raises `project(Spidy VERSION
...)` in `CMakeLists.txt`, runs `bootstrap.ps1 -Observer`, `build.ps1 -Observer`
with the C++ tests, the Python protocol tests and `package.ps1`, checks that the
launcher carries the new version, and only then commits the version ("Release
Spidy 0.1.2", by github-actions), tags it `v0.1.2`, pushes both to `main` and
publishes "Spidy 0.1.2" with the zip, its SHA-256 and the commits since the
previous release. Only the repository's owner can release: a run anyone else
starts is skipped. Only what is on GitHub's `main` is released, so push first;
afterwards `git pull` brings the version commit here. A failed build changes
nothing. If `main` moved during the build, the push is refused: run it again.
Without *publish* the run does all of this but only checks the push, and keeps
the zip with the run for 14 days; on another branch it test-builds that branch
as it is. Given an existing tag, it builds and publishes that tag again (the way
out when publishing failed after the tag was pushed).

`tools/release.ps1` makes the version commit and tag on this PC instead; the
pushed tag starts the same workflow:

```powershell
.\tools\release.ps1 -DryRun   # the next version, and what the push takes along
.\tools\release.ps1 -Wait     # 0.1.1 -> 0.1.2 (-Bump minor, -Bump major, -Version 0.3.0), then follows the build
```

It stops when the checkout is not on `main`, lacks commits from `origin/main` or
has uncommitted edits in `CMakeLists.txt`, and undoes its commit and tag when
the push fails. Given the current version, `-Version` tags the current commit
without a commit of its own.

### Antivirus false positives

Spidy does, for a good reason, what antivirus heuristics are built to flag: an
unsigned program attaches a DLL to the running game (`observe_game.py` /
`bridge_game.py`), reads its memory, and updates itself by downloading and
replacing its own files. Engines that score behaviour and reputation can't tell
that from malware, so some flag the launcher or a module. To reduce it, in order
of impact:

- **Sign the binaries.** Reputation then carries across releases instead of
  resetting to zero on every new unsigned build. `build.ps1` signs the launcher
  and the shipped modules with `signtool` when a certificate is configured, and
  is a no-op otherwise (unsigned dev builds still work). Set, locally or as
  repository secrets of the same name (release.yml passes them through):
  - `SPIDY_SIGN_THUMBPRINT` — SHA-1 thumbprint of a code-signing certificate in
    the Windows store (a hardware token, or an imported PFX), or
  - `SPIDY_SIGN_PFX` (+ `SPIDY_SIGN_PASSWORD`) — a PFX file, or
  - `SPIDY_SIGN_ARGS` — raw `signtool sign` arguments for anything else, e.g.
    Azure Trusted Signing's `/dlib`.

  `SPIDY_SIGN_TIMESTAMP_URL` overrides the timestamp server. Certificate options
  for an individual: Azure Trusted Signing (cheapest, identity-history gated),
  Certum open-source code signing (token), or a standard OV certificate (token
  or HSM). A hardware token can't be used on a hosted CI runner; Azure Trusted
  Signing and a PFX can.
- **Report the false positive** to each vendor that flags it (Microsoft's
  Defender submission portal, and the others by name); for an open-source
  project this is usually resolved in days. Every shipped binary now carries
  version metadata (`apps/spidy_module.rc`) so a vendor can identify it.
- **Upload each release to VirusTotal** and keep the link in the release notes:
  it shows which engines flag it (generic/ML names like `Wacatac` or
  `ML.Attribute.HighConfidence` mean a heuristic false positive, not a real
  signature) and lets players verify the zip against its published SHA-256.
- Don't ever pack or obfuscate the binaries — that raises detections.

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
  [automation and its verified controls](AUTOMATION.md).
- Optional `spidy_observer.dll` and `tools/observe_game.py`: camera-update timing
  and transform diagnostics in the supported game process, with automatic hook
  disable/restore after capture. This is a diagnostic, not the VR game adapter.
- Experimental `spidy_bridge.dll` and `spidy_view.dll`: bounded native input,
  final camera pose, and lens tests. Commands expire without renewal.
- `Launch Spidy VR.cmd`: checks the headset, starts Steam Spider-Man with its
  launcher skipped, and attaches VR when your save has loaded. It stays running
  until game exit or Ctrl+C in its console. Eye resolution follows the active
  runtime recommendation, times `-RenderScale`. Click both thumbsticks to
  switch between VR and a flat game screen inside the headset. These new
  launch/toggle features are built and locally tested; headset validation is
  pending.
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
  it, in a freshly started game; `--watch SECONDS` watches it land and come to
  rest after the throw (3 s by default), `--bots` does the same with a bot, and
  `--fling-test` checks the game's flung reaction on the nearest bot.
- `tools/probe_game_body.py`: the player's body and fists in a freshly started
  game, no headset: lists the hero's rig (joint names, parents, rest pose),
  turns the body toward a scripted headset and controllers and measures how
  far the wrists and head land from them, saves the eyes' image looking down
  at the body (`eyes`), drives a scripted fist through the nearest thug as a
  controller would (`fist`), and sends one blow straight to the game's damage
  system (`punch`; `--punch-hero` also staggers the hero).
- `tools/probe_aim.py`: what a grip press would do with a scripted hand aimed
  at the sky, around the horizon, at the floor and at the nearest prop, read
  from the swing module's aim previews (what the headset's aim markers show),
  in a freshly started game; checks that nothing faulted and every hook entry
  is restored.
- `tools/probe_shooter.py`: the web shooter in a freshly started game, no
  headset: a scripted hand pulls its trigger aimed level, down, at the sky and
  to either side, holds it, and pulls three times quickly, through the swing's
  input commands; follows each ShotWebShooter the game spawns (where it
  starts, its direction and speed, where it ends), shoots the nearest bot
  within 40 m if there is one, stops and starts the shooter during play, and
  checks that nothing faulted and every hook entry is restored.
- `tools/probe_combat.py`: Spidy's web balls, fists and web pulls on the
  game's enemies, flying ones included. `--list` reads the game only: every
  bot as Spidy's scan sees it (the same RTTI rules: mover manager, traits,
  states, health, webbing), plus any actor with a `Bot` component the scan
  would miss. Without `--list`, in a freshly started game, it compares the
  web grab's own scan with that list, then tries web balls, a scripted fist
  and a pull on the nearest enemies (`--target flyer|walker|0x...`).
- `tools/probe_air_handoff.py`: whether the swing keeps the player it flies,
  in a freshly started game loaded with the virtual controller: jumps, swings
  up on a web, and in the air turns the input unfocused for 0.3 s (web held),
  suspends the game 0.25 s, turns the input unfocused for 2 s, and suspends
  the game 3 s like a pause; reads every step of the player's mover and fails
  on an airborne step without Spidy's command or a jump in vertical speed.
  `--modules DIR` runs it with another build's movement and ray modules.
- `tools/probe_wall_run.py`: the swing's own walls in a freshly started game
  loaded with the virtual controller: finds the nearest wall with world rays,
  jumps, webs it and reels in until the swing has the player on it, then
  walks along it and up it with the swing input's stick, stops, and jumps off
  with its jump; reads every step of the player's mover and fails if one of
  the game's own wall states appeared, a step on the wall was not the swing's
  in the air mode, the centre was not about 0.9 m from the wall (world rays),
  or the walk, the stop or the jump were off. `--walls off` runs the same
  approach with the game's wall crawl, for comparison.
- `tools/probe_wall_mount.py`: walking up onto a wall, in a freshly started
  game loaded with the virtual controller: onto the wall as above, down it to
  the ground with the swing input's stick, then the swing's stick at the wall
  (the mount: the probe presses the controller's A while the swing asks for
  the game's jump, as the VR worker does), a climb, down again, and into the
  wall with the controller's stick alone until the game's crawl begins (the
  hand-over). Fails if the wall did not take the player within 1.5 s of the
  stick, the game's crawl began during the mount, the climb was not the
  swing's at the walk's speed and clearance, or, the game's crawl once
  begun, the swing's wall did not have the player within 2 s and keep him.
- `tools/probe_wall_crawl.py`: how the game holds the player on a wall, with
  the swing's walls switched off, in a
  freshly started game loaded with the virtual controller: finds the nearest
  wall with world rays, jumps, webs it and reels in until the game sticks the
  player to it, reads the player's actor (feet, up) and mover every few
  milliseconds, measures with rays how far the feet and the eyes (placed as
  before, and stood off as `GameTrackingRig` does) are from the wall, jumps
  off, and saves the game window on the wall and after the jump.
- `tools/probe_vr_load.py`: renders the VR views at the headset's resolution
  where the player stands and reports, per phase, the game's frame rate, its
  render commands per frame, GPU use, and the CPU time of each game thread;
  `--profile-threads` samples where chosen threads spend their time, and
  `--gpu capture` with the `shots` phase saves eye images with the eyes'
  occlusion culling on and off. No headset is required.
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
mid-swing stays released until you squeeze that grip again. A web that meets
nothing within 100 m holds in open air there; switch **Webs hold in open air**
off (VR settings, the launcher's options) or start with
`Launch Spidy VR.cmd -NoAirWebs`, and it misses instead.

**WEB BUTTON** in the VR settings (*Web button* in the launcher's options,
`Launch Spidy VR.cmd -TriggerWebs`) swaps the two buttons: the trigger shoots
and holds a web, and the grip reels it in and shoots web balls. The swap is
made where the controllers' input becomes the swing's (`trackedSwingInput`),
so the swing, the grab, the web shooter and the aim markers all follow it;
fists, the T-pose calibration and the game's menus keep the controllers' own
buttons. A change lets go of both webs, so a button held across it does
nothing until it is let go.

Webs are drawn by the game's own web-line system. If they look wrong in the
eyes, start with `Launch Spidy VR.cmd -OverlayWebs` for Spidy's overlay strands.

Aim the web at a throwable prop (one the game's combat lets you web and
throw) or a thug, and it catches it instead. Pull the trigger to reel it in, or
pull the hand back sharply to yank it to you; it then hangs below your hand on
a short web and swings as you move. Swing your arm and let go of the grip to
throw it: it flies with what the swing gave it, a heavy one slower, and a
throw close to a thug is aimed to hit them. The controller hums with the web's
pull.
`Launch Spidy VR.cmd -NoWebGrab` keeps webs for swinging only. See
[docs/WEB-GRAB.md](WEB-GRAB.md).

Each hand without a web shows an aim marker where its grip would send the
web now, in its hand's colour (the left's sky blue, the right's orange): a
ring around a dot where the web would hold, a faint dashed ring where it
would hold in open air (no marker there with webs in open air switched off),
a red cross where it would miss, a turning ring of three arcs with claws around
a prop or thug it would catch. It tightens as you squeeze. **X** hides or
shows the markers; `Launch Spidy VR.cmd -NoAimMarkers` starts with them hidden.

Pull the **trigger** of a hand whose web is not attached to shoot a web ball
from that wrist where the controller points: the game's own web-shooter shot.
Aimed within about 7 degrees of a thug, with nothing in between, it goes to
him; otherwise it splats on the first thing in its way, or ends in open air
about 60 m out. One ball per pull; the hand ticks as it leaves. A trigger held
from a reel or a menu shoots only after a release. Start with
`Launch Spidy VR.cmd -NoWebShooter` to keep the trigger for reeling only;
since October 8 neither the VR settings nor the launcher's options have that
switch.

**VR settings** are a tab of the game's own Settings: pause (the menu
button), choose Settings, then **SPIDY VR**, after KEY MAPPING (Up from GAME
reaches it; the list wraps). The game builds and draws it with its own option
code, so it handles like its other tabs: choose the WEB BUTTON (GRIP or
TRIGGER; `GameTrackingRig::triggerWebs`, XrConfig options bit 12, XrData
settings bit 64), switch the aim markers and webs in
open air, or step the swing speed limit, weight, snap turn, smooth turn,
controller vibration, the game screen's size and the HUD (OFF, SMALL, MEDIUM,
LARGE; XrConfig and XrData `hud`, see [The HUD in VR](#the-hud-in-vr)) with
left and right, switch the experimental FLIPS (off by default;
`GameTrackingRig::flips`, XrConfig options bit 11, XrData settings bit 32)
and step their FLIP SPEED (90 to 480 degrees a second at full tilt of the
left stick, 180 by default; `GameTrackingRig::flipSpeed`, XrConfig and XrData
`flipSpeed`), and switch STAND ON WALLS (on by default: walking a wall or
stopped on it, the view turns so the wall is the floor; `GameTrackingRig::standOnWalls`, XrConfig
options bit 13 for off, XrData settings bit 128); XrConfig and XrData are
version 20.
X resets a setting, Y the
whole tab (to Spidy's defaults). Web grabbing, the web
shooter, your body and punching are not in it or in the launcher's options
(since October 8): they are always on unless a launch option below turns
them off, and the game draws the webs unless `-OverlayWebs`. Changes apply at once; Spidy Launcher starts your next
session with them. Weight is the swing's gravity in percent of real gravity
(40-300%, 60% by default: Spidy's 6 m/s² since October 5), while webs fly the
player and after letting go until landing (`Swing::setGravity`, game_swing
`Settings` version 3). Smooth turn (off by default) replaces snap turning while it
is on: the right stick turns you about your head at up to the chosen degrees a
second, in proportion to its tilt past a 0.2 dead zone (full from 0.9)
(`GameTrackingRig::smoothTurn`).
`src/game_menu.cpp` adds the tab when the pause menu's Settings hand their tabs
to Flash and answers the game's questions about its rows (setting numbers from
0x200, past the game's 123), so the game's own settings and its settings file
never see them. The title screen's Options build their own lists and have no
such tab. `tools/probe_menu.py` checks it in the running game
([docs/AUTOMATION.md](AUTOMATION.md)).

You are Spider-Man's body: look down to see it, your arms and hands follow the
controllers, and the body turns with you once you look far enough to the side.
A squeezed grip, or a hand moving fast, closes into a fist. Punch a thug with a
fist moving 2.2 m/s or more and he takes the game's melee damage and reacts as
hard as you hit him; the controller kicks. `-NoBody` hides the hero and draws
gloves instead; `-NoPunch` lets fists pass through. See
[docs/BODY.md](BODY.md).

The intro, menus, loading, the map, hint cards, cutscenes, finishers and death
appear on a screen in front of you, as the game shows them on the monitor; VR
resumes when play does. On that screen the VR controllers are the game's Xbox
controller: the left thumbstick moves through menus, A selects, B goes back,
X and Y are the game's X and Y, the grips are the bumpers (menu tabs), the
triggers are the triggers, the right thumbstick and the stick clicks are the
same on both, and the menu button is Start. The game shows Xbox prompts, which
sit where the Touch controller buttons are. In immersive VR the menu button
pauses the game, Y opens the game menu (map, suits, skills), and B is the
game's Y, its interact button (backpacks, doors, consoles, prompts; web strike
in a fight), except while a web carries you. Every other control stays with
VR, which walks and jumps through the same virtual controller. A held from the
screen (Resume, Continue) does not jump, nor B held from it interact. A real
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

When the launcher starts the game, the game opens in a small 1920 x 1080 window
(16:9, at most three quarters of the desktop's height), centred. The engine still runs culling, shadow
setup, and auto-exposure for that view; a small window keeps that work and skips
most of its rendering. The launcher saves the game's window settings in
`reports\desktop-view-before-vr.json` and writes them back after the game
closes. If the launcher stops while the game is still running, they are written
back the next time it starts the game, or run `python tools\vr_display.py
--restore` after closing the game. A game that is already running keeps its
size. Add `-FullDesktopView` to start the game with your own settings.

Whatever the window, a game the launcher starts also runs without frame
generation (`Graphics\DLSSG` and `FrameGen` 0, where the game saved them) and
without Windows.Gaming.Input (`Input\EnableWindowsGamingInput` 0, created if
missing and deleted again afterwards); the backup's `session` entry holds the
values to put back. Under SteamVR the game finds Steam's virtual gamepads
(Valve 28de:11ff) through Windows.Gaming.Input, then turns XInput off, and
`game_pad.cpp` serves the VR controllers through XInput: in the Steam Link
session of October 7 the game never read it (`pad_reads` 0) and its menus did
not move. At 0 the game skips Windows.Gaming.Input (`1d14bf0` reads the
setting, `1d14c89` jumps past its setup). A session whose game screen ignores
held buttons for 5 s says so (`pad_ignored`).

Each session writes its console to `reports\game-vr-<time>-console.log`, and a
session that ends before VR starts writes `game-vr-<time>.json` with the
`stage` it reached, the `error`, `xr_runtime`, `queue_search` (why each look
for the game's queue failed), the game's log copy and `game_modules` (overlays
and capture tools loaded into the game). The October 7 Steam Frame player's
folder had neither: the launcher had stopped looking for the queue.

A game that runs as administrator (Steam started as administrator, or
`Spider-Man.exe` set to) is closed to a session that does not: Windows
refuses opening its process (error 5, "Access is denied"), for reading too.
`vr_launcher.wait_for_game` asks Windows whether Steam, then the game, runs
as administrator (`elevated`: the process token's elevation, which Windows
answers for a process it otherwise keeps closed) and ends the launch with
`NeedsAdministrator`, for Steam before anything is changed or started. The
session then exits with code 5 (`NEEDS_ADMINISTRATOR_EXIT`,
`kNeedsAdministratorExit` in the launcher), at which the launcher's window
asks to start again as administrator (`restartAsAdministrator`: the `runas`
verb with `--wait-for <pid>`, as after an update). A game that refuses for
5 s (`REFUSAL_PATIENCE`) without that explanation ends the launch naming a
security program, or administrator rights as the first thing to try where
Windows does not say how the game runs. The startup report's `administrator`
says whether the session itself ran as administrator.

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
not a fixed 1536-square image. The render scale multiplies each side by 50% to
200% (the launcher's "Render resolution", `-RenderScale`, `--render-scale`):
above 100% the image is supersampled, sharper and less jagged, for frame rate
and memory. Scaled sizes are multiples of 8, at most the runtime's
`maxImageRect` and 8192 a side, keeping the recommendation's shape
(`spidy::scaledEyeSize`). The launcher prints the selected dimensions and what
they are of. For an explicit square size instead, use
`tools/launch-game-vr.ps1 -Size 2048`.

On this PC (RTX 5090, the user's save, without a headset, October 8) 150% of
3072 x 3264 rendered 66 frames a second looking ahead and 71 turning, against
90 and 103 at 100%, with the GPU 92% busy; it took 2.5 GB more video memory
and as much more commit, about 108 bytes per extra eye pixel
(`EYE_COMMIT_BYTES` in `run_game_vr.py`, which the memory warning and the
launcher's memory figure use). Aim markers are drawn at their 100% size
whatever the scale.

The new launcher, automatic dimensions, and thumbstick toggle are built. Their
combined headset check was deferred at the user's request. Returning to VR
after stopping the launcher is not yet seamless. Restart the game before
reattaching after a stopped session or a rebuilt DLL.

### Launch options

`Launch Spidy VR.cmd` passes its arguments to `tools\launch-game-vr.ps1`:

| Option | Effect |
|---|---|
| `-SwingSpeed 32` | Swing speed limit in m/s (1-65) |
| `-Weight 100` | How heavy you are while webs fly you, in percent of real gravity (40-300; 80 by default) |
| `-SnapTurn 30` | Snap turn angle in degrees; 0 turns it off |
| `-SmoothTurn 120` | Smooth turning in degrees a second at full tilt, instead of snap turning (0-360; 0, the default, snap turns) |
| `-Haptics 100` | Controller vibration in percent; 0 turns it off |
| `-ScreenSize Medium` | Size of the game screen in the headset: Small, Medium or Large |
| `-RenderScale 125` | Eye resolution in percent of the runtime's recommendation, per side (50-200; 100 by default) |
| `-Size 2048` | Square eye resolution instead of the runtime's recommendation (not with `-RenderScale`) |
| `-NoWebGrab` | Webs swing only; they do not catch props or thugs |
| `-NoAirWebs` | A web that meets nothing within 100 m misses instead of holding in open air |
| `-NoWebShooter` | The trigger only reels |
| `-NoAimMarkers` | Start with the aim markers hidden (X shows them) |
| `-Flips` | Experimental flips on (off by default): in the air the left stick turns you over, and a tap of A flips you |
| `-FlipSpeed 240` | How fast a flip turns you at full tilt of the left stick, degrees a second (90-480, default 180) |
| `-TriggerWebs` | The trigger shoots and holds webs, and the grip reels them in and shoots web balls (WEB BUTTON: TRIGGER) |
| `-EyeHeight 1630 -ArmLength 590` | Your T-pose calibration in millimetres (the console's last "VR settings from the headset" line has it): Spider-Man's body takes your eye height and arm length, and the first gameplay asks for none |
| `-NoCalibrationPrompt` | Without a calibration, do not ask for the T-pose at the first gameplay (Settings > SPIDY VR > CALIBRATE BODY still does it) |
| `-NoBody` | Hide the hero and draw gloves |
| `-NoPunch` | Fists pass through thugs |
| `-OverlayWebs` | Spidy's overlay strands instead of the game's web lines |
| `-FullDesktopView` | Start the game with your own window settings, not the small VR window |
| `-StockMonitorView` | Keep the stock camera on the monitor (culling and shading follow it, not your head) |
| `-NoEyeOcclusion` | Turn off the eyes' occlusion culling, for comparison |
| `-XrRuntime <manifest>` | Use this OpenXR runtime manifest instead of the one the headset is connected to |
| `-AttachOnly` | Attach to a game that is already running instead of starting it |
| `-Seconds 20` | A timed test that stops after 2-25 seconds |
| `-CaptureImages` | Save eye images (costs readback time) |

## Memory for a VR session

Two memory limits matter, and neither of them is RAM running out.

**Render memory.** The game keeps two frames of draw lists and render commands
in a 128 MB ring. Three scene views need more, so the launcher gives the game a
512 MB ring while the game starts (see the [changelog](CHANGELOG.md), October 5, fifth build). A few seconds
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

- the runtime-recommended resolution (3072 x 3264 in the latest session) times
  the render scale, with private scene buffers at that size;
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
at 1290 x 540 it was about 3%, at 1720 x 720 about 6%, and at 1920 x 1080
(since October 9's seventh build, for the HUD: see
[The HUD in VR](#the-hud-in-vr)) it is about 7% of 3840 x 4080 eyes, 10% of
3072 x 3264 ones. The October 5
morning session averaged 44 new
stereo pairs per second at
3072 x 3264 per eye with the headset at 120 Hz; about 40% of the frames sent to
the headset repeated an earlier pair, which doubles fast-moving scenery. The
largest cost is eye resolution: lower Virtual Desktop's quality preset, or the
render scale below 100%. Ray-traced
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

## The HUD in VR

The game's HUD has two parts (`include/spidy/native_hud.hpp`,
`src/native_hud.cpp`, and the placement in `src/stereo_probe.cpp`):

- **The panel**: health, gadgets, minimap, prompts. `ui/export/ModelHudFull.gfx`
  is drawn into a texture (a `ScaleformRTTStream`: base 1920 x 1080, its width
  times the window's aspect over 16:9) that a 16:9 model ("modelHudFull", 2.25
  units tall) carries. `PlayerModelHudFollower` places that model every frame
  (`0x73ab80`, main thread) 20 m in front of the camera manager's camera
  (`0x60444d0`, read through `0x1642040`), scaled so the texture covers what
  that camera's view shows (at the October 9 save the view is 0.8125 half wide
  as a tangent, the model's 56% middle). It is drawn in each view's GUI pass,
  so the eyes draw it, but in VR that camera is still the third-person one.
- **The second movie**: world markers (POIs), subtitles, QTE and other
  prompts, pause menus. One Scaleform movie (global `0x7be3e00`) laid out in
  the window's pixels. The "Scaleform" render command (`0xa8`, handler
  `0x1d2b6c0`) draws a list of movies each frame (two lists at `0x7be3f40`,
  128 items of 24 bytes: movie, render target or 0 for the game's view, frame
  stamp, clear flag). The list holds the panel's movie (into the panel's render
  target, cleared) and this one (into the game's view), which the eyes never
  get. Its markers are placed through `0x1f10b60` / `0x1f10ad0` (to `0x1f10c20`:
  the active view's view-projection at view +0x100, returning 0 to 1 across
  the screen); the POI code calls them on the main thread.

What Spidy does:

- **Placement.** While immersive, after the follower's own placement, Spidy
  moves the panel in front of the head with the game's transform setter
  (`0x191c0e0`): 2 m ahead, the part the game's view showed spanning the HUD
  setting's width (`native_hud::halfWidth`: 50, 60 or 70 degrees across), all
  three scales changed alike. A transform set any later (at `0x1920240`, where
  the eyes are placed) did not reach the frame's render. The frame's head is
  latched once (`latchHead`, at the first camera submit): the active view, the
  panel, its markers and the eyes all use that command and that travel, so the
  panel never trails the eyes (`offsetMismatch` stays 0).
- **Lazy follow.** The panel faces where `native_hud::Follow` turned it, not
  the head's exact way. The XR worker updates it every headset frame from the
  head's orientation in the tracking space (the room): it holds still while
  the head looks within 2 degrees of it, then glides back in front of the head
  (63% of the way every 0.15 s) until within 0.25 degrees, upright. It is
  reset to the head when play, the immersive view or a recentred room begins.
  The worker hands its orientation seen from the head to `native_hud::follow`
  under the eye command's serial; `latchHead` takes the one for its command
  and turns the head pose by it (`native_hud::turned`), and the panel and its
  markers use that turned pose, so the panel stays fixed in the room between
  glides. A head-locked panel shook on the headset: a quarter of the images
  shown in the October 9 session were earlier ones, turned by the runtime to
  the newer head, and the panel turned with them.
- **HUD setting.** `vr_settings::Values::hud` (0 off, 1-3 small to large) goes
  to `native_hud::setSize` whenever the settings apply. Off, the panel's render
  instance is left out of the eye views in `renderActor` (`0x17991a0`, per eye
  and draw), the second movie stays in the game's view and markers keep the
  game's projection; the game screen, which copies the game's view, keeps it
  all.
- **Texture.** For the session the panel's stream makes its texture at the
  second movie's size (the window's: `textureBase` picks the base width the
  stream's `int(base * factor)` turns into it; `+0x78`, `+0x7c`, and `-1` at
  `+0x9c` re-create it). The stream's update (`0x2104400`) runs on the main
  thread before the follower's update; clearing the follower's `+0xc0` makes
  that update bind the new texture in the same frame. `stop()` puts the game's
  size back and binds it.
- **Second movie on the panel.** While the headset shows the eye views
  (immersive or flat; `eyesShown()` from `placeEyes` every frame), the render
  command draws the second movie into the panel's render target (stream
  `+0x20`, size at `+0x1cc`) after the panel's own movie, instead of into the
  game's view. Within 100 ms of the eye views stopping (menus, cutscenes,
  loads: the game screen), it goes back to the game's view.
- **Markers.** While immersive, the HUD's own callers of the two projections
  (return addresses in `0x72a000`-`0x7c0000` and `0x1f00000`-`0x1f10980`; the
  `0x81xxxx` callers are targeting code and keep the game's view) get the
  point projected from the head onto the panel (`native_hud::project` through
  the turned pose: where the line from the head to the point crosses the
  panel), with the game's margins for "on screen". On the flat screen the
  panel is where the game put it, so the game's own projection fits.

The VR window is 1920 x 1080 (`tools/vr_display.py`; 16:9 whatever the
desktop's shape, at most three quarters of its height). The game sizes its HUD
by the window's height: at 1080 rows the panel's texture (1920 x 1080) has
about the detail of a Quest 3's eye images at 125% across its 60 degrees
(1660 against 1640 pixels per unit of tangent; 1720 x 720 had 1490), and its
16:9 layout shows everything a third larger on a panel as wide as the 21:9
one of the fifth build. `SpidyHudData` (version 3) counts the
game's placements, panels placed and rejected, the texture's size and changes,
frames with the second movie on the panel, markers projected, the HUD setting,
frames and eye draws that left the panel out, placements the follow turned,
and the follow's angle from the head; session reports carry it as `hud`.
`tools/probe_hud.py` checks all of it in the game without a headset: it drives
VR-like eyes through head poses, the game screen and the flat screen, sets the
HUD's size and a turned panel through `SpidyHudSet` (the XR worker's follow
needs a headset), and saves both eyes and the window.

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
| Trigger on a webbed prop or thug | Reel it in until it hangs from your hand |
| Pull hand sharply back from it | Yank it to your hand |
| Swing your arm and release grip while it hangs from your hand | Throw it |
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
in [AUTOMATION.md](AUTOMATION.md). A 30-second free-roam run captured 3,362
valid camera/player observations; its hook was disabled and original entry
bytes restored afterward. Native eye rendering is now demonstrated in a bounded
diagnostic; the playable VR game adapter is still in development.

## Development map

See [validation results](VALIDATION.md) for what passed locally and what
still requires a running game or headset.

| Path | Role |
|---|---|
| `include/spidy`, `src/swing.cpp` | Engine-independent physics and control logic |
| `src/xr_session.cpp` | OpenXR timing, tracking, actions, stereo submission |
| `src/d3d12_renderer.cpp` | Lab renderer, tracked hand overlay, diagnostic readback |
| `src/native_webs.cpp` | The game's own web lines, started at the tracked wrists |
| `src/web_grab.cpp`, `src/lab_props.cpp` | Web grab: catching, yanking, carrying and throwing props and characters; the lab's props |
| `src/body_ik.cpp`, `src/native_body.cpp` | The player's body: the solver, and the hero's joints turned after the game's pose writer |
| `src/body_calibration.cpp`, `src/overlay_text.cpp` | The T-pose calibration (the player's eye height and arm length, and its panel in the headset); the overlay's stroke font |
| `src/punch.cpp`, `src/game_punch.cpp` | Punching: fists against characters, and the game's own melee damage for each punch |
| `src/shooter.cpp`, `src/game_shooter.cpp` | The web shooter: trigger pulls and their aim, and the game's own web-shooter shot fired from the hand |
| `src/slow_motion.cpp`, `src/game_time.cpp` | Slow motion: the left stick's click, focus and the eased time scale; the game's own time system slowed to it (its look: `D3D12Renderer::Grade`, the meter in `web_visual.cpp`) |
| `src/game_grab.cpp`, `src/native_bodies.cpp`, `src/game_targets.cpp` | Web grab in the game: candidates, freed Havok props, flung bots |
| `src/native_render_memory.cpp`, `tools/vr_launcher.py` | A larger per-frame render memory ring, installed while the game starts |
| `src/web_visual.cpp` | Fallback game-style web strands for the headset overlay and lab |
| `src/game_xr.cpp`, `src/game_tracking.cpp` | Native eye presentation and Quest controls |
| `src/native_hud.cpp`, `include/spidy/native_hud.hpp`, `tools/probe_hud.py` | The game's HUD in VR: its panel in front of the head, the second movie (markers, subtitles, prompts) on it, and its check in the game |
| `src/native_rays.cpp`, `src/native_movement.cpp`, `src/game_swing.cpp` | Native world queries and collision-controlled swing requests |
| `src/lab_world.cpp`, `apps/xr_lab.cpp` | Synthetic collision world and VR lab |
| `tools/inspect_game.py`, `tools/discover_render_types.py` | Offline game research |
| `src/game_observer.cpp`, `tools/observe_game.py` | Opt-in native camera callback diagnostic |
| `tools/game_driver.mjs` | Observed game launch, screenshots, and input workflow |
| `tools/vr_display.py` | Small desktop window, no frame generation and no Windows.Gaming.Input for VR launches; restores the game's settings |
| `tools/xr_runtime.py` | The OpenXR runtimes, and the one a headset is connected to (`--detect`) |
| `docs/MILESTONE-1.md` | In-game acceptance criteria and remaining integration work |
| `docs/REFERENCE.md` | Reference mechanics, executable evidence, source attribution |

The bounded adapter connects `spidy_openxr` to the game's D3D12 device and direct
queue, then copies matching native eyes after their submitted GPU markers.
See [milestone details](MILESTONE-1.md) for its remaining validation.
