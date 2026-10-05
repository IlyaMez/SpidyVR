# Validation — 2026-10-05

## Small desktop window for VR launches — current build, awaiting headset check

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
