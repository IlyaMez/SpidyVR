# Optional recording FOV

Pause, open Settings → SPIDY VR → DISPLAY → RECORDING FOV.
OFF is the default: the existing left-eye mirror, including Spidy's drawings,
and the existing resting active-view path are unchanged.

Selecting 80–150 degrees renders the game's active view again for recording.
This costs frame time. Spidy's eye-only combat and aim overlays do not appear
in that window, although they remain in the headset. Menus and game-screen
presentation retain their existing path.

The selected angle is a minimum, not a crop: the recording lens expands to
contain both eye frusta, including their full height, while preserving the
window aspect ratio. A requested 80 degrees may therefore produce a much
wider image on a tall headset. The headset eye projections are unchanged.
The existing shadow-covering lens uses that same safe covering volume.

The setting is stored as `spectator_fov` in the existing
`%APPDATA%\Spidy\spidy_vr.ini`, through `tabOnlyText` and `withTabOnly`.
The render thread reads an atomic value; it never opens or polls an INI file.
Old per-build `SpidySpectator.ini` files are not used.

## Review validation

- Automated tests cover OFF defaults, the nine menu choices, persistence, and
  containment of asymmetric eye bounds at several monitor aspect ratios.
- Windows Release build: 423/423 core tests and 138 Python protocol tests passed.
- Played in the headset by iplusd on October 21, 2026, using the isolated
  review package built from upstream 422b22c plus this revision. The loaded
  stereo module SHA-256 was
  `d5d44d379ac1970a71a55ace26456f238f38e86a1a09a6a527d0b43556c12021`.
- Same scene, stationary OFF → 130 → OFF comparison, approximately 30 seconds
  per interval. Eye resolution reported by XR telemetry: 3032 × 3248 per eye.
  Counts below are newly rendered scene pairs, excluding reused XR images.

| Setting | New scene frames/s | Mean scene interval |
| --- | ---: | ---: |
| OFF, first | 117.11 | 8.54 ms |
| 130 | 84.50 | 11.83 ms |
| OFF, repeat | 112.34 | 8.90 ms |

The extra interval with recording enabled was 2.93–3.30 ms in this session.
These are wall-clock scene cadence measurements, not isolated GPU render
duration or a general performance guarantee. Frame pacing, headset refresh
and scene variation affect them. Background OpenVR compositor counters were
also collected but are not attributed to this game's GPU work here.
- iplusd reported no new missing objects at the headset edges or distant
  building artifacts while looking around with 130 enabled.
- The in-game change was observed in `%APPDATA%\Spidy\spidy_vr.ini` as
  `spectator_fov=130`; the setting's read/write round-trip is automated-tested.
  Persistence across an actual restart was not separately tested.

For the live comparison, use the same save, spot, look direction, graphics
settings, eye resolution and runtime refresh rate. Let loading settle, then
collect OFF, ON (130), OFF intervals of at least 30 seconds each. Record
SteamVR CPU/GPU frame times, including reprojection status, as well as newly
rendered scene rate. An FPS cap can hide additional GPU cost, so FPS alone
is insufficient. Inspect the headset's top/bottom and left/right edges,
distant buildings, menus, overlays, and the return to OFF. Restart once to
check persistence. Do not compare changing scenes or menu time as gameplay.
