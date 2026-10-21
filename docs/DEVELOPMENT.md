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
  *Change...*, or in the Epic Games Store's install manifests. It compares
  `Spider-Man.exe` with the supported build's SHA-256 (read from
  `tools/inspect_game.py`). Another file of `EXPECTED_VERSION` (an Epic Games
  Store copy) is tried, marked yellow: every module checks the game's code where
  it works with it and refuses where it differs, and a session starts such a
  copy through the Epic Games Launcher's link when Steam has none. For any
  other file it names the file's version against
  `EXPECTED_VERSION` and, from Steam's app manifest beside the game, the fix
  (a beta chosen in *Properties > Betas*, an update waiting, else *Verify
  integrity of game files*); only a newer version needs a Spidy update.
- **The VR runtime:** every registered OpenXR runtime, after *Automatic* (the
  default since October 7; `xr_runtime=auto` in `launcher.ini`, which replaced
  `runtime=`, so every earlier install starts on Automatic). Registered means
  Windows' active runtime, the Khronos registry's `AvailableRuntimes`, and
  SteamVR by the folder OpenVR's own file names
  (`%LOCALAPPDATA%\openvr\openvrpaths.vrpath`, its `runtime` list, with
  `steamxr_win64.json` in it): SteamVR is in the registry only while it is the
  active runtime, so beside an installed Virtual Desktop or Meta Quest Link
  app a Steam Link headset had no SteamVR to choose or to ask (0.2.9 and
  older). Automatic leaves
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
- **Spidy's own files:** the modules, Python, and a writable folder.

START VR runs `tools/run_game_vr.py` with the chosen options and shows its
output live; STOP VR (or closing the window, after a question) stops the
session through a named event that `run_game_vr.py --stop-event` treats as
Ctrl+C, so the hooks come out and the report is written as before. Options
and the game's hash are remembered in `%APPDATA%\Spidy\launcher.ini`. A session
that ends with other VR settings than it began with (the SPIDY VR tab in the
game's Settings) prints them on its last line, "VR settings
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

It holds the launcher, the seven game modules, bHaptics' library and the
haptics test ([Body haptics](#body-haptics-bhaptics)), the Python tools a session
imports, the Windows embeddable Python 3.12.10 (hash-pinned), the notices and
[docs/PLAYERS.md](PLAYERS.md) as `README.txt`. Players extract it anywhere
they can write and start `Spidy Launcher.exe`. The version comes from
`project(Spidy VERSION ...)` in `CMakeLists.txt`. The supported game build is
one Steam build: a game update needs new addresses in Spidy before the
launcher accepts it. Other runtimes than Virtual Desktop and other controllers
than Quest Touch are untested: Index and Steam Frame layouts are suggested
too (`controller_layout.hpp`), neither tried on the controller.

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
  runtime recommendation, times `-RenderScale`. VIEW in the game's Settings >
  SPIDY VR switches between VR and a flat game screen inside the headset
  (a click of both thumbsticks did until October 12).
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
- `tools/preview-marks.ps1` (and `spidy_mark_preview`, `apps/mark_preview.cpp`):
  draws the combat marks over two eye captures of a VR session, or over a
  plain backdrop, as PNG sheets of their states and poses, and the gadget
  wheel over the near one (`near-wheel.png`: as it lands, at rest, as a
  gadget lights). The overlay's own vertices, one sample a pixel, at about
  the headset's pixel density. No game and no headset.
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
- `tools/probe_sense.py`: Spider-Sense in a freshly started game: whether
  the hero's dodge manager is found and the game's clock read, every warning
  the game gives (attacker, answer, flags, how long) and the census of them;
  `--provoke` punches the nearest enemy first so a fight starts, `--counter`
  throws a scripted fist at each warning asking for a counter and reads the
  counter, the break, the attacker's states and the player's health across
  the blow. Writes `reports/sense-probe.json`.
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
- `tools/probe_prompt_watch.py`: what the game asks a player for, in the
  running game with no headset. The prompt reading rides game_menu's hooks, so
  a VR session is not needed: `SpidyMenuStart` puts the same hooks in and
  `SpidyPromptsWatch` (0 off, 1 read, 2 read and rewrite) turns the reading on,
  `SpidyPromptsWatched` copies out the counters, the millisecond timeline and
  the census of every name the UI called with one example of its arguments,
  and `SpidyPromptsNote` puts the probe's own presses in that timeline beside
  the game's calls. `--press y` sends the game's interact every couple of
  seconds.
- `tools/probe_prompt_load.py`: the same reading through a save's load, which
  is where it used to end the game (October 13 to 20: keepLine's overflow).
  It launches the game itself, turns the reading on at the title, presses A
  through CONTINUE and watches free roam (`--seconds`), writing
  `reports/prompt-load.json` every second, so a death leaves what came
  before it, and listing every second the game named something. `--dll`
  loads another build's stereo module for a before and after. Close the game
  with `tools/close_game.py` and end Steam afterwards.
- `tools/probe_vent.py`: the same reading, plus driving the game there from one
  attach, with a little script of steps - `a*6@2` (a press, repeated, seconds
  apart), `walk:4` and `look:2,-1,0` (the sticks, held), `watch`, `watched`,
  `press:y` (a press with a note), `shot:name`, `wait:2` and `release`. One
  process for a whole run instead of one per press. `release` calls
  `SpidyPadRelease`, which hands the player his own controller back: the
  virtual pad holds input slot 0, and the game stops reading the keyboard once
  it has seen a pad press, so without it a probe's press leaves whoever is at
  the keyboard unable to move until the game is closed. A probe that never
  submits a pad state never takes the pad at all, so a read-only watch can run
  while someone plays.
- `tools/probe_vr_load.py`: renders the VR views at the headset's resolution
  where the player stands and reports, per phase, the game's frame rate, its
  render commands per frame, GPU use, memory and clock, the GPU time of each
  view (the game's own, then each eye), and the CPU time of each game thread;
  `--upscale`, `--no-eye-antialiasing` and `--views` choose what a session
  would (`--views 29 --no-eye-antialiasing` is the eyes before October 10's
  eighth build);
  `--profile-threads` samples where chosen threads spend their time, and
  `--gpu capture` with the `shots` phase saves eye images with the eyes'
  occlusion culling on and off. No headset is required.
- `tools/probe_render_commands.py`: counts the render commands the game
  queues in a frame, by command and by view (the game's own, each eye, and
  the lists no view owns), with the engine's names for them.
- `tools/close_game.py`: closes the game after a probe (its window first,
  the process if that does not do it) and restores the settings a session
  changed; `--keep-settings` leaves those alone.
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

**Aim.** Each hand aims from its web shooter (`webShooter`, tracking.hpp,
made in `trackedSwingInput`): the palm side of the wrist the body draws, 8 cm
back from the controller's grip point, pointing along the hand (the grip's
-y, from the wrist out past the knuckles; up is the thumb's side). The swing,
the grab, the web shooter, gadgets, pointing at the game's interactions and
the aim markers all take it, and the webs the game and the overlay draw
start there. The controller's aim pose is not used: Meta's runtime, and
Virtual Desktop's copy of its poses, turn it 30 degrees from the hand toward
the thumb. Punches put the fist 13 cm on along the hand (game_punch).

**Webs in the air.** game_swing's `physicsConfig` turns on
`SwingConfig::taut` - a web in the air is no slacker than its hand made it by
reaching toward its end (`HandState::slack`, `handSlack` 0.8 m at most),
checked after the winch and allowing for the hand's own pull in the step, or
those pulls are taken in twice - and `groundClearance` 1.4 m: a body a web
holds from above has its fall turned along the ground below it at
`groundTurn` (15 m/s^2), its speed kept, so it skims with the feet 0.4 m up.
No web is wound in for the ground: an October 15 build that did pulled the
player toward the web's end half the time he swung. A bare `SwingConfig` has
both off, as it has walls and physical webs.

**Arcade webs** (WEB STYLE: ARCADE, `SwingConfig::arcade`, `Swing::allowArcade`,
the swing's settings version 7). Every ray a web's shot, its clear line, its
held line, its bends and its unbending cast goes through `Swing::solid`: past a
hit that is not fixed, or that `Swing::thin` calls thin, and out of it - the
far face the ray then meets from inside, turned along the ray, is skipped -
six things at most. Thin: a ray from `thinDepth` (1.5 m) behind the face,
back out to it, meets a face turned against it (the thing's far side, so the
ray started in the open). Started inside a building, it meets the same face
from within, turned the same way, or nothing: the game's rays meet faces from
either side. A web then neither sticks to a lamp post, a sign or a car, nor
bends round one, nor is cut off by one; the shot's anchor is what comes after.
`skimGround` uses `arcadeClearance` (6 m) for `groundClearance`. Each shot
through something and each held line coming onto something is an
`EventKind::Pass`, counted in the rope data's `passed`.

**Webs round corners.** A web bent round edges (physical webs) is drawn by
the game's rope from the web shooter round each edge to where it sticks: the
ray module hands the edges on (`SpidySwingBends`, `game_swing::Bends`,
nearest the hand first), and native_webs lays them as the rope's first points
in the game's hand-points step (95f9c0, `native_webs::ropePoints`), as it lays
a web held in both hands.

**Hand wall climbing** (HAND WALL CLIMBING, experimental and off by default;
Adrian's pull request 24). `Swing::allowHandClimb` (the swing's settings
version 8, `handClimb`). On the swing's own wall, with the stick at rest, a
hand squeezed within 18 cm of the wall holds it where a ray from 35 cm out,
back along the wall's normal, finds fixed geometry (`Swing::wallContact`);
the body then moves by that hand's motion relative to the head, turned into
the world and flattened onto the wall, two hands averaged, at most 3 m/s
(`climbInputs`, `wallMotion`). A held hand shoots no web and grabs nothing
(`Swing::wallHand`, `game_swing.cpp`'s `wallHands`). The swing command's
version 5 carries each grip's place and the controller's own squeeze
(`wallPositions`, `wallGrips`), whichever button WEB BUTTON gave the webs, and
`wallRotate` while both grips turn the view. `SpidyClimbSample`
(`game_swing::ClimbData`) counts each hand's new holds, for a buzz, and says
which hands hold, for open palms (`HandPoseInput::wallContact`). The view:
`GameTrackingRig::handClimb` keeps it upright beside the wall, the head
lowered up to 0.7 m and `wallDistance` (WALL CAMERA DISTANCE, at least
`HeadClearance::margin`) out from it; both grips roll it about the view's
axis (`WallRollGesture`: 10 degrees of play, full speed, 90 degrees a second,
from 45); the stick runs along the wall with the heading taken once and then
steered by the head's yaw only (`wallRunHeadYaw`); round an outside corner
the view turns onto the next face at 4 radians a second and the swing carries
the body 0.85 m on along it. Head clearance casts 0.75 m further and takes up
to 32 passes between opposing walls while climbing close.

**Realistic flips** (REALISTIC FLIPS and the nine rows under it,
experimental and off by default; pull request 23, iplusdvr, after the
interaction of Blaze's Spider-Man for Blade & Sorcery, with none of its code).
`RealisticFlip` (`realistic_flip.hpp`) keeps an angular velocity and turns the
tracking space's tilt by it, about the head, as `FlipMotion` does; no turn has
a set size. On a web, above a travel speed, the body's up is eased toward the
webs' pull (their directions weighted by tension, from `game_xr.cpp`'s flight
sample); a web let go leaves the spin of the swing's orbit; arm swings faster
than a threshold, read from the hands relative to the head before any of
Spidy's motion (`AutomaticHandMotion`), add spin; drag takes it away; landing
or A in the air (`RealisticControls::brake`) brings the body upright with the
heading kept. The rig's yaw keeps that heading: while the player's up is
within a quarter turn of the world's, `RealisticFlip::takeHeading` hands the
tilt's turn about the vertical to `GameTrackingRig` every frame, which adds it
to the yaw about the tilt's pivot (nothing placed moves), so the tilt holds
only the lean and is the identity again once level. Until October 20 the turn
stayed in the tilt, which the stick's walk leaves aside: after a landing every
walk went off to the side of the look, and the body, taking the turn for a
lean, kept its legs in the air pose. With FLIPS on too, the left stick, once
let go in the air, steers it. `GameTrackingRig::automaticFlips` switches between the two;
`tests/realistic_flip_tests.cpp` is its own test program (ctest
`realistic_flips`).

The SPIDY VR tab's settings that no launcher option carries (NEAR CLIP,
RECORDING FOV, HAND WALL CLIMBING, WALL CAMERA DISTANCE, the realistic flips'
rows) are kept by the stereo module itself:
`vr_settings::tabOnlyText` lines in `%APPDATA%\Spidy\spidy_vr.ini`, read as VR
starts and written when the tab changes one (`game_xr.cpp`, `tabOnlyPath`). A
file beside the module would not do: it runs from a copy staged per build
(`reports/stereo-modules/<sha256>/`).

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
options bit 13 for off, XrData settings bit 128), and step GAME SCREEN 3D
(OFF, LOW, MEDIUM, HIGH: the depth cutscenes have on the game screen;
`vr_settings::screenDepths`, XrConfig and XrData `screenDepth`); XrConfig is
version 23 (664 bytes: `screenDepth`; `upscale`, `monitorRate`; options bit 15
for no eye anti-aliasing, bit 16 for the game's own monitor view, bit 17 for
each eye's own shadow set) and XrData version 24 (832 bytes: `lookError`,
`lookTurns`; `heals`, `zips`; `screenDepth`, `stereoSubmitted`;
`interactSteps`).
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
hard as you hit him; the controller kicks, time stops for a moment, a burst
shows where the blow landed and it sounds. A fist thrown at a thug up to 10 m
away flies you to him and lands as you arrive (the web strike), blows in a row
count up to a finisher, an uppercut lifts him and you can follow him up
(`include/spidy/web_strike.hpp`). He fights one enemy at a time (`Lock`): a
look marks the enemy the next punch goes to, a blow locks the mark on him
until he is down, and a strike goes to the marked enemy alone
(`StrikeWish`); a knockout of his is lettered and eases time for an instant.
The marks of a fight (the ring over the
thug the next punch goes to, the called blow's arrow, the bursts, the count)
are printed as the graphics of Spidy's videos are: the style system's colours
(ink `#0b0a14` for every keyline, paper `#f7f5ee`, red `#e3262f`, yellow
`#ffe14d`, cyan `#12d6ff` and magenta `#ff2e93` for the misprint's two
ghosts), a keyline round every shape, a hard drop instead of a shadow, and
motion on twos: a pose held a twelfth of a second, then the next
(`Print` in `src/web_visual.cpp`). To see a change to them without a
headset, run `tools/preview-marks.ps1`. `-NoBody` hides the hero, with no hands
of any kind (Spidy's own gloves went on October 15); `-NoPunch` lets fists pass
through and switches the strike off with them. See [docs/BODY.md](BODY.md).

The intro, menus, loading, the map, hint cards, cutscenes, finishers and death
appear on a screen in front of you, as the game shows them on the monitor; VR
resumes when play does. Scenes the game plays there (cutscenes, finishers,
deaths: a player exists and the game's frames take game time) are in stereo:
the game renders its own camera once for each eye, set apart for your eyes
and that screen (eye command mode 3, `native_eyes::Stereo`), and the headset
shows each eye its own image on the screen. The far background lies as far
behind the screen as a horizon, what is `GAME SCREEN 3D` of the screen's
2.5 m from the camera (LOW 0.5, MEDIUM 0.7, HIGH 1) lies on it, and nearer
things come out of it; the HUD's panel with the subtitles is moved to that
distance, so it lies on the screen. Menus, hint cards and loading keep the
game's flat frame, and so does a scene whose eyes get no HUD panel
(`ScenePanel`). `-ScreenDepth Off` is the flat frame everywhere. On that screen the VR controllers are the game's Xbox
controller: the left thumbstick moves through menus, A selects, B goes back,
X and Y are the game's X and Y, the grips are the bumpers (menu tabs), the
triggers are the triggers, the right thumbstick and the stick clicks are the
same on both, and the menu button is Start. The game shows Xbox prompts, which
sit where the Touch controller buttons are. In immersive VR the menu button
pauses the game, X opens the game menu (map, suits, skills), and B and Y are
the game's own: B its dodge, the way the left stick points, and Y its interact
button (backpacks, doors, consoles, prompts; web strike in a fight), both
except while a web carries you (`game_pad::Walk`; XrData `dodges` and
`interacts` count the presses that reached the game). The game takes its Y
only within about a metre of the thing (a door's "Open Door" prompt showed
1.1 m from its middle and not 1.2 m), so Y held past 0.3 s walks the player
on where they look, at half stick, until the game's scene ends play, Y is let
go, the walk meets a wall, or 2 s have passed; not in a fight, in the air or
on a wall (`game_pad::InteractStep`, XrData `interactSteps`). The game also
takes its Y only for a thing its own follow camera has in view (about 30
degrees to either side at a door) and its hero faces, and VR shows neither:
the virtual controller's right stick turns that camera to the head's level
direction all through VR play (`game_pad::CameraLook`, `Walk::look`, XrData
`lookError` and `lookTurns`), and every press of Y out of a fight starts with
the left stick where the player looks for 0.15 s, which turns the hero, the
game's Y staying down 0.6 s at least. The targets of fists, webs and web
balls are chosen by what the game says of them, not by their classes
(`game_targets`): a bot in play (a state in its state machine) that is up
(hit points left, none of the game's states for one who is down), and for
web balls any other actor with the game's webbing tracker, a `Kind::device`
(a gun turret). The right thumbstick's
click is the game's LB + RB together, its web pull where it shows the two (a
door, a grate), under the same condition (`game_pad::PullPress`,
`Walk::pull`): the grips shoot webs in VR and are no bumpers there. That
stick pushed straight down is the game's D-pad down, its heal (the
controllers have no D-pad), in the air too; pushed straight up it is the
game's LT + RT together, its zip to a point and into a vent, while a web
carries you too since October 18 (`game_swing::AirYield`). A push counts
from the stick at rest, at 0.75 or more and no more than 0.45 aside, after
0.15 s; turning, a diagonal and a flick push nothing (`game_pad::StickPush`,
`Walk::heal` and `Walk::zip`; XrData `heals` and `zips`). Held on where the
game has no point, LT alone is its aim mode, which slows its own time after
about 0.6 s and snaps its aim to the points it can zip to: once its time
has slowed, LT stays down alone and letting go of the stick presses RT
again beside it for 0.2 s, the zip to the point the aim holds
(`game_pad::ZipPress`, `Walk::aim`; XrData `zip_releases`). Until October 12 neither had a control, and the first mission
stopped at its heal and its vent. Until
October 11 B was the interact, Y the game menu and X switched the aim
markers, which are a setting only now. Every other control stays with
VR, which walks and jumps through the same virtual controller. A held from the
screen (Resume, Continue) does not jump. B, X and Y stay what they were
pressed as until let go (`game_pad::PressOrigins`): held from the screen they
do nothing in play, and held from play nothing on the screen. A real
gamepad keeps working. The keyboard does not: once the virtual controller has
pressed a button, the game ignores the keyboard for the player. The screen has the size of the
game's desktop window, small when the launcher starts the game;
`-FullDesktopView` makes it sharper.

Which button is which comes from the layouts Spidy suggests to the runtime
(`controller_layout.hpp`, `XrRuntime::suggest`): Touch, Index and the Steam
Frame. A runtime that gets no layout for the controller in the player's hand
translates one of another's, and SteamVR's translation of the Touch layout
for the Frame makes its B, X and Y all Touch B (its
`frame_controller_remapping.json`): until October 12's eighth build a Frame
player's X and Y dodged. The Frame's layout needs Valve's extension
(`XR_VALVE_frame_controller_interaction`), so it is offered only where the
runtime lists that; its View button and D-pad are actions of their own, made
only there, and reach the game as its Back and its D-pad in play and on the
screen alike (`buttonView`, `buttonDpad...` in `XrFrame::buttons`). Its
bumpers are left unbound: Valve's page names them `/input/bumper`, Godot's
layout `/input/shoulder`, and one path the runtime does not know loses the
whole layout. A refused layout costs nothing else: VR starts and the runtime
translates as before. XrData version 26 (856 bytes) has `controllers`, the
layout the runtime uses for each hand now (`ControllerKind`: Touch, Index,
Steam Frame, or one of the runtime's own), and `controllerLayouts`, the ones
it took; the session log prints the first once the runtime says. A Frame on
`touch` with `steam_frame` among the layouts taken is a binding the player
chose in SteamVR; without it there, a SteamVR that lacks the extension.

VIEW in the SPIDY VR tab (GAME SCREEN, or VR) switches between immersive VR
and the game as it is on the game screen inside the headset; until October 12
a click of both thumbsticks did, which a player hit by accident (each stick's
click has a use of its own: slow motion, the game's LB + RB). Each session
starts in VR. GAME SCREEN plays like the game screen of menus and cutscenes
(`vrPlay` false in `game_xr.cpp`): the stock game camera, in 3D at GAME SCREEN
3D's depth when the HUD panel reaches it (eye mode 3, else the presented frame,
flat), the controllers the game's Xbox controller (`Mapping::menus`) with Back
and the D-pad added by `game_pad::ScreenControls`; VR movement, the body and
web input are released. Until October 15 it was eye mode 2, flat, played with
a real gamepad. In immersive VR the monitor shows your
left eye, cut to the window's shape (the game does not render a view of its
own then: see [Graphics settings in VR](#graphics-settings-in-vr)); in flat
mode and on the game screen it shows the normal game view. `-GameMonitorView`
has the game render your head view for the monitor instead, horizontally
stretched, as before October 10's eighth build. Add `-StockMonitorView`
to keep the normal game view on the monitor in immersive VR too, at the cost of
culling and shading that follow the stock camera instead of your head.

When the launcher starts the game, the game opens in a small 1920 x 1080 window
(16:9, at most three quarters of the desktop's height), centred. The engine still runs culling and
shadow setup for that view, and renders it whenever you are not in immersive VR
(the game screen, the flat screen); a small window keeps that rendering small,
and the game sizes its HUD by the window. The launcher saves the game's window settings in
`reports\desktop-view-before-vr.json` and writes them back after the game
closes. If the launcher stops while the game is still running, they are written
back the next time it starts the game, or run `python tools\vr_display.py
--restore` after closing the game. A game that is already running keeps its
size. Add `-FullDesktopView` to start the game with your own settings.

Whatever the window, a game the launcher starts also runs without frame
generation (`Graphics\DLSSG` and `FrameGen` 0, where the game saved them),
without vertical sync (`Graphics\VSync` 0, since October 10's eighth build:
with it on the game waits for the monitor before each frame, so the eyes'
frame rate can only be the monitor's rate divided by a whole number; the
session of October 10, 19:34 started with it on at 120 Hz) and
without Windows.Gaming.Input (`Input\EnableWindowsGamingInput` 0, created if
missing and deleted again afterwards); the backup's `session` entry holds the
values to put back. Under SteamVR the game finds Steam's virtual gamepads
(Valve 28de:11ff) through Windows.Gaming.Input, then turns XInput off, and
`game_pad.cpp` serves the VR controllers through XInput: in the Steam Link
session of October 7 the game never read it (`pad_reads` 0) and its menus did
not move. At 0 the game skips Windows.Gaming.Input (`1d14bf0` reads the
setting, `1d14c89` jumps past its setup). A session whose game screen ignores
held buttons for 5 s says so (`pad_ignored`).

Since October 13 such a game also runs without ray-traced reflections
(`Graphics\RTReflections` 0, where the game saved it). The eyes cannot have
them, and the game's own view, the only one that did, rests in VR play
(`restGameView`). With them on, the first frame that view drew again hung the
graphics device: the game ended with its display driver error
(`DXGI_ERROR_DEVICE_HUNG`) as a menu, the map or a cutscene opened. In a
report that is a session whose last samples have `presentation` `immersive`
with `gate` `no_camera_commit`, the frame's render memory up by about half
(the view has a job again) and no frame after it; the game log has
`RT Reflections: On` and `Device removed detected`. Why the game's ray-traced
path does that after a rest was not looked into. A game that runs with the
setting on all the same (one the launcher did not start, or whose settings
Windows kept from it) keeps its own view rendering, as with
`--game-monitor-view` (`ray_traced_view` in `tools/run_game_vr.py`; the
report's `eyes.ray_traced`). Turning the setting on in the game during a
session is not covered.

Since October 13's third build such a game also runs with its Aspect Ratio at
Auto (`Graphics\ForcedAspectRatio` 0, where the game saved it). The value is
the choice in the game's Display settings counted from Auto: the option's
reader (`1cc1e70`) and writer (`1cca6d0`) touch offset `0x28` of the
settings alone, which the settings table (`1cf7bc0`) names
`ForcedAspectRatio`; `AspectRatio` beside it (`0x24`) is a number in
thousandths. With a fixed shape two players' games ended as VR started
(October 11; both had VR at Auto), and one report of it (16:9, a 1920 x 1080 window) ends with
the device removed after 30 eye pictures: `gpu.completed` is
18446744073709551615 and `gpu.pairs` 0. What the game does to an eye's view
with a fixed shape was not looked into.

Since October 18 it also runs with NVIDIA Reflex off (`Graphics\ReflexMode`
0, the choice counted from Off, where the game saved it). An RTX 4070 SUPER
player's game (Reflex On + Boost, windowed on his third monitor at 165 Hz,
Windows build 26300) lost its graphics device by the 19th eye picture in
every session, on Virtual Desktop and SteamVR, on every preset, with the
reason 0x887A002B (DXGI_ERROR_ACCESS_DENIED) in its log and the same
`gpu.completed` all ones; its log lists the one card as two adapters, which
no other game log kept here does. Reflex paces the game by its presents,
and in VR play the stereo module holds back the presents beyond the
monitor's rate (`native_gpu::holdPresent`, since October 11): the one session
run here with Reflex on (October 10, Low Latency) predates that. Whether
Reflex is the cause is not known. A session that ends with the game's log
saying `Device removed detected` prints the code and what to try
(`device_removed`, `device_lost_advice` in `tools/run_game_vr.py`; the
report's `device_removed`), and every report lists the adapters the game
listed (`game_adapters`). "Normal camera on the monitor" (`--stock-monitor-view`)
is the launcher's way around everything VR play starts doing to the game's
own view: it keeps that view rendering, so nothing rests it, draws the left
eye over its window or holds back its presents.

A game that already holds Spidy - VR ran in it, or another build's module is
in it: it was open while Spidy was updated, or another Spidy folder started
VR in it - is named before the headset is looked for (`restart_reason`,
`other_build` in `tools/run_game_vr.py`, over `SESSION_MODULES`; `OtherBuild`
from `bridge_game.prepare` if one appears later). The session ends with exit
code 6 (`CLOSE_GAME_EXIT`, `GameNeedsRestart`), and the launcher offers to
close the game and start VR again: the next session gets `--close-game`,
posts `WM_CLOSE` to the game's windows (`vr_launcher.close_game`: its close
button, never a kill, so it saves as it quits), waits for its process to end
and 3 s for Steam, restores the other folder's saved game settings if
another folder's build was in it, and starts the game as usual. Until
October 18 the render memory module's warning said only "Different bridge
build is resident", and the queue search went on for three minutes.

Once the game's process is there, the session looks in its folder for
another program's way into the game (`other_mods` in `tools/run_game_vr.py`):
a file named as one of Windows' libraries (`WRAPPERS`), which the game loads
from its own folder before Windows'. The game's folder holds none. With the
R.E.A.L. VR mod's files beside one (`REAL_VR`) the session ends at the stage
"checking the game's folder" with the files in the report's `other_mods`:
that mod's `dxgi.dll` left a player's headset black in six sessions (no
frame submitted, `slow_motion.status` 9702, once `gpu.error` 3502). Any other
such file is named in a warning and the session goes on.

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

That is the size of the image each eye sends to the headset. The scene inside
it is rendered smaller and brought to that size by the game's temporal
anti-aliasing (the launcher's "Upscaling", `-Upscale`, `--upscale`): 50% to
100% of the eye's size per side, 67% by default (2064 x 2192 for a
3072 x 3264 eye), in multiples of 8 (`spidy::eyeRenderSize`). 100% is
anti-aliasing alone. The HUD, subtitles and markers are drawn at the eye's
full size. See [Graphics settings in VR](#graphics-settings-in-vr).

On this PC (RTX 5090, the user's save, without a headset, October 8) 150% of
3072 x 3264 rendered 66 frames a second looking ahead and 71 turning, against
90 and 103 at 100%, with the GPU 92% busy; it took 2.5 GB more video memory
and as much more commit, about 108 bytes per extra eye pixel. Aim markers are
drawn at their 100% size whatever the scale.

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
| `-ScreenDepth Medium` | Depth of cutscenes on the game screen (stereo): Off (flat), Low, Medium or High |
| `-RenderScale 125` | Eye resolution in percent of the runtime's recommendation, per side (50-200; 100 by default) |
| `-Size 2048` | Square eye resolution instead of the runtime's recommendation (not with `-RenderScale`) |
| `-Upscale 67` | The size the scene inside each eye is rendered at, in percent of the eye's size per side; the game's temporal anti-aliasing brings it to the eye's size (50-100; 67 by default, 100 is anti-aliasing alone) |
| `-NoEyeAntialiasing` | The eyes as they were before October 10's eighth build: no anti-aliasing, no upscaling, for comparison |
| `-NoPromptHelp` | Leave the game's own button prompts alone: Spidy neither reads which button they ask for nor names the VR control in them, and the interact button is the game's Y as before (the pad layer still gives the whole controller) |
| `-NoWebGrab` | Webs swing only; they do not catch props or thugs |
| `-NoAirWebs` | A web that meets nothing within 100 m misses instead of holding in open air |
| `-NoWebShooter` | The trigger only reels |
| `-NoAimMarkers` | Start with the aim markers hidden (AIM MARKERS in the SPIDY VR tab shows them) |
| `-Flips` | Experimental flips on (off by default): in the air the left stick turns you over, and a tap of A flips you |
| `-FlipSpeed 240` | How fast a flip turns you at full tilt of the left stick, degrees a second (90-480, default 180) |
| `-TriggerWebs` | The trigger shoots and holds webs, and the grip reels them in and shoots web balls (WEB BUTTON: TRIGGER) |
| `-ArcadeWebs` | Arcade webs (WEB STYLE: ARCADE): webs go through lamp posts, signs, cars and other thin or loose things, and swings stay 6 m above the street |
| `-EyeHeight 1630 -ArmLength 590` | Your T-pose calibration in millimetres (the console's last "VR settings from the headset" line has it): Spider-Man's body takes your eye height and arm length, and the first gameplay asks for none |
| `-NoCalibrationPrompt` | Without a calibration, do not ask for the T-pose at the first gameplay (Settings > SPIDY VR > CALIBRATE BODY still does it) |
| `-NoBody` | Hide the hero (no hands are drawn) |
| `-NoPunch` | Fists pass through thugs |
| `-OverlayWebs` | Spidy's overlay strands instead of the game's web lines |
| `-FullDesktopView` | Start the game with your own window settings, not the small VR window |
| `-StockMonitorView` | Keep the stock camera on the monitor (culling and shading follow it, not your head) |
| `-GameMonitorView` | The game renders its own head view for the monitor while you are in VR, as before October 10's eighth build (a third scene view; the monitor otherwise shows your left eye) |
| `-MonitorRate 30` | How often the monitor gets your left eye in VR, pictures a second (1-240; 30 by default; 0 is every frame, as before October 11). The launcher's "Monitor frame rate" |
| `-OwnShadows` | Each eye renders its own sun shadow maps, as before October 11; without it both use one set fitted to a lens covering both |
| `-NoEyeReflections` | The eyes without the game's screen-space reflections, whatever its Screen Space Reflections setting (they follow it otherwise) |
| `-EyeLensDistances` | The eyes judge detail, draw and texture distances by their own wide lenses, as before October 19: about half the flat game's distances (see [Graphics settings in VR](#graphics-settings-in-vr)) |
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
did not fit; the report has the same under `render_memory`. Since October
10's eighth build the game's own view is not rendered while you are immersive,
and a frame's commands fell from 7.6 to 5.6 MB at one measured spot (11.6 to
8.4 MB turning); the ring stays at 512 MB.

**Windows commit.** Windows promises each program the memory it asks for, out
of RAM plus page file, whether or not the program then uses it. The game's GPU
allocations count as well. In VR at 3072 x 3264 per eye the game takes about
17 GB of promises (measured before the eyes' scene was upscaled; on October
10 a laptop with an RTX 4080 Laptop GPU, 12 GB, measured 13.8 GB committed
and 6.4 GB of video memory on a rooftop with 0.2.11, 13.2 and 5.8 GB with
the eighth build's default upscaling, 14.2 and 6.7 GB with upscaling off).
On October 5 this PC had 61.6 GB of RAM with half of it
free, but other programs held about 50 GB of promises and the page file was
4 GB, which left 14-15 GB. The game went past that. Windows enlarges a system-managed page file when this happens, and while
it does, requests for memory can stall or fail. The report records
`free_commit_mb` at the start and at its lowest. Until October 10 the launcher
had a Memory row and asked before a start with less than about 19 GB left, and
the session printed a warning; all three are gone (the row was rarely green,
on a PC with 32 GB of RAM too, and players asked how to make it so). Either of
these makes room:

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
(windowed, size, position), frame generation, vertical sync, ray-traced
reflections, the aspect ratio, depth of field, sharpening and Windows.Gaming.Input for that
session and writes them back after the game closes; see
[Start Spider-Man in VR](#start-spider-man-in-vr).

The two eyes are extra native scene views. They inherit the game's own quality
settings, including textures, shadows, the Level of Detail setting, crowds,
ambient occlusion, and motion blur strength; not its ray-traced reflections,
its anti-aliasing and upscaling, nor the distances it judges detail by, which
Spidy sets for them. For each eye, Spidy sets only:

- the runtime-recommended resolution (3072 x 3264 in the latest session) times
  the render scale for the eye's image, and private scene buffers at the
  upscale's share of that (67% per side by default);
- the tracked eye pose and asymmetric headset lens, with the sub-pixel jitter
  the game's own camera has: a Halton (2, 3) point each frame (`eyeJitter` in
  `src/stereo_probe.cpp`; 8 phases at 100%, 18 at 67%, 32 at 50%);
- temporal anti-aliasing with upsampling, the game's own (since October 10's
  eighth build). An offscreen view gets none from the game with an upscaler
  chosen in its settings: the views' anti-aliasing mode (render context
  `+40c`) is then 0, and the upscaler (DLSS, FSR or XeSS: one instance, sized
  for the game's view) copies every offscreen view as it is. The eyes also
  had no jitter and no targets for it: the game makes a buffer set's
  `kTaaTarget0/1` and `kTaaDepth0/1` only for its own kinds of set, and sizes
  them by the game's display. Spidy sets the mode to 1 for an eye's job
  (`copyJob`), has the eye's set made with those targets (`initBuffers`) at
  the eye's size (`sizeBuffers`), and jitters the lens. The game's
  TemporalAa command then resolves the scene, rendered at the upscale's
  share, to the eye's full size from the last frames' samples.
  `SpidyEyeTemporal` chooses the parts and the share; the addresses are in
  [VALIDATION.md](VALIDATION.md). DLSS itself is not used for the eyes;
- the targets screen-space reflections need (since October 11's second
  build): the game makes `kSsrHistory` and the rest of that kind only for its
  own views' buffer sets, and its ScreenSpaceReflection command, queued for
  every view, returns at once without them. An eye's set is made with them
  (`initBuffers`: the six `kSsr` targets and the four shared half-size ones of
  the same kind; without those four the pass runs and adds nothing, as
  October 11's third build showed), unless Screen Space Reflections is off, or
  Ray-Traced Reflections on, in the game's settings when VR starts
  (`eye_reflections` in `tools/run_game_vr.py`) or `-NoEyeReflections` is
  given: then the eyes are as before that build. The buffers are made once,
  so a change of the setting reaches the eyes at the next VR start; the pass
  itself stops at once when the setting goes off;
- the active post-processing profile and the main camera's display path;
- the main camera's exposure, so both eyes are drawn equally bright. The game
  adapts exposure on the GPU, per view: a view's luminance pass (`18449f0`)
  averages the view's own image and moves the view's "Adapted Lum" buffer
  (view `+16f0`) towards it, later draws read that buffer (`t67`), and the
  tone mapper multiplies the image by it (`189c710`, `t11`). Each eye's render
  job reads the main view's buffer instead of its own (`shareExposure` in
  `src/stereo_probe.cpp`, since October 10's sixth build; until then each eye
  was exposed for what it alone saw). The numbers the game reads back to the
  processor (`+1708`, `+170c`) are copied from the main view as well. While
  the game's own view is not rendered (below) it measures nothing: the left
  eye then keeps its own exposure and the right eye takes the left's;
- the HUD's sRGB overlay setup pass, which the game skips for secondary views;
- avatar visibility while immersive VR is active (this also hides the avatar
  and its shadow on the monitor);
- the distances the game judges models by (since October 19). The engine
  scales every such distance by a number each view's scene submit takes from
  the view's lens (`189e4b0` puts it on view `+1cc4`: 0.6745 over the tangent
  of half the lens's width, or 0.3794 over that of half its height for a lens
  wider than 16:9), times view `+1cc8`, which only cutscenes set. It decides
  which detail level a model draws (`17930b0`), how far away a model is drawn
  at all (`1792b90`, `1796c40`, which also apply the Level of Detail setting)
  and up to where its textures are asked for (16 times its size times the
  scale, `17993e7`); many other systems read the same product. The game's
  65-degree camera has 1.06, an eye's 98-degree lens 0.59: in VR, windows came
  onto buildings, people onto the street and textures into focus at about half
  the distance the flat game has them. Each eye's `+1cc8` now brings its
  number to 1, the engine's own 68-degree lens (`matchDistances` in
  `src/stereo_probe.cpp`, `native_view::eyeDetail`: 1.71 for that eye, never
  below 1, at most 3). An eye's model jobs also ask for textures as strongly
  as for its image, not for the smaller scene the upscale renders
  (`setupModels` on the job setup `1796d20`: job `+2fc` is half the pixels per
  unit of lens tangent, from the view's rendered size, `+448` and `+44c`; 1.49
  times at 67%), as upscalers are meant to have it; what a job leaves out as
  too small to see (`+300`) stays at the rendered size. The session report's
  `appearance` has `eye_detail` and `texture_request_raise`;
  `-EyeLensDistances` (views option 64) is the old way, and
  `tools/probe_streaming.py` reads the texture streamer's requests (the
  `TextureStream` at `621b4e0`) and the game's own texture memory lines to
  compare the two.

While immersive, Spidy also moves the game's own camera view, the one on the
monitor, to your tracked head with a lens covering both eyes. The game does some
per-frame work only for that view: occlusion, "drawn in any view" checks, and the
shading inputs shared by every view of a frame, which carry the key-light shadow
setup and a camera position available to every shader. Moving the view lines
that work up with the headset. That view now covers a wider field of view than
the stock camera, which may cost some frame rate; compare with `-StockMonitorView`.

That view on the head, and the eyes, take NEAR CLIP's near plane (2 cm unless
the player chose another, 1 to 10 cm; `near_clip.hpp`, set by `game_xr.cpp` as
the settings apply) instead of the stock camera's 10 cm, which cut off hands
held up before the face. The engine's depth is reversed, so the nearer plane
costs no precision far away. Pull request 22 (iplusdvr), tried by its author in
a headset.

Since October 10's eighth build the game does that work for its view but does
not render it while you are immersive, both eyes are live and no game screen
is wanted (`restGameView` in `src/stereo_probe.cpp`: the view's `+1208` is
cleared, at which the game's SubmitScene, `19206a0`, leaves at once; views
option 32). Rendered, it took 5.1 ms of the GPU in each 28 ms frame on an
RTX 4080 Laptop GPU, as much as an eye at 67%, and a third of the frame's
shadow-caster commands. The window then shows the newest left eye instead,
cut to the window's shape about its middle (`mirrorEye` in
`src/native_eye_gpu.cpp`, drawn over the back buffer in the Present hook).
The view renders again the moment a game screen is wanted, in flat mode, and
when the session stops; `-GameMonitorView` keeps it rendering throughout.

RECORDING FOV (the tab's, last under DISPLAY, off by default; pull request 21,
iplusdvr) keeps it rendering too, for recording: with an angle picked
(`recording_fov::set`, from `game_xr.cpp` as the settings apply) `placeEyes`
leaves the view awake, so the window shows it instead of the left eye, with
none of Spidy's drawings, and every present reaches DXGI (`holdPresent` holds
them only while the window mirrors). Its lens is then not the head's lens but
`recording_fov::covering`'s: centred, of the window's shape (`flatAspect`, the
stock camera's), at least the angle across, and never covering less than the
head's lens, since the eyes rely on this view's culling, occlusion and far
buildings' full models. Unlike `-GameMonitorView`'s head lens it is not
stretched in the window. A Quest 3 (54 degrees out, 44 in, 48 up and down,
with `headView`'s 5%) in a 16:9 window needs 128.5 degrees, so the choices are
130, 140 and 150. Both of the frame's submits of the view take that lens
(`viewLens`: `activeView`, which sets up the frame's culling, and
`placeEyes`), and the eyes' shared shadow cascades stay fitted to the head's
lens (`shadowLens`): rendering, the view fits cascades of its own. The left eye
stands in for nothing then (`isActiveView` and `impostorQueries` act only
while the view rests), so far buildings draw their full models within the
game's own 145 m. The author measured it in a Quest 3 at 3032 x 3248 an eye,
standing in one place 30 s each: 117.1 newly rendered frames a second off,
84.5 at 130 degrees, 112.3 off again, about 3 ms a frame more.

Some of what a resting view no longer does was done for everyone. Far
buildings are impostors, cards standing in for the model, and which of them
draw their full model, with the lit windows and the rooms behind them, the
game decides for its active view alone (`189daa0`: the view at
ViewContextManager `+f8`): `ImpostorHQModelRegJob` (`16fb630`, `16fb750`)
applies that view's wanted state per impostor from its visibility queries
(`16faf10`, in each view's scene submit), and the impostor submit job marks
it (`16f8afa`). Resting, the game's view submits nothing, so until October
19's second build every building beyond the nearest stayed a dark card in VR
and lit up only while a menu rendered that view. Since then, while it rests,
the left eye stands in for it at those five checks (`isActiveView`: a hook on
`189daa0` that answers yes for the left eye from those five return addresses
only), and the session report's `appearance.impostor_frames` counts the
frames it did. Only one view may: the registration applies a view's wanted
states over the previous ones. How far away a building draws its full model
is what the view's queries carry in their result (`+88`): 145 m in the game
(`7a89170`, which region changes rewrite), more for a lens narrower than 65
degrees. While the left eye stands in, both eyes' results get 150% of it,
218 m (`impostorQueries`, a hook on `16faf10`; `SpidyImpostorReach`, and
`--building-reach` for a session, set 50-400%); on October 19 that cost
nothing measurable at 3072 x 3264 eyes, and 200% about 1%. The right eye's outer edge, which the left
eye does not see, is not covered.

What the player sees is the eyes' since October 20's second build. Each
view's scene submit clears its list of drawn instances (`+1b80`, a byte per
instance, `+1b88` of them; `1920e54`) and its model jobs mark what they draw
(`1799a7d`, through job `+2b0`). Gameplay reads the lists: the
ViewportUpdateSystem's enter and leave events from the active view's
(`214f150`), and "drawn in any view" from the game's pool views' (`18a1890`,
7 direct call sites, and 55 more through `159e940`, which passes it the
index at `+64` of the instance it is given). The callers named so far: the
pedestrian groups and bots (`d9aa90`, `dbb220`), `PedestrianSystem`,
`PedestrianInteractBotManager`, the joint matching of attached models
(`20cc568` and its neighbours), `ClosestOnScreenTargeting` (`5a3020`), the
target scoring (`211c950`, named by projects-87), `TrackingSenseFocus`
(`8e4800`) and `AmbientFlyerStraight` (`1d40a00`); the traffic phases
(`d67740` to `d6b8b6`) reach none of them within three direct calls.
Offscreen views get no list (`189ce20` skips kind 2), and a resting view's
list kept its last rendered frame, so from 0.2.12 on nothing in VR counted
as on screen. While the game's view rests, `placeEyes` clears its list each
frame and `setupModels` points each eye's model job at it;
`SpidyDrawnFrames` counts those frames. The clear is in the hook of
`1920240`, which `175a060` calls just before the views' scene submits
(`19206a0`), so gameplay reads the list at the same point of its frame as
in the flat game (a read between the clear and the jobs' marks finds it
empty there too). Other active-view-only work still
stops while the view rests: animated models get part of their joint update
only from the game's view (model job `+342`), and `189daa0` has about 15
other callers (shadows, post-processing, decals).

Since October 13 that left eye is the one the headset showed. The XR worker
draws Spidy's own things (gloves and webs, the aim markers, the combat marks,
the focus meter, slow motion's colour, the calibration panel) onto the
headset's images, after the game's eye was copied into them, so the game's
own image of the eye has none of them, and until then the window had none
either. After that pass the worker hands the left image to
`native_gpu::keepShownEye`, which copies it to a texture of the bridge's own
on the game's queue, and only when the window has drawn since the last copy:
one eye-sized copy for each picture the window gets, none for the headset
frames in between. `mirrorEye` draws that copy while it is fresh
(`shownEyeFresh` in `include/spidy/monitor_rate.hpp`: half a second) and the
game's image of the eye otherwise, which is the first picture after a menu
and the window of a headset that takes no frames. The window's picture is
later than it was by up to one of its own pictures. `SpidyPresentCount(3)`
counts the presents that drew a copy, `(1)` every mirrored one.

The game presents its window once a frame whether or not it rendered a view
for it, and a present is not free: on a laptop whose screen hangs on the
other graphics adapter, each one of a 3840 x 2400 window took 8 ms of a 23 ms
frame (October 11: 43 frames a second with every present, 66 with none).
While the window mirrors the left eye, only the presents the monitor's rate
asks for reach DXGI (`holdPresent` in `src/native_eye_gpu.cpp`,
`include/spidy/monitor_rate.hpp`; 30 a second by default, the launcher's
"Monitor frame rate", `-MonitorRate`). The game goes on without the others;
the newest eye pair's pose was fresher for it.

The sun's shadow cascades are made once for both eyes since October 11. The
game's scene submit (`19206a0`) lets a view take another view's shadow sets
as a parent's, which it never does for offscreen views; the second eye's
submit now gets the first one's (`submitScene` in `src/stereo_probe.cpp`). A
view's cascades are fitted to its own lens, and the eyes' mirrored lenses put
their boxes most of a half size apart, so the first eye fits its set to the
head's lens covering both (`fitCascades`, `shadowLens`). That lens is wider:
the boxes are 15% larger a side over shadow maps of the same size, and shadow
edges that much coarser. A frame's render commands fall by a quarter; frames
a second by 1-3% where the graphics card is the limit and 4-10% where the
processor is. `-OwnShadows` is the old way. The light set (local lights'
shadows) is each eye's own still.

When it renders, the desktop view does so at the game window's size. Before the small VR window,
it rendered the full 3440 x 1440 desktop, about a fifth of the frame's pixels;
at 1290 x 540 it was about 3%, at 1720 x 720 about 6%, and at 1920 x 1080
(since October 9's seventh build, for the HUD: see
[The HUD in VR](#the-hud-in-vr)) it is about 7% of 3840 x 4080 eyes, 10% of
3072 x 3264 ones. The October 5
morning session averaged 44 new
stereo pairs per second at
3072 x 3264 per eye with the headset at 120 Hz; about 40% of the frames sent to
the headset repeated an earlier pair, which doubles fast-moving scenery. The
largest cost is the number of pixels rendered: choose a lower Upscaling
setting first (the eye's image keeps its size), then lower Virtual Desktop's
quality preset or the render scale below 100%. On an RTX 4080 Laptop GPU
(12 GB, drawing 74-80 W) on October 10, 3072 x 3264 per eye, without a
headset: 36 frames a second with 0.2.11, 39 with upscaling off, 61 at 67%
and 77 at 50% ([VALIDATION.md](VALIDATION.md)). Each view still renders its
own shadow maps. Ray-traced
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
  latched once (`latchHead`, at the first camera submit, after the game has
  moved the player): the active view, the panel and the eyes all use that
  command and that travel, so the panel never trails the eyes
  (`offsetMismatch` stays 0). The game projects the markers before it moves
  the player, so they take `earlyHead`: the newest command's head, moved to
  where the frame will draw the player (`native_eyes::HeroTravel`: where the
  last frame drew him plus that frame's travel); `markerMismatch` is the
  furthest that head was from the frame's. From October 9's fifth build to
  October 10's third (0.2.8, 0.2.9) the markers latched the frame's head
  themselves, with the last frame's player: the active view, which the
  monitor shows, and the panel trailed the eyes by a frame of travel (up to
  1.35 m in a swing; `active_view_lag_frames` in the session reports).
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

## Body haptics (bHaptics)

A bHaptics TactSuit (the vest, 32 motors), a TactSleeve on each forearm (3
each) and a TactVisor on the headset (4) feel what happens to Spider-Man. Three
parts:

- **The patterns** (`include/spidy/body_haptics.hpp`, `src/body_haptics.cpp`,
  in `spidy_core`, pure and covered by the core tests). The XR worker posts what
  happened (`Event`: a web shot, a web taking hold, a zip, a landing, a blow
  given or taken, Spider-Sense, slow motion, ...) and every frame what goes on
  (`State`: the body's velocity, each web's tension, a grab's pull, a charged
  leap, slow motion, the health). `Mixer::render` makes every motor's strength
  for one moment: the patterns are code, nothing is designed or deployed in
  bHaptics' portal. The vest's motors carry bHaptics' TactSuit Pro index (front
  0-15, back 16-31, rows from the shoulders, column 0 the wearer's left, from
  the diagram at docs.bhaptics.com/sdk/further/motor); `frontMirrored`,
  `backMirrored` and `visorMirrored` flip a side if the devices say otherwise.
  Directions are taken around the body from the torso's facing (`Facing`: the
  vest is not tracked, so it follows the head once the head turns past 34
  degrees, and turns with the room at once - snap and smooth turns, a wall
  stood on, a flip): a blow, a zip, a wall or Spider-Sense plays on a spot of a
  few motors toward where it comes from, the nearest at full strength
  (`Paint::toward`); the wind meets the motors that face the flight, in free
  flight only (in the air, off any wall, no web holding the player), from 15
  m/s. Strengths are a share of a motor's full strength at 100%: what goes on
  all the time stays under a third and grows from nothing (a web's pull 2% when
  hanging, 23% at the bottom of a fast swing; the wind 0-16%), events play at
  10-45%, the heaviest blows up to 75%. Every kind belongs to a `Group` (webs,
  combat, movement; the wind is its own), which the tab's switches silence.
- **The output** (`include/spidy/bhaptics_output.hpp`,
  `src/bhaptics_output.cpp`, the `spidy_bhaptics` library). bHaptics' C/C++ SDK
  2.7.1 (tact-cpp2, fetched by `tools/bootstrap.ps1`, the library's SHA-256
  pinned): its header names the functions, its `bhaptics_library.dll` is loaded
  at run time from beside the module, by a thread of its own, only once BODY
  HAPTICS is on. Every SDK call waits for the SDK's own worker, so the headset's
  frame loop never calls one. The thread renders the mixer every 40 ms and sends
  each connected device its frame (`playDot`, 100 ms long, a request id for each
  device), stopping the device's last frame first, so one plays at a time:
  whether the Player replaces a playing frame under the same id or plays both
  (and how it would mix them) is not to be seen from outside. Until the second
  build of October 20, 70 ms frames every 40 ms overlapped. A motor under 3% goes
  as off (`minimumLevel`); a device that falls quiet (a menu) is stopped at
  once. It connects with `registryAndInit`, which keeps reconnecting by itself:
  a Player started after VR is found; the Player lists its devices a second or
  so after it connects, so "no device" waits three seconds. Without a Player
  installed it shuts the SDK down and does nothing more; the library stays
  loaded until the game ends (one VR session a process; a library unloaded
  under its own threads is a crash).
- **The game** (`src/game_xr.cpp`): each controller pulse has its body pattern
  beside it; the state goes out once a frame, live only in VR play. Blows the
  player takes come from his health: `game_player`'s watch finds the hero's
  `HeroHealth` (vtable `38a6df0`, a `SegmentedHealth`, a `Health`; hit points a
  float at +0x88, 110 to 105 under a probe's blow of 5,
  `reports/body-probe.json`), the frame loop reads it, and a fall in it is a
  blow from the attack the game warned of last (Spider-Sense's threats, within
  a second), else on the chest and the back at once. Zips play toward the
  zipped webs' ends, walls on the side the swing's wall normal puts them, hand
  climbing's grips in their arm. Landings come from the swing's samples
  (`LandingWatch`: the speed it came down at in the flight's last fifth of a
  second).

**The first session on the devkit** (October 20, a TactSuit Pro and both
TactSleeves; the visor was not paired): 5,483 frames, none refused, but "way
too agressive, directionality didnt really feel like anything, felt like 0 to
100 always with sometimes still going when im standing". The report and the
Player's log showed why: the wind blew 94% of the time in the air (the swing's
speed was 31 m/s at the middle, and it blew from 12 m/s) and along walls too,
most events played at 50-100% on half the vest, frames overlapped, and the vest
faced wherever the head looked. The pause menu itself was quiet: nothing went to
the Player in its 4 s. Reading the Player: its log
(`%LOCALAPPDATA%\com.bhaptics.player\logs`, about a megabyte every 10-30 s while
a game streams) has every `SdkPlayDotMode`, `SdkStopByRequestId` and its active
request ids (each listed twice, a single frame's too), a second's resolution;
its legacy socket (`ws://127.0.0.1:15881/v2/feedbacks`) reports connected
devices but not the SDK's dots, and `isPlayingByRequestId` stuck on after some
streams, so none of them tells what a motor does.

**The bHaptics app.** The Player knows an app by the workspace ID and API key
of an app on the [bHaptics Developer Portal](https://developer.bhaptics.com):
create one, add one event with any pattern (Spidy never plays it, but an app
needs one to deploy), deploy it, and make an API key (its API Key tab). The ID
is the workspace's: on October 20 the app's own record ID got "No workspace"
from bHaptics' server, and the workspace ID worked (the server returned the
deployed app, and the Player sent its event list within a fifth of a second of
connecting). A Player that connects but sends no event list within 5 s did not
take the app: the status says `rejected`. A build takes them from
`SPIDY_BHAPTICS_APP_ID` (the workspace ID) and `SPIDY_BHAPTICS_API_KEY` in the
environment, else from `bhaptics-app.local` beside `CMakeLists.txt`
(git-ignored):

```
app_id=...
api_key=...
```

The release workflow passes the two repository secrets of those names. A build
without them plays no body haptics and says so in the headset; `cmake` prints
which it has. bHaptics calls them no secret (every build carries them), but the
repository is public, so they stay out of it.

**The settings.** The SPIDY VR tab's HAPTICS section: CONTROLLER VIBRATION,
BODY HAPTICS (OFF, 25-100%) and four switches, WEB, WIND, COMBAT and MOVEMENT
HAPTICS (the player, October 20: "one switch per effect group"). BODY HAPTICS
is "Body haptics" in the launcher (`body_haptics` in `launcher.ini`),
`--body-haptics` for `tools/run_game_vr.py`, and rides XrConfig version 24 in
version 23's spare word; the switches are the tab's alone, kept in
`%APPDATA%\Spidy\spidy_vr.ini` (`body_haptics_webs`, `_wind`, `_combat`,
`_movement`) with the other tab-only rows. A session puts a copy of
`bhaptics_library.dll` beside the stereo module it stages
(`reports\stereo-modules\<sha256>\`). The session report's `haptics` (and each
sample's; `SpidyHapticsData` version 2) says what the connection did
(`no_player` on a PC without the Player), the devices on now and at any time,
frames sent, blows felt (and how many by an attacker's direction), landings,
the seconds of VR play with the Player connected and each device's share of
them with something playing and its strongest motor's mean then (`played`), the
switches on (`groups`) and the events by kind (posted, played or not); the
headset says once which devices are on, or why there are none. The library is
unsigned, like Spidy's modules: one more file an antivirus heuristic may look
at.

**Without the game**, `build\windows-ninja\spidy_haptics_test.exe` plays every
pattern through the same mixer and output: `check` (the Player and the devices),
`map` (each part of each device in turn, to say where it is felt; `--step`
waits for Enter), `effects` (each pattern with what it should feel like;
`--only NAME`), `stream` (a steady frame and a ramp: one buzz, or a pulse?).
`--dry` needs no Player and prints the frames as the devices would get them, a
digit a motor in tenths rounded up (1: 3-10%, 9: over 80%), the vest's front
and back row by row from the wearer's left; `--app-id` and `--api-key` try an
app before a build has it.

## Run the lab

Connect Quest 3 to Virtual Desktop on this PC, then double-click
`Launch Spidy Lab.cmd`, or run:

```powershell
.\tools\launch-lab.ps1
```

Use `-Probe` to check the runtime without starting a headset session; it
lists the controller layouts the runtime takes ("Controller layout Touch:
taken"). The default
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
| `src/web_strike.cpp` | Fighting across the room: the web strike that flies the player to the thug a fist is thrown at, the flow's count and finisher, how each blow is delivered (melee, or the kinetic blow that sends him flying), and the hit stop; `game_punch.cpp` runs it and `game_swing.cpp` flies the player |
| `src/spider_sense.cpp`, `src/game_sense.cpp` | Spider-Sense: the game's own warnings of an attack about to land, read from the hero's `HeroDodgeManager` (`game_sense`), what each asks for, the counter a fist thrown at the attacker makes and the blow after it (`spider_sense`); `game_punch.cpp` runs it, `game_xr.cpp` tingles and draws the marks (`web_visual.cpp`'s `appendSense`, `appendSenseSight`) |
| `src/impact_sound.cpp` | The blows' sounds and a strike's rush, synthesized as WAVs in memory |
| `src/body_haptics.cpp`, `src/bhaptics_output.cpp`, `apps/haptics_test.cpp` | Body haptics on a bHaptics vest, sleeves and visor: the patterns (pure, in the core tests), the bHaptics SDK on a thread of its own, and the test tool without the game ([Body haptics](#body-haptics-bhaptics)) |
| `src/shooter.cpp`, `src/game_shooter.cpp` | The web shooter: trigger pulls and their aim, and the game's own web-shooter shot fired from the hand |
| `src/slow_motion.cpp`, `src/game_time.cpp` | Slow motion: the left stick's click, focus and the eased time scale; the game's own time system slowed to it (its look: `D3D12Renderer::Grade`, the meter in `web_visual.cpp`) |
| `src/game_grab.cpp`, `src/native_bodies.cpp`, `src/game_targets.cpp` | Web grab in the game: candidates, freed Havok props, flung bots |
| `src/native_render_memory.cpp`, `tools/vr_launcher.py` | A larger per-frame render memory ring, installed while the game starts |
| `src/web_visual.cpp` | Fallback game-style web strands for the headset overlay and lab |
| `src/prompt_buttons.cpp`, `include/spidy/prompt_buttons.hpp` | The game's own button vocabulary: what each icon id of its nine device families asks of the pad, the letters its prompt texts spell out, which of them VR play can send, what the player does instead, and a prompt text rewritten in those terms. Pure and covered by the core tests; `game_pad.hpp` is built on it |
| `src/game_prompts.cpp`, `tools/probe_prompts.py` | What the game is asking for now: the prompt and QTE interface watched through `game_menu`'s hooks on the UI's call wrapper and text lookup, the live demand the interact button sends, the counters a session report carries; plus the October 11 research exports (for a game with no VR session running) |
| `src/game_offers.cpp`, `src/game_warp.cpp`, `include/spidy/offer_pointing.hpp`, `tools/research_warp.py` | The game's interactions on Spidy's wall: the hero's offer processor, each offer's check and a vent's own check answered as the game's crawl would where the swing holds the player, and the offers it gathered near him; a hand pointing at one and the interact's use of it - the game's own crawl entry under him (`game_warp`), its pick, then its Y; the in-game check (`research_warp.py --use`) |
| `src/game_xr.cpp`, `src/game_tracking.cpp` | Native eye presentation and Quest controls |
| `include/spidy/wall_roll.hpp`, `src/swing.cpp` (`climbInputs`) | Hand wall climbing: palms that hold the wall and pull, the two-grip roll of the view |
| `include/spidy/realistic_flip.hpp`, `tests/realistic_flip_tests.cpp` | Realistic flips: spin from the webs' pull, a let-go swing and arm swings, drag, and the way back upright |
| `src/native_hud.cpp`, `include/spidy/native_hud.hpp`, `tools/probe_hud.py` | The game's HUD in VR: its panel in front of the head, the second movie (markers, subtitles, prompts) on it, and its check in the game |
| `src/native_rays.cpp`, `src/native_movement.cpp`, `src/game_swing.cpp` | Native world queries and collision-controlled swing requests |
| `src/lab_world.cpp`, `apps/xr_lab.cpp` | Synthetic collision world and VR lab |
| `tools/inspect_game.py`, `tools/discover_render_types.py` | Offline game research |
| `src/game_observer.cpp`, `tools/observe_game.py` | Opt-in native camera callback diagnostic |
| `tools/game_driver.mjs` | Observed game launch, screenshots, and input workflow |
| `tools/vr_display.py` | Small desktop window, no frame generation, no vertical sync, no ray-traced reflections, Aspect Ratio at Auto and no Windows.Gaming.Input for VR launches; restores the game's settings |
| `src/stereo_probe.cpp`, `src/native_eye_gpu.cpp`, `include/spidy/eye_resolution.hpp` | The eyes' scene views: their buffers, lenses, anti-aliasing and upscaling, exposure; the game's own view at the head and its rest; the staged eye pairs and the monitor's left eye |
| `tools/probe_vr_load.py`, `tools/probe_render_commands.py`, `tools/probe_exposure.py` | The eyes' cost and picture in the game without a headset: frame rate, GPU time by view, render commands by view, and both eyes' brightness |
| `tools/xr_runtime.py` | The OpenXR runtimes, and the one a headset is connected to (`--detect`) |
| `docs/MILESTONE-1.md` | In-game acceptance criteria and remaining integration work |
| `docs/REFERENCE.md` | Reference mechanics, executable evidence, source attribution |

The bounded adapter connects `spidy_openxr` to the game's D3D12 device and direct
queue, then copies matching native eyes after their submitted GPU markers.
See [milestone details](MILESTONE-1.md) for its remaining validation.
