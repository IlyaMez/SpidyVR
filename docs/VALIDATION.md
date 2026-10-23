# Validation — 2026-10-21

## Better Climbing — 2026-10-23

Complete Windows Release build with OpenXR, observer/game modules and launcher.
Core: 430 checks passed, including retained climbing progress, another grip
and pull, no zip impulse while climbing, repeated input, tracking gaps,
switching off, persistence and legacy settings packets. All three CTest
targets and the 138 Python protocol checks passed. Not tested in the live
game or a headset; repeated strokes and the feel of the handover need that check.

## Every SPIDY VR setting in the launcher (latest build)

The user: "why doesnt the new launcher include all the realstic flips stuff?",
then "yeah lets add all". `apps/launcher/ui.cpp` (`options()`: `tabCombo`,
`tabToggle`, the Walls and Flips cards, Reset options).

| | |
| --- | --- |
| Build | The launcher and its tests in a scratch folder (subst drive, `-DSPIDY_BUILD_XR=OFF -DSPIDY_BUILD_LAUNCHER=ON`): no warnings. `build\windows-ninja` not rebuilt. |
| Launcher tests | 30 passed. |
| Looked at | PrintWindow shots at 150 % of a scratch copy beside the user's own launcher (lock and window class renamed, off the screen's edge but one column): the Options tab top to bottom; each new row showing the user's `spidy_vr.ini` (hand wall climbing on, 40 cm, near clip 2 cm, UI style Comic, swing momentum off, hand momentum 75 %, angular drag Very slow, minimum hand speed 3 m/s); the haptics switches indented under Body haptics. |
| Writes | Realistic flips switched on: `realistic_flips=1` in `spidy_vr.ini` and its nine rows under it. Angular drag's list showed Fast, Quick, Medium, Slow, Very slow; Medium wrote `angular_drag_ms=1500`. Reset options wrote every `spidy_vr.ini` key's default. |
| Settings | launcher.ini and `spidy_vr.ini` hash-identical to before after each run; no profile written. |
| Not run | A VR session started after changing them in the launcher; another Windows scaling than 150 %. |

## Launcher facelift (the build before)

The user: "lets give the launcher a facelift with out style, better layout and
UX", "we have a control map visual, maybe use it?", "i feel like the ui is a
bit much with the colors and contrast". `apps/launcher` (`ui.cpp` drawn anew,
fonts and the controls picture compiled in).

| | |
| --- | --- |
| Build | The launcher and its tests in a scratch folder (subst drive, `-DSPIDY_BUILD_XR=OFF -DSPIDY_BUILD_LAUNCHER=ON`): `/W4` clean. `build\windows-ninja` not rebuilt. |
| Launcher tests | 30 passed, with the UI STYLE row of the build before. |
| Looked at | PrintWindow shots at 150 % of a scratch copy run beside the user's own launcher (its single-instance lock and window class renamed in the copy; the window off the screen's edge but one column, so it renders and covers nothing): Play, Options with the profile menu open and the Save as a profile dialog, Controls (the picture drawn pixel for pixel), About; the smallest window (980 x 700) with the shortcut bar. |
| States | A scratch copy of `ui.cpp` with the session faked from an environment variable (not in the tree): in VR, stopping with Force stop, ended, failed with code 3, needs administrator, close the game first, could not start, not ready, checking, the update offer. In the smallest window a failure shows its error, its offers and START VR, without the steps. |
| Picture | `docs/images/readme/controls-quest3.jpg` re-shot from its page at 3840 x 2160 and scaled to 1920: issue box PC VR, the red box "Press that one. Its LT, LB, RT, RB: your triggers." Every other binding on it checked against PLAYERS.md. |
| Glyphs | Barlow and Comic Neue hold every character the launcher prints but the arrow of a profile's changes, which Segoe UI (merged) draws. |
| Settings | launcher.ini and `spidy_vr.ini` hash-identical before and after; no profile written. |
| Not run | A VR session; Windows scaling other than 150 %; Windows 10 (Segoe MDL2's icons); a real update; tooltips and hovers (posted mouse moves cannot hold a hover). |

## Tech UI style

The user: "lets make an optional alternative set of our custom ingame vr ui in
more scifi techy style like the based game ui". UI STYLE in SPIDY VR (COMIC or
TECH); the marks in `src/tech_marks.cpp`.

| | |
| --- | --- |
| Core tests | 426 (4 new): every mark (aim marker, meter, blow, call, knockout, Spider-Sense and its sightline, wheel, panel) draws as before with no style, the same as COMIC, and anew as TECH; tech blows white, amber, red with amber, called cyan, the count's callout with COMBO in cyan, springing out and no pose held, faded by 0.72 s and gone at 0.76; the call's ring of 24 segments running down a segment at a time, its chevrons from outside the ring on the fist's side to his middle for all four ways, the finisher amber in red and larger, snapping on from wider; the knockout red with KO and its amber bar, 5-30 cm at 1.2 m, gone by its time; Spider-Sense cyan, red, amber, over his head, its ring closing, amber only at the last notch, waving, flickering on, a counter throwing it out, 4-12 degrees across at 1.5-9 m; the sightline mark 14-21 degrees off the look, the rim's arcs to 38-45, right of the view, upward for an attacker above; the wheel within 30 cm, a cyan wedge and the name in the middle when lit, the equipped one amber, at most 60,000 vertices fully charged; the meter's cyan segments, red on a refusal, a sweep in slow motion, nothing hidden, over the wrist; the panel translucent blue, nothing behind it, the hint amber, the hold cyan. The aim markers' test runs for both styles (size on screen, squeeze, shadows first, edges fading, each hand's colour, fading, a round target ring closing in, the right hand's wider). The tab's sections test with UI STYLE after the HUD; `ui_style` in `spidy_vr.ini`, read back and clamped; the choice round trip. |
| Launcher tests | 30: profiles hold `ui_style` (tab-only, 0-1), the defaults' text with `ui_style=0` after `hud=2`, the change list's "UI STYLE COMIC → TECH". |
| Protocol suites | Display permission, game VR, launcher, observer, runtime, stereo: all OK. No protocol change. |
| GPU | `spidy_graphics_test` at 1536x1536: all passes, D3D12 debug layer clean. |
| Build | Scratch build (subst drive) of every target but the launcher, whose `ui.cpp` was mid-change in a peer session; `/W4` clean in the new code. |
| Cost | Vertices and build time per mark, comic → tech: wheel 45,582 → 22,020 (137 → 67 µs), blow with count 6,210 → 2,664, knockout 9,054 → 2,688, target ring 2,436 → 5,517, Spider-Sense 6,804 → 9,936, meter 3,504 → 7,686, calibration panel 3,996 → 24,168 (21 → 74 µs). |
| Looked at | `tools\preview-marks.ps1 -Style Tech` and `-Style Comic` over the October 20 session's captures 0011 (street, 6 m) and 0076 (rooftops, 0.75 m): every sheet (marks, blows, sense, sense-eye, aim, wheel, meter, panel). |
| Not run | The game and a headset; `tools/probe_menu.py` (one more Down for the new row). |

## Settings profiles

The user: "lets add spidy settings profiles so people can easily import and
export and share them". The launcher's Profile row; the format in
`launcher_profile.hpp`.

| | |
| --- | --- |
| Launcher tests | 30 (new 6): every SPIDY VR row but VR VIEW and CALIBRATE BODY held once, `spidy_vr.ini`'s 17 keys all held and nothing else of the tab's own, wall running the one launcher option, keys unique, the tab-only ranges `vr_settings::sanitized`'s; the defaults' exact text (33 settings, in the tab's order); every setting away from its default read back, graphics and calibration untouched; a Discord message with a quoted name, `key = value`, a quoted line, `hand_climb=on` and a `flips: yes` passed over (3 settings, the rest unchanged), a session's line (4 of its 7), launcher.ini with a BOM (`gravity_percent` alone), switch words and words apart by `,` `;` and code marks, `hud=2.5` passed over, clamping, an out-of-range number passed over, texts with no setting, a 40-character name; the change list in the tab's titles and choices (`90°/S`, `1 CM`, `QUICK`); file names (refused characters, CON/COM1/LPT9, Hebrew and an emoji kept) |
| Planted bugs | Built from a copy of `include/`, one at a time: two keys swapped, a quote's `>` kept, no clamping, device names allowed, HAND WALL CLIMBING left out, WALL CAMERA DISTANCE's range cut to 75, the nearest choice broken. Each failed at least one test (the quote only after the test's name line was quoted) |
| The launcher, driven | `build\windows-ninja\spidy_launcher.exe` on this PC (PrintWindow shots, posted clicks and keys), launcher.ini and `spidy_vr.ini` backed up first. Save as a profile ("Claude test: mine" saved as `Claude test mine.spidy`, its `hud=1`, `hand_climb=1`, `hand_momentum=75` the player's own); Aim markers switched: the row "Claude test mine*" and "Changed since you used..."; the profile's tooltip "AIM MARKERS OFF → ON"; Copy to share: the clipboard held the code block, `aim_markers=0`, no name; Paste of an 8-setting Discord message: the preview listed the 8 and "It has 8 of the 33"; Save and use: launcher.ini changed in those (`arcade_webs=1`, `swing_speed=56`, `gravity_percent=150`, `snap_turn=0`, `smooth_turn=90`), `spidy_vr.ini` in `near_clip_mm`, `realistic_flips`, `rotation_strength` only, `Previous settings.spidy` written; a posted WM_DROPFILES of `Seated comfort.spidy` (no name line, `wall_run=off`) opened the preview named after the file; Save only left launcher.ini's hash the same; Previous settings swapped back and forth (after a fix: "Back to your previous settings...", no `*`); Export and Import through Windows' dialogs (a `.txt` holding the message, "You have a profile of that name"); Delete: in the Recycle Bin; a clipboard with no profile: "The clipboard holds no Spidy profile." in amber |
| Put back | launcher.ini and `spidy_vr.ini` from the backup, SHA-256 `E00279B7...` and `6156FCE4...` as before; the profiles folder (new) removed, the deleted test profile out of the Recycle Bin, the clipboard's text restored |
| Other suites | Core 422, realistic flips, all six Python suites (138). No struct changed |

**Not checked.** A VR session that starts after a profile was used, its
stereo module reading the `spidy_vr.ini` the launcher wrote. The text is
`vr_settings::tabOnlyText`, the module's own writer, and core tests read it
back.

## The game's prompts read again, and pressed by name

The user, about a helper's 50-item playthrough (October 14-19): "try to come
up with a unified immersive fix", then "build it". Its first part: the
buttons the game names reach it from the VR controls of the same names, and
the prompt reading that names them is on again.

| | |
| --- | --- |
| The reading's death, from the code | `keepLine` stamped each prompt call's line (`formatCall`, up to `callChars` - 1 = 127 characters, a text cut where the line ends) with `sprintf_s("%6llu %s")` into a buffer of `callChars`: a line of 121 characters or more overflowed it, and `sprintf_s` ends the process for that (the CRT's invalid-parameter handler), with no exception for a guard to catch and no crash dump. Fixed with `_snprintf_s(..., _TRUNCATE)` straight into the ring. `read` now asks `VirtualQuery` (`readable`) before it touches the game's memory |
| The control, in the game | 0.4.0's `spidy_stereo_probe.dll` (the play folder's), the reading on from the title (`SpidyMenuStart` + `SpidyPromptsWatch`), A every 2 s: dead right after CONTINUE, twice. Run 2: the reading on at 28.4 s, the process gone at 64.5 s; the last reading a second before held 12 UI calls of the title and no prompt call; the game's log ends `[Save] Request load save type 0` (08:12:20), `[Save] Request save type 2` (08:12:22). Run 1 the same, its log ending 08:10:20 / 08:10:22. Steam had refused the first launch (`WaitingPrevProcess`, `AppError_16`): it was still holding the previous day's game |
| This build, in the game | The same run: the reading on at 26.0 s, the player at 68.1 s, free roam watched to 188.5 s, the game alive. 38 UI calls, 7 prompt calls (`ShowText` 3, `HideText` 4). The ring's lines: `ShowText(7, 'Press [STICK_LEFT_PRESS] to <span class='emphasis'>dive</span> and ...` 93 ms after the load (127 characters, cut at the ring's width), then `HideText(7, 0)` at 6453 ms, and two police dispatch lines on channel 1 (123 and 112 characters) |
| Saves | The save folder backed up before, compared after: all 17 files byte-identical |
| Core tests | 365 (new 5): `NamedPress` sends each named button from the control of its name and keeps it until let go; a press begun before the prompt stays the player's; the web-ball buttons as LT or LB and RT or RB, their travel, a gesture's trigger; the right stick's ways, decisive only; out of play nothing; bad readings press nothing. Fists: a thrown fist is X, a hold's held, X + A thrown down in the air both, a level throw in the air neither; not with nothing named, not from a hand on its web, not pulled back, not slow, once a blow. Every button the game can name is sent in VR play. The help lines as measured (the dive's `[STICK_LEFT_PRESS]`, a subtitle naming nothing, the leaks, the vaccine, the camera, the vent takedown, the Ground Strike, the gadget select). What stays live: a call's demand 2 s after its last call or until its clear, a channel's text until its own HideText (not another channel's), replaced by the next text, 60 s at most |
| Planted bugs | X sending Y, the recoil's sign flipped and `[STICK_LEFT_PRESS]` removed, together: 4 tests failed, each on its own message; the files restored and checked by hash |
| Protocol | All six Python suites; no struct changed |

**A third run, the final build** (the user: "run however many times you
need"): alive again through CONTINUE (the player at 69.6 s) and two minutes
of free roam. The load's help line was another this time, `ShowText(7,
'While in the air, <span class='emphasis'>hold</span> [ACTION_WEB_YANK_BOT]
to yank enemies up to you. ...')`: a third kind of word, an action of the
game's own binding configs (the exe holds their names - HeroUsabilityBindings
Config's Fire, Swing, WebZip, WebYankBot, WebStrike, AimMode, UseGadget,
Heal and the rest - not their buttons). It named nothing (`named` 0), as it
should until the configs are read. Game and Steam closed after each run.

**A help line read live** (the final build with `LiveDemands`, a fresh game
until a load's tip was in words Spidy reads; the first was): `ShowText(7,
'Press [BTN_X] then [BTN_B] to perform the <span class='emphasis'>Dodge
Under</span> move on enemies with sh...')` 94 ms into the ring, `HideText(7,
0)` at 6579 ms. Polled every second, the live demand read `0x6000` - the
pad's X and B - from 64.0 s to 69.6 s and 0 from 71.0 s on: live for as long
as the line was shown, where the old rule kept it 2 s. `named` 1,
`beyond_play` 1 (X is no button VR play had). The player at 71.0 s, free
roam to 161.3 s, alive; game closed, Steam shut down, the 17 save files
identical.

**Not checked.** Everything a headset needs: `NamedPress` runs in the XR
worker, so the presses, the fists and the swing letting the game have its
move (AirYield) on a named press in flight have not run.

## Spider-Sense and the counter (October 18 evening build)

The user: "design a vr version of the alert/spidey senses for combat (make it
arkham shadow style)". Arkham Shadow, as its coverage tells it: a blue icon
over an enemy about to strike, "an icon in your sightline" for one off
screen, a hand thrown toward it counters ("Batman instantly abandons his
current target in favor of countering the incoming punch"), a red one is
dodged (Road to VR's hands-on, Destructoid's, Way Too Many Games' and
UploadVR's reviews; UploadVR names its "Combat widget", blue or yellow).

| | |
| --- | --- |
| Where a warning is | Read from Spider-Man.exe offline (docs/REFERENCE.md's table): each attack registers with the hero's `HeroDodgeManager` (16 slots of 0xa8 from +0x78, count +0xaf8): attacker handle +0x00, warn time +0x30, flags +0x5c, state +0x60 (0x10 warned), blow +0x80, on the game clock `0x7a7fbd0`; the warning (`0x926580`) plays the hero's Spider-Sense sound and sets 0x10. The flags' names and values from the game's reflection table `0x5df3d80` |
| Reading it | `game_sense::read`: the manager by its type on the hero's actor record (the game's own lookup), kept while its vtable and owner hold; the attacks and the clock copied under SEH; an attacker's handle resolved as `0x15a0560` does, placed by the target watch's bot (feet, capsule) or his record's transform |
| Core tests | 350 (new 5): what each set of flags asks for, a warning's standing and its notched time left; a fist thrown at a warned attacker counters once after its 30 ms, his next blow is the counter's, a slow fist / one thrown away / one at a dodge's attacker / a hand at the chest counter nothing, a counter in the last 0.2 s is perfect, a blow landing in a warning counters by itself, a counter's blow waits 1.2 s at most; the sightline's 31 and 26 degrees and the side kept behind; the printed mark's plates, keyline, colours, ring closing, last notch, pop-in, break and its 4-12 degrees at 1.5 to 9 m; the sightline mark at 21 degrees with its arrowhead and the rim's lines to 45 |
| Protocol | all six Python suites: XrData 39 (1176), `sense_snapshot` (632 bytes) decoded, a torn read refused, another build's struct reported as `protocol_mismatch` without ending the session; the cross-check finds `game_sense::Data` and its reader agree |
| Modules | `spidy_ray_bridge.dll` loaded outside the game: `SpidySenseSample` 0 and the struct's magic, version 1, 632; `spidy_stereo_probe.dll`'s `SpidyXrData` 39, 1176 |
| The marks seen | `tools\preview-marks.ps1 -Scale 4` over `game-vr-20261012-140623-eyes\0013-view.png` (a thug 2.6 m off, chest 337,262) and `0027-view.png` (0.75 m, 300,400): `far-sense.png` / `near-sense.png` (landing poses, the ring at 100, 50 and the last notch, dodge, jump, no time set, boiling, the three break poses) and `*-sense-eye.png` (a counter over him, a dodge to the right and a counter up to the left in the sightline). The first sheets put the ring across his face and the sightline mark on his belt: the ring now starts at 1.57 of the lines' size, the mark stands 0.14 m + half its size over his head, and the sightline mark sits at 21 degrees, not 17 |

**In the game, no headset** (with the user's OK; `tools/probe_menu_pad.py
start`, `pad a --until-player`: profile 1's Continue, free roam on a roof at
(-554, 51, 1491), player at 35 s; then `tools/probe_sense.py --seconds 20`;
`reports/sense-probe.json`): the hero's `HeroDodgeManager` found by its type
and kept (`managers` 1, at `0x19aa9170fa0`), read at 409 of 409 input samples
with no fault, holding 0 attacks; the game's clock at `0x7a7fbd0` ran from
80.85 to 100.80 in 18.9 s of real time (1.06). Modules stopped, every hook
entry restored, game closed, Steam shut down, the 17 save files identical to
the backup taken before. **No enemy was near** (`probe_combat.py --list`: 0
bots; a fresh free-roam session has none until crimes spawn), so no warning
was seen.

**Not checked.** That the attacks the manager holds are what the disassembly
says, which attacks the game marks counterable or jumpable, how long its
warnings are, that the game's flinch breaks an attack in time and the player
takes no blow, and how the marks, the tingle and the counter feel. They need
a fight: `tools/probe_sense.py --provoke --counter` in a fresh game with an
enemy within 40 m (it punches him to start the fight, throws a scripted
counter at each blue warning and reads the player's health across its blow),
or a headset session, whose report's `sense` census reads them in play.

## The zip out of the game's aim; a printed gadget wheel (the build before)

The user, after a session on the build below
(`dist\Spidy-0.3.4\reports\game-vr-20261018-140811.json`, 7 minutes):
"lets make it so that when holding up (zip) and it enters the slowmo aiming
mode, on release we will zip also lets make the gadget selection screen look
like other better designed ingame ui".

| | |
| --- | --- |
| The zips in that session | 37 sent (XR `zips`). Six in the air with the swing holding the player were taken by the game: the swing let go 0.37-0.42 s after the zip went out (`game_swing::AirYield`), and the player rose up to 14 m or travelled up to 41 m in 1.5 s |
| The slow-downs | the game's own time (`slow_motion.world_scale`, Spidy's slow motion off: `wanted` 1) went to 0.2 eight times, each 0.56-0.64 s after a zip went out: for 3.0, 1.7, 1.5, 1.3, 3.0, 1.3 and 0.8 s, and once for one sample (the stick let go then). No zip followed any of them; in the air the game's air handler ran through them (`air_events` advancing every step) and the swing kept the player |
| The game's aim mode | the exe's strings: "Lock out the standard aim mode functionality (L2) while the photo gadget is equipped", "If true, we'll snap to zip-to-perch targets", "How long to delay at the start of aim-mode before triggering time-scale", "How long in real time to allow player driven time-scale to persist", "How long between player driven time-scale requests"; RTTI `HeroStateAimModeIdle` `0x38d1cf8`, `HeroStateAirFire` `0x38c14a8` (its table holds the air handler `0xa7b3a0`, as the jump and fall states' do) |
| Core tests | 345 (new: `ZipPress` - a zip in normal time unchanged, the aim keeping LT down alone, one zip as the stick is let go and RT up before it, a moment out of play, a hold a menu drops, bad frame times, the pad's LT alone for the aim; the printed wheel - a picture for each gadget, the caption only for the lit one, the lit badge's snap, the wheel's landing, the grey and the yellow badges, under 60,000 vertices; the lit gadget's time) |
| Launcher and protocol | the launcher's 24; all six Python suites (XrData 38, `zip_releases`) |
| GPU test | passes at 1536 x 1536 and 2400 x 1800; the wheel 33,771 and 37,605 vertices (the overlay's buffer is made for some 80,000) |
| The wheel seen | the GPU test's `gadget-wheel.bmp` at 2400 x 1800, and `tools\preview-marks.ps1 -Near <that session's 0066-view.png> -Scale 3`: `near-wheel.png`, the wheel landing, at rest, lit (its snap and its boil), equipped and lit, empty and lit |

**Not checked.** That RT pressed in the game's aim zips to the point the aim
holds (the game's LT + RT as its controls have it), what RT does there with no
point held, and the aim on the ground (the game slowed its time once with the
player standing). Nothing ran in the game or a headset.

## Eight Discord reports (October 18, morning)

The user: "lets fix these issues - more info on discord". What the evidence
was, and what was checked; nothing of it ran in the game or a headset.

| | |
| --- | --- |
| Air events in flight | the user's October 15 sessions (`dist\Spidy-0.3.4\reports\game-vr-20261015-*.json`): in driven airborne motion samples the game's air handler ran 2 a step (`(steps, air_events)` deltas (3, 6) 306 times, (2, 4) 112, (4, 8) 95 in the 19:12 session); no driven airborne sample advanced without it. Airborne steps without it were the game's wall grasp (mover flags `0x80a0`, `0x82a0`), not owned |
| Impact Web | `reports\grab-probe\gadget-impact_web.png` (October 15 15:20): the game's HUD reads "Web Shooter" with WeaponImpactWeb (`3919a80`) equipped; the exe's text at `3abd870`: "When a bot is hit by the Web Blast gadget (aka Impact Web)"; the player's clip (16 s) shows IMPACT WEB 8/8 to 1/8 hitting as web balls |
| The crash (RTX 4070 SUPER) | the player's report `game-vr-20261016-140542`: VR play began at 12.72 s, XR frames stopped at 914 (13 tracked) by 12.92 s, `gpu.completed` 18446744073709551615, `left_begins` 19, the game log's `Device removed detected (0x887A002B)` 0.4 s after the virtual pad's disconnect; adapters 0 and 1 both "NVIDIA GeForce RTX 4070 SUPER" (none of the 84 game logs on this laptop lists a card twice); `Reflex: Low Latency with Boost` (the only other Reflex session, October 10, Low Latency, played 7600 tracked frames on 0.2.11) |
| SteamVR and the Quest's menu button | SteamVR's `drivers/oculus/resources/input/touch_profile.json` (GameTracking-SteamVR mirror): the left `/input/system`, no menu; `vrcompositor_bindings_touch.json`: its single press toggles the dashboard |
| Core tests | 343 (new: `HeadClearance` alone and its rays; the rig keeping the eyes out of Spidy's wall; the head back over the feet after a break; `AirYield`; `PausePress`, the pad layer on release, the game screen's L3 + R3 on release, the heal's push through a moment out of play; held bots; the fist's and the web-shooting hand's fingertips side by side, which fail with the gather off; the pressing fingers as deep as a fist's) |
| Launcher and protocol | the launcher's tests (`kCloseGameExit`, `--close-game`); all six Python suites (new: Reflex off for a session, the lost device named from the game log, the adapters, another build named and the game closed for the player, `close_game` never kills, the SteamVR pause line) |
| GPU test | passes (1536 x 1536) |

## One line round an edge; the 17:43 crash

The user: "game crashed. also when web goes around edges a second line is
drawn from the original edge and the hand". The session:
`dist\Spidy-0.3.4\reports\game-vr-20261015-174325.json` (the 17:42 deploy:
the fist on the web and the late-night swing build).

| | |
| --- | --- |
| How it ended | XR frames advance smoothly to 222.59 s, then stop; `game_exited` true, `final.status` 3 (VR active), `error` 0, `presentation` immersive, gate open |
| What the web did | the right hand's web took an edge between swing serials 60985 and 61001 (anchor (83.98, 226.02, 2534.81) to (86.59, 220.87, 2525.41), length 58.67 to 47.64 m; `ropes.bends` [0, 1], `wraps` 22 in the session); the last swing sample is serial 61005 |
| The game's own log | ends at 17:46:46 with its once-a-minute `[Render]` line: no `[Crash]` block, no `NIXXES log closed` (the 17:39 session before it closed cleanly) |
| Crash reports | none: no minidump in `crs\reports` (newest 10:19) or Documents, no WER report, no Application Error event |
| Memory | `free_commit_mb` lowest 12203; the game's commit 14.4 GB at 17:46:46 |
| Frames before | `xr_ms` 11-22 ms, no stalls, `dropped` 18 all session |

**The second line, from the game's code.** In RopeManager::Update, `679dc0`
lays Start (manager `+0x7d60 + hand * 12`) as point 0, calls the hand points
(`call [rax + 0x78]` at `0067a5cf`, where Spidy lays the wrist and the edges),
then measures the line from Start - not from the last point laid - to the
target (`0067a5d2`-`0067a647`) and fills every point after the laid ones on it:
point i at `t = (i - (count - 1)) / (n - 1)` from Start (`0067ac0c`-`0067acf1`),
n = max(2, (min(128, most) - count + 1) x the shot's progress), at most to
min(128, most) points. So a rope laid [wrist, edge] came back from the edge to
just past the wrist and ran on to the web's end. Start is also read by the
hero's update `95f6c0` (the hand's velocity from how Start moves, `+0x7df8`,
used only by the game's own tail, which a zero Grip leaves out) and by
`679670` (a rope's own search for where it sticks, before it holds).

**The fix.** native_webs lays the points in its update, sets Start to the last
of them for the game's update, and puts the wrist back afterwards. 337 core
tests, all six protocol suites. Not run in the game; `tools/probe_web_bends.py`
(new) is the check: straight, an edge on the line (the moment a web takes it),
1 cm to 5 m off it, two and six edges, an edge at the wrist and at the end, both
hands round an edge, and an edge sweeping off the line, each read back from the
game's rope slot (off the line from the last edge to the end, back toward the
wrist, the game still running). `--modules DIR` runs it with another build's
spidy_stereo_probe.dll: a build without the fix shows the fold.

## A hand on its web is a fist (the build before)

The user: "when web connects and held lets change the hand to fist and not
keep the horn pose".

| Check (`HandPoser`, 90 Hz) | Result |
| --- | --- |
| Pressed, the web holding from that frame | the pose whole at 0.1 s; gone by 0.35 s, the fist whole; both so five seconds later |
| Let go; then the next web | both gone within 0.5 s; the pose whole again 0.1 s into the next web |
| The trigger as the web button (the grip open), the web holding | at 0.1 s the pose and no fist; at 0.5 s the fist and no pose |
| Pressed, the web missed | the pose whole after a second |
| The checks before (a missed web held 5 s, half let go, let go; the other hand's web; a web ball; a punch) | unchanged |

**Checked:** 337 core tests, the launcher's, all six protocol suites. Not run
in the game, not tried in the headset.

## No pull to the web's end, near misses, webs round corners (late-night build)

The player's session on the build below (`game-vr-20261015-165818.json`,
8 minutes, 184 webs, 104 zips), against the one before it (`155311`):

| | before (15:53) | build below (16:58) |
| --- | --- | --- |
| pull past gravity while hanging in the air, median / p90 | 0.9 / 24.6 m/s^2 | 10.6 / 51.3 m/s^2 |
| share of that time over 4 g | 5.9% | 13.9% |
| speed while on a web, median | 17.0 m/s | 26.5 m/s |
| rope wound in, not by the trigger nor a zip | 47 m in 50 s | 1,345 m in 196 s |

After zips the rope kept 91% of its length (median), and swings over 5 g
followed 14% of zips (12% before): the zips were not the difference.

The same stand-in for the game's mover as below, the game's swing:

| start (from the web's end) | before today | this build |
| --- | --- | --- |
| 25 m short, 5 m under it | -6.0 m, 0.5 m/s, 20.2 m/s at the bottom | 1.4 m, 1.2 m/s, 16.4 m/s |
| 10 m short, 10 m over it | 4.8 m, 10.5 m/s (slack caught) | 9.5 m, 1.3 m/s |
| 40 m short, 15 m under it | -23.1 m, 0.4 m/s | 1.4 m, 1.1 m/s |
| 25 m short, 20 m over it | -13.0 m, 10.6 m/s (slack caught) | 1.6 m, 2.7 m/s |

(Lowest body centre over the street, worst change of speed in a step.) Keeping
0.6 m of any slack, tried on the way, caught a fall past the web's end 0.6 m
late at 9.1 m/s; only the hand's own slack is kept now.

**Checked:** 336 core tests (the hand's reach pumps 4 cm a cycle, against
0.4 m when all slack was taken in; the rope's points laid round edges, with a
web in both hands too; a corner's edge handed on), the GPU test, all six
protocol suites. Not run in the game.

## Webs from the web shooter, taut in the air, over the street (night build)

The player's session (`dist\Spidy-0.3.4\reports\game-vr-20261015-155311.json`,
Virtual Desktop, Quest 3, weight 100%): 69 webs, 42 zips; while the swing owned the
flight a web was slack (the body nearer its end than its length) by up to
56 m, and taut ones held the body 0.5-1.3 m beyond their length (the hand's
place, physical webs). Most were 100 m webs in open air. From 124 s to 139 s,
on 26-100 m webs, the feet swung down to 0.2 m over the street and touched it.

Virtual Desktop's poses (`virtualdesktop-openxr/action.cpp`, its calibration
of the Oculus runtime's): in the grip's frame the aim pose's forward is
(0, -0.866, -0.5), the grip turned 30 degrees about its +x from the hand's
-y toward the thumb; its origin (0, -0.067, -0.074) from the grip point.

A stand-in for the game's mover (tests' `NativeMover`: the step in flight,
settled step lengths, steps of 16-28 ms as in the October 5 probe), the
game's swing at real weight, a web shot at rest to its end 20 m over a street:

| start (from the web's end) | before: lowest, worst step | now: lowest, worst step |
| --- | --- | --- |
| 25 m short, 5 m under it | -6.0 m, 0.5 m/s | 5.8 m, 1.4 m/s |
| 10 m short, 10 m over it | 4.8 m, 10.5 m/s (slack caught) | 9.5 m, 1.3 m/s |
| 40 m short, 15 m under it | -23.1 m, 0.4 m/s | 2.1 m, 1.4 m/s |
| 25 m short, 20 m over it | -13.0 m, 10.6 m/s (slack caught) | 9.4 m, 3.0 m/s |

(Lowest: the body's centre over the street; the clearance asked is 2.5 m.)
Braking the body at the ground below it instead, the first try, kept it 2.5 m
up but yanked it toward the web's end 9 m/s in one step when the web lay
near level, and took a swing's speed at the bottom from 20 to 27 m/s.

**Checked:** 334 core tests, the GPU test (13 checks), all six protocol
suites. Not run in the game: the probes give the swing their own aim poses,
and the webs' start is the XR worker's.

## The vent from where the player stands (noon build)

The player's session (`dist\Spidy-0.3.4\reports\game-vr-20261015-111549.json`, the
build below): on Spidy's wall up the lift shaft from 13 s to 38 s, feet at 90.3 m
from 20 s (the vent's middle at 92.37); offers listed near him: **0** all
session (XrData `most_seen`); the hooks in; 39 presses of Y on walls, 32 on
Spidy's.

The game's gather (855460), read from the exe: a sphere of 3.35 m about the
hero's real transform, through the world's spatial index; an offer of a kind in
the mask is listed when Offer::IsAvailableTo says yes, any other when its
vtable +0x58 and 1e6e610 say yes on the real hero and IsAvailableTo says no.

Measured on fresh games, the hero up the shaft on Spidy's wall from below
(the game's crawl only to 85 m), feet at 90.12 - as in the session -
(`tools/research_warp.py --use --climb 90.1`, the prompt reading on through
`probe_vent.py watch`, the probe's steps noted in the game's prompt timeline):

| run | Y after the game's pick | listed, reasons | spot found | the game's crawl | prompt raised | result |
| --- | --- | --- | --- | --- | --- | --- |
| use3 | same frame | vent, `0x4` | (-528.96, 91.27, -587.85) | 0.10 s | (not read) | stayed on the crawl; later presses did nothing until he walked out of reach and back |
| use5 | 1.0 s | vent, `0x4` | the same | 0.10 s | 0.094 s after it | `[BTN_Y] Enter` 15 ms after Y; in the duct |
| use6 | 0.3 s | vent, `0x4` | the same | 0.10 s | 0.094 s after it | `[BTN_Y] Enter` 16 ms after Y; in the duct |

The spot is where the game's own crawl stands at that vent (to_vent's climb
stops there): 1.1 m below its middle, 1.15 m up the wall from the hero.

**Checked:** 324 core checks (the use's press waits 0.3 s after the pick and
is made once more if the game keeps holding the offer; a spot answers only
the range), all six protocol suites (XrData 36).

## Spidy's walls everywhere; the game's interactions by pointing (late-morning build)

Measured in the running game, without a headset: a fresh game per run, Steam
shut first, the swing, the rays and the web shooter running as a VR session
runs them (walls on). The headset's own half - the rings, the haptic tick, the
press - is the unit-tested `offer_pointing`; the probe does what it does.

**The fall out of the world, in the session** (`dist\Spidy-0.3.4\reports\game-vr-20261015-094756.json`,
the morning build): from 46.9 s the game's crawl on the lift shaft's wall,
normal (-0.71, 0, -0.71); at 54.76 s the game turning him off it (mover
`0x80a6`, the swing not holding him); at 55.07 s Spidy's wall taken with normal
**(+0.71, 0, +0.71)** - the same wall's face met from inside it - the body
0.61 m behind it; then held on it up the shaft to 69 m, on the wrong side.
Two guards now: no wall while the game holds the body in its own wall states
(mover flag `0x80`), and for 1.5 s after its crawl no face turned against the
crawl's wall and behind it (`Swing::supportSurface`).

**A use at the first mission's vent** (`tools/research_warp.py --use --nudge 1.15`,
profile 3: from the game's crawl at the vent, the walk on onto Spidy's wall,
back up to the vent's height on it; a probe cannot walk Spidy's wall, so the
offers' crawl pose was nudged 1.15 m up, to the vent). Twice, two fresh games,
the second with the back-face guard in; both alike:

| | time from the use | state |
| --- | --- | --- |
| on Spidy's wall | before | the vent among the offers gathered: reasons `0x0`, on; the game's own pick: none |
| `SpidyOffersUse(vent)` | 0 | 0 (asked) |
| the game's crawl under him (game_warp) | 0.10 s | wall 2, mover `0x80a6`; crawl `0x8001` at 0.18 s, feet on the posed point |
| the game picks the vent (its best = the vent) | 0.19 s | |
| the game's Y, 0.35 s | 0.20 s | its vent entry, mover `0x80a1` from 0.7 s |
| inside, on the duct's floor crawl | 1.8 s | wall 3, mover `0x8001`, y 92.0 (screenshot: the duct) |

**The duct, the swing running** (a scratch probe: the stick to the game's crawl
and, as a session sends it, to the swing): 1,683 samples over 40 s - forward,
right, forward again - **no Spidy wall, no ownership, no mount** in any; the
floor crawl in all but the first.

**Walking at a wall from the ground** (`tools/probe_wall_mount.py --skip-hand-over`,
profile 1, a street wall): the stick at the wall at 9.83 s, Spidy's wall at
**10.06 s**; up it 9.2 m at 6 m/s, 0.95 m off it, no game crawl. Passed.

**Checked:** 324 core checks (new: walked onto a wall the swing's but not in a
vent; the game's wall states take no wall; the crawl's wall not taken from
behind; the crawl hands its wall over only to a walk; pointing at an offer; the
interact's use of it), all six protocol suites (XrData 35).

## The crawl's stick, the duct's roof, the body in a duct, the view's turn (morning build)

From the headset session of October 14, 19:57
(`dist\Spidy-0.3.4\reports\game-vr-20261014-195702.json`, the deployed build):

| measured in the session | value |
| --- | --- |
| on the game's wall crawl, stick pushed | median **0.67 m/s** (crawl full speed 2.6), 482 of 852 samples under 1 m/s |
| on the game's floor crawl (the duct), stick pushed | median **2.6 m/s**, 77 of 584 under 1 m/s |
| CrawlStick, the turn from the way sent to the way taken | per 2 s window: -172 to 162 degrees; agreement 0.51 over the session |
| the duct's roof over the feet | found 93.5 m (1.4 m of room) along most of it; **nothing within 3 m** around (-525, 92, -583), where the eye snapshots at 56 s and 141 s show the view out through the roof |
| gate `tracking` (a frame the rig could not place) | 67.12 s and 94.01 s, each as the game turned the hero onto or off a vent shaft's wall: the view levelled and turned back 90 degrees in 0.2 s |
| the game's crawl on a floor | mover `0x8001`, grounded, actor upright - 15, 7 and 32 s stretches in the ducts; 0.2-0.3 s at the top of a wall onto a roof |

**The game's crawl takes its stick in its camera's frame laid onto the wall**
(`tools/research_crawl_free.py`: a building's face in
free roam, normal -z, the camera turned seven ways with the right stick, 56
pushes of 1 s, 1087 moving samples):

| rule (angle from the way the hero went, per sample) | mean | median | over 45 degrees |
| --- | --- | --- | --- |
| the camera's look laid onto the wall; its up where under 0.2 of the look lies along it (shipped) | **23.8** | 17.3 | 126 |
| the camera's level look laid onto the wall (the rule replaced) | 41.8 | 21.5 | 355 |
| the level axes turned onto the wall as the view is | 43.1 | 25.9 | 388 |
| the camera's look and right laid onto the wall | 52.7 | 35.7 | 494 |
| the stick's own direction on screen, onto the wall | 76.7 | 86.7 | 630 |

On the lift shaft's wall the game's camera is locked looking up the shaft and
every rule agrees (stick up went up, right went along the wall).

Then the two rules side by side on the same face (`tools/research_crawl_verify.py`),
each direction asked for along the wall's own axes and recomputed from the live
camera each frame:

| | new rule | old rule |
| --- | --- | --- |
| mean angle off the way asked | **29** | 61 |
| worst | 90 (one push against something on the wall, 0.34 m/s) | 141 (backwards) |
| mean speed, all pushes | 2.6 m/s but that one | down to 0.16 and 0.66 m/s |

The camera turned to hug the wall (looking out from it) leaves both rules off
by 30-70 degrees; in a session the game's camera is not steered on a wall.

**Checked:** 320 core checks (four new: the crawl's stick in four measured
camera frames, the view kept through frames play cannot place and levelled for
a menu, the eyes' straight way onto a wall, the kneeling legs), all six
protocol suites (XrData 34).

# Validation — 2026-10-14

## Stop on any wall and the game's own crawl takes you; up a wall, the game's own wall run

Measured in the running game (profile 3, the first mission's lift shaft, a
fresh game per build, Steam shut first) with the swing, the rays and the web
shooter running as a VR session runs them (walls on). The rig is
`research_vent_offer.py`, `research_warp.py` and `research_wallrun.py` in the
session's scratchpad; the game's own offer state is read from its memory.

**Why the game offers nothing on Spidy's wall.**

| measured | value |
| --- | --- |
| the hero's offer processor (`Hero::HeroOfferProcessor`, mask at `+0x1c8`) | **0xffffffff** on the ground, **0x23** on the game's crawl, **0x2** on Spidy's wall (the game's air), 0 in between |
| the vent's own offer (`VentOffer`, kind 0x1) | on the crawl: the processor's best offer, no reason against it; off it: reason **0x40**, its own entry check (`+0x128`) |

**Onto the wall, the game's own way** (`game_warp`: the transition manager's
`+0x360`, `HeroStateWallCrawlEnter`, asked on the main thread):

| measured | value |
| --- | --- |
| at rest on Spidy's wall, 0.25 s | crawl asked once; the game began (`0x80a6`) 0.01 s later, its crawl (`0x8001`, standing) at **0.17 s** |
| height through the hand-over | **90.26 -> 90.26 m**, no drop; offer mask back to 0x23 |
| hand-overs | taken **1 of 1** in each of four fresh games, declined 0 |
| then, on that crawl, up to the vent and Y | the vent the best offer; **into the duct** at (-528.04, 92.0, -586.93), as from a walked-onto crawl |
| the jump off the crawl (RT + A) | caught by Spidy's wall at once, 1.0 m under the crawl, as before |

**Up the wall, the game's own wall run** (its RT with the stick up, on its crawl):

| measured | value |
| --- | --- |
| alone (nothing of Spidy's moving the player) | 9.1 m up in 1.6 s from the crawl, 5.8 m/s mean, 9 m/s peak; let go, its crawl again at the new height |
| with the swing running | mover `0x80a1` (contact 2); **52 -> 58.4 m in 0.76 s**; the swing reported the game's hold and never owned the body; let go, `0x80a6` then the crawl (`0x8001`) at 60.94 m |
| RT with the stick to the side | a jump off the wall, not a run - so only up the wall holds RT |

**The dead ends, each measured and none shipped.**

| tried | what happened |
| --- | --- |
| `WarpToWallCrawl` (straight to `HeroStateWallCrawlIdle`) | held at once, no drop - then the game crashed in its audio thread the moment the crawl moved (null at `+0x148`, `0x2fde47d`) |
| the crawl's own let-go (`HeroStateWallCrawlToFall`) to walk on with Spidy | 0.35 s of the game pushing the hero 1.3 m off the wall, then a fall to the shaft's floor |
| the swing taking the body when the stick let go of a run | stopped the game's turn back onto its crawl; an 11 m fall |
| any `0x80` flag near the wall taken for a run | the RT + A jump shows it too; the swing let go, and he fell 43 m |

## The vent, end to end: walked onto, a wall is the game's; nothing hands a player over in midair

Everything below was measured in the running game, on profile 3 at the first
mission's lift shaft, with a fresh game per run and Steam shut first. The rig:
`probe_vent.py`/`probe_vent_walk.py` drive the virtual pad, the swing runs as a
VR session runs it (walls on), and the game's own prompt calls are read where
noted. The player's own three headset sessions of October 14 are the first rows.

**Why the earlier fixes could not work - every hand-over from Spidy's wall.**

| measured | value |
| --- | --- |
| headset, swing's wall to the vent | game's crawl in **0** of 905 / 494 / 503 swing samples; airborne (`native_contact` 2) in 834 / 364 / 407; **no prompt raised** in any |
| where the swing took the wall vs where "indoors" fired | serial 1476 at y 47.9 (the shaft's floor) vs serial 3562 at y ~88 - 42 m later |
| releases by switching walls off | `owned` stayed **1** through every metre of the fall: the swing went on flying the body, the game never got a step |
| the hand-over's push into the wall | 3 frames in 3 tries - the wall's normal vanished the instant the walls went off - and 7.5 m drops |
| held against the wall by the swing (0.9 -> **0.59 m**, the game's collision) | no crawl in 5 s, still or with the game's stick any way |
| the body yielded to the game against its wall | its air state's vertical speed **-0.1 -> -42.1 m/s in one step**; a 43 m fall past the wall |
| the swing walking up into the vent's opening | the wall flickered off and the swing's flight dropped him 43 m (very likely the player's "wall walked to the vent and fell") |

The game counts its own fall from the time airborne, right through the swing's
flight, and its first step of its own uses it: nothing can hand a player to it
in midair. And the interact a vent raises is decided per interactable by a
virtual call with the hero (`0x857687`, `call [rax+0x50]`, in the interaction
update `0x857360` that raises `POIInteractAvailable` through `0x79ca40`), so
there is no single "registered" flag to set either.

**The fix - and what it measured.** A body on the ground no longer mounts a wall
(`SwingConfig::mountFromGround` off): walked onto, the wall is the game's crawl.

| run (profile 3, swing running, walls on) | result |
| --- | --- |
| forward from the load point, up the shaft | **1,265 / 1,263** samples on the game's crawl, **0** on the swing's wall, `owned` **0**, no jump asked for, no drop |
| at the vent | the game raised `POIInteractAvailable(5, 0, '[BTN_Y] Enter')` |
| the game's Y | into the duct: (-528.04, **92.0**, -586.93), inside it on screen |
| both runs | identical |

**Leaving a crawled wall** - so that walking onto walls costs no swing:

| on the game's crawl, 22 m up the shaft | result |
| --- | --- |
| A with the stick none / back / left / right / back-left / forward | stays on the wall every time; the directions leap **along** it (back 1.2 m down, forward up) |
| B, B back, A held 0.9 s, LB, RB, RT, LT | stays on the wall, no movement |
| **RT + A** | **off the wall in 0.088 s** |
| a web, before the shot fix | refused, 160 tries: its clear-line check started on the wall's own surface |
| a web, the takeoff's A alone | held, then the takeoff timed out and retried |
| a web, RT + A at once | first press ignored, the retry 0.6 s later left: **1.1 s** web to swing |
| a web, RT held from the web's hold | first press: **0.46 s** |
| a web, and the takeoff off once 12 cm clear of the wall | **0.16 s** web to swing (a web from a ledge: 0.17 s) |
| the swing's own lease on a crawled body | `controlled` 0: the movement module is not in the crawl's path at all |
| the crawl's climb | 2.6-3.6 m/s on the stick; RT with the stick throws him off the crawl instead |

**Also measured.** The module accepts the swing's `walls` 0, 1, 2 and 3, and
rejects 4 (`probe_walls_setting.py`) - 4 was the yield, removed. The prompt
observer (`SpidyPromptsWatch`, probe-only: VR sessions leave it off) killed the
game twice on profile 1's free roam, seconds after it was turned on; with it
off the same run lives. The world-ray bridge answered 1000 to every start after
a stop in one game process; it switches its hook back on now.

**Checked:** 313 core checks, 23 launcher checks, all six protocol suites. The
turned view, the standing body and the headroom on the crawl are unit-tested
only. **Not yet played in a headset.**

## Indoors the swing takes no new walls, which is why the vent now reaches the crawl

The first build of October 14 fixed how the crawl looks and never ran: the
headset session after it (`dist\Spidy-0.3.4\reports\game-vr-20261014-124656.json`)
came back

| | |
| --- | --- |
| `wall == 2`, the game's crawl | **0** of 905 swing samples |
| `wall == 1`, the swing's own | 723, at its 0.9 m stand-off |
| `native_contact` | **2 = airborne in 834 of 905** |
| `prompts.raised` | **0** - the game offered no interact at all |
| `indoor_frames` / `walls_handed_to_game` | 1914 / 3 |

Wall running was on, so the swing took every wall and the game had the player
airborne the whole climb; nothing that makes the crawl look right could run,
because the crawl never held him. The indoors rule fired 1914 frames' worth and
changed nothing, because its hand-over is guarded against the drop and the
guard never lifts up a shaft.

**The fix is a setting that cannot drop anybody.** `Swing::takeNewWalls(false)`
- the `walls` setting's value 2 - keeps the wall the body is on and takes no
other. Indoors that is all the rule needs: the player keeps what he is on until
he leaves it himself (a jump, its top, a web), and the next wall is the game's
own crawl. Letting go outright still means letting go, which is what 0 is for.

Measured in the running game (`tools/probe_walls_setting.py`): the module takes
`walls` 0, 1 and **2**, and rejects 3 with 2001. Unit-tested in the solver: the
wall already held survives 60 steps of it, its normal unchanged; a body that
flies into a new wall is left to the game; switching back on takes walls again;
and `allowWalls(false)` still lets go, as it always did.

That same session also caught a bug in this build's own headroom ray: it was
cast along the actor's up, which is the world's up while the swing holds the
player on a wall, so it found the shaft's ceiling **0.16 m** over his feet and
crouched his head for 22 frames. It is cast along the surface the view actually
stands on now, and a reading tighter than a crouch is not treated as a roof.

## One wall, not two: the game's crawl holds it and Spidy gives it the view

Two headset rounds on October 14, by the player, on the build before this one:
wall running off on a plain wall and up the lift shaft of the first mission,
then both again with wall running on.

| | |
| --- | --- |
| the vent, crawl holding him | **opened** - "managed to get throught the vent" |
| the camera, inside the vent | through its roof - "the camera clips through the top of it when inside" |
| the shaft, wall running on | "the wallwalking worked only at some point, most was crawling" - the indoors rule handing the walls over, as built |
| the body, either way | the game's crawl pose - "i dont want the crawling animation on legs or hands ever" |

So the interaction was already answered, and by the crawl: it is the only hold
the game itself calls standing on a wall, which is why its own interacts work
there and why they cannot work on a wall of the swing's, where the game has the
player airborne and takes its interact away 63 ms after he leaves a surface.
What was left was that the crawl did not **look** like standing. Three things,
each measured to a line of code:

- **No view.** `SurfaceHold::crawl` and the whole of the rig's crawl path were
  built and tested in October (`core_tests.cpp`, "game rig stands on the game's
  wall crawl at once"), and `game_xr.cpp` passed it nothing: its own comment
  said why - "the game takes the stick there in its own way, which is not
  measured yet". So the one state whose interactions work on a wall was the one
  state without the view that makes a wall a floor.
- **Crawling legs.** `native_body.cpp` set the body's up to `toModel.rotate({0,
  1, 0})` - the **world's** up, always. On a wall that puts the IK's ground
  plane a quarter turn off the feet, so no foot is ever within `groundedFoot`
  (0.3 m) of it, and `body_ik` leaves the legs to the game's pose by design.
  The game's pose there is the crawl.
- **Crawling fingers.** `Targets::Hand::fist` at 0 means "the game's fingers",
  and an open hand is 0. The crawl splays them onto the wall; the player's
  hands are where his controllers are, with nothing to splay onto.

And the head: a ray from the feet along the surface the player stands on now
says how much room is over him, and the head stops `headClearance` (0.2 m)
under it, never closer to the feet than `leastHeadroom` (0.5 m) - a crouch,
which is what a duct is. One rule for a vent, a duct, a low corridor and a
wall, applied in the level frame so the surface turn carries it onto a wall's
own normal.

**What is still unmeasured, and now measures itself.** How the game's crawl
reads the stick. The axes it is sent were the world's horizontal ones, which
throw the whole of "up the wall" away; they are now the game camera's forward
brought into the surface and the surface's own right, which is right if the
crawl reads them in the camera's frame. Whether it does is what `CrawlStick`
gathers: the turn from the direction sent to the direction taken, in the
surface's own axes, as a circular mean.

| `crawl.stick_agreement` | what it means | what to do |
| --- | --- | --- |
| near 1 | the crawl's frame is fixed against the surface | rotate the stick by `crawl.stick_turn_degrees` - one line |
| near 0 | there is no fixed frame | the stick is the wrong channel: drive the mover, as the swing does on a wall of its own |

Either answer ends it, out of one session. Three builds of guessing could not.

**Checked:** 308 core checks, 23 launcher checks, all six protocol suites.
`CrawlStick` against a fixed frame, no frame at all, a resting stick, a still
body, a step into the wall and numbers that are not numbers; the rig's stick
axes reconstructing the player's own turned intent exactly on the crawl and
staying level on a wall of the swing's; the headroom cap at a metre, at 0.2 m,
over the player's own head, at nothing and on a wall; the open hand landing on
its own angles from three different game poses, leaving them alone at 0, and a
fist closing over it. **Not yet run in a headset.**

## "Crashed on game load" was the title screen being pressed blind

The session (`dist\Spidy-0.3.4\reports\game-vr-20261013-210325.json`), read
rather than guessed at:

| | |
| --- | --- |
| `game_exited` / `error` | true / null — Spidy reported no failure |
| `presentation` | `game_screen` for all 119 of its samples; `gate` never opened (`no_player`) |
| `tracked` | **0**, in all 148 samples and in the final count of 414 frames |
| `head` / `hands` | all zeros / identity — no pose ever arrived |
| `vr_buttons` | `['b']` in 9 samples, `['a']` in 3 — the player's own Touch buttons |
| `pad_buttons` | `0x2000` in those 9, `0x1000` in those 3 — each one passed to the game |
| last press | frame 405, of 414 |
| the game's own log | `[Save] Request save type 2` at 21:03:43, process gone at 21:03:46 |

The game saved and quit, 20 seconds after it started: the title screen's QUIT
GAME is two moves from the top, and the presses were made while the headset was
being put on. Not a crash, and no dump was written because nothing faulted.

The fix is `game_pad::mappingFor`: `Mapping::none` until the headset has given
a tracked head pose once, whatever the screen and whatever the pad layer. Nine
assertions cover it, including that every case after the first pose is what it
was. It cannot regress a session that reaches the headset, because the gate
opens on the first pose and never closes.

## The rule never fired: `frame.seconds` is the frame, not a clock

A whole session in the vent came back with **`indoor_frames: 0`**, while the
player was on the swing's own wall for **786 of 838** swing samples and sent the
game its Y **38** times (`pad_buttons` 0x8000). The reason is one line.

`XrFrame::seconds` is the time **since the previous frame** - about 11 ms - and
every rule here takes it that way and accumulates it; `game_pad::PadLayer` says
so in as many words: "seconds: since the previous update", clamped to [0, .1].
`Enclosure` and `WallHandover` were written taking it for a clock, so
`now - since_` came out at about zero every frame and no reading could ever hold
for the 400 ms the verdict waits. Both accumulate now, and their tests feed them
frame lengths rather than timestamps.

The counters are what found it, in one line of a report: the rays were right,
the room was right, and the rule simply never decided.

## Indoors the game keeps the walls - the rule, and the rooms it was read in

`spidy::Enclosure`: five of the world rays a session already casts (one up,
four along the ground, in the batch's free slots), and a count of how many ways
the player is closed in - a ceiling within 4 m, or surfaces within 4 m on
opposite sides. Two of three is indoors, held for 0.4 s before the verdict
moves, and applied only on a frame where the swing does not have him on a wall
of its own.

**Read in the game, profile 3, walking from the Fisk lobby up into the vent**
(`tools/probe_enclosure.py`, 30 readings):

| Where | up | north | south | east | west | ways | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fisk lobby (23 readings) | 5.0 | 5.0 | 5.0 | 4.8 | 5.0 | **0** | wall running stays on |
| the shaft | 5.0 | 1.72 | 3.82 | 3.82 | 1.74 | **2** | indoors |
| the shaft, turned | 5.0 | 3.96 | 1.00 | 1.00 | 3.96 | **2** | indoors |
| inside the duct | 1.65 | 0.02 | 0.30 | 0.30 | 0.02 | **3** | indoors |
| the duct, further | 0.88 | 0.02 | 0.29 | 0.29 | 0.02 | **3** | indoors |

(5.0 is the ray's own reach: nothing found.) The lobby reading is the one that
matters as much as the vent: a room can be indoors in the ordinary sense and
still be nowhere near closed in, and there wall running is left alone.

**The first build of this got the city wrong**, and the player found it in a
sentence: "now i never stand on the wall". Two things in it read the open world
as closed in, and both are measured now:

- The rays started at the **feet**. On a wall the feet are on the surface, so
  every ray started inside the building and came back at nothing. They start at
  the **head**, which stands off the wall, in open air.
- They counted **anything** they hit. A car, a crate or a passer-by is within
  four metres on every street in the game. Only what the world is built of
  counts now (`native_rays::fixedSurface`, the same test the swing uses to pick
  a wall).

Read again, both ways round:

| Where | readings indoors |
| --- | --- |
| The city, walking the street past buildings (profile 1) | **0 of 30** - wall running untouched |
| The Fisk lobby, then the shaft, then the duct (profile 3) | **20 of 30**, ways 2: north 3.74, south 2.10, east 2.11, west 3.76 |

The second is the one the filter could have killed, and did not: a duct is built
of the world, so it still closes the player in.

Sixteen assertions cover the rule itself - the vent, the shaft, the corridor,
the alley that must NOT count, the open city, a single wall, the settle on the
way in and on the way out, a one-frame flap that must not take the walls back,
and a clock that goes backwards.

## The fix that did not work: the interact hands the player back

`game_pad::WallHandover`, a rule with three conditions: the interact is
pressed, the swing holds the player on a wall of its own
(`game_swing::Surface::wall`), and the game is offering no interact. Then the
swing's walls go off for 0.9 s (`Swing::allowWalls(false)`, which leaves the
wall it holds), and go back on when the moment ends - on the timer, or as soon
as the player lets go after a quarter second, or if the session's clock goes
backwards or comes out as no number.

**Why it cannot regress anything.** Those three conditions are exactly the case
in which the press does nothing today: the game has withdrawn its interact, so
there is nothing for any button to land on. Where the game IS offering
something, `offered` is true and nothing is handed back - the ordinary path
(the prompt's demand, the pad layer, the game's Y) is untouched. A player who
set wall running off never reaches it either: the hand-over only runs when the
settings last sent to the swing had walls on, and it puts back that value, so
the setting cannot be turned on behind the player's back.

| Checked | |
| --- | --- |
| Core checks | **302**, with a new one of 20 assertions over WallHandover: it fires on the press, lasts its moment, ends early when the button goes, needs a new press rather than a held one, never fires with an interact offered or off the swing's wall, counts the hand-overs and the ones the game answered, and puts the walls back for a time that went backwards or was no number |
| Protocol checks | **116**, all six suites, including the cross-side guard that reads the C++ magic/version/`static_assert` size and compares it with what the tools demand — the test that catches the mismatch which killed three sessions earlier the same day |
| XrData | **version 29, 952 bytes**: `wallHandovers` and `wallHandoversOffered` |
| Build | clean Observer build |

**Not measured in the game.** The hand-over end to end needs a VR session (the
XR frame loop runs the rule), and the one step that could still surprise is
whether the game's own crawl catches the player where he stands when the swing
lets the wall go. It rests on the behaviour the October 10 work already
measured for `--walls off`, which is the shipped `-NoWallRun` mode: with the
swing not taking walls, the game's own wall state takes the player. Three
headless attempts to see it directly failed for a reason of their own - the
swing only takes a wall once it has the player (a jump and a web), the wall-run
probe's reel lost him 34 m short at the save's load point, and the pad's walk
does not move the player in free roam there - and the ray bridge allows one
start per game launch, so each attempt costs a restart. The two new counters
answer it in the first headset session instead: `wall_handovers` rising with
`wall_handovers_offered` is the game taking him back and offering the vent
again.

**The crash.** The game left a dump at 20:45 (`tools/read_crash_dump.py` reads
these now, without a debugger): **access violation reading address 0, inside
`Spider-Man.exe` at +0x2fde47d**, in none of the six Spidy modules that were
resident. No VR session was running, so none of this build's code was in play;
it followed a probe whose web reel lost the player and left him stuck
unsupported in mid-air for ten seconds.

## The vent, in the game, measured: the game takes its own interact away

You: "third profile is closest to the vent (you just need to run straight
forward from load to get to it and it asks for f on keyboard)", then "try until
you reach it stop giving up".

Reached it, on the backup laptop, with no headset: `probe_menu_pad.py start`,
then `probe_vent.py` drove the pad through SELECT PROFILE (three A presses,
down, down, A) into **profile 3**, CONTINUE, and walked straight forward about
23 seconds up the Fisk Tower elevator shaft to the vent the marker counts down
to (40 m, 24 m, 0).

**What the vent asks for.** Its prompt is not a text and not an icon id. It is
an argument of the POI call:

    POIInteractAvailable(5, 1, '')              the interact becomes available
    POISetButton(5, 2.24094e+09)                the button, as a hash
    POIInteractAvailable(5, 0, '[BTN_Y] Enter') the interact goes away, with its text

So the vent says **`[BTN_Y] Enter`** — the markup token, read correctly:
`prompts.demand` reads 32768, which is the pad's Y. On the keyboard the same
token draws as **F**, which is what you saw. The reading works: `texts` went
from 0 in every earlier session to 193, then 5527.

**Y works.** Standing at the vent on the game's own wall crawl, the probe's Y
was consumed in the same millisecond it was sent (`>> the probe's y` at 153188,
`POIInteractAvailable(5, 0, '[BTN_Y] Enter')` at 153203), twice over, and
Spider-Man went into the vent.

**What takes it away.** Standing still at the vent for thirty seconds with the
interact live, the probe pressed **A alone** — no stick, no drift:

    54797  >> the probe's a
    54860  POIInteractAvailable(5, 0, '[BTN_Y] Enter')

**63 milliseconds after leaving the surface the game withdrew its own
interact.** It does not ignore the button. There is no longer an interact to
press, so no button can work — not Y, not the pad layer, not the prompt-driven
press. It comes back only once he is on a surface in range again (measured:
walking back raised `POIInteractAvailable(7, 1, '')`).

**Why that is the vent bug.** Spidy's wall running drives the hero in the
**game's air state** — that is how the swing moves him, and
`probe_wall_run.py` asserts it (a step on the swing's wall that is not "the
swing's in the air mode" fails the probe). So with walls on, at a vent, the
game has already withdrawn the interact before the player presses anything.
That is the whole of it, and it is not about buttons at all: every patch so far
answered "which button", while the game had taken the interact away.

It also predicts the rest of the family — every world interact reached while
Spidy holds the player off the surface behaves the same.

**The workaround that exists today:** **Wall running off** (the launcher's
toggle, or `-NoWallRun`), added earlier the same day. The game's own wall crawl
then holds him, and the interact stays. Measured in the same run: with the
swing started but not owning the player (`owned: 0`, surface = the game's wall
crawl), Y consumed the interact.

**Not yet measured:** the interact with the swing actually owning the player on
its own wall (`wall: 1`). `probe_vent_wall.py` starts the rays and the swing at
the vent and walks the hero at the wall, but the swing never took it from the
game's crawl there, so that last link rests on `probe_wall_run.py`'s own air
mode check rather than on a reading at the vent.

**Saves:** backed up before every run and compared after. Only `slot0-s.save`
and `-userprefs.save` ever changed, both restored; profile 3's own save was
never written. Display settings restored.

## The game's own prompts answer in VR — current build, never run in the game

The user: "there is a repeated pattern of interaction bugs that both i
encoutered during sessions and are reported on discord most of them mostly
rb+lb or interaction button related during story events or random encounters.
I feel like we're patching stuff but there must be a more systemic way",
then "build it fully".

**What the hole was**, read from the code and from Discord (the bug, help and
idea forums plus 400 chat messages, pulled read-only on October 13):

| | |
| --- | --- |
| Controls `fromControllers` can send in `Mapping::gameplay` | Start, Back, A, B, Y, LB + RB together, D-pad down, LT + RT together, R3, and the Steam Frame's own View and D-pad |
| Controls it can never send (Touch and Index) | the game's X, its L3, each bumper alone, each trigger alone, D-pad up, D-pad left, D-pad right — nine |
| Why the game's X is missing even though punches work | punches call the damage system directly (`native_bodies::damage`), so no X ever goes to the pad; in play VR's X is the game's Back |
| Why cutscenes were fine and story beats were not | `Mapping::menus` (the whole pad) is chosen whenever the game owns the screen; a beat or an encounter during gameplay gets the nine-control subset |
| How each gap was found before | one player report at a time: the right stick's click for LB + RB on October 11 (a first-mission door), that stick's pushes for the heal and LT + RT on October 12 (that mission's heal and its vent) |
| Open topics that are this one cause | about 20 of 83, among them "Can't pull objects with LB + RB (Pico 4 Ultra)", "cant use Vents", "tap x repeat but tapping x opens the menu", "SO MUCH controlls dont work", "Air Launch", "Sable-Turrets no grab with Web bug", "not being able to free Civillians in cage", "you can't collect coward's pigeons", "3.0 cant heal" |

**Where the game names the button** (image offsets in the supported
Spider-Man.exe, read from the file, no game running):

| | |
| --- | --- |
| The prompt and QTE interface | `placeablePromptManager` with ShowPlaceablePrompt / HidePlaceablePrompt / SetPlaceablePromptProgress, `qteManager` with AddQTE(`button`, `subtype`, `miscVal`, `miscValTwo`) / UpdateQTE / ClearQTE, and POIInteractAvailable / UpdateInWorldPrompt |
| Their movies | `ui/export/HUD/PlaceablePrompts.gfx`, `ui/export/HUD/QTEs.gfx` |
| Where the names live | the same name table as `BotMoverManager` (0x3833a78) and `HoverMoverManager`, which the enemy scan already reads |
| The call-name RVAs used | ShowPlaceablePrompt 0x3896ce8, HidePlaceablePrompt 0x3896cd0, SetPlaceablePromptProgress 0x3896d00, UpdateInWorldPrompt 0x3895ca0, POIInteractAvailable 0x38982c8, AddQTE 0x3896e5c, UpdateQTE 0x3896eb8, ClearQTE 0x3896e68 |
| How those RVAs were checked | the same file-offset-to-RVA conversion gives `OpenOptions` = 0x389f4c0, which is `game_menu.cpp`'s own `openOptionsName` constant |
| The icon vocabulary | 221 strings at 0x3a80300 and up: nine device families (X360IconMap, XOneIconMap, DualShockIconMap, DualSenseIconMap, SteamDeckIconMap, SwitchProIconMap, GenericIconMap, MkbIconMap, SteamIconMap) sharing one set of endings, plus `YUIIconMarkupConfig`, `MarkupText`, `IconMapping`, `Separator` |
| Where the hooks come from | `game_menu` already hooks the call wrapper 0x1d1cf30 and the text lookup 0x1749970, and MinHook takes a target once, so it hands both on rather than this hooking them again (as the research exports do) |

**Checked without the game:**

| | |
| --- | --- |
| Every control of the pad can be sent in VR play | every combination of the VR controls (face buttons, both grips, both triggers, both stick clicks, the right stick's four ways, each flag the swing leaves — 17280 states a mapping) through `fromControllers`, against `prompt_buttons::controls()`: all 18 controls come out of the gameplay or the pad mapping, the nine are exactly the ones play alone cannot send, and `reachableInPlay` agrees with the mapping for each |
| The pad layer | latches on the chord and not before `chordSeconds`, toggles once per chord (it must be let go of first), one stick alone never latches it, a web carrying the player ends it and spends the chord, 20 s with nothing pressed lets it go while a control in use keeps it |
| The interact button carrying a prompt | the demand a press began with stands for that press even when the prompt ends or changes; a prompt that arrives while Y is already held (the hero walking up to a door) is taken; presses counted |
| The icon ids | ids of all nine families, the Both / Outline / Small / Softpull endings, the PlayStation shapes (its X is the pad's A, its Square the pad's X), the stick ids, keyboard and mouse ids, gadgets, emoji, a Steam back grip, and "XBButtonAway" which is not XBButtonA |
| A prompt text read and rewritten | "LB + RB (Hold) Throw" gives both bumpers and the hold and reads "RIGHT STICK CLICK (Hold) Throw"; "Tap LB" reads "Tap Y" (play has no control of its own for a single bumper); the PlayStation spelling, a vent's LT + RT, two spellings apart; an icon id in markup is read but never rewritten; a text naming no button is left alone; a full buffer writes nothing |
| The session report | XrData 27 with the prompt fields packed: every counter parsed, `LB + RB (hold)` decoded, and the Python `reachable_in_play` and log line agree with the C++ for X, LB, LT, D-pad up, L3, LT + RT and the stick ways |
| Nothing else broke | 300 core checks (5 new), 116 Python protocol checks (1 new), a clean Observer build with no new compiler warnings |

**Not checked:** nothing in this build has run in the game. The PC is under
the no-launch rule of October 10, so what a prompt's text actually looks like
in play, whether the eight call names still match the game, and the feel of
the pad layer in a headset are unproven. In a session report
`prompts.invokes` rising while `prompts.calls` stays 0 says the call names are
wrong; `prompts.texts` at 0 says no prompt text reached the lookup. The pad
layer and the coverage it gives depend on neither. `--no-prompt-help`
(XrConfig option bit 20) leaves the game's prompts alone if reading them ever
misbehaves.

## The game window shows the eye the headset showed — October 13 build (released in 0.3.5), not run in the game

The user: "why are the combat indicators and custom ui not visible on the
mirror on screen? shouldnt it litterally show on e of the eyes?", then:
"showing the battle ui is most important, i also don't want to hit
performance too much".

**Where it came from** (read, not run):

| | |
| --- | --- |
| The window's picture | `mirrorEye` (`src/native_eye_gpu.cpp`) drew `stagedEyes[0]`: the game's eye image as `stagePair` copied it |
| Spidy's own drawings | `overlay.renderViews` in the XR worker (`src/game_xr.cpp`), onto the headset's images, after `copyLatest` filled them from the staged pair |
| So | the staged image never had them: combat marks, aim markers, focus meter, slow motion's grade, calibration panel, overlay gloves and webs |

**What it does now:** after the overlay pass the worker calls
`native_gpu::keepShownEye` with the left image. While the window mirrors the
eye and has drawn since the last copy, it copies that image to `shownEye`
on the game's queue (the overlay's commands went to that queue before it).
`mirrorEye` draws `shownEye` while `shownEyeFresh` (500 ms; two and a half
pictures of a window slower than 5 a second) and the staged eye otherwise.

**Checked without the game:**

| | |
| --- | --- |
| Core tests | 286 pass. New: a copy stays half a second, longer for a window slower than 5 pictures a second; none before the first copy; a clock that went back |
| Graphics test, Direct3D debug layer, 1536 x 1536 and 2064 x 2208 | pass, no debug-layer error. New: the copy `keepShownEye` makes (headset image in RENDER_TARGET, the bridge's own texture resting in COPY_SOURCE, read as a shader resource) queued right behind an asynchronous overlay, from a typed sRGB and a typeless image: the overlay's drawings exactly, the headset's image unchanged |
| Python checks | 114 pass, none new: no protocol changed |

**Not checked:** the game. Nothing was started on this PC. So not seen: the
marks in the window, the window with the headset off the head, and what the
copies cost. In a session, `SpidyPresentCount(3)` against `(1)` says how
many of the window's pictures were the headset's; a recording of the window
in a fight shows the rest.

## Steam Frame controllers: a layout of their own — preceding build, not tried on a Steam Frame

The user: "check if people with steam frame reported bugs on discord and fix
them". Read from Discord on October 12 (the bug, help and idea forums, the
chat back to October 7, #general and the feedback channel). One report is
open: chrisdrenegade, #spidy-bugs, on 0.3.0, "X is registering as dodge on
Steam Frame Controllers. Multiple buttons are mapped to dodge on steam frame
controller for some reason."

**Where it comes from** (SteamVR 1789498647 on this PC, read, not run):

| | |
| --- | --- |
| Layouts Spidy suggested until now | Touch and Index (`xr_session.cpp`) |
| SteamVR's translation of the Touch layout for the Frame (`drivers\frame_controller\resources\input\frame_controller_remapping.json`, `from_controller_type` `oculus_touch`, priority 95) | right B to the Frame's right B, X and Y; left Y to D-pad left, right and up; left X to D-pad down; left menu to View; each trigger to the trigger and the bumper |
| Valve, "Steam Frame Input" | "The top button of the Oculus Touch controllers (B / Y) will map to all three top buttons of the Steam Frame Controllers (Dpad Left, Dpad Up, Dpad Right / X, Y, B)"; "SteamVR will always use a Frame Controller binding by default" |
| The Frame's OpenXR profile and extension | `/interaction_profiles/valve/frame_controller_valve`, `XR_VALVE_frame_controller_interaction` (Valve's page; both strings in `vrclient_x64.dll`, the profile in `vrserver.exe` too) |
| Another controller's translation that reads the Frame's layout | none: `frame_controller` is no `from_controller_type` in any remapping file under SteamVR's `drivers` and `resources` |

**Checked without a headset:**

| | |
| --- | --- |
| The Frame layout's 24 paths, its profile and its extension in `vrclient_x64.dll` | all there |
| Each path against the driver's `frame_controller_profile.json` | a control of the profile, on the hand the layout names |
| `spidy_xr_lab --probe` on Quest Link's runtime (Oculus) | "Controller layout Touch: taken", "Index: taken"; the Frame's not offered (no extension) |
| The same on Virtual Desktop's runtime (VirtualDesktopXR) | the same |
| Core tests | 285 pass. New: the Touch and Index layouts are the bindings of 0.3.2 in their order; every Frame button is bound once, to a path of Valve's list, on its hand; the View button and the D-pad are the game's Back and D-pad in play and on the game screen |
| Python checks | 113 pass. New: `controllers` and `controller_layouts` of XrData 26, and the session log's line |

**Not checked:** anything under SteamVR (its runtime answers only with a
headset connected, and starting it was not asked for), and anything on a
Steam Frame. Whether SteamVR lists the extension without a Frame connected
is not known either; where it does not, nothing changes for that session.
The hands' pose on the Frame ("a bit janky", October 8, on 0.2.2) is
untouched: no report from a Frame holds its poses.

## One rope for one web, the upper hand bears, the pose stays — current build, not tried in the headset

The user, after the 21:53 session with the build below
(`C:\Spidy-0.2.12\reports\game-vr-20261012-215357.json`, 165 s, `ropes.joins` 14):
"still strong stutters and freezes that werent there. grabbing the web with
other arm still does the shooting sound and when moving hands while dual
holding they switch grab position. also the web shooting pose turns almost
immidiatly to a fist".

**Tests.** 293/293 core checks, the launcher's, 113 Python checks.

| Check | Result |
| --- | --- |
| `WebRopes`: a web shot, then taken by the other hand 0.3 m nearer its end | one rope, the key of the shot; it leaves the taker's wrist; no second rope |
| The same hands moved, no hold changed; then the first hand's rope reeled shorter | nothing changes hands; then the first hand is the nearer |
| The hand that shot it lets go; it shoots anew; the held web is let go of | the rope stays, from the hand that has the web; the new web gets the other rope with its own key; the old rope ends and remembers its hand |
| Two holds on one web seen in one frame | one rope |
| `Swing`: of two hands on one web | the shorter rope bears (`bears()`); with one length for both (no PHYSICAL WEBS) both do |
| The lower hand moved 0.3 m down the web | the body stays (within 2 cm) |
| The lower hand's yank; the upper's; the upper's again | no zip; one; no more |
| Hanging with weight, the upper hand lets go: the lower hand's hold 0.3 m of slack below, or moved 0.2 m further down, or 0.25 m up | the body within 3 cm of where it hung for the next half second, each time |
| `HandPoser`: the web button held 5 s; half let go; let go | the pose whole throughout; still whole at 0.5; gone, and the fist with it, 0.5 s after |
| A hand on the other hand's web, the button going down and held; the same with the trigger as the web button | a fist, no pose |
| A web ball | the pose within 6 frames, whole at 0.43 s, gone at 0.77 s |

Not run in the game, not tried in the headset. That the shot's sound came
with the game's rope is inferred (the rope was all a take did in the game);
whether it is gone needs the headset.

**The session itself.** Still `display_period` 8.33 ms (120 Hz), 69.3
pictures a second submitted, `xr_ms` end_frame 13.1 ms in the median, 7
held rounds; `free_commit_mb` 4822 at the start and 300 at the lowest. The
reports' `xr_runtime.headset`, and frames of 80 ms or more per minute of
immersive play (the last 15 s of a session left out: the headset coming
off), each a time-out in `xrEndFrame`:

| Sessions | Headset | `display_period` | Freezes a minute | Lowest free commit |
| --- | --- | --- | --- | --- |
| Oct 11 12:42, 13:57, 15:24, 16:49 | Quest 3 | 8.33 ms | 0.0, 0.2, 0.6, 0.0 | 6-14 GB |
| Oct 11 18:57, 20:23, 21:45 | Quest Pro | 11.1 ms | 0.0, 0.5, 0.3 (all three at 21:45 came with a jump of thousands of metres: a loading) | 3-4 GB |
| Oct 11 22:18 | Quest 3 | 8.33 ms | 2.2 | 8.3 GB |
| Oct 12 11:38, 14:06, 15:42 | Quest 3 | 8.33 ms | 1.3, 1.3, 1.3 | 5.7, 2.0, 2.6 GB |
| Oct 12 16:46 | Quest 3 | 8.33 ms | 8.0 (stalls through its last half minute) | 2.3 GB |
| Oct 12 16:51 | Quest 3 | 8.33 then 13.9 ms | 0.0 | 1.1 GB |
| Oct 12 21:09, 21:53 | Quest 3 | 8.33 ms | 2.4, 1.7 | 0.3 GB |

At each freeze of the 21:09 and 21:53 sessions the game delivered 21 to 49
new pictures a second in the half second before and after (its usual rate),
the player stood still at six of the fifteen, and `gpu_waiting` was 0 to 2.
So the stream held, not the game; memory does not sort the sessions (22:18
froze with 8 GB free); the headset does, and for the Quest 3 the evening of
October 11 does. Why the Quest 3's sessions changed then is not found:
Virtual Desktop's own log keeps one session and says nothing of it
(`C:\ProgramData\Virtual Desktop\OpenXR.log`: runtime 1.0.10, streamer
1.34.22, HEVC 10-bit, recommended 3072x3264).

## The web-shooting hand; one web in both hands — preceding build, tried in the headset (21:53)

The user, October 12, after the 21:09 session: "lets add hand poses like the
real iconic ones from spiderman on web shootinh. also allow to dual hand the
same strand of web." What each became is in [CHANGELOG.md](CHANGELOG.md).

**Tests.** 292/292 core checks, nine of them new:

| Check | Result |
| --- | --- |
| A press with the free hand 0.3 m up the other hand's web | takes hold (`EventKind::Join`, no attach, no shot); the two webs are one strand; its rope to that hand is 0.3 m shorter (19.1 of 19.4 m); the body does not move; one hand letting go leaves the web in the other. Without PHYSICAL WEBS the same, the rope as long as the first |
| The upper hand pulled down 0.3 m, the lower let go, raised 0.3 m past the other, pressed, pulled down 0.3 m | the body 0.3 m up, then 0.6 m; the second hold's rope 18.8 m |
| A hand 0.4 m beside the web, pressed | shoots a web of its own; with `strandReach` 0 a hand on the web does too |
| Both hands on one web, each yanked in turn | one zip |
| A press the swing keeps (`WebGrab::claim`'s `kept`) with a prop in the aim | catches nothing, then or while held; the next press does |
| `SharedStrand` | the hand nearer the web's end is the first; hands 2 cm apart along it keep their places, 10 cm trade them; the farther hand's part ends on the nearer wrist |
| `HandPoser`, 90 Hz | the button at 0.3: the pose at 0.46, the fist at 0.3; pressed with a web: the pose whole within 0.1 s and still at 0.25 s, gone at 0.55 s with the fist whole; let go: both gone in 0.5 s. A web ball: whole within 6 frames. A hand at 3 m/s: the pose whole; with an enemy marked: none. The trigger as the web button: a fist on the web |
| The pose on Spider-Man's hand (`body::solve`, plain and mirrored frames; from an open hand, over a fist, and half way from the fist) | every finger joint within 0.01 rad of its angle; the two middle fingertips on the palm's side and back toward the wrist; the index and little fingers within 37 degrees of the hand's way; the thumb within 0.03 rad of 57 degrees out to its side, its bones their lengths and within 20 degrees of straight; the wrist on its controller |

The launcher's tests and 113 Python checks in six suites (one new: the rope
telemetry's version 2, with `joins` and `shared`).

**Not run in the game, not tried in the headset.** The XR worker
(game_xr.cpp) runs only with a headset: the tick, the pose's timing against
the real buttons, and the strand's drawing by the game's own ropes
(`native_webs`, the farther hand's rope aimed at the nearer wrist) are
untested there. Whether a game rope a few centimetres long draws cleanly is
not known. `tools/probe_game_body.py --phases rig,ik,eyes --shoots 0,1`
(new option) would show the pose on the hero's mesh; I did not start the
game for it tonight.

## The 21:09 session's "drops": the headset at 120 Hz, not the build

The user, after that session (`C:\Spidy-0.2.12\reports\game-vr-20261012-210952.json`,
build `xr_hash` 40b0de4e, the same as at 16:46 and 16:51): "preformance isnt
great in the last run, was better before (or is it network again)?", and
then: "we didnt have the drops yesterday, was silky smooth".

From the session reports of October 10 to 12 (29 with samples, in three
play folders), immersive samples only:

| Session | `display_period` | To the headset | `xr_ms` wait_frame / end_frame | Frames within 1 ms of the median | Over 16 ms |
| --- | --- | --- | --- | --- | --- |
| Oct 11 20:23 (78f8bdfa) | 11.1 ms (90 Hz) | 86.8 /s | 8.7 / 0.8 ms | 65% | 1.5% |
| Oct 11 21:45 (6e77e5ba) | 11.1 ms | 88.5 /s | 9.7 / 0.8 ms | 80% | 1.0% |
| Oct 11 22:18 (6e77e5ba) | 8.33 ms (120 Hz) | 70.5 /s | 0.0 / 13.1 ms | 45% | 11% |
| Oct 12 11:38, 14:06, 15:42, 16:46 | 8.33 ms | 70-72 /s | 0.0 / 13 ms | 40-55% | 9-14% |
| Oct 12 16:51 (40b0de4e) | 8.33, then 13.9 ms | 71.7 /s | 0.0 / 13.2 ms | 68% | 4% |
| Oct 12 21:09 (40b0de4e) | 8.33 ms | 70.2 /s | 0.0 / 12.9 ms | 38% | 13-15% |

- At 90 Hz `xrWaitFrame` paces the loop and `xrEndFrame` returns in under a
  millisecond; at 120 Hz the wait returns at once and `xrEndFrame` takes 13
  ms: the loop is always behind the display. The split is the same in light
  and heavy scenes (`render_frame_mb` under 8 against 14 and over).
- Network: cable up, no Wi-Fi, 15 held rounds (1.5 s) in the 21:09 session.
- The game's own rate at one spot (the roof at -555, 165, -620, standing,
  eyes at 50%): 54.5 and 55.2 at 90 Hz (78f8bdfa, 6e77e5ba), 56.5 and 57.3
  at 120 Hz (6e77e5ba, 73056b18). No build changed it, nor did the mode.
- It follows `render_frame_mb`: over stall-free 5 s windows, 48-55 a
  second at 6-9 MB and 47-50 at 9-12 MB in each session since October 11
  evening, whatever the build. The 21:09 session spent 35 s at 17-22 MB (a
  swing 80-190 m up, x -16 to 274, z 954 to 1441): 34-39 a second.
- `free_commit_mb`: 5163 at the start, 329 at the lowest (10136 and 5677 at
  11:38). The game's log gives its own commit as 13.4-14.7 GB in both; the
  rest is other programs (the Claude app held 8.9 GB two minutes after the
  session).

Not confirmed: a session at 90 Hz after this. Who set 120 Hz is not known;
Spidy requests no refresh rate (`xr_session.cpp` only reads the runtime's
period).

## One enemy at a time, called strikes, KO — preceding build, run in the headset (16:46, 16:51, 21:09), no word on it yet

The user, after a session with the build below (15:42 to 15:56,
`C:\Spidy-0.2.12\reports\game-vr-20261012-154237.json`; he plays from
`C:\Spidy-0.2.12` since 15:41, a copy of `dist\Spidy-0.2.12`, its modules
identical to that build's): "i had a looot of accidetal targeting and
chaining of enemies where i just wanted to finish one. also its not clear
how directional strikes work or they dont register properly since swinging
from that direction doesnt do anything", and: "maybe finishing an enemy need
some cue since its not clear when they're dead (maybe a light slow motion or
visual)".

**The session's fight** (302 to 631 s; `strike_samples`, one row per strike
opened or ended and per blow):

| | |
| --- | --- |
| Blows | 113: 88 by web strike, 25 by a fist at arm's reach |
| Strikes opened / landed / missed | 96 / 88 / 8, at 64 different thugs (at most 4 at one, 38 thugs once) |
| Strikes from 4 m or more / 7 m or more | 44 / 22 |
| Strikes within 1.5 s of the blow before | 51: 16 at the thug that blow was on, 24 at another (1.7 to 9.9 m off, median 5.6), 11 after a fist's punch |
| Of those 24, the ring on the thug flown to as it opened | 18 |
| All strikes: the ring on another thug / on nobody as it opened | 12 / 3 of 93 |
| 474 to 488 s | 14 strikes at 9 thugs, from 2.1 to 9.9 m, 6 to 10 enemies in the fight |
| Calls / answered / finishers | 27 / 2 / 1 |
| Blows landed while a call stood (the flow at 2 or more) | 58: 42 strikes (no strike could answer), 16 fists (2 answered, 14 not: the module's own count) |
| Blows with the finisher called and the count at 4 | 32, of them 29 strikes |
| The 23 fists' blows sampled, share of their motion toward him | 0.71 to 1.00 in all but a slam straight down; across him at most 0.66 |

So the mark did not hold (it went to the enemy nearest the line of sight
each sample, with 0.04 of a cosine's stickiness), the strike did not follow
it (it took the enemy the fist's motion pointed at best within 37 degrees
and 10 m), and a strike could answer no call. Whether the 16 fists under a
call were meant as hooks and judged too strictly, or were jabs, the report
does not say: their direction is the fist's as it lands, and the plain
punches without a call look the same.

Also in that report, on what the fifth build left open: three "Request load
save type -1, control 128" (15:47:18, 15:53:50, 15:55:24) with no crash and
no minidump, the game closed by itself at 15:56. `steam_guard` is not in the
report (the path for a game that closes by itself left it and `ropes` out;
fixed here), so the guard's counts for that session are not known. Yanks
51, catches 9; moments: blow 2, called 2, air pull 9, perfect dodge 1;
charged leaps 2; scans 3.

**What changed.** `Lock` (web_strike.hpp): the look's mark as before; a
blow, a strike's opening or a web's hold makes it a lock, which holds until
he is out of the fight, 4 s pass with no blow at him, or the player looks
within 20 degrees of another for 0.3 s with the locked one more than 30
degrees aside. `WebStrike::open` takes a `StrikeWish`: with an enemy marked
the strike is his alone, from a fist thrown within 60 degrees of him, or
swung within 60 degrees of the called way with at least 0.15 of its motion
toward him (such a swing need not go out from the body); a swing that would
have opened one at another enemy is counted (`passed_over`); with nobody
marked and the look known, no strike opens. A strike's
blow is judged against the call by the fist that opened it and, when it
answers, delivered along that fist's way. An enemy struck within 3 s who is
up no longer (`game_targets::up`: out of hit points, or in one of the game's
down states) is a knockout: `Strikes` version 3 carries it with `locked`, `swung` and
`strike_called`; the headset letters "KO!" (`appendKnockout`), pulses both
hands (0.8 for 0.09 s) and starts `Moment::knockout` (time at 0.55 for
0.2 s, in 0.07 s and out 0.35 s, no ripple). `appendCue`: a look's ring is
plain and thinner, a lock's has the ticks; the arrow runs from 1.3 radii
outside his middle, on the side the fist comes from, to 0.3 past it.

**Seen.** `tools\preview-marks.ps1 -Scale 6` over the same two captures: the
looked-at ring, the locked ring, the four calls and the finisher's with the
arrow coming in from outside, and the knockout's five poses. Nothing else
of this can be seen without a fight.

**Tests.** 283/283 core checks. New: the lock (a look marks and the mark
follows; a blow locks; a glance of 0.22 s and looking away for a second
take nothing; 0.33 s squarely at another 37 degrees aside moves it; two
thugs 14 degrees apart never trade it; a thug gone frees it at once; 4 s
idle; one beyond 10 m is no look's mark but stays locked), the strike's
wish (unmarked the fist picks as ever; marked, a fist at another flies
nowhere and is counted once a swing; a fist 47 degrees off the marked thug
goes to him though it points better at another; none with him at arm's
length; a hook the called way opens one, and not unmarked, uncalled, the
other way or drawn back; an uppercut and a blow down; the opening fist's
way answers the call), the knockout's mark and its lighter moment. The
mark's test follows the arrow's new place. Launcher tests and 112 Python
checks pass (the strike snapshot at version 3, 328 bytes).

**Not tried in the headset: all of it, and not in the game without one
either** (no probe was run: the lock, the knockout watch and version 3 of
the strikes' record have only compiled and passed their unit tests). Open:
whether `up` calls every knockout and no false one (a live thug the game
puts in a ragdoll state would read as one); whether 0.3 s is the right
dwell in a crowd; false strikes from a wide swing while a call stands.

## The combat marks, printed — preceding build, tried in the headset (15:42)

The user: "lets make our custom combat indicators look better and slicker and
more aligned with this while not being too over the top", with the style
system of Spidy's videos (its tokens: ink `#0b0a14`, paper `#f7f5ee`, red
`#e3262f`, yellow `#ffe14d`, cyan `#12d6ff`, magenta `#ff2e93`; keylines and
hard drops; the misprint, cyan to the left and magenta to the right, wide on
the pose a graphic lands; twelve held poses a second; a graphic lands in
three poses, 30%, 116% and 95% of its size).

**What changed.** `appendCue` and `appendImpact` (`src/web_visual.cpp`) draw
through `Print`: each shape once per plate (the misprint's ghosts when it
has them, the drop, the ink keyline, the colours), its edges a pixel soft.
The marks' inputs (`CueView`, `ImpactView`), when they show and what they
mean are as they were. Sizes: the ring's radius 0.10 of the reach factor
(was 0.13; the finisher's 0.13, was 0.17), the count's height 0.042 (was
0.05; a finisher's 0.06, was 0.075) and at most 2.5 times that far off
(was 3), a burst's star three quarters of the old ring's reach and a
finisher's six tenths (a filled star the old ring's size filled the view at
arm's length: seen in the first sheets and cut down).

**Seen** (`tools\preview-marks.ps1 -Scale 6`, new: the overlay's own vertices
drawn one sample a pixel, source-alpha blended in linear light, over an eye
capture enlarged six times, to that session's eye images of 3072 x 3264,
about 1410 pixels a unit of tangent). Over two captures of the 14:06
session, `game-vr-20261012-140623-eyes\0013-view.png` (a thug standing
2.6 m off, his chest at 337,262) and `0027-view.png` (one at arm's length,
300,400), and with the first also over the sunlit wall behind him:

| | Before | Now |
| --- | --- | --- |
| Target | White ring at 60%, four ticks across it: a scope | Thin paper ring keyed in ink, four ticks in from it, 12 degrees off level |
| Call | White block arrow on a dark disc at 45%, in a cyan ring; it covered his torso at 2.6 m | Paper arrow in a marker's stroke on a cyan drop, cyan ring over an ink track; he shows through |
| Finisher's call | The same in gold | Yellow arrow on a red drop in a red zigzag: told apart by shape |
| Landing | Eased from 135% over 0.12 s, faded in | Three held poses (30%, 116%, 95%), cyan and magenta ghosts on the first two |
| Jab | Pale flash, ring and rays, fading | Yellow star around paper, two poses; a pose of paper lines |
| Called blow | Cyan ring and a second ring | Red star around yellow, a cyan ghost left and a magenta one right |
| Count | Thin pale strokes with hard ends, rising and fading | Yellow round strokes keyed in ink, 8 degrees off level; a red drop for the finisher's |

On the sunlit wall every mark keeps its outline (the paper ring is the one
that needs it: without the keyline it is pale on pale). Enlarged five
times, edges are smooth and no plate shows past a keyline. One fault found
this way and fixed: where a drop's edge ran along its shape's, a hairline of
the drop's colour showed outside the keyline; what lies under a keyline is
now a pixel smaller than it.

**Not seen: the marks in motion, in stereo, or in a fight.** Whether twelve
poses a second reads as print or as stutter in a headset, whether the
arrow's boil is noticed at all, and whether the misprint's edges read as
print or as a lens's colour fringe are the headset's to answer. The marks
are fixed in the world and drawn alike for both eyes, so nothing of this
moves an eye's image against the other's.

**Tests.** 280/280 core checks. Rewritten for the look: the mark over a
thug (every colour one of the plates; an ink keyline on every mark; the
arrow's barbs ahead of its middle for each of the four ways; the ring runs
down in notches; the finisher's ring goes in and out where a call's is
round; a call's three landing poses and the poses its misprint is on; a
pose is held; the arrow boils by 1 to 6% of the ring's radius and the ring
keeps still; the plain ring is paper alone, a hand wide, clear of his
middle), and the burst (a jab's star yellow around paper and growing; its
lines clear of the middle; a pose held a twelfth of a second; a harder
blow's red around yellow with two poses of lines; a finisher's wider with a
third star and three poses of lines; the count above the blow from the
second blow on, popping in, holding, leaving in two poses, never fading).
New: round lettering (a dot at each of a stroke's points, a stroke between,
edges that fade; `TextStyle::round`, which nothing else uses). Launcher
tests and 112 Python checks pass.

## The crash on Quit, the target ring, the leap, the camera's pitch, the air pull — preceding build, not tried in the headset

The user, after three sessions with the build below (13:59, 14:03 and 14:06;
`dist/Spidy-0.2.12/reports/game-vr-20261012-135905.json`, `-140346`,
`-140623`): "game crashes on quit to menu, something is wonky with the
target indicator - enemies can be targeted for punch but no indicator, hold
jump jumps like normal, there is a closed vent i need to go through and cant
interact with, the slowmo trigger for in air pulls feel bad - i dont get
near the enemy like ever, i intened for it be like a pull to me in the air
and have slowmo when they're close enough to punch in the air (like dmc air
combat)".

**The sessions.**

| | 13:59 | 14:03 | 14:06 |
| --- | --- | --- | --- |
| Ended | "Request load save type -1, control 128" (Quit), then `[Crash] Unhandled exception at address 0x00000000c26b53ed` 0.2 s later, in each | | |
| Blows / web strikes' / calls / answered | 0 | 0 | 24 / 13 / 17 / 0; flow at 4 from 28 s to 34 s and from 46 s to 56 s, no finisher |
| Yanks / catches | 0 | 0 | 16 / 0; every yank let go 0.2-0.5 s later, from 2.6 to 40 m |
| Moments | 0 | 0 | `air_pull` 7 |
| Held A (RT + A sent) | 2 | 0 | 1 |
| Scans (hand at temple) | 12 | 0 | 0 |
| Y presses in play; LB + RB; LT + RT | 3 (167.9, 169.6, 209.7 s, on a roof by a skylight); never; never | | |

**The crash.** The same record in twelve game logs from October 11, 18:57 on,
every one a VR session ended by Quit; ten earlier quits (October 10 and 11)
have none. The minidumps of 22:22 (October 11), 11:42 and 14:09 are alike:
access violation reading `0x7ff600000000` in `steamclient64.dll+0x11453ed`
(its memcpy), called from `CUtlBuffer::Put(src, 0x88)` (`+0xce69b0`), under
the IPC stub at `+0x75b1c0`, whose return address on the stack is
`Spider-Man.exe+0x2aa6400`. That is the game's call `mov rcx,[rax]; mov
rax,[rcx]; call [rax+68h]` at `2aa63f7`: slot 13 of the interface its
context at `5d69010` holds, with `rdx` a buffer, `r8d` 0x400 and `r9` a
count: `ISteamUser::GetAuthSessionTicket`. The stub writes its fifth
argument (stack, `[rsp+0xc0]` in its frame) as 0x88 bytes: a
`SteamNetworkingIdentity`. The game sets no fifth argument on this path
(`2aa6317`: with its worker thread made already it jumps to `2aa63d0`); on
the first path `[rsp+20h]` is zeroed at `2aa63c5` for another call. So the
identity is the game's stale stack: `0x00007ff6` over a zeroed low half.

Quits to the main menu in the game without a headset, October 12, each in a
freshly started game loaded with the virtual controller (the input bridge
and the render memory module in, as for every probe):

| With | Result |
| --- | --- |
| nothing more | main menu, alive |
| the settings tab's hooks (`SpidyMenuStart`) | alive |
| the eye views and the stereo game screen running (`probe_game_screen.py --stereo 0.7 play wait:40`) | alive |
| the ray, swing and movement modules, after `probe_air_handoff.py` | alive |
| the guard in, twice in a row | alive; guard: 0 calls |
| the guard in, after 170 s of play | alive; guard: 0 calls |

So without the VR worker the game does not ask for a new ticket on its way
to the menu at all (the guard counted none in three quits), and why a VR
session makes it ask is **not found**. What the guard does when it is asked
is therefore not seen: `game_steam.cpp` hooks the function (found through
the game's own interface pointer; in `steamclient64.dll`, checked), and a
call that returns to `2aa6400` gets a null identity. The session report's
`steam_guard` has `calls`, `from_game`, `cleared` and `last_identity`.

**Charge Jump and the camera** (the build below measured RT + A; this one the
right stick): the game's follow camera at rest is pitched 10 degrees down;
`thumbRY` +1 for 0.6 s took it from -10 to +34 degrees, -1 for 1.5 s to -85
(its stop), +1 for 2 s to +68 (its stop), +0.6 for 1 s moved it 3; it stays
where it is left for 3 s at least; `thumbRX` does not pitch it. Sign and
curve are the yaw's (`CameraLook`), so `CameraPitch` uses its constants.

**Tests.** 279/279 core checks (new: ChargeJump as a charged leap; the
swing's leap and command version 4; CameraPitch; the flow's call that stands
until answered, `Flow::left`, the longer hang; the plain ring), the
launcher's, 112 Python checks.

**Not tried.**
- The snatch (`game_grab.cpp`): it steers a flung thug with the velocity the
  web's own pulls use (`native_bodies::fling`), from the player's position
  and velocity at each input sample. Whether he arrives, how he looks
  floating, and whether the kinetic blow that knocks him into the flight
  (`launchInto`) hurts more than a yank should: the headset.
- The leap in the game: `SwingTakeoff` takes it off the ground as it takes a
  point launch (the event is counted as one), which sessions have used since
  October 5; the leap's own numbers are from the unit test only.
- The mark: it needs `Input::look` (command version 4), which only the VR
  worker sends.
- The vent itself: that the game shows a prompt there once its camera looks
  down is the hypothesis, from the door of October 12's second build.

**The saves.** The probes ran in Profile 1, the user's own (the roof by the
auction house is "Don't Touch the Art", that profile's mission). During the
swing probe the game was suspended while the pad walked the pause menu, and
A landed on MANUAL SAVE: `slot0-s-manual-5.save` appeared (14:32), and the
game autosaved `slot0-s.save` twice more (14:53). With the game closed,
`slot0-s.save` was put back from the 14:20 backup, the manual save moved to
`reports/backups/saves-20261012-quit-crash/made-by-the-tests/`, and the
folder compared equal to the backup.

## Called blows, moments, ropes, Charge Jump, the scan — preceding build

The user, October 12, in one message: slow motion sprinkled into play ("on
attacks set, when pulling an enemy while in the air, on perfect dodges etc"),
"visuals cues and system to and required directional o specific attacks ...
(like in batman arkham shadow)", "holding jump execute Charge Jump", a scan
("ingame r3") by "putting my hand to my temple pressing trigger", no "two
triggers to go to virtual screen mode" but an option in the pause menu, webs
"physical, less stretchy" that "curle around edges (instead of cutting them
off)", "the default weight to 100%", and "disable depth of field sharpening
(allow users to override from settings)". What each became is in
[CHANGELOG.md](CHANGELOG.md).

**Tests.** 277/277 core checks (11 new: a web bends round a building's corner
and comes off it again, and without PHYSICAL WEBS the corner cuts it; a
pulled hand pulls the body 1:1 and gives it its speed, a pushed one pushes
nobody; a snap turn throws nobody; pay-out; ChargeJump; TemplePress; Cues;
called deliveries; SlowMoments; the arrow), the launcher's, and 112 Python
checks in six suites.

**In the game, no headset** (Profile 1, "Don't Touch the Art", level 5; saves
backed up to `reports/backups/saves-20261012-charge-jump/`, none of the slots
changed, the game's `-userprefs.save` put back).

| Check | Result |
| --- | --- |
| A tap of A on the virtual controller (0.1 s), three times | off the ground 0.07-0.09 s after A went down; 2.2-2.4 m up |
| RT + A down together for 1.2 s, RT 60 ms longer | off the ground 0.08 s after A went down, 2.9 m up; with RT still down in the air the game began its own swing (17 m away, 4.2 m lower, 5 s later) |
| RT + A together for 0.4 s | the same jump, 2.1 m; no swing (1.5 m of travel) |
| RT down 0.2 s before A, then both for 1.2 s | off the ground 0.08 s after A; the game's swing again (17 m) |
| A alone held 1.5 s; LT + A; LT led | a jump as A went down each time, 1.95 m, no travel |
| LT + RT led, then A | the game's zip to a point: 9.2 m up, 37 m away |
| RT alone for 1 s on the ground | nothing |
| The game's Skills > Webslinger > Charge Jump | "Hold RT + A while running or standing to build up charge, then release A for a huge leap." LOCKED: no Webslinger skill is bought on this save (0%), three skill points, four skills to buy on the way to it |
| R3 (`rs`) held 0.3 s on the street | the mission's objective panel came up (the scan); nothing else changed |
| `probe_air_handoff.py` with this build's modules: jump, web up, reel, input unfocused 0.3 s, release, game suspended 0.25 s, coast 2 s, pause 3 s | web attached and kept, 0 airborne steps without Spidy's command in every phase, largest change of vertical speed 0.13 m/s, landed; swing and ray stop 0, every entry restored |
| `SpidyRopeData` and `SpidyStrikeData` (version 2) read from that game afterwards | physical on, 0 wraps, 0 cut, 0 steps paid out (the web was shot in the air, over open street); no blows, no call |

So the game's Charge Jump is RT + A by its own words, and a hero without the
skill jumps as A goes down, with RT before it or not. `ChargeJump` therefore
takes two such holds in a row as "no skill" and passes A straight through
from then on (`missing()`), and lets RT go the moment the hero is off the
ground: a second of RT in the air is the game's swing.

**Not found, not tried.**
- The charge itself: no save here has the skill, and I did not spend the
  profile's skill points. Whether RT may come up 50 ms after A, as
  `tailSeconds` has it, is untested.
- The perfect dodge's signal. `SlowMoments`' dodge starts when the game's
  own time scale (`game_time::telemetry().own`) drops below 0.9 within 0.7 s
  of a B that went down with it above 0.95. That the game slows its time for
  a perfect dodge is an assumption from its time system having a channel per
  cause; nothing here measured it. The report's `slow_motion.moments
  .perfect_dodge` counts them.
- A web round an edge in the game. The unit test's building is a box; the
  game's facades have sills and ledges, where `wrap()` may find several
  edges in a row (six at most) or none (then the web lets go as before, and
  `ropes.cut` counts it). The report's `ropes` has `wraps`, `unwraps`, `cut`
  and `paid_out`.
- Everything the XR worker does with these (game_xr.cpp runs only with a
  headset): the arrow's place and size, the moments' look, the temple's
  zone, the VIEW row's switch, the Charge Jump's haptics.
- The game's Sharpness at 1: that it is the slider's lowest is from
  DEVELOPMENT.md's note on this PC's stored values; that the eyes' image
  changes with it is not measured.

Headset check, in this order: (1) swing round a building's corner holding
the web, and back; (2) hang from a web and pull your hand down; (3) walk off
a roof holding a web shot from where you stood; (4) hit a thug twice and
follow the arrows through to the gold one; (5) hand to temple, trigger; (6)
pause, Settings > SPIDY VR > VIEW; (7) hold A on the ground.

## No hand takes a body; web balls web what the game webs up — preceding build, not tried in the headset

The user, October 12, after a session with the build below
(`dist/Spidy-0.2.12/reports/game-vr-20261012-113823.json`, 225 s): "bodies
are still targetable by hands (for webbing i guess), they shouldnt be. after
entering the door there are turrents that the game tells me to press rb
repeatedly to web but it doesnt work"; and while it was looked into: "can you
implement a global solution so we wont need to redo every enemy?"

**The session.** The door opened at the third Y press (82.7, 83.3 and 87.2 s,
the fight over at 67 s; `look_error` +5 to +7 degrees, `look_turns` 52 in
all). In Fisk's office from 145 s: 135 web balls fired, `collisions` 135,
`webbed` 0; LB + RB held for 2 s at 153 s; the checkpoint loaded again at
160 s; the session ended at 225 s.

**The turrets in the game without a headset** (profile 2's checkpoint in the
office; scratch scripts over `probe_combat.py`'s `World` and the virtual
controller; the hero's hit points kept up from outside):

| | |
|---|---|
| What they are | two `EnemyTurret` actors, at (-604.1, 172.1, -676.1) and (-618.3, 172.1, -661.9): no bot mover manager; `Health` 3.0; `StatusEffectTrackerWebbed` 0 of 30; `PhysicsComponent`, `BreakableComponent`, `AimModHeroYankState` |
| The game's RB, tapped | "Tap RB repeatedly to web up the turret": the tracker 10, 20, 30 of 30 in three hits, the hit points untouched; the turret wrapped in web, the game's "LB·RB" marker on it and "Hold LB + RB to yank and throw turrets" |
| How long | the webbing full for 4.3 and 4.4 s, then 0; balls in the 7 s after that added nothing, 10 s after it they did |
| LB + RB held, its camera on the turret | the turret left its base 1.56 to 2.61 s into the hold, went round the hero and was gone 4.9 to 5.0 s in (eight times, from 13.5 to 17.8 m) |
| From further off | nothing from 22.2, 23.9 and 27.2 m |
| With the hero hit during the hold | nothing (hits 1.27, 1.51 and 3.82 s in); with no hit, the yank, twice in the same game minutes later. Seven earlier holds that came to nothing were made with the other turret firing; the hits were not counted in them |
| The hero turned 66 degrees off the turret | the yank all the same |

Not Spidy's doing, each checked by itself: the game's yank works with
Spidy's ray and swing modules running and fed (the turret off its base
2.61 s in), with the web shooter's too (1.57 s), and after Spidy's balls
have been fired (1.56 s). For an hour the holds that came to nothing looked
like Spidy's; they were the other turret's fire.

**Changes.**

- `include/spidy/game_targets.hpp`, `src/game_targets.cpp`: `up` (a bot in
  play with hit points left and in no state for one who is down);
  `fighting` is an enemy who is up; `foe` is gone. `Kind::device`: an actor
  that is no bot and has a `StatusEffectTrackerWebbed`, its marker component
  that tracker, found by the component and not by a class's name;
  `standing` (a device with hit points left where it has any).
- `src/game_grab.cpp`: the webs, the throw's aim and the blow of a thrown
  thug take the `fighting`. `src/game_punch.cpp`: a fist lands on a bot who
  is `up`.
- `src/game_shooter.cpp`: its watch has the devices; the aim takes one that
  stands, at its transform, within 1.5 m of the line (its housing; with
  0.6 m the line of sight to its middle counted the housing as a wall); a
  ball that strikes one deals the webbing and no damage; never the player's
  own actor.

**The build in the game** (the same office; Spidy's ray, swing and web
shooter modules running):

| Check | Result |
|---|---|
| The hand's own pulls, aimed 4 degrees beside a turret 25.8 m off (the first build of the rule, the turret by its class) | 4 fired, 4 `targeted`, 4 `resolved`, 4 `webbed`: 11, 22, 30 of 30; hit points 3.0 |
| Test shots at the other, 20 m off | 3 `webbed`: 11, 22, 30 |
| With the turret by its tracker (this build): test shots at both, 15 to 17 m off | 4 and 3 balls, 30 of 30 each |
| Then LB + RB, the game's camera on the nearer one, 14.8 m off, the hero 66 degrees off it | the turret left its base 1.56 s into the hold; no hit on the hero meanwhile |
| A capture 0.5 s into a hold on a turret Spidy had webbed | the web on it, and the game's "LB·RB" marker |

**Tests.** Core 267 (the thug's test has the knocked out for fists and
webs, and a device; the class test, that no class makes one), launcher 23,
Python 111.

**Not shown.**
- Anything in the headset: that a ball aimed by a real hand reaches a
  turret, and the hold on the right stick's click (the worker's part).
- The webs' and the fists' new rule in the game: no thug was knocked out
  with this build running. The rule is the one the strike has had since the
  second build, with the same reads.
- What else in the game carries a webbing tracker without being a bot, and
  what a ball's webbing does to it. The player's own actor is left out.
- Whether a throw needs LB + RB held to its end: every hold here went on
  until the turret was gone.
- The saves: as the user left them (`reports/backups/saves-before-turret-check-20261012`
  is that state). The Steam client lost its connection during the checks
  and asked "Unable to Sync ... Play anyway"; the user answered it.

## The thugs a punch goes to are the ones in play; Y takes what the player looks at — preceding build, tried in the headset

The user, October 12: "in combat "dead" enemies and random objects are being
targeted by the lockon system"; then "in the same section there was also a
door that i needed to go through to progress but i couldnt interact with at
all (maybe because i started walking on it when coming close enough?"; and,
from footage of the mission on a PS4: the door shows a triangle prompt when
the player comes close.

**The session** (`dist/Spidy-0.2.12/reports/game-vr-20261011-221822.json`,
248 s: the hall on the 170 m floor of Fisk Tower in "The Main Event", from
the checkpoint at the fight's start):

| | |
|---|---|
| The fight | combat camera from 25.2 s to 90.5 s |
| Both fights of the session | 14 strikes opened and landed, 35 blows; `punch.unnamed` 10 (blows to a bot whose `BotAttackResponseManager` names no `AwarenessDB`); `unflown` 8; at the end, the second fight over, `strikes.enemies` 9 |
| The door | reached at 107 s (17 s after the fight); 25 Y presses up to 155.9 s, the last seven standing at (-589.14, 170.0, -646.20), 0.40 m from the door's face; three clicks of the right stick (LB + RB); `interact_steps` 0 (no press held 0.3 s) |
| On the way to it | a zip onto the wall above the door (103.6 s, 5 m up); the swing stood the player on the door four times (`mount` at 109.1, 112.6, 128.6 and 142.9 s); his last steps went (-0.89, 0, 0.45), along the door's face, which runs (-0.71, 0, 0.71) |
| The game's time | 1.00 of real time from 85 s to 120 s (the mover's steps against the clock) |
| After | Start, Restart Last Checkpoint (173.6 s), the fight again to 236 s, the game closed at 244 s |

**In the game without a headset** (`probe_menu_pad.py`: profile 2 on the
profile screen, Continue; scratch scripts over `probe_combat.py`'s `World`;
the game window must stay in front, or the game ignores the controller). The
fight opens with a hint card and three prompts that stop the game's time
(Y: web strike, B: dodge under, X: attack from behind); test blows sent
before they are through leave the hero unable to walk.

*The bots*, as Spidy's scan lists them at the fight's start:

| Bots | Count | State machine | Attack responses' `AwarenessDB` (+0xa4) | Hit points | Where |
|---|---|---|---|---|---|
| Thugs in the fight | 5 | a state (`BotStateGotoGame`, `BotStateAim`, ...), in use | resolves | 50-80 | in the hall |
| Thugs waiting | 17 | no state (+0x70 null, +0x80 0) | names nothing | 60-80, three at 6 | stacked where they will appear: 3 at (-554.4, 170.0, -612.1), 1.4 m from the player's start; 1 at (-555.8, 169.9, -613.5); 1 at (-557.7, 170.0, -611.7); 4 at (-549.4, 170.0, -618.7); 8 on the floors at 152 m and 133 m |
| A civilian, waiting | 1 | no state | none | 100 | the floor at 152 m |

None of the waiting ones is drawn. All 23 have an `AwarenessDB` component.
A thug knocked out (a test blow of 500, `SpidyPunchTest`): `BotStateDeathAnimGame`,
then `BotStateRagdollGame`, 0.0 hit points, still set up. So the old rule
(an enemy bot whose health does not read 0 or less) had 22 targets at the
start and 17 with all five thugs down, 9 of them within 13 m: the session's
count. With the game's own punches as the last blows (hit points set to 3,
the controller's X) the thugs end the same way.

*The fight's end.* Ended by test blows alone (twice) the fight finishes as
with the game's own blows: the call to Yuri Watanabe begins about 3 s after
the last thug falls, and the door then shows its prompt, whether the player
walks to it during the call or after. Spidy's blows were not the cause.

*The door* (its face through (-589.2, y, -646.7), normal (0.71, 0, 0.71);
the hero 0.4 m from it at (-588.9, 170.0, -646.4), the fight over):

| The game's camera | The hero | Prompt | Y |
|---|---|---|---|
| on the door | faces it | "Y Open Door" | |
| turned away with the right stick | faces it | none | a tap: nothing |
| 30 degrees off, either side | faces it | "Y Open Door" | |
| 50 degrees off: one side / the other | faces it | none / "Y Open Door" | |
| 70 degrees off, either side | faces it | none | |
| on the door | turned away (the stick pushed 0.13 s) | none, the objective's marker instead | two taps: nothing |
| on the door | turned away; Y with the left stick toward the door at half tilt for 0.15 s, Y down 0.6 s | | the door opened: the scene beyond it began |

In VR the game's follow camera is not the view (the bridge only reads it,
`game_bridge.cpp`; the tracking rig takes its forward once and for the walk's
axes), and `Mapping::gameplay` sent no right stick: nothing turned it.

*The camera under the right stick* (the bridge's `before` matrix, row 2 its
forward): +X turns it toward cross(forward, up). At full tilt 95.6 degrees in
0.6 s and the coast after it, 180 degrees a second from 0.2 s on; at 0.76
tilt 22 degrees a second, at 0.6 nine, at 0.5 three. Steered with the stick
as `CameraLook` sets it, from 170 degrees off: within 4 degrees in 1.43 s,
and at rest there (three runs: -3.8, -3.8, +3.8 degrees left).

*The hero under the left stick* (the player's matrix, row 2 his forward):
0.3 tilt for 0.4 s: no turn, no step. 0.4 for 0.4 s: 176 degrees round by
0.33 s, 0.38 m moved. 0.5 for 0.15 s: round by 0.55 s, 0.13 m moved.

**Changes.**

- `include/spidy/game_targets.hpp`, `src/game_targets.cpp`: `inPlay` (a bot
  with a state in his state machine; one without a state machine counts),
  `foe` (an enemy in play), `fighting` (a foe with hit points left where they
  can be read, in none of `BotStateDeathAnim`, `BotStateRagdoll`,
  `BotStateWallStuck` or a class derived from them, by the game's type
  information). A health component is known by deriving from `Health` or
  `BotHealth` too, not by the two vtables alone.
- `src/game_punch.cpp`: fists pass through a bot not in play; a strike's
  targets are the `fighting` ones. `src/game_grab.cpp`, `src/game_shooter.cpp`:
  webs, the blows of a thrown thug, web balls' webbing and the pull's aim
  take `foe`s.
- `include/spidy/game_pad.hpp`: `CameraLook` (the right stick's X from the
  angle between the head's level direction and the camera's forward: it
  starts beyond 12 degrees and rests within 4; the cube root of the turn
  wanted above 0.45, full from 45 degrees); `Walk::look`, the virtual
  controller's right stick X in `Mapping::gameplay`. `InteractStep`: a press
  out of a fight points the stick where the player looks for 0.15 s and keeps
  the game's Y down 0.6 s at least (`pressed`); held 0.3 s it walks as before.
- `src/game_xr.cpp`: the worker steers the camera all through VR play, not on
  a wall the player stands on; XrData is version 24, 832 bytes (`lookError`,
  `lookTurns` after the first build's `heals` and `zips`; a sample's
  `look_error`, `look_turns`).
- `tools/probe_combat.py`: `--target` takes several records.
- `docs/PLAYERS.md`, the launcher's CONTROLS page, `tools/launch-game-vr.ps1`,
  `README.md`: look at the thing and press Y.

**The build in the game** (`probe_combat.py --phases strike,strike` at two
chosen thugs, one waiting 4.7 m off and one in play, after the fight's
prompts; `reports/combat-probe-lockon.json`): the strike's `enemies` was 5 in
all 111 samples, with 5 thugs in play and 9 waiting within 13 m. The fist
thrown at the waiting thug opened a strike that ended 3.6 m from him (a
strike at him stops 0.95 m from his axis): it went to a thug in play that
way.

**Tests.** Core 267 with the first build's (3 new: a thug waiting or knocked
out; the camera's turn, against a camera that answers as the game's; a tap's
turn and its Y; the walk's test counts the turn), launcher 23, Python 111
(`look_error`, `look_turns`; 832 bytes). The two builds of the day met in
`game_pad.hpp`, `game_xr.cpp`, the tool and its test: the player's right
thumbstick pushed down or up is the first build's (`StickPush`), the virtual
controller's right stick X this one's; XrData has both builds' fields.

**Not shown.**
- The camera's turning and the turn on Y in the headset: the worker's part
  needs one. The stick's signs and both matrices' rows were measured in the
  game; that `headPose`'s forward is the look the session's `head` rows show
  is read from the code.
- Whether the camera turns as fast with another camera sensitivity in the
  game's settings, and what the combat camera does with the stick in a fight.
- The camera's pitch: nothing turns it. A thing far above or below the
  hero's chest may still be out of its view.
- Whether the game's LB + RB prompts ask for the same (the pull has the
  camera now, not the turn).
- `BotStateWallStuck` (a thug webbed to a wall) was not seen; it is taken by
  its name.
- A press of Y held 0.6 s where the game tells a tap from a hold.
- The saves: profile 2's is as it was before the checks; the save the game
  wrote past the door is in `reports/backups/saves-after-fisk-door-20261012`.

## Heal and LT + RT on the right stick; OpenXR Toolkit switched off for the game — preceding build, not run in the game

The user, October 12: "check out the bugs section in discord, map out the
bugs you think we have enough data to handle", then "fix the things you know
how." The 31 posts of #spidy-bugs were read with their logs, report zips,
screenshots and clips; four had a known cause.

**Heal and vents.** `game_pad::fromControllers` gave the game, in VR play,
Start, Back, A, B, Y, LB + RB and the left stick: no D-pad and no triggers.
The game heals on D-pad down and zips to a point on LT + RT. Players in
#spidy-bugs and #spidy-help: "game gets stuck when the game forces you to
heal, and you cant heal" (0.3.0), "How do I get past the heal part", "cant
use Vents", "Stuck in the elevator. no buttons open vents" (Pico 4; "I'm on a
quest and it's the same"), "How do you get in vents?".

**What was built.** `game_pad::StickPush`: the right thumbstick pushed
straight down is `Walk::heal` (the game's D-pad down, whenever the
controllers play VR), pushed straight up `Walk::zip` (its LT and RT at 255
together, only while no web carries the player, as the dodge and Y). A push:
from rest (within 0.5), to 0.75 or more with no more than 0.45 aside, held
0.15 s; it holds until the stick is back within 0.5 or goes over to the other
side. A stick deflected aside first, or held in from the game screen, pushes
nothing until it has been at rest. The right controller ticks (0.2) as either
begins. `XrData` version 23 (824 bytes) counts both: a sample's `heals` and
`zips`. The right stick's up and down had no use in VR play before
(`game_tracking.cpp` reads its left and right for turning only).

**OpenXR Toolkit.** A player (pepie87, Pimax Crystal Super, RTX 5090),
October 9: "sony logo comes up and gone", "before i tried 5 times", "instead
of pimax openxr runtime, I used steamvr and it works", "seems to be
openxrtoolkit which the launcher doesnt like". His game log's last lines, 6 s
after the start:

```
[Crash] Unhandled exception at address 0x000000003c1041ca (code: 0xe06d7363): "Unknown exception"
```

The address is outside the game's module. The OpenXR loader puts every
implicit API layer into the process that creates an instance, which with
Spidy is the game (`game_xr.cpp` creates it there). `LayerSwitches`
(`game_xr.cpp`) reads the registry's `SOFTWARE\Khronos\OpenXR\1\ApiLayers\
Implicit` (machine and user), and for each enabled manifest that
`xr_layers::switchOff` finds on the list (`XR_APILAYER_MBUCCHIA_toolkit`
alone) sets the manifest's own `disable_environment` variable in the game
until the instance exists. A variable the player set already is left alone.

**Checks.**
- 264 core checks pass (2 new). The stick: the time a push begins; a push
  eased a little; up and down; a flick; a turn and a turn rolled round the
  rim; a diagonal; down swung over to up; a push slid into a turn; the game
  screen and a stick held into play; bad readings and bad frame times. The
  layers: OpenXR Toolkit's manifest as its repository has it today
  (`DISABLE_XR_APILAYER_MBUCCHIA_toolkit`); Virtual Desktop's and ReShade's
  as this PC has them stay on; a layer that only mentions the Toolkit; a
  manifest without a variable, and one with a variable no process could set;
  a key's text as a value; a cut-off file.
- 111 Python checks pass (`xr_snapshot` reads `heals` and `zips`), the
  launcher's checks and the graphics test.
- `spidy_stereo_probe.dll` loaded outside the game exports `SpidyXrData`
  version 23, 824 bytes.
- The switch, on this PC outside the game: `LayerSwitches` as the source has
  it, compiled into a harness and given ReShade's layer (installed here) for
  the Toolkit's (not installed). It set `DISABLE_XR_APILAYER_reshade_1`;
  `spidy_headset_probe.exe` started under it with `XR_LOADER_DEBUG=all`
  logged "Implicit layer C:\ProgramData\ReShade\ReShade64_XR.json is
  disabled" where the probe alone had tried to load it, and loaded Virtual
  Desktop's layer both times; with the shipped list nothing on this PC was
  switched off; afterwards the variable was gone.

**Unproven.**
- Nothing ran in the game or the headset: the game was not started.
- That the game heals on the D-pad down it gets, in the forced heal of the
  first mission and elsewhere.
- That the vent of the first mission takes LT + RT: the prompt is LT + RT as
  remembered, and was not looked at in the game. What the game does with
  LT + RT while no point is in view (its aim camera would put the game
  screen in the headset for as long as the stick is up).
- That a game with OpenXR Toolkit installed starts now: no PC here has it.
- Steam Input (a player's buttons dead in VR, keyboard keys in the game's
  prompts, ended by turning Steam Input off for the game): not reproduced,
  cause unknown, nothing changed but the warning's text and PLAYERS.md.

## Fighting across the room: web strikes, flow, blows that are felt — preceding build, tried in the headset

The user, October 11 evening: "check how Batman: Arkham Shadow combat works
and reimagine our current combat with it in mind build v1 for me to test";
while it was built: "i think our swinging and acrobatics could allow for airal
combat. also punches have zero feedback now and feel bad"; after a session
with the build: "i like the direction".

**Arkham Shadow**, as its coverage describes it (Road to VR's hands-on and
review, UploadVR's review, MIXED's preview, the abstract of the GDC session
"Punching Across the Room in 'Batman: Arkham Shadow'"; the talk itself was
not readable): a punch thrown at a targeted enemy slides the player to him
while time slows; follow-up blows are asked for in given directions; a
warning icon is countered by swinging an arm toward it; a cape gesture stuns
armoured enemies; the player vaults over some; a combo counts up to special
moves; harder punches score more and flash.

**Why punches felt like nothing.** A landed punch was one haptic pulse of
25 ms at 0.5 + 0.5 x strength (`XrRuntime::haptic`) and nothing else, and the
game answers kMelee with kFlyBack, kSuperFlyBack or kAirborne by
`BotStateHitReactGame` alone (the October 8 table below). In the user's
20:20 session that day 29 punches landed, strengths 0.07 to 1.0. In the 14:49
session both hands moved at 4.4-4.5 m/s with `busy` set and landed nothing:
an attached web kept its hand from punching, and a clenched fist shoots one.

**What was built** (`include/spidy/web_strike.hpp` holds every number with
its reason; `src/game_punch.cpp` runs it in the game, `src/game_swing.cpp`
flies the player, `src/game_xr.cpp` gives the feedback):

| Part | How |
|---|---|
| Web strike | A free fist at 2.6 m/s or more relative to the player, going out from the base of the neck (cosine 0.3), for 40 ms, within 37 degrees of an enemy 1.3-10 m from the chest with nothing fixed in between (one ray, the last metre his own cover): the player is flown to 0.95 m from his axis at 26 m/s, no hop shorter than 0.12 s, and the opening fist's blow (no weaker than strength 0.5) lands on arrival. Not within 0.35 s of the hand's web letting go, nor within 0.3 s of its web button going down; one per swing. The swing's webs are let go |
| The flight | `game_punch::fly` each step of the player's mover, in real time: the request is the velocity over the game's clock scale, capped at the movement module's 65 m/s. On the ground too, where the swing otherwise leaves the player to the game. A flight more than 0.6 m up from a standing player waits for the game's jump (`SwingTakeoff`), 0.7 s at most. It ends 0.25 s after it should have arrived (the blow lands within 1.8 m), or after 0.3 s without a step |
| Along the web | A hand whose web holds an enemy bot strikes him alone, within 66 degrees; the grab lets go |
| In the air | After a blow landed off the ground and off a web the player is held still for 0.45 s, 3 s in all between two landings |
| Delivery | A blow of strength 0.85 or more, an uppercut, a blow at a thug in `BotStateFlung` and a finisher go out as kKinetic with kFlyBack, KnockbackAmount 10 and ImpactImpulse 30 (the web grab's knock), and his flight is given a velocity (`native_bodies::fling` for five samples once he is on it; asked for once after 0.4 s if he is not): heavy 9-14 m/s along the blow and 3 up, uppercut 8.5 up, in the air 4.5 up, down at a thug in the air 24 down with 1.6 times the damage, finisher 18 along and 5 up with 2.5 times. Other blows stay kMelee; a firm one down onto a standing thug is a kKnockdown |
| Flow | Blows within 3.5 s count up; damage x (1 + 0.1 per blow, 1.5 at most); every fifth a finisher |
| Feedback | Haptics 60-140 ms at 0.6-1 for a plain blow, 180 ms for a heavy one and 320 ms for a finisher with the other hand too; the game's time at 0.1 for 30-100 ms (0.2 for 0.4 s for a finisher), easing back over 0.14 s (`HitStop`, the smaller of it and slow motion); a burst at the blow (`appendImpact`: flash, ring, rays, the count from the second blow on); a sound through `PlaySound` from memory (`impactWave`: 0.12-0.42 s, peaks 0.42-0.86 of full scale, and a 0.22 s rush for a strike) |
| A standing hand's web | A hand is busy when its web holds a target or carries the player (`owned`, or not grounded); a web hanging from a wall beside a standing player no longer keeps the hand from punching |
| Targets | Enemy bots only, and not one whose health component (BotHealth `3839e98`, else Health `3839e08`, a float at +0x88) reads 0 or less |

**The user's session** (`dist/Spidy-0.2.12/reports/game-vr-20261011-213953.json`,
a fight on the ground; the report's new `strikes` and `strike_samples`, and
its swing samples):

| Measure | Result |
|---|---|
| Strikes opened / landed / ended without a blow | 13 / 12 / 1 (opened 1.45 m off, it never took a step and ended 0.8 s later) |
| Blows | 21: 12 strikes' and 9 fists'; 2 heavy, 2 finishers (108.2 and 77.0 damage asked), no uppercut, no blow at a thug in the air, no slam |
| Flow | reached 7 twice; finishers at the fifth blow both times |
| The flights, from the swing samples (12 driven stretches, all on the ground) | 0.38-7.40 m in 0.15-0.63 s; height changed by -0.11 to +0.04 m; no web attached |
| Their speed | 9.7-11.7 m/s over the six longest (3.6-7.4 m), with 65 m/s requested: the cap. 26 m/s was designed |
| Why | The mover's step there was 3.4-3.7 ms at about 57 steps a second (0.197 s of mover time per real second), where a swing's flight in the 20:20 session stepped 14-15 ms (real speed 0.97 of nominal, 74 pairs). The game's clock scale as `worldScale` read it was about 0.2 too (requests of 18-49 m/s for flight velocities of 3.7-9.6), so the request was the velocity times five, up to the cap. `slow_motion` in the report: game scale 1.0, no slow motion used |
| Thugs sent flying | 4 blows asked for a flight; `flown` 0, `unflown` 4: none was in `BotStateFlung` within 0.4 s |
| Enemies alive within 13 m | 14 at the first strike, 9 at the last |

**Checks.** 262 core checks (19 new): a strike across the room and where it
stops, its time and speed; what opens none (slow, wide, short, far, busy, a
hand that just let go, a hand drawn back, three fast samples, a web button
just pressed); the thug pointed at of two, the nearer of two in line, none
behind a wall; the held thug alone; one strike per swing; a hop's minimum
time; a flight that cannot arrive, a target gone, a cancel, a player who takes
no steps; waiting for the jump; up to a thug in the air and the hang, its
restart and its limit; a dive; the flow's count, window, finisher and damage;
each kind of blow's delivery; the hit stop's hold, ease, finisher and
overlap; the sounds' WAV headers, lengths, peaks, attack and ends; the burst's
plane, spread, colour, count and fade; rejected tunings; and a spent swing
(`Punches::spend`). 111 Python checks (1 new: `strike_snapshot`, with a torn
read). `tools/probe_combat.py` has new phases `strike`, `heavy` and `launch`
for the game without a headset.

**Unproven.**
- Nothing ran in the game without a headset: Steam was offline (its bar read
  "NO CONNECTION") and stopped the launch with "Unable to Sync ... Play
  anyway / Cancel", which was left for the user. The new probe phases have
  never run.
- Uppercut launches, blows at a thug in the air, slams, the hang, and a
  strike that leaves the ground by the game's jump: none happened in the
  session.
- Whether a thug sent flying flies: none was seen in `BotStateFlung`. Each
  such blow asked for 49 damage or more (a street thug had 50 health in the
  October 9 probe); what a thug the blow knocks out does was not looked at.
- What the short step of a standing player's mover is (3.4 ms against a
  frame's 18), and whether the clock scale of about 0.2 is the eyes' own
  frames. Until that is known a strike on the ground goes at about a fifth of
  65 m/s.
- How the bursts, the hit stop and the sounds look, feel and sound: the user
  said only "i like the direction". Comfort of the flight at its designed
  speed is untried, since it never reached it.

## No session on a Windows in another language (0.2.13) — preceding build

A player (pepie87, Pimax Crystal Super), October 11, in #spidy-bugs: "2.13
bug, didnt happen with 2.12", "wont start". Their console log ends, after the
headset probe's lines:

```
Exception in thread Thread-5 (_readerthread):
UnicodeDecodeError: 'utf-8' codec can't decode byte 0x84 in position 802: invalid start byte
  File "tools\run_game_vr.py", line 1122, in session
    links = network_links()
  File "tools\run_game_vr.py", line 225, in network_links
    for block in re.split(r'^(?=\S)', addresses, flags=re.M):
TypeError: expected string or bytes-like object, got 'NoneType'
```

**Cause.** `network_links` (the fourth build's Wi-Fi check) ran `netsh wlan
show interfaces` and `ipconfig` with `subprocess.run(..., text=True)`. The
launcher starts Python with `-X utf8`, so their output was decoded as UTF-8;
they print in the console's code page (0x84 is `ä` in code pages 437 and
850). `subprocess`'s reader thread raised, `run` returned `stdout=None`, and
`re.split` raised on it. Line 1122 is before `starting the game`.

| Check | Result |
|---|---|
| A command printing `b'   L\x84nklokal IPv6-adress'`, read with `text=True` under `python -X utf8` (3.12.10, as packaged) | the reader thread's `UnicodeDecodeError`, `stdout` `None`: the player's failure |
| The same bytes read as bytes and decoded `'oem'` | `'   Länklokal IPv6-adress'` (this PC: OEM 850, ANSI 1252) |
| New check `command_text` on a command printing 0x81 (no letter in UTF-8 or in code page 1252), `tests/game_vr_protocol_tests.py` | passes with and without `-X utf8`; 0.2.13's own read of that command gives `None` in both |
| New check `test_a_probe_answer_with_a_byte_that_is_not_utf8_is_still_read`, `tests/runtime_protocol_tests.py` | on 0.2.13's tools: `AttributeError: 'NoneType' object has no attribute 'strip'`; passes now, with and without `-X utf8` |
| `network_links()` with this PC's real `netsh` and `ipconfig` under `-X utf8` | cable found, no Wi-Fi, 714 characters of `ipconfig` read |
| The six `*_protocol_tests.py` suites | 110 checks pass (2, 50, 27, 5, 13, 13) |

**Not checked:** a session on a Windows in another language (none here), and
that both commands print in the OEM code page on every Windows. The second
no longer matters for starting: a byte the code page lacks becomes `?`. The
packaged tools read no other program's output (`vr_launcher.py` starts Steam
and reads nothing). `tools/probe_link.py`, a developer's probe that is not
packaged, still reads the same two commands the strict way.

**Left as it was:** `network_links` matches English words (`State :
connected`, `Ethernet adapter`, `IPv4 Address`), so the warning is silent on
a Windows in another language. Asking Windows itself (`GetAdaptersAddresses`,
the WLAN API) would not depend on the language.

## Cutscenes in stereo on the game screen — preceding build

The user, October 11: "lets keep cutscenes in a virtual screen but with proper
3d stereoscopic depth".

**Design.** Without gameplay the headset showed the game's presented frame on
one quad (both eyes the same image). A scene the game plays there now gets
the game's own camera twice: eye command mode 3 (`native_eyes::Stereo`,
`include/spidy/native_eye_frame.hpp`), the two eye views the engine's camera
set apart along its right axis, each lens shifted the other way
(`stereoEyes`, `placeEyes` in `src/stereo_probe.cpp`). The worker shows the
pair on the same quad twice, the left image for the left eye and the right
one for the right (`XrRuntime::presentation`'s `stereo`,
`XR_EYE_VISIBILITY_LEFT` / `_RIGHT`).

The numbers, for a viewer whose eyes are `i` apart, a screen `W` wide and `D`
away, a lens of half width `t` (tangent) and the depth setting `d`:

- each lens is shifted by `t i / W`: a point infinitely far away lands `i / W`
  of the image's width apart in the two images, `i` metres on the screen, so
  the eyes look at it in parallel;
- the cameras stand `2 t (d D) i / W` apart, at most `i`: what is `d D` from
  the camera lands on one spot of both images and shows at the screen's
  distance, and a point `z` from the camera shows `z / d` from the viewer;
- a lens narrower than `W / (2 D)` (0.64 for the medium screen) gets cameras
  closer together than the eyes: its scene shows larger than life, `W / (2 t
  d D)` times. A wider lens keeps them an eye distance apart and the screen's
  plane comes nearer.

`vr_settings::screenDepths` gives `d`: LOW 0.5, MEDIUM 0.7, HIGH 1. The
viewer's eye distance is the tracked eyes' (`eyeDistance`, 50 to 80 mm, else
64).

**When.** `scene` in `src/game_xr.cpp`: the game screen is up, GAME SCREEN 3D
is not OFF, there is a player, and the game's last frame took game time
(`SceneClock`, `game_time::Telemetry::gameFrame`, the double at `0x7a7fbd8`).
The eye views start with the first scene if no gameplay came before it. The
screen shows the game's frame until the newest pair is a stereo one, and
again when the pairs stop.

**The HUD and subtitles.** The eyes get the HUD's panel and, on it, the
second movie (subtitles, prompts, markers). `placeScreenHud` moves the panel
toward the camera until its face is at the convergence distance, smaller by
as much, so both eyes see it in one place, on the screen. The stereo screen
shows the panel whatever the HUD setting. The worker shows a scene in stereo
only while the panel went onto its screen within 200 ms
(`native_hud::onScreen`), and gives a scene's eyes up after a second without
it (`ScenePanel`): without the panel the second movie stays in the game's own
frame, and that frame is shown.

**In the game without a headset** (`tools/probe_game_screen.py --stereo 0.7
play pad:start wait:2 paused pad:start wait:2 resumed`, the rooftop save, the
window 3840 wide; `reports/cutscene-3d.json` and its images). The probe sends
mode 3 as the worker does (eyes 64 mm, the medium screen) and matches patches
of the two eye images:

| | Expected | Measured |
|---|---|---|
| The far shore and sky | 76.8 pixels (2% of the width) | 73 to 77 |
| Spider-Man, about 3 m from the camera | 76.8 (1 - 1.75 / 3) = 32, more for the roof behind him | 46 to 53 |
| The HUD's corners, a subtitle, a prompt | 0 | 0, 0, 0, -1 |
| The panel's distance (`hud_distance`) | 1.75 m | 1.75 m |

New pairs 65 to 75 in each 2 s phase (the window at 3840 x 2400 ran at 31
frames a second), the panel placed in 68 of 69 frames of play and the second
movie drawn on it in 66.

Two findings of the first run changed the build:

- **The pause menu is no scene, and the time system does not say so.** With
  the pause menu open the `TimeScaleSystem` went on updating, 61 times a
  second at scale 1.0, as in play. The first build read "time runs" from
  those updates and would have sent stereo commands in the pause menu, with
  the menu moved onto a panel the game does not draw there: the menu was in
  neither the eyes nor the window (first run's `paused` images). A frame's
  game time (`0x7a7fbd8`) is 8 to 32 ms in play and exactly 0.0 with the menu
  open: the gate reads that. And the second movie goes onto the panel only in
  a frame whose panel went onto the stereo screen: the game's HUD follower
  does not run in the pause menu (`game_frames` 0), so the menu stays in the
  game's frame whatever is sent (second run: `layer_frames` 0, the window
  shows the menu).
- **The panel's face is behind its position.** Placed by the instance's
  position, the HUD lay 9 to 10 of the horizon's 51 pixels behind the screen:
  1.22 times the convergence distance. The instance's bounding sphere
  (`+0x40`: centre in the model's axes, radius) has its centre 0.4053 units
  along the model's -z, 4.46 m at the game's z scale of 11.0: the face is
  24.46 m from the camera, not the follower's 20. `placeScreenHud` scales by
  the face's depth. (In immersive VR the panel "2 m ahead" has had its face
  at 2.45 m since October 9; not changed.)

The first build was in the play folder from 16:24 to 16:31 and was replaced
by the previous one before the user ran it.

**Tests.** Core 243/243 (3 new: the stereo command's numbers and limits; the
two cameras' geometry at three lenses, a wide one, medium depth and an
off-centre lens; the scene clock, the panel's wait and the eye distance; and
the GAME SCREEN 3D row in the two settings tests), launcher 23/23
(`--screen-depth`, `screen_depth`), Python 109 (`game_screen_3d`,
`screen_3d_submitted`, `screen_depth`), the graphics test.

**Protocols.** XrConfig version 23, 664 bytes (`screenDepth`); XrData version
22, 816 bytes (`screenDepth`, `stereoSubmitted`); `Presentation::stereoScreen`
4 (`game_screen_3d`). The eye command keeps version 2 and its 208 bytes: mode
3's three numbers travel in the first eye's fov, which modes 2 and 3 do not
read. The saves the two game runs changed (`slot0-s.save`, `-userprefs.save`)
are back as they were at 16:25 (`reports/backups/saves-20261011-cutscene-3d`).

Not checked: a real cutscene (none could be reached in the user's saves
without playing past a story step); whether the game runs its HUD follower
and draws the panel in cutscenes, on which the stereo screen depends (if not,
cutscenes stay flat and `placed_frames` stands still there); the two quads
in the headset; camera cuts with the eyes' temporal anti-aliasing; depth of
field and other effects of the game's own view that eye views do not have;
cutscenes that are videos; the frame rate of a cutscene's three views.

## Y held steps up to what it opens — preceding build

The user, October 11, after the 15:24 session: "b (is now both dodge and
interact, making interactions not register also can we add proper active
controller buttons to the ui indicators?"

**What the buttons sent** (`game-vr-20261011-152435`, 570 s). B and Y never
went to the game together: of the samples in VR play 90 have the game's B
alone (`pad_buttons` 0x2000), 14 its Y alone (0x8000), none both; `dodges`
34, `interacts` 5. The first Y (106.8 s, at the console of the server room)
worked: the game's scene began 0.5 s later. The other four were at the red
double door of "The Main Event" (its middle near -539.3, -597.6; the feet
from the swing's samples):

| Time | Press | Feet from the door's middle |
|---|---|---|
| 510.7 s | B | 1.64 m, the dodge to 1.49 and back to 1.81 |
| 513.8 s | Y | 1.92 m: nothing |
| 524.0 s | B | 1.60 m, the dodge to 1.14 and on to 2.34 |
| 542.5 s | B | 1.87 m, the dodge to 1.24 and back to 1.81 |
| 548.1 s | B | 2.12 m, the dodge to 2.88 |
| 552.6, 558.1, 558.4 s | Y | 2.53 m: nothing |

He walked up to 1.64 m and stopped there (508 s); the nearest he came was
0.95 m, in a dodge. A and the web pull (four times) were tried there too.

**The same door in the plain game** (no headset; `probe_menu_pad.py start`,
profile 2 chosen on the profile screen with the virtual controller, Continue:
the checkpoint 31 m from the door). No prompt 3.6 m in front of the door,
nor against the wall 1.2 m to the side of its middle (-538.1, -597.4); the
game's "Y Open Door" at (-538.86, -596.71), 1.1 m from the middle. A Y tap
from 3.6 m: nothing, the gate stayed open, as in the headset. Y held with
the left stick at 0.6 toward the door from 3.6 m: the door opened on the way
in (the bomb squad's scene, the gate closed) within 2.5 s. So the game takes
its Y only within about a metre, and takes a Y that is already held as the
player comes into reach. That scene saved the game past the door: the two
save files the check changed are back as they were at 15:45
(`reports/backups/save-before-door-20261011-1545`; as the check left them in
`save-after-door-test-20261011-1600`).

**Changes.** `include/spidy/game_pad.hpp`, `InteractStep`: Y held 0.3 s
walks the player where they look at half stick, for 2 s at most, and no more
once the walk stalls (under 0.3 m/s for 0.25 s after its first 0.4 s), Y is
let go, or the game no longer has the player on the ground out of a fight.
`src/game_xr.cpp`: the worker turns the head's level direction into the
game camera's stick axes as `GameTrackingRig` does for the left stick, only
while the player's own stick rests; the swing is told of no stick, so no
wall takes a player walked up to a door; a tick in the left hand as the walk
begins. A fight is the combat camera (`bridge::combatCamera`, now in
`game_bridge_protocol.hpp` with the follow camera's). XrData is version 21,
808 bytes (`interactSteps`; a sample's `interact_steps`). The launcher's
CONTROLS page, `tools/launch-game-vr.ps1`, `docs/PLAYERS.md`, `README.md`,
`docs/DEVELOPMENT.md` and `docs/AUTOMATION.md` say so.

**Tests.** Core 240/240 (1 new: a tap walks nobody, a hold walks for
`walkSeconds`, a wall and a fight end it, one walk a press), launcher 23/23,
Python 109 (`interact_steps`).

Not checked: the walk in the headset (the worker's part needs one): its
direction, how it feels, and that the door opens with it; things other than
this door; a walk held into a wall the game crawls (it ends after 0.65 s
there). The game's prompts still name Xbox buttons: LB + RB and X are none
of the Touch controllers' in VR play.

## The first mission's crash; the game's LB + RB by a two-hand web pull — preceding build

The user, October 11: "game crashes on first mission, theres a chase sequence
mixed with a tutorial ... at some point during one of the steps it crashes
for everyone"; then "there is a door that cant be opened to proceed - it
prompts me to rb+lb and i cant target it with webs"; then "why cant we use
the existing grip webshooting with both hands to to physically pull objects
that need rb + lb".

**The crash** (session `game-vr-20261011-144923`, a new game; the game's
minidump of 14:54:32 and its log). The game's record: access violation at
`0xda9c4d3e`, with the module at `7ff6da6b0000` `Spider-Man.exe+314d3e`, a
read of address 0 on the main thread. Unwound with the executable's own
unwind data (every frame in `Spider-Man.exe`, down to the thread's start):

| Frame | Function | What it is |
|---|---|---|
| 0 | `314c70` (+`314d3e`) | `BotAttackResponseManager`: its response to a request's damager |
| 1 | `313420` | its vtable slot 11 (vtable `3836d88`): a damage modifier's turn at a request |
| 2 | `1ed1110` | the victim's `CommonComponentStash` runs its damage modifiers |
| 3-5 | `1eb7a70`, `1eb9b30`, `1eb8cf0` | DamageSystem: delivery, request processing |
| 6 | `1eb7d50` | DamageSystem's update |

`314c70` takes the damager's record from the request (`+0x138`), then
resolves the component handle at the manager's `+0xa4` (`16798f0`) and reads
the result's vtable without a test. That handle is set when the bot is set
up (`313150`, the tenth of ten sibling components it looks up on its own
record): the type at `7bf4e30`, whose name is `AwarenessDB`; 0 when the bot
has none. The call that follows (vtable +0x50, with the damager's handle)
returns an entry whose first byte is compared with 3, and the damager is then
tested for `JobManagerExplosionWarning`, `ProximityMine`, `SpiderDrone` and
`WebBomb`. With no damager the function returns before all of it (`15a0560`
gives 0). In the dump `rbx` is the session's `player_record` (the damager)
and `r14` the left hand's grab target.

The session's samples: the first fight began at 296.9 s; both hands caught a
thug each at 298.36 s; the right hand's yank at 298.41 s sent its kinetic
blow (`launches` 1) and that thug was in the game's flight by 298.47 s
(`flown` 1); the left hand's yank at 298.47 s sent the second, and the game's
camera commits and controller reads stop there. The game's log has its crash
record 10 s later (it collects memory figures first). So some thugs of that
fight have an `AwarenessDB` and some, in its first second, have not; why not
is not known (the fists counted 11 bots within reach for a moment, then 2).

**The change.** `native_bodies::damageNow` (every blow of Spidy's: punches,
web balls, the web's knock-downs and strikes) looks the victim's
`BotAttackResponseManager` up as the game does (type `6c74b40`; `16799a0`
through the record's table at +0x80 when it has one, else `167ab40` through
its list at +0x58: the pattern at `3437f0`) and resolves the handle at its
+0xa4 through the registry. A manager whose handle names nothing gets the
blow without its damager (`Counters::damageUnnamed`, a report's
`punch.unnamed`). `start` checks the two lookups' first bytes and the type's
name.

**The door** (session `game-vr-20261011-142032`, another save: Fisk Tower).
After its fights the player stood at (-531.6, 160.0, -599.1) and within 2 m
of it: 16 grip presses in a row missed, no web attached; 16 interact presses
and 23 web balls. In `Mapping::gameplay` the virtual controller had no LB or RB
at all, and on the flat screen during play it has the same mapping.

**The change.** `include/spidy/web_pull.hpp` (`WebPull`): both web buttons
pressed while each hand's aim is `AimKind::blocked` (a web would miss on what
it points at) within 12 m of the hand and 4 m of the other hand's point, the
game having the player on the ground (`steering`, not `owned`, `grounded`, no
web attached, no grab, no calibration input); then either hand moving away
from its point as the swing's zip has it (0.14 m within 0.25 s at 1.3 m/s or
more). From then the game's LB + RB (`Walk::pull`) stay down while both web
buttons do, 0.3 s at least and 4 s at most. `game_pad::PullPress`: the right
thumbstick's click is the same two buttons, held from 0.12 s on (not half of
clicking both sticks), a shorter click for 0.2 s. The aim preview is sampled
whether the markers are drawn or not. The swing's own code is unchanged.

**In the headset** (the user, session `game-vr-20261011-152435`, 570 s, the
build of 15:23, continued from the autosave before the first fight):

| | |
|---|---|
| The game | no crash record; closed normally at 15:34 |
| Blows | 359 issued, 0 dropped, 9 as nobody's: at 19.0 s (the first fight began at 15.7 s), six between 104.4 s and 110.6 s, one each at 115.3 s and 167.9 s |
| Fists and webs | 29 punches; 99 grabs, 80 yanks, 66 knock-downs, 35 impacts; 456 web balls, 226 webbing blows |
| The door | two grip presses missed at (-535.2, 160.0, -604.1), then "Web pull" at 404.7 s, LB + RB (`pad_buttons` `0x300`) down for 1.4 s; the player's next position on record is a floor up (y 170) |
| Later | LB + RB down four more times a floor up, near (-538, 170.0, -594): at 503.1 s, 505.9 s, 550.5 s and 551.3 s, 0.3 to 0.5 s each, each within a second of two grip misses; a cutscene began at 517.4 s and at 553.4 s. "Web pull" is on the console for 503.1 s and 550.5 s; a repeated line is printed once, so the other two are pulls or the right stick |
| Between | seven grip presses missed between 521 s and 547 s, one hand at a time but for one pair, and pulled nothing; the report has no hand speeds, so whether that pair was yanked is not known |

Not shown: that each of the nine would have crashed (one did, in the 14:49
session); what the game does with a nobody's blow in other fights; a pull
where the game shows no prompt; an LB + RB prompt on a thing a web holds on
(a yank zips there; the right stick's click is for that). No run in the
running game without the headset was made for this build.

**Tests.** Core 239/239 (2 new: the right stick's click, and the two-hand
pull; the Xbox-controller test has the pull and the grips), launcher 23/23,
Python 109 (the punch data's `unnamed`, 168 bytes).

## Dodge on B; Y interacts, X opens the game menu — preceding build

The user, October 11: "add a dodge button and ability (you can remap some
stuff - no need for a flip button when flips are enabled".

**The buttons in VR play.** B is the game's B (its dodge), Y the game's Y
(interact, web strike; B before) and X the game menu (Back; Y before). The
aim markers have no button now (X before): AIM MARKERS in the SPIDY VR tab
and in the launcher's options stay. A dodge needs the left stick for its way
and so a button under the right thumb, and with it A, B and Y are the
buttons the game's own prompts name. Flips keep A in the air as before. B
and Y reach the game while it has the player (`steering && !swingState.owned`,
as the interact did): not while a web or Spidy's own flight carries him.

**What the game does with that B** (`tools/probe_dodge.py`, the rooftop save
in its mission, no headset, module build of 14:34; presses of 200 ms on the
virtual controller, the player followed for 1.5 s after each):

| Press | Over the ground | Fastest | Gate open | Camera |
|---|---|---|---|---|
| none (1 s) | 0.00 m | 0 m/s | 100% | follow |
| left stick to the left | 0.23 m | 1.6 m/s | 100% | follow |
| B, left stick to the right | 2.46 m (and 3.0 m down, off the perch) | 8.6 m/s with the drop | 100% | follow |
| B alone | 0.99 m, level | 6.0 m/s | 100% | follow |

The speeds of this run are between two camera commits, the drop included.
A second run from where that left the player, with the ground speed taken
over a tenth of a second: the stick alone 0 m (a crate in the way), B with
the stick 8.4 m at up to 6.7 m/s, over the roof's edge (1.25 m up, 16 m
down), B alone after it 4.36 m; the gate open in every sample and the
follow camera throughout, the fall included. So the game dodges on the
virtual controller's B, further with the stick than without, and the
player's camera stays the one VR needs. The game rewrote its autosave during
the check, as in every check; the save folder as it was is in
`reports/backups/save-before-dodge-20261011`.

Not shown: the XR worker's way from the controller's B to that press (it
needs a headset; the core tests below cover the mapping), a dodge in a fight
or just before a hit, and how the dodge's leap feels in the headset. Not run
in the headset.

**Changes.** `include/spidy/game_pad.hpp`: `Walk::dodge` (the game's B),
Back from X instead of Y in `Mapping::gameplay`, and `PressOrigins` (B, X and
Y stay what they were pressed as between the game screen and VR play; before,
only B did). `src/game_xr.cpp`: the worker sends them; X no longer switches
the aim markers; XrData `dodges` where `reserved` was (version 20 still: 0
from older modules), `interacts` counts Y. During the T-pose calibration B
skips and does not dodge; Y interacts there. `tools/run_game_vr.py`:
`dodges` in a sample. `tools/probe_dodge.py` is new. The launcher's CONTROLS
page, its options' texts, `tools/launch-game-vr.ps1`, `docs/PLAYERS.md`,
`README.md`, `docs/DEVELOPMENT.md`, `docs/BODY.md` and `docs/AUTOMATION.md`
name the new buttons.

**Tests.** Core 237/237 (1 new: B, X and Y across the game screen and VR;
the Xbox-controller test has the dodge and the new Back), launcher 23/23,
Python 109 (the XR sample's `dodges`).

## The reflections restored: the shared half-size targets are needed — preceding build

The user, October 11, after a session on the cable with the build below:
"preformance was good again, but no ssr visible (turned it off and on in the
menu".

**The session** (`game-vr-20261011-124240`, module 2c4f63e5..., 215 s):
`network` Wi-Fi none, cable yes; `stream_stalls` 0 rounds (77 in the session
before); `eyes.reflections` true; the game's log has Screen Space Reflections
Normal at the start, Off at 12:44:07 and Normal again a second later.

**What was wrong.** The third build made an eye's set without the four
`kSharedHalfResFp16Target` (the two sections below: only `189cae0` takes
them, called in `172b180` on the ray-traced path, in depth of field and in
`1a054a1`). With that set the pass's command runs and the eye's image has no
reflection. The four pictures of the sections below, the same crop enlarged
three times (`reports/ssr/statue-four-a.png`, `-b.png`; half-size crop 655,
760 to 915, 900):

| Picture | Module, the game's setting | The statue's mirror image in the water |
|---|---|---|
| `old-eyes` | no targets, Normal | none |
| `new-eyes` | second build (all ten targets), Normal | there, with the island's |
| `ssroff-new-eyes` | second build, Off | none |
| `lean-on-eyes` | third build (six targets), Normal | none |

The third build's check ("the left eye's image has the mirrored sky on the
wet roofs again") compared `ssroff-new-eyes` with `lean-on-eyes` at half size
and took a brighter roof for reflections; it was not. In one game with the
third build, the game's own view rendering beside the eyes (`--views 29`,
`reports/ssr/setting-ab`): the statue's mirror image is in the game's window
(`reports/menu-pad/ab1.png`) and not in the left eye taken at the same time
(`a-normal.png`).

**Changes.** `src/stereo_probe.cpp`, `initBuffers`: the offscreen kind's mask
gets bit 1 with nothing left out, as in the second build (targets
`kSsrHistory`, `kSsrHistoryDepth`, `kSsrFinalColor0/1`,
`kSsrFinalConfidence0/1` and the four shared ones). `tools/run_game_vr.py`:
`eye_reflections` also refuses with the saved `RTReflections` set (the
ray-traced path through `172b180` and `1a054a1` was never run in an eye, whose
set has no kind-0x80 targets). `tools/probe_vr_load.py`: the phase `sway` (a
head 2 cm to either side and a degree of turn, about once a second). Tried
and taken out: clearing the job's render context `+710` in `copyJob` to
switch the pass off for a comparison (the reflections stayed).

**Checked in the game** (no headset; the game's setting Normal; module
6f0097a4..., `reports/ssr/final.json`, image capture):

- `reflection_targets`: 1032 x 1096 for `kSsrHistory` and for the first
  shared target in each eye.
- The statue's mirror image, the crop enlarged (`final-eyes/statue-final.png`):
  in the still left eye (`heading0-on.png`) and in two taken while the head
  swayed (`sway-1.png`, `sway-2.png`). With the build before this one's
  clean-up (74e1564c..., `full-reflections/statue-still.png`,
  `statue-sway.png`): the same.
- Frames (`--pacing`): still 50.4 a second, 19.82 ms median, 21.42 ms the
  99th percentile, 22.25 ms the longest; swaying 49.8, 19.97, 21.94, 27.76;
  still again 50.2, 19.96, 21.66, 22.45; none over 1.5 medians. An eye's GPU
  time 6.76 and 6.90 ms, video memory 6,349 MB, 13,813 MB committed.
- 236 core and 23 launcher checks, 109 Python checks pass.

**Not verified.** The headset. The right eye. The setting High, and ray
tracing on (the eyes then get none, by `eye_reflections`; not run). Whether
the 10:51 and 11:00 sessions' eyes showed the reflections (the second build:
they should have).

**In the play folder** (October 11, 13:13):
`build\windows-ninja\spidy_stereo_probe.dll` (6f0097a4...) and
`tools\run_game_vr.py`, the ones they replaced in its
`reports\backups\play-folder-before-reflections-restored-20261011-131313`.

## The stalls are Virtual Desktop's, with the PC on Wi-Fi — preceding build

The user, October 11, 12:21, after one session with the build below (the
reflections on): "preformance sucked with and a without ssr, huge stutter
spikes".

**The session** (`game-vr-20261011-121756`, module 2c4f63e5..., 154 s, 100 s
of them immersive; the first with `xr_ms` and `gpu_waiting` in its samples).
The runtime's display period is 8.33 ms; the worker's round 13.9 ms at the
median.

| Step of the worker's round | Median | 99th percentile | Longest |
|---|---|---|---|
| Whole round | 13.9 ms | 100.3 ms | 100.7 ms |
| `xrWaitFrame` | 0.0 | 7.3 | 8.7 |
| Tracking, prepare, acquire, release | 0.2 | 0.4 | 1.2 |
| Copy of the staged pair | 0.1 | 0.3 | 1.4 |
| Hands and overlay | 0.1 | 20.5 | 23.5 |
| `xrEndFrame` | 13.2 | 93.0 | 100.1 |

114 of 2,289 immersive samples have a round over 60 ms, in five stretches
(23-25, 39-40, 49-51, 75 and 116-118 s): 77 rounds, each 99.8 to 100.7 ms,
of which `xrWaitFrame` 6.9 ms and `xrEndFrame` 92.9 ms (`stream_stalls`: 77
rounds, 7.7 s). In those samples `gpu_waiting` is 0 (40), 1 (58), 2 (14) or
3 (2), no more than in the others (median 1, 4 at most), and the image's
generation moves 6 a round (60 pairs a second; 2 to 3 a 47 ms sample
otherwise). So the runtime's submit waited out a time-out of a tenth of a
second while the game's queue was short and the game had more of the
graphics card than usual: Virtual Desktop was not taking frames.

**The PC's network that day** (Windows' NetworkProfile and WLAN-AutoConfig
logs):

| Time | Event |
|---|---|
| 09:59:01 | The cable's network connects (the Ethernet adapter) |
| 10:01:56 | Wi-Fi disconnects |
| 10:07-10:20 | Session, first build: no stretches |
| 10:32:50 | The cable's network disconnects; Wi-Fi connects to the headset's network (802.11ax, 5 GHz) |
| 10:51-10:55, 11:00-11:06 | Sessions, second build: 8 stretches each |
| 12:17:43 | The cable's network connects again; Wi-Fi stays connected |
| 12:17:56-12:20 | Session, third build: 5 stretches |
| 12:21:48 | Wi-Fi disconnects |

The headset's access point is on the same network as the cable. Over the cable afterwards (`tools/probe_link.py`,
`reports/ssr/link-idle-ap.json`): 2,967 replies in 75 s, 0.7 ms median,
1.2 ms the 99th percentile, 4.7 ms the longest, none lost.

**Not shown.** That the headset's stream ran over the Wi-Fi address in the
12:17 session (both were connected; a connection made before 12:17:43 would
have). A session on the cable alone with the reflections on: the one smooth
session was the first build's. Why the cable's connection ended at 10:32:50
(no driver event in the System log). The link's times with the PC on Wi-Fi,
and during a session.

**Changes** (`tools/run_game_vr.py`; the module is the build below's):
`network_links` (`netsh wlan show interfaces`, `ipconfig`), `link_warning`
at a session's start for Virtual Desktop, SteamVR and ALVR, `stream_stalls`
(rounds of 90 ms or more of which the runtime's two waits are 80 ms or more)
said when the game closes if there are 5 or more; `network` and
`stream_stalls` in the report. `tools/probe_link.py`. 109 Python checks pass
(1 new). Nothing here was run in a session.

**In the play folder** (October 11, 12:29): `tools\run_game_vr.py` and
`README.txt`, the ones they replaced in its
`reports\backups\play-folder-before-link-warning-20261011`.

## The headset loop's stalls with the reflections, fewer buffers, the game's setting — preceding build

The user, October 11, after two sessions with the build below: "this run with
the reflections was much more stuttering compared to my first run before the
ssr fix (even with ssr disabled its stuterry) any ideas why?"; then, the first
build's module back in the play folder for a comparison that was not run:
"fix the ssr fix".

**The sessions** (the play folder's reports; `game-vr-20261011-100710` is the
first build's module cee13954..., `-105143` and `-110002` the second's
5d9037fa...; the same options: 3072 x 3264 eyes at 67%, monitor rate 5,
shared shadows; the report's samples are about 47 ms apart):

| | 10:07, first build | 10:51, second | 11:00, second |
|---|---|---|---|
| Immersive seconds sampled | 637 | 169 | 274 |
| New eye pairs a second, 10 s windows | 34-53 | 33-49 | 32-50 |
| Samples at least 2 submissions short of 72 a second | 40 of 11,665 (0.3%) | 156 of 3,145 (5.0%) | 148 of 5,054 (2.9%) |
| Half seconds with the worker under 30 rounds a second | 0 of 1,384 | 12 of 365 (3.3%) | 5 of 592 (0.8%) |
| Stretches at about 10 rounds a second (6 samples or more) | none | 8, 0.3-2.3 s | 8, 0.3-1.6 s |
| Video memory, the game's log, most | 7,444 MB | 7,387 MB | 7,769 MB |

October 10's session (`game-vr-20261010-193410`, 0.2.11): 0 of 232 half
seconds, one stretch of 0.67 s. In a stretch the worker's frame counter moves
once in two samples while the image's generation moves 4 to 6 (the game went
on at 40 to 60 pairs a second), and render memory a frame is 17 to 21 MB in
about every third sample where it was 10 to 12. One in each session is at its first eye images, several
start as the player lands or a web starts or ends, and some end with one
game-screen frame. In the 11:00 session the game logged `Screen
Space Reflections: Off` at 11:03:40 (about 215 s); stretches at 240.0, 290.7 and
302.9 s follow. Where the worker waits is not in these reports: the stage
times were only read at a normal stop, and the game was closed first.

**Without a headset** (the rooftop save, a fresh game for each run, 30 s
after the load; the game's window full screen, as this shell's registry
writes do not reach the game):

- `tools/probe_frame_pacing.py` (new; `probe_vr_load.py --pacing`) reads the
  render memory module's frame counter about a thousand times a second.
  The second build's module, the game's setting Off (as the user left it;
  `reports/ssr/pace-new.json`): the game alone 119.2 frames a second, 8.48 ms
  median, 9.98 ms the 99th percentile, 10.92 ms the longest; eyes live 66.3 a
  second, 15.09, 16.68, 20.14 ms; turning 66.6, 15.01, 17.58, 19.26 ms. No
  interval over 1.5 medians in any phase, and none in two more phases of 35 s
  with the pad held on run, jump and swing (`move-new.json`, 20.24 ms the
  longest; the player ended in the
  harbour at the mission's edge, and the game was closed without an answer to
  its "Abandon mission" question).
- With the setting Off and the second build's targets, the left eye's image
  has no mirror image of the statue and flat roofs (`ssroff-new-eyes`): the
  pass does not run. What the targets still change then: RadianceCapture
  (0x44, `1849200`) renders into `kSsrHistory` and its depth instead of
  `kToneMapRadiance` (`189cc60` picks it), and the extra memory.
- Depth of field is not it. Its handler (0x8c, `18371f0`) is the other user
  of the shared half-size targets (`189cae0`) and skips without them, but the
  command is queued (`1922fee`, `1923076`) only where the game's byte
  `609d8f6` is 0 and a view's `+1d6c`..`+1d78` are set: 1 with Depth of Field
  Off, and (0, 0, 100, 0) in both eyes. In 525 frames of running, jumping and
  swinging no view got it (`swing-new-trace.json`), an eye's GPU time was
  7.0 ms at the median and 8.8 ms at most, and a fourth scene view rendered
  69 of them (not identified).
- The game does not queue frames ahead of the graphics card with the
  monitor's presents held: at 100% (3072 x 3264 scenes, 40.9 frames a second,
  GPU 99% busy) 0.6 staged pairs waited on it on average and 2 at most at
  monitor rate 5, 1.0 and 2 with every present (`depth-100.json`).
- OneDrive does not sync this project, the play folder or the saves (their
  folders are not its placeholders), so no upload of the reports shared
  the Wi-Fi the headset streams over (802.11ax).

**Not found:** what the worker waits for in those stretches, and whether the
second build's module causes them. Its GPU cost does not (the pass off, they
went on), video memory peaked no
higher than in the first session (the table; the budget 11,227 MB), and the
game's frames are even. Three sessions one after another on a laptop that ran this shell's
probes in between are the whole evidence.

**Changes.**

- `src/stereo_probe.cpp`, `initBuffers`: for the call, the four
  `kSharedHalfResFp16Target` (table `609b060`, targets 0x6e-0x71, flags
  0x80011 at `+28`) have kind 0x10 instead of 0x11, so an eye's set (mask
  0x25) is made without them; the game's own sets (0xff) get them as before.
  The reflections take them only on the ray-traced path (`172b180` with its
  argument set, `172c086`), where an eye's set lacks the other targets too:
  with ray tracing on, the second build's set would have gone further into
  that path than an eye's set can serve.
- `src/game_xr.cpp`: options bit 0x40000 gives `SpidyEyeTemporal` its bit 8
  (no targets for reflections); options up to 0x7ffff.
- `tools/run_game_vr.py`: `eye_reflections` (the game's saved
  `ScreenSpaceReflections`, 0 Off, 1 Normal, 2 High as `1ca9026` names them;
  Off or `--no-eye-reflections` sets the bit), the console's line, `eyes.
  reflections` in the report; each sample's `xr_ms` (`XR_STAGES`, the last
  round's milliseconds from `SpidyXrTimingData`) and `gpu_waiting` (the GPU
  bridge's fence less its completed value); `timing` in a report however the
  session ends. `tools/launch-game-vr.ps1`: `-NoEyeReflections`.
- `tools/probe_frame_pacing.py`; `tools/probe_vr_load.py`: `--pacing`, and
  `reflection_targets` lists the first shared target's size as well.

**Checked in the game** (no headset; this build's module 2c4f63e5...):

- The game's setting Off (`reports/ssr/lean-off.json`, staged copies):
  `reflection_targets` 1280 x 720 and 1280 x 720 for the game's view, 1032 x
  1096 and 0 x 0 for each eye; 65.3 frames a second, 15.27 ms median, 17.01 ms
  the 99th percentile, 19.37 ms the longest; turning 65.4, 15.23, 17.79,
  19.64 ms.
- The setting Normal, set in the game's menu with the virtual controller
  (`lean-on.json`, image capture as in the section below): the left eye's
  image has the mirrored sky on the wet roofs again (`lean-on-eyes`) —
  **wrong: it has no reflections; see the section above**; 51.2
  frames a second, an eye's GPU time 6.05 and 6.54 ms, video memory 6,269 MB,
  13,733 MB committed (the section below: 50.9, 6.48 and 6.79, 6,175, 13,626
  without the targets; 49.7, 6.67 and 7.05, 6,359, 13,858 with all of them);
  19.47 ms median, 21.53 ms the 99th percentile, 21.96 ms the longest;
  turning 19.61, 22.89, 24.26 ms.
- 236 core and 23 launcher checks pass; 108 Python checks (1 new: the
  setting's rule and the stages' order).

**Not verified.** The headset: nothing here went through OpenXR, so neither
the stalls' end nor the new samples (`xr_ms`, `gpu_waiting`) were seen in a
session. The option bit through the launcher (`run_game_vr.py` read this
shell's own copy of the registry here). The setting High, and ray tracing on.

**The game and its settings afterwards.** Screen Space Reflections is on
(Normal; the user had set it Off at 11:03). The game wrote its autosave
(`slot0-s.save`) at 11:24 and 12:07 during these runs, at the mission's
rooftop each time; the saves of 10:39 are in
`reports/backups/save-before-ssr-20261011`.

**In the play folder** (`Documents\Spidy-0.2.11`, October 11, 12:10):
`build\windows-ninja\spidy_stereo_probe.dll` (2c4f63e5...),
`tools\run_game_vr.py` and `README.txt` (this build's `docs/PLAYERS.md`),
the files they replaced (the first build's module and its script) in its
`reports\backups\play-folder-before-reflection-setting-20261011-121052`.
With the play folder's Python the script lists `--no-eye-reflections`.
Nothing was started from there.

## Screen-space reflections in the eyes — preceding build

The user, October 11: "screenspace reflections don't seems to work in vr (i
see them in normal screen during your tests)".

**Cause.** The game's settings (its log): `Screen Space Reflections: Normal`,
`RT Reflections: Off`. ScreenSpaceReflection is command 0x43 (handler
`1734a80`, registered at `18890c0`); the October 10 trace
(`reports/perf/trace-main-on.json`) has it queued once a frame for the game's
view and for each eye. The handler returns unless the float at `7a2e910` is
above 0 and the buffer set at the view's `+1648` has `kMrtSsrHistory` and
`kMrtSsrHistoryDepth` (records 105 and 106 from the set's `+14a0`: `+dcc8`,
`+deb0`, a size at `+1cc`); its work is `172b180`, with the same tests. In the
table of targets (`609b060`) those two, `kSsrFinalColor0/1` and
`kSsrFinalConfidence0/1` have kind 1 and the four `kSharedHalfResFp16Target`
kind 0x11; an offscreen view's mask (`3d11b18`, third word) is 0x20.

**Change.** `initBuffers` (`src/stereo_probe.cpp`) makes an eye's set with
the mask's bit 1 as well as bit 4. `SpidyEyeTemporal`'s bit 8 leaves bit 1
out (`tools/probe_vr_load.py --no-eye-reflections`); the probe reports
`reflection_targets` (the size of `kSsrHistory` for the game's view and each
eye).

**Checked in the game** (no headset; the rooftop save, a fresh game for each,
30 s after the load; `probe_vr_load.py --phases vr,shots --gpu capture
--seconds 6`, eyes 3072 x 3264 at 67%, views 61):

| | `kSsrHistory`: game, left, right | Frames/s | GPU ms: left, right | Video memory | Committed |
|---|---|---|---|---|---|
| Without (`reports/ssr/old.json`) | 1280 x 720, 0 x 0, 0 x 0 | 50.9 | 6.48, 6.79 | 6,175 MB | 13,626 MB |
| With (`reports/ssr/new.json`) | 1280 x 720, 1032 x 1096, 1032 x 1096 | 49.7 | 6.67, 7.05 | 6,359 MB | 13,858 MB |

Left eye, looking the game camera's way (`reports/ssr/new-eyes/heading0-on.png`
against `old-eyes`, viewed at half size): with the targets the Statue of
Liberty's mirror image lies on the water below it and the wet roofs to the
right mirror the bright sky, as the game's own window shows them
(`reports/menu-pad/ssr-new-before.png`); without, the water under the statue
is plain and those roofs flat. Twelve captures at four headings, the views
stopped and the game closed with no error either time. 2 core and launcher
test programs pass.

**Not verified.** The headset. The right eye's image (the shots are the left
eye's). The reflections in motion and at the eyes' edges: they are made from
what each eye sees, so the two eyes' can differ where one eye's source is off
its image. Ray-traced reflections (off here; their targets, kind 0x80, are
still not in an eye's set), volumetric fog (kind 8) and motion blur (kind 2).
The Python checks were not run.

**In the play folder** (`Documents\Spidy-0.2.11`, October 11, 10:50):
`build\windows-ninja\spidy_stereo_probe.dll` (5d9037fa..., the module
measured above), the one it replaced in its
`reports\backups\play-folder-before-reflections-20261011-105057`. Nothing was
started from there.

**The user's sessions with it** (`game-vr-20261011-105143`, `-110002`, the
play folder's reports; `-100710` is the first build's): "much more stuttering
compared to my first run before the ssr fix (even with ssr disabled its
stuterry)". The reports do not show it: 42-45 new pairs a second while
immersive in each (10 s windows), 41-43 before and after Screen Space
Reflections went to Off in the game's menu at 11:03, video memory at most
7,387 MB of an 11,227 MB budget (7,201 MB with the first build), no render
memory overflow. They sample every 50 ms and cannot show single frames. For
the user's comparison the first build's module (cee13954...) is back in the
play folder since 11:20; this build's is kept in its
`reports\backups\play-folder-with-reflections-20261011`.

## The monitor's presents, and one shadow set for both eyes — preceding build

The user, October 11, after running `python tools\vr_display.py --restore`:
"i ran the command. continue with the rest". Later: "why do we need it as
settings?", "dont do another measure just let me test", "monitor mirror can
be a setting".

**Setting.** The same PC and rooftop as the section below. The game's display
settings are the user's own again (full screen, 3840 x 2400), and this shell
cannot write the game's registry settings, so every measurement here is of
the full-screen game, not of the launcher's 1920 x 1080 window. Moving the
window to 1920 x 1080 with `SetWindowPos` changed nothing (42.8 frames a
second, `reports/perf/final-win1.json`): the swapchain kept its size. Eyes
3072 x 3264 at 67%, views 61, a fresh game for each run, measured from 40 s
after the load.

**The baseline moved** (`reports/perf/final-base2.json`, October 10's build):
43.2 frames a second ahead and 42.0 turning (61.0 and 59.6 in the small
window on October 10), GPU 99% busy, the eyes' GPU time 5.4 and 6.0 ms of a
23.2 ms frame.

**Presents.** `SpidyPresentEvery` (an experiment in this morning's build:
while the window mirrors the left eye, only every nth of the game's presents
reaches DXGI), switched between phases of one game
(`reports/perf/final-pr1.json`):

| Presents reaching DXGI | Frames/s | Frame | Eyes' GPU ms |
|---|---|---|---|
| every one | 43.1 | 23.2 ms | 5.6, 6.2 |
| every second | 52.7 | 19.0 ms | 6.5, 7.2 |
| every fourth | 58.8 | 17.0 ms | 6.5, 7.5 |
| one in a thousand | 66.2 | 15.1 ms | 7.0, 7.7 |
| every one, again | 42.8 | 23.4 ms | 5.7, 6.2 |
| every one, turning | 42.5 | 23.5 ms | 5.5, 6.3 |
| one in a thousand, turning | 65.5 | 15.3 ms | 6.9, 7.7 |

A present is 8.1 ms of the frame. The eyes' own GPU time rises as presents
go (the card, held at 80 W, runs them slower when it has no pauses), and with
none the eyes are the frame. The game did not stall without presents in any
phase. Latency (`reports/perf/final-lat1.json`; `pose_age_ms` is the time
since the eye command whose pose the newest pair handed to the GPU carries
was sent, `pairs_waiting_on_gpu` the bridge's fence less what the GPU had
finished): median 65.8 ms and 1.46 pairs with every present, 43.8 ms and 0.53
with one in a thousand; turning 66.5 and 44.3 ms.

**Shadows.** From the executable and the running game:

- `19206a0(view, parent, flags, last)` (the scene submit) returns a 0x58-byte
  record of what it made for the frame: `+18` the view's light set, `+28` its
  KeyLightContext (0x3de0 bytes: `+d` and `+f` count the cascades, `+150`
  holds one each 0x3e0 bytes, with the box's half sizes at `+170`, its
  transform at `+198` and its shadow map's atlas slot at `+3c8`), `+30`, and
  others. Given such a record as `parent`, a view whose `+120a` is set
  (`189d3d2` sets every view's) takes the parent's light set (`1920eeb`) and
  copies its KeyLightContext (`19212e9`), and then skips `1716390`, which
  queues the cascades' SetShadowMapTarget passes (through `1715880` and
  `1747140`) and their casters. The game's callers pass no shadow sets: the
  offscreen views' (`186d72a`) passes a record with `+48` alone.
- `1716270` builds a view's KeyLightContext: `170fbc0(context, view, ...)`
  fits the cascades, reading the view's pose (`+10`, `+20`, `+30`) and lens
  (`+330`, `+3fc`, `+400`..) from the view's descriptor, its first 0x440
  bytes (`1899ab0`, a view's descriptor submit, is a copy of those); then
  `1715e10` starts each cascade's caster queries.
- Each eye's own set, compared in the game (`SpidyShadowData`,
  `reports/perf/final-sh2.json`): six cascades each; the first four boxes'
  half sizes 13.24, 33.08, 82.69 and 206.73 m for both eyes; the two eyes'
  nearest boxes 13.0 m apart along the box's first axis (32.3, 80.7 and
  201.6 m for the next three). The probe's lenses: 54 degrees out, 44 in.
- A cascade's shadow map: `1740890(atlas, key, size)` on the atlas at
  `774c4d0`, the size the atlas's first word, 1792 here.

**Changes.**

- `src/stereo_probe.cpp`: hooks on `19206a0` (`submitScene`) and `170fbc0`
  (`fitCascades`). With `eyeShadows` bit 1 (`SpidyEyeShadows`; the VR worker
  sets it unless options bit 17), the eye submitted first in a frame has its
  descriptor replaced by `shadowLens` for the length of `170fbc0`, and the
  second eye's submit gets a parent record holding the first one's
  KeyLightContext. `shadowLens` is the head's lens covering both eyes, built
  in `placeEyes` as the game view's has been (now also when that view keeps
  the stock camera). Each eye keeps its own set on the flat screen, when
  the eyes are not commanded, and when the first eye made none.
  `SpidyShadowData` reports what each eye made or took.
- `src/native_eye_gpu.cpp`, `include/spidy/monitor_rate.hpp`: while the
  window mirrors the left eye and no game screen is wanted, a present beyond
  the monitor's rate returns before DXGI (`holdPresent`,
  `monitorPresentDue`; `SpidyMonitorRate`, 30 a second from the VR worker by
  default, 0 every one). `SpidyPresentCount(2)` counts those held back.
- `src/game_xr.cpp`: XrConfig 22: `monitorRate` (0-240) where the spare word
  was, options bit 17 (each eye its own shadow set).
- `tools/run_game_vr.py` (`--monitor-rate`, `--own-shadows`; `eyes` in
  reports has `monitor_rate` and `shared_shadows`),
  `tools/launch-game-vr.ps1` (`-MonitorRate`, `-OwnShadows`), the launcher
  (Monitor frame rate: Every frame, 60, 30, 15, 5 a second; `monitor_rate=`
  in launcher.ini).
- `tools/probe_vr_load.py`: `--eye-shadows`, `--monitor-rate`, phases
  `vr_s0`..`vr_s7`, `vr_m0`, `vr_m9` (and for `turn`, `narrow`),
  `pose_age_ms`, `pairs_waiting_on_gpu`, `presents_held`, and each phase's
  `shadows`.

**Measured with shared shadows** (presents one in a thousand throughout; own
and shared in alternating phases of one game):

| Eyes | Phase | Own | Shared | Render MB/frame | GPU busy |
|---|---|---|---|---|---|
| 3072 x 3264 at 67% (`final-sh2.json`) | ahead | 66.4, 65.7 | 66.7, 66.7 | 5.3 to 4.2 | 98% |
| | turning | 65.7, 65.8 | 67.4, 67.3 | 8.9 to 6.5 | 96-98% |
| 1536 x 1632 at 50% (`final-sh3.json`) | ahead | 98.4, 98.2 | 104.0, 101.7 | 5.4 to 4.3 | 48-50% |
| | turning | 78.9, 80.5 | 87.1, 86.0 | 8.4 to 6.5 | 44-45% |

Shared, both eyes report the same boxes, the first four 15.23, 38.06, 95.15
and 237.86 m half a side: 15% larger than an eye's own, over shadow maps of
the same 1792 pixels.

**The picture** (`tools`' capture bridge, both eyes at one pose, own and
shared in turn; `reports/perf/shadow-ab2`, `shadow-ab3`). The scene has the
player's arms and enemies' laser sights moving through it, so whole-image
differences say nothing (own against own: blocks up to 119 levels apart).
Crops of sunlit roofs with tree shadows, at full size and enlarged twice
(`shadow-ab3/side-2560-1850-left.png`, own left, shared right): the same
shadow shapes in the same places, the shared ones' edges slightly softer. No
missing or misplaced shadow was found in the crops looked at; the edges of
the eyes' images were not examined separately.

**Tried and taken out.** Larger shadow maps for the covering set (a hook on
`1740890` asking for 15% more): the game crashed within seconds
(`Marvel's Spider-Man Remastered-v4.630.0.0 2026-10-11-09-20-11.mdmp`: a
read of `[rax+c]` with `rax` 0 at `Spider-Man.exe+16364df`, where the render
code takes a cascade's map from `[r10+278]`). The atlas had no room for all
six, and a cascade without a map is not survived.

**The graphics card.** `nvidia-smi --query-gpu=power.default_limit,
power.max_limit`: 80 W and 150 W. Every measurement of these two days ran at
74-80 W.

**Checks.** 236 core checks (1 new: the monitor's rate), 23 launcher checks
(the `--monitor-rate` argument), 107 Python checks (1 new: `--monitor-rate`'s
range) and the GPU test pass.

**Not verified.**

- This build in the game. The measurements are of this morning's builds:
  presents held back by count (`SpidyPresentEvery`), shared shadows switched
  by `SpidyEyeShadows` from the probe. Holding presents by the clock
  (`monitorPresentDue`), the VR worker's two calls and XrConfig 22 were built
  after the user asked to test it themselves, and have only their unit checks.
- The headset, as before; and with it what the monitor's rate is worth in
  the launcher's small window, where a present is smaller (the frame there
  was 16.4 ms on October 10 with 13.4 ms of it the eyes').
- Shared shadows at the edges of the eyes' images, at night (the scene had
  no shadow-casting local lights: both eyes' light sets were empty, and they
  are not shared), and through a long session.
- The player's hands or body in front of the head: the capture probe does
  not drive them.

**In the play folder** (`Documents\Spidy-0.2.11`, October 11, 09:45):
`build\windows-ninja\spidy_stereo_probe.dll`, `tools\run_game_vr.py`,
`Spidy Launcher.exe` and `README.txt`, the files they replaced in its
`reports\backups\play-folder-before-shared-shadows-20261011-094525`. With the play folder's Python its modules
load and export every entry `run_game_vr.py` names. Nothing was started from
there.

## Anti-aliased, upscaled eyes and a resting game view — preceding build

The user, October 10: "now that spidyvr is pretty stable and far along, our
preformance in vr sucks with memory hogging, lag spikes and overall pretty
bad performance for a very well optimized base game. get us to 90 fps without
visual quality or feautres loss, there should be plenty of easy wins and
massive gain (for example dlss doesnt work)".

**What the report shows.** `game-vr-20261010-193410.json` (Quest 3 through
Virtual Desktop at 72 Hz, 3072 x 3264 per eye, render scale 100): while
immersive, 20 to 31 new eye pairs a second per 5 s (2,645 new, 5,012 reused);
73 submissions a second throughout. `free_commit_mb` 11,269 at the start,
5,095 at its lowest; the game's own log: 15.1 GB committed, 7.9 of 11.2 GB of
video memory. The game's log also has the session's settings: `Window mode:
Fullscreen`, 2560 x 1440 (the console log has no "Desktop view" line, and
`hud.texture` is 2560 x 1440: the small window was not in use), `VSync: On`
at 120 Hz until the user turned it off in the menu two minutes in, upscaling
DLSS Quality, anti-aliasing TAA.

**This PC.** RTX 4080 Laptop GPU (12 GB), Core Ultra 9 185H, 32 GB; plugged
in, Windows' Balanced plan. `nvidia-smi` reported 74-80 W at 97-99%
utilization in every measurement below. (The RTX 5090 figures elsewhere in
these documents are another PC's.)

**Baseline** (`tools/probe_vr_load.py --views 29 --no-eye-antialiasing`, the
user's save on its rooftop, a fresh game, measured from 40 s after the load
so that streaming has settled; `reports/perf/final-old.json`):

| Phase | Frames/s | Frame | Render MB/frame | GPU busy | GPU ms: game view, left, right |
|---|---|---|---|---|---|
| Game alone (1920 x 1080 window) | 111.0 | 9.0 ms | 2.1 | 67% | 6.1 |
| VR views, looking ahead | 35.6 | 28.1 ms | 7.6 | 99% | 5.1, 10.4, 10.1 |
| VR views, turning a full circle | 36.9 | 27.1 ms | 11.6 | 97% | 4.9, 9.8, 9.8 |

The per-view GPU times are the engine's own: `189e3a0` (a view job's end)
divides the GPU timestamps around the job's commands by the timestamp
frequency (`609ca78`) into the float at `*(view+1690)`. Frame time against
the eyes' pixels, with the game's view still rendering and the eyes
anti-aliased (`reports/perf/m-scale100`, `m-scale67`, `m-scale50`: 28.9, 20.1
and 17.0 ms; eyes of 2064 x 2192 and 1536 x 1632 at 100%, `m-out2064`,
`m-out1536`: 18.5 and 15.2 ms): 0.7 to 0.8 ms per million pixels rendered,
0.12 to 0.14 ms per million pixels of eye image, and about 11 ms that no eye
size changes.

**Cause 1: the eyes had no anti-aliasing.** From the executable and the
running game:

- A view's commands run with its render context (0x7a0 bytes, the job
  setup's `settings`; `1886ea0` copies it to `7a2e200`). Its anti-aliasing is
  `+40c` (`7a2e60c`). TemporalAa (command 0x8a, handler `1842e10`) returns at
  once unless it is 1. Sampled in the game with DLSS chosen: 0 for every
  view, the game's and both eyes'.
- Upscale (command 0x8b, handler `1849e80`) is queued for every view, but
  leaves for the plain copy at `184a82a` when the view's id (`+f90`) is
  negative, as every offscreen view's is. Its upscaler (`NxUpscalerImpl`,
  `1d05510`: Streamline's DLSS with viewport 0, or NGX, FSR, XeSS) holds one
  instance, sized for the game's view.
- The eyes' lenses were built with no jitter (`placeEyes`, the `Lens` call).
- A view's buffer set is made from a table of 132 render targets
  (`609b060`, 0x30 bytes each; `1873af0` makes those `1875270` allows). A
  target with a kind in its flags' low byte is made only where the buffer
  set's kind mask (`3d11b18`: 0xff for the game's sets, 0x20 for an offscreen
  view's kind 2) has it: an eye's set had no `kTaaTarget0/1` or
  `kTaaDepth0/1` (kind 4), and, incidentally, none of the SSR history (1),
  motion blur (2), shared half-size (0x11), volumetric fog (8) or ray
  tracing (0x80) targets. Listed in the game (the records from the set's
  `+14a0`, 0x1e8 bytes each, size at `+1cc`): `kMrtTaaTarget0` 1920 x 1080 in
  the game's set, 0 x 0 in an eye's.
- `1874f40` sizes a set: the render size from its arguments (times the
  temporal upscale ratio at `+12c68`, which `ce71b0` sets for the game's
  set), and the size of the anti-aliased image (`+12bf8`..`+12c04`:
  `kResolved`, `kTaaTarget0/1`) from the game's display (`7a2ef00`),
  whatever the view: `kMrtResolved` was 1920 x 1080 in a 3072 x 3264 eye's
  set.

**Cause 2: three scene views.** Per-view GPU time above: the game's own view
took 5.1 ms of each frame, as much as an eye at 67%. It follows the game's
dynamic resolution (968 x 544 when the frame took 28 ms, 1840 x 1032 at
19.6 ms: `Dynamic Render Buffers`) and has every effect. Render commands a
frame by view (`tools/probe_render_commands.py`, a hook on `187e130`):
with the game's view, 8,556 `ModelShadowCasters`, 481 `ModelGBufferBatch`,
585 `ModelAlpha`, 261 `ModelForward`; each view queues its own
`SetShadowMapTarget` (5 or 6) and so its own shadow maps.

**Changes.**

- `src/stereo_probe.cpp`:
  - `eyeTemporal` / `eyeRenderPercent` (`SpidyEyeTemporal`: percent in bits
    0-7, bits 8 up: 1 jitter, 2 temporal anti-aliasing, 4 its command where
    the profile leaves it out). The VR worker sets 7 and the session's
    `upscale` before it starts the views.
  - `initBuffers` (hook on `1873470`): an eye's set is sized at
    `spidy::eyeRenderSize` (percent of the eye per side, multiples of 8),
    and made with the kind mask's bit 4 set for the call (`3d11b18`, entry
    bytes checked, restored after).
  - `sizeBuffers` (new hook on `1874f40`): the anti-aliased image's size in
    an eye's set is the eye's, and flag 2 ("both sizes equal") follows.
  - `copyJob`: an eye's render context gets anti-aliasing 1.
  - `setupDisplay`: queues TemporalAa where the profile's `+68c` leaves it
    out (it did not, with these settings).
  - `eyeJitter`: a Halton (2, 3) point per frame, as the game's camera gives
    its own view (descriptor `+428`, `+42c`: twice the pixel offset over the
    render size); 8 phases at 100%, 18 at 67%, 32 at 50%. The flat screen
    (the eyes copy the stock camera) takes the eyes' jitter on the stock
    lens.
  - `restGameView` (views option 32, with 8): while immersive, both eyes
    live and no game screen wanted, the game view's `+1208` is cleared, at
    which `19206a0` (SubmitScene) leaves at once; set again as soon as any
    of those ends, at `SpidyStop`, and never on a view that replaced it.
  - `shareExposure` / `update`: while the game's view rests, the right eye
    takes the left eye's adapted luminance (buffer and processor copy) and
    the left keeps its own.
  - Diagnostics: `SpidyCommandTrace` / `SpidyCommandData`.
- `src/native_eye_gpu.cpp`: `mirror()`; in the Present hook, while wanted and
  no screen copy is, `mirrorEye` draws the newest staged left eye over the
  back buffer (one triangle, one linear sample, the eye cut to the window's
  shape about its middle; display-encoded values through unchanged; a float
  back buffer is left alone). `SpidyPresentCount` (presents, or with 1 the
  mirrored ones).
- `src/game_xr.cpp`: XrConfig 21 (656 bytes): `upscale` (50-100, default 67)
  and a spare word; options bit 15 (no eye anti-aliasing) and 16 (the game
  renders its own view); views flags 13|16|32 by default.
- `include/spidy/eye_resolution.hpp`: `validEyeUpscale`, `eyeRenderSize`.
- `tools/run_game_vr.py` (`--upscale`, `--no-eye-antialiasing`,
  `--game-monitor-view`; the console's rendering line; `eyes` in reports),
  `tools/launch-game-vr.ps1` (`-Upscale`, `-NoEyeAntialiasing`,
  `-GameMonitorView`), the launcher (Upscaling: Off, High 77%, Quality 67%,
  Balanced 58%, Performance 50%; `upscale=` in launcher.ini).
- `tools/vr_display.py`: `VSync` 0 among a session's settings, restored with
  the others.
- `tools/probe_vr_load.py` (views 61 and `--upscale` by default, per-view GPU
  time), `tools/probe_exposure.py` and `tools/probe_hud.py` (`--views`,
  `--upscale`), `tools/probe_render_commands.py`, `tools/close_game.py`.

**Measured with them** (the same spot and eye size, a fresh game for each;
`reports/perf/final-new100.json`, `-new67`, `-new50`):

| Upscale | Scene | Ahead | Turning | GPU busy | GPU ms: left, right | Video memory | Committed |
|---|---|---|---|---|---|---|---|
| 0.2.11 | 3072 x 3264 | 35.6 | 36.9 | 99% | 10.4, 10.1 (+5.1 game view) | 6,378 MB | 13,785 MB |
| 100% | 3072 x 3264 | 39.4 | 39.2 | 99% | 11.0, 11.8 | 6,744 MB | 14,195 MB |
| 67% | 2064 x 2192 | 61.0 | 59.6 | 99% / 97% | 6.3, 7.1 | 5,766 MB | 13,186 MB |
| 50% | 1536 x 1632 | 77.1 | 69.6 | 97% / 86% | 4.7, 5.4 | 5,367 MB | 12,810 MB |

Render commands fell from 7.6 to 5.6 MB a frame (11.6 to 8.4 turning). The
game's view resting, everything else equal (67%, anti-aliased;
`reports/perf/m-scale67` and `e9-rest`): 49.8 to 61.2 frames a second ahead,
48.1 to 59.8 turning.

The table and the checks below are of the module as built before 22:38
(`dll_sha256` a6fe634e...). Comments in `stereo_probe.cpp` and `game_xr.cpp`
were edited after them and the module built again at 22:56 (86938153...,
the one in the play folder); that one, measured the same way at 67%
(`reports/perf/final-ship67.json`): 60.4 frames a second ahead, 60.0
turning, eyes rendered at 2064 x 2192, 5,756 MB of video memory, 13,183 MB
committed. The exposure and HUD probes were not run again on it.

**Checks in the game.**

- Images, left eye, one viewpoint, a crop at full size
  (`reports/perf/q-old`, `q-100`, `q-67`, `q-50`): 0.2.11's beams are
  stair-stepped and its scaffold breaks into moire; at 100% and 67% the
  edges are clean and the scaffold whole, 67% slightly softer; 50% softer
  again; the HUD and subtitles are as sharp at every setting. A frame taken
  while the head turned 26 degrees a second at 67% (`q-67`, `-moving`): a
  window grid with no trails.
- The game's view resting: `+1208` 0, back at 1 after the stop; the game
  goes on presenting, each present with the left eye drawn over
  (`SpidyPresentCount`); the window shows the left eye's middle band with the
  HUD (`reports/perf/win-rest.png`). Render commands a frame, on and off
  (`reports/perf/trace-main-on.json`, `-off`): the view's own lists fall
  from 677 to 7 (Scaleform, VisualEffectsCPUReadback and
  SceneObjectRegistryFixup stay), the lists no view owns from 10,106 to 6,518
  (ModelShadowCasters 8,556 to 5,604), the eyes' own stay. The eyes' images
  with it on and off, each eye exposed on its own: mean colour
  (93.3, 85.8, 94.9) and (93.5, 85.8, 94.8).
- `tools/probe_exposure.py` (views 61, eyes of 1536 x 1632;
  `reports/perf/exposure-rest.json`): the right eye's image over the left's,
  median of the blocks compared, 0.9989 to 1.0011 at five poses (level, the
  head on either side, a quarter turn, looking up); each eye on its own,
  0.44 to 2.26.
- `tools/probe_hud.py` (views 61, eyes of 1536 x 1632 at 67%;
  `reports/perf/hud-rest.json`): `ahead flat:1 flatview flat:0 back
  screen:menu resumed turn:90 turning`: the panel placed in every frame, 0
  rejected; the flat screen shows the stock camera, anti-aliased; with the
  eyes' lease lapsed the window shows the game's own view again
  (`menu-window.png`); the eyes resume as before.
- 235 core checks (1 new: the upscaled sizes), 23 launcher checks (the
  `--upscale` argument), 106 Python checks (3 new: the upscaled sizes and
  console line, `--upscale`'s range, vertical sync saved and restored) and
  the GPU test pass.

**Not verified.**

- The headset: nothing here went through OpenXR. The frame rate there (the
  runtime's encoding shares the GPU), the upscaled image in motion, and the
  monitor during play.
- The picture's brightness with the head on its side: it now follows the
  left eye alone (the exposure probe's left eye adapted to 0.30 and 0.59
  looking the same way with the head on either side); the game's view,
  covering both lenses, was one value for both. `-GameMonitorView` is the
  old way.
- SSR and volumetric fog in the eyes: an eye's buffer set never had their
  targets (above), before this build or in it. (SSR's since October 11's second
  build.)
- Other graphics cards, and the game's other settings (TAA without an
  upscaler, SMAA, DLAA, ray tracing): the eyes' context is set to temporal
  anti-aliasing whatever the game's is.
- One game crashed while it sat between measurements with only the input
  bridge and the pad module started (`Spider-Man.exe+2fde47d`, a null
  `[rbx+148]` on one of the game's own threads); not seen again in the
  launches after it.

**Left for later.** Each view renders its own shadow maps; the eyes could
share one set. DLSS in place of the engine's upscaler needs an instance per
eye (the game's Streamline calls use viewport 0). The XR worker copies the
staged pair into the headset's images at every headset frame, new or not.

**In the play folder** (`Documents\Spidy-0.2.11`, October 10, 23:12):
`build\windows-ninja\spidy_stereo_probe.dll`, `tools\run_game_vr.py`,
`tools\vr_display.py`, `Spidy Launcher.exe` and `README.txt` (this build's
`docs/PLAYERS.md`), the files they replaced in its
`reports\backups\play-folder-before-upscaling-20261010-231248`. Its other
modules are as they were (the movement and ray modules are the seventh
build's). With the play folder's Python (3.12.10) its six modules load and
export every entry `run_game_vr.py` names, and `run_game_vr.py --help` lists
`--upscale`. Nothing was started from the play folder: the launcher there,
and a session from it, are untried.

## Dives on physics steps over 50 ms — preceding build

The user, October 10: "I get pulled down strongly mid swings sometimes, which
stops the web and drops me to the ground super fast, happened last session at
least twice".

**What the report shows.** `game-vr-20261010-193410.json` (the play folder
`Spidy-0.2.11`, Quest 3 through Virtual Desktop, 3072 x 3264 per eye, swing
speed 32, weight 80): 161 s of `motion_samples`, the mover's step (`dt`, the
game's smoothed frame time: steps 114, 174 and 223 ms apart in wall time
carry a `dt` of 43 to 45 ms) at a median 38.6 ms. Three handoffs, by the
samples' `qpc` from the first one:

| At | Step | Before (driven) | After | Fall |
| --- | --- | --- | --- | --- |
| 140.48 s | 50.9 ms | both webs, +9.7 m/s up | -42.0 m/s, `air_vertical` -42.8 | 10.05 to 1.00 m in 0.24 s |
| 153.39 s | 50.4 ms | both webs, +6.5 m/s up | -42.0 m/s, `air_vertical` -42.8 | 24.91 to 1.32 m in 0.52 s |
| 159.81 s | 51.7 ms | both webs, on a wall run, +1.6 m/s | -42.0 m/s, `air_vertical` -42.8 | 28.54 to 1.31 m in 0.77 s |

In each, the last step sampled before it was 46.2 to 49.5 ms and driven
(status 2), and the step itself is status 1 with serial 0: it ran without the
command. The swing's samples stop there and come back with no webs and
`owned` 0. The retained samples have 22 steps over 50 ms: these three falls
and the standing after the last. None is driven.

**Why.** Two limits, both from the first native swing builds:

- `native_movement.cpp` (`query`, `handleAir`, `queryDriven`) drove a step
  only when `dt <= .05f` and `length(c.velocity) * dt <= 2`. Otherwise the
  step ran stock: in midair on the game's airborne state, at the fall speed it
  keeps counting (the "Midair drops" section below).
- `Swing::predictNativeStep` returned an invalid prediction for
  `seconds > .05f` after `releaseAll()`, and `game_swing.cpp` cancels the
  swing on an invalid prediction (webs released, movement relinquished).

The October 7 changes kept every other let-go from handing the player over
in midair and left "an invalid prediction still cancel[s]"; a step over
50 ms was such a prediction, and the module's own refusal came a step earlier.

**Changes.**

- `native_movement.hpp`: `drivable(dt)` (finite and positive) and
  `stepDelta(velocity, dt)` (`velocity * dt`, limited to `farthestStep`, 2 m).
  The three hooks drive every drivable step by `stepDelta`; the airborne
  event's displacement override is the same vector. A request of the size the
  module already accepted, for a step of any length.
- `Swing::predictNativeStep`: no upper limit on `seconds`; a step over
  `longestNativeStep` (100 ms) is predicted as one of that length. A step of
  no length, a non-finite one and unfocused input are invalid as before.
  `Swing::settleStep` winds the rope for the share of a longer step that was
  predicted.
- `game_swing::InFlightStep`: the travel of the step in flight and the
  velocity the previous command should have produced both go through
  `stepDelta`, with the previous step's own length, so a step the module
  shortened is not taken for a collision (which removes the velocity into the
  "surface": here all of it).
- `game_swing.cpp`: the movement lease is `2 * longestNativeStep` plus the
  16 ms tick grain, 216 ms (the module accepts up to 250). The request's cap
  is unchanged (1.75 m a step, written as 0.875 of `farthestStep`): under
  18.3 steps a second it is below 32 m/s.

**Checks.** Full observer/XR build with the Visual Studio 2022 Build Tools
(MSVC 14.44), no compiler warnings: 234/234 core checks, launcher checks,
103/103 Python checks. New core checks:

- "a swing goes on through physics steps longer than 50 ms": the report's
  steps (44.4 to 66.5 ms and back to 41.2) through the simulated mover
  (`NativeMover`, which now moves by `stepDelta` and caps its request as
  `game_swing.cpp` does), from 20 m/s at the bottom of a 60 m rope. Every
  prediction valid, the web held, the body within 5 cm of its rope (1 mm
  measured), the observed velocity changing by under 1 m/s a step (0.44
  measured, the rope's turn and gravity), never downward, 18.2 m/s at the end.
- "a step the movement module shortens takes no speed from the flight":
  `InFlightStep` alone (shortened: the solver's velocity kept; blocked by a
  wall: removed, as before), and free flight at 60 m/s under a 65 m/s limit
  with the step going from 28 to 35 ms: never under 49 m/s, then the longer
  step's cap of 50.
- "a movement command moves a step of any length, 2 m at most, and is never
  refused for it"; "a native step longer than the longest moves the body as
  far as the longest".
- Changed: "external prediction keeps its web through a long step and
  relinquishes unfocused input" (was: "relinquishes stale or unfocused
  input", which required a 51 ms step to release the web); the rope
  correction check gains the share of a 200 ms step.

With 0.2.11's logic under the new names (`drivable` with the 50 ms limit,
`stepDelta` unlimited, `predictNativeStep` and `settleStep` as released),
built with clang (Emscripten, where 232 of the 234 checks build), those six
checks fail and the other 226 pass.

The two modules from this build load in the play folder's Python and export
every entry `run_game_vr.py` asks for. They are in the play folder
(`Spidy-0.2.11\build\windows-ninja`) since 20:02, the released ones in its
`reports\backups\play-folder-before-long-steps-20261010-200244`.

**Not verified.** Anything in the game or the headset: no probe ran, and the
game was not started. That the native request behaves the same in a 51 to
67 ms step as in a shorter one rests on its being the same call with a
displacement inside the range used before. The web grab still stops stepping
when its step passes 50 ms (`game_grab::due`, `game_grab::step`,
`WebGrab::step`): a held prop or thug is let go there, which the report does
not exercise (no grabs). A session below 20 steps a second is the check: in
its report, `motion_samples` with `dt` over 0.05 should stay at status 2.

## Desktop mirror and small facade ledges — preceding build

The user reports the camera behind the body in the desktop mirror only,
and falling off buildings while walking across small ledges.

`stereo_probe.cpp` now places the main view again immediately before the
offscreen render jobs, using the same eye command and final player-travel
offset as the eyes. The earlier placement still supplies culling and
lighting. The fallback that hides an unsolved hero also covers this head
mirror; the stock monitor option and flat-screen mode keep their cameras.

`Swing::senseWall` looks along the facade for a ledge lip, samples its front
before the capsule reaches it, and keeps clearance until the feet pass it.
Tracing the lip also catches strips too thin for an endpoint sample. Shallow
setbacks up to the 0.75 m step allowance stay attached while clearance
settles; deeper setbacks and outward web pulls still release.

Checks: full observer/XR build, 230/230 core checks, launcher checks, and
103/103 Python checks pass. Three new regression checks cover 0.7 m
projections, upward and downward passage over a 0.4 m sill, sideways passage
over an 0.08 m strip in both directions, and shallow/deep setbacks and pulls,
at 90, 30 and 20 Hz. Capsule clearance is checked along each requested step.
The existing roof, corner, mount, wall-jump and web-pull checks still pass.

Live check: the preceding build's stationary body probe produced matching
headset and desktop views (`reports/mirror-before.json`, images preserved in
`reports/mirror-before-images`). It did not reproduce the reported fault.
The final moving-camera probe did not complete: one run rejected a module
rebuilt after the game loaded it, and the last attempt found the game closed.
The exact mirror failure and the ledges still need an in-game/headset check.
`probe_game_body.py` now saves desktop images alongside the eyes and records
the desktop camera's distance from the actual eye-view midpoint.

## One exposure for both eyes — preceding build

The user, October 10: "there is a visual bug where a discrepancy happens in
brightness and shadow rendering between left and right eye. there's an auto
brightness or something and it's calculated per eye I think and looks bad -
for example standing on the side of a building - one eye that sees more of
the sky is brighter than one that's closer to the city, if I rotate around to
look the other way they flip".

**What the executable shows** (offline, `tools/research_game.py`; shaders
from its embedded DXBC with `D3DDisassemble`).

- A view's setup (`189ce20`) makes it an "Adapted Lum" buffer (`+16f0`: four
  floats; its shader view at `+16f8`, its writable view at `+1700`), a
  256-bin "Lum Histogram" (`+1728`) and a readback of each (`+16a0`,
  `+16c0`). A view made with a parent (mode 2) has the parent's buffers
  passed to its own (`1893fd0`'s last argument); offscreen views such as the
  eyes are made without a parent.
- The luminance pass `18449f0` runs per view on the render thread: it sums
  log luminance over the view's image, then a one-thread compute shader
  (`g_LumAccumBuffer`, `g_RWAdaptedLumBuffer`) moves element 3 of the view's
  buffer towards the new value at the pass's rate and stores the compensated
  luminance in element 0, its inverse in element 1 and the measured value in
  element 2. It then binds the buffer's shader view at `t67` for all graphics
  stages (`g_AdaptedLumBuffer`) and queues the readback.
- The tone mapper `1846c20` binds `189c710(view)`, the same shader view, at
  `t11`; eleven tone-map compute shaders declare it (`g_TmAdaptedLumBuffer`),
  and the one disassembled multiplies the radiance by element 1 times
  `m_AcesMiddleGray` and does not read the constant made from the processor's
  copy (`m_InvAdaptationLum`, from view `+1708`).
- `18a01c0` reads the readback into `+1720`/`+1724`, and `189bd30` moves
  `+1708`/`+170c` towards those. Spidy's `update` hook copied the main view's
  `+1708`/`+170c` (and, until this build, `+1720`/`+1724`) into each eye: the
  processor's numbers were shared, the picture's exposure was not.
- Render jobs work on a per-frame copy of the view's first `0x1f70` bytes
  (`19223e0`, stored at settings `+4b0`); the luminance pass, the tone mapper
  and the readback all take the buffer from that copy.

**What changed.** `shareExposure` (`src/stereo_probe.cpp`), in the `copyJob`
hook: the eye's copy gets the main view's shader view at `+16f8` (as
`189c710` picks it: the view's own buffer, else its render buffers'
`+12ae8`), when the main view exists (pool bit 0) and both have a buffer.
The eye's own buffer and its writable view stay, so the eye's pass still
adapts its own buffer and reads it back (`+1720`: what the eye would have
used), and nothing writes the main view's buffer but the main view. The
`update` hook copies only `+1708`/`+170c` now. `0x189c710`'s first bytes are
checked at start with the other entries. `SpidyEyeExposure(1)` turns the
sharing off for comparison. `SpidyAppearanceData.exposureShared` (the unused
field at 252, no version change) counts the copies; `run_game_vr.py` reports
it as `appearance.exposure_shared`.

**Checks.**

- Core: 227/227. Python: 103, 1 new (the probe's head with roll; the columns
  both lenses see; block light of a scene drawn by two eyes, equal and 0.8
  times as bright; black images; the assessment fails when shared images
  differ, when the shared phase did not share, or the own phase did).
- In the game, `tools/probe_exposure.py` (report `reports/exposure-probe.json`,
  images in `reports/exposure-probe-eyes`), eyes 1536 x 1632, views 29, the
  user's save on the Fisk construction site (-208, 63, 2611) at sunset, HUD
  off. Per pose: 4 s each eye on its own, both eyes saved, 1 s shared, both
  saved. Left eye over right eye, median of the block ratios over the part
  both lenses see (428 to 575 blocks of 576), linear light:

  | Pose | Own | Shared | Adapted luminance: game view, left, right |
  | --- | --- | --- | --- |
  | level | 1.278 | 1.0002 | 0.472, 0.427, 0.518 |
  | roll 90 (right eye below) | 0.430 | 1.0005 | 0.413, 0.630, 0.315 |
  | roll -90 | 2.224 | 0.9996 | 0.442, 0.343, 0.655 |
  | yaw 90 | 0.628 | 0.9997 | 0.477, 0.668, 0.437 |
  | yaw 180 | 1.419 | 1.0013 | 0.149, 0.138, 0.174 |
  | yaw 270 | 1.264 | 0.9998 | 0.207, 0.191, 0.225 |

  The eye with the higher luminance of its own (more sky in its lens) is the
  darker one in every pose. `exposure_shared` rose by 0 in each own phase and
  by 244 to 298 in each shared second; the processor's copies in use were the
  game view's in all three views (0.47197 against 0.47199).
  The blocks spread around that median in the shared images (a tenth below
  0.83 to 0.96, a tenth above 1.02 to 1.12): near things sit at different
  places in the two images, among them two dark pieces of the hero's head
  right in front of the probe's eyes (they look like the mask's eye lenses;
  the probe runs no body, and they are in the own images too).
- Stops: eye views 0, GPU 0. The game closed with WM_CLOSE in 1.5 s; display
  settings restored. Saves: `slot0-s.save` re-saved by the game after
  Continue (13:16:55), the other ten files byte-identical.
- Play folder `dist\Spidy-0.2.9`, 13:19: `spidy_stereo_probe.dll`
  (`9e6ff833`, the build the probe ran) and `tools\run_game_vr.py` (the
  folder's file plus the `exposure_shared` and `marker_mismatch_m` reads;
  the tree's file has other sessions' launcher changes) hash-verified, import
  check 42, backup in its
  `reports\backups\play-folder-before-exposure-20261010-131936`.

**Not tested.** The headset. Other things that may differ per eye (the
per-view "Fog Color Map" at `+1030`, screen-space effects) were not examined;
the shared images' medians leave no room for an overall difference at these
six poses. A session whose main view is not on the head (`-StockMonitorView`)
exposes both eyes for the stock camera's picture. In a session report,
`appearance.exposure_shared` should be twice the eye frames.

## Walking up onto a wall, and the view while walking it — preceding build

The user, October 10, after the first session with the walls: "frequently wall
walking isnt triggred properly and the regular game wallcrawling mechanic
clicks it, also when walking towards a wall and starting to walk on it the
perspective change happens only once i let go of the joystick".

**What the report shows.** `dist\Spidy-0.2.9\reports\game-vr-20261010-112053.json`
(the second build, 627 s of samples; swing samples matched to the mover's by
`source_step`, the XR samples by the best overlap of a turned view with the
swing's wall: 105 samples later, 3417 of 3418 turned samples on it).
The swing's walls took the player 46 times (`walls` 19 to 65), 8 jumps off.
The game's crawl (`wall` 2: the actor turned, a ray down its up meets the
wall) held him five times, 36.4 s in all: at 243.1 s (10.1 s), 430.0 s (9.6 s),
439.7 s (7.0 s), 483.2 s (5.2 s) and 491.1 s (4.5 s), each straight out of a
stretch on the ground, speed 3 to 5 m/s into a wall and then 0: at 429.68 s he
stops against the wall, at 429.83 s the mover is `0x80a6` / `0x2060005` and
rising. In the crawl the game reports the body as standing (`grounded` 1 in
all 709 crawl samples), so the swing, which leaves a standing body alone,
never saw a wall there. The view: `view_tilt_deg` 0 through all five. On the
swing's walls it turned only below 1.5 m/s: from 73.5 s he walks up a wall at
6.0 m/s for 29 s, tilt 0 until he stops; likewise 20.0 s (15 s), 203.6 s.
Also in it, unchanged by this build: a walk up a wall over a ledge 3 to 4 m
deep flies over the ledge onto the next wall (72.7 s, 500.4 s: the top edge's
hop, 2.3 m high under the swing's gravity), and at 305.9 s a wall run at
28.5 m/s met something on the wall no ray saw and stopped dead, the game's
fall taking him for 0.45 s.

**What changed.** `Swing::senseWall` takes the input. A standing body
(`SwingConfig::mountReach` 1.3 m, `mountSeconds` 0.1, `mountHold` 0.5,
`mountHeight` 1.2): a ray along the level stick (tilt 0.35 or more) meets a
wall it faces within 45 degrees, and a second ray 1.2 m higher meets the same
wall; held for 0.1 s, `mounting()`; from the first step, `mountBegun()`.
`game_swing` runs a second `SwingTakeoff` on it (the game's jump: 60 ms
released, pressed up to 160 ms) and reports `Data::mount` (1; the last field
of version 4, unused before), on which the XR worker keeps the stick and the
walking keys from the game (`swingNativeKeys` with the mount as owned flight,
`game_pad::Walk` zero). Off the ground within `mountHold` of the last
mounting step, the body joins that wall with its speed along it, at most the
walk's; again if the game stood it in between. In the air a wall within
`mountReach` that the stick faces within 60 degrees is joined with no closing
speed, the speed off it removed. The game's crawl: its normal is now found
before the solver's step, and while the game reports the body standing it is
offered to the solver (`Swing::offerWall`), which asks for the same jump
(`mount` 2) and joins that wall at rest. `GameTrackingRig`: standing latches
below `wallStrideSpeed` (7.5 m/s) while the left stick is tilted past 0.2, as
well as below `wallStandSpeed` (1.5).

**Checked in the game, no headset** (`tools/probe_wall_mount.py`, the user's
save at the Fisk site, the brick wall 17.7 m off, three fresh games):

| | run 1 (first build of it) | run 3 (this build) |
| --- | --- | --- |
| stick at the wall to `mount` | 0.11 s (after the wait) | 0.00 s |
| to the game's jump asked | 0.17 s | 0.16 s |
| to off the ground, and on the swing's wall | 0.23 s | 0.22 s |
| the game's crawl during the mount | none | none |
| climb, stick held | lost the wall after 0.17 s | 6.0 m/s, every step the swing's |
| centre from the wall (world ray) | 3.2 m, falling | 0.90 m climbing, 0.90 m stopped |
| crawl began after meeting the wall | 0.09 s | 0.06 s |
| crawl to the jump asked | 0.24 s | 0.23 s |
| crawl to the swing's wall for good | never (2 steps, then off) | 0.66 s, at rest, 0.90 m |

Run 1's failure, by its samples: at 14.547 s the mover leaves the ground
(`grounded` 0) and the wall takes the body; the next step reports `grounded`
1 again with the mover in its air flags (`0x80a0` / `0x2060010`, contact 0),
the wall lets go and the command is given up; the step after, airborne, the
body moves as the first command moved it, (-3.8, 6.0, 1.3) m/s: 4 m/s off the
wall, the push out to the clearance. The stick took the wall again with that
speed, which nothing removed, and 0.17 s later he was past the wall's reach.
Run 2 (the fixes) passed the mount; its hand-over never met the game's crawl,
because the controller walked the player off the 0.8 m ledge he stood on, and
the swing's wall took him as he fell at the wall. The probe now goes down
again from there and walks him into the wall from the ground. Run 3's
hand-over: crawl at 23.266 s; `wall` 2 from 31 degrees of tilt (23.391 s);
`mount` 2 and the jump asked for at 23.500 s; at 23.657 s the game leaves the
crawl (`0x80a6` / `0x2060000`, contact 2, 78 degrees) and the swing has the
wall for two steps; the game then stands him at the wall's foot (36 samples,
0.19 s) and jumps (9.4 m/s up, 5.8 off the wall); at 23.907 s the swing has
the wall again, brings him to 0.90 m and holds him at rest, the mover in the
air mode, until the probe's own jump 1.3 s later.
The save: `slot0-s.save` rewritten by the game at 12:03 (its autosave on
loading); the other ten files byte-identical to the backup
(`reports\backups\save-before-wall-mount-20261010-120230`).

**Checks.** 226 core (5 new: "walked at a wall the standing body mounts it by
the game's jump", "a stroll past a wall, a glance at it, a kerb and a far wall
mount nothing", "the game's crawl hands its wall to the swing as the body
leaves it", "pushed at a wall beside it a falling body takes the wall", "game
rig turns the view as soon as the player walks the wall, not on a run the
stick steers"), 22 launcher, 101 Python.

**Not verified.** The headset: the hop, the view turning during the climb,
mounts nobody meant (fights beside walls, walking up to something on a wall).
The XR worker's part (the stick kept from the game during a mount) ran only
as the probe's stand-in for it. The hand-over higher than a metre above the
ground, except by the user's own A press in the 11:20 session (253.3 s).

## Walls: run, stop, walk, jump, and a view that stands on them — preceding build

The user, October 10: "autosticking, walking on walls and wallrunning feels
prettly bad right now - autostick mechaninsm is clunlky both to get on and off
a surface, you need to jump off walls using a, maybe lets align camera to
orient by the new (floor). for wallrunning (specifically while actively
swinging lets make it better feeling".

**What the reports show.** The October 9 sessions (`dist\Spidy-0.2.7`
15:37, `dist\Spidy-0.2.8` 19:51; motion and swing samples matched by time).
A swing that meets a wall: the mover goes from `0x8020` / `0x2060010` to
`0x80a0` / `0x2060000` at contact 2 (sweeping, out of the air mode, the game's
air events stop). There a command with any part into the wall is carried out
not at all, its part along the wall included (19:51 at 161.2 s: requested
(29.1, -0.5, 13.3) m/s, achieved 0 for 1.0 s); one with none is (436.0 s),
and the air mode comes back when the body has left the wall. After 1.0 s in
that state the game turns the actor onto the wall (`0x80a6` / `0x2060005`, 0.6
to 0.7 s), then holds it in its crawl (`0x8001` / `0x2060000`, contact 0),
which the takeoff's jump leaves in 0.5 s when it lands outside the turn and
1.4 s when it does not. 160.9 s to 164.0 s: 28 m/s to 0, 3.1 s on the wall.
The game's classes for it (RTTI names in the executable): `HeroStateWallGrasp`,
`HeroStateWallRunIdle`, `HeroStateWallRunToWallCrawl`, `HeroStateWallCrawlEnter`,
`...Idle`, `...Move`, `...ToJump`.

**What changed.** `Swing` (`SwingConfig::walls`, on in the game's swing):
`senseWall` once a native step, `wallMotion` and `holdWall` each solver step.
Off a wall three rays (the level heading and both its sides, 1.5 clearances
plus the step's travel long) find a fixed surface within `wallSlope` of
vertical that faces the ray; the body joins it when it closes on it faster
than 0.5 m/s and would be within `wallClearance` (0.9 m, centre to wall) by
the step's end. On it one ray down the wall's normal follows it (the normal
eased over 0.08 s), one along the travel finds an inside corner, and, the
wall's ray missing at a walk, one from beyond the wall's end finds the next
face, which the body rounds at 3 m/s. Joining: the speed along the wall times
min(|v|/along, 1 + `wallCarry` x into/|v|). On the wall: no gravity; the
stick's direction with its part into the wall turned up the wall, at
`wallWalkSpeed` (6 m/s) or the run's own speed that way, by `wallAcceleration`
(24 m/s^2); without stick or web `wallBrake` (3 m/s^2) above the walk's speed
and a stop below it; never nearer than the clearance, back to it at 2 m/s
from further, out to it at 4 m/s from nearer. A jump: out `wallJumpOut` (6),
up `wallJumpUp` (5), no wall again for `wallJumpPause` (0.3 s). The wall's ray
missing for `wallGrace` (0.15 s) ends it; going up it then, a hop
(`crestHop`, 4.5 m/s) and a push over the roof for `crestSeconds`. More than
`wallReach` (1.5 m) from it and not closing: off. `releaseAll` keeps the
wall, `leaveWall` does not. `game_swing`: the swing drives while a wall holds
the body, also from the game's own jump or fall; a pause keeps the wall
(`cancelSwing(true)`); `Data` version 4 (272 bytes: `wall` 0 none / 1 the
swing's / 2 the game's crawl, `wallNormal`, `wallDistance`, `walls`,
`wallJumps`), the crawl's normal from a ray down the actor's up; `Settings`
version 4 (32 bytes, `walls`; version 3 still read). `GameTrackingRig`:
`SurfaceHold` and `standOnWalls`; standing (speed under `wallStandSpeed`,
1.5 m/s, then until the wall lets go) turns the level tracking space about
the feet at `wallTurnRate` (a quarter turn in 0.15 s), each frame the short
way from where its up is, with its floor moved onto the wall under the body;
level again, the turn left about the vertical goes into the yaw. The swing's
stick is turned with it, the game's stays level. No flip on a wall. The XR
worker passes the swing's own walls only (the game's crawl: upright and stood
off, as before). `vr_settings::Values::standOnWalls` (on), row 14 STAND ON
WALLS, the last of COMFORT (18 rows); XrConfig version 20 (options bit 13 the
view upright, bit 14 the walls the game's; up to 32767), XrData version 20
(settings bit 128, `viewTilt` degrees at 796 where `spare` was). Launcher
`SessionOptions::standOnWalls`, `--no-stand-on-walls`, `stand_on_walls` in
launcher.ini and the headset's line, "Stand on walls"; `launch-game-vr.ps1
-NoStandOnWalls`, `-NoWallRun`.

**In the game, no headset** (`tools/probe_wall_run.py`, the user's save in a
Fisk hideout, October 10; `reports/wall-run.json`, `reports/wall-run/`). The
nearest wall: brick, 17.68 m from the hand, level, normal (-0.945, 0, 0.327),
the ray 21 degrees off it. Jump, web, reel: 23.7 m/s into the wall and 21.0
along it at 1.95 s, when the swing had the body on the wall (0.84 s after the
reel began; 0.81 s on the first run). By 2.08 s the speed into it was gone:
22.9 m/s along the wall and 7.0 up, the centre 0.901 m from the wall (world
ray; 0.900 to 0.917 by the swing over 5.1 s). On every step of those 5.1 s
the mover was `0x10008020` / `0x2060010`, contact 2, driven (status 2), two
air events a step, the actor's tilt 0: none of the game's wall states. The run
slowed from 22.9 to 19.7 m/s in 1.1 s (3 m/s^2); the stick against it turned
it at 24 m/s^2 to 6.01 m/s the other way, level; the stick at the wall: 6.00
m/s up; let go: 0.00 m/s within 0.6 s, the height unchanged for 0.3 s after;
the jump: 6.01 m/s out, 4.3 m/s up 0.1 s later, then the swing's gravity.
All eight of the probe's checks passed, on the first build and again on the
last. With `--walls off` (`reports/wall-run-off.json`): the same reel met the
wall at 2.19 s, the mover went to `0x80a0` / `0x2060000` for 0.44 s, the speed
from 31 to 2.6 m/s, and the body fell 63 m to the street.
`tools/probe_menu.py`: 20 steps, no failures; STAND ON WALLS switched off as
the twelfth change, RESET ALL put eleven settings back (25 changes), the hooks
restored. Its first run opened Photo Mode: the hideout's pause menu has ABANDON
MISSION above PHOTO MODE; it now presses Up twice from RESUME.
`tools/probe_wall_crawl.py` (now with the swing's walls off, and reading the
surface the swing reports) did not reach the game's crawl from this spot: the
reel ended with the body hanging on its web 1.5 m from the wall. The crawl's
reported normal is therefore not checked in the game.
Saves: `slot0-s.save` was written by the game at 11:09 (its autosave); the
others are byte-identical to `reports/backups/save-before-wall-run-20261010-104456`.

**Checks.** 221 core checks pass; 9 new: a body flying into a wall at an
angle runs along it at the clearance, no gravity along it, and comes to rest;
head on it stops, the stick walks it (at the wall: up; away: down) and a jump
leaves it for good; a web's pull takes the body off and never through; over
the top edge it is pushed on over the roof; at a walk it rounds a corner onto
the next face, a run flies off; walls leave a standing body, a passing one and
a swing without walls alone, and let go when switched off; the view turns only
once the player rests, in 0.15 s, with head, hands, hand motion and the stick,
stays while walking, takes no flip, and comes back level over the feet; a
corner's next face turns it on and the heading is kept when the wall lets go;
the game's crawl turns it at once, and with STAND ON WALLS off it stands off
as before. The settings tab's rows, the launcher's options and line, and the
Python readers have the new setting and versions: 22 launcher checks, 101
Python checks.

**Not checked.** The headset: the feel of all of it, and the view's turn at
all outside its unit checks. In the game: a corner, a top edge, a facade with
ledges or recesses, low frame rates, a pause on a wall. The game's crawl with
the view turned (not passed to the view). Not in the play folder.

## FLIP SPEED, 180 degrees a second by default — preceding build

The user, October 9: "lets also do this much like how you can change the
speed of turning and weight in the game it'd be cool to have different flip
speeds you could toggle from the launcher", then "set default speed to
lower".

**What changed.** `FlipMotion::speed(radians a second)` sets `scale_`, the
stick's full-tilt speed over 2π/`holdTurnSeconds` (240 degrees a second),
clamped to 0.25-4 (not finite: 1); the held turn, a tap's flip (2π/
`tapTurnSeconds`) and the return (2π/`returnTurnSeconds`, twice from a
landing) all take it; the 80 ms spin-up, the 0.25 s tap and the easing angle
do not. `reset()` keeps it. `GameTrackingRig::flipSpeed` passes it on.
`vr_settings::Values::flipSpeed` (degrees a second, 180; `sanitized` 90-480),
steps 90/120/150/180/240/300/360/480, row 17 FLIP SPEED under FLIPS (tab rows
0x200-0x210). XrConfig v19 (`flipSpeed` where `spare` was, 90-480, else
1001), XrData v19 (800 bytes, `flipSpeed` at 792), menu probe structs v5
(`flipSpeed` where `spare` was). The XR worker applies it with the other
settings (start, the tab), and reports it. Launcher: `SessionOptions::
flipSpeed` (180), `--flip-speed` when not 180 (clamped 90-480), `flip_speed`
from the headset's line and launcher.ini, a "Flip speed" combo under
"Flips (experimental)". `launch-game-vr.ps1 -FlipSpeed`.

**Checks.** 212 core checks pass; 1 new (FlipMotion at 180 degrees a second:
a quarter turn over 0.5 s at full tilt within 0.02 rad, the way back from the
same angle and a tap's whole flip 1.25-1.42 times as many frames as at 240, a
whole turn exactly; after `reset()` the same speed; 1e6 rad/s clamps to four
times, NaN to once; `GameTrackingRig` at 120 turns half as far as at 240 in
40 frames after a `reset()`). The tab's checks: 17 rows, FLIP SPEED last
with one choice per step; its default choice 180, steps to 240, 90 and 480,
none past either end; a launcher 200 shows 180; `sanitized` clamps 9999 and
0. 21 launcher checks (default 180, `--flip-speed 240`, 9999 and 1 clamped
to 480 and 90, `flip_speed=300` and `=5000` from the headset). 89 Python
tests (XrData v19 at 800 bytes, `flip_speed` decoded from 792, the start
settings and the launcher line carry it).

**Not tested.** `tools/probe_menu.py` (its FLIP SPEED step and 23 changes)
has not run in the game; the launcher's new combo has not been looked at;
the headset: which speed feels right.

## The left stick flips without A — preceding build

The user, October 9: "when flips are turned on lets allow to use the
joystick to flip at all times (without holding a)".

**What changed.** `FlipMotion` (game_tracking) has `stickFree_`: set once
the left stick is in its dead zone (0.2) while the player is in the air and
on no wall or ceiling, cleared on landing (80 ms on the ground, a wall or a
ceiling) and by `reset()`. A free stick pushed past the dead zone in the air
starts the holding phase (or takes over a flip or a return to level where it
is, as a press of A does), and holding goes on while A is held from the air
or the free stick is pushed. With neither, it ends: a whole flip only if the
hold began with a press of A let go within 0.25 s with the stick never
turning the player alone meanwhile (`tap_`), else back level the short way
at 2π a second. The stick's speed, dead zone, spin-up and the return are the
held-A flip's. `steering()` (holding) still zeroes the swing's move and the
walk, so a flipping stick steers nothing. `GameTrackingRig` is unchanged:
flips off resets `FlipMotion` every frame, so the stick never arms.

**Checks.** 211 core checks pass; 2 new:
- `GameTrackingRig`: the stick held from the ground into the air walks and
  stays level for 30 frames; let go once in the air, then ahead a front flip
  (look below -0.5 in 30 frames), back a backflip, no move or walk while
  flipping; let go, not snapped level and level within 30 frames; landing
  mid-flip levels within 40 frames and the stick held from there walks, into
  the air too; on a wall (airborne as the game reports it there) the stick
  let go and pushed walks, level; flips off, the stick let go and pushed in
  the air walks, level.
- `FlipMotion`: the stick alone steers the flip and turns at 2π/1.5 s at
  full tilt (within 0.02 rad over 0.5 s); A pressed while it turns, the angle
  kept at rest within 1e-5; let go of A, back level no more than half a turn;
  a 6-frame flick turns less than a quarter turn in all; A tapped while the
  stick turns is no whole flip.
The six earlier flip checks pass unchanged (their stick is held from the
first frame, so it never arms). 21 launcher checks and the 5 Python suites
(89 tests) pass.

**Not tested.** The game and the headset: comfort of a stick that flips
with no button, losing air steering once the stick is let go in the air
(swinging included), and whether `game_swing::airborne` holds steady enough
on walls and perches that the stick never arms there.

## A steadier, sharper HUD, and a HUD setting — preceding build

The user, October 9, after a Quest 3 session (Virtual Desktop, eyes 3840 x
4080 at 125%) with the fifth build's HUD: "the hud is still a bit too blurry.
cam be smoothed a bit and follow the head maybe (its a tad jittery)", then
"also add option to disable hud in settings".

**Their session** (`dist\Spidy-0.2.7\reports\game-vr-20261009-143726.json`,
8 minutes): `hud.placed_frames` rose with every immersive frame (27,385),
second movie and markers on the panel throughout. 53.6 new eye pairs a second
on a 72 Hz display, 8.9 reused: about a quarter of what the headset showed
were earlier images turned by the runtime to the newer head, which turns a
head-locked panel with them (the jitter). The panel's texture had 1720
pixels across 60 degrees, about 1490 per unit of tangent, where 3840-pixel
eye images have about 1640 (Quest 3 lenses as `probe_hud.py` models them; the
blur, with text the game made for 720 rows). 1920 x 1080 gives 1660.
The game-exit report had no `hud` block (that report path never wrote one);
it now writes the last one.

**What was built.**
- `native_hud::Follow`: the panel's orientation in the tracking space, from
  the head's. Still within `hold` (2 degrees); beyond, it glides back at
  1 - e^(-dt / 0.15 s) a frame (at most a quarter second's worth) until within
  0.25 degrees; yaw and pitch only (level), the yaw from the face's direction
  within 64 degrees of level and from the top of the head nearer the poles.
  The XR worker updates it with each eye command and resets it without play,
  on the flat screen and when the room is recentred; `native_hud::follow`
  keeps its orientation seen from the head by command serial (64 entries).
  `latchHead` takes it for its command (`FrameHead::panel`, through
  `native_hud::turned`); `placeHud` and the marker projection use the turned
  pose. Without one (probes), the panel faces the head's way.
- HUD setting: `vr_settings::Values::hud` (0 off, 1 small, 2 medium, 3
  large; `native_hud::widths` 50, 60, 70 degrees), row HUD (OFF, SMALL,
  MEDIUM, LARGE) after GAME SCREEN SIZE: 16 rows, numbers 0x200-0x20f.
  XrConfig v18 (648 bytes: `hud`, `spare`), XrData v18 (`hud` where `spare`
  was, offset 748), `vr_settings.hud` in samples and the launcher line.
  Launcher: `SessionOptions::hud`, `hud=` in launcher.ini, a "HUD" choice
  after "Game screen size", `--hud N` when not medium. `run_game_vr.py
  --hud`, `launch-game-vr.ps1 -Hud`. Menu probe structs v4 (48 and 72 bytes).
  Off: `hudPlace` hands the panel's instance to `renderActor`, which skips it
  in eye views (`hidden_frames`, `hidden_draws`); the second movie stays in the
  game's view and markers keep the game's projection.
- Window: `vr_display.small_window` makes 16:9, 1080 rows, at most three
  quarters of the desktop's height (1920 x 1080 on 3440 x 1440 and 2560 x
  1440, 1440 x 810 on 1920 x 1080).
- SpidyHudData v3 (192 bytes): `size`, `hidden_frames`, `hidden_draws`,
  `followed_frames`, `follow_angle`, `follow_angle_max`. `SpidyHudSet` (probes)
  sets the size and a fixed turn.

**Tests.** Core 209/209: the tab test finds HUD at row 13 with its four
choices and FLIPS last at row 15; "the HUD: MEDIUM by default, OFF to LARGE";
HUD sizes clamp; a new follow test (still within the hold, the first glide
step, settled within 0.25 degrees after a second, 63% in one glide time, no
more than a quarter second's step, level with a tilted head and facing as the
face does, straight down finite, a reset without a glide; `turned` and a point
along it in the panel's middle; the three widths). Launcher 21/21 (`--hud 0`,
capped at 3, `hud=` from the headset line). Five Python suites (XrData v18
`hud`, SpidyHudData v3, the settings line with `hud=2`, the window sizes).
GPU test passes.

**In the game, without the headset** (`tools/probe_hud.py --width 3072
--height 3264`, after `probe_menu_pad.py start` and `pad a --until-player`;
the user's save at the Fisk construction site; `reports/hud2-probe.json`,
captures in `reports/hud2-probe-eyes/`):
- Window 1920 x 1080; the second movie's viewport 1920 x 1080, and the panel's
  texture made once at that size (`texture_resizes` 1; the game's own for
  this window is 1932 x 1080: its factor at `0x7a4ae50` is about 1.006).
- ahead, turned (40 degrees), turning (90 degrees a second): the panel at the
  same place; 1,621 placements, 0 rejected, offset mismatch 0 m.
- small, large: half widths 0.4663 and 0.7002 (50 and 70 degrees); a world
  marker near the crates on the same spot in both.
- panel15 (`SpidyHudSet` turn of 15 degrees): the panel, subtitles and minimap
  15 degrees to the left; `follow_angle_max` 15.
- off, flatoff: no panel and no subtitles in either eye, immersive and on the
  flat screen; 424 hidden frames, 848 eye draws left out.
- Back at medium, the game screen (eye commands paused) and the stop as in the
  fifth build: `restored` true, the game's texture (1932 x 1080 at the game's
  own window factor) bound again.
- At half the headset's resolution the subtitle line reads cleanly; the fifth
  build's (1720 x 720) was barely legible in the same crop.
- The user's saves: all byte-identical but `slot0-s.save`, which the game
  re-saved itself right after Continue (15:23:45), as in every probe.

**Not tested.** The headset: the follow's feel, the XR worker's
`follow`/`setSize` path and the sharpness through Virtual Desktop.
`probe_menu.py`'s new HUD steps have not run in the game.

## Default weight 80% and 12 s of slow motion — preceding build

The user, October 9: "lets set the default weight to 80, increase the max
duration of slow-mo".

- `vr_settings::Values::weight`, XrConfig's `weight`, the launcher's
  `SessionOptions::weight`, `run_game_vr.py --weight` and
  `launch-game-vr.ps1 -Weight` all default to 80 (was 60), and the launcher
  leaves out `--weight` at 80. The launcher saves `weight_percent=` in
  `%APPDATA%\Spidy\launcher.ini`. It reads the old `weight=` only when no
  `weight_percent=` is there, and takes 60 (the old default, saved for every
  player) as the new default.
- `SlowMotionTuning::drainSeconds` 12 (was 7); refilling unchanged (12 s,
  after 1.2 s).
- Tests: core 208/208 ("the defaults as the tab shows them" at step 2, 80%;
  the swing's default gravity 7.848 m/s²; focus empties at drainSeconds;
  the refused-press test now spends the meter below 85% instead of for 1 s,
  which no longer spent enough at 12 s). Launcher tests and ctest pass; game
  VR 45, launcher 16, observer 5, runtime 11, stereo 12 Python tests OK; the
  GPU test passes (slow motion's grade and meter).
- Not run: the game (no in-game probe; `probe_menu.py`, `probe_weight.py`,
  `probe_slow_motion.py` updated to the new default), the launcher's
  migration of an old `launcher.ini` (`loadSettings` has no unit test), and
  the headset.

## The HUD in VR — preceding build

The user, October 9: "lets fix the hud to stay view, be a bit sharper (its
very blurry rn) and showing missing elements like trackers that you see in
game".

**What the eyes showed.** The user's October 8 20:38 session (eye snapshots in
`dist\Spidy-0.2.5\reports\game-vr-20261008-203808-eyes`) had HUD pieces at
odd places that moved with the game's camera, not the head ("New Equipment
Available" top right, a gadget ring near the middle). A probe at the save
(views 29, eyes at the hero's head looking along the camera's heading) showed
only the health bar, small, left of centre; the gadget and minimap were out of
view.

**Where the HUD comes from**, read in the executable and measured live with
throwaway hook DLLs (scratchpad, not in the tree):

- `PlayerModelHudFollower` (vtable `0x38929c8`) places the panel in
  `0x73ab80`: the camera manager's camera (`0x1642040`, transform rows left,
  up, forward, position), forward 20 m (`+0x48`), scales x 14.44, y 10.68,
  z 11 (`+0x4c`), from a FOV of 1.082 rad and the window aspect factor
  (`0x72d420`, 2.404 / (16/9) = 1.352, kept at `0x7a4ae50`). It ends in
  `0x191c0e0`, which writes the render instance's transform at +0 and marks
  it changed. The stock camera's view is 0.8125 half wide (tangent): the
  texture covers that much of the panel, its middle 56%.
- Order on the main thread each frame (logged): camera submits (5 to 7), marker
  projections, the panel stream's update (`0x2104400`), the follower's update
  (`0x73a740`, which binds the texture when `+0xc0` is clear), its placement
  (`0x73ab80`), `0x1920240` (eye placement), the render command's enqueue
  (`0x1d277c0`), view maintenance.
- The second movie is the global `0x7be3e00`, viewport 1290 x 540 at +0xa0
  (Scaleform's 0x34-byte viewport; `SetViewport` is vtable slot 0x60,
  `0x31a9ba0`). The render command `0xa8` ("Scaleform", registered at
  `0x1d266ee` into the handler table at `0x7a2d1a8`) draws a list per frame:
  the panel's movie into a 2596 x 1080 target with clear, then this movie into
  the game's view. Changing the viewport, its Scale, or the render-side
  viewport did not scale this movie's content: it is laid out in pixels.

**Experiments in the game.** Appending a copy of the second movie's item with
the panel's target drew it into the panel's texture at 1:1 (top-left
quarter). Making the panel's texture the window's size (stream `+0x78`,
`+0x7c`, `+0x9c` = -1) re-created it at 1290 x 540; the panel then sampled a
freed texture (a washed-out frame) until the follower's `+0xc0` was cleared,
which bound the new one. With both, the copied marker landed within a few
pixels of the original on the monitor. Putting `+0x78`/`+0x7c` back
re-created it at 2596 x 1080.

**What was built** (see DEVELOPMENT.md, "The HUD in VR"): the panel placed in
front of the frame's latched head right after the follower's placement; the
texture at the window's size for the session; the second movie moved onto the
panel while the eye views are shown; the HUD's markers projected from the head
while immersive; the VR window at 720 rows. `SpidyHudData` v2; `hud` in
session reports.

**Checks.**

- Core: 208/208 (new: the panel's transform keeps the game's proportions, its
  centre 2 m ahead and the game view's part at the asked width; markers ahead,
  at the right and top edges and behind; texture base widths for 1290, 1720,
  2580, 3440). Python suites all pass (new: the `hud` block decodes and rejects
  torn reads; the five hooked entries are checked fresh and restored; the
  720-row window). D3D12 graphics test passes.
- In the game, `tools/probe_hud.py` at the save, eyes 1536 x 1632 and
  3072 x 3264, VR window 1720 x 720 (reports `hud-probe-final.json`,
  `hud-probe-full.json`):
  - First try, placing the panel at `0x1920240`: no effect (the eyes showed the
    game's placement). Moved into the follower's placement hook: the panel held
    the same image position looking ahead, 40 degrees right, 25 and 70 degrees
    down, mid-turn at 90 degrees a second, in both eyes, ~1 degree of disparity
    (2 m).
  - Its first size came out 0.6 of the asked width: the game's model is
    larger than its view. Scaling by the stock camera's half width fixed it:
    the HUD's elements span the asked 60 degrees.
  - Final run: 2,349 placements, 0 rejected, offset mismatch 0 m; the second
    movie on the panel every immersive frame (2,684 by the end, flat screen
    included); 2 markers a frame from the head (4,642); texture 1720 x 720 made
    once for the session; after stop 2596 x 1080 again, restored.
  - Looking down 25 degrees the interaction ring sat on its crate; looking
    ahead, with the crate below the panel, the game's edge indicator showed at
    the panel's edge, moving to the other edge after turning right.
  - The panel was not hidden facing the floor 1.7 m away: the GUI pass draws
    over the scene.
  - Eye views paused for 1.2 s (as on the game screen): the window showed the
    whole flat HUD, the ring and subtitles; the second movie had gone back to
    the game's view about 0.35 s after the last eye frame.
  - Flat screen (eye mode 2): subtitles and the ring show (before: neither).
- A first build crashed the game at the first marker: inside
  `native_hud::start`, the detour's name `project` resolved to the header's
  `native_hud::project`, so the game called the math function. The detours are
  named `marker`/`markerXY` now (minidump: `spidy_stereo_probe.dll+0x175bf`
  reading address 0x30). An earlier crash (`+0x17ff0a1`, occlusion readback)
  came from `probe_vr_load.py`'s occlusion on/off step, not this work.
- The save folder was byte-identical to its backup after all runs
  (`reports/backups/saves-before-hud-probe-20261009`).

Not tested: the headset; a fight, a QTE, an objective marker far away, a
level load or a respawn with VR running; another window size or aspect.

## Each enemy's own size — preceding build

The user, October 9, after hearing that the fists, web balls and webs took
every enemy for a street thug's size: "fix the size issue".

**Where the game keeps a bot's size.** The executable's strings name a
`MoverBodySize` (vtable `0x500e870`) with fields `BodyBottom`, `BodyTop` and
`BodyRadius`. `MoverConfig` (vtable `0x500e950`, 0x78 bytes) embeds one at
+0x58 (its constructor at `0x22c9300`). Every mover manager keeps its live
body at +0xdb8, after its mover's handle at +0xdb4: `MoverManager` code
reads +0xdc0, +0xdc4 and +0xdc8 there. `0x1fbba90` builds the mover's
capsule from them: the lower end at `position + up × (+0xdc0 − +0xdc8)`, the
upper at `position + up × (+0xdf8 × +0xdc4 + +0xdc8)`. So +0xdc0 and +0xdc4
are the spheres' centres above the transform, +0xdc8 their radius, and
+0xdf8 a scale on the upper one. Read from outside in the game, at the
user's save:

| Actor | Body at +0xdb8 (bottom, top, radius, scale) | Capsule above the transform |
|---|---|---|
| 12 Fisk thugs (`ThugBot`, `BotMoverManagerGame`) | 0.85, 1.15, 0.45, 1 (also in their `MoverConfig` at +0x48, +0x60) | 0.4 m to 1.6 m, 0.45 m around |
| The hero | 0.86, 1.3, 0.4, 1 | 0.46 m to 1.7 m, 0.4 m around |

**What was built.** `game_targets::sizeOf` reads that body from a bot's
mover manager (the scan's marker component). It accepts it only behind the
`MoverBodySize` vtable, with a radius of 0.02 to 10 m and a span of 0.05 to
20 m, and keeps a street thug's size otherwise. Each scan stores it in the
candidate (`Candidate::size`). `Size::height` carries a height tuned on a
street thug (feet at 0) over to the bot: it maps his capsule's ends, 0.4 and
1.6 m, onto the bot's. `Size::width` scales a width by the radius against
0.45 m. A street thug's own measures come back unchanged. They now set:
- the web balls' aim point (1.15 m) and the aim assist's width (0.45 m);
- the punch target, an upright capsule from his feet to 1.8 m, 0.3 m around,
  and the distance worth testing grows with his height;
- the grab's centre (0.95 m), its radius (0.45 m) and the aim markers'
  target;
- the strikes' capsule, 1.8 m by 0.35 m, for the enemy a flying enemy or a
  thrown prop strikes.

Mass is unchanged.

**Checks.** 207 core checks, 1 new: a fake mover manager laid out like the
game's reads 0.4 to 1.6 m and 0.45 m. A street thug's 0, 0.95, 1.15 and
1.8 m and his 0.3 m come back unchanged; a scale of 2 reaches 2.75 m; a
drone's (−0.3 to 0.3 m, 0.2 m) and a heavy's (0.6 to 3 m, 0.9 m) scale as
mapped. A zero radius or another class at +0xdb8 reads nothing and leaves the
size as it was. The rebuilt ray module was not run in the game. For street
thugs it gives the same numbers as the build measured there (see "Every
enemy in the target scan" below).

**Not verified.** No enemy of another size was near the save: how well a
drone's, a flyer's or a heavy's capsule fits his model is unseen.

## The launcher updates itself — preceding build

The user, October 9: "lets add autoupdater for the launcher".

**What was built.** `include/spidy/launcher_update.hpp`: a small JSON reader
(`parseJson`), `parseRelease` (GitHub's `/releases/latest` answer: tag
`vX.Y.Z`, the asset `Spidy-<version>-win64.zip` over https with its size, and
the asset's `digest`, else the SHA-256 line in the notes; the notes' "- "
lines as changes), `releaseVersion`, `newerVersion` and `updatePlan` (replace,
add, retire). `apps/launcher/update.cpp`: `Updater` (start/check/install/
cancel on a worker; WinHTTP; download into `.spidy-update\`, size and SHA-256
checked, `tar.exe` unpacks it; renames with a journal and rollback),
`installFolder`, `restartLauncher`, and the refusal when a process runs from
the folder or the game has its modules loaded. `ui.cpp`: the Play tab's
update banner (offer, progress, failure, "updated"), About's UPDATES card,
`update_check=` in launcher.ini. `main.cpp`: `--wait-for <pid>`,
`--updated-from <version>`, and the new launcher started after the old one's
window and lock are gone. `system.cpp` shares `capture` and `sha256`. CMake:
`update.cpp`, `winhttp`.

**Checks.** 21 launcher checks, 5 new:
- JSON: nesting, escapes with a surrogate pair, the scalars, nine malformed
  texts, and nesting past 64 levels refused.
- GitHub's real v0.2.6 answer, cut short: version, zip, address, size,
  digest, page and changes.
- The notes' SHA-256 (upper case) when the digest is null. Refused: no SHA-256,
  another asset only, an http address, a tag without a version, and not JSON.
- Versions: good and bad tags, newer field by field.
- `updatePlan`: case-insensitive matches, stale files retired only in the
  package's folders.

The scratch build (subst, `-DSPIDY_BUILD_XR=OFF`) is clean at /W4.
End to end, in a copy of the 0.2.5 release with this launcher (v0.2.5, from
`CMakeLists.txt`), against the real v0.2.6 release; clicks were posted to the
window, and `launcher.ini` was backed up and restored:
- The banner offered 0.2.6 and About's card agreed. *Update now* showed the
  progress and installed it. The 0.2.6 launcher took over. 68 files were
  hash-identical to the 0.2.6 zip, `reports\` and `my notes.txt` stayed,
  `tools\stray_tool.py` was in `.spidy-update\previous\`, and `.spidy-update` was
  hidden.
- A first run that started the new launcher before the old one released its
  lock lost the window: 0.2.6 ignores `--wait-for` and saw the lock. The
  restart moved to the end of `wWinMain`, and the second run handed over.
- With `python\python.exe` from the folder running, the banner said "Close
  python.exe first: it still uses Spidy's files."
- With `tools\xr_runtime.py` held open by another Python, the swap failed at
  that file and undid every rename: all 69 files were hash-identical to
  before, the running exe included ("Nothing was changed"). *Try again* after
  the lock was released installed 0.2.6.
- `--updated-from 0.2.4 --wait-for <a 4 s sleeper>` opened the window after
  4.3 s and deleted `.spidy-update\`. Once *Later* hid the 0.2.6 offer, it
  showed "Spidy is updated: 0.2.4 to 0.2.5".

**Not verified.** *Cancel* mid-download, a SHA-256 mismatch, a proxy or no
network, and an update into a release that has the updater (none is
published yet, so the first real one is the release after this build).

## WEB BUTTON: webs on the grip or the trigger — preceding build

The user, October 9: "lets add an option to switch between grip and trigger
action mapping".

**What was built.** `vr_settings::Values::triggerWebs` (off by default) and a
WEB BUTTON row, the first under WEBS in the SPIDY VR tab: a switch named by
its two choices, GRIP and TRIGGER, as CALIBRATE BODY is (15 rows, setting
numbers 0x200-0x20e). `trackedSwingInput(frame, rig, triggerWebs)` gives the
swing each controller's trigger as its web button (`HandInput::grip`) and its
grip as its reel (`HandInput::trigger`). `GameTrackingRig::triggerWebs` holds
the setting (`reset()` keeps it), and a change sets `releaseWebs` on the next
frame of play, so each hand needs a fresh press. Everything that reads the
swing's input follows: the swing (shoot, hold, reel, zip), the web grab
(catch, reel, yank, throw), the web shooter (a free hand's reel button) and
the aim markers (the ring's squeeze). What reads the controllers directly
keeps their own buttons: the fists (the grip closes the hand), the T-pose
calibration (both triggers) and the virtual Xbox controller on the game screen
(grips as bumpers, triggers as triggers). The lab (`spidy_xr_lab`) keeps the
grip.

**Protocol.** XrConfig version 17, layout unchanged: options bit 12 (4096)
starts a session with the trigger webbing, and `SpidyXrStart` takes options up
to 8191. XrData version 17, layout unchanged: settings bit 64. The menu
probe's flags bit 256 (`ProbeSettings` and `ProbeSample` stay version 3).
run_game_vr.py: `--trigger-webs`; `trigger_webs` in `vr_settings` and in the
"VR settings from the headset" line. Launcher: `SessionOptions::triggerWebs`,
a "Web button" choice (Grip, Trigger) in OPTIONS before "Webs hold in open
air", `trigger_webs=` in launcher.ini and from the headset line.
launch-game-vr.ps1 `-TriggerWebs`; its console lines name the buttons as set.

**Checks.** 206 core checks, 1 new: `trackedSwingInput` swaps each hand's two
buttons both ways and clamps them. In `GameTrackingRig`, the default mapping
holds in play. A change in play lets go of both webs for exactly one frame,
then the trigger webs. The same button set again lets go of nothing. A change
made with no gameplay (a menu) lets go once play is back. `reset()` keeps the
button. The tab test finds WEB BUTTON at row 1 with GRIP and TRIGGER, headings
at rows 0, 6, 8 and 13, and FLIPS last. Its choices: GRIP by default,
TRIGGER, nothing past them, and RESET ALL puts GRIP back. 16 launcher checks
(the `--trigger-webs` argument, `trigger_webs=1` from the headset line), 88
Python checks (data version 17; settings bit 64 decodes as `trigger_webs`
alone; the start values and the launcher's line carry it), and the GPU test
(debug layer clean).

**Not verified.** Nothing ran in the game. `tools/probe_menu.py` now sets WEB
BUTTON to TRIGGER first and expects 18 changes, but has not run. Swinging
with the trigger needs the headset: the XR worker runs only in an OpenXR
session.

## Every enemy in the target scan — preceding build

The user, October 9, passing on a player's report: "Flying Sable agents
ignore punches, and webs pass through them. in general those should be
working on all enemies." Asked whether that meant a save per enemy type: "cant
we do something more systemic?"

**Cause.** `game_targets` took an actor for a bot by one component class,
`BotMoverManagerGame` (vtable `0x38533e0`), compared exactly. The
executable's RTTI has five classes derived from `BotMoverManager`:
`BotMoverManagerGame` and, derived from it, `HammerheadBotMoverManager`
(`0x3853558`) and `MechaHammerheadMoverManager` (`0x385c618`);
`HoverMoverManager` (`0x384ce18`, beside the `BotStateHover*` states,
`HoverBehaviorMap` and `BehaviorRangedCombatHover`) and, derived from it,
`DocOckMoverManager` (`0x384cfa0`). The fists (`game_punch`), the web balls'
aim assist and webbing (`game_shooter`) and the web grab (`game_grab`) all
act only on that list, so a bot any other class moved was invisible to them.
The game's `Team` component (red and blue teams) was looked at for a hostility
test instead of classes: no actor in the running game had one.

**What was built.** `game_targets` classifies every component class it
scans from the game's RTTI: the complete object locator before the vtable
(signature 1, offset 0, its own image offset), the class hierarchy it names,
and each base class's type descriptor name. A rule names a class, and that
class and everything derived from it marks a kind or gives a trait:
`ThrowableHelper`, `BotMoverManager` and `PedestrianMover` mark the kinds;
`HoverMoverManager` gives the new `hover` trait; `ThugBot`, `CivilianBot`,
`AllyBot`, `MissionFollowBot`, `StatusEffectTrackerWebbed` and
`BreakableSystemComponent` give the traits as before; `BirdBot`,
`Helicopter` and `SilverSableCraftBot` give the new `neutral` trait, which
`enemy()` excludes like civilians and allies (the fists skip neutral bots
too). Results are cached per vtable and per type descriptor under one lock,
which the three watches' scans share. Only vtables inside the game image are
classified. `PhysicsComponent` and `SyncStaticStateMachine` stay exact, as
`native_bodies` needs them. `live()` compares the vtable the scan recorded.
`botMover()` holds for every bot: the mover's handle at +0xdb4 is
`MoverManager`'s, read by its methods `0x1fb9c60`, `0x1fb9cb0`, `0x1fb9d60`,
`0x1fb9dd0` and `0x1fba3d0`. No protocol change.

**Checks.** 205 core checks, 1 new: the test program's own stand-in classes,
named like the game's, through `game_targets::classOf` (MSVC lays out their
RTTI as it did the game's). `DocOckMoverManager` (two levels below
`HoverMoverManager`) is a bot that hovers, `BotMoverManagerGame` a bot that
doesn't, `SilverSable` (from `ThugBot`) a thug and no bot, `CivilianBot`,
`BirdBot`, `ThrowableHelper` and `StatusEffectTrackerWebbed` exactly their
own bits, an unrelated class and plain data nothing.

In the game, headless, on the user's save (a Fisk hideout on a construction
site: 12 `ThugBot`s on `BotMoverManagerGame`, no hover bot loaded anywhere),
with `tools/probe_combat.py` (new):

| Check | Result |
|---|---|
| `--list`, the same rules read from outside | 12 bots, all enemies, none flying; no actor with a `Bot` component lacked a bot mover manager |
| The web grab's own scan inside the game (props and bots) | 74 candidates, as the outside scan counts |
| `shoot`: 3 web balls at a thug | 3 fired, 3 collisions, 3 webbing blows, all on him; webbing 30 of 30, `BotStateWebStruggle`; health 50 to 48.5 |
| `punch`: a scripted fist through a thug at 6 m/s (swing input) | 1 punch on him at 5.36 m/s, 1 request issued: damage 27.9, `BotStateHitReactGame`, health 50 to 19.34 |
| `pull`: grip, yank, carry, throw | caught him; `BotStateFlung`, then `BotStateWallFlop`; 1 launch, 588 steered steps, 1 impact blow; moved 28.6 m; health 50 to 25.9 |
| Stops and hooks | every module stopped with 0; every hook entry restored |

The hideout's snipers shot the hero down during the run (health 0 in the
last capture). The game saved `slot0-s.save` once itself (`[Save] Request
save type 0` at 11:24:38). Every other save file stayed byte-identical; the
backup is `reports/backups/saves-before-combat-probe-20261009-110805`.

**Not verified.** No flying enemy was loaded near the save (a player met
them). Which enemies `HoverMoverManager` flies comes from the class names,
not from a sighting. A flyer's reaction to a fist and to webbing is the
game's damage, as for any bot. To a pull: the kinetic knock asks for
`BotStateFlung`; if a flyer doesn't take it within 0.25 s, its
`MoverStandard` is steered as for bots the game won't fling, and once let
go it is brought down under Spidy's rays.

## Slow motion — preceding build

The user, October 8: "lets add a blade and sorcery like slow motion button
with nice smooth transition in and out, effect when its active and a "mana"
and recovery system (for the slowmo)".

**What was built.** `slow_motion.hpp/.cpp` (spidy_core): `SlowMotion` (focus
drains 1/7 per real second while on, ends it at 0; refills 1/12 per second
from 1.2 s after it ends; a press needs 15%; the ease is a smootherstep over
0.4 s in and 0.55 s out, mapped to time as `scale^blend`, so 0.3 at full) and
`StickPress` (the left stick's click counts after 0.12 s held alone or at an
earlier release, never as half of clicking both sticks). `game_time.cpp`
(stereo module) hooks the game's `TimeScaleSystem` update and applies the
smaller of the game's own scale and Spidy's to the system, the clock's
double and Havok's step after each update, putting the game's own values
back before the next. `native_bodies` keeps web-held props in the world's
time. The XR worker steps it each headset frame (or every 0.2 s without
frames), pulses the controllers at a change, recolours the eye images
(`D3D12Renderer::Grade`, a copy of each image and a full-screen pass, only
while visible) and draws the meter (`appendSlowMotionMeter`).

**Checks.** 204 core checks, 5 new: the ease's shape and lengths (no frame
changes time by a seventh of the way; halfway along the curve time runs at
the square root of 0.3), focus spent and refilled to the hundredth of a
second, refusals, interruptions and frame-time limits, the view's ring and
meter timing, and the stick press against chords, late chords and a click
held through a menu. The GPU test draws an orange wall into both eyes (typed
sRGB and typeless): at rest the grade leaves them byte-identical; full, the
middle's red-blue gap shrinks by over a quarter and blue rises 20+ levels,
the corner is 8%+ darker than the middle; the ring at a fifth of its way
lifts green and blue on its band and leaves the middle within 2 levels; the
meter shows 2018 cyan and the hourglass's pale pixels; the debug layer stays
clean (it caught a copy remade mid-list for the typeless eye, fixed by one
typeless copy for both). 16 launcher checks and the 5 Python suites pass
(new: `slow_motion_snapshot`, torn reads, the hook in `GAME_HOOKS`).

**In the game** (headless, two launches with the user's OK; the first run's
flight stalled when VS Code came to the front, and the world-ray module
starts once per process): `tools/probe_slow_motion.py` after
`probe_menu_pad.py start` and `pad a --until-player` (player at 33-37 s,
perched on a lamp post by the waterfront). Three clock runs: the clock's
scale 1.0, 0.30, 1.0; a frame's game time per real time 1.043, 0.311, 1.035
(a floor near 1/240 s at the small window's 250 frames a second; 0.298 of
normal when slowed); Havok's step 1/30 s, 0.010 s, 1/30 s; the game's physics
flag set. A released swing flight (jump, web up, reel, let go; default 60%
weight, the web grab offered): normal 1.022 game seconds per real second and
gravity 5.888 m/s² on game time; at 30% 0.301 and 5.891; back 1.016 and
5.892 (5.886 asked; no step undriven, airborne throughout). The web grab's
step time 0.00402 s, then 0.00129 s; physics time per second of the world's
8.29, then 7.75 (unchanged up to the frame rate). After stop the clock 1.0,
the step 1/30 s, every stop code 0, the hooked entries restored. The save
folder stayed byte-identical; the game closed by WM_CLOSE in 1.5-2.6 s.

**Unproven.** The headset: the look of the grade and the ring, the meter's
place and size on the wrist, the haptics, whether 30%, 7 s and 12 s feel
right. The game's sound: Spidy does not touch it, and nobody listened to
whether it slows. Thugs and traffic slowing was not filmed (the clock that
drives them was measured). A web-held prop in slow motion was not flown.

## Why a game version is not supported — preceding build

The user, October 8, passing on a player's screenshot: "users says he's on the
latest version in steam yet it his spiderman version is not recognized".

**Facts.** Steam's app info (api.steamcmd.net, October 8): the `public`
branch is build 23986256 (built June 30, published July 8, 2026), the build
and depot manifests installed here, whose `Spider-Man.exe` hashes to
`EXPECTED_SHA256` (checked again today) and reads version 4.630.0.0. The other
branches are `previous_version` (build 12423814, October 2023) and
`previous_version2` (build 10131361, v1.1212.0.0). So a player whose copy
fails the hash has an older or changed file, not a newer one, and the old
message ("a game update needs a Spidy update") misled them.

**What changed.** `launcher_text.hpp`: `vdfTokens` (the tokenizer
`vdfValues` had inline), `vdfBlock`, `SteamApp`/`steamApp` (UserConfig's
`BetaKey`; StateFlags bit 2 or `TargetBuildID` other than `buildid` for an
update waiting) and `unsupportedGame`. The scan reads `EXPECTED_VERSION` from
`tools/inspect_game.py`, the game file's version resource, and the app
manifest two folders above it when its `installdir` is the game's folder
(looked up before and after the true path, which follows junctions). The
game row shows `unsupportedGame` instead of the fixed text.

**Checks.** 16 launcher checks (new: blocks, nested and with a quoted brace;
the app manifest on a beta, leaving one, up to date, downloading; each
message). The launcher's `Scanner` (system.cpp compiled into a harness beside
a `tools` copy; built in a scratch folder, not `build\windows-ninja`) on: the
real file, supported; a copy reading 1.1212.0.0 with `previous_version2` in
UserConfig and MountedConfig, the beta message; a copy reading 3.618.0.0 with
StateFlags 6, the update message; a copy with one header padding byte changed,
the changed-file message with Verify integrity; that copy outside a Steam
library, the choose-Steam's-copy message.

**Unproven.** The row on screen (the wrapped text's height); the player's
actual case (their version and manifest are unknown here).

## Flips as an experimental setting — preceding build

The user, October 8: "ok lets put as an experemental option in settings (off
by default)".

**What changed.** `vr_settings::Values::flips` (false), `Item::flips` (the
last item), a 14-row tab: EXPERIMENTAL heading and FLIPS switch at rows
12-13 (setting numbers 0x20c-0x20d). `GameTrackingRig::flips(bool)` (off by
default, kept by `reset()`): off, the flip is reset and the tilt level every
frame. XR worker: `values.flips` from XrConfig options bit 11 (2048), applied
with the other settings, reported as XrData settings bit 32; XrConfig and
XrData version 16 (layouts unchanged; `SpidyXrStart` now takes options up to
4095). Menu probe flags bit 128. Launcher: `SessionOptions::flips`, `--flips`,
`flips=` in launcher.ini and from the headset line, an options row "Flips
(experimental)". run_game_vr.py: `--flips`, `vr_settings.flips`, config and
data version 16. launch-game-vr.ps1 `-Flips`.

**Checks.** 199 core checks (new: off by default A in the air turns nothing
and the stick still walks; on, it turns; switched off mid-flip, level at once;
a reset keeps the switch. The tab test: four sections, FLIPS a switch under
EXPERIMENTAL, the last row; the choices test: OFF by default), 13 launcher
checks (the switch on adds `--flips`; `flips=1` from the headset), the 5
Python suites (87: data version 16, `flips` in vr_settings and in the
settings line), the export checks and the graphics test.

**Unproven.** `probe_menu.py`'s new FLIPS step in the game; the launcher's new
row on screen; the headset.

## Flips held with the stick — preceding build

The user, October 8, in the headset: "right now holding a just start
continiously spinning super fast in the direction".

**The session.** `dist/Spidy-0.2.4/reports/game-vr-20261008-171227.json`
(17:12-17:19, the eleventh build: its ray and stereo DLLs hash as built at
16:59). 6644 swing samples, none with an error: the version 3 swing
commands worked in the game. The head's up in the report's samples (about
20 a second) went below the horizon in 0.41-0.55 s stretches 0.7 s apart:
held flips turning nonstop at the build's 0.7 s a turn, e.g. 8 turns at
316-324 s, 4 at 328-332 s.

**What changed.** `FlipMotion` (game_tracking): phases level, holding,
flipping, settling. A press that starts in the air holds: the left stick's
tilt (dead zone 0.2, full from 0.9, as smooth turning) turns the player
about the stick's axis (the head goes the way it points and down) at up to
2π/1.5 s, reaching 63% of what it asks in 80 ms; at rest the angle stays.
Let go after 0.25 s or more: back level the short way at 2π a second (the
last 0.4 rad slowing to no less than 30%). Let go sooner (a tap): a whole
flip the way the stick pointed during the press, else the way it was
turning, else ahead, at 2π a second after an 80 ms spin-up (1.16 s from
rest); pressed during that flip, A holds it where it is, still turning, and a
tap carries it on to level. Landing (80 ms) levels at twice the return
speed. The web-held input is gone (rig and XR worker). Same command
layouts as the eleventh build; only `spidy_stereo_probe.dll` changes in
code.

**Checks.** 198 core checks: five flip checks rewritten (a tap's front flip
in 95-115 frames about a still head, bit for bit level after; a ground
press held into the air turns nothing and keeps the stick walking; held, a
whole turn in 1.5 s at full tilt and half as fast half tilted past the dead
zone, the angle kept at rest within 1e-5, letting go no more than half a
turn back; a tap with the stick back a whole backflip; a tap during a flip
one whole turn; the stick's four ways held 31 frames, walking zeroed, level
within 30 frames of letting go; landing upside down level within 40
frames; a menu; a held A turns no more). The graphics test, the 5 Python
suites (87) and the export checks (v2/v3 swing, v1/v2 body) pass. Built
17:30 in build\windows-ninja (spidy_stereo_probe.dll 46F39C7CA2A9...,
spidy_ray_bridge.dll 40E84B29556E...: its code is unchanged, its debug
directory changed with the header).

**Unproven.** The headset: the top speed, the return to level, taps.

## Flips — preceding build

The user, October 8, after the Ultimate Spider-Lair's VRChat controls (flip
modes, both triggers held to flip, the sticks turning the player): "wdyt
about adding something like the flip mode from here without hurting existing
mapping?", then "build it, only the flip rotation stuff".

**Mapping.** Only inputs idle in the air. A pressed in the air (kept from
the game since the ninth build; Spidy's swing acts on A only on the ground)
starts a flip. The left stick picks its way at the press and steers it while
A is held; only then are the swing's air steering and the game's stick
zeroed. The Spider-Lair's both-triggers and both-grips holds collide with
reeling, web balls and webs, so they were not used.

**What changed.**
- `FlipMotion` (game_tracking): level, spinning, finishing, settling. A
  press that starts airborne (`game_swing::airborne` from the latest swing
  sample, the AirJumpFilter's rule) starts it. Held, it turns at 2π/0.7 s
  about an axis from the stick (the head's left, a front flip, by default),
  reaching 63% of that in 50 ms; the stick past 0.3 steers the axis. Let go,
  or a web holding the player or a catch, and it finishes the turn toward
  level the way it turns, slowing over the last 0.4 rad to no less than 30%;
  let go within 0.6 rad past level after half a turn or more and it goes
  back. 80 ms on the ground, a wall or a ceiling and it settles the short
  way at twice the speed. A tap measures 72 frames (0.8 s) for the whole
  turn at 90 Hz; a landing half way levels in 24 frames (0.27 s). Inactive
  play (menus, scenes, stutters), a recenter and another player level it at
  once; a held A must be let go before it flips again.
- `Rig` gains `tilt` and `pivot` (the tracking-space head where the flip
  began): `toWorld` turns tracking-space poses about the pivot, then yaws and
  places them. With the identity tilt it takes the old path, bit for bit.
- `Input::tilt` and `trackingTurn(in)` (yaw times tilt): the zip pull
  (swing.cpp), the grab's yank (web_grab.cpp), the punch's arm motion and
  its turn compensation (`Punches::turn(Quat)`, game_punch.cpp) and the aim
  markers' steadying (`AimMarkerMotion::aim(direction, Quat)`) use the whole
  orientation; level, it is the yaw exactly.
- `game_swing::Command` version 3, 184 B: the tilt after version 2's 168
  bytes. `valid` and `SpidySwingSubmit` take version 2 too (level), reading
  as many bytes as the command names.
- `native_body::Command` version 2, 160 B: the tilt in world axes after
  version 1's 144 bytes; `SpidyBodySubmit` takes version 1 too. body_ik
  solves the body for the player turned back level about the eyes, then
  turns everything under the body's root with the tilt (step 7, before the
  head shrinks); tilted more than 0.09 rad, the legs keep the game's pose.
- game_xr: the rig gets `game_swing::airborne` and whether a web or a catch
  holds; A is masked while the T-pose calibration has the controllers; the
  swing and body commands carry the tilt; the aim markers steady with
  `trackingTurn`. XrConfig and XrData are unchanged, so the session report
  has no flip counter.

**Checks.** 198 core checks (11 new: a tap's front flip about a still head
in 60-90 frames, ending bit for bit level; a ground press held into the air;
hold and release, just past level, a web holding; the stick's four ways and
the stick taken from walking while held; landing, a menu, a held A; the
rig's tilt about its pivot and a snap turn while tilted; an upside-down pull
zipping and push not; a flip that is no punch; aim markers steady through a
flip; swing commands v2 and v3; the body turned about the eyes with wrists
on their controllers), the graphics test, 13 launcher checks and the 5
Python suites (87). Built in a separate folder (a peer's in-game check was
using build\windows-ninja). The rebuilt `spidy_ray_bridge.dll` and
`spidy_stereo_probe.dll`, loaded in Python with no game: `SpidySwingSubmit`
took a probe's v2 command and a v3 one with a tilt (2002, not running) and
refused a scaled tilt and a v2 header claiming 184 B (2001);
`SpidyBodySubmit` took v1 and v2 (0) and refused a zero tilt and a v1
header claiming 160 B (7502).

**Unproven.** Everything in the game and the headset: the XR worker's flip,
comfort and speed, whether the game draws a rolled or upside-down view
correctly (sky, shadows, particles, its active view following the head), the
body seen mid-flip, and held images while flipping (a dropped frame shows the
previous tilt, as with smooth turning).

## Web balls and pulls on thugs — preceding build

The user, October 8: "when shooting web projectiles at enemies and objects
nothing really happens same with webbing and pulling enemies - they get
pulled but dont really react to the web, collision damage etc"

**The session.** `dist/Spidy-0.2.3/reports/game-vr-20261008-135010.json`
(13:50-14:06): the shooter fired 58 shots, 30 aimed at thugs, the game took
all 30 targets (`resolved`); the grab caught bots 4 times, yanked 3, asked 3
flings (`flings` 3, `steers` 3); 8 punches landed (10-29 damage).

**Measured headless** (a research DLL hooking the shot's event handler
0xd2a570, `ShotActionDamage` 0x20b7200, the damage system's processing
0x1eb8cf0 and delivery 0x1eb9b30, `RequestState` 0x20e51c0 and the web hit
0xd4ea30, and running shots, state requests and damage requests on the main
thread). The save loaded at (-481, 7, 452); a street crime of 7 `ThugBot`s
(60 HP, webbed threshold 30) and 2 civilians was found at (-484, 1, 104) by
swinging there with the virtual pad (its scripted thugs ignore hits until a
blow starts the fight):

| What | Result |
|---|---|
| Spidy's shot at a thug | event Collision, HitActor = the target; no damage action, no request, no state |
| Its `ShotActionDamage` (from `ShotWebShooterPrius` DamageData) | damage 0, type kNone, status -1, impulse 22 |
| The game's own RB (Impact Web on the user's save) | damage action: kWebImpact, status 19 amount 10, kTwitch |
| Shot with options 3 + limb 0 (what the hero's melee web shots set) | `WebBlanket` spawned on the thug 4 of 4 times; no state change |
| kWebImpact 0.1 + status 19 (kWebEncase) 100, or 25 on top of an earlier 22 | BotStateWebStruggle |
| status 19: 3 x 4, then 10; 3 x 9 within 1 s | nothing (27 < 30; webbing decays to 0 within seconds) |
| kWebCategory + status 12 at 100 | 0.1 damage, nothing else |
| `RequestState(BotStateFlung)` on a fighting thug | returns 1; the thug keeps alternating Aim / Face |
| kMelee kFlyBack / kSuperFlyBack / kAirborne, kExplosion kFlyBack (from 15-20 m) | BotStateHitReactGame only |
| kMelee kKnockdown | HitReact, then BotStateStunned |
| kKinetic + kFlyBack + KnockbackAmount 10 + ImpactImpulse 30 | BotStateFlung (3 of 3), away from the damager at ~12 m/s, then BotStateGroundFlop, BotStateStunned |
| BotStateFlungLocal +0x94 written every frame toward the hero for 1 s | pulled 7 m toward him and up 3 m (sliding along an obstacle), then flop, stun |
| The same, holding it 2.4 m above where it stood for 4 s | held there for 4 s, then flop, stun |
| Damage taken | requested 3 -> 6, 5 -> 11 HP (the difficulty's own scaling) |
| kKinetic 30 + kFlyBack + impulse 30 on a throwable trash can | processed, never delivered; the can stays |

**What changed.**
- `game_shooter` hooks the shot's event handler (ShotWebShooter +0xd8). A
  collision of one of Spidy's own shots (its registry handle at +0x14, its
  shot id at +0x104) reads the event's HitActor, HitPosition and HitNormal
  with the game's own readers (1bcf3e0, 1f9db60, 1f7b760). A webbable thug
  (`StatusEffectTrackerWebbed`, no `CivilianBot`) takes a kWebImpact blow of
  0.5 with kTwitch and 11 of status 19 (`native_bodies::Damage` gained the
  status, added by 0x1ed20e0); a throwable that is no breakable is launched
  along the shot at 3.5 m/s plus 1 m/s up.
- `game_targets` candidates carry traits: thug, civilian, webbable,
  breakable.
- `game_grab`: the first pull on a thug deals him kKinetic 2 + kFlyBack +
  KnockbackAmount 10 + ImpactImpulse 30 (from the hero); while he is in
  BotStateFlung the web's velocity is written to the flight
  (`native_bodies::fling(..., request = false)`), never his mover; thrown or
  let go, his flight gets the velocity once and the game lands him. Not
  flung 0.25 s after the blow: his mover as before. Came down on the web: he
  lies until 1.5 s after the last knock. Strikes (web_grab `StrikeConfig`):
  a flight's pace over two steps that drops under 45% of the fastest of the
  three steps before, from 7 m/s up, while the web asked no such stop, or a
  landing at 8 m/s or faster: a kinetic blow by the speed lost (2 + 0.7 per
  m/s over the threshold, up to 15; kKnockdown from 14 m/s); a flying thug
  or thrown prop touching another thug (a 1.8 m capsule, 0.35 m around) at
  6 m/s or faster: 2 + 0.7 per m/s over 6, kKnockdown, or from 9 m/s kFlyBack
  with an impulse (the one hit is knocked away from the flying thug);
  0.8 s between blows to one bot.
- Telemetry: SpidyShooterData v2 (192 bytes: collisions, webbed, pushed,
  last_hit), SpidyGrabData v3 (368 bytes: launches, flown, impacts, struck);
  the session report samples them.

**Verified with Spidy's modules** (second game, no thugs came near: the
save loaded at (-486, 5, 233), the crime was gone, and swinging 450 m west
met none): SpidyShooterTest at a trash can: 1 collision, `last_hit` its
record, not pushed (breakable); at a dormant thug 300 m away (6 HP, no
state): 3 collisions, 3 webbing blows queued, `last_hit` his record (a
probe's test shot carries no damager, so the game drops those; trigger shots
take the player from the input sample). Every hook entry restored on stop.
All 147 throwables near the save were breakables.

**Checks.** 185 core checks (2 new: impact loss and blows; what a flying
body touches and the strike's blow), the launcher's, and the 5 Python suites
(87; the shooter and grab layouts updated).

**Unproven.** Spidy's own pull path on a thug in the game: the knock, the
steered flight, the strikes and their thresholds (false or missed impacts);
the webbing through a trigger shot in play; the headset feel.

## A pressed in the air is no web zip — preceding build

The user, October 8: "lets disable the regular og game webshooting mechanic
that happens when pressing a in the air or mid jump"

**Cause.** The controllers' A (`XrFrame::jump`) reached the game as the
virtual Xbox controller's A and the input bridge's Space whenever VR
gameplay steered, in the air too: `swingNativeKeys` passed the jump during
Spidy's owned flight ("manual jump during a web-controlled landing"). In
the air the game's A is its web zip (`HeroStateWebZip`).

**What changed.**
- `AirJumpFilter` (include/spidy/swing_takeoff.hpp) decides at the press:
  a jump key pressed while airborne is withheld until released; one pressed
  on support passes for as long as it is held.
- `game_swing::airborne(Data, nowQpc, frequency)` (include/spidy/game_swing.hpp):
  status 1 or 2, a step within 100 ms (a perch or ledge the game animates
  without steps leaves an old airborne sample), and owned flight or the
  mover not grounded with collision flags bit `0x10`.
- The XR worker (src/game_xr.cpp) filters `motion.nativeKeys` before
  `swingNativeKeys`, so the pad's A and the bridge's Space both follow it.
  Spidy's own jump input (`motion.swing.jump`: takeoff, point launch, both
  on support only) and the takeoff's own jump press are unchanged. No
  protocol change.

**Why that rule.** Contact 2 alone is not the air: the October 8 11:54
session began on a perch at contact 2 (`0x80a0` / `0x2060001`, standing
still), and a wall-run-like stretch ran at contact 2 with `0x2060000`.
Swing samples of the 11 October 6-8 session reports, each matched to the
movement sample of the step it observed (`source_step`), against the game's
air-state handler (0xa7b3a0, `air_events`) in that step: rule air and
handler ran 37,612; neither 23,168; air without the handler 734 (a jump's
first steps at `0x80a0` / `0x2060010`, and owned flight along walls);
handler without air 143 (landing steps at contact 0): 98.6% agree. Perch
(`0x2060001`, 1,434) and wall-crawl steps (`0x2060005`, and `0x8001` /
`0x2060000`, 313) never counted as air, nor ran the handler.

**Checks.** 183 core checks (2 new: the filter's press decisions, a landing
during a withheld press, other keys kept; `airborne` on owned flight, the
game's air state, standing, landing, perch, wall crawl, wall run, an old
sample, a stopped or faulted swing), the 5 Python protocol suites. Not run
in the game: a peer's research session had it open, and the XR worker
needs the headset.

## Steadier aim markers, a colour per hand, a new target ring — preceding build

The user, October 8: "lets make the aim markers nicer (diffirent color shade
per hand maybe) and they feel very jerky\jittery rn can we smooth them a bit
while staying snappy. also the square target marker is ugly as heck"

**Why they jittered.** The marker sat on the raw aim ray of the image's
input sample. A held hand's tremor (about 8-12 Hz) and the controller's
tracking noise swing that ray by about 0.4 degrees peak to peak (a typical
tremor, simulated: 0.1 + 0.05 degree sines at 9.5 and 7.1 Hz plus 0.03
degree noise per frame at 72 Hz), as wide as the ring at 20-25 pixels a
degree, at any distance. The shapes were aliased (no multisampling, opaque),
so their edges crawled by whole pixels as they moved by fractions of one; a
change in the preview's kind swapped the shape at once (at an edge, every
few frames); and a target's centre moved in the game's steps.

**What changed.** `AimMarkerMotion` (src/web_visual.cpp, spidy_core), one per
hand in the XR worker's overlay callback (src/game_xr.cpp):
- `begin(t)` per eye image, `t` its predicted display time: the same image
  again changes nothing; a gap over 0.25 s starts afresh.
- `aim(direction, yaw)` turns the aim direction into the tracking space
  (`Quat::yaw(-trackingYaw)`, so snap and smooth turns are no motion) and
  filters it: a 1 euro filter, cutoff 1 Hz + 60 x max(speed - 0.06 rad/s, 0),
  the speed taken from the step off the last filtered direction and
  low-passed at 4 Hz. Simulated (the tremor above, 5 seeds): at rest 0.40 to
  0.10 degree peak to peak; at a constant 2.3-8.6 deg/s, 0.06-0.10 degree
  behind; sweeps at 0.3, 1 and 3 rad/s at most 0.28, 0.31 and 0.35 behind;
  within 0.05 degree 19-31 ms after stopping. (Without the dead band rest
  kept about twice the wobble; textbook settings, 10 per rad/s and a 1 Hz
  speed low-pass, trailed sweeps by up to 1-1.5 degrees.)
- `markers(wanted, origin, direction, out)`: per kind a weight (new: 0.35 at
  once, +1 per 40 ms; unwanted: -1 per 80 ms), for anchor, air and blocked a
  distance along the steadied ray (eased in log space, 30 ms), for a target
  its centre and radius (eased, 35 ms) and a lock (0 to 1 in 120 ms; the ring
  starts 1.8 times wider, and widens again as it fades). Each kind still
  fading is drawn too, at its weight's opacity, where it was last wanted (a
  fired web's ring fades where the web went, not along the moving hand).
- The placement rules (onAimLine for surfaces, the preview's reach for air,
  the 2 / 3 / 0.5 m hide distances) are unchanged, on the steadied ray. Off
  (X), untracked, or with the hand's web attached, the marker fades out; the
  flat screen resets it.

`appendAimMarker` draws soft-edged shapes (`softArc`, `softBar`,
`softTriangle`: edges fade over 0.8 px), every shadow first (60% dark, 1.3 px
past the shape outside and 0.5 px inside, fading over 1.8 px; a dot's half
that), then the colours. Linear RGB: left (0.13, 0.55, 1), right (1, 0.40,
0.05), miss (0.9, 0.07, 0.05), centre dots 60% toward white. At 100%: anchor
ring 10.5 px radius, 2.3 px wide, dot 2 px (the ring shrinks 40% as the grip
closes); air 8.5 px, eight dashes of 45%, 75% opaque; the cross's arms 5.5
px; the target ring max(1.15 x radius, 13 px), the right hand's 15% wider and
turned 60 degrees, three 75-degree arcs with claws 20% of the radius (4.5-10
px) and a dot, turning 0.8 rad/s plus 2.4 x grip.

Renderer: `Vertex::alpha` (default 1, so every `{position, color}` stays
opaque); the overlay blends (source alpha / inverse source alpha; at 1 the
pixels are as before); `renderViews(..., translucent)` draws a second span
after the first with a pipeline that tests depth but never writes it, so
soft edges and shadows never cut holes in each other. The worker sorts the
markers farthest first.

**Checks.** `spidy_tests`: 181/181. New: "aim marker motion steadies hand
tremor, keeps up with a sweep and ignores turns" (a 0.15-degree 9 Hz tremor
to under half; a 2 rad/s sweep under 0.6 degree behind; a 30-degree snap
turn no motion; the same image no change; a gap restarts; a bad direction
passes) and "... fades markers in and out and eases them to new places".
Reworked: "aim markers keep their size on screen and stand out on any
background" (shadows painted before colours, a quarter of the vertices at
opacity 0, each hand's colour, opacity, the target ring round (its outer edge
in at least 18 of 36 directions), closing in, both hands' rings apart).
`spidy_graphics_test` 1536x1536: both hands' anchors, open air, a miss and two
rings on one target over the dark scene, a bright sky and a lit wall drawn
169 m out: each marker's colour, and on the panels pixels darkened at least
15% by its shadow; D3D12 debug layer clean. ctest (core, launcher) and the
five Python suites (87) pass. The saved `aim-markers.bmp` was looked at.

**Not checked.** The headset: how the steadying feels (the tremor is a
model, not the user's controllers), the colours on the game's own scenes,
the ring's turning. The game was not run: the markers are drawn only in the
XR worker's eye images, through the same renderer the GPU test runs.

## Body calibration in a T-pose — preceding build

The user, October 8: "lets add an in game calibration section on first start
(and as an option in the vr menu) where we ask the players to stand in a t
pose and hold triggers to get correct measurements to scale the avatar."

**What changed.** `src/body_calibration.cpp` (spidy_core, engine
independent): `Calibration` takes the headset and controllers in the
tracking space each frame and, after 1.5 s of a held T-pose (both
controllers tracked, eyes at least 1 m up, each arm within 41 degrees of
straight out to its side from the hero's shoulder at the player's size and at
least 72% of his arm, the two within 12%, the head within 26 degrees of level
and 34 of square to the arms, both triggers past 0.6 then 0.35, head under 0.3
and wrists under 0.35 m/s; a lapse under 0.25 s pauses), measures the eye
height (mean head height) and each arm's reach from that shoulder to the wrist
the solver places for the grip; the arm length is the longer. `Proportions`
(the hero's rest pose: eyes 1.697 m up, shoulder joints 0.111 m behind, 0.266
below and 0.170 to the side of them, arms 0.559 m) come from
`body::Rig::shoulders` (new, from `prepare`) through
`native_body::proportions`, with Spider-Man's numbers until the body has run.
`bodyScale` (0.7-1.3 calibrated) and `armScale` (0.8-1.25) size the body;
`body::solve` takes `armScale` and scales each arm's subtree about its
shoulder after the body's scale. `appendView` draws the panel (1.2 x 0.6 m,
1.4 m ahead, 12 cm under the eyes, where the player faced; it stays in the
tracking space) and a ring at each controller; `src/overlay_text.cpp` is the
stroke font. The XR worker starts it at the first 1.5 s of immersive play
without a calibration (not while Spidy's swing owns the player, and only with
the body on) or when `vr_settings::Values::calibrate` is set by the tab's
CALIBRATE BODY (`Item::calibrate`, a switch named NO / ON RESUME, row 6 under
a BODY heading: 12 rows, 0x200-0x20b), and clears that flag as it starts.
From its start until each is let go afterwards, the swing's triggers and grips
and the jump are zeroed and B is no interact; B skips. Done, the body command
carries the eye height (with the new `calibrated` flag for the wider range)
and the arm length (`Command::armLength`, the old spare word).

**Protocol.** XrConfig version 15, 640 bytes: `eyeHeightMm`, `armLengthMm`
(both 0, or 1000-2500 and 250-1200; else 1001) and options bit 10 (no prompt
at the first gameplay). XrData version 15, 792 bytes: from 752 the
calibration's phase, hint, progress, eye height and arm length (mm), done and
skipped counts, flags (bit 0: no prompt) and each arm's reach. The body's
status is version 2, 144 bytes (`armScale`, `calibrated`). The menu probe's
flags gain 64 (a calibration asked for). Python: `--eye-height`,
`--arm-length`, `--no-calibration-prompt`; samples' `calibration` and
`vr_settings` `eye_height_mm`, `arm_length_mm`, `calibration_prompt`, which the
settings line carries; `body.arm_scale`. Launcher: `SessionOptions`
`eyeHeightMm`, `armLengthMm`, `calibrationPrompt`; launcher.ini
`eye_height_mm`, `arm_length_mm`, `calibration_prompt`; a "Body calibration"
row with Redo / Ask again; "Reset options" keeps the measurements.
`launch-game-vr.ps1 -EyeHeight -ArmLength -NoCalibrationPrompt`.
`probe_game_body.py --phases tpose` drives the body with a T-pose and the arm
length; `probe_menu.py` steps CALIBRATE BODY (14 changes in all).

**Checks.** 179 core checks pass (new: arms 0.65 m from the shoulders
reached only with the arm scale, every bone 1.16 times longer and the hips
unmoved; the test rig's proportions, the scales and their clamps; a T-pose
measured at 134/135 frames, the eye height and 0.6 m arms exact, the longer
of two arms taken; each check's panel line, crossed and hanging arms
included; a 0.2 s lapse pausing and a 0.3 s one restarting, the trigger's
hysteresis; the panel's place and drawing; the font's glyphs for every panel
text, its widths and bars), 13 launcher checks (the calibration round trip,
half or out-of-range values refused, the skip) and 87 Python checks (XrData
v15's calibration, the body's status v2, the settings line). The GPU test
draws the panel holding at 60% and the result over the left eye
(`calibration-panel.bmp`, `calibration-done.bmp`): every line legible at 1536
pixels for 92 degrees, the rings filling clockwise from the top.

In the game without a headset (the user's save, `probe_menu_pad.py start` +
`pad a --until-player`, two fresh games, with the user's OK):

- `tools/probe_menu.py` (player at 28.5 s) passed every step: BODY and
  CALIBRATE BODY between WEIGHT and COMFORT, NO stepped to ON RESUME
  (`calibrate` true, 6 changes), RESET ALL back to NO (14 changes), every
  hook restored. The capture (`reports/menu-probe/calibrate_on_resume.png`)
  shows the row like the game's own; its help wrapped "T-pose" at the
  hyphen, so the help was reworded afterwards ("Fit Spider-Man to you: pick
  ON RESUME, then hold both triggers in a T-pose.").
- `tools/probe_game_body.py --phases rig,tpose` (player at 35 s; report
  `reports/body-probe-tpose.json`): the hero's rig gave the proportions the
  defaults hold (eyes 1.6969 m up, arms 0.5586 m). Eyes 1.66 m up as a
  calibrated height (body 0.9782), both wrists 0.62 m straight out from the
  rest pose's shoulders: with the hero's own arms they ended 8.5 and 7.1 cm
  short of their controllers (left, right); with the arm length the body's
  arms scaled 1.1346 (0.62 / (0.5586 x 0.9782)) and ended 2.7 and 0.8 cm from
  them. What is left is the game's standing pose holding the shoulders 3-7 cm
  higher than the rest pose, with the far reach lifting the clavicles
  (shoulders at 1.47 m against 1.40 estimated). The screenshot
  (`reports/body-probe/tpose-calibrated.png`) shows him in a T-pose with the
  longer arms.

Save files unchanged except `slot0-s.save` (the game's own save when the menu
probe left the pause menu). The calibration itself runs only in the XR
worker, so the panel in the headset, the pose's thresholds against real
players, and how the scaled arms feel need the headset. Not in the play
folder (the user said not yet).

## Five options removed — preceding build

The user, October 8: "lets remove web catch props, web shooter, your own body,
punch thugst, webs drawn by from options".

**What changed.** The launcher's OPTIONS card lost "Webs catch props and
thugs", "Web shooter", "Your own body", "Punch thugs" and "Webs drawn by"
(`App::segmented`, used only by the last, is gone). `SessionOptions` lost
`webGrab`, `overlayWebs`, `body`, `punch` and `webShooter`:
`sessionArguments` never passes `--no-web-grab`, `--overlay-webs`,
`--no-body`, `--no-punch` or `--no-web-shooter`; launcher.ini's `web_grab`,
`overlay_webs`, `body`, `punch` and `web_shooter` are no longer read or
written; `headsetSettings` ignores those keys on the session's settings line.
The SPIDY VR tab lost WEBS CATCH PROPS AND THUGS, WEB SHOOTER, the BODY
heading, YOUR OWN BODY and PUNCH THUGS: 10 rows, setting numbers 0x200-0x209
(WEBS: aim markers, webs hold in open air, swing speed limit, weight; COMFORT:
snap turn, smooth turn, controller vibration, game screen size).
`vr_settings::Item` lost `webGrab`, `webShooter`, `body` and `punch`; `Values`
keeps their fields, which only run_game_vr.py's switches set, so RESET ALL
leaves them as the session started.

**Protocol.** Unchanged: XrConfig and XrData version 14, the menu probe's
structs version 3; run_game_vr.py and its `--no-*` switches as before.

**Checks.** 172 core checks pass (the tab's two sections and ten rows, a
switch's round trip on WEBS HOLD IN OPEN AIR, RESET ALL leaving web grab,
punching, the body and the web shooter off when a session started them off),
12 launcher checks (the arguments without the removed flags, the headset line
ignoring their keys) and 86 Python checks. `tools/probe_menu.py` follows the
new rows (aim markers off; webs in open air off and back with X; swing speed
40 m/s; weight 80%; past the COMFORT heading to snap turn 45 degrees and
smooth turn 60 degrees a second; RESET ALL: 12 changes, the web shooter still
off).

In the game without a headset (fresh game, the user's save,
`probe_menu_pad.py start` + `pad a --until-player`, player at 31 s):
`tools/probe_menu.py` passed every step, 12 changes, every hook restored; the
captures show WEBS with its four rows, then COMFORT. Save files unchanged
except `slot0-s.save` (the game's own `[Save] Request save type 0` when the
probe left the pause menu). Deployed in place to `dist\Spidy-0.2.2`: Spidy
Launcher.exe, spidy_stereo_probe.dll, spidy_ray_bridge.dll (relinked) and
README.txt replaced, 29 files hash-verified, import check 42; previous files
in `reports\backups\play-folder-before-options-20261008-130334`.

## Weight — earlier build

The user, October 8: "the locomotion in the air feels too floaty, lets maybe
add controllable weight setting to ingame settings?"

**Cause.** Flight Spidy owns (a web attached, then released flight until the
player lands) falls under the swing solver's gravity, `game_swing::Config`
6 m/s² since October 5 (`physicsConfig`): 61% of real gravity, a fifth of the
game's own airborne gravity (the movement module's `air_gravity` reads 30.0
m/s² in `reports/weight-probe.json`). 6 was what the October 4 builds applied
in practice (configured 18, a third reaching the body through the one-step-old
steering that `InFlightStep` fixed).

**What it does.** `vr_settings::Values::weight`, percent of real gravity
(9.81 m/s², `vr_settings::gravity`), 60 by default (5.886 m/s², 2% under the
old 6). The SPIDY VR tab has a WEIGHT row under SWING SPEED LIMIT in WEBS (row
6, setting number 0x206; the tab now runs 0x200-0x20e, BODY and COMFORT one
lower) with 40, 60, 80, 100, 125, 150, 200, 250 and 300% (nine, the most a row
takes); `Item::weight` is the last item. `Swing::setGravity` changes the
solver's gravity from its next step; `SpidySwingSettings` takes it
(`game_swing::Settings` version 3, 28 bytes, `gravity` up to `maxGravity`,
30 m/s², else 2001), and the XR worker starts the swing with it and sends it
with every settings change. Coasting without input after a break falls at it
too.

**Protocol.** XrConfig version 14, still 632 bytes: the spare word after the
render scale is now `weight` (40-300; anything else is 1001). XrData version
14, 752 bytes: `weight` at 744, then a spare word; Python
`vr_settings.weight`, and the settings line for the launcher carries
`weight=`. The menu probe's `ProbeSettings`/`ProbeSample` are version 3 (40
and 64 bytes) with `weight`. Launcher: `SessionOptions::weight`, `--weight`
(left out at 60), `weight=` in launcher.ini (40-300), a "Weight" list;
`launch-game-vr.ps1 -Weight`; `run_game_vr.py --weight` (40-300).

**Checks.** 172 core checks pass, new: the swing's fall over 20 ms at the
default weight and at 150% after `setGravity` (velocity within 0.001 of
gravity × time), NaN, negative and infinite gravities refused with the old one
kept; the row under the swing speed, its nine steps, a launcher weight between
two steps (70 shows 60%, 71 shows 80%), the 40-300 range, the heaviest weight
within `maxGravity`. The launcher checks (the argument, its 300 cap, the
headset line and its 40 floor), 86 Python checks (XrData v14 with the weight,
the settings line) and the GPU test pass.

In the game without a headset (fresh game, the user's save, perched where it
loads; `probe_menu_pad.py start` + `pad a --until-player`, player at 33 s):
`tools/probe_menu.py` passed every step, WEIGHT stepping from 60% to 80% with
the left stick (change 3) and back to 60% with RESET ALL (13 changes), hooks
restored. `tools/probe_weight.py` jumped, webbed the open direction, reeled
1.1 s, let go and in that flight fitted the vertical acceleration of every
airborne step on game time (about 200 steps a second in the small window):

| Weight | Asked (m/s²) | Measured (m/s²) | Steps | Vertical speed |
|---|---|---|---|---|
| 60% | 5.886 | 5.884 | 99 in 0.505 s | 11.13 to 8.18 m/s |
| 150% | 14.715 | 14.716 | 100 in 0.501 s | 6.65 to -0.67 m/s |
| 300% | 29.430 | 29.530 | 60 in 0.301 s | -3.72 to -12.47 m/s |

Every airborne step ran Spidy's command; the player landed after the last
window; a gravity of 31 m/s² was refused (2001); the modules stopped and the
hook entries were restored. Save files byte-identical but `slot0-s.save`,
which the game saved itself at 12:26:29 (`[Save] Request save type 0`, the
probe leaving the pause menu, before the jump); backup in
`reports/backups/save-before-weight-probe-20261008-122456`.

How each weight feels, and the XR worker taking the tab's change during a
headset session, need the headset. In the next report: `vr_settings.weight`.

## Fingers in a fist — earlier build

The user, October 8: "my fingers in vr appear twisted and tangled in most
poses". The hand crops of their October 7 23:11 session
(`game-vr-20261007-231152-eyes/0015-right-hand.png`, `0017-left-hand.png`,
64-68 s, swinging with the grips held): the right fist a claw, its fingertips
bent up out of it; the left hand's fingers bent back out and crossing.

**Cause.** A hand closes into a fist by its grip (every web) or by moving
faster than 1.2 m/s against the head. The fist bent each finger joint toward
the palm about cross(bone before it, palm's normal). That axis turns over
once the bone before has curled past the normal: at a full fist the middle
bones point back at 178° from the bones in the palm, so each tip joint bent
60° backward; from a hand the game had already closed, middle joints folded
back through themselves. And `prepare` took the rest pose's palms as facing
down (the thumb overruling it only when clearly elsewhere). The hero's rest
pose holds them 30° from down, toward the thumb, measured from the knuckles:
the fingers closed diagonally across the palm, and each hand sat rolled 30°
on its controller. The game's own bends agree with the knuckles: in the
perch pose of `reports/body-probe-fist.json` it bends each finger's middle
and tip joints about axes 3-9° from the knuckles' hinge, 24-32° from the
assumed one.

**What it does.** `body::prepare` takes the palm's normal across the line
from the first finger's knuckle to the last's and along the bones in the
palm, when an arm names two fingers or more and it lies within 60° of the
old guess. Each finger joint after the palm's gets a hinge, across its
finger's bone in the palm and the palm's normal at rest, kept in the frame of
the joint before it (`Rig::fingerHinges`); the thumb's two joints get one
across its first bone and the way to the little finger (`Rig::thumbHinges`).
`body::solve` turns each joint about its hinge from the game's bend to
`fistBend` (83°, 95°, 63°), the fist's share of the way. The thumb's last
joint bends to `thumbBend` (1.1 rad), the joint before it as far as puts the
tip as far from the thumb's base as its place (at most `thumbBendMax`,
1.4 rad), and the base swings the tip there: over the middle bones of the
index and middle fingers, `thumbRest` (0.016) out of the fist. `thumbFold` is
gone. `tools/probe_game_body.py` gains `--fists LEFT,RIGHT` and `ik.hands`
(each palm against its controller's, every finger joint's bend about its
hinge, the thumb's bends, twist and tip height).

**Measured without the game**: a scratch program with the old and the new
solver on Spider-Man's own left hand (the rig's rest positions), a controller
held thumb up:

| | Before | Now |
|---|---|---|
| Palm against the controller's palm | 29.9° | 0.0° |
| Full fist, every finger's knuckle, middle and tip joint | 82°, 96°, −60° | 83°, 95°, 63° |
| Half fist from straight fingers | tips −35° on three fingers | 42-50°, 47°, 31-32° |
| Half fist from the game's own fist (101-118°, 109°, 68-69°) | ring middle joint 172°, little −175° | 92-100°, 102°, 66° |
| Joints bent backward, any start, half or full fist | 1 to 8 | 0 |
| Fingertips off their finger's own plane | up to 7.1 cm | 2.6 cm, the rig's own spread |
| Thumb | no hinge; tip at the index finger's middle joint | 59° and 63° about one hinge; tip 1.6 cm out of the fist |

**Checks.** 171 core checks (3 new, with both of Spider-Man's hands and with
turned and mirrored joint frames: each palm faces its controller's within
0.6° for three grips; every finger joint goes a quarter, half or all of the
way from the game's bend to the fist's within 0.6°, from straight fingers,
fingers bent back 15° and the game's own fist, with the fingertips tucked in
side by side; the thumb's tip within 3 mm of its place, its joints bending
the same way about one hinge, the last at `thumbBend`). The scratch program's
numbers fail the new checks for the old solver. 86 Python checks.

**Not yet seen:** the game and the headset. The user declined the game check
for now; `probe_game_body.py --phases rig,ik,eyes --fists 0.5,1` is ready for
it. In the next report read the hand crops in `<report>-eyes/`, and ask how
the hands sit on the controllers (the palm turned 30°).

## Render resolution above the headset's — earlier build

The user, October 8: "right now we can't change resolution to be above our
current headset making the game look blurry\aliased, maybe add an option to
go above it?" Two Discord reports the same day: a Quest 3 player over
Virtual Desktop whose log shows "Recommended eye 0: 2496x2688" but
"Rendering 2048 x 2048 pixels per eye" (the launcher's 2048 x 2048, below
their headset's), and a player whose Virtual Desktop recommends 4032 x 3648.

**Cause.** The eyes were the runtime's recommended size (`XrRuntime`), or a
square override (`--size`, the launcher's 2048 down to 1280). Every layer
refused more than 4096 a side (`maximumEyeSize`: OpenXR setup, the game's eye
views, the GPU bridge, the session's snapshots, the probes).

**What it does.** `spidy::scaledEyeSize` (`eye_resolution.hpp`): the
recommendation itself at 100%, otherwise each side times the percentage in
multiples of 8, shrunk with its shape to the runtime's `maxImageRect` and to
`maximumEyeSize`, now 8192. XrConfig version 13 (632 bytes) carries
`renderScale` (50-200; with `eyeSize` set only 100) and a spare word that
must be 0, else 1001. The launcher's "Render resolution" is a slider in steps
of 5 (`render_scale=` in launcher.ini; `eye_size=` is no longer read); after
Check its line shows the eye size from the probe's "Recommended eye 0", and
its memory figure is `vrCommitGb`. `run_game_vr.py --render-scale`,
`launch-game-vr.ps1 -RenderScale`; the report has `render_scale` and
`xr_runtime.recommended_eye`. Aim markers are drawn at `pixelAngle` times the
eye width over the recommended width (at least 1), so they keep their size at
100%. The blit that puts the game's presented frame into the headset image
samples at `SV_Position` times one over the target size instead of an
interpolated coordinate.

**In the game, no headset** (`tools/probe_vr_load.py`, the user's save,
phases stock, vr and turn, a fresh game per size, `spidy_stereo_probe.dll`
38f9ca8e pinned because another session rebuilt meanwhile):

| Eyes, views | vr fps | turn fps | GPU busy | video memory, vr | commit, vr |
|---|---|---|---|---|---|
| 3072 x 3264, 29 (occlusion) | 90.3 | 102.7 | 85-87% | 11,594 MB | 16,010 MB |
| 4608 x 4896, 29 (occlusion) | 66.1 | 70.6 | 92-93% | 14,166 MB | 18,620 MB |
| 4608 x 4896, 13 (none) | 32.2 | 42.0 | 67-74% | 14,617 MB | 19,276 MB |

The game alone (stock): 221-235 fps, video memory 8.6-10.0 GB, commit
12.7-14.2 GB. 150% against 100%: 2,532-2,572 MB more video memory and
2,524-2,610 MB more commit (vr and turn phases), 105-109 bytes per extra eye
pixel, so `EYE_COMMIT_BYTES` is 110. The engine reported both eyes' size,
render size and viewport as 4608 x 4896. `reports/vr-load-4608-shots-eyes/`
(capture mode, one heading, at half size) is a whole street view with no
black or flat rows or columns; a full-size crop from the bottom-right corner
past 4096 shows the pavement's texture. The first 150% run had no eye
occlusion (the probe's default views 13); its render commands were 67 MB a
frame against 17 and its frame rate half. Each game was closed with WM_CLOSE
and the display settings restored.

**Checks.** 168 core checks before another session's body tests were added
(new: the scale's sizes, the 8192 limit), 12 launcher checks (new: the
headset check's eye size and `vrCommitGb`; the session arguments without
`--size` and with `--render-scale`, capped at 50 and 200), 86 Python checks
(new: `scaled_eye_size` with the core cases, `vr_commit_mb`,
`rendering_line`, the argument checks; the probe's video memory and commit),
and the GPU test at 1536, 3072 x 3264, 4608 x 4896, 6144 x 6528 and
8192 x 8192. At 4608 x 4896 it first failed twice: the aim marker check's
window was too small for the catch target, which has a size in metres
(widened), and the game-screen copy changed values by 2 levels where 1 is
allowed (fixed by the blit change above). 4096 x 4096, 4608 x 1536 and
1536 x 4896 had passed it before.

**Not yet seen:** the headset. In the next report check `render_scale`,
`xr_runtime.recommended_eye` and the eye size samples; ask how sharp it
looks, and compare new eye pairs a second with the 77 of October 7.

## Walls the game sticks the player to — earlier build

The user, October 8: "sometimes the player gets attached to the walls in vr
(crawling mode i guess) which looks and feels like im clipping through the
wall. jumping from it fixes it."

**Cause.** The game's wall crawl (`HeroStateWallCrawl*`): a player who flies
into a wall sticks to it. In the session reports it is mover flags `0x80a6`
with collision flags `0x2060005` (the game moves the player without sweeping),
then `0x8001` / `0x2060000`, contact 0, Spidy not driving, body not grounded:
six stretches in the October 6-7 reports (16:36 four, 10:43 one, 22:08 one at
81 s), 1-2.4 s each, 20-200 m up, each ended by a jump. The eyes were the
tracked head placed upright from the player's feet (`GameTrackingRig`), and
the feet are on the wall: the 22:08 session's left eye at 180 s is on the
brick wall's plane.

**In the game, no headset** (`tools/probe_wall_crawl.py`, the user's save,
October 8; `reports/wall-crawl.json`, `reports/wall-crawl/on-wall.png`): the
nearest wall was 6.07 m from the hand, 12° up. Jump, web at it, reel: the game
took the player at 2.45 s (`0x80a6` / `0x2060005`, contact 0) and turned the
actor's up from 0° to 90° from the world's in 0.25 s, ending at (1, 0, 0), the
wall's normal by rays to 2e-7. Rays along that normal: feet 0.00 m from the
wall's surface; eyes 1.65 m upright from the feet (as before) 0.00 m from it,
stood off as now 0.50 m. Idle on the wall (`0x8001` / `0x2060000`) the actor's
up leaned away from the normal by up to 14° over 30-80 ms and snapped back,
again and again (clean rotations, not torn reads), so the stand-off goes level
from a wall and straight down under a ceiling, not along the actor's up.
The jump off made the actor upright in 0.3 s (airborne from 81° down); Spidy's
own flight (owned) never tilted it. Hook entries restored, modules stopped
with 0, the save files byte-identical afterwards. The first attempt that
morning stopped at the title: another window came in front, the game paused,
and it was closed.

**What it does.** `GameTrackingRig::update` takes the player's up (its actor's
second row; the world's up while Spidy's swing owns the flight). Past 45° from
upright (back within 35°) the player is on a surface: the head, eyes, hands
and swing aims are placed `standOff` away from it, level from a wall, straight
down from a ceiling, the feet staying the render anchor. The stand-off puts
the head `wallClearance` (0.5 m) from the surface's plane for the first
0.5 s (the actor turning onto it), then only keeps it `minWallClearance`
(0.25 m) away; it never shrinks while on the surface, so leaning back moves
away from the wall. It approaches its target by 63% in 0.06 s; off the
surface it returns to zero the same way. A snap or smooth turn pivots at the
head and a recenter keeps the head, stand-off apart.

**Checks.** 167 core checks (5 new: stand-off on a wall with eyes, hands and
web hands moved and the feet kept as anchor; the actor rocking 14° does not
move the head; leaning in to the minimum and back; turning and recentering
on a wall; a ceiling, a 30° slope, the 35-45° hysteresis), 84 Python checks
(XrData version 13, 744 bytes: `surfaceEntries`, `surfaceFrames`, `heroUp`,
`standOff`, `surfaceHeight`, `surfaceClearance`; report `surface`), the GPU
test.

**Not yet seen:** the headset. In the next report, `surface.entries` counts
the stretches; `head_height_m` near 0 or below with `head_clearance_m` near
0.5 is the fix at work. Ask how leaning on a wall feels.

## Smooth turning — earlier build

The user, October 7: "add smooth turning option".

**What it does.** `vr_settings::Values::smoothTurn`, degrees a second (0, the
default: the right stick snap turns as before). The SPIDY VR tab has a SMOOTH
TURN row after SNAP TURN in COMFORT (row 11, setting number 0x20b; GAME
SCREEN SIZE moves to 0x20d) with OFF, 60, 90, 120, 180 and 240°/S;
`Item::smoothTurn` is the last item (`vr_settings::lastItem`, which
`game_menu.cpp`'s `copyItems` now loops to). `GameTrackingRig::smoothTurn`
takes radians a second. While it is on, the right stick's X past a 0.2 dead
zone turns the tracking space about the head by `speed × tilt × seconds`
each headset frame, tilt rising linearly to full at 0.9; snap turning is off
meanwhile. A stick held into play waits for its release (below 0.3) after a
break over `controlHoldMs` (500 ms, a menu), as a snap turn always did, but
not after a stutter, so a turn in progress goes on through a hitch. The body
follows each turn through the tracking yaw it already takes
(`native_body`, `body::turnState`).

**Punches.** `game_punch` measured the fist's motion as the grip relative to
the head turned to the world by the tracking yaw, so any change of that yaw
counted as arm motion: a still fist 0.6 m out at 240°/s measured 2.5 m/s, over
the 2.2 m/s punch speed; a 30° snap turn put 15-20 m/s into one sample.
`Punches::turn` now turns each hand's last relative position and smoothed
velocity by the yaw change since the previous sample (`std::remainder` of the
tracking yaws), so only the arm's own motion counts.

**Protocol.** XrConfig version 12, 624 bytes: the reserved word after the
screen size is now `smoothTurn` (0-360; anything else is 1001). XrData
version 12, 712 bytes: `smoothTurn` at 704 (then a spare word); Python
`vr_settings.smooth_turn`, and the settings line for the launcher carries
`smooth_turn=`. The menu probe's `ProbeSettings`/`ProbeSample` are version 2
(36 and 64 bytes) with `smoothTurn`. Launcher: `SessionOptions::smoothTurn`,
`--smooth-turn` (left out at 0), `smooth_turn=` in launcher.ini, a "Smooth
turn" list; `launch-game-vr.ps1 -SmoothTurn`.

**Checks.** 162 core checks pass, new: smooth turning at full, half and
dead-zone tilt and to the left for a second at 90°/s (the head's forward
within 0.002 of 90°, 45°, 0 and -90°, its position unchanged with the head
0.4 m off the tracking origin); a 20 ms gap goes on turning, a 610 ms one with
the stick held turns nothing until it is let go and held again; a still arm at
240°/s measures over 2.2 m/s without `turn` and under 0.01 with it, and a
5 m/s punch during the turn measures 5 ± 0.05; the tab's rows, the new steps,
a launcher speed between steps (100 shows 90), the 0-360 range. 10 launcher
checks (the argument, the headset line, the range), 72 Python checks (XrData
v12 with smooth turning, the settings line) and the GPU test pass.
`tools/probe_menu.py` steps SMOOTH TURN to 60°/S before RESET ALL and expects
11 changes. How smooth turning feels, and the XR worker applying the tab's
change at once, need the headset.

## Midair drops — earlier build

The user, October 7: "sometimes while in the air i get pulled down fast as im
diving out of no where I think its a game mechanic lets stop it".

**What the reports show.** Every place in the recent session reports where a
step of the player's mover driven by Spidy in the air was followed by one the
game ran itself (`motion_samples`, status 2 then 1, contact 2): 14:13 session
(play folder `dist\Spidy-0.1.1`), 3 in 11 minutes of retained samples; 14:09,
1; 10:43, 2; October 6 19:22, 2; 16:36, 2; 14:50, 4. In 7 of those 14 the
game's airborne vertical speed (`air_vertical`) went in one step from Spidy's
(+6 to -8 m/s) to -36 to -48 m/s, and on from there toward about -50; in 3 to
about -20 m/s. The 14:13 session's three, by the XR samples' `seconds`: 508 s
(-1.2 to -42.5 m/s, both webs held, two steps in 128 ms), 583 s (+3.0 to
-36.3, released flight, one step in 107 ms) and 786 s (-8.2 to -42.4 on the
first step after 13.3 s in the pause menu, which the menu button had opened in
midair). The game's air state (event handler
`0xa7b3a0`, vtables `0x38c1340`, `0x38c9090`) keeps its own fall going through
Spidy's flight, from the time airborne; the displacement override Spidy gives
its airborne event while driving does not reach that, so any step it runs
itself in midair starts at that fall speed. Each place was a let-go by the
swing: `relinquishMotion` (enabled 0) or a lease that ran out.

**Why the swing let go.** In `game_swing.cpp`, any of these in midair cancelled
the swing (webs released, flight handed to the game): the input unfocused or
its 100 ms lease lapsed (the XR worker's gameplay gate closes when the game
camera has not committed for 100 ms, so a frame of over 100 ms closes it, and
the first active headset frame after any break marked both hands untracked);
no step of the mover for 50 ms (a long frame); a step the visits missed; a
controller sample more than 0.1 s after the previous one. And a step that came
more than 150 ms after Spidy's command ran without it (the pause).

**Changes.**

- `game_swing::InputHold` (`include/spidy/game_swing.hpp`): a focused input
  sample stays in use for `controlHoldMs` (500 ms, the XR worker's existing
  stutter allowance) after input stops or turns unfocused, as a sample that
  is simply not new (same serial: no new presses, yanks, shots or punches).
- `game_swing.cpp`: past the hold, flight the swing owns coasts (`coast()`,
  once per stretch): webs, grabs and shots end, the input clocks start over,
  and the body flies on with no input under the swing's gravity until it
  lands; the game has it on landing, as before. Without owned flight, the
  swing cancels as before. No step for 50 ms: wait for the next one (the swing
  still lets go when input is live and no step came for 500 ms, as on a
  perch). A step the visits missed: carry on from the observation. The world
  identity, the mover's 0x80000000 flag, the movement module stopping and an
  invalid prediction still cancel.
- `GameTrackingRig` (`src/game_tracking.cpp`): the first active frame after a
  break shorter than `controlHoldMs` keeps the hands tracked; after a longer
  one, a recenter or the first activation, each hand must squeeze again, as
  before.
- `Swing::predictNativeStep` (`src/swing.cpp`): a controller sample more than
  0.1 s after the previous one starts each hand's motion afresh (no yank from
  the gap) instead of releasing the webs.
- `native_movement` (`commandApplies`): a command governs the first step of
  the player's mover after it arrives, however late that step comes, then
  every step within its 150 ms lease as before. No protocol changed.

**Measured in the game without a headset** (`tools/probe_air_handoff.py`, the
user's save perched above a rooftop, loaded with the virtual controller; the
same probe and save for both builds, a fresh game each): it jumps with A,
shoots a web 60 degrees up the most open direction, reels from 0.5 s, and at
1.6 s turns the input unfocused for 0.3 s with the grip and trigger held
(`hold`), releases at 2.7 s, suspends the game process for 0.25 s (`hitch`),
turns the input unfocused for 2 s (`coast`), then unfocused 0.15 s, the game
suspended 3 s and unfocused 0.3 s more (`pause`), reading every step of the
player's mover. Reports: `reports/air-handoff-before.json` (the play folder's
movement and ray modules, `dist\Spidy-0.1.1`) and `reports/air-handoff-after.json`.

| | play folder's build | this build |
|---|---|---|
| `hold`: airborne steps without Spidy's command | 138 | 0 |
| `hold`: vertical speed, start → end | +11.6 → -46.9 m/s | +11.6 → +11.3 m/s (reeling) |
| `hold`: largest change between two steps | 26.8 m/s | 0.01 m/s |
| web still held after `hold` | no | yes |
| `hitch`, `coast`, `pause` | not reached: the player had landed | 0 steps without command; at most 0.09 m/s a step |
| `coast`: vertical speed over 2.3 s | | +5.9 → -8.1 m/s (6 m/s²) |
| `pause`: first steps after the 3 s freeze | | driven; -8.0 → -15.0 m/s over the window |

Swing samples showed the flight owned throughout `coast` (398 samples, webs
released) and the web held through `hold` (63 samples). Both modules stopped
with 0, every hook entry was restored, and the save files were byte-identical
to the copy taken before the runs. 159 core checks (new: the input hold, the
rig keeping webs through a stutter, the first sample after a gap, a late step
taking its command), 10 launcher checks, 72 Python checks and the GPU test
pass.

Not checked: a real session's hitches and pause menu (the probe stands in
with process suspension and unfocused input commands), and how gliding under
Spidy's gravity after a long break feels. In the next headset report, a drop
would show as an airborne `motion_samples` entry with status 1 right after one
with status 2; gliding shows as status 2 steps while the XR samples' input is
unfocused.

## VR settings in the game's own Settings — earlier build

The user, October 7: "can we put the ingame vr settings as actual new items in
the ingame settings menu (not a seperately rendered drawer)?", then "build it".

**How the game builds its Settings.** From the executable's strings, RTTI,
reflection tables and disassembly, then a research DLL in the running game
that logged the Settings' Flash calls and callbacks (addresses in
[REFERENCE.md](REFERENCE.md)). The Settings are Scaleform (AS3) screens. The
pause menu's Settings (`PopupSystemMenuUI`) build every tab of the settings
config (`configs/uiconfig/uisystemmenu.config`: 9 tabs, ids 2-10) with one
builder (`0x807740`) and hand the list to Flash (`OpenOptions`, through the
UI's call wrapper `0x1d1cf30`): 8 of them, since CONNECTIONS (id 10) has no
row to show on PC and its builder returns nothing. A tab's items name the game's settings by number (0-122); the settings
system answers a number's kind, value and default from unchecked 40-byte
records. Flash calls back `UpdateOption` (setting, value), `ResetOption` and
`ResetAllOptionsForCurrentOptionsMenu` (tab id); set and reset skip numbers
above 0x7a, RESET ALL tab ids above 10. A setting with no choices of its own
is the game's OFF/ON list.

**What Spidy adds** (`src/game_menu.cpp`). When the Settings call
`OpenOptions`, one more tab is built by the game's builder and pushed onto the
list: a copy of the GAME tab (id 93) holding 13 items copied from the config's
first heading and its first setting with choices, numbered 0x200-0x20c, their
texts and choices Spidy's (`vr_settings::rows`: WEBS, BODY, COMFORT; 6
switches, 4 lists). Hooks answer the getters for those numbers (kind, value =
the choice shown, changeable, default), take set and reset, answer the texts'
hashes only while Spidy's tab is being built, and take RESET ALL for tab 93
(Spidy's defaults, then the tabs built again as the game does). 11 hooks; each
function's first bytes are compared with the supported game's first, and all
are enabled and disabled together. The XR worker installs it when it starts,
takes the tab's changes each headset frame (`take`, then `applySettings` as
before) and publishes its values (`publish`: what X switched shows too). A
launcher value between steps shows as the nearest step and stays until changed.
The panel is gone: `vr_settings::Panel`, its canvas, `D3D12Renderer::blitPixels`
and `XrRuntime::panel`; no trigger is kept from the virtual controller.

**Protocol.** XrConfig v11 (layout unchanged) and XrData v11: the panel's
frame counts became `menuTabs` (the game built its Settings with the tab),
`menuInstalled` and `menuStatus` (93xx-94xx not hooked, 95xx not built);
`run_game_vr.py` reports `menu_tabs`, `menu_installed`, `menu_status`. Headless
probes: `SpidyMenuStart`, `SpidyMenuSample`, `SpidyMenuStop`.

**Measured in the game** (`tools/probe_menu.py`, the user's save, free roam,
steered with the virtual controller's left stick): Settings listed SPIDY VR
after KEY MAPPING (Up from GAME); the tab showed the start values (33 m/s as
32 M/S, the web shooter OFF); right on AIM MARKERS gave OFF, on SWING SPEED
LIMIT 40 M/S, on YOUR OWN BODY OFF, X put it back ON, right on SNAP TURN 45°; Y
asked "Are you sure?" and A put Spidy's defaults back (4 more changes, the web
shooter ON); each step's values as expected, 9 changes, the tab built 7 times
(each opening and refresh), status 0; B three times resumed play. All 11 hook
entries patched while in and the game's own bytes after `SpidyMenuStop`. In an
earlier run of the same code (texts not yet gated to the tab's build), Settings
with the hooks out listed the game's 8 tabs only, and with them in again SPIDY
VR (opened from the remembered position: KEY MAPPING, then down). The game's
settings file (`-userprefs.save`) stayed byte-identical; `slot0-s.save` was
rewritten by the game's own save after Continue, as on every probe's load. 155
core checks (the panel's 5 replaced by 2: the tab's rows, sections and choices;
switches and lists round-trip, launcher values show the nearest step, every
default together is `Values{}`), 10 launcher checks, 72 Python checks
(`menu_tabs`, `menu_installed`, `menu_status`) and the GPU test (its panel part
removed) pass.

Not checked: the XR worker's part (install at session start, changes taken and
applied, XrData telemetry) runs only in an OpenXR session; how the tab reads on
the headset's game screen. The title screen's Options keep their own lists and
show no SPIDY VR tab.

## The web shooter — earlier build

The user, October 7: "Lets add the ability to shoot the web projectiles
spiderman has normally (not the regular webs, the web bullets)".

**What a pull does.** The trigger of a hand whose web is neither attached nor
holding a target shoots one web ball (`Shooter`, engine independent). A pull is
the trigger past 0.65 after it was below 0.35, the swing's reel thresholds; one
hand shoots at most every 0.12 s. A trigger already pulled when the shooter
starts, when its hand was busy, or through a tracking or focus loss (a menu,
the flat screen) is no pull until released. The ball leaves 8 cm ahead of the
aim pose. A bot whose chest (feet + 1.15 m) is within 0.12 rad of the hand's
line, or within his 0.45 m radius of it, nearer than 45 m and with no surface
between, takes it (the one nearest the line, as a share of what he may be off
it); otherwise the aim point is 0.4 m past the first surface the line meets
within 60 m, else open air at 60 m.

**How the game fires it.** Traced on October 7 with a research DLL hooking the
weapon path while the virtual pad's RB fired the equipped gadget (the Impact
Web: RB fires the gadget wheel's selection, and the web shooter was not it):
the hero's weapon state hands the weapon a fire event on the main thread
(`WeaponGame` vtable +0x108, 0xe3bd80), which stores the event's aim and fires
(0x2150460); `SpawnShot` (+0x110, 0x2150830) asks the muzzle (+0x160,
0x2150c40, the emitter component of that index) and spawns the shot actor
(+0x2f8, 0x215c190). The hero's `WeaponWebShooter` (vtable 391c950) is a
component of its own weapon actor, 0.5 m from the hero, not of the hero's
record. Fire events injected from the camera manager's update (0x897d30) with
an overridden muzzle spawned a `ShotWebShooter` at the scripted point that
flew straight to the event's aim point (+0x20) at 53-61 m/s and ended there,
or at the first surface; its matrix rows did not change its course. The event
layout, the shot ids (0x215f040 allocates from the game's own counter; 0x215f840
registers one, despawning what an older shot left in its ring slot) and the
target reference (0x1f7b8e0) are in `game_shooter.hpp`. A pedestrian has no
reference (0x1f7b8e0 gives 0), so the aim assist offers bots only.

**The module.** `game_shooter` in the ray bridge: the swing's input callback
updates it with each sample (after the swing's step, beside the aim previews,
so its rays never fault the swing); pulls are queued with the player's actor
and position, and the camera manager's update fires them on the main thread:
the web shooter fills its own part of the event (+0x100), Spidy sets the
emitter (right wrist 0, left 1), a new shot id, the target's reference (with
+0x39, so the game's own target point replaces the aim point when it
resolves), the aim point and the level facing, and the weapon's fire event
runs with the muzzle hook returning the hand (moved with the player since the
sample). The gadget is found by a registry scan on its own thread (nearest the
player, within 10 m); a fire checks that it is still registered and that the
two things the game reads without a check exist (its setup at +0x48, the
actor at +0x5b0). Signatures of all seven functions and four vtable slots are
checked before the hooks go in. Shots take no gadget ammo.

Protocol: XrConfig version 10 (options bit 512: no web shooter, up to 1023),
XrData version 10 (settings bit 16), both sizes unchanged; ray module exports
`SpidyShooterStart`/`Stop`/`Sample`/`Test` and `SpidyShooterData`
(`game_shooter::Data`, 160 bytes). The runner's report has `shooter` and
`shooter_samples`, and each sample `shooter` (fired, dropped, bots on offer).
The VR settings panel has "Web shooter" under WEBS: 13 lines, 560 x 940
points, 840 x 1410 pixels.

Checks without the game: 158 core checks (new: one shot per pull, a held or
half-released trigger never again, repeated samples nothing, both hands
together; pulls closer than the interval shoot once; a busy hand, a trigger
held after its web let go, through a tracking loss or a reset shoot nothing;
the aim assist takes the thug 3 degrees off the line, not one 14 degrees off,
behind a wall, behind the hand or 50 m away, one beside the hand within his
width, not one clear of it, and none behind a pillar, whose surface the shot
goes to instead; sky and wall aims; invalid tuning refused), 10 launcher checks
(`--no-web-shooter`; `web_shooter=0` from the headset's line), 72 Python checks
(the shooter's telemetry and torn reads, XrData v10 settings bit 16, the start
values and the line with `web_shooter`), and the GPU test at 1536 x 1536 and
3072 x 3264: the panel painted at 840 x 1410 pixels with the new switch on.

In the game, without the headset (`tools/probe_shooter.py` in a fresh game on
the user's save, perched on the Times Square lamp post, the player 35 s after
launch), the scripted left hand 1.2 m above the feet pulling its trigger
through `SpidySwingSubmit`:

| Aim | Shots | Flight | Off the aim | Start from the hand | End |
|---|---:|---|---:|---:|---|
| Level, along the camera | 1 | 60.2 m at 60.2 m/s | 0.00° | 0.00 m | 60 m out (1 s), surface 73 m |
| 30° down | 1 | 6.5 m at 51.7 m/s | 0.01° | 0.00 m | the pavement (0.4 m from the aim point) |
| Sky | 1 | 60.2 m at 60.2 m/s | 0.00° | 0.00 m | 60 m out, open air |
| 60° left | 1 | 10.4 m at 55.7 m/s | 0.01° | 0.00 m | a wall (0.4 m from the aim point) |
| 60° right | 1 | 60.0 m at 59.1 m/s | 0.00° | 0.00 m | 60 m out |

A trigger held for a second shot once; three pulls 0.2 s apart shot three
times; the shooter stopped and started again during play (0, 0) and shot
again. 10 pulls requested, 10 fired, none dropped; the main-thread hook ran
1,692 times; no swing fault; swing and rays stopped with 0, every hook entry
restored, and the save files byte-identical to the backup. Window captures at
the game's frame rate while a test shot crossed the street 5 m ahead show the
ball as a white motion-blurred streak, frame by frame. That run used a range of
80 m; a shot ends after one second (about 60 m) anyway, so the build sets 60 m
and nothing else changed.

Not checked: a ball reaching a thug and the game taking the thug as its target
(no bot was within 40 m in free roam; the report's `shooter.targeted` and
`resolved` count them in a fight), the sound, how the tick and the pull feel in
the headset, and the XR worker's start, stop and haptics, which run only in an
OpenXR session.

## Webs in open air as a setting — earlier build

The user, October 7: "lets make the ability to hook webs on max distance
without hitting an object (webs on nowhere) as an optional setting (on by
default)".

The swing already had the switch: `SwingConfig::airAnchors`, on, read by
`Swing::shot`, which both the grip press and the aim previews call. A ray that
meets nothing within reach (`maxRange`, 100 m) attaches the web in the air
there when it is on, and misses when it is off. Nothing in the game turned it
off until now.

- `Swing::allowAirAnchors` switches it during play. Shots from then on follow
  it; a web already attached keeps its anchor. (The panel shows only with the
  game screen, when the swing has let go of every web anyway.)
- `SpidySwingSettings` passes it: Settings version 2, still 24 bytes, the
  reserved word is now `airWebs`; version 1 and values above 1 are refused
  (2001). `previewAims` reads the solver's configuration too, so with it off a
  hand aimed into open air previews `none` (no marker, as a press would miss),
  and the red cross for an air point the body has no line to goes with it.
- VR settings: "Webs hold in open air" under WEBS, after "Webs catch props
  and thugs". One more 66-point row: the panel is 560 x 874 points, 0.9 x
  1.40 m in the headset, 840 x 1311 pixels at 1.5 pixels a point.
- A session starts with it off from XrConfig options bit 256
  (`run_game_vr.py --no-air-webs`, `Launch Spidy VR.cmd -NoAirWebs`, the
  launcher's "Webs hold in open air"). XrData settings bit 8 reports it; the
  headset's line and the launcher's settings file carry `air_webs`.

Protocol: XrConfig version 9 and XrData version 9 (sizes unchanged, 624 and
704 bytes; options up to 511), Settings version 2. As before, the runner and
the DLLs must come from one build: an older runner against these DLLs fails
with "Game XR start: 1001", this runner against older DLLs with "Game XR
protocol mismatch".

Checks without the game: 153 core checks (new: switched off during play, a
press into open air misses and its preview has neither web nor hit, a web
already attached keeps its anchor, a surface still holds, and switched on
again open air holds; the panel's switch switches back), 10 launcher checks
(`--no-air-webs`; `air_webs=0` from the headset's line), 71 Python checks
(XrData v9: settings bit 8 on and off; the start values and the line with
`air_webs`), and the GPU test: the panel painted at 840 x 1311 pixels, the
new switch showing its own value (on) beside punching off, drawn into the
eye image within 0 levels, the D3D12 debug layer clean.

In the game, without the headset (`tools/probe_aim.py --settings` in a fresh
game on the user's save, the player 26 s after launch): the sky aim
previewed open air at 100.0 m, as before. `SpidySwingSettings` with `airWebs`
0 returned 0, and the same aim previewed none (no web, nothing met: a press
would miss); with 1, 0 again and open air at 100 m. The grab switched as in
the preceding build (a prop 6.3 m away: anchor, prop, anchor, prop), a 70 m/s
limit was refused (2001), punching started and stopped twice (all 0). No
swing fault in 21 aims, both modules stopped with 0, every hook entry
restored, and the save files byte-identical to the backup taken before.

Not checked: what only the headset shows: the new row in the Quest, a click
on it, and the XR worker handing the launch option and the panel's switch to
the swing (`applySettings`), which runs only in an OpenXR session.

## VR settings beside the game's menus — earlier build

The user, October 7: "can you add a vr settings section to ingame menu?"

The game draws its menus itself; what the headset shows of them is the
game's presented frame on a virtual screen (October 5, sixth build), and Spidy
draws nothing into the game's UI. The section is therefore Spidy's own panel,
hung beside that screen whenever the headset shows it.

**Where and when.** The panel is a second OpenXR quad layer, 0.9 m wide,
starting 8 cm past the game screen's right edge and square to the viewer's
line of sight there, centred on the screen's height
(`vr_settings::placement`). It opens when the game screen comes up within 2 s
of the menu button pressed in play (the pause menu); on any other screen (the
game menu, the main menu, loading, cutscenes) it is folded to a 0.42 m tab
level with the panel's top. Its X folds it for the rest of the session; the
tab opens it again.

**Input.** Each controller's OpenXR aim pose (the space the screen is placed
in) is intersected with the panel's quad (`aimAt`; a ray along the face or
from behind misses). A pull past 0.75, released below 0.35, acts on what the
ray points at: a switch's whole line switches it, a stepper's left or right
half steps it. A pull already held when the screen came up, or begun off the
panel, does nothing. The virtual Xbox controller gets a copy of the frame in
which the trigger of a hand pointing at the panel, or holding a pull the
panel took, is 0; every other control stays the game's. A click ticks that
hand (0.3, times the vibration setting).

**Drawing.** `vr_settings::Canvas` paints text with GDI (Segoe UI, grayscale
antialiasing) into a 32-bit DIB and the switches, buttons and pointer dots as
signed-distance shapes, 1.5 pixels a point (840 x 1212 for the panel), less
when the eye images are smaller. It repaints only when what it shows changes
(hover, a pointer's whole-point position, a value, open or folded).
`D3D12Renderer::blitPixels` uploads the pixels (B8G8R8A8, one copy) and draws
them with the game screen's blit into the top-left of the right eye's
swapchain image, which the game screen leaves unused; `XrRuntime::panel`
submits that rectangle as the second quad. Every frame's swapchain image gets
it drawn; only a repaint uploads. A panel that fails to paint or upload is
dropped for the session with a status message, and the game screen goes on.

**The settings, live.**
- Aim markers: the worker's switch, as X switches it.
- Webs catch props and thugs: `SpidySwingSettings` (new ray-module export,
  Settings version 1, 24 bytes) calls `game_grab::allow`. Off, the grab lets
  go of what it holds or trails at the next input sample (`cancel()`: props
  back to their physics, bots to the game's flight), and previews and presses
  go to the swing. A swing started without the grab (`-NoWebGrab`) starts it
  there, its hooks installed under the swing's simulation lock.
- Swing speed limit: the same export sets the swing's cap and the solver's
  (`Swing::limitSpeed`). The movement module is now always started with
  65 m/s, the panel's highest step: it only rejects faster requests.
- Punch thugs: `SpidyPunchStart` and `SpidyPunchStop` from the worker.
- Your own body: the body command's `bodyOn` flag. Off, `native_body::drawn()`
  is false at once, so the eyes hide the hero and the overlay draws gloves; a
  session started with `-NoBody` starts the body module when switched on.
- Snap turn: `GameTrackingRig::snapTurn`, off or 15-90 degrees (30 until now,
  always); `reset()` keeps it.
- Controller vibration: `XrRuntime::hapticStrength` scales every pulse; off
  sends none.
- Game screen size: 2.4, 3.2 (as before) or 4.2 m wide at 2.5 m, the game
  screen and the flat-mode screen alike.

**The next session.** XrData reports the settings. When a session ends with
other values than it began with (the panel, or X), `run_game_vr.py` prints
"VR settings from the headset: aim_markers=... screen_size=..." and the
launcher saves them as its options (`headsetSettings`). The launcher's
options card, `run_game_vr.py` (`--snap-turn`, `--haptics`, `--screen-size`)
and `Launch Spidy VR.cmd` (`-SnapTurn`, `-Haptics`, `-ScreenSize`) start a
session with the three new ones.

Protocol: XrConfig version 8, 624 bytes (snapTurn, haptics, screenSize and a
reserved word after the runtime path); XrData version 8, 704 bytes (settings
bits 1 web grab, 2 punch, 4 body; snapTurn, haptics, screenSize, swingSpeed,
settingChanges, panelFrames, tabFrames). `run_game_vr.py` and the DLLs must
come from one build: an older runner against these DLLs fails with "Game XR
start: 1001", this runner against older DLLs with "Game XR protocol
mismatch".

Checks without the game: 152 core checks (new: the layout fits and each
control hits its own setting; steppers stop at their ends and step a value
between steps to the next one; the panel hangs past the screen's edge, square
to the viewer, for all three sizes, and aim rays meet it at its centre but
not from behind or from the screen; one click per pull, a held pull, a pull
begun off the panel and a trigger held as the screen came up click nothing,
a press keeps its trigger from the game until released; the tab opens it,
close folds it for the session; snap turn off and 90 degrees), 10 launcher
checks (the new arguments; the headset's line applied, clamped, ignored when
unchanged), 71 Python checks (XrData v8 fields; the line printed only for
changes), and the GPU test: the panel painted at the headset's 3072 x 3264
eye size (840 x 1212 pixels), drawn into the corner of a typeless sRGB eye
image within 0 levels with the rest of the image untouched, drawn again
without new pixels, the tab uploaded at its new size, and the D3D12 debug
layer clean (`vr-settings.bmp`, `vr-settings-tab.bmp`).

In the game, without the headset (`tools/probe_aim.py --settings`, the user's
save, perched on the lamp post): the swing started without the grab, as a
`-NoWebGrab` session does, and a hand aimed at the throwable prop 51.3 m away
previewed an anchor at 51.0 m, on the prop's surface; `SpidySwingSettings` with the grab on and 48 m/s
returned 0 and the same aim previewed the prop; off at 10 m/s, the anchor
again; on at 32 m/s, the prop. A 70 m/s limit was refused (2001). Punching
started, stopped, started and stopped (all 0). No swing fault in 19 aims,
both modules stopped with 0, every hook entry restored, and the save files
byte-identical to the backup taken before.

Not checked: anything only the headset shows. The panel is drawn by the XR
worker alone, which needs an OpenXR session: its size and sharpness in the
Quest, pointing and clicking with real controllers, the pause detection with
the game's real pause timing, and the quad layer on Virtual Desktop.

## Interact button and aim markers — preceding build

The user, October 6 evening: "lets add an interaction button in vr and maybe
some nice optional indicator\crosshairs for better aim".

**Interact.** The game's context actions and its web strike share one button:
Triangle, Xbox Y, keyboard F (the game's registry binds `Button_Y_1 = 33`,
scancode F, and lists no separate interact key). A GameFAQs walkthrough:
"Press Triangle to open the elevator doors", "press Triangle to interact with
the computer", backpacks likewise. In immersive VR the virtual Xbox controller
passed only Start (menu button), Back (Touch Y), A (the swing's jump) and the
left stick, so nothing could press the game's Y. Now Touch B gives the game's
Y (`game_pad::Walk::interact`) while VR steers and the swing does not own the
player. A press keeps the meaning it started with until it is let go: B held
from the game screen, where it is Back, is no interact (as A from the screen
is no jump), and an interact still held when it opens a menu or a scene (the
screen comes up 250 ms after play stops) is no Back there. XR telemetry
counts the presses (`interacts`).

**Aim markers.** What a grip press would do is worked out in the swing
module, inside the game's world-query callback, with the rays and picks the
press itself uses, for each free hand: first the grab's pick
(`WebGrab::preview`, against a read-only view of the targets: the press's own
pick starts following its target, the preview's does not), then the swing's
shot (`Swing::shot`, which the press now calls too, so the two cannot
disagree). The result per hand is anchor, air (no surface within 100 m: an air
anchor), blocked (what the ray meets cannot hold a web, the anchor is nearer
than the shortest rope, or a wall stands between body and anchor), prop or
character, with its point and normal. Previews run once per input command,
only while sampled (`SpidyAimSample`, 250 ms lease: with the markers off they
cost nothing), and after the swing's step in each callback: an error from a
preview ray is never read, so it cannot fault the swing. The headset overlay
draws one marker per free hand: a white ring and dot (anchor), a faint dashed
ring (air), a red cross (blocked), amber corners around the target (prop,
character); facing the viewer, a fixed size on screen (13 pixels of radius at
the eye image's pixel angle), a dark outline behind the colour, tightening as
the grip closes. A surface marker is placed where the image's own aim line
crosses the surface's plane (`onAimLine`), so it stays on the line the
hand points along while the next preview arrives. None for a hand whose web
is attached or holds something, none nearer than 2 m to the hand (a blocked
one 3 m: a lowered hand points at the floor by the feet). X switches them in
immersive VR (a short pulse on the left hand); `-NoAimMarkers`, the launcher's
"Aim markers" switch, or XrConfig option bit 128 starts with them hidden.

Protocol: XrData version 7, 664 bytes (`interacts`, `aimMarkers`, `markers`
drawn); XrConfig options up to 255; the ray module exports `SpidyAimSample`
and `SpidyAimData` (AimData version 1, 96 bytes). An older ray module without
them leaves the markers off.

Checks without the game: 146 core checks (new: a shot preview attaches where
the press attaches, and names what it met when it would miss; marker size on
screen at 5 and 50 m within 2%, outline behind the colour, squeeze tightens,
invalid input draws nothing; the aim-line crossing and its fallbacks; B is the
game's Y only through the swing's leave in VR and stays B on the screen), 9
launcher checks (`--no-aim-markers`), 69 Python checks (XrData v7 fields), and
the GPU test, which now draws every marker kind with the overlay renderer over
the lab scene in a 1536 x 1536 sRGB eye image and finds each one's colour and
outline (`aim-markers.bmp` beside its other images).

In the game without a headset (`tools/probe_aim.py` after `probe_menu_pad.py
start` and `pad a --until-player`, the user's save: free roam, Spider-Man
perched on a lamp post beside a building; `reports/aim-probe.json`): a
scripted hand 1.2 m above his feet got a preview for each of its 15 aims, the
swing never faulted, both modules stopped with 0 and every hook entry was
restored. Sky and the open street: air at 100 m. The building beside him:
anchors at 11.4-16.1 m; facades across the street at 36 and 60 m. Thirty
degrees down: the ground and the building at 5.3-12.4 m. Straight down:
blocked at 1.2 m, the lamp under his feet nearer than the shortest rope.
Aimed at the nearest throwable prop, 51.3 m away: prop. The save files were
byte for byte unchanged afterwards.

Not yet seen: the headset session (how the markers look and feel; B on a real
interact prompt). Whether the game's own interact prompt appears in the eye
images is unknown: its world markers, such as enemy reticles, do not (see
"Game screen for menus, hint cards and cutscenes" below), so a prompt may
show only on the monitor and the game screen.

## VR stays immersive in fights — checked in the game without a headset

The user, after the 16:36 session
(`dist/Spidy-0.1.0-win64/Spidy-0.1.0/reports/game-vr-20261006-163630.json`):
"in combat game goes to flatscreen mode for some reason".

What the report shows. Its samples cover the last 622 s. Between 946 and
1002 s the headset showed the game screen six times: 3.1, 4.3, 4.1, 17.9 and
17.5 s, then 0.4 s without a player. Every stretch had gate `no_camera_commit`
with the commit counters standing still, and the last committing camera was
still `follow`. The game was not paused: 207 physics steps a second in the
17.9 s stretch (the game's own rate once the eyes stop), and the hero moved
76 m during it. Each stretch began 0.1-1.2 s after the user's web caught a thug
(bot grabs at 944.7, 945.5, 950.5 and 962.9 s; no fling was asked, `flings`
stayed 0). The last one ended with the game replacing the player (a respawn,
then an autosave at 16:53:22). Prop grabs in the same session, and two bot
flings at 13:36, left VR immersive. In none of the October 6 reports did
`camera_mover` ever read `combat`.

Cause, from the executable. A camera mover's per-frame update is vtable
slot 21 (+0xa8). The camera pipeline calls it at 1e1b7c6 with the mover, its
CameraTarget, the frame's seconds in xmm2 and two flags. FollowCameraMover,
LookCameraMover, LookCameraMoverGame, TurretCameraMover, PerfTestCameraMover
and PhotomodeLookCameraMover share one update (1e1cf80), and it ends by calling
slot 24: the commit (1e1d600) that the input bridge hooks. CombatCameraMover's
update (5c2630, used by no other class) never calls the commit. It places its
camera itself, through SetTranslation (191c590) on the same camera
(`[[mover+8]]`), and reads its target's +8 as the actor record, as the commit
check does. The gate accepted the combat camera's vtable, but no commit ever
came from it. So every fight read as no camera commit for 100 ms, the
condition meant for pauses, loading and cutscenes, and after 250 ms the headset
put the game's frame on the screen. The Miles Morales first-person camera
research (`.research/miles-camera`, same engine) hooks the combat mover's
slot 21 separately for the same reason.

Change: `game_bridge.cpp` hooks the combat camera's update. At start it checks
the update's entry bytes and that the combat vtable's slot 21 points at it
(else 1003). After the game's own update it records the camera exactly as a
commit, with the same validation (the player's CameraTarget, the follow or
combat vtable, finite orthonormal transforms) and the same telemetry. Only the
follow camera's commit takes the camera-offset experiment's write. No protocol
change: bridge Data and XR telemetry are as before, and in a fight
`camera_mover` should now read `combat`. `bridge_game.py` and
`capture_movement.py` check the fifth hook entry too. The fixed bridge is copied
into `dist/Spidy-0.1.0-win64/Spidy-0.1.0` (the old one is staged under its hash
in that folder's `reports/bridge-modules/caea951a…`).

In the game without a headset (`tools/probe_menu_pad.py` start, `pad a
--until-player`, `player --follow`, `stop`), on the user's save (the checkpoint
written after the 16:36 respawn):

- The bridge started (0) with the combat update hooked (its entry a jump).
- Free roam with the player handed over: gate open in 98.9% of 95 samples,
  486 of 486 camera calls on the player, all from the follow camera. Outside a
  fight the combat camera did not update.
- SpidyStop returned 0, and both camera entries were restored byte for byte.
- No bot was in the game (the crime was over), so a fight was not measured
  under the new hook. The save files were unchanged afterwards
  (`reports/backups/saves-20261006-combat-camera/`).

143 core checks, the GPU test and 68 Python checks pass. There are no new
tests: the hook needs the game.

Headset check: start a fight (a crime with thugs). VR should stay immersive,
with webs and punches working and no flat screen. In the report during the
fight, `gate` should be empty, `camera_mover` `combat` and `presentation`
immersive. If a fight still goes flat, its `gate` and `camera_mover` say
which camera it was. Finishers and death use movers that place their camera
from an animation (MeleeRelAnimCameraMover, DeathCameraMover); those still go
to the game screen.

## The body through the game's own moves — current build, not yet run in the game

The same 16:36 session was the body's first in the headset (details:
[BODY.md](BODY.md#the-first-headset-session-october-6-1636)). With the eye
views on, the body was on the hero in all 10,761 samples; at full blend the
head joint sat 0.0 m from its place in every one and each wrist within 1 cm of
its target in 89-96% of them (at most 0.20 m, beyond the arm's reach); the
hero turned between his pose job and the render in 21 samples, at most
0.099 rad.

The defect: for 302 samples (about 16 s, in stretches of up to 8 s) the
body's blend fell from 1 to under 0.1 within a frame and kept starting over
every frame or two, while the game itself moved the hero (a landing after a
web release, ledge climbs, a jump off a roof): the hero showed mostly the
game's animation, arms up to 1.9 m from the controllers. Only a change of rig
in the hero's pose job started the blend over, so his jobs alternated between
rigs. Now each rig keeps its own cache entry and the blend and yaw stay across
them; the eyes hide the hero when his latest job is one the body could not
turn. New telemetry: `body.rig_switches`, `body.hero_jobs_max`, the rig in each
sample, and per-sample `punch` (bots within reach, punches, fist speeds).

No punch landed in that session: its fights were on the flat game screen
(above), where the fists get no tracked input.

143 core checks, the GPU test and the Python checks pass; the fix has not run
in the game yet.

## Your own body and punching — preceding build, measured without a headset

The user: "lets add spiderman actual avatar as a full body presense we control
and allow physical punching". Design, numbers and open items:
[BODY.md](BODY.md).

Measured with `tools/probe_game_body.py` in five fresh game processes on the
user's rooftop save (the hero perched on a vent cap), October 6:

- The hero's rig as its pose job names it (1601290, rig at job +0x28): 237
  joints, every name resolved through its hash (`reports/body-probe.json`).
  `a_body` is the body's root with `pelvis` and the spine under it; the
  identification by name is built in, and found the same rig in every run.
  One hero pose job per render frame.
- Body on, a scripted headset at 1.75 m eye height looking ahead, the left
  controller raised above the head, the right one at the chin 0.5 m ahead:
  both wrists 0.0 mm from their targets in the world, the head joint 0.0 mm
  from its place, the head shrunk to 0.001, body scale 1.031, 0.03 ms of
  solving a frame (`reports/body-probe/ik-third-person.png`: the hero standing
  on the vent with his left hand raised and no head).
- The left eye's image, looking 50 degrees down
  (`reports/body-probe/eye-body.png`): his right arm reaching ahead, the hand a
  closed fist with the thumb across the fingers, the web shooter at the wrist,
  his feet on the vent below, nothing of the head. With the body off the eyes
  hide the hero, as before (`eye-hidden.png`).
- The game's damage system: a blow of 30 (kMelee, kFlyBack) straight to a bot
  126 m away took its health from 100 to 67; 5 (kStagger) to the hero, 110 to
  105, and he was shoved 7.9 m. A scripted fist driven through that bot's chest
  at 6 m/s, as controller samples through the swing module's input, landed one
  punch (5.6 m/s, strength 0.64, kKnockdown, 29.1 damage), issued one request,
  and took the bot's health from 100 to 68 (`reports/body-probe-fist.json`).
  The bot was in `BotStatePlayCinematicGame` and played no reaction.
- Not measured: a free-roaming thug's reaction (none came within reach), and
  the hero's turn between pose job and render (he did not walk off the perch;
  `body.turn_max` in the session report measures it in VR).

143 core checks (18 new), the GPU test and 68 Python checks (2 new) pass.

## Webs as ropes: weight, swinging, the web let go of follows its prop — preceding build, tried in the headset

The user, after trying it (`Spidy-0.1.0\reports\game-vr-20261006-133652.json`,
from the release folder): "i tested in vr and it worked good,only issue is on
release the bins fell into the floor (still visible but partially in it)". In
that session 11 yanks, 5 catches (0.5-0.6 s after the yank), 4 throws at
10.2-16.7 m/s, no web lost. Eye snapshot 0025 shows a thrown blue bin as a thin
sliver in the road; 0023 shows the let-go web lying on the road to it.

The bins, measured in the game without a headset (`reports/grab-probe-rope.json`,
`grab-probe-rope-yank.json`, new `--bodies` option): the thrown bins' bodies
rested on the road (the player stood on it at y 0.85; their centres at
1.20-1.22), so the bodies did not sink: the drawing did. A breakable trash can
is two bodies; the game draws it from the top piece, which is the only body
Spidy's freeing lets move: the can itself (the primary) keeps zero velocity
whatever it is given, held as an unbroken breakable. Within 0.1 s of being
freed the top piece fell 0.74 m through the can and the can was drawn with it,
0.74 m into the pavement; thrown, it landed with the can drawn 0.8 m into the
road. Drawn from the primary instead, the can stood upright and never moved;
that change was reverted. Breaking the prop off as the game's own yank does
(`BustBreakablesWhileWebYanked`) is the open fix; details in
[WEB-GRAB.md](WEB-GRAB.md#breakable-props-only-the-top-piece-flies-open). The
same probe measured the let-go web: its drawn end followed the prop 15.5 m, a
median 0.45 m from it, until it dissolved after 2 s.

The first report of the session, as written before the headset check:

The user: "right now it feels like objects have no weight while webbed and dont
feel like they are being pulled by a web, they feel like the web is a stick
that holds the object (i can hold a bin in the air with a web). when releasing
the web the objects also seem like they barely have any weight. also when
releasing the object the web stays in place attached to last point of contact
with the object as if its still there instead of falling".

The grab samples of the 10:16 session (`reports/game-vr-20261006-101603.json`,
14 grabs, 10 yanks, 5 catches, 5 throws, physics at 0.9-2.2 times real time):

- Two of the four throws reached the 40 m/s cap, (3.2, -25.2, 30.9) and
  (-30.1, 16.2, 20.8) m/s: the held prop moved at 18 m/s or more and the throw
  multiplied that by 2.2. A held prop followed a point 1.35 m ahead of the
  wrist, held up, so a turn of the wrist alone swung it fast. The other two
  left at 5.4 and 1.5 m/s.
- Yanks launched props at 17-45 m/s, mostly upward: (2.3, 26.8, -12.0),
  (9.3, 32.8, 8.6), (-29.5, 32.1, 11.1). The arc aimed where the hold point
  would be after the whole flight, carried on at the hand's velocity, which
  during the pull gesture is the gesture itself. Let go of 0.14-0.41 s into
  the flight (5 times), props flew on at 12-33 m/s, four of them rising at
  12-13 m/s.
- The web line: the game draws the webs. A released rope dissolves with its
  far end at its target position, which Spidy stopped updating when the grab
  ended.

Changes (design and numbers in [WEB-GRAB.md](WEB-GRAB.md)):

- Every web is a tension-only rope with weight on its end: one web pulls with
  at most 2400 N (80 m/s² on the game's 30 kg props), its stiffness and
  damping are rates, gravity always acts. A held prop hangs from the wrist on a
  web 0.25 m longer than its radius and swings; turning the wrist does not move
  it. A target the web brings in is braked to the edge of reach as a hand
  catches it, then caught.
- Throws: the prop's own velocity from the swing, times 1.6 for targets up to
  60 kg (less above), capped at 25 m/s, keeping its own spin.
- Yanks: 7-15 m/s from the pull (5 times its speed), slower for heavier
  targets, aimed where the hand is carried with the player, the jerk no faster
  than 1.5 times that; in flight the web reels in at 0.8 times it and pulls
  only what falls behind, along the web.
- The web let go of: for 1.5 s the grab telemetry (version 2, 336 bytes)
  reports the target as trailing; the XR worker asks for that web with
  `attached` 2; `native_webs` releases the rope keeping its handle (ReleaseRope
  67b610's fourth argument) and aims its far end at the prop each update until
  it has dissolved. A released rope's update still reads that position (679dc0
  → 6786c0 → +67c into the anchor +720), from the disassembly.
- The controller hums by the web's tension (0.6 times it) between event pulses,
  in the game and the lab. The grab telemetry carries each hand's tension.

Offline: a simulation with ground and friction (90 Hz hands, 30 Hz physics):
a can reeled from 12 m is caught after 1.2 s and hangs 0.7 m below the hand;
yanked from 8, 20 and 40 m it launches at 15-20 m/s and is caught at under
1.5 m/s after 0.7-2.7 s, never more than 0.5 m behind the wrist; from a perch
32 m up it is reeled up along the web and caught; an underhand swing throws
a 30 kg can at 13 m/s, 80 kg at 5 m/s; a wrist flick at 0.3 m/s. 125 core
checks (30 for the grab, 10 of them new or rewritten), the GPU test and
66 Python checks pass. Not run in the game: the rope's behaviour on Havok
bodies at VR frame rates, and the released web following its prop.

Headset check: web a trash can and reel it in; it should hang below the hand,
swing, and not stay up when the hand points ahead. Swing it underhand and let
go; it should fly with the swing, not with a flick. Yank one and let go
mid-flight. Watch the web after a throw: it should go with the prop and fall
away. In the report, the grab samples' `tension` and `trailing` per hand.

## Eye occlusion culling — preceding build, awaiting headset check

The user, after the 10:16 session (`reports/game-vr-20261006-101603.json`):
"performance feels not to too good in vr considering that i alt tabbed and saw
that my cpu and gpu were barely hitting 20% load, with the hard drive at 44%
load. at these numbers i would expect 120 fps stable on headset".

The session: 619 s of samples at 3072 x 3264 per eye, the headset at 90 Hz (XR
frames 89.8 a second). New eye pairs 38.0 a second, submissions 83.1, of which
30.0 repeated an earlier pair; the game log's minute frame rate was 41-64 in VR
minutes. Physics step `dt`: median 20.2 ms, 5th percentile 13.1, 95th 32.4.
Frame cost followed the per-frame render memory: 24-37 MB at 57-71 new pairs a
second, 58-77 MB at 30-38. The render ring never overflowed (512 MB, worst two
frames 155 MB). The texture budget was full in every VR minute (3,837-3,871 of
3,874 MB), the source of the disk reads. The last 40 s have the gate at
`tracking` and `no_camera_commit`: the headset not viewing and the game paused,
as it is while its window is not in front, which fits the alt-tab. Earlier
sessions at 120 Hz (`timing` in the 09:22 Oct 6, 09:36 Oct 5 and 18:47 Oct 4
reports): mean XR frame 13.9-14.0 ms, `xrEndFrame` 13.4-13.5 ms of it, 71
submissions a second. The 10:16 report has no `timing`: the game closed first,
and that path does not read it.

In the game, no headset, `tools/probe_vr_load.py` at the user's save (a rooftop
at night), eyes 3072 x 3264 with Quest 3's lens (54 degrees outward, 44 inward,
48 up and down), views 13 as in VR (`reports/vr-load-3072.json`):

| Phase | Game fps | Busiest thread | Main thread | GPU busy | Render MB/frame |
|---|---|---|---|---|---|
| Game alone | 250 | 90% of a core | 65% | 43% | 3.9 |
| VR views, looking ahead | 65 | 95% | 47% | 66% | 31.1 |
| VR views, turning a full circle | 48 | 96% | 40% | 58% | 45.4 |
| VR views, 60-degree lens | 94 | 95% | 45% | 77% | 18.3 |

Across these, frame time is about 2.4 ms plus 0.41 ms per MB of render
commands. The busiest thread does not log; the main thread (the one logging
`[NxApp]`) does. 7,751 instruction-pointer samples of each
(`reports/vr-load-3072-profile.json`): the busiest was in the game 68% of the
time, the NVIDIA driver (`nvwgf2umx`) 19%, D3D12Core 4%, ntdll 7% (3% waiting),
with no function above 6.5%: per-draw work spread over the render code. The
main thread spent 57% of its samples in one wait (`ntdll+164a00`, from
`1bbf3fa`). The busiest is the render thread, and the main thread waits on it.
Spidy's GPU bridge made no difference (63.6 fps without it, 65.3 with).

Cause, from the executable:

- The offscreen view setup `186c1f0` (Spidy's buffer hook returns at its
  `186c325`) calls `18a0020` with 1 as the sixth argument, which becomes the
  pool initializer `189ce20`'s seventh: skip occlusion. `189ce20` sets the
  view's `+1208` to 1 unconditionally, but creates the occlusion object at
  `+1ba0` (0x8c0 bytes, "ViewContext::InitOcclBufferClasses" and
  "InitOcclBufferPointers"; depth target and coverage texture through
  `17f9310`) only when that argument is 0.
- ModelOcclJob (`17935a0`, created in `1793f5b`) tests each instance against
  the view's object (`17fd5b0` near, `17fdec0` far) only when `+1208` is set and
  `+1ba0` holds an object whose `+184` is clear. A dozen other culling functions
  read the same pair (`16b4990`, `16fad46`, `17164c0`, `173a940`, `173e890`,
  `1795110`, `1796d20`, `191f2d0`, `19f0bf0`, `1a072a1`, `1a0f120`). Without
  an object, every instance in the frustum within draw distance is drawn.
- `189fed0` builds the object for a view that has none, from the view's render
  buffers (`+1640`); it stores the pointer before constructing the object. The
  view's display setup (`1920310`) then queues its occlusion depth sample
  (command 0x9c), and its render job (`19206a0`) runs the occlusion update
  `17f97c0` (`OcclReprojJob`), both guarded by `+1208`; the per-view update
  `189bd30` calls `17f8f80` on the object every frame, on the main thread
  during view maintenance. The engine itself sets and clears `+184` each frame
  (`17fa880`, `17f97c0`), for example while the view's camera flags (`+438` bit
  1) mark a discontinuity.

Change: `stereo_probe.cpp` calls `189fed0` on each eye right after creating it
in view maintenance, before any render job has copied it (view option 16,
entry bytes checked). VR uses it unless started with `-NoEyeOcclusion` (XR
option bit 4). The eye sample's former reserved word reports the object, and
session reports carry `eye_occlusion` (eyes with it) in every sample.

With it (`reports/vr-load-occlusion.json`, no GPU bridge, same save and spot;
"before" from the run above):

| Phase | Before | With eye occlusion | Render MB/frame | GPU busy |
|---|---|---|---|---|
| Looking ahead | 65 fps | 125 fps | 31.1 to 12.8 | 66% to 88% |
| Turning a full circle | 48 | 105 | 45.4 to 14.4 | 58% to 82% |
| 60-degree lens | 94 | 138 | 18.3 to 8.2 | 77% to 79% |

In one process (`reports/vr-load-shots.json`, the GPU bridge reading every pair
back, which costs GPU time), the probe took each eye's object pointer away (to
null, which every reader tests) and put it back: looking ahead 88 fps with it,
56 without. Clearing `+1208` instead stops the eye from rendering at all (0
pairs a second): it is no occlusion switch.

Images, left eye at four headings with the culling, without it, and with it
again (`reports/vr-load-shots-eyes/`): render MB 13.2/30.8, 13.4/51.8,
15.4/59.2 and 12.4/33.9 with/without. Pixels whose largest channel moved by
over 48: with against without 0.003%, 0.34%, 0.17%, 0.18%; with against with
again 0.003%, 0.29%, 0.10%, 0.18%. Mean brightness is equal within 0.4%.
Difference maps put the changes on moving cars and people, birds, an animated
billboard and leaves; no building, prop or light is missing.

Not checked: when the eyes resume after a menu or cutscene, their first frame
tests against the depth of the last frame before they paused. Static geometry
still occludes correctly; something that moved meanwhile could hide an object
for that one frame.

Headset check: play as before. `eye_occlusion` should read 2 while immersive;
compare new pairs a second with the 10:16 session (38) in similar places, and
with `-NoEyeOcclusion`. At 120 Hz, see whether submissions reach 120 a second.

Launcher: the user's first launch of this build ended with `Spidy VR
unavailable: [WinError 24]`, before VR started and without a report.
`CreateToolhelp32Snapshot` fails with ERROR_BAD_LENGTH while the process it
lists is loading or unloading modules, and Windows documents retrying it; the
game loads dozens of DLLs in its first seconds, while the launcher lists its
modules to load Spidy's. Only finding the game and the render memory module
retried. `capture_game_state.checked_snapshot`, which every process and module
listing goes through, now retries that error for up to 2 s; any other error
stays final.

Offline: 120 core checks, the GPU test and 58 Python checks pass (new: the load
probe's eye command passes the native eye check, the eye sample reports the
object, the phase summary's arithmetic, and the module listing retrying only
ERROR_BAD_LENGTH).

## Rays through a city block, web-moved props on the game's physics — preceding build, awaiting headset check

The user, after the 09:22 session (`reports/game-vr-20261006-092258.json`):
"game exited to flatscreen mode mid session. also pulling objects with webs
doesnt work well, they get stuck, behave unrealistically".

Flat screen: the session ended with `Tracked swing input: 2002` after 138 s.
The swing module had faulted with 2103 (`swing.error`), a world ray whose hit
collector held 16 hits, and a faulted swing refuses input, which ends VR. The
controller aim rays (100 m) met 10-14 bodies in their last batches, aimed
across a street at a facade 15 m away; the batch at serial 34230 met 16. That
was not an error: past its capacity the game's collector (vtable 3d0a948, add
1804200) replaces its farthest hit with a nearer one (1803fd0 finds the
farthest), so the 16 it holds are the nearest. A full collector is now
accepted. The game log has no crash; the game was closed normally at 09:27.

Props: six grabs, all on two trash cans in the street. The first was yanked,
caught and thrown. The second, beside the subway entrance, was yanked four
times; the first straight flight at the hand stopped after half a metre
against the railing and lamp post, every yank after it gave up as snagged, and
the web went on commanding 3-5 m/s at a can that did not move. Eye snapshots
0015-0018 show it. Since the fifth build the web set each prop's velocity every step from its
own prediction, and flew a thrown prop along Spidy's path, adopting the game's
velocity only after a change of more than 1 m/s: friction and rotation never
counted. Releasing a yank in flight threw the prop at 2.2 times its speed
toward the player (55 m/s at 63.2 s).

The fix, in [WEB-GRAB.md](WEB-GRAB.md): a command is a law the game's physics
thread evaluates against the prop's actual state; Spidy keeps props it moves,
or let go of, in real time with the game's own contacts until they rest; a yank
is an arc, higher where the lower one is blocked; a hold turns the prop with
the hand; only a held prop is thrown.

Offline: 120 core checks (new: a yank lobbed over a railing, a yank let go in
flight, the laws, turning with the hand, one hand of two letting go), the GPU
test and the Python protocol tests pass.

In the game, no headset, Times Square perch, a throwable prop 43 m below
(`reports/grab-probe-physics-yank.json`): caught after 1.70 s along an arc,
carried 0.3-2.0 m from the hand, thrown at 15.1 m/s; in the following 1.94 s it
kept exactly the launch's horizontal velocity and fell at 10.0 m/s² with the
game at 7.4 times real time. No web lost; hooks restored. A second run, to watch
the landing and slide, stopped: the game cannot run in the background, and the
user was using the PC.

## Walking, jumping and web pulls through the virtual controller — October 6 second build, awaiting headset check

The user, after the 08:39 session (`reports/game-vr-20261006-083949.json`, the
first headset session with the seventh build's virtual controller): "i cant
move in game in vr (i can shoot webs but can pull myself or move or jump) menus
work fine".

The session: the player stood at (-305.31, 32.15, -153.30), the Times Square
perch of the save, for all 2,122 movement samples; the head moved only as far
as the user leaned (0.6 m). Webs attached 25 times. Pulls that wanted lift from
the perch made the takeoff press jump (`takeoff_phase` 1, 2, 3), and it gave up
after 1.4 s, 6 times; the collision flags stayed 0x2060001 and the swing
controlled no step. In the 14:39 session of October 5, at the same perch, the
same takeoff flipped them to 0x2060010 about 0.1 s after its press and the
player rose at 11 m/s. The worker did send the keys (W/A/S/D combinations and
Space in 209 samples). The game log has `XInput controller connected`,
`Gamepads connected: 0 -> 1` and `Gamepad for main player: 8` as VR started;
no earlier session's log has an XInput controller.

In the game, no headset, same build and perch; only the input bridge and the
virtual controller loaded:

| Menus played with | Bridge W 1.5 s / Space 0.35 s | Real Space / S (`SendInput`) | Controller A | Controller left stick |
|---|---|---|---|---|
| Virtual controller (as in VR) | 0 m / 0 m (game read the keys 769 and 362 times) | 0 m / 0 m | 2.87 m jump | full 7.42 m, half 1.55 m in 1.5 s |
| Keyboard (Enter), controller never pressed | Space 3.01 m jump | | | |
| Keyboard, then the controller connected at rest | Space 2.99 m jump | | | |
| Keyboard, then one A on the controller | Space 0 m | 0 m | first A 0 m (swallowed), second 2.55 m | |

The device that last pressed a button plays the player; a connected controller
at rest takes nothing over, and key presses after a controller press did not
switch back. VR menus are played with the virtual controller, so in VR the game
ignored every key the bridge served. Takeoff presses jump for 160 ms: a 160 ms A
jumped the player 1.95 m from the street.

The fix: in VR the virtual controller also carries walking and jumping as the
swing leaves them, besides the bridge's keys. Its left stick is the Touch stick
turned into the stock camera's axes, analog (the keys only have 8 directions at
full speed), zero while the swing owns the body; A is the bridge's Space bit,
with the takeoff's release and press. The worker submits the controller after
the swing's sample, as it does the keys.

Offline: 115 core checks (new: the rig's stick in the camera's axes; gameplay
controller with walking and jumping), the GPU test and 56 Python checks pass.

## Web grab: catching, yanking, carrying and throwing props and thugs — preceding build, awaiting headset check

The user asked: "design and implement a system like the one in the game where
i could web objects and npcs and throw them around". Design, tuning and the full
measurements are in [WEB-GRAB.md](WEB-GRAB.md).

Offline. `spidy_tests.exe` passes 114 checks, 20 of them for the grab (core and
lab); `spidy_graphics_test.exe` passes; the Python protocol tests pass 56,
including the grab telemetry's layout. Simulated tuning: a critically damped
hold spring carried a target 12-18 cm past a hand that moved 1 m in 0.2 s and
stopped; the first-order follower that replaced it, 4-6 cm.

In the game, no headset (`tools/probe_game_grab.py`, Times Square, the player
perched 32 m up; `reports/grab-probe.json`, `grab-probe-yank.json`):

- A throwable prop's bodies are static (flags 0x1, motion 0, category 0x43f
  including the web bit), so before this build a web aimed at one swung from it.
- Freed with SetFreebody and SetMode debris alone, its bodies moved (6.5 m up in
  0.5 s) while the prop was still drawn where it had stood: it had no keyframe
  record. A physics rebuild gives it one, and the game draws it after its body.
  Moved in the steps before the game's first sync, it was drawn 5 m off; three
  still steps after the rebuild fixed that.
- The game steps Havok by a fixed 1/30 s per frame (609a560; TimeScaleSystem
  owns it). At 240 frames a second a released prop fell 32 m in 0.3 s, about 64
  times real gravity. With the ratio measured each step and props flown in real
  time until they rest, a thrown prop crossed 44 m in 2.4 s.
- Reel: 43 m in 4.25 s; caught, carried 0.8-1.6 m from the hand around a 0.3 m
  circle; thrown at 19 m/s. Yank: 43 m in 1.77 s, nearest 1.09 m to the hand,
  thrown at 17 m/s. No web lost in either run; the hooks were restored.
- Earlier runs failed in ways now fixed: a prop's own body cut its web after
  0.3 s; ticking the grab twice per physics step gave half the pull; a slack web
  let the game's 8-times physics drop the prop and the web snapped it back (a
  yo-yo of +-45 m/s); a fixed 1.5 s yank timeout stopped 8.5 m short.

Bots: the components (BotMoverManagerGame +0xdb4 names the bot's MoverStandard)
were read in a live game, and the RequestState slot and the flung reaction's
entry points offline. Not verified in the game: the only bot in free roam was
160 m away and its mover did not step. Pedestrians have no physics and are not
offered.

## The player after a reload, VR from the game's start, menus with the VR controllers — preceding build, awaiting headset check

After the 21:12 session the user reported "game always stays flat", then asked:
"also add ability to navigate menus in vr and start the game in vr from the get
go".

What the report shows (`game-vr-20261005-211234`). 4,330 frames went to the
headset in 68 s, every one the game screen; `tracked` stayed 0, so the gameplay
gate never opened, and no swing or motion sample exists. The eye snapshots show
the session: Peter Parker at F.E.A.S.T. (a story mission), the pause menu, a
loading screen at 31-41 s, then Spider-Man in free roam at Times Square from 46
s, still on the screen. The game log has the load at 21:14:13.

Cause. The launcher found the player once, from outside the process, and
started the input bridge with that hero and actor record. The bridge accepts a
camera commit only when its target is that record. Loading a save, restarting a
checkpoint or switching characters replaces the player's actor and components,
so from the load on no commit could match. Measured in the game without a
headset (`tools/probe_menu_pad.py`, the user's save at Times Square):

- After a checkpoint restart the old hero, record and mover were gone and new
  ones appeared 6.1 s later. With the old target the follow camera committed
  470 times in a second and none matched: the 21:12 session from 46 s.
- Handed the new player (`SpidyRetarget`), 496 of 496 commits matched and the
  gate's conditions held in 99% of samples; after the first load, 500 of 500.
- The Peter section before the load had the player the launcher found, so it
  failed for another reason. The session has no camera data; Peter's camera is
  most likely one the gate does not accept (the commit method also belongs to
  LookCameraMover, LookCameraMoverGame and TurretCameraMover) or one that does
  not commit. XR telemetry now records it.

The game pauses while another window is in front: started from Steam behind the
launching window, its intro sat still for three minutes, drawing black, and it
read no controller. In front, it played on at once.

Changes:

- `game_player.cpp` finds the player in-process from the component registry, as
  `capture_game_state.py` does from outside: exactly one hero (vtable
  `38a93c8`) with a valid actor transform, exactly one HeroMoverManager on the
  same record, and its MoverStandard by handle. A thread keeps it: a live
  player is re-checked every 100 ms, a lost one searched for every 250 ms (7-9
  ms per search). Same hero and record as the outside search, both times.
- The VR worker hands each new player, or none, to the input bridge
  (`SpidyRetarget`, new), the swing module (`SpidySwingRetarget`, which resets
  its solver and retargets the movement module, `SpidyMotionRetarget`), the
  game's web lines (`native_webs::retarget`; rope handles reset with the rope
  manager) and the avatar hiding; the tracking rig and image history start
  over.
- VR starts with the game. The launcher waits for the renderer (30 frames
  through the render memory module, then exactly one direct queue), starts the
  input bridge without a player, and starts the VR worker with none (config v6).
  The GPU bridge, and with it the game screen, starts when the headset is
  focused; the eye views start with the first gameplay, as before the sixth
  build. No 30-second headset deadline: VR waits for the headset. Measured: the
  renderer was ready 8.8 and 10.3 s after launch.
- `game_pad.cpp` serves controller 0 through the game's XInput
  (`XInputGetState` and `XInputGetCapabilities` in whichever of xinput1_4,
  1_3 and 9_1_0 is loaded; the game loads the DLL itself). On the game screen
  the Touch controllers map to Xbox buttons, sticks, triggers and bumpers (the
  grips); in VR only the menu button (Start) and Y (Back) pass. A real
  controller in slot 0 is merged. Once served, the controller stays connected.
  The OpenXR session binds B, X and the left menu button. A held from the screen
  does not jump.
- The launcher brings the game window to the front (`bring_to_front`).
- XR telemetry v6 (648 bytes): `gate` (no_player, bridge_stopped,
  no_camera_commit, other_camera, tracking), `camera_mover` (the last committing
  camera's class), `camera_commits` and `player_commits` (both rising under
  other_camera: a second camera commits after the player's every frame),
  `players`, `player_record`, `pad_buttons`, `pad_installed`, `pad_reads`.

In the game, driven only through the virtual controller: A presses every 2 s
took the game from its intro logos through the title and main menu to a loaded
save (39 s from launch to player with the window brought forward by the
launcher's function; 13 s from the title when brought forward by hand). Start
opened the pause menu, with Xbox prompts; D-pad down and two flicks of the left
stick each moved the selection one item; A and the confirmation restarted the
checkpoint; Back opened the game menu (map, LB/RB tabs, LT/RT zoom); B closed
it. The game read controller 0 about 2,500 times a second while in front, and
not once while behind another window.

Validation: 94/94 core checks (new: the controller mapping in menus, in VR and
without the headset, and bad readings), 26 + 13 + 11 + 5 Python checks (new:
XR telemetry v6 with gate reasons, camera names and commit counts; VR waits for
frames and one queue). Saves: the game rewrote `slot0-s.save` with its own autosave on
Continue; the copy from before is in `reports/backups/saves-20261005-menu-pad/`.

Headset check: start with `Launch Spidy VR.cmd` with the game closed. The
headset should show the intro about ten seconds after launch; play the menus
with the controllers, Continue, and VR should start with gameplay. Pause with
the menu button and resume with A; open the map with Y. Restart a checkpoint or
die once: VR must come back after the load. In the report, `players` counts the
players found; a scene that stays on the screen while you play shows its reason
in `gate` and its camera in `camera_mover`.

## Game screen for menus, hint cards and cutscenes — preceding build

After the 19:36 and 19:40 sessions the user reported: "main menu, cutscenes and
i think stuff like damage indication overlay or spidey senses are causing the
screen to go black (in vr). also everything looks a bit to dark (or too bright)
like an extra ambient occlussion or something is active", and then that the
session's eye snapshots look brighter than the headset.

What the reports show. The VR loop had a picture only while its gameplay gate
was open: a camera commit (`1e1d600`) from the follow or combat camera mover,
less than 0.1 s old, with valid player and camera transforms. Closed, every
frame went out with no layer, which is black. In `game-vr-20261005-193658` it
was closed for 7.9, 12.2, 4.7, 5.8 and 16.2 s; in `-194057` for 4.4, 2.9 and
22.4 s. Aligned with the physics samples by time:

- Four stretches of the 19:36 session ran no physics step at all: the game was
  paused, while render memory shows it still drew about 5 MB a frame of its own
  view. The opening mission's hint cards (Spider-Sense, health) pause the game
  until dismissed, which fits what the user took for spider sense and damage
  overlays.
- The last stretch at 19:36 and the one from 21.5 s at 19:40 ran physics at 240
  steps a second with the player standing: the game's own rate once the eyes
  stopped, in a cutscene.
- 17-21 s at 19:40 ran physics at the usual rate with the player walking
  indoors at 5 m/s: a camera that does not commit.
- The commit method (vtable slot `0xc0`) belongs to FollowCameraMover,
  CombatCameraMover, LookCameraMover, LookCameraMoverGame, TurretCameraMover
  and PerfTestCameraMover; the gate accepts the first two.
  MeleeRelAnimCameraMover, RelativeAnimCameraMover, DeathCameraMover,
  ExteriorCameraMover, VehicleCameraMover, PhotomodeLookCameraMover and the
  cinematic cameras never call it.

In the game without a headset (`tools/probe_game_screen.py`, the user's save
at the Fisk construction site; reports `game-screen-2` to `-5`):

- The eye views kept rendering in the pause menu: 139 new pairs in 2 s, against
  117 in play.
- They carry the HUD but not the pause menu, subtitles, or world markers (enemy
  reticles); the game draws those into its presented frame only. An eye copying
  the stock camera showed the paused scene without the menu or its dimming
  (luma 81 of 255, the window 36).
- Brightness, same camera: eye 81.8, window 83.9 (linear light 0.170 against
  0.174); darks -0.5 to -1.6 levels, midtones -3. Alpha was 255 in every pixel.
  The session snapshots are the exact bytes copied into the
  `R8G8B8A8_UNORM_SRGB` swapchain, so a darker look in the headset arises after
  that handoff, which receives standard sRGB content with opaque alpha. This
  build does not change it.

Changes:

- Without gameplay the last immersive image stays up for 250 ms
  (`GameScreen`), then the headset shows the game's presented frame on a level
  virtual screen 2.5 m ahead (`screenAhead`, heading only), until gameplay
  returns. The eye views get no command meanwhile and stop rendering.
- `native_eye_gpu.cpp` hooks DXGI Present and Present1, whose addresses come
  from a throwaway 64-pixel swapchain on a hidden window. While the screen is
  wanted it copies the back buffer on the game's render queue before the
  present. The swapchain reports a Streamline-wrapped queue, not the one the
  game renders on (first attempt: no copies), so the game's swapchain is
  recognized by its back buffer's device.
- `D3D12Renderer::blit` draws the copy into the left eye image for the quad.
  An sRGB target gets the stored values unchanged; float frames are encoded.
- `game_xr.cpp`: native views start when the headset is focused, not only in
  gameplay; the first-image watchdog counts new images, from each start of
  gameplay; the pose history survives gameplay gaps (cleared on focus loss and
  recentering), and held images age by the frame's display time. XR telemetry
  v4 (592 bytes) adds `presentation` and `screen_submitted`. The flat-screen
  toggle places its screen level too.
- The GPU capture's readback range overran the buffer by the last row's pitch
  padding when a row was not a multiple of 256 bytes (1290 pixels here); every
  freeze failed with 3904. VR sizes were multiples and unaffected.

Validation: 93/93 core checks (new: the screen's hold, entry, return to
gameplay and clock reversal; level placement at five pitches, with roll), the
GPU test at 1536 x 1536 (new: 8-bit, 10-bit and float frames drawn into an
sRGB typeless target within 1 level; debug layer clean), and 53/53 Python
checks. In the game (`game-screen-5`): 118, 137 and 123 copies of the
presented frame in 2-second phases of play, pause menu and resumed play
(R8G8B8A8_UNORM, 1290 x 540), each identical to the window to the byte; all
entries restored. The draw into the headset's swapchain and the quad were not
run in the game: they need the headset. The game re-saved its autosave and
preferences on each Continue, as in the user's sessions;
`reports/backups/saves-20261005-game-screen/` holds the files from before.

Headset check: open the pause menu and the map, dismiss a hint card, watch a
cutscene, die once. Each should appear on a screen ahead, and VR resume with
play. The report's `presentation` shows `game_screen` for those stretches and
`screen_submitted` counts the frames. The screen has the desktop window's
resolution (1290 x 540 with the launcher's small window); `-FullDesktopView`
sharpens it at the cost of rendering that view at full size in VR.

## Render memory ring, held images, memory headroom — preceding build

After the 14:39 session with the fourth build the user reported: "webs are
fixed! still got the artifact i got before - black flickering and quads all
across my vision, objects, building and ground dissapearing, geometry randomly
exploding into impossible polygons", and then "the game crashed on the second
spike of those". The session's report is `reports/game-vr-20261005-143902.json`,
with the game's log and the left-eye images beside it.

What the report shows. In three stretches (220-260 s, 350-390 s, and 461-495 s
into the session, after which the game crashed) the game copied eye render jobs
and never rendered them: 60-87 a second, 3,047 of the session's 22,379 copies.
In those stretches new eye pairs arrived 11-20 times a second while the game's
own frame rate rose to 48-56, and Spidy ended 2-19 headset frames a second
without an image. The second stretch has none of those; the user was standing
still. In the session's 329 s of driven flight no physics step ran without its
movement command, so the lease described in the next section was not involved.

The cause is the game's frame allocator, "RenderAlloc" (the object at `7938880`):

- `1872d90` creates it (its only call is `188a19c`): a 128 MB ring, reserved and
  committed at once. `1872930` (316 call sites) and `1872860` (35) allocate from
  it with an interlocked add. `1872b90` (only call `187e623`, inside `187e320`)
  rolls it over at the end of each frame. The frame that just ended stays
  allocated while the render thread draws it, so a frame may use the ring minus
  what the frame before it used.
- A request that does not fit sets the overflow flag (`+0x41`). `1872930` then
  returns null. `1872860` falls back to the heap and queues the block for
  release two frames later (`1c5dab0`; flushed by `1c5e900` from `175a1a4`).
  With a null result `19206a0` skips the view and `1793be0` the actor's job.
- The stock game uses 6-8 MB a frame. With the two eye views and the game view
  moved to the head, frames used 47-64 MB swinging at Times Square with
  512-pixel eyes and the same with 1536-pixel eyes: the memory holds draw lists
  and commands, not pixels. Two such frames are 119-127 MB of the 128.
- Refused requests still advance the usage counter. One sampled overflow frame
  had 526 MB requested of the 128 MB ring, and the game's own two-frame record
  reached 1,223 MB.

Read from the code and not observed running: of the 316 call sites of
`1872930`, a heuristic pass found the result tested before use at 289; two of
the others pass it straight to `memset` (`1857a9e`, `185b158`). The rollover
takes the usage counters as the extent of the frame, so after a frame with that
much refused it leaves the next frame no region at all, and computes the region
after that from a null pointer (`first - ring` at `1872ca4`), which gives a size
unrelated to the ring.

Why the ring is replaced where it is created and nowhere else. Render commands
refer to frame memory by 32-bit offsets from the ring's base (encoded at
`179db08`, decoded at `177059c` and `1770a13`). A ring moved at a rollover
crashed the game in the decoder (`17706db`). The address space for 4-6 GB around
the ring had no free gap of 16 MB, so the ring cannot grow in place, and no
second arena fits within reach of an offset.

Changes:

- `spidy_render_memory.dll` (`src/native_render_memory.cpp`) hooks `1872d90`.
  After the game's own creation, and only if the allocator is exactly as
  creation leaves it, the hook allocates a 512 MB ring, points the allocator at
  it, and releases the game's. A hook on `1872b90` counts frames, frames with a
  refused request, and the largest frame and pair of frames. Signature checks
  cover both functions and their call sites. `Data.status` says which ring the
  game is on. The ring outlives the session that installed it, and a later
  session on the same game recognizes it.
- `tools/vr_launcher.py` looks for the game process every 50 ms and loads that
  module as soon as the process exists, before it opens the game for anything
  else. The game creates the ring 3.4 s after its process is first seen
  (kernel32 and user32 are loaded at that point, d3d12 0.4 s later, the GPU
  driver 1.5 s later).
  Loading is retried while the new process still refuses module listings. Run
  as a script, the launcher starts the game the same way for checks without a
  headset.
- `run_game_vr.py` records `render_memory` in the report, samples
  `render_frame_mb` and `render_overflow_frames`, prints which ring the game is
  on, and warns when it is the game's own (a game that was already running).
- Images. `XrRuntime::frameStereo` ends a frame with no layer when the draw
  callback has no image. `copyLatest` refused a staged pair older than 150 ms,
  and `NativeEyeHistory::find` refused its pose, so at 11-20 new pairs a second
  the headset went black between pairs. Both now hold the last pair for
  1000 ms (`imageHoldMs`). Controls need a new image within 500 ms
  (`controlHoldMs`; before, any submitted image within 250 ms), and the
  three-second stall watchdog counts from the last new image.
- Memory headroom. `run_game_vr.py` warns before it starts the game when
  Windows can promise less than a session takes (19 GB, less what a game that
  is already running holds), and records `free_commit_mb` at the start, with
  every sample, and at its lowest.

In-game checks without a headset, RTX 5090. Each run used a freshly started
game, which was closed afterwards; all hook entries were restored and the saves
stayed byte-identical to the copy taken before. "Release stress" is a scratch
variant of `probe_web_frames.py`, not in the repository, in which both hands
shoot and release game webs every 0.9 s during the swing. All runs have three
views (`--views 13`); the eye images are 90° wide unless noted. Frame counts
start when the module does, so they include menus and loading.

| Report | Ring | Test | Frames that did not fit | Eye jobs lost | Most used by two frames |
|---|---|---|---|---|---|
| `web-frames-6` | game's, 128 MB | `probe_web_frames.py` | 0 of 14,085 | 0 of 345 | 119.4 MB |
| `web-release-7` | game's | release stress, 110° eyes, 8 s swing | 0 of 14,183 | 0 | 127.2 MB |
| `web-release-8` | game's | release stress, 120° eyes, 8 s swing | 13 of 13,609 | 24 of 395 | over the ring |
| `web-frames-5` | Spidy's, 512 MB | `probe_web_frames.py` | 0 of 23,289 | 0 of 345 | 121.6 MB |
| `web-frames-7` | Spidy's, final build | `probe_web_frames.py` | 0 of 14,817 | 0 of 369 | 120.0 MB |
| `web-release-10` | Spidy's, final build | release stress, 120° eyes, 6 s swing | 0 of 22,666 | 0 of 305 | 136.6 MB |

Two earlier runs of the 110° stress on the game's ring, before the allocator
was instrumented, lost 15 and 8 eye jobs (`web-release-5`, and `-6` with
1536-pixel eyes); `web-release-7` is the same test fitting with under 1 MB to
spare. A first run of the 120° stress on Spidy's ring (`web-release-9`) was
stopped after 5.7 s by the test's own guard on Windows commit, with 138.3 MB as
the most for two frames and no job lost. In the final build, a second start of
the module in the same game reported Spidy's ring, 512 MB, with its counters
restarted, and stopping it restored both entries.

The crash. The game's log ends with an access violation at `0x1199cfbb`. The
log prints the low half of the address only; with the module at `7ff70fd40000`
that is `Spider-Man.exe+1c5cfbb`. No minidump was written. `1c5cf90` takes a
block from a pool's free list, and the faulting instruction reads the block's
link to the next (`mov rcx, [rax]`); its callers are the game's small-block heap
(`1c63770`). So the head of a free list was not a readable address: the heap's
bookkeeping had been overwritten, or memory it counted on was not there. Two
conditions of that session can lead there, and which one did is not established:

- Render memory overflow, above. The game is not built to run in that state.
- Windows had no memory left to promise. The game's log gives 14.0 GB available
  of a 65.5 GB commit limit when the game started. Its once-a-minute memory line
  shows the process holding 14,968 MB a minute in, 16,721 MB a minute later, and
  17,112 MB at the end. At the crash the limit was 67.5 GB (Windows had enlarged
  the page file) with 0.6 GB available. In a separate experiment a job limit on
  the game's commit made it log `CreateHeap ... failed` (`0x8007000e`) and
  freeze (`reports/memory-limit-1.json`); the user's log has no such line, so no
  GPU allocation failed outright, which does not rule out a failed allocation
  elsewhere. On the test PC other programs held about 51 GB of commit with half
  of the 61.6 GB of RAM free, and the page file was 4 GB.

Validation: 91/91 core checks (new: the allocator's fields as creation leaves
them, as replaced, and as a later session finds them; a recent new image as the
condition for control; the hold boundaries of the pose history), the GPU test
at 1536 x 1536, and 53/53 Python checks (new: the module is loaded once per
process before the game is opened, retried while the process starts, and not
retried after its own refusal; the render memory reader; the memory warning).

Not verified: everything in the headset. Whether the break-up is gone; how a
held image looks while the game is slow; whether 512 MB covers every scene
(the report's `render_memory` gives the most two frames used and the frames
that did not fit); what caused the crash. One left-eye image of the 14:39
session (`0168`) shows a black polygon 0.72 s after a web release; the release
stress did not reproduce it in 60 releases, and it is unexplained. A session
attached to a game that is already running stays on the game's ring: it only
warns.

## Eyes placed before the frame's render jobs, reel momentum — preceding build, webs confirmed by the user

After a session with the third build at 12:05 the user reported: "offsets still
happens and game began breaking up after a few minutes of gameplay". That session
left no report. `run_game_vr.py` called `SpidyXrKeepAlive` while the game was
closing; the call raised `OSError`, and the launcher exited before writing.

Why the web still started away from the hand. The frame function `175a060` runs
in this order (disassembly; call sites in parentheses):

1. `18a13c0` (`175a42f`): for every pool view and the eight offscreen slots,
   `189ee00` copies the view's descriptor `+0` to `+0x530` and the old `+0x530`
   to `+0xa60`. Previous camera = current camera.
2. The gameplay update phases: hero movement, the hero's rope update
   (`95f6c0` → `676dd0`), and the stock camera's submit (`1646e10` → `1899ab0`).
3. `1920240` (`175aaa9`): for each offscreen slot of the manager at `7a34dd0`,
   `186d050` → `19206a0` → `19223e0` copies the whole view (0x1f70 bytes) into
   its render job. The pool views follow.
4. `187e320` (`175acb4`) → view maintenance `18a0bb0`.

Spidy submitted the eye poses in its maintenance hook, step 4, after step 3 had
copied the views. Two consequences:

- Every eye image was rendered from the previous frame's head and player
  position. The game's web was built in step 2 from the current frame's hand,
  so in the eye it started one frame of travel ahead of the hand: 0.4-0.7 m at
  20-32 m/s and 15-33 ms frames, and different in every frame.
- At the next step 1 the pose became its own previous camera. `186e520` fills
  the per-view shader constants `GlobalViewportCBuffer` from that history
  (`m_PrevCamWorldToClipMat` at `+0x80`, `m_PrevCameraPos` at `+0xc0` from the
  previous descriptor at view `+0x530`), so for both eyes the previous camera
  always equalled the current one. What that did to the image (anti-aliasing
  history, motion blur, reflections) was not measured.

Changes:

- `stereo_probe.cpp`: a hook on `1920240` places the eyes (`placeEyes`: latch the
  pose command, anchor it to this frame's hero sample, set lens and pose, submit)
  before the original copies the views. Maintenance only creates and retires the
  views. `SpidyEyePlacement(1)` restores the old placement for comparison.
  Signature checks cover `1920240`, its call site `175aaa9`, and `189ee00`.
- Eye frame telemetry v4 counts, per eye job copy, whether the pose was placed in
  that frame (`same_frame_poses` / `late_frame_poses`) and whether the previous
  camera differs from the current one (`history_moved` / `history_still`).
  Appearance v5 adds `web_hand_gap_*`: the distance, in each presented image,
  between the wrist the overlay draws and the game rope's first point.
- `Swing`: a taut rope that is being reeled now carries the body's velocity at
  the winch rate; the pull used to move only the position. With two taut ropes
  the pass over both keeps what each has pulled and lets a rope hand back a
  surplus (accumulated impulses); kept, the surplus pushed the body sideways
  every step. The speed two ropes carry together is followed exactly up to a
  right angle between them and held at that value beyond it, because it grows
  without bound as they come to oppose each other. Winches that reach the span
  between their anchors now share the remaining length instead of the left
  hand's taking it. A rope within 5 mm of taut is treated as taut, so float
  rounding no longer decides which of two ropes acts on the velocity.
- `Swing::settleStep`, called by `game_swing.cpp`: a prediction covers a native
  step whose length is unknown until the next observation. The rope was wound for
  the previous step's length while the body travelled for the real one. The rope
  is now wound for the difference, at the rate the winch really ran (none at the
  shortest length).
- `game_swing.cpp`: the movement command's lease is 150 ms instead of 50 ms. The
  command is submitted in one frame and applied by MoverStandard's prequery in
  the next, so with 50 ms (47-62 ms in practice, the lease is measured with the
  16 ms tick count) any slower frame ran its step without the command. The game
  then moves the body at the fall speed it derives from the time spent airborne.
  In `reports/web-frames-3.json` two such steps occurred 0.9 and 1.1 s after
  takeoff, at 27.5 and 32.6 m/s downward: the body dropped 0.9 m and 1.4 m in
  one step, the solver pulled it back at the 32 m/s cap, and the velocity
  alternated for about five steps. The morning headset sessions each contain
  one such place in their retained samples (10:25: one step at 20.6 m/s down in
  321 s of driven flight; 09:36: two steps in 156 s, one sampled at 42 m/s
  down, speed 16 to 43 m/s). 150 ms covers two steps of the longest length the solver accepts
  (50 ms), because one missed observation is tolerated.
- `run_game_vr.py`: the report is written however the session ends (game closed,
  Ctrl+C, console closed, a failed stop). It copies the game's log to
  `<report>-game.log` and parses its memory lines into `game_memory`. The left
  eye as presented is copied without blocking (`D3D12Renderer::capture` /
  `captured`, `EyeSnapshotSchedule`) and saved to `<report>-eyes/`.
- `probe_game_swing.flight_summary` reports `steps_without_command`: native
  steps inside controlled flight that ran without the command.

In-game checks, no headset, RTX 5090, game closed afterwards and all hook entries
restored:

- `probe_eye_frames.py` at the main menu (`reports/eye-frames-menu.json`). Old
  placement: 658 eye job copies, 0 with a pose placed in that frame, previous
  camera equal to the current one in all 658. New placement: 657 of 657 placed in
  the frame, and the previous camera differed in 193 copies, exactly the 193 pose
  commands sent.
- `probe_web_frames.py` in free roam, final build (`reports/web-frames-4.json`):
  one 100 m web, reeled, speed cap 32 m/s, gravity 6, a game web held 0.6 m in
  front of a sideways eye camera. Old placement: the web's first point was a mean
  0.455 m (max 0.605 m) from the expected point over 90 frames at a mean
  19.6 m/s; mean frame travel was 0.453 m. New placement: mean 13 µm, max 23 µm
  over 218 frames at a mean 31.6 m/s (0.60 m of travel per frame). In the saved
  left-eye images the web ends on the marker with the new placement and misses it
  with the old one. `web_start_error` and `hero_lag` stayed 0.0 m.
- Flight in the same run, per native step: the second difference of achieved
  velocity (an isolated jolt) had a median of 0.006 m/s and a 90th percentile of
  0.029 m/s; the run before the reel changes (`reports/web-frames-1.json`) had
  0.111 and 0.524, with 1.3-2.8 m/s jolts where the frame time moved by about a
  millisecond, and lost 16.4 m/s in the step that released the web. The final
  build lost nothing at release. Starting the reel is still a 16 m/s step, as
  designed.
- The lease: one driven step of that run took 62 ms of wall time, longer than
  the old lease, and no step ran without the command. The run before the change
  (`web-frames-3.json`, the same build otherwise) had the two dropped steps
  described above.
- The saves were byte-identical to the copy taken before the probes.

Validation: 90/90 core checks (new: release while reeling keeps the winch speed;
two reeling webs carry the body at the geometric speed and keep it on release;
two winches never exceed 1.5 times the reel speed or teleport the body from any
of six starting heights and three reel combinations; a rope at its shortest
length ignores frame time corrections; reeling stays smooth with 16-28 ms frame
times in a simulated one-step-late mover; eye snapshot schedule and projection),
the GPU test including the nonblocking eye copy, and 48/48 Python checks.

Not verified: everything in the headset. In particular whether doubled webs are
gone (the repeated-frame doubling of the whole scene at 36-50 new pairs per
second remains), how the corrected previous camera changes the image, and how
reeling with momentum feels. "The game began breaking up" is not explained with
certainty. The expired movement lease fits it: a metre-sized jerk of the whole
world on every frame slower than about 50 ms in mid-swing, more often as the
game slows down or hitches. But no report exists for that session, so the user's
description has to confirm it. The 12:05 game log, read before the game was
started again, showed about six minutes of VR at 44-51 frames per second,
texture usage at its 3874 MB budget, a normal quit, and no crash, GPU, or
low-memory event in the Windows logs. Windows had about 15 GB of commit left
with the game closed, and the game commits 15-17 GB in a long session, which is
another possible cause but not an observed one. In the next report,
`flight_summary`-style counting of steps without the command (compare `steps`
with `controlled` between driven `motion_samples`), `game_memory`, and the eye
images are there to settle it.

Outcome, from the 14:39 session with this build: the user confirmed the webs
fixed. The break-up continued, and the movement lease was not its cause; that
session had no step without the command in 329 s of driven flight. The cause
was the game's render memory, in the section above.

## Grip-only webs, straight game webs, in-flight steering — preceding build, web offset remained

The user reported from `reports/game-vr-20261005-102512.json` (874 s, second
build): web starts still offset at high speed and seen double, more after pressing
the trigger mid-swing; webs sometimes shooting again and again mid-swing without
releasing buttons; and after a physical yank, the whole world shaking until
landing. They asked for webs to shoot on a grip press instead of trigger plus
grip.

The report's swing, motion, and XR sample lists are separate 12,000-entry queues
appended at different rates, so their indices do not line up. Align them by the
shared command serial (XR `serial`, swing `serial`), and convert swing/motion
`qpc` at 10 MHz.

Session findings:

- Shake after a yank. `native_movement.cpp` applies the leased command at each
  MoverStandard prequery, and `game_swing.cpp` observes that prequery's start
  position and the previous step's achieved velocity, then submits the next
  command. That command takes effect one step later. Steering from the observed
  state made each command a function of the command two steps earlier, so even
  and odd steps formed two independent trajectories. Every owned flight showed
  them: achieved velocity alternated between, for example, (10.0, y, 28.5) and
  (10.1, y + 1.9, 27.6) m/s, swapping whenever the sampler skipped a step. Each
  trajectory received gravity only every other step and through the step-average
  velocity, so vertical speed fell at 5-6 m/s² with gravity configured at 18. A
  yank adds its impulse in one solve, so it reached only one trajectory, and
  nothing re-coupled them in free flight until landing returned control to the
  game. Simulating that pipeline with a yank reproduces it: vertical speed
  alternates between about 11.5 and 0.5 m/s on consecutive steps, and gravity
  acts at a third of its setting.
- Web start offset. `hero_lag` stayed 0.0 m over 41,378 frames: the render
  transform did not move between the rope update and view maintenance, so the
  second build's shared hero sample changed nothing. `web_start_error` (rope `+1c`
  to the requested start) began near 0 at each rope creation and grew to
  0.42-0.45 m within about a second, also while standing. `679dc0` builds a point list at
  rope `+1c` (count `+6ac`). When Grip (`7d78`) is non-zero and finite, it passes
  two hand points to `HeroRopeManager` `vtable+0x78` (`95f9c0`), which for swing
  ropes (type 2) appends `lerp(Start, Grip2, t)` and `lerp(Start, Grip, t)` as
  points 8-9 and rebuilds points 0-7 as a tail from point 8 along minus the
  hand's world velocity (`7df8`, from `95f6c0`) and down, each segment
  length-limited. The tube runs through the tail, the hand, then the main line to
  the anchor (`+720`). At swing speed the tail streams about 0.45 m behind the
  wrist; in the headset it read as an offset start and a second strand.
- Repeated shots. With trigger and grip held, `Swing::inputs` re-shot every
  0.15 s after any automatic release or miss. The line from the body to the
  anchor releases a web at the first hit anywhere along it, and swinging along a
  facade it grazed ledges and sills next to the anchor. The swing samples hold
  144 attachments, 22 obstruction releases, and 3 misses; 16 of the 22 releases
  were followed by a new attachment within 0.5 s, 8 of them before the next
  20 Hz sample.
- The second build's eye job reclamation held: 41,467 generations, 1,026 dropped
  copies reclaimed, no presentation stall. New pairs still arrived at 44.4 per
  second, and 36.5% of the 70 frames per second sent to the headset were repeats.

Changes:

- `include/spidy/game_swing.hpp` `InFlightStep`, used by `game_swing.cpp`: the
  solver starts from the observed position plus the displacement of the command
  in flight, at the solver's own end velocity for that command. When the next
  observation shows a command lost speed to collision, the motion into that
  surface is projected out once, which cannot double-count one contact. Takeoff
  still follows measured native progress.
- `game_swing::physicsConfig`: gravity 6 and air acceleration 4 m/s², the values
  the earlier builds applied in practice, so the swing feel the user approved is
  kept. Yanks now apply their full configured strength.
- `Swing`: a grip press (above 0.65, released below 0.35) shoots; the trigger
  only reels, after a release if it was held at the attachment. There is no
  retry while held. A hit within max(1.5 m, a tenth of the rope) of the anchor,
  or within the body radius, is not an obstruction, for shooting or holding. A
  held web releases only after 0.15 s of continuous obstruction.
- `native_webs.cpp`: Grip is written as (0,0,0), so `679dc0` keeps one hand
  point, skips the hero override, and runs the rope straight from the tracked
  Start to the anchor. Grip2 and End are written as Start.
- OpenXR action names now read "Reel web" (trigger) and "Shoot and hold web"
  (grip); the lab's console help matches.

Validation: 83/83 core checks (the held-gesture retry test now asserts the
opposite; new checks cover grip-only shooting, no re-fire while held, a ledge
beside the anchor, a passing versus a lasting wall, a simulated one-step-late
mover with a yank followed by free flight at configured gravity with no
alternation, and a wall contact removed exactly once), the GPU test, and 39/39
Python checks. The tail removal follows from the disassembly; that the rope now
starts at the wrist, and how the new controls and corrected gravity feel, need
the headset. In the next report, `web_start_error` should stay near 0 m.

## Small desktop window for VR launches — previous build

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

## Swing response and fall handoff — preceding build

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

The camera observer sampled the supported Steam executable in a flat-screen
scene.

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
