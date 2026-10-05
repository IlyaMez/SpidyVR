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
`-AttachOnly` to launch/wait automatically. Size zero uses recommended dimensions;
seconds zero uses a heartbeat-controlled session until game exit or Ctrl+C.
For validation beyond the old timeout, `run_game_vr.py --auto-launch --stop-after 45`
exercises the untimed worker with a bounded external stop.

The launcher checks headset availability before starting the game or injecting.
It starts the game but does not select a save or drive menus. When Codex operates
the game, use the observed UI driver for those
steps and close the game promptly after the prepared test. Keep it closed during
builds and research. A sandboxed headset failure is inconclusive; the preflight
must run with access to the desktop runtime before declaring the headset absent.

The worker waits for an accepted native eye pair before enabling movement, stops
active controls after a 250 ms presentation lapse, and aborts a three-second
image stall. The native mover controls collision; requests expire if the swing
or XR worker stops renewing them. Owned movement synchronizes the native airborne
velocity and clears the mover's duplicate fall accumulator. This correction
still needs an in-game handoff test. Reports now include native movement samples
with gravity-correction and airborne-event counters.

Grip plus trigger attaches a hand's web. Release grip to let go, or release and
press trigger again while gripping to reel. That press takes up existing slack
immediately; reeling runs at 16 m/s. Physical yanks use tracked sample
timestamps. Left stick steers, right stick snap-turns, and A jumps. The landing
window for point launch is core-tested; the combined in-game headset controls
remain unverified. Stereo/head movement and visible hands have user confirmation.
Left Y retains its lab-only reset behavior.

The test report rejects intermediate XR/swing faults, incomplete GPU work, failed
stop calls, and any checked game entry left patched. Its success flag validates
these recorded checks; it does not establish visual quality, comfort, or completion
of the milestone. `-CaptureImages` enables native eye PNGs captured before the
hand overlay; image readback is otherwise disabled to avoid its per-frame cost.

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
recommended dimensions. Native and OpenXR image checks now allow up
to 4096 pixels per dimension, subject to runtime limits.

The 1536-square test `game-vr-20261004-133248.json` confirmed VSync Off but still
spent 29.76 ms of its 31.92 ms mean frame in the matching-pair wait. The staged
handoff subsequently passed `game-vr-20261004-135733.json` at the same resolution:
62.38 fresh pairs/second, 70.93 submissions/second including reuse, and 0.089 ms
in the copy call. The user reported much better frame rate. Mean `xrEndFrame`
time is now 13.43 ms; further profiling must separate rendering and runtime costs.
Its native-view lease includes initialization margin so it will not expire
ahead of the worker. The worker publishes a stopping state before normal cleanup,
and rate analysis excludes frozen cleanup samples while retaining active drops.
