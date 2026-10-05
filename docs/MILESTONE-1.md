# Milestone 1 — native stereo, tracked controllers, physical swinging

Target: Spider-Man Remastered **Steam 4.0630.0.0**, Quest 3 with Virtual Desktop.

## Completion criteria

This milestone is complete only when all of these work **inside Spider-Man**:

- Head position and orientation control the view at 1:1 scale.
- Each eye sees a separately rendered scene with its OpenXR pose and asymmetric
  projection, using the same game simulation snapshot and predicted display time.
- Each controller aims its own web at verified world collision. Both hands can
  attach, release, reel, and perform deliberate physical yanks independently.
- Swinging obeys rope length, gravity, collision, and momentum on release.
- A zip followed by landing and a timely jump produces a point launch.
- Tracking loss, menus, cutscenes, death, loading, and device loss relinquish
  control cleanly. Controller/head tracking survives recentering.
- The Quest test confirms stereo scale, low latency, reliable buttons, and
  acceptable frame timing. Offline tests cannot establish comfort or game compatibility.

**The in-game milestone is not complete.** The standalone VR lab works. Native
game stereo has passed GPU capture tests, and one native web hold/release/landing
sequence passed a bounded flatscreen test. The corrected headset run submitted
509 eye pairs, and the user confirmed stereo, head movement, and visible hands.
Resolution and performance were poor. Full-speed traversal, physical headset
web controls, and lifecycle behavior remain open.

## Implemented

- Portable C++20 physics and input core, operating in meters and seconds.
- Fixed 120 Hz physics, swept sphere collision in the lab, unilateral rope
  constraints, independent hands, maximum range, occlusion/unload release.
- Grip to hold, trigger-plus-grip to attach, deliberate yank-to-zip with speed
  and distance thresholds, reeling, and timed point launches.
- OpenXR instance/session lifecycle, predicted poses, per-eye swapchains, input
  actions, Touch/Index binding suggestions, haptic events, and focus checks.
- D3D12 scene rendering, asymmetric eye projections, depth testing, and GPU
  fences before image release. Both eyes share one simulation update.
- An OpenXR library that can bind to an existing game's D3D12 device and direct
  queue, validating its adapter and feature level against runtime requirements.
- Snap turning around the head, head-relative yank samples, and lab recenter handling.
- Offline executable inspection and camera/render RTTI discovery tools.
- A bounded, read-only live object capture tool, exercised in free roam across
  two game launches. It finds the player/camera pair and a plausible camera lens.
- An opt-in camera-update observer DLL. A 30-second test captured 3,362 valid
  observations on one callback thread, including controlled camera rotation.
  The hook was disabled and the original entry bytes restored after capture.
- A game test driver for launch/restart, menus, screenshots, and mouse camera
  control. Native input-query injection now produces a verified in-game jump.
- Expiring camera translation, orientation, and asymmetric lens controls through
  the game's own camera/render descriptor routines, with hook restoration.
- Two native offscreen eye views with distinct GPU allocations. Retaining excluded
  views fixed the immediate deletion failure in the tested idle/reuse cases.
  Eye pixels show native parallax. A moving-view GPU test captured 338 matching
  pairs after their actual submitted timestamp commands and completed the copy fence.
- A shared game tracking mapper that keeps head, hands, and eyes at one predicted
  display time and handles recentering, focus loss, snap turns, and native key mapping.
  The bounded OpenXR worker now connects this mapper to native eyes and the
  controller key bridge. Live head/controller samples were received in the first
  headset run, but that run submitted zero images.
- Continuous native collision queries validate fixed body IDs and invalidate
  expired commands. The swing solver runs inside a native query lease, sends
  expiring movement requests, and uses the game's achieved position/velocity on
  every physics step. The native mover remains responsible for body collision.
- Released flight stays under the solver until landing, avoiding the known bad
  handoff to the game's accumulated airborne velocity. A 61-step native test
  verified attachment, hold, release, flight, landing, and hook restoration.
- Quest trigger/grip, steering, and jump connect to that solver after successful
  eye submission. New controller samples have their own time interval, so faster
  physics callbacks do not reset a physical yank. Presentation stalls gate input.
- Articulated procedural gloves and web lines render over the copied native eyes.
  GPU tests verify the overlay preserves scene pixels and source textures. It
  uses its own depth buffer; world occlusion and native skeleton binding remain open.
- Native prediction retains the post-zip landing window for point launches, with
  a regression test for direction lost on ground contact. This change is not yet
  verified in a live native point launch.
- User-confirmed Quest 3 / Virtual Desktop lab smoke test. Detailed feature and
  performance checks remain open; this does not validate the game integration.

## Game integration work remaining

1. **Extend the camera observation to lifecycle changes.** The candidate update
   function now has live callback evidence in free roam. Establish object lifetime
   across menu/cutscene/loading transitions and a stable simulation frame identifier.
   A callback count is not yet a verified simulation or render frame identifier.
2. **Validate the native views during motion.** Native eye views, asymmetric
   projection, active lighting, and 338 matching GPU pairs now have live evidence.
   Check culling, separate depth and temporal histories, motion vectors, and
   frame generation interaction during traversal.
3. **Validate OpenXR presentation.** The adapter uses the game's device/queue and
   copies only matching native eye generations after GPU completion. Test the
   compiled path in the headset, confirm both eyes use the same simulation state,
   and validate shadows, transparency, HUD, and post-processing.
4. **Validate native swinging beyond the bounded test.** The solver now connects
   queries and native movement. Exercise two simultaneous anchors, physical yanks,
   reeling, point launches, wall collisions, and higher speeds. Released flight
   remains controlled until landing; unexpected midair cancellation still needs
   a clean native momentum handoff.
5. **Tracked body presentation.** Bind hands to the player skeleton, handle
   first-person head visibility, render web endpoints at the wrists, and add
   room-scale body/head collision and world occlusion. Current hands are original
   procedural gloves rendered as an overlay.
6. **Lifecycle and headset tests.** Validate real tracking, haptics, recentering,
   save/load, respawn, streaming anchors, menus, cutscenes, device loss, and
   performance in the actual game. The user has confirmed the standalone lab
   works on Quest 3 / Virtual Desktop and confirmed the in-game stereo/head/hands
   smoke test. Physical web controls and performance still need their own checks.

Moving anchors, rope wrapping, wall traversal, combat, gadgets, finger tracking,
and optional flips are later work. The current camera keeps a level horizon;
head rotation is always preserved, and body animation does not rotate the view.

## Rendering contract

The normal launcher now starts the Steam game, skips its launcher screen, waits
for a loaded save, and attaches an untimed VR worker. Eye dimensions default to
the active runtime recommendation. Both thumbstick clicks switch between native
stereo and the stock camera on a flat quad inside the headset. The shortcut needs
a full release between presses and disarms on focus loss. These additions pass
local build/input/GPU checks; the user deferred their combined headset test.

The launcher renews the worker heartbeat. Normal stop or heartbeat loss retires
native views, stops world/movement work, and restores the input bridge. Movement
and pose commands retain their shorter leases. Timed diagnostics remain separate.
Full save/load and respawn recovery is still pending.

The game adapter runs OpenXR on its own thread. `frameStereo` reads tracking
once, then acquires/waits both eye targets before its paired `draw` callback.
Preparation publishes one expiring pose pair and records its tracking-space poses,
world-space camera/hand poses, and web state. Matching native render generations
are copied into staging textures on the game's direct queue. The draw callback
copies the newest staged pair without waiting for another native render. Both
eyes share a serial and generation. Missing/stale frames submit zero layers.
The lab's `frame` wrapper still draws each eye separately. XR color
images must leave the callback in `D3D12_RESOURCE_STATE_RENDER_TARGET`.

The copy accepts the compatible RGBA8 typeless backing used by VDXR. Movement
requires an accepted image within the preceding 250 ms, and a three-second
presentation stall ends the test. Hand/web drawing preserves the copied scene
color. Both eyes now use one overlay command list and upload. All image work is
submitted on the exact direct queue bound to OpenXR before image release;
queue ordering supplies the runtime dependency. CPU waits remain where upload
memory and overlay command allocators are reused, and at final cleanup/readback.
Native capture and presentation each have three command allocator slots; a busy
pool skips the attempt instead of blocking. Shared staging texture writes and
paired reads are ordered by the same queue and submission lock.

These state and copy requirements follow the
[OpenXR D3D12 image contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_D3D12_enable-swapchain-image-state.html)
and [D3D12 CopyResource restrictions](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copyresource).

Projection layer poses remain in OpenXR tracking space and come from the exact
pose serial used to render the submitted image, including reused images. Cached
metadata expires after 150 ms and is cleared on focus loss or recentering. Both
the native scene and overlay use the saved world-space poses. World locomotion is
applied to the camera used by the renderer. Applying it again to the submitted
tracking-space layer would cause incorrect reprojection.

The lab keeps synchronous rendering by default. The game uses asynchronous paired
overlays, nonblocking staged copies, and no CPU image readback by default.
Stage timing separates runtime pacing, input preparation, native-copy duration,
overlay submission, acquisition/release, and frame submission. The historical
`copy_wait` metric now measures the nonblocking staged copy call. Reports include
new native scene pairs/second separately from total XR submissions and reuse.
The next headset test must establish the new handoff's performance.
