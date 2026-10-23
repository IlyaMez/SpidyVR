# Spidy changelog

Every build, newest first, with what changed, why, and what was measured.
Per-check results are in [VALIDATION.md](VALIDATION.md).

**Unreleased: optional WEB SLINGSHOT (iplusdvr).** Off by default, under
SWINGING. When on, attaching a web on the ground no longer jumps automatically.
Stand still, shoot a separate web from each hand at two solid surfaces, draw
both hands back at least 16 cm and hold. Both controllers vibrate as the launch
charges. Release both web buttons together to launch toward the anchors'
midpoint, within the chosen swing speed limit. Pushing the hands forward,
walking, jumping, losing tracking or switching it off cancels the charge.
This adds no web bridges, enemy tethers or rope climbing.

**Latest build (October 21, fourth): RECORDING FOV, optional - pull request
21 (iplusdvr).** You: "review and fix whats needed if needed", then "it should
be an optional feature".

- **RECORDING FOV, the last row under DISPLAY, OFF unless you pick 130, 140
  or 150 degrees.** Off, the game window mirrors your left eye as it has
  since 0.3.5, Spidy's marks and all. With an angle, the game renders its own
  view again for the window, at least that wide across and of the window's
  shape: a wider picture to record, without Spidy's marks (they stay in the
  headset), every frame whatever the monitor frame rate, at a cost in frame
  rate. That view is the engine's active view, which the eyes rely on for
  culling, occlusion and the far buildings' full models, so its lens never
  covers less than the head's lens covering both eyes: a Quest 3 in a 16:9
  window needs 128.5 degrees. While it is on, far buildings draw their full
  models within the game's own 145 m, not the half as far again they get
  while the game's view rests.
- **Settings profiles leave it out** (October 20, eighth, below), as they
  do the monitor and the other graphics options: it suits one PC, and a
  shared profile would cost someone else frame rate. Using a profile keeps
  your `recording_fov` line in `spidy_vr.ini` as it was.
- **In the launcher too:** Options, the Monitor card, "Recording FOV",
  with the tab's own choices on the same `spidy_vr.ini` line (the build
  before put every other SPIDY VR setting in the launcher). Reset options
  turns it off.
- **Measured by the author** in a Quest 3 (3032 x 3248 an eye), standing in
  one place 30 s each: 117.1 newly rendered frames a second off, 84.5 at 130
  degrees, 112.3 off again, so about 3 ms a frame while it is on. They saw
  nothing missing at the headset's edges or in the far buildings.
- **What the review changed.** The eyes' shared shadows stay fitted to the
  head's lens: the pull request fitted them to the recording lens, which
  would have coarsened the headset's shadows while recording (the game's view
  fits cascades of its own when it renders). The view's first submit of the
  frame, which sets up its culling, takes the recording lens too; only its
  last one had it. The choices are 130, 140 and 150 degrees: the pull
  request's 80 to 120 all showed the same 128.5 on a Quest 3. The code and
  `spidy_vr.ini` call it what the tab does (`recording_fov.hpp`,
  `recording_fov=`; it was spectator), the help is in the tab's words, and
  the angles carry their degree signs. The tab's layout test checks the row
  where it is instead of leaving it out, and the docs are here, in
  PLAYERS.md and DEVELOPMENT.md rather than a file of their own. Kept from
  the pull request: the menu's record of changed settings has 64 bits, since
  the body haptics' switches took the settings past 31, where a 32-bit shift
  is undefined.
- **Checked:** core tests (new: the recording FOV's choices, range and line
  in the tab's file, its lens holding lopsided eyes at four window shapes and
  a Quest 3's at 16:9), the launcher's, all the protocol suites. The author's
  headset check ran their version; the review's lens changes have not run in
  the game, and the launcher's row was not looked at on screen.

**Earlier build (October 21, third): every SPIDY VR setting in the launcher's
options.** You: "why doesnt the new launcher include all the realstic flips
stuff?", then "yeah lets add all".

- **Why they were missing:** the settings that launcher.ini has no key for
  (REALISTIC FLIPS and its nine rows, HAND WALL CLIMBING, WALL CAMERA
  DISTANCE, NEAR CLIP, UI STYLE, the four body haptics switches) were only
  in the game's SPIDY VR tab, which keeps them in `%APPDATA%\Spidy\spidy_vr.ini`.
  The facelift laid out the options the launcher had; none of these were
  among them.
- **Now in Options:** "Walls and flips" is two cards. Walls: wall running,
  hand wall climbing (experimental), wall camera distance. Flips: flips,
  flip speed, realistic flips (experimental), and while it is on its nine
  rows under it (rotation strength, max rotation speed, minimum flight
  speed, swing momentum, rotation response, hand momentum, angular drag,
  minimum hand speed, air rotation brake). Body and haptics: web, wind,
  combat and movement haptics under Body haptics while it plays. Picture:
  near clip. Game screen and HUD: UI style.
- **Same file, same steps:** the launcher changes them in `spidy_vr.ini`,
  where the game reads them as VR starts, and its lists are the tab's own
  choices (written "Very slow", "20 cm", "90°/s"), so the two never offer
  different steps. Reset options puts them back to Spidy's own too. Profiles
  held them already.
- **Checked:** the launcher and its tests built in a scratch folder, no
  warnings; 30 launcher tests pass. In a scratch copy beside your launcher:
  every new row showing your `spidy_vr.ini` (hand wall climbing on, swing
  momentum off, hand momentum 75%, angular drag Very slow, minimum hand
  speed 3 m/s); switching realistic flips on wrote `realistic_flips=1` and
  showed the nine rows; Angular drag's list (Fast to Very slow) and picking
  Medium wrote `angular_drag_ms=1500`; Reset options wrote the defaults.
  launcher.ini and `spidy_vr.ini` put back byte for byte after.
- **Not checked:** a VR session started after changing them in the
  launcher.

**Earlier build (October 21, second): the launcher in Spidy's print, laid out
again.** You: "lets give the launcher a facelift with out style, better layout
and UX", then "we have a control map visual, maybe use it?" and "i feel like
the ui is a bit much with the colors and contrast".

- **The look:** the print of Spidy's videos and README, toned down after your
  third note: a dark ground and dark cards; Barlow Condensed capitals for
  names, buttons and tabs; the narrator's Comic Neue for the one line that says
  what to do next; the name SPIDY and white headlines misprinted (a cyan ghost
  left, a magenta one right); the release as an issue box (`No. 0.4.0`,
  `FREE`); halftone dots in the corners. Colour is kept small: red is START VR
  and the tab you are on, yellow what needs a look, cyan what is fine, violet
  a switch that is on. The fonts are compiled into the launcher (SIL Open Font
  License, in `docs/licenses` and THIRD_PARTY_NOTICES).
- **Play:** a panel that says what the session is doing in a headline (READY,
  STARTING THE GAME, IN VR · 12:34, VR COULD NOT START), what to do next in the
  narrator's line, and the session's three steps (headset, game, play), ticked
  off from the log as they pass and the one that failed marked; START VR at
  its foot, with the profile VR starts with. When VR fails, the error and what
  to do about it (copy the log, ask on the Discord, restart as administrator,
  close the game and start VR, force stop) are in the panel. This PC and the
  session log on the right, with shorter words while nothing has run.
- **Options have their own tab:** the Profile card (and Reset options), then
  the options in seven cards in two columns: webs, moving, walls and flips,
  body and haptics, picture, game screen and HUD, monitor. The two sliders
  are gauges.
- **Controls:** the README's picture of the controls first, then every control
  in words. The picture was re-shot: its issue box says PC VR, not 0.3.7, and
  its red box what is true since the prompts read by name: the game shows a
  button? Press that one; its LT, LB, RT, RB are your triggers. The README
  shows the same file (its alt text says that too).
- **Ko-fi, highlighted** (you: "lets highlight kofi a bit more and maybe change
  the copy", and you chose "Buy me a coffee"): the header's Ko-fi button is
  yellow-keylined, "♥ BUY ME A COFFEE / ON KO-FI", beside a quiet "JOIN THE
  DISCORD". After a session that got into VR the panel adds "Had fun? Buy me a
  coffee: it pays for the next update." and the same button. About's Ko-fi card
  (yellow-keylined) reads "SPIDY IS FREE. THE AI TOKENS ARE NOT. / BUY ME A
  COFFEE / Your tip pays for updates. And the next full VR conversion.", beside
  the Discord card.
- **The README's launcher picture** is the new Play tab.
- **Checked:** the launcher and its tests built in a scratch folder, `/W4`
  clean; 30 launcher tests. Every tab looked at on this PC (150 %), in a
  scratch copy run beside your own launcher: the profile menu, the Save as a
  profile dialog, the controls picture drawn pixel for pixel, the smallest
  window (980 x 700). The states a session brings, in a scratch copy with the
  session faked: in VR, stopping, ended, failed, needs administrator, close the
  game first, could not start, not ready, the update offer. launcher.ini and
  `spidy_vr.ini` the same before and after.
- **Not checked:** a VR session with it, another Windows scaling than 150 %,
  Windows 10, a real update, tooltips.

**Earlier build (October 21, first): a tech look for Spidy's marks, as the
game's own HUD.** You: "lets make an optional alternative set of our custom
ingame vr ui in more scifi techy style like the based game ui".

- **Where:** UI STYLE in Settings > SPIDY VR, under DISPLAY after the HUD:
  COMIC (the printed marks, as before, and still the default) or TECH. It
  changes at once. Like NEAR CLIP it is the tab's own: Spidy keeps it in
  `%APPDATA%\Spidy\spidy_vr.ini` (`ui_style=0` or `1`), and settings
  profiles carry it.
- **The look**, taken from the game's own pause menu, Settings and HUD:
  lines a pixel or two wide in its pale line white, cyan for what is picked
  or answers now, amber for what is equipped or finishes, red for danger,
  translucent blue panels edged with thin rules, the menus' corner brackets
  and diamonds, lettering upright in square strokes. Every shape has a soft
  dark halo under it, so it reads on the sky and on a lit wall, and the lit
  ones a faint glow. Nothing holds a pose: the marks ease in, flicker on as
  the game's displays do, turn and fade.
- **Each mark**, in the same place, at the same size and for the same time
  as its comic one:
  - aim markers: a scope's four arcs round a diamond with a tick out at each
    gap (the left hand's cyan-blue, the right hand's amber-orange, as
    before); faint dashes for open air; an open red cross for a miss; for a
    target three ticked arcs with claws round a dashed ring turning the
    other way, closing in as before;
  - the mark over the enemy: four arcs; locked, cyan, with a pointer in at
    each side and a dashed ring turning; a call, three chevrons the way the
    fist must go, a light running along them, over a ring of 24 segments
    that runs down; the finisher's amber inside a red ring with six
    diamonds;
  - a landed blow: a hit marker's four strokes springing out and a ring
    going out (white; amber for a hard blow; red with an amber hexagon
    turning for a finisher; cyan ticks for a called one), and the count on
    a callout, "X3" under the game's COMBO;
  - a knockout: a red reticle closing on where he stood, a cross in it, and
    a "KO" tag drawn out beside it;
  - Spider-Sense: five glowing wavy lines over his head, as the game draws
    its own round the hero's, cyan, red or amber for what to do, inside a
    ring of dashes closing on the blow, amber with pointers in at its last
    notch; a counter throws the lines out with a ring. In the sightline:
    the same mark, chevrons pointing to him, and three arcs pinging at the
    rim of the view;
  - the gadget wheel: a translucent blue ring of eight places with line
    pictures, ticked round its edge; the lit place's wedge cyan inside the
    menus' corner brackets; its name between two ruled lines and its
    charges in the middle; the equipped gadget amber;
  - slow motion's meter: a watch face over the wrist, the focus in 20
    segments round an hourglass whose sand runs down, a sweep going round
    in slow motion, red for a refused press;
  - the calibration panel: one of the game's menu panels, its title on a
    tab, the hint amber, the hold's bar in segments, the controllers' rings
    in segments, "(B) SKIP" as the game's prompts are.
  The phone keeps its own look, the game's FNSM app's.
- **Its cost**, built for each image as the comic marks are: the wheel
  22,020 vertices (the comic one 45,582), a blow with its count 2,664
  (6,210), a target's ring 5,517 (2,436), a Spider-Sense mark 9,936
  (6,804), the meter 7,686 (3,504), the calibration panel 24,168 (3,996),
  each under 0.1 ms.
- **Checked:** 426 core tests (4 new: COMIC is every mark's default and
  TECH draws each anew; the tech marks' colours, motion, placement and
  ends; Spider-Sense over his head and in the sightline where the comic
  one is; the wheel, the meter and the panel, the panel translucent with
  nothing behind it, the wheel under 60,000 vertices), the aim markers'
  test now for both styles, 30 launcher tests (profiles hold `ui_style`,
  the change list says "UI STYLE COMIC → TECH"), the six protocol suites,
  the GPU overlay test (debug layer clean), and the game's modules built
  but the launcher. Sheets of every mark's states in both styles over two
  headset captures: `tools\preview-marks.ps1 -Style Tech`, which now draws
  the aim markers, the meter and the panel too.
- **Not checked:** in the game or a headset (no game launch while the PC's
  hard resets are unexplained). `tools/probe_menu.py` takes one more Down
  for the new row; not run.

**Earlier build (October 20, eighth): settings profiles, to keep, share and
import.** You: "lets add spidy settings profiles so people can easily import
and export and share them".

- **Where:** "Profile", the first of the launcher's options. Its menu lists
  your profiles (pick one to use it; a tooltip says what it would change),
  *Save as a profile...*, *Save changes to* the one you used once you have
  changed something (the row then shows its name with a `*`), *Copy to
  share*, *Export a file...*, *Paste a profile*, *Import a file...*, *Delete a
  profile* (to the Recycle Bin) and *Open the profiles folder*. A `.spidy`
  file dropped on the launcher's window is imported too.
- **What a profile holds:** every setting of the SPIDY VR tab but VR VIEW and
  CALIBRATE BODY, the tab's own ones too (NEAR CLIP, the four body haptics
  switches, HAND WALL CLIMBING, WALL CAMERA DISTANCE, REALISTIC FLIPS and its
  nine), and "Wall running": 33 settings. Not the graphics options (render
  resolution, upscaling, the game window, the monitor, the game's depth of
  field and sharpening), which suit one PC, and not the body calibration.
- **The text** is what the files hold: a line saying what it is and how to
  use it, `name=`, `spidy=0.4.0`, then a `key=value` line per setting, with
  launcher.ini's and spidy_vr.ini's keys. *Copy to share* puts it in a Discord
  code block. *Paste a profile* takes it back from a whole message, quoted or
  not, and also takes a session's "VR settings from the headset" line,
  launcher.ini, or a few lines written by hand (`hud=3`, `wall_run=off`).
  Values out of range are clamped; anything else is passed over. A setting a
  profile lacks stays as it is: a profile from an older Spidy, or a partial
  one, changes only what it names.
- **Importing shows what changes first:** each setting it would change, as
  the tab names it ("WEB STYLE  REALISTIC → ARCADE", "NEAR CLIP  2 CM → 1 CM"),
  and how many of the 33 it holds; then *Save and use*, *Save only* or
  *Cancel*. A name you already have says saving replaces it.
- **Nothing is lost by trying one:** using a profile while no saved profile
  holds your settings first keeps them as "Previous settings"; picking that
  goes back, and keeps what you had in its place, to switch again.
- **Where they live:** `%APPDATA%\Spidy\profiles\<name>.spidy`, beside
  launcher.ini. The launcher now reads `spidy_vr.ini` for the tab's own
  settings and writes it when a profile changes one, the way the stereo
  module does. launcher.ini gets `profile=` (the one last used or saved). All
  of it is off while VR runs, as the options are.
- **Checked:** 30 launcher tests (6 new: the profile holds every tab row and
  `spidy_vr.ini`'s keys at the module's ranges, the exact text of the
  defaults, a round trip that leaves graphics and the calibration alone, a
  Discord message, a session's line, launcher.ini and words, the change list
  in the tab's words, file names), and each of seven planted bugs fails one.
  The launcher driven on this PC with your settings backed up: save, the
  `*` after a change, the tooltip, copy (the clipboard held the code block),
  paste of an 8-setting message (preview, then *Save and use*: launcher.ini
  and `spidy_vr.ini` changed in those 8 only, Previous settings saved), a
  dropped file, *Save only*, going back and forth with Previous settings,
  export and import through Windows' dialogs, delete, a clipboard with no
  profile. Then launcher.ini and `spidy_vr.ini` put back (same SHA-256), the
  test profiles and the clipboard too. Core 422, realistic flips, all six
  protocol suites (138). No protocol change: the launcher changes, and the
  stereo module is only rebuilt (`vr_settings::steps` made public).
- **Not checked:** a VR session started after a profile was used, reading
  the `spidy_vr.ini` the launcher wrote (the same text the module writes;
  core tests read it back).

**Earlier build (October 20, seventh): after a realistic flip the stick walks
where you look.** You: "i tried the new experimental locomotion and crawling
settings - they somehow caused offset where moving forward would cause me to
walk left (see last session)".

- **The session** (16:58, 6 minutes 44 seconds; HAND WALL CLIMBING and
  REALISTIC FLIPS switched on at 1:32-1:36, FLIPS at 1:40): from the first
  flights with REALISTIC FLIPS on, every walk on the ground went off to the
  side of where you looked, measured from your view against the way you
  moved: 91 degrees to the left at 3:45, 85 to 90 at 5:35-5:45, 55 at 6:35,
  and the game was sent its left key (A) where you pushed ahead. Your body's
  legs kept their air pose on the ground from 2:30 to the end; before the
  switches they stood (164 of 166 samples on the ground).
- **Why: the heading a realistic flip keeps stayed in its tilt.** A realistic
  flip brings you upright with your heading kept, and it kept that heading in
  the tilt it turns the view by. The stick walks by the tracking space's yaw,
  which leaves a flip's tilt aside, and the game's camera turns with the
  view: whatever turn about the vertical a spin left was missing from every
  walk, until a menu turned the view back. FLIPS' own flips always end
  level, so they never had it. The body took the leftover turn for a lean,
  and a lean over 5 degrees has its feet off the ground. HAND WALL CLIMBING
  was no part of it: its turns go into the yaw, and its roll ends with the
  wall.
- **Fixed:** while you are within a quarter turn of upright, the yaw takes
  the spin's turn about the vertical every frame, about the point the tilt
  turns round, so nothing in view moves; the tilt keeps only the lean, level
  again once you land or press A in the air. The stick walks, and steers in
  the air, where you look; a menu or a recenter keeps the way you face; the
  legs stand again after a landing.
- **Checked:** a new rig test (both arms swept to the left in the air, then a
  landing or A: the stick walks along the look, the head stays where it is,
  the view never jumps a frame) failed on the old code and passes, and fails
  with the place not made up for as the yaw turns. In the realistic flips'
  own test, a twin that keeps the turn in its tilt turns the same to 0.000006
  radians over 900 frames of webs, a release, arm swings, A and a landing,
  and leaving out any of the five things that turn with the heading breaks
  it. 422 core tests, the realistic flips' and the launcher's programs, all
  six protocol suites. Only the stereo and ray modules change; no protocol
  change. Not run in the game or a headset.

**Earlier build (October 20, sixth): body haptics on bHaptics devices, retuned
after the first session on the devkit, with a switch for each group.** You:
"plan and build the bhaptics implementation. im getting my devkit tomorrow
(TactSuit Pro, TactSleeve, and TactVisor)"; after the first session: "ok it
worked buy way too agressive, directionality didnt really feel like anything,
felt like 0 to 100 always with sometimes still going when im standing (on the
wall or in pause menu etc)"; then "also add a toggle to settings?" (one switch
per effect group).

- **The first session** (a TactSuit Pro and both TactSleeves; the visor was
  not paired): 5,483 frames to the devices, none refused, 227 events (80 webs
  taking hold and 80 letting go, 52 zips, 11 walls, 2 landings, a scan). The
  report and the bHaptics Player's own log showed what you felt: the wind blew
  94% of the time in the air (the swing's speed was 31 m/s at the middle and
  it blew from 12 m/s) and along walls too; most events played at 50-100% on
  half the vest; each device's 70 ms frames came every 40 ms, overlapping for
  30 ms of each; and the vest faced wherever your head looked. The
  pause menu itself was quiet: nothing went to the Player in its 4 s.
- **Gentler, and from nothing.** Events play at 10-45%, the heaviest blows
  (finishers, heavy blows) up to 75%. A web's pull grows with its tension from
  nothing: 2% hanging, 8% in the middle of a swing, 23% at the bottom of a fast
  one, 30% at most (until now any pull over 2 m/s^2 played 12% at once). A
  motor under 3% is sent as off.
- **The wind only in free flight**: in the air, off any wall, no web holding
  you. From 15 m/s (nothing) to 40 m/s (16%), on the motors that face where
  you fly, gliding instead of fluttering, coming and going over a quarter of a
  second; in a fall it comes up round the waist.
- **Spots instead of halves.** A blow, a zip, a wall and Spider-Sense play on
  a few motors toward where they come from, the nearest at full strength (a
  blow from the side was weaker than one from ahead). Zips play toward where
  the web pulls you, walls on the side the wall is; a blow from nowhere known
  on the chest and the back.
- **The vest faces where your body does**: a look over your shoulder (up to 34
  degrees) leaves it; past that it follows your head, and it turns with snap
  and smooth turns, a wall you stand on and a flip at once.
- **One frame at a time.** Each device's last frame is stopped before the next
  plays. Whether the Player played two overlapping frames at once, adding them
  up, is not to be seen from outside; now it cannot.
- **The switches.** A new HAPTICS section in the SPIDY VR tab: CONTROLLER
  VIBRATION (from COMFORT), BODY HAPTICS (OFF, 25-100%) and four switches:
  WEB HAPTICS (shots, holds, the swing's pull, zips, grabs), WIND HAPTICS,
  COMBAT HAPTICS (punches, hits, Spider-Sense, dodges, heals, slow motion and
  the heart) and MOVEMENT HAPTICS (landings, walls, wall climbing, leaps). A
  group switched off stops at once. They are the tab's alone, kept in
  `%APPDATA%\Spidy\spidy_vr.ini` like the other tab-only rows; "Body haptics"
  in the launcher stays the level.
- **What else you feel.** The arm whose web takes hold ticks, then its
  shoulder; a web ball ticks in its forearm. Landings come up from the waist,
  as hard as you came down; a charged leap builds round the waist and lets go
  up the body; HAND WALL CLIMBING's grips tick in their arm. Every punch kicks
  back through its arm and shoulder, a heavy blow through the chest, a
  finisher through everything. Spider-Sense tingles in the visor, two motors
  at a time a dozen times a second, strongest on the attacker's side, and on
  a spot at your shoulders toward him; a warning to dodge harder and longer.
  Blows you take are as hard as the health they took (falls in the hero's
  `HeroHealth`, vtable `38a6df0`, hit points at +0x88), from the attack the
  game warned of last. Below 30% health your heart races left of the chest's
  middle; slow motion beats it slowly. Menus, scenes and the game screen are
  quiet.
- **How.** The patterns are code (`body_haptics`, in the core): nothing is
  designed in bHaptics' portal. bHaptics' C/C++ SDK 2.7.1 (tact-cpp2) plays
  them, its library loaded only once BODY HAPTICS is on, on a thread of its
  own (`bhaptics_output`). A PC without the bHaptics Player does nothing more;
  a Player started after VR is found by itself. "No device" waits three
  seconds for the Player's device list.
- **The bHaptics app.** The Player plays only for an app of bHaptics'
  Developer Portal: its workspace ID (not the app's own record ID, which gets
  "No workspace") and API key go in `bhaptics-app.local` (git-ignored) or the
  `SPIDY_BHAPTICS_APP_ID` / `SPIDY_BHAPTICS_API_KEY` variables, and the
  release workflow takes repository secrets of those names (not set yet). Your
  app, SpidyVR, is in this PC's `bhaptics-app.local`. A Player that connects
  but sends no event list within 5 s says `rejected`.
- **`spidy_haptics_test.exe`**, in the zip too: `check`, `map` (each part of
  each device in turn), `effects` (every pattern, named, with what it should
  feel like), `stream`, and `--dry` (the frames as the devices would get them,
  no Player).
- **Protocol:** XrConfig version 24 (`bodyHaptics` in version 23's spare
  word); a new export, `SpidyHapticsData` (version 2, 256 bytes: the report's
  and each sample's `haptics` - now also the seconds of play with the Player,
  each device's share of them playing and its mean strength (`played`), and
  the switches on (`groups`) - and `vr_settings.body_haptics`); a new kind,
  `wall_grip` (35); the menu probe's structs version 6 (`body_haptics`);
  launcher.ini `body_haptics`; `--body-haptics`. A session puts
  `bhaptics_library.dll` beside the stereo module it stages. The DLLs,
  run_game_vr.py and the launcher go together. On main's 3ce3baa (October 20,
  fifth) with pull requests 22, 23 and 24 and the prompt fixes.
- **Checked:** 421 core tests (new here: the TactSuit Pro's grid and the
  body's frame, the torso staying put for a look aside and turning with the
  room, a pattern on its own side only and out of VR play never, each switch
  silencing its group at once, blows on a spot toward the attacker and
  Spider-Sense on his side, the wind in free flight only and the webs' pull,
  mixing below full strength and the heart, landings and health, the tab's
  HAPTICS section and its lines in `spidy_vr.ini`), 24 launcher tests, the
  realistic flips' tests, all six protocol suites (138 tests). The Player,
  probed from this PC: `stop` ends a frame at once, its legacy socket does not
  report the SDK's dots, and `isPlayingByRequestId` stuck on after some
  streams. The retuned patterns have not been felt yet, nor run in the game.
  `tools/probe_menu.py` follows the new rows (26 changes); not run in the game.

**Earlier build (October 20, fifth): realistic flips, experimental - pull
request 23 (iplusdvr).** You: "review and merge or fix the open prs".

- **REALISTIC FLIPS, under EXPERIMENTAL, off by default, and nine rows that
  tune it.** While it is on it takes the place of FLIPS' turns: on a web,
  above a travel speed, the body turns toward the web's pull; a web let go
  leaves the swing's spin; arm swings in the air add spin and swings the
  other way take it out; drag lets it die away, and a spin can go past a
  whole turn, with no flip of a set size. A in the air, or landing, brings
  you upright with your heading kept. With FLIPS on as well, the left stick,
  let go once in the air, steers the turn. The author drew on the
  interaction of Blaze's Spider-Man for Blade & Sorcery, with none of its
  code, and tried it in a headset. Off, the flips are as they were.
- **What the review changed.** The ten rows came first in the tab, above
  WEBS; they are last under EXPERIMENTAL now, and FLIPS keeps its own help
  (the pull request had rewritten it for the new mode). The settings went
  into `SpidyRotation.ini` beside the stereo module, which runs from a copy
  staged per build, so every update would have reset them; they are kept in
  `%APPDATA%\Spidy\spidy_vr.ini` with the tab's other own settings. The new
  files are formatted as the rest (`.clang-format`), the speeds read as
  FLIP SPEED's do (degrees a second), the separate test program is
  `spidy_realistic_flip_tests` (ctest `realistic_flips`), and the flight is
  only sampled for the rig while the switch is on.
- **Checked:** 408 core tests (new: the pull request's three, and one that
  realistic flips off turn nothing and that the rows step through their
  values), the realistic flips' own test program, the launcher's, all six
  protocol suites. Not run here in the game or a headset.

**Earlier build (October 20, fourth): NEAR CLIP - pull request 22
(iplusdvr).** You: "review and merge or fix the open prs".

- **NEAR CLIP, the last row under DISPLAY: how near the eyes the headset
  still shows things, 2 cm unless you change it (1, 1.5, 2, 3, 5, 7.5 or 10
  cm).** Spidy used to keep the game's own camera's near plane, 10 cm: right
  for a camera behind the hero, but on the head it cut off hands held up
  before the face. The head's view and both eyes take it while you are
  immersive; the game screen and menus keep the game's. The engine's depth
  is reversed, so the nearer plane costs no precision far away, and the
  hero's head is already shrunk to a millimetre in VR, so its inside does not
  show. The author tried it in a headset.
- **What the review changed.** The row came first in the tab, above WEBS;
  it is under DISPLAY now. The setting went into a file beside the stereo
  module, which runs from a copy staged per build, so every update would
  have reset it; and the render thread read that file every half second. It
  is kept in `%APPDATA%\Spidy\spidy_vr.ini` with the tab's other own
  settings (see the build before), and the XR worker hands it to the lenses
  (`near_clip::set`) as the settings apply. `tools/probe_menu.py` steps past
  the new row.
- **Checked:** core tests (new: the near clip's choices and range, and its
  line in the tab's file), the launcher's, all six protocol suites. Not run
  here in the game or a headset.

**Earlier build (October 20, third): hand wall climbing, experimental -
Adrian's pull request 24 (AdrianGzzEsc).** You: "review and merge or fix the
open prs".

- **HAND WALL CLIMBING, under EXPERIMENTAL, off by default.** On a wall with
  the left stick at rest the view stays upright beside it, a little
  crouched, the eyes WALL CAMERA DISTANCE out (40 cm; 20 to 100). A grip
  squeezed within about 18 cm of the wall holds on, open-palmed, with a short
  buzz, and pulling it moves you the other way along the wall; two hands
  share the pull. Both grips held and rolled turn the view along the wall
  (90 degrees a second at most); the stick runs along the wall, steered by
  the head's left and right; A jumps off. Round an outside corner the view
  turns onto the next face and the body goes 85 cm on along it. Off, the
  climbing paths do not run.
- **What the review changed.** The pull request came from the author's own
  preview builds, and three of their changes would have reached every
  player: the launcher's title, its settings moved to a folder of its own
  (`%APPDATA%\SpidyWallClimbPreview`, so everyone's saved options would have
  gone), and the update check switched off. Those are out, and so are the
  preview's notes (`docs/WALL_CLIMB_PREVIEW.md`); the player's part is in
  PLAYERS.md. The swing command went from version 4 to one version 5 (240
  bytes: each grip's place and squeeze, the roll's flag) instead of the
  previews' versions 5, 6 and 7, whose gaze direction nothing read any more.
  WALL CAMERA DISTANCE lost 10 and 15 cm: the eyes keep 20 cm from any
  surface anyway (`HeadClearance::margin`, as the pull request's own test
  showed), and the tab shows nine choices at most. The two rows were for one
  session only; they now stay, in `%APPDATA%\Spidy\spidy_vr.ini`, which the
  game's side of Spidy keeps for the SPIDY VR rows the launcher does not
  show. Every row is checked against the tab's nine choices.
- **Checked:** 403 core tests (41 new from the pull request; new here: the
  tab-only settings' lines and their reading), the launcher's, all six
  protocol suites. The rebuilt ray module takes a version 2 and the probe's
  version 5 command and refuses a roll flag of 2 and the previews' version
  7. The author played earlier previews; the last one (corners) had not been
  played. Not run here in the game or a headset.

**Earlier build (October 20, second): what the eyes draw counts as on
screen.** You: "its not the textures its like the game lod system range is
very short with low LOD objects seen from pretty close and stuff popping in
from nearby (cars pedestrians). it didnt use to be like this".

- **The cause, from 0.2.12 on.** Each of the game's views keeps a list of
  the render instances it drew in its last frame (view `+1b80`, a byte each,
  327,808 at your save). The view's scene submit clears it (`1920e54`) and
  its model jobs mark what they draw (`1799a7d`). Gameplay reads these lists
  to know what you see: the ViewportUpdateSystem sends actors their
  entered- and left-the-viewport events from the active view's list
  (`214f150`), and about 60 places ask whether an instance was drawn in any
  of the game's views (`18a1890`, for one instance `159e940`), among them
  the pedestrian groups and bots, the joint matching of attached models,
  and the game's targeting (`ClosestOnScreenTargeting`, `5a3020`, and the
  target scoring, `211c950`; projects-87 found these two). The eyes never
  had a list (the view setup, `189ce20`, gives offscreen views none), and
  since 0.2.12 the game's own view rests in VR and submits no scene, so its
  list kept the last frame it drew before VR started. In 0.2.11 that view
  still rendered from your head every frame, which kept its list right.
- **Measured, in the game without a headset** (your save, the eyes at a
  street, `reports/drawn-ab.json`): before VR the game's camera had 116
  instances marked. With the eyes on as 0.4.0 has them, the list did not
  change for whole 16-second stretches: it held the game camera's last
  rendered frame (113 to 116 instances) while the eyes drew 3,900 to 4,600.
- **What changed.** While the game's view rests, Spidy clears its list each
  frame before the eyes' jobs are set up (`placeEyes`), and each eye's model
  job marks what it draws into it (`setupModels`, job `+2b0`). The clear
  comes where the game's own would (`1920240`, just before the views' scene
  submits in `175a060`), so gameplay reads the list at the same point of
  its frame as in the flat game. With this build (`reports/tree-drawn.json`,
  `reports/tree-fly.json`): 4,460 to 4,600 instances at the street, every
  frame (`SpidyDrawnFrames`, a new export, counts them; a sample taken
  between the clear and the jobs' marks reads empty, in the flat game too);
  following the view through a 15 m/s flight down the street (4,897 down to
  884 as it opened up); with the game's view rendering again and resting
  again in between; the eyes stopped cleanly each time.
- **Not shown without the headset:** what the game does differently with a
  right list. The viewport events go to the actors' own scripts, and people
  and traffic come and go at random, so a few minutes of eye images showed
  no difference I could put down to it. In the headset: swing along a street
  with people and cars, and watch whether they still appear and vanish close
  by; and where there is something to throw or yank, watch whether its
  marker stays. hantop's playthrough lost those markers in VR (Fisk's
  turrets, the Shocker bank's throwables, the car-crash boxes) and got them
  back by switching to GAME SCREEN and back, which is what a list frozen at
  the game camera's last frame does. Traffic's own code (`d67740` to
  `d6b8b6`) reaches none of the readers within three direct calls, so cars
  that still pop in may have another cause. Not changed: animated models get
  part of their joint update only in the game's view's model job (`+342`),
  which rests in VR; not looked into.
- **Checked:** the build, both C++ suites and all six Python suites. Only
  `spidy_stereo_probe.dll` changed; no protocol changed.

**Earlier build (October 20): press what you see.** You, about hantop's
playthrough in #mod-chat (50 items, October 14-19, the Oscorp research
stations among them): "review the research hantop did ... try to come up
with a unified immersive fix or a set of fixes", then "build it" for its
first part.

- **What the playthrough came down to.** The moments it could finish only on
  the game screen name buttons VR play never sent. Read off its recordings:
  "Hold LT and tap RB to fire webs and seal the leaks" (Under Pressure,
  Chemical Leak, Ventilate the Problem), "Press LB to disperse vaccine"
  (Vaccinate the Fish, Pigeon Vaccine), "Press D-pad up to equip the
  camera", "Hold LT to aim the camera" (the smog stations, the landmarks),
  "Press X for Vent Takedown" (Fisk Tower, the museum), "hold X + A to
  Ground Strike" (Shaking Harlem), "Hold RB to bring up the Gadget Select"
  (the Web Bomb's lesson), and a hostage's "use the left stick to aim, RB to
  disarm". In VR play X opened the map or the wheel, the triggers and grips
  shot Spidy's webs, and the game's LB, RB and LT on their own and its D-pad
  up could not be sent at all.
- **Why "Y sends what the prompt asks for" never helped.** Every session
  since October 15 had the prompt reading off: on October 13 it closed the
  game at CONTINUE, every time. The fault was Spidy's own logging, not the
  game: each prompt call's line for the report went through `sprintf_s` with
  a timestamp in front into a 128-byte buffer, and a call's line alone can
  be 126 characters. A long text - the help line a save's load brings, a
  police dispatch subtitle - overflowed it, and `sprintf_s` ends the process
  for that, with no exception and no crash dump. The line is cut to its
  buffer now (`keepLine`), and every raw read of the game's memory asks
  Windows first (`readable`), and the reading is on in every session again.
- **Measured, in the game without a headset** (your save, CONTINUE, the
  reading on from the title): with 0.4.0's module the game died right after
  CONTINUE, both times, its log ending on the load's own save request. With
  this build the same run went through CONTINUE (the player at 68 s) and two
  minutes of free roam, and lived. The first thing the game showed after the
  load was `ShowText(7, 'Press [STICK_LEFT_PRESS] to <span
  class='emphasis'>dive</span> and <span class='emphasis'>gain speed</span>
  while swinging. ...')`, 127 characters in the report's ring. A later
  load's help line, "Press [BTN_X] then [BTN_B] to perform the Dodge Under
  move", came through as the pad's X and B and stayed live the 6.5 s it was
  shown. Your 17 save files came out byte-identical after every run.
- **Press what you see** (`game_pad::NamedPress`). While the game names a
  button, the VR control of that name sends it. A, B, X and Y as they are: X
  is then no menu or wheel, A no leap or flip, and B goes out while you
  swing. LT or LB is the left hand's web-ball button and RT or RB the right
  one's: the trigger, or the grip with WEB BUTTON: TRIGGER. The grips stay
  your webs, so a mission that asks for its triggers still swings. L3 and R3
  are the sticks' clicks (no slow motion, no pull meanwhile), and D-pad up,
  left or right is the right stick pushed that way (no zip, no turn
  meanwhile). A control is the game's only when it goes down while the game
  names it, and it stays so until it is let go. Pressed in flight, it asks
  the swing to let the game have its move (AirYield): the dive, the game's
  own web swing, the Ground Strike.
- **Fists.** While the game names its X, a fist thrown fast (2.2 m/s, and
  not pulled back toward the head) is that X: held 0.6 s where the prompt
  says hold (the Air Launch's uppercut). With X and A named in the air, a
  fist thrown down is both: the Ground Strike. Spidy's own blows stand down
  meanwhile (the swing's `noBlows`), so a takedown is the game's alone.
- **A help line lasts as long as it is shown.** `ShowText(channel, text)`
  stays up until `HideText(channel)`. What it names now lives that long (60
  s at most without a call), not 2 s, and not until any `HideText` (a
  subtitle sends one on its own channel). The rules are
  `prompt_buttons::LiveDemands`, tested on their own.
- **New words.** `[STICK_LEFT_PRESS]` and `[STICK_RIGHT_PRESS]`, which the
  exe does not hold (the texts' own data has words of its own), and
  `[FUNC_MELEE]`, the melee's button: X. **Not read yet:** a third kind,
  the game's actions by name. Another load showed "While in the air, hold
  `[ACTION_WEB_YANK_BOT]` to yank enemies up to you": an action of its own
  binding configs (Fire, Swing, WebZip, WebYankBot, WebStrike, AimMode,
  UseGadget, Heal and the rest), whose buttons the configs hold and the exe
  does not. A line that names an action names nothing to Spidy until those
  are read; guessing would press the wrong thing.
- **What went.** `ContextPress`, the interact sending a prompt's demand: it
  never ran with the reading off, and with each button now its own, there is
  nothing left for it. A pull with both webs at a thing whose prompt names
  both triggers still sends LT + RT.
- **Docs:** PLAYERS.md (WHEN THE GAME ASKS FOR A BUTTON, the troubleshooting
  line), the README, the launcher's Controls page. Discord's Controls post
  and Webster's `faq.ts` still say "press Y": they follow with the release.
- **Protocol:** none. The report's `prompts.context_presses` now counts the
  presses that went to a named button, thrown fists included, and the
  report's prompts section (census, timeline) fills again.
- **Checked:** 365 core tests (new: the named presses, the fists, a coverage
  test that every button the game can name is sent in VR play, the help
  lines measured in the game, and what stays live how long); three bugs planted on purpose, each caught by
  its test; all six protocol suites; the launcher's tests. **Not yet in a
  headset:** the presses themselves, the fists, and the game taking its move
  from the swing.

**Earlier build (October 19, fourth): arcade webs, and the SPIDY VR tab in
sections.** You: "the current web swinging is hard - webs get tangled over
lightpoles and other small geometry, also they pull you down too much to the
street. lets make an optional second set for arcady webs where they don't
collide with those things, all sort the options in spidy vr settings ingame
into nice sections".

- **What your sessions showed.** Today's three sessions bent 12, 21 and 18
  webs round edges and took them off again 0, 3 and 1 times: a bend stayed
  until the web was let go. Several bends sat 3-7 m over the street with
  the body 1-3 m up, a 100 m web cut down to a 7 m rope round a post (session
  13:08 at 547, 2.4, -1548).
- **WEB STYLE, the first row under WEBS: REALISTIC (as before, the default)
  or ARCADE.** Arcade webs go through thin things - lamp posts, signs,
  railings, a roof's parapet - and through anything that is not fixed (cars,
  props), as they are shot and as they swing: no sticking to them, no bending
  round them, no cutting off. The shot goes on to the building behind; with
  nothing behind it, the web holds in open air (or misses, with that off).
  Buildings still hold webs and bend them round their corners. And a swing
  comes down to the street no nearer than 6 m (the body's centre; feet
  about 5 m up, over buses) instead of 1.4 m: the fall is turned along the
  street from higher up, its speed kept, as the near misses were. Let go to
  come down.
- **Thin, by the game's own rays.** Where a web meets a face, a ray from
  1.5 m behind that face back out to it either starts in the open and meets
  the thing's far face (a post, a sign: thin), or starts inside a building
  and meets that face from within or nothing (the game's rays meet faces
  from either side). One rule for every object, no list of props. A post
  less than 1.5 m in front of a wall counts as the wall's (that ray starts
  inside the wall), as before.
- **The tab's sections:** WEBS (web style, web button, aim markers, webs in
  open air), SWINGING (swing speed limit, weight), COMFORT (snap turn, smooth
  turn, vibration), DISPLAY (game screen size, its 3D, the HUD), BODY
  (calibrate body), EXPERIMENTAL (flips, flip speed). The launcher has "Web
  style" above "Web button".
- **Protocol:** the swing's settings version 7 (`arcade`; the probes' 3-6
  still taken); XrConfig option bit 21 (`--arcade-webs`); XrData settings bit
  512 and the report's `vr_settings.arcade_webs`; rope data version 3, whose
  spare word is now `passed` (the report's `ropes.passed`: shots past a thin
  or loose thing, and swings' lines coming onto one); menu probe flag 2048.
  launcher.ini `arcade_webs`. The DLLs, run_game_vr.py and the launcher go
  together.
- **Checked:** 361 core tests (new: a lamp post a realistic web bends round
  and an arcade web goes through; a shot through a post or a car on to the
  building; a building's corner still bending an arcade web, with rays that
  meet faces from either side; arcade swings held 6 m off the street), the
  launcher's tests, all six protocol suites, the graphics test.
  `tools/probe_menu.py` follows the new rows (24 changes); it was not run in
  the game, and nothing of this has been in the game or a headset yet.

**Earlier build (October 19, third): full buildings half as far again.** You,
after the second build in the headset: "can we increase the distance a
bit?"

- **The distance.** A far building draws its full model within 145 m of the
  view that asks (the impostor queries' result `+88`, from `7a89170`, which
  region changes rewrite; the game's lens of 65 degrees gets exactly that, a
  narrower one more). The eyes now ask with 150% of it, 218 m
  (`impostorQueries`, a hook on `16faf10` that raises the result for both
  eyes while the left eye stands in; the game's own setting is left alone).
  `--building-reach` (50-400%, default 150) sets it for a session, through
  the module's new `SpidyImpostorReach`; the report's `eyes` has
  `building_reach`.
- **Measured, in the game without a headset** (your save, the eyes 40 m up,
  `tools/probe_streaming.py --modes rest@100,rest@150,...`): fine detail 2.74
  at 100%, 2.89 at 150%, 2.75 at 100% again; a tower beside the view went
  from its card's painted windows to its model's. 200% and 300% added
  nothing more from that spot. Frames a second with 3072 x 3264 eyes
  (`tools/probe_vr_load.py`, phases `vr_r100` to `vr_r200` in one game):
  64.4 at 100% (two runs, 65.7 and 63.0), 64.7 at 150%, 63.7 at 200%;
  turning 73.2, 73.1, 73.6. Render commands 2 to 5% more, video memory 20 MB.
- **Checked:** 357 core tests, all six protocol suites, the graphics test.
  The module checks the queries' entry, their 145 m first write and their
  last write of the distance; where any of it differs, no eye stands in.

**Earlier build (October 19, second): far buildings light up again.** You,
after the first build in the headset: "nope, I feel like its the culling or
lod of some shader (windows, fake cube scenes etc). I did notice that
entering menu refreshes it", with two pictures of the same view: the
buildings in the middle distance dark slabs in one, full of lit windows in
the other; then "we had a similiar looking problem in the past where vr
camera would be offset from the culling controlling camera".

- **The cause.** Far buildings are impostors: cards that stand in for the
  model. Which of them draw their full model, with the lit windows and the
  rooms behind them, the game decides for one view only, its active view
  (`189daa0`: the view at ViewContextManager `+f8`, the game's own camera):
  `ImpostorHQModelRegJob` (`16fb630`, `16fb750`) applies that view's wanted
  state for each impostor, from its visibility queries (`16faf10`), and the
  impostor submit job (`16f8afa`) marks it too. Since 0.2.12 that view does
  not render while you play in VR (`restGameView`), so it submitted nothing
  and no far building drew in full. A menu renders it again, which lit them
  until it went back to rest. The eyes run the same impostor code but are
  never the active view. This is the culling camera you remembered: the
  game's view sits on your head since October 5, and still did the culling,
  but stopped doing this part of it.
- **What changed.** While the game's view rests, the left eye stands in for
  it at exactly those five checks (`isActiveView`, a hook on `189daa0` that
  answers yes for the left eye only when called from those five places).
  One view only: the registration applies a view's wanted states over the
  last ones, and two views would undo each other's every frame. When the
  game's view renders (menus, cutscenes, the flat screen, Ray-Traced
  Reflections on) it decides again, as before.
- **Shown, in the game without a headset** (your save, the eyes 40 m up,
  `tools/probe_streaming.py`, each in a fresh game): the game's view resting
  gave the eyes dark buildings (fine detail 1.81), rendering gave lit ones
  (2.21), and resting again went dark within seconds. Rendering it without
  its impostor work (flag 0x100 masked out of the game's views, `609d708`)
  stayed dark (1.81): the impostor work is the whole difference. Taking away
  its stale occlusion, or giving the eyes that flag (they have it: 0x37ff),
  changed nothing. With this build, resting: 2.19, every frame stood in for
  (`impostor_frames`); the picture matches yours after a menu.
- **Not covered: the right eye's outer edge.** The left eye's view decides,
  so buildings only the right eye sees (its outer 10 degrees) can stay dark
  until you look toward them. Covering both needs the left eye's impostor
  queries built from the lens over both eyes (`189fea0`, `189b820`), not
  done.
- **Seen elsewhere, not changed:** the same active-view-only rule hides two
  more things in VR. Pedestrians ask "drawn in any view" (`18a1890`), which
  reads only the game's views (possibly kinxy0.0's "people just randomly
  spawn"), and animated models get their joints updated only from the
  game's view (model job `+342`, `1781df0`). About 15 other callers of
  `189daa0` (shadows, post-processing, decals) were not looked at.
- **Protocol:** `SpidyAppearanceData` stays version 6; its last field is
  `impostor_frames` now (unused before). Deploy the whole package.
- **Checked:** 357 core tests, all six protocol suites (`impostor_frames`
  decoded), the graphics test. The module checks `189daa0` and the five calls
  to it before hooking; where they differ, no eye stands in.

**Earlier build (October 19): the city at the flat game's distances.** You,
about a #spidy-bugs topic: "review and try to fix this (some other topics
mention it imo)". The topic, "Building textures load late while swinging
(pop-in)": Keith (Valve Index, RTX 4090, Spidy 0.3.6, Render resolution
200%, Upscaling 75%, Texture Quality Very High) sees the buildings' textures
come in only once he is close, and "Flat game does not have the issue. I can
see far out into the city". HaNToP (RTX 4090, Very High) sees the same and
says 0.2.11 looked sharper and loaded at once. In "Recording problem",
kinxy0.0 (0.3.6, Quest Link): "my view distance is bad", "textures people
just randomly spawn". In Keith's clip a row of buildings across the street
gets its windows in one frame, a fifth of a second apart, as he swings at it.

- **The cause.** The game judges every distance a model is drawn by against
  a number it takes from the view's lens (`189e4b0`: 0.6745 over the tangent
  of half its width, 1 for a 68-degree 16:9 lens): which detail level a model
  draws, how far away it is drawn at all (people, props) and up to where its
  textures are asked for (16 times its size times the number), and many other
  systems read it too. The game's own camera, 65 degrees wide, has 1.06; a
  headset eye, 98 degrees (Quest 3 through Virtual Desktop), had 0.59. So in
  VR all of that happened at about half the distance it happens on the
  monitor. The game's Level of Detail setting multiplies in on top, in both.
- **And since 0.2.12** the eyes render their scene smaller and upscale it
  (Upscaling, 67% by default): the game sizes a view's texture requests by
  the pixels it renders, so the eyes asked for two thirds of the detail the
  headset shows, half at Performance. 0.2.11 had no upscaling.
- **Not the cause:** the game's own view not being rendered in VR (since
  0.2.12). Rendering it again changed nothing the texture streamer was asked
  for: its lens covers both eyes, so it asks for less than they do.
- **What changed.** Each eye now gets the game's 68-degree lens's distances:
  1.71 times its own for that Quest 3 eye, never less than its own, at most
  three times (`matchDistances`, view `+1cc8`, which only cutscenes set).
  Each eye's texture requests are sized for its image, not for the smaller
  scene it renders (`setupModels` on the model job setup `1796d20`: 1.49
  times at 67%), as upscalers are meant to have it. The game's Level of
  Detail and Texture Quality settings apply as before. The report's
  `appearance` has `eye_detail` and `texture_request_raise`;
  `-EyeLensDistances` (`--eye-lens-distances`) is the old way.
- **Protocol:** `SpidyAppearanceData` version 6 (360 bytes), read by
  `run_game_vr.py`. Deploy the whole package.
- **Measured, in the game without a headset** (this PC's RTX 4080 Laptop GPU,
  your save, the eyes 120 m over the city flying 300 m back and forth at 25
  m/s for three minutes, `tools/probe_streaming.py`, a fresh game each):
  - the module gave each eye 1.7055 in every frame and raised 412,468 model
    jobs' texture requests 1.49 times; held still at that spot, the streamer
    was asked for 28% more textures and 33% more materials;
  - Texture Quality High: the game's texture memory rose from 2,252 to 2,317
    MB of its 3,000 (your own sessions on this PC: 2,028 to 2,755 MB);
  - Texture Quality Very High, which Keith and HaNToP play at: the budget
    (3,874 MB) held 3,803 to 3,813 MB without, and was full with (3,869 to
    3,871). A lens like the game's own camera (65 by 39 degrees, 1920 x 1080,
    no upscaling) on the same flight filled it too (3,870 to 3,872): at Very
    High the flat game runs its budget full over the city as well, and what
    stays loaded is what is asked for most. Without the change the eyes never
    asked for anything beyond about half the flat game's distance, so that
    went first, and came back as you got close;
  - frames a second: 2% fewer at street level (64.2 to 62.9 looking ahead,
    69.0 to 67.8 turning, `tools/probe_vr_load.py` in one game, the eyes'
    multiplier set from outside between phases). The flights gave 4% (High,
    68.7 to 66.0 a minute on average) and 5% (Very High, 67.7 to 64.3), but
    each pair ran one after the other on a laptop that slows as it warms
    (every run lost 5 to 10% from its first minute to its third), so that is
    the most it costs, not what it costs.
- **Left as it is: the budget.** A headset sees more of the city at once than
  the monitor, so a full budget is spread over more textures than in the flat
  game. The budget is the game's own (`TextureAllocUltra` and the rest, the
  global at `7a793c0`), an account the streamer evicts by (`19b50b0`, free =
  budget - the texture heap's use, `19b2d90`), not memory set aside: raising
  it in VR on cards with video memory to spare looks possible, and is not
  done here.
- **Not shown: the picture.** Captured at the same moments of two flights,
  with and without, the eye images hold the same detail (0.999), at night and
  at half resolution, and an eye held still shows every building's windows
  either way. Whether windows and people now come in as far out as on the
  monitor needs a headset; Keith's clip is the one to compare with.
- **Checked:** 357 core tests (new: the lens's number for the game's camera,
  a Quest 3 eye, a lens wider than 16:9 and one with flag 2; the multiplier's
  1.71, its floor of 1 and its cap of 3; the request size of 67% and 100%),
  all six protocol suites (`appearance` version 6 decoded, the old size
  refused), the graphics test. The module checks the code it relies on
  (`1796d20`, its reads of `+1cc8`, `+1cc4`, `+448` to `+424` and its stores
  to `+2fc` and `+300`, `189e4b0`'s and its two constants and 16:9) and keeps
  the eye lenses' own distances where any of it differs.

**Earlier build (October 18, evening): Spider-Sense, and Arkham Shadow's
counter.** You: "design a vr version of the alert/spidey senses for combat
(make it arkham shadow style)". The October 11 combat left out exactly this:
"the counter (Spidy cannot tell yet when an attack is coming)". Now it can.

- **When the game warns, you are warned.** Every enemy attack you can dodge
  registers itself with the hero's dodge manager (`Hero::HeroDodgeManager`):
  the attacker, when you are to be warned, when the blow lands and the
  attack's own flags. At that moment the game plays its Spider-Sense sound
  and lights the lines on Spider-Man's mask, which in VR you are inside of
  and never see. Spidy reads that list at every input sample
  (`src/game_sense.cpp`), so every enemy the game warns of is warned of
  here, at the game's moment, by its data: no list of enemies.
- **What you see.** Over the attacker's head, the comic's Spider-Sense lines,
  printed like the KO! and the called blows: blue (cyan) asks for a counter,
  red for the dodge (B), yellow, under a paper chevron, for the jump (A). A
  white ring closes on them a notch at a time as the blow comes; its last
  notch is a yellow burst: now. An attacker more than 31 degrees off your
  look gets Arkham Shadow's icon in your sightline: his mark, small, 21
  degrees from the middle of your view on his side, an arrowhead pointing
  to him, and his lines at the rim of your sight on that side. The
  controller on his side tingles as the warning goes off (both for one
  ahead or behind), a dodge's harder.
- **The counter.** Throw a fist at a blue-marked attacker while his lines
  show (2.2 m/s or faster, within 40 degrees of him, out from your guard):
  his attack breaks at once (the game's own flinch), and he is your target,
  whoever the ring was on, as Arkham Shadow's "Batman instantly abandons his
  current target in favor of countering". Within reach the fist lands; out
  of it the web strike flies you to him (10 m at most). The blow that lands
  is a heavy one (he flies off along it at 11 m/s, a quarter harder), it
  counts in your combo, his mark breaks (a paper flash, its lines thrown
  wide), time slows for a moment and both hands feel it. A punch that lands
  on him while his lines show is a counter by itself. One thrown in the
  warning's last fifth of a second is counted as perfect; nothing more comes
  of that yet.
- **Red and yellow come from the game's flags.** An attack whose
  kIsCounterable is clear ("Hero dodging can interrupt the attack") asks for
  your dodge; one with kUsesJumpButton for your jump; a blue one from beyond
  a strike's reach for your dodge too.
- **What the session report shows.** `sense`: the warnings that went off and
  what they asked for, the counters (thrown, landed, perfect, their blows,
  the breaks sent), the warnings standing at the end, and a census of what
  the game's warnings were made of (each set of flags, how often, the
  longest warning it gave). `sense_samples`: a row per warning and counter.
  In each sample's `sense_headset`: marks shown and drawn, sightline frames,
  tingles, marks broken, and B pressed during a warning.
- **Protocol:** XrData version 39 (1176 bytes, six Spider-Sense counts at the
  end) and the ray module's new `SpidySenseData` (632 bytes), both read by
  `run_game_vr.py`. Deploy the whole package.
- **Checked:** 350 core tests (new: what each set of flags asks for; a fist
  thrown at a warned attacker counters once after 30 ms and his next blow is
  the counter's; a slow fist, one thrown away, one at a dodge's attacker and
  a hand at the chest counter nothing; a blow landing in the warning counters
  by itself; the sightline's 31 and 26 degrees and the side kept behind you;
  the printed marks' plates, ring, burst, pops and breaks, and their size at
  1.5 to 9 m), all six protocol suites (new: `sense` decoded, a torn read
  refused, another build's struct reported without ending the session), the
  modules' exports loaded outside the game, and every mark over two of your
  eye captures (`tools\preview-marks.ps1`: `far-sense.png`, `near-sense.png`
  and the whole eye with sightline marks in `far-sense-eye.png`). The first
  sheets put the ring across the thug's face and the sightline mark on his
  belt; both moved. **In the game without a headset** (your OK, profile 1's
  Continue, `tools/probe_sense.py`): the hero's dodge manager is found and
  read at 409 of 409 input samples with no fault, and the game's clock runs
  with real time (1.06). No enemy was anywhere near, so **no warning has been
  seen yet, and nothing was tried in a headset.** Which of the game's attacks
  are counterable, how long its warnings are, and that the flinch breaks an
  attack before it lands are in your next session's `sense`, or in
  `tools/probe_sense.py --provoke --counter` next to an enemy.

**Earlier build (October 18, afternoon): the zip out of the game's aim, and a
printed gadget wheel.** You, after the 14:08 session: "lets make it so that
when holding up (zip) and it enters the slowmo aiming mode, on release we will
zip also lets make the gadget selection screen look like other better designed
ingame ui".

- **Let go of the stick to zip from the game's aim.** The right stick held up
  is the game's LT + RT: where the game has a point, it zips at once, as
  before. Where it has none, LT held on is its aim mode (its L2), which
  slows the game's time after a moment and snaps its aim to the points it
  can zip to. In your session that is what happened eight times: time went
  down to 20% 0.6 s into the hold, for up to 3 s, and letting go zipped
  nowhere. Now, once the game slows its time during a hold, LT stays down
  alone (the aim stays) and letting go of the stick presses RT again beside
  it for 0.2 s: the game's LT + RT in its aim, the zip to the point the aim
  holds. The right controller ticks as it goes; while you swing, the swing
  lets the game have you once its zip begins, as this morning. A hold let go
  before the time slows is the zip it was (`game_pad::ZipPress`).
- **The gadget wheel is printed, as the KO!, the bursts and the called
  blows' arrows are.** Each gadget is a paper badge keyed in ink with a
  picture of what it does (a web, the Impact Web's burst, a bomb, a bolt, a
  mine and its line, a blast, a spider, an orb in its orbits), on a ring
  with a bead where a place is empty. The one you have is yellow, one out
  of charges grey, and the dots beside each are its charges, yellow while
  left. The one you point at turns red and larger and its name is lettered
  in a yellow caption across the middle; the names round the ring are gone,
  as on the game's own wheel. The wheel lands in three poses as it opens,
  and the lit badge snaps on, out of register for a pose, then boils as a
  held drawing does.
- **Protocol:** XrData version 38, same size (`zip_releases`, the zips sent
  as a hold was let go, in a spare word). Deploy the whole package.
- **Checked:** 345 core tests (new: `ZipPress` - a zip in normal time as it
  was, the aim keeping LT down alone, one zip as it is let go and RT up
  before it, a moment out of play, a hold dropped by a menu; the printed
  wheel - a picture for each gadget, the caption only for the lit one, the
  snap and the landing, the grey and the yellow badges, under 60,000
  vertices), the launcher's, all six protocol suites, the GPU test (the
  wheel at about a headset's pixel density: 37,605 vertices) and the wheel's
  poses over one of your eye captures (`tools\preview-marks.ps1`, its new
  `near-wheel.png`). **Not run in the game, not tried in a headset.** That RT
  pressed in the game's aim zips to the point it holds is the game's LT + RT
  as its controls have it; nothing here measured it.

**Earlier build (October 18): eight reports from Discord.** You: "lets fix
these issues - more info on discord".

- **The eyes stay out of buildings.** Nothing kept them out: they go where
  the headset is, over the feet as the game has them, and on Spidy's own
  wall the body runs 0.9 m off the facade, so a lean toward it, or a cornice
  at head height, put them inside the building ("I do a wall run on some
  buildings and i get a full pov of the inside of the building"); on the
  ground a step toward a wall did too. Now the five rays from the head
  (from inside the body to past the eyes, and from the eyes ahead, to each
  side and up), and the wall that holds you, hold the eyes 20 cm off any
  surface the world is built of, at once, sliding along it; the hands and
  the body go with them. Once the head is clear the push lets go over a
  sixth of a second (`HeadClearance`). Those five rays only read the room
  for the report before ("indoors", which decided nothing since October
  15). Cars and other things that move are not walls to it.
- **After a break the head is over the body again.** A break longer than
  half a second (a menu, a load, the headset losing its picture as you walk
  out of your play area) puts the head back over the feet, as at the start:
  the walk you made meanwhile was left as a lean away from the body ("i
  left my playspace and came back in ... then desyncs"). A recenter still
  keeps the head where it was.
- **Zip to a point and web strike while you swing (car chases).** Until now
  the right stick up (the game's LT + RT) and Y were held back while a web
  carried you: handing you to the game in mid-flight dropped players at the
  game's own fall speed. Now both go to the game while you swing (the zip
  from Spidy's own wall too), and the
  swing lets the game have you once the game has begun its move, and not
  before: through Spidy's flight the game's air handler runs about twice a
  step (never once missing from a driven step in your October 15 sessions),
  and none of the game's moves is one of its air states, so the handler
  going quiet within 0.4 s of your press is the game's zip or web strike
  under way (`game_swing::AirYield`). Your webs let go then; the swing
  takes you again once you land, the game has you back in the air, or 3 s
  pass. A press the game does not take changes nothing.
- **Pause under SteamVR with a Quest.** There the left menu button is
  SteamVR's own: its Touch profile has no menu button, and SteamVR opens its
  dashboard with it. Now holding both thumbsticks in for 0.7 s is the game's
  Start everywhere (both hands buzz); the menu button stays it too. A click
  of both sticks is still the pad layer, now as you let go. The session log
  says how to pause under SteamVR. The heal reads the stick even while the
  controller's pose is lost for a moment, and a push under way keeps
  through a quarter second out of play.
- **The hand.** Webs leave the suit's nozzle: the hero rig's own webStart
  joint, 2.5 cm further back toward the elbow than before, which was on the
  heel of the palm, where the web shooter's button is ("it comes out of the
  button"). Web balls and gadgets leave 2 cm past it (8 cm, inside the
  curled fingers, before); punches land where they did. The two middle
  fingers of the web-shooting hand reach the palm (they stopped 2 cm short:
  "the fingers ain't touching the button"), and a closing hand gathers its
  fingers instead of keeping the rest pose's spread, which fanned the fist's
  tips 4 cm apart ("when your hand closes your hand looks off"). The aim is
  unchanged: along the hand, as you set it on October 15. One player aiming
  along his wrist with the thumb up saw webs go low: Meta's aim ray is 30
  degrees off the hand toward the thumb, low with the thumb up, inward with
  the palm down.
- **IMPACT WEB fires the Impact Web.** The wheel's IMPACT WEB was the game's
  charged Web Shooter gadget: the class named WeaponImpactWeb is what the
  game's HUD calls "Web Shooter" (your October 15 probe's screenshot), and
  the Impact Web is WeaponWebBlast (the game's text: "the Web Blast gadget
  (aka Impact Web)"), which the wheel never listed. Now IMPACT WEB is the
  Impact Web, and the charged web ball is not on the wheel (its top gives
  web balls already).
- **Pulls take only enemies who are free to fight.** A web dragged thugs out
  of a car in a chase (the game's takedown never finished, the crime never
  ended and the car drove on by itself), and pulled hurt police officers
  sitting in a burning car around the street. A bot in one of the game's
  states in which something else moves him (attached to an actor - a car's
  seat -, carried, a cinematic's, another's synced animation as its guest,
  a slotted interaction, frozen, waiting for the game's own takedown yank)
  is not taken by webs, web balls, web strikes, thrown bodies or fists.
- **"Different bridge build is resident."** That was a game still holding
  another Spidy build (left running across an update, or VR started from
  another Spidy folder). The session now says so plainly before anything
  starts, the launcher offers to close the game (as its close button does:
  it saves as it quits) and start VR again, and the other folder's saved
  game settings go back first. The render memory warning says it is the
  graphics card's memory, not the PC's.
- **The display driver error as VR starts (RTX 4070 SUPER, 0x887A002B).**
  The cause is not known. The game lost its graphics device within the
  first 19 eye pictures, every session, on Virtual Desktop and SteamVR
  alike; its game log lists the one card twice. VR sessions now turn
  NVIDIA Reflex off (back after the game closes): the headset paces the
  game, and Spidy holds back the window's presents beyond the monitor's
  rate since October 11, which Reflex was never run with. When the game's
  log says the device was lost, the session log names the code and what to
  try (the launcher's "Normal camera on the monitor", which keeps the game's
  own view and its window as they were; the game on the main display; the
  newest driver), and the report lists the adapters the game saw.
- **Protocol:** XrData version 37, same size (the head clearance in the
  slots of the room around the player); the swing command's spare field is
  `asks`. Deploy the whole package.
- **Checked:** 343 core tests (new: the clearance alone, the rays, leaning
  into Spidy's wall, the head after a break; the game's own move taking the
  hero; the pause hold, the pad layer on release, the game screen's L3 + R3,
  the heal through a moment out of play; the fist's and the shooting hand's
  fingers side by side, which fail without the gather; held bots), the
  launcher's, the GPU test, all six protocol suites. **Not run in the game,
  not tried in a headset.**

**Earlier build (October 15, late night): one line round an edge, and the
crash.** You: "game crashed. also when web goes around edges a second line is
drawn from the original edge and the hand".

- **One line round an edge.** A web that bends round an edge is drawn with the
  game's own rope: Spidy lays its first points, your web shooter and then each
  edge, and the game draws the rest to where the web sticks. The game draws
  that rest from the rope's start, which was still your wrist, not from the
  last edge: a second line ran from your hand straight to where the web stuck,
  through the building. Now the rope's start is the last edge while the game
  builds it, so the line goes on from there and only from there.
- **The crash.** Your 17:43 session stopped 3 minutes 42 seconds into VR,
  mid-swing, a tenth of a second after your right hand's web took an edge.
  The game wrote no crash report of its own and Windows recorded none: it
  simply ended, with 12 GB of memory free and nothing wrong in the frames
  before. At the moment a web takes an edge, that edge lies on the line from
  your wrist to where the web sticks, so the rope went out to the edge and
  straight back along itself to your wrist: a rope folded back on itself,
  where drawing a rope as a tube is likeliest to break. With the second line
  gone, so is the fold. That the fold was the crash is my best reading, not a
  measurement: the game left nothing to read.
- If Spidy cannot lay the rope's points (the game's hook missing), a web round
  an edge is drawn to the nearest edge as before tonight, never straight
  through the building to its end.
- **Checked:** 337 core tests, all six protocol suites. **Not run in the game,
  not tried in the headset:** you are trying it. A new probe,
  `tools/probe_web_bends.py`, lays one of the game's ropes round edges in the
  running game (the moment a web takes an edge among them) and reads back
  what the game built; it has not been run yet.

**Earlier build (October 15, late night): punches hit 30% softer.** You: "lower
the damage by about 30% i want enemies to last longer".

- A punch now deals 7 to 28 of the game's hit points, by how fast your fist
  goes (10 to 40 before; a street thug has about 50). A web strike's blow is a
  punch, and the combo, called blows, slams and finishers multiply it, so all
  of those are 30% softer too. How hard a thug flinches, falls or flies still
  comes from your fist's speed alone. Webs and thrown enemies are unchanged.
- **Checked:** the core tests. **Not tried: in the game or in the headset.**

**Earlier build (October 15, late night): a hand on its web is a fist.** You:
"when web connects and held lets change the hand to fist and not keep the
horn pose".

- **The web-shooting hand is for the shot.** The two middle fingers still
  come down as the web button goes down, and the hand is the web-shooting
  hand as the web leaves your wrist. Once the web holds, 0.15 s after the shot
  (while it is on its way to what it sticks to), the first and little fingers
  and the thumb close over: a fist on the web, for as long as you hold the
  button. The same for a web that caught a thug or a prop, and with WEB
  BUTTON: TRIGGER (the hand closes though the grip is open).
- A web that misses keeps the web-shooting hand while you hold the button,
  and a web ball still shows it for half a second.
- **Checked:** 337 core tests (one new: a web that holds, the pose whole
  0.1 s after the shot and gone by 0.35 s, the fist closed and still closed
  five seconds on; the same with the trigger as the web button; a web that
  missed keeps the pose), the launcher's, all six protocol suites. **Not
  tried: in the game or in the headset.** If the fist comes before the web
  lands, or after, that is one number (`HandPoseTuning::connectSeconds`).

**Earlier build (October 15, late night): no pull toward the web's end, near
misses over the street, and webs drawn round corners.** You, after the build
below: "no swinging it too springy and throw me too much", "and the wind is
too strong probably", "i want the near misses", and "when bending around edge
the rope origin point seem to teleport to the new edge (its been like that
for a while)".

- **Nothing pulls you toward the web's end any more.** The build below wound a
  web in wherever its swing would bottom out near the street, from the moment
  it was shot. In your 16:58 session that was half of all your swinging: the
  ropes took in 1,340 m that way (47 m in the session before), you were pulled
  ten times as hard as before in a typical moment (10.6 against 0.9 m/s^2 past
  gravity) and swung at 26.5 m/s where you had swung at 17. That pull was the
  wind and the throwing. It is gone.
- **Near misses.** A web in the air still never lets you hit the street: as
  you come down to it, your fall is turned along it, keeping your speed, so you
  skim it with your feet 0.4 m up and swing on. Nothing winds the web in for
  it; it only acts in the last few metres.
- **Your arms pump nothing.** The rope takes in only the slack your body makes
  (a zip, a jump, swinging past close to the web's end). The slack your hand
  makes reaching toward the web's end it keeps, and your hand takes that up
  first as it draws back. Taking all slack in, the build below climbed you up
  the web with every reach and pull of an arm.
- **Webs round corners stay on what they stuck to.** A web that bends round an
  edge was drawn only as far as that edge, its end jumping from the building
  to each new edge. The game's rope now goes from your web shooter round each
  edge to where the web sticks.
- **Checked:** 336 core tests, the GPU test, all six protocol suites. In a
  stand-in for the game's mover at your session's frame times: a swing that
  bottomed out 6 m under the street skims it with the feet 0.4 m up, at
  16.4 m/s (an unhindered swing: 20.2 m/s, through the street), at most
  1.2 m/s of change a step; a fall past a web's end is caught by 1.3 m/s a
  step (10.5 m/s with the webs before today). An arm reaching 0.4 m toward the
  web and back five times moves the body 4 cm a time. **Not tried: in the game
  or in the headset**; the corners' drawing is in the game's own rope update,
  which no test reaches.

**Earlier build (October 15, night): webs from the web shooter, aimed where
your hand points, and taut in the air.** You: "lets make the webs come out of
the webshooter and something is off with the aim, it feels off to the side a
bit. also webs are too strechy when you're in the air with them it makes it
hard to swing and feels like hitting the ground when i shouldnt".

- **Out of the web shooter.** A web started 3 cm inside your fist, by the
  knuckles: in your 15:53 session's pictures of your hands it comes out
  between the fingers. Now webs, web balls and the web you hold in both hands
  leave Spider-Man's web shooter, on the palm side of his wrist, where your
  body puts that wrist (8 cm behind the controller's grip).
- **Aimed where your hand points.** Webs, web balls, catching props and
  thugs, gadgets and pointing at the game's interactions all went along the
  controller's aim ray. Meta's runtime tilts that ray 30 degrees from the hand
  toward the thumb, and Virtual Desktop copies Meta's poses (its source
  records them). Held palm down, as a web is shot, that is 30 degrees inward
  of where Spider-Man's hand points: the right hand's webs went left of it,
  the left hand's right. Now they go straight along the hand, from the wrist
  out past the knuckles, the way his fingers point, and the aim markers with
  them. It is a 30 degree change: if it now feels high or low, that is one
  angle to set.
- **Taut in the air.** In the 15:53 session most webs were 100 m webs in open
  air, and whenever you came nearer a web's end (a zip, a jump, flying up) it
  went slack, 5 to 50 m at a time, and you fell free until it caught you again
  with a jolt. Now a web in the air takes its slack in as you come nearer its
  end: it holds you from where you got to, and the swing starts there. On the
  ground and on walls a web keeps its length, and pays out as you walk, as
  before.
- **Swings clear the street.** A web whose swing would bottom out within
  2.5 m of the ground under it (your body's middle; your feet 1.5 m up) winds
  in until it does not, from the moment it is shot: speeding up gently, never
  a yank, and slowing as it gets there. In the session a 100 m web held you
  2 m over the street, and 26 to 36 m webs ran you along it. While you hold a
  web you stay off the ground; let go of it to land.
- **Punches** still land with the knuckles: the fist is measured 13 cm along
  the hand from the web shooter, 7 cm from where it was.
- **Checked:** 334 core tests (5 new or rewritten: where the web shooter is
  and where it aims; a body thrown at its web's end hangs where it got to; a
  swing kept off the street; and both with a stand-in for the game's mover at
  your session's frame times), the GPU test, all six protocol suites. In that
  stand-in, a body falling past its web's end was caught at 10.5 m/s in one
  step by the old web and changes speed by 1.3 m/s a step at most with the new
  one; a swing that bottomed out 6 m under the street stays 5.8 m over it
  (19.4 m/s at the bottom, 20.2 m/s before); a low shot at a far building,
  23 m under the street before, stays 2.1 m over it. **Not tried: in the game
  or in the headset.** How the webs feel and where the hand aims them are
  yours to try first.

**Earlier build (October 15, evening): the game screen from the pause menu.**
You: "move the VIEW: GAME SCREEN to the first menu (the one with resume)",
and "its the same menu as the ones spidy settings are in just not the same
depth".

- **SWITCH TO GAME SCREEN, under RESUME.** The pause menu's first page now
  has it right under RESUME: A plays the game as it is on the 3D screen, and
  the line then reads SWITCH TO VR, which A takes back to VR. It is no longer
  in Settings > SPIDY VR. As you said, the page is built from the same kind
  of items as the Settings tabs; it shows them as buttons, so this is a
  button that says what it does next.
- Measured in the game, without a headset: the line under RESUME, A turning
  it to SWITCH TO VR (the view's setting to GAME SCREEN) and A again back;
  the SPIDY VR tab without it, every one of its other rows still changing,
  resetting and RESET ALL as before (tools/probe_menu.py, brought up to date:
  it still stepped through STAND ON WALLS, gone since October 14).
- `tools/probe_menu_trace.py` records the menus' calls into Flash and its
  callbacks, which is how the page was found.

**Earlier build (October 15, afternoon): hands of your own, one web through
both hands, webs always physical, the game on a 3D screen, Epic copies.** You,
on October 15: "Grabbing webs with both hands makes the section between the
hands change shader", "Hand poses for web shooting (and in general) seem to be affected by some
animations which is bad", "remove non physical web, no need for it", "remove
old hands graphics", "add Epic Games Store support (or remove blocker)", and
"add a pause menu button\toggle to switch between full vr mod and normal
version in a virtual desktop (with 3d depth) and normal game conrols and
mechanics".

- **Your hands are never the game's.** Spidy bent only the finger joints past
  the palm, and only while a hand made a fist or shot a web. The rest of the
  hand was the game's animation: the bones in the palm, how far each finger
  spreads and twists, the thumb, and the whole hand whenever it held nothing
  (pokenate28's "the hands stay fists after web swinging until i jump" was
  the game's fists). Now every frame starts the hand from Spider-Man's own
  hand at rest, held to your wrist, and bends it from there: open when you
  hold nothing, a fist, or the web-shooting hand. In the test, a game hand
  with its wrist bent, every bone in the palm spread and twisted and every
  finger joint and the thumb turned off its hinge comes out the same, joint
  for joint, as a hand from rest.
- **A controller the headset loses keeps its arm.** Out of the headset's view
  (behind your back, close to your face) the arm went back to the game's
  animation until the controller came back. Now the hand stays where it was
  last seen from your body, moving and turning with it, and opens.
- **No more red gloves.** Spidy's own gloves showed whenever the body was not
  on the hero for a moment: the first frames, a respawn, a hitch of the game.
  They are gone, so in such a moment there are no hands.
- **The web between your two hands is the game's.** That piece was Spidy's
  own strand, flat colour drawn over the game's picture: another material,
  drawn over your fist, in full colour in slow motion. Now the game's rope for
  that web leaves your lower wrist, bows as far as the web between your hands
  is slack, and runs through the upper wrist on to where it hangs. It is still
  one rope, so taking hold makes no shot's sound. Spidy hooks the game's own
  step that lays a rope's points at the hand (95f9c0) for it; where that hook
  cannot go in, Spidy's strand draws the piece as before.
- **Webs are always physical.** PHYSICAL WEBS is gone from Settings > SPIDY VR
  and from the launcher's options: a web always bends round an edge, is held
  by your hand and pays out as you walk.
- **VIEW: GAME SCREEN.** VIEW in Settings > SPIDY VR now switches between VR
  and the game as it is: its own camera on the game screen in the headset, in
  3D at GAME SCREEN 3D's depth as cutscenes are, Spider-Man and his webs the
  game's, and the controllers an Xbox controller. Two controls the Touch
  controllers lack: hold the menu button for the game's Back (the map; a tap
  still pauses), and hold the left stick's click while you push the right
  stick for its D-pad (down heals). The left stick's click alone is still the
  game's L3, and both sticks clicked its L3 + R3. VIEW: VR goes back. Until
  now that view was flat, and only a real gamepad played it.
- **Epic Games Store copies.** The launcher took only Steam's file, by its
  checksum. A copy of the same version (4.630.0.0) that is not Steam's file,
  as the Epic Games Store's is, is now tried: the launcher marks it yellow,
  and a session starts it through the Epic Games Launcher when Steam has no
  copy. Whether VR runs on it depends on whether the code in Epic's file sits
  where it does in Steam's. Each of Spidy's modules checks the game's code
  where it works with it and refuses where it differs, so if it does not, VR
  stops with a code in the session log instead of misbehaving.
- **`-NoPromptHelp` started no VR**: its switch set a bit the XR module's
  start refused (code 1001). Fixed.
- **Gadgets: a wheel of your own.** Your Blade & Sorcery wheel ("wheel
  shows im front of arm while holding button, motion with arm selects and
  releasing sets seleciton"): hold X and a ring of your gadgets opens in
  front of that hand (the right one on a Steam Frame, where X is), each with
  its charges; move the hand toward one and it lights, with a tick; let go of
  X and it is equipped in the game - its own HUD shows it - and your free
  hand's trigger fires it from your hand. The web shooter at the top gives
  you web balls again, as before (the wheel starts there). A tap of X is
  still the game menu, sent as you let go.
  - The gadgets are the ones the game's own wheel offers, read from your
    inventory, equipped the way its wheel equips them, and fired through the
    gadget's own fire, aimed where your hand points; the game takes the
    charge. A gadget with no charge left does not fire.
  - Measured in the game, without a headset, on your save: Electric Web, Web
    Bomb, Trip Mine, Spider-Drone, Suspension Matrix and Impact Web each
    equipped (the HUD followed), fired once from the hand and gave up one
    charge (4 to 3, Impact Web 10 to 9); Concussion Blast had none left.
    The gadget you had equipped was equipped again afterwards.

**Checked:** 328 core tests (new: the game's animation changes nothing in a
hand, a lost controller's arm, the game screen's two controls, the rope's
points between two hands; rewritten: the
open hand and the fist start from the body's own hand; the SPIDY VR rows
without PHYSICAL WEBS), the launcher's, the GPU test, and the Python checks
(new: an Epic copy started by its launcher's link when Steam has none, a game
in another Steam library, the Epic manifest). With the hand's reset turned
off, the new hand test fails as it should.

**Run in the game without a headset** (15:00 to 15:50, fresh games each
time): the body probe put every finger joint of the fist and of the
web-shooting hand at its angle and the wrists on their targets, the fist
clean in the eye image; the web probe with a web in both hands drew the
game's own web from the lower wrist, bowing, through the upper one (its
start on the upper wrist in all 271 frames); the shooter probe still fired
web balls 8 of 8 from the hand; the gadget probe is above.
**Not tried in the headset:** the hands' feel, the wheel itself (its look is
in the GPU test's image), and VIEW: GAME SCREEN, whose gameplay only a
headset session reaches. The web probe's command is version 2 (the web held
in both hands), and `probe_web_frames.py --held` holds one that way;
`probe_gadgets.py` equips and fires each gadget.
No one with an Epic copy has tried it yet.

**Earlier build (October 15, noon): the vent from where you stand.** You, on the
build below: "didnt work, couldnt trigger it to ask for interaction". Your
session (`game-vr-20261015-111549`) had you on Spidy's wall up the lift shaft,
18 s of it at the vent's height, 32 presses of Y on that wall, and the game
listing nothing near you the whole time - so no ring, and nothing for Y to use.

- **The vent was never listed.** The game lists an offer near the hero (within
  3.35 m) when its own check says yes - which Spidy answers as the crawl would
  where you stand, and your feet were 2 m below the vent, out of its 1.25 m
  reach - or when two more checks of its own on the real hero say "show it, not
  yet usable", which a hero who came up the wall from below never passed. My
  test runs had all climbed to the vent on the game's crawl first, and that is
  why they listed it. Spidy now lists every offer the game checks for you,
  whatever it answers.
- **Usable from a couple of metres, not only from on top of it.** For the one
  you point at, Spidy tries the wall around it, with the game's own check, for
  a spot where the game says yes - within 2.5 m of you - and the use puts the
  game's crawl there. From where you were that is a metre up the wall, to
  where the game's own crawl stands at that vent.
- **Spidy's Y came too early.** The game raises its "[Y] Enter" in the same
  frame it picks the vent, and a Y in that frame was lost - and took the prompt
  with it, so no Y worked after it until the hero walked out of reach and back.
  Spidy now presses 0.3 s after the game's pick, and once more if the game
  still holds the vent 0.8 s later.
- The report (XrData 36) also carries the offer module's own counts: the
  updates it ran with the crawl's mask, the offers the game checked for you,
  the spot searches and the spots found, and Spidy's second presses.

Measured in the game, without a headset, on three fresh games where the hero
came up the lift shaft on Spidy's wall from below, the vent 2 m above his feet
as in your session: the vent was listed, out of reach; the search found the
spot the game's own crawl uses at it (1.1 m below its middle, 1.15 m up the
wall from him); the use had him there on the game's crawl in 0.1 s, and the
game picked the vent and raised its prompt 0.1 s later. Pressed in that same
frame, Y did nothing, and no press after it did either. Pressed 0.3 s or 1 s
after the pick, the game's "[Y] Enter" came 16 ms after the press and he went
into the duct. Not yet seen in a headset: the ring and the reach from a real
wall walk.

**Earlier build (October 15, late morning): Spidy's own walls everywhere; point
at a vent and press Y.** You, after the morning build: "feels really bad, and i
oobed in like no time without the option to get back. can you not just use our
locomotion system only and override the games state to what we need (make the
game think we're crawling when on a wall when on a wall so that interactions
work)", and then "why not allow me to point with my hand to select
interactions?"

- **Every wall is Spidy's again.** Walking at a wall from the ground takes you
  up onto Spidy's own wall (the game's jump, then Spidy's hold), as swinging
  into one does. Stopping on a wall no longer hands you to the game's crawl,
  and the stick up a wall no longer runs the game's own wall run. Those
  hand-overs between the game's crawl and Spidy's wall were what felt bad,
  and one of them put you out of the world.
- **The fall out of the world.** In your session (`game-vr-20261015-094756`,
  55 s in, the lift shaft) the game was turning you from its crawl (its wall
  state `0x80a6`), with your body on the wall's surface, inside its collision.
  The stick pushed at the wall from there, and Spidy's ray met the wall's back
  face, turned toward the body: Spidy took that face as a wall (its normal
  pointing into the shaft's wall, you 0.6 m behind it) and held you on it, on
  the wrong side. Spidy now takes no wall while the game holds you in one of
  its own wall states, and for a second and a half after the game's crawl has
  held you on a wall it never takes that wall's face met from behind. The one
  way off the game's crawl onto Spidy's wall is the walk on from it, which
  takes the crawl's own wall, found from the outside.
- **Point at a vent or a door, press Y.** On Spidy's wall the game offers
  nothing - it counts you as in the air - and found in the game, three things
  shut it: the hero's offer list takes only the air's kinds there, each offer
  judges your place from the hero's record (0.9 m off the wall), and a vent
  also wants the crawl's state on you. While Spidy's wall holds you, Spidy
  now answers those three as the game's crawl would where you stand; nothing
  else in the game sees a change. The game still picks nothing for you there
  by itself, so you pick: point either hand at the thing, and a turning ring
  shows round it in that hand's colour, with a tick in the hand, when you can
  use it from where you are; a faint ring when you need to come closer. Press
  Y: the game's own crawl takes you onto the wall there (its own way onto a
  wall, under a fifth of a second, no drop), the game picks the thing itself,
  and Spidy presses its Y for you. It is the game's own vent entry from its
  own crawl. Your stick waits until that is done; if the game will not take
  you, you stay on Spidy's wall.
- **Vents and ducts.** Inside one the game crawls you, as in the plain game:
  no mount and no Spidy wall in there, its shafts included. Stand up or jump
  out at its end and every wall is Spidy's again.
- The session report (XrData 35) counts the frames a hand pointed at
  something usable or out of reach, the presses that asked for a use, the
  ones the game refused, the ones that reached its Y (and whether it had
  picked the thing first), and the ones given up.

Measured in the game, without a headset: at the first mission's vent, from
Spidy's wall, the vent came up usable, the use was asked for, the game's crawl
had the hero 0.1 s later, the game picked the vent 0.1 s after that, and its Y
took him in: he ended on the duct's floor crawl, inside. Then 1,683 samples of
crawling the duct with the stick sent to the game and to Spidy as a session
sends it: no Spidy wall and no mount in any of them. In free roam, walking at
a wall from the ground: Spidy's wall had him 0.23 s after the stick, and he
walked up it at 6 m/s, 0.95 m off it. Not yet seen in a headset: the rings,
the reach from a real wall walk, and how it all feels.

**Earlier build (October 15, morning): the stick goes where you push on a wall,
the duct keeps its roof, and the view turns about your eyes.** You, after the vent
worked: "transition between crawl and standing is a bit awkward, in crawling is
still see some of the body and in the vent im move really slow sometimes like im
stuck or blocked by something and can clip through the roof sometimes". Your
session (`game-vr-20261014-195702`) showed all four.

- **Slow or stuck on a wall: the stick went the wrong way.** On the game's
  crawl with the stick pushed, your median speed was 0.67 m/s against the
  crawl's 2.6, and the game often moved you sideways when you pushed up. Spidy
  laid the stick onto the wall in the wrong frame. I measured the game's own
  rule on a building's face with its camera turned seven ways: the crawl takes
  the stick along the game camera's look laid onto the wall, or along the
  camera's up where the camera faces the wall too squarely for that, and right
  is the wall's own right of that. Spidy now sends the stick in that frame.
  Checked in the game on the same wall, eight directions per camera angle,
  each sent both ways: the new rule went 29 degrees off on average at the
  crawl's full speed; the old one 61 off, as far as 141 (backwards), and some
  pushes crawled at 0.2-0.7 m/s. In the lift shaft and in a vent's shaft,
  where the camera looks up the shaft beside you, the new rule sends "up" as
  up; the old one sent it as a push to the side.
- **Through the roof in the duct.** Part of the first mission's duct has a
  roof the game gives no collision, so the ray that keeps your head under a
  roof found nothing there and your head stood at your own height, out through
  it (your eye snapshots at 56 s and 141 s). The game crawling you on a floor
  now counts as a crawl too (`game_swing::Surface::floor`: its crawl flag, the
  hero upright), and after 0.4 s of it your head goes no higher than a crouch,
  a metre over the floor, whatever a ray finds - the game only crawls you on a
  floor where you cannot stand. A crawl over a wall's top onto a roof stands
  you up within 0.3 s and never reaches it.
- **Some of the body while crawling.** In the duct the game's crawl laid your
  legs out in front of you (your snapshot at 136 s): the body only planted its
  feet on walls, and on the duct's floor it kept the game's crawl stance. It
  now stands on the duct's floor as it stands on a wall, its fingers its own,
  and under a roof that holds your head down its legs kneel - knees down,
  shins back along the floor behind you - where a squat would have folded the
  knees up in front of your eyes.
- **The awkward turn between a wall and a floor.** Three things. Twice in
  your vent the view turned onto a shaft's wall, snapped level for a frame and
  turned again - a frame the game or the headset could not give (the gate's
  `tracking`) reset the view to level. A break shorter than half a second now
  keeps the view standing where it stood. The view used to turn about your
  feet, swinging your eyes round an arc as high as you stand; it turns about
  your eyes now, your eyes going straight to where they stand on the new
  floor, the two ending together in the same sixth of a second. And at a
  shaft's top the view stayed on the wall a third of a second after the game
  had taken you over the lip, then jumped to you: the hold kept for the
  game's turns now follows your feet, and only while you are at rest.
- The session report (XrData 34) counts the frames play could not place and
  why, the frames the game crawled you on a floor, held your head there and
  knelt the body, and the game camera's look and up.

Not yet seen in a headset: the view's turn, the duct's roof, the kneeling body,
the stick on a shaft's wall in a vent.

**Earlier build (October 14, evening): stop on any wall and the game holds you;
push up it and you run.** You: "fixed nothing still body in front of me", "wall
walking is the same functionality ingame wallrunning, wall standing is like wall
crawling. there is no way it cant be fixed properly", "i dont want it to be lying
at all", "I want the wall walking to feel like it and for me to be able to enter
the vent".

- **The body lying in front of you.** My last build handed the body the wall's
  quarter turn twice on the game's crawl: once as the hero's own frame (the
  crawl turns him onto the wall) and again as the view's tilt, so it lay along
  the wall in front of you. On Spidy's own wall the same tilt counted as "in
  the air" and left the legs in the game's pose. The body is now told the
  surface you stand on - its up and a point on it, exactly the floor your view
  stands on - and stands along it with both feet planted on it, on any wall,
  whoever holds you there (`native_body` command 4, `body::Targets::floor`).
  The new test (Spidy's wall: hero upright, you on the wall) fails on the old
  code and passes now.
- **Why the vent ignored you on Spidy's wall, found in the game.** The hero's
  offer processor only takes offers of the kinds its mask allows: every kind on
  the ground, `0x23` on the game's crawl, `0x2` in the air - and Spidy's wall is
  the air to the game. The vent (kind `0x1`) also checks for the crawl itself.
  Read live at the first mission's vent: on the crawl the vent was its best
  offer; off it, the mask went to 0, then 0x2.
- **Stop on Spidy's wall and the game's own crawl takes you, right there.** The
  game has its own way onto a wall (its transition manager's crawl entry, the
  one its wall grabs use). A quarter second after you come to rest on Spidy's
  wall, Spidy asks it for the crawl at the floor your view stands on, holding
  you until the game has begun, then letting go. Measured: the crawl had the
  probe in 0.17 s at the same height, no drop (taken 1 of 1); the vent became
  its best offer; Y took it into the duct, exactly where the walked-onto crawl
  goes (-528.04, 92.0, -586.93). If the game doesn't begin within 0.4 s or you
  drop half a metre, Spidy has you back and doesn't ask again until you move.
- **Walking up a wall is the game's own wall run.** On the game's crawl,
  pushing the stick up the wall now holds the game's RT for you: its wall run,
  about 7 m/s up (52 to 58 m in 0.76 s, measured with Spidy's swing running and
  staying out of it the whole way). Let go and the game stands you on its crawl
  again at the new height. Across or down a wall you move at the crawl's own
  speed (about 2.6 m/s). A wall you swing onto is still Spidy's, for fast
  running, until you stop.
- **No drops on the way.** Dead ends measured today and not shipped, each of
  which dropped the probe or worse: the game's direct warp into its crawl
  (held at once, then crashed the game's audio as soon as the crawl moved); the
  game's own let-go of its crawl (pushes you 1.3 m off the wall for 0.35 s,
  then you fall - nothing can hold that body); Spidy taking you back the
  moment you let go of a run (fell 11 m); and taking the game's jump off the
  crawl for its run (fell 43 m - the run now begins only from your stick up
  the wall). The final binaries ran the whole thing end to end: jump off the
  crawl (caught by Spidy's wall at once), rest (the crawl, no drop), up to the
  vent, Y, into the duct.
- **Your view through the switches.** Spidy's wall and the game's crawl now
  put your floor on the same point of the wall, and the view keeps its wall
  for up to 0.4 s while the game turns the hero between its states, so neither
  hand-over moves or tilts you (your session had a 48 degree dip there).
- Also: the swing and movement bridges start again after a stop (a second VR
  session in one game process used to get no swinging at all), and the session
  report records every hand-over (XrData 33: crawls asked, taken, declined,
  frames of the game's run, frames the view held through a gap).

Not yet seen in a headset: all of it.

**Earlier build (October 14, later): no more out-of-bounds, and no crawl on your
legs.** Your first headset session with the walk/swing rule found three things.

- **"I then swung again and got oob completely."** My doing. Jumping off a
  wall you had walked onto (the game's crawl holds your body right on the
  wall's surface), Spidy's wall caught you on the same wall - but its ray
  started inside the wall's surface and recorded the wall facing backwards:
  normal (+0.71, 0, +0.71) on a wall facing (-0.71, 0, -0.71), your body
  "0.5 m behind" it. Your body was fine; the view stands you out along that
  normal, so it was placed through the wall, into the void. Spidy now refuses
  any wall the body is behind (`Swing::joinWall`; the test fails without the
  guard and passes with it).
- **"With crawling animation playing."** On every one of 1,256 crawl frames
  the body's legs fell back to the game's pose - the crawl. The torso, head and
  arms were already yours; the legs were only "standing" if the game's own
  feet were within a few cm of the floor, and the crawl's never are. On the
  game's crawl the feet are now planted flat on the floor under the hips, the
  game's legs ignored (`body::Targets::plant`). Tested on both test rigs, not
  yet seen in a headset.
- **"Swinging and attaching to wall from swing looked like wall walking but
  vent didnt work."** As built: a wall you swing onto is Spidy's, and the game
  registers nobody on it. Measured today, every way of handing you from
  Spidy's wall to the game's crawl fails - held against the wall the game
  never grasps you; pressed into it at 3 m/s it grasps for 0.5 s at the vent
  and lets go, and on a plain wall doesn't grasp at all. So the vent needs a
  wall you walked onto - or "Wall running" off, which makes every wall the
  game's crawl (now with your view and a standing body).

Also measured in that session: the game's crawl reads the stick 15.5 degrees
off the axes Spidy lays into the wall, the same on every sample (agreement
0.999 over 26). Left as is for now - one wall, one session.

**Earlier build (October 14): walk onto a wall and the game holds you; swing
onto one and Spidy does.** You: "the vent in the top is the target, there are
similiar events in the game with vents outside. its not even prompting me to
press anything in walking mode which means that its not registering me", and
"solve it end to end with no regression".

**What was wrong.** A wall Spidy holds you on is one the game calls the *air*,
and the game registers nobody in the air: in every session on Spidy's wall at
the first mission's vent the game raised no prompt at all. It can't be handed
your body in midair either - the game counts its own fall from the moment you
left the ground, right through Spidy's whole climb, and its first step of its
own falls at **42 m/s** (measured at that vent: vertical speed -0.1 to -42.1 in
one step). Every hand-over tried today went through that, which is why each one
dropped you. And walking up into the vent's own opening breaks Spidy's wall,
which is very likely your "wall walked to the vent and fell".

**What changed.** Who holds you is decided where it costs nothing - on the
ground - by how you got onto the wall, not by where you are:

- **Walk onto a wall from the ground, and it is the game's own crawl.** Spidy
  no longer jump-mounts it (`SwingConfig::mountFromGround`, off). The crawl is
  the only hold the game calls standing, so every vent, door and prompt on that
  wall works, anywhere in the city. Your view still turns so the wall is your
  floor, your body still stands on it, and your head stops short of a vent's
  roof - the work from this morning, which only ever applies on the crawl.
- **Swing or fly onto a wall, and it is still Spidy's**, as fast as before.
  That is where the game's own wall state used to freeze a swing for 2-3 s, so
  it stays Spidy's.
- **Nothing ever changes who holds you in midair.** The indoors rule, the
  interact hand-over and every release from Spidy's wall are gone.

**Leaving a wall you walked onto** needed two fixes to stay as quick as before:

- A web from the game's crawl was refused: its "clear line from the body"
  check started on the wall's own surface. It starts a body's width out now.
- The takeoff's A alone does nothing on the game's crawl - every stick
  direction only leaps you *along* the wall; B, the bumpers, the triggers and a
  held A do nothing at all. Only the game's **RT + A** leaves it (in 0.09 s).
  So any jump while the crawl holds you - yours or a web's takeoff - goes out
  as RT + A, with RT already down from the moment a web holds. And the takeoff
  counts you off once you are 12 cm clear *of the wall*, not 12 cm above where
  you started.

**Measured in the game, the whole path, with Spidy's swing running and walls
on** (`tools/probe_vent_walk.py`, profile 3, twice): forward from the load
point, up the 42 m shaft - **1,265 samples on the game's crawl, none on
Spidy's wall, Spidy never owning the body** - the vent raised its
`[BTN_Y] Enter`, Y took you into the duct. A web from the crawl 22 m up the
shaft: Spidy swinging **0.16 s** after the web held, the same as a web from a
ledge (0.17 s).

**The trade-off, said plainly:** a wall you *walk* onto now climbs at the
game's crawl speed, about 2.6-3.6 m/s, where Spidy's ran at 6. RT does not make
the crawl run - it throws you off the wall. Swing onto the same wall and it is
Spidy's speed as before.

**Also fixed:** the world-ray bridge could not be started twice in one game
(a second VR session in the same game got no rays); the headroom ray was cast
along the wrong axis on Spidy's wall and crouched your head for 22 frames.

313 core checks, 23 launcher checks, all six protocol suites pass. **Not yet
played in a headset.**

**Earlier build (October 13): the game's own prompts answer in VR.** You:
"there is a repeated pattern of interaction bugs ... most of them mostly rb+lb
or interaction button related during story events or random encounters. I feel
like we're patching stuff but there must be a more systemic way."

There was. These were never separate bugs.

**What they were.** VR play could send nine of the pad's sixteen controls, and
the game picks the one a moment asks for out of all of them. So every story
beat or encounter that wanted one of the other seven stopped a player, and each
was patched on its own after someone reported it: the right stick's click for
the game's LB + RB on October 11 (a door of the first mission), that stick's
pushes for its heal and its LT + RT on October 12 (that mission's heal and its
vent). What play could not send at all, on Touch and Index: **the game's X, its
L3, each bumper and each trigger on its own, and three ways of its D-pad.**
Around 20 of the 83 open topics in #spidy-bugs and #spidy-help were that one
hole - "Can't pull objects with LB + RB (Pico 4 Ultra)", "cant use Vents", "I
can't beat it bc it says tap x repeat but tapping x opens the menu" (in play,
VR's X is the game's Back), "SO MUCH controlls dont work", Air Launch, the Fisk
turrets, the caged civilians, the pigeons. Players were guessing for each
other: "Just click x because the things they want you to click is y to web
strike. Or b."

**What the game already tells us.** It names the button of every prompt and QTE
it raises, through its own UI: `placeablePromptManager` (ShowPlaceablePrompt,
HidePlaceablePrompt, SetPlaceablePromptProgress), `qteManager`
(AddQTE(button, subtype, miscVal, miscValTwo), UpdateQTE, ClearQTE) and the
world markers (POIInteractAvailable, UpdateInWorldPrompt). All of them pass the
one call wrapper at 0x1d1cf30 that a VR session already hooks for the SPIDY VR
tab, and their texts the localized lookup at 0x1749970 that it hooks too. The
names sit in the same table as BotMoverManager, which the enemy scan already
reads. So the vocabulary is the game's, not a list of moments:

- **One table over the icon ids**, whose endings are the same in all nine
  device families (X360IconMap, XOneIconMap, DualShockIconMap,
  DualSenseIconMap, SteamDeckIconMap, SwitchProIconMap, GenericIconMap,
  MkbIconMap, SteamIconMap), plus the letters the texts spell out. A
  PlayStation family's X is the pad's A, and its Square the pad's X.
- **Y sends whatever is asked.** While a prompt is live, the interact button
  sends its buttons - combos and holds included - instead of the game's Y. One
  control for every prompt in the game, present and future, in place of a
  gesture a moment. A pull with both webs at the thing does the same, so the
  gesture stays the first way in and the button is the fallback.
- **The prompt reads in VR terms.** A text that spells its button out is
  rewritten: "LB + RB (Hold) Throw" becomes "RIGHT STICK CLICK (Hold) Throw",
  and where play has no control of its own the prompt says "Y". An icon id is
  read but never rewritten - the ids sit inside the UI's markup, whose form is
  still unknown, and a name changed in there would be markup the game cannot
  resolve.
- **The pad layer closes the hole by construction.** Clicking both thumbsticks
  (free since October 12: neither stick's own press is half of the chord)
  latches the whole controller - grips as LB and RB, triggers as LT and RT, X
  as the game's X, the right stick as its D-pad. It never latches while a web
  carries the player and lets go after 20 s of nothing pressed.
- **A check over the whole pad, not the reported moments.** A test builds every
  combination of the VR controls in both mappings and asserts that each of the
  pad's 18 controls comes out of one of them; it also asserts that the nine are
  the ones play alone never had, and that what Spidy tells a player matches
  what the mapping sends. That test is what would have failed before a player
  found the next gap.
- **The session report counts the asks.** Its "prompts" section carries the
  game's calls into Flash and the prompt interface's among them, prompts raised
  and cleared, texts that named a button and texts rewritten, what it is asking
  for now and last, the asks VR play could not have sent on its own, the
  interact presses that carried one, and the pad layer's latches. The next
  release's list comes from that instead of from whoever happens to post.
- **Keyboard prompts are counted too.** "it says click f to decipher its only
  giving me keyboard prompts" is the game showing the icons of the device it
  last saw; those name no pad button, so they are counted rather than guessed
  at.
- **The whole vocabulary, not the [BTN_*] half of it.** The first pass read the
  twelve `[BTN_*]` tokens. The exe keeps the rest of the alphabet in one place:
  at image 0x387ae40, under the name "CineQTEShotController - Button Prompt
  Location Handles", sit UNKNOWN, `[BTN_A]`, `[BTN_B]`, `[BTN_R2]`, `[BTN_R3]`,
  `[BTN_L1]`, `[BTN_L2]`, `[BTN_L3]`, the four `[DPAD_*]` and `[MKB_CUSTOM00]`
  to `[MKB_CUSTOM08]`; `[LS]` and `[RS]` stand beside it for
  CineQTEShotControllerStickMotionAction. That is the cinematic QTE's own
  table - the prompts of exactly the story beats and encounters you reported -
  and three of its families were unreadable: a D-pad way, a stick the game asks
  you to move, and a key. They are all read now, a stick to be moved carries no
  way (the demand has its own bit for it) and `[LS]` is the player's own left
  stick in VR while `[RS]` needs the pad layer. The exe also puts each token
  beside the thing that raises it: `[BTN_X]` by PileOnJobHost and "Skirmish
  Job" (the random encounters), `[BTN_Y]` by MrNegabig, `[BTN_R1]` by
  ZipToGroundTarget, `[BTN_CANCEL]` by HeroStateDialog - which is why
  `[BTN_CANCEL]` stays unmapped.
- **The keyboard count was looking for the wrong word.** It tested a text for
  "Mkb", the icon families' spelling. The token the game actually sends is
  `[MKB_CUSTOM00]` and up, in capitals, so a keyboard prompt was counted only
  when it came as an icon - and "it asks for F on the keyboard" would not have
  shown in a report at all. A prompt now carries the fact itself
  (`Demand::keyboard`), from either spelling.
- **A probe can hand the controller back.** The virtual pad holds input slot 0,
  and the game stops reading the keyboard once it has seen a pad press, so a
  probe that pressed anything left the player unable to move until the game was
  closed ("i cant control it"). `SpidyPadRelease` releases it. Fixing that
  found a real bug beside it: `install()` skipped every module it had already
  hooked and then called that a failure, so **the pad was dead for the whole of
  a second VR session in one game process** - the first session's end
  uninstalls it. Starting VR twice without restarting the game now works.

**And then the vent itself, in the game.** Driven there with the pad through
SELECT PROFILE into profile 3 and straight up the Fisk Tower elevator shaft,
with no headset. The vent's prompt arrives as an argument of
`POIInteractAvailable(5, 1, '')` / `POIInteractAvailable(5, 0, '[BTN_Y] Enter')`
with `POISetButton` beside it, so it says `[BTN_Y]` and the reading gives 32768,
the pad's Y - the same token that draws as **F** on a keyboard. Y works: it was
consumed in the same millisecond it was sent. Then, standing still at the vent
with the interact live, **A alone - no stick - and 63 ms later the game withdrew
its own interact**. It never ignored the button: there was no interact left to
press. Spidy's wall running drives the hero in the game's air state (which
`probe_wall_run.py` asserts), so with walls on the interact is gone before the
player presses anything, and no button of any kind can open the vent. Until the
swing is taught to hand the player back for an interact, **Wall running off**
is the answer at a vent. Details and the timelines: VALIDATION.md.

**And the thing that closed your game, which was not a crash.** You: "Meet
crashed on game load." The session report says otherwise, and says exactly what
happened. The game ran 20 seconds, `game_exited` true and `error` null, the
whole time on its own screen (`presentation: game_screen`, the gate never
opened), and **`tracked` was 0 in all 414 frames: the headset never tracked
once**, its head pose all zeros and both hands identity. Meanwhile
`vr_buttons` shows the player's own Touch **B three times and A twice**, and
`pad_buttons` shows each of them going out as the pad's `0x2000` and `0x1000` -
the last A four frames before the end, followed in the game's own log by
`[Save] Request save type 2` and the process ending. The title screen's **QUIT
GAME** is two moves from the top. The game saved and quit, because buttons
pressed blind while the headset was being put on were driving its menus.

So nothing reaches the game until the headset has given a tracked head pose
once (`game_pad::mappingFor`). Before that the player cannot see anything Spidy
draws, so a press of theirs is a press at a screen they are not looking at;
after that first pose the mapping is exactly what it always was, so a session
that reaches the headset at all is unchanged.

**The fix, in the end, was the player's: indoors the game keeps the walls.**
You: "i also think that wall walking will need to be disabled inside vents, the
camera was fine without it." That turns it from a rule about the press into a
rule about the place, and that is what makes it safe - the walls are off before
you are ever stuck to one, so there is nothing to let go of and nothing to drop
you. Indoors the game's own crawl holds you, so its interacts work, its camera
behaves, and wall running is untouched everywhere else.

`spidy::Enclosure` reads the room from five of the world rays a session already
casts - one up, four along the ground - and counts how many ways the player is
closed in: a ceiling within 4 m, or surfaces within 4 m on opposite sides. Two
of those three is indoors. A vent has all three, a lift shaft its two pairs, a
corridor its sides and its ceiling; an alley with open sky has one, and a
street, a rooftop or a building face none. A reading has to hold 0.4 s before
the verdict follows it, so a doorway does not flap the walls, and the change
waits for a frame where the ground or the game's own crawl has the player -
never while the swing holds him on a wall of its own, which is the drop.

There is no list of places in it. The next vent is already covered.

Measured in the game on the way in to that vent (profile 3, the Fisk lobby and
the shaft above it): **0 ways in the lobby** - nothing within 4 m, wall running
stays on - then **2 ways in the shaft** (north 1.72 m, west 1.74 m) and **3 in
the duct** (north 0.02 m, south 0.30 m, ceiling 0.88 m). The switch happens
while the player walks on the floor, which is exactly the safe frame the rule
waits for. **XrData is version 30, 960 bytes**, carrying `indoor_frames` and
`walls_handed_to_game`.

**The fix that did not work: the interact hands the player back.** Spidy cannot wait for the game
to ask - while it holds the player the game is offering nothing, so there is no
prompt to answer. So the press does it: while the player asks to interact on a
wall of the swing's own and the game is offering nothing,
`game_pad::WallHandover` turns the swing's walls off for 0.9 s.
`Swing::allowWalls(false)` leaves the wall it holds, the game's own crawl takes
him where he stands, its interact comes back and the button it names is already
down. The walls go back on when the moment ends, however it went, and they are
never turned on for a player who set wall running off.

It cannot take anything away, because of when it fires: it needs a press of the
interact, the swing's own wall under the player, and the game offering nothing -
and that is exactly the case where the press does nothing at all today. Where
the game is offering something the ordinary path already works and is left
alone. A press that finds nothing costs its 0.9 s on the game's own crawl, and
the walls come back by themselves.

The report says whether it worked: **XrData is version 29, 952 bytes**, with
`prompts.wall_handovers` (presses that handed him back) and
`prompts.wall_handovers_offered` (those the game answered by offering an
interact again). The second rising with the first is the vent opening.

Measured: 302 core checks (7 new), 116 protocol checks, a clean Observer build.
XrConfig stays at version 23 (option bit 20 leaves the prompts alone:
`--no-prompt-help`); **XrData is version 28, 944 bytes**, so the runner and the
DLLs must match. Run in the game on October 13, headless, on the backup laptop:
`SpidyMenuStart` + `SpidyPromptsWatch` put the reading in with no headset and
the game's UI calls came through it by name - PingPOIs, HideText(1, 1),
ToggleInGameOverlays(0), Refresh - so the route and the call table are live.
What a prompt's own text looks like at an interaction is still unproven: the
vent was not reached (Continue loads profile 1, not the profile the vent is
in), and the pad layer works either way.

**Earlier build (October 13, third build): two starts that could not work,
found in players' reports, and Linux under Proton, experimental.** You
asked: "review discord bugs and find unresolved actionable ones that you
have enough info to fix and fix them", then "release it and handle relevant
topics" and "also review the pr for linux and add it if its ok befoire".
The bug forum's 47 topics and the help forum's 44 were read with the report
files players attached. Two had a cause the launcher can deal with.

- **The game's Aspect Ratio is at Auto while you are in VR.** On October 11
  two players' games ended as VR started, and each had VR once Aspect Ratio
  in the game's display settings was back at Auto, which a third player had
  advised. One of the two named his choice, 16:9, and his report of that day
  has the graphics device gone by the 30th eye picture. A session now puts the option at Auto, as it does
  with frame generation and vertical sync, and puts yours back when the game
  closes (`ForcedAspectRatio` in `tools/vr_display.py`: the option's choice
  counted from Auto, which is all the option reads and writes). The session
  log says so when it changed yours.
- **The R.E.A.L. VR mod in the game's folder is named, and the session
  stops there.** A player's headset stayed black on SteamVR and on Virtual
  Desktop in six sessions over three days. His reports had no picture of the
  game in any frame and could not say why. The folder listing and the game's
  module list he sent on October 13 could: the other VR mod's `dxgi.dll`
  sits beside `Spider-Man.exe`, the game loads it before Windows' own, and
  it takes the game's picture. Spidy now looks at the game's folder as the
  game starts. With R.E.A.L. VR there it says which files to move out, and
  that Steam's file check leaves them. Any other file there that stands in
  for one of Windows' libraries (`dxgi.dll`, `version.dll`, `dinput8.dll`
  and nine more: ReShade, an upscaler swap, a trainer's loader) gets a
  warning by name and the session goes on, since none of those was seen to
  break VR (`other_mods` in `tools/run_game_vr.py`; the files are in a
  failed start's report as `other_mods`).

- **Linux (Steam Proton), experimental: Sebi's pull request 7
  (sjrl2000).** Spidy runs inside the game's Proton session, started by
  `tools/spidy-linux.sh` in place of the launcher. `docs/LINUX.md`, in the
  zip as `LINUX.md`, has the setup and the limits. Two changes in the
  modules made it possible. The search for the game's graphics queue falls
  back to a swapchain on a hidden window where the system has no
  composition swapchains (`src/render_probe.cpp`). And the lock around the
  eyes' recorded draws may be taken again by the thread that holds it:
  under vkd3d-proton Spidy's own calls come back through its hooks
  (`src/native_eye_gpu.cpp`). On Windows Spidy calls the original functions
  for its own lists, nothing comes back, and the lock is as it was. He
  played about 20 minutes on a Quest 3 over Steam Link with 0.3.2 and the
  lock patched by hand.
- **What the review added to it.** The package copied a fixed list of
  tools, so the zip had no starter, and a Windows checkout gave the script
  Windows' line endings, with which bash runs nothing. The package now
  ships it with Unix line endings (`tools/package.ps1`, `.gitattributes`)
  and `LINUX.md` beside it. The notes say `bash tools/spidy-linux.sh` (a zip
  made on Windows keeps no run permission), and that a game Spidy did not
  start keeps its own settings: frame generation, vertical sync,
  Ray-Traced Reflections and Aspect Ratio are set by hand there.

**Checked:** 286 core tests, the launcher's, the graphics test with
Direct3D's debug layer, and 116 Python checks (new: Aspect Ratio to Auto and
back, and what the session says; a clean game folder, R.E.A.L. VR's files,
another tool's, and the session ending before anything of Spidy's is in the
game). The new folder check finds nothing in the game's folder on this PC.
The pull request had not been built with MSVC: it builds here (14.43) with
no new warning. The rebuilt render probe starts, stops and starts again in
a stand-in process, and so does a scratch copy whose composition swapchain
is made to fail, which is the new fallback on Windows' own DXGI. The
package was made and opened: the starter is in it without a carriage
return, and `LINUX.md`. No protocol changed.

**Not run in the game, and not under Proton.** No game was started on this
PC. What a fixed Aspect Ratio does to an eye was not seen here: the fix
rests on those players' accounts and the one report. The player with
R.E.A.L. VR has not yet said that VR shows a picture without it. The lock
change is in the module every Windows session loads; that it changes
nothing there is a reading of the code, not a session.

**Read and left open, with what each needs:**
- *The zip to a point (right stick up) does nothing in the air after a
  swing, or standing on a wall* (three topics). The cause is in the code:
  the stick's push reaches the game only while the swing does not have the
  player (`playing` in `src/game_xr.cpp`), and the swing has him from the
  first web until he lands, and on its walls. Sending it there means handing
  flight to the game in mid-air, which dropped players at the game's fall
  speed until October 7's fifth build. It needs runs in the game.
- *A crash with "Stack overflow" seconds after the game starts* (two
  players, 0.2.14 and 0.3.5). Neither report says where; the game's `.mdmp`
  would.
- *The view sits too high in a vent* (a save and a clip are in the topic),
  *the wall walk falls off a ledge*, *a broken window in the Fisk tower fire
  is a wall*, *Fisk takes no Y*, *the web jumps to its bend*: each needs the
  game run at that spot.
- *"Monitor frame rate" 15 to 45 slower than 60*: one player's numbers, his
  monitor and its sync still unknown.
- *PS VR2 buttons*: no report yet, and Spidy has no layout of its own for
  those controllers.

**Earlier build (October 13, second build): the game window shows Spidy's
combat marks.** You asked: "why are the combat indicators and custom ui not
visible on the mirror on screen? shouldnt it litterally show on e of the
eyes?", and of a fix: "showing the battle ui is most important, i also don't
want to hit performance too much".

Why they were missing. The window did show your left eye, but as the game
rendered it. Spidy draws its own things onto the headset's pictures after
that: the target ring, a call's arrow, the bursts, the count and the
knockout lettering, the aim markers, the focus meter on the wrist, slow
motion's colour, the calibration panel, and the gloves and webs where the
game draws none. The window's picture was taken a step before them. The
game's own HUD is on a panel the game renders into the eye, so it was in
the window already.

What changed:

- **The window shows the eye the headset showed.** Each time the window
  has taken a picture, the headset's loop keeps a copy of its next finished
  left eye, and that copy is the window's next picture (`keepShownEye` in
  `src/native_eye_gpu.cpp`).
- **One copy for each picture the window gets, and nothing else.** At the
  default "Monitor frame rate" that is 30 a second; the headset's frames in
  between are not copied, and in menus, cutscenes and the flat screen none
  are. A copy moves one eye's pixels once on the graphics card (18 MB for a
  2064 x 2208 eye) and one more image of that size stays in video memory.
  The window's own draw is the same as before, from another image.
- **With no fresh copy the window shows the game's image of the eye**, as
  until now: for the first picture after a menu, and when the headset has
  taken no frame for half a second.
- **The window is a little later than it was**: by up to one of its own
  pictures, a thirtieth of a second at 30.

**Checked:** 286 core tests (new: how long a copy stays on the window). The
graphics test, with Direct3D's debug layer, at 1536 x 1536 and at a
headset's 2064 x 2208 (new: a copy taken right behind an overlay that is
still being drawn, from both kinds of headset image, has the overlay's
drawings exactly and leaves the headset's image as it was). 114 Python
checks. No protocol changed: `SpidyPresentCount(3)` is new, for a probe.

**Not run in the game, and the cost is not measured.** No game was started
on this PC. That the marks are in the window is a reading of the code and
the graphics test, not a look at the window. The cost is expected to be
small; there is no number for it yet.

**Not changed:** which part of the eye the window shows (its middle, cut to
the window's shape: about half its height in a 16:9 window), and a window
whose picture is in float (HDR) values, which gets no eye at all.

**For recording, today:** "Monitor frame rate" 60, a 1920 x 1080 window
(the cut from the eye has about that many pixels, a larger window only
stretches it and costs the headset frames), and HDR off in the game if the
window stays on an old picture.

**Proposed, not built:** the cut aimed where you look (it is taken about the
image's middle, and a headset's eye sees further outward and downward than
inward and upward), and a steadied, level camera for the window.

**Earlier build (October 13): no display driver error when a menu opens,
and a Steam Frame player's word on 0.3.3.** You sent the Steam Frame topic of
#spidy-bugs, where pokenate28 had answered 0.3.3's call for testers with two
session reports.

What pokenate28 reported (Steam Frame, SteamVR, RTX 5080, 0.3.3):

- **The Frame's own layout works.** The log has "Controllers: Steam Frame,
  with their own layout", and "controls work and all the button mapping
  seems good so far". Dodge and heal are untried: no fight was reached.
  "hands are normal position".
- **The game ended every time it left VR play**: four times at a police
  beacon, then on X (the game menu) and on Menu (pause), with the game's box
  "A problem has occurred with your display driver ... (0x887A0006:
  DXGI_ERROR_DEVICE_HUNG)".
- **Fists.** "the hands stay fists after web swinging until i jump then they
  open but when i land they close again."

What the reports show about the error:

- Both sessions end the same way. VR play runs (81 s and 31 s). Then the
  game's camera stops: a menu or a cutscene begins. The game draws a few
  more frames, the last of them with 35.4 MB of render memory where play's
  took 21.5 (the game's own view has a job again), and nothing comes after
  it: no frame, no read of the controller. The headset's loop waited 5.1 s
  for the graphics card, and then the game's log has "Device removed
  detected (0x887A0006: DXGI_ERROR_DEVICE_HUNG)".
- The game had **Ray-Traced Reflections on** (Very High).
- domalden's two sessions in the topic that collects this error (RTX 4070
  Ti, Quest 3, Virtual Desktop) end in the same state: VR play, the camera
  stopped, the graphics device gone. Ray-Traced Reflections was on in both.
- Of the 30 game logs kept on this PC, all with it off, none has the error.
- Players had found it themselves. fad_zi on October 11: "CRASH WHEN OPENING
  MAP", then "disabling RT fixed it". _thebloodson_ on October 13: "It does
  this for me every time I try to open a menu", then "I turned ray tracing
  off"; bzmlo there: "it was ray tracing the reason".

The cause, as far as it is known. Since October 10's eighth build (0.2.12)
the game's own view rests while you play in VR: its picture only went to
the monitor. It draws again when a menu, the map or a cutscene opens. With
ray-traced reflections on, the first frame it draws again hangs the
graphics device. Why the game's ray-traced path does that after a rest was
not looked into: nothing here ran the game.

What changed:

- **A VR session turns Ray-Traced Reflections off**, as it does frame
  generation and vertical sync, and puts your setting back when the game
  closes (`RTReflections` in `vr_display.SESSION`). Nothing is lost in the
  headset: the eyes never had them. A player who had them on is told so in
  the session log, and now gets the screen-space kind in the eyes where
  Screen Space Reflections is on.
- **A game that runs with them on all the same** (one the launcher did not
  start, or whose settings Windows kept from it) **keeps its own view
  rendering**, as before 0.2.12, at about a fifth fewer frames. The log says
  so, and the report has it as `eyes.ray_traced`.
- **A Steam Frame's own wireless link is no Wi-Fi to warn about.** The same
  log said "this PC is on Wi-Fi (frame_CBC1602B) as well as on a network
  cable ... Turn this PC's Wi-Fi off". That network is the headset's own:
  the Frame makes a 6 GHz network and the PC joins it through the adapter
  from the headset's box, so turning it off would cut the headset off. Such
  a network is now left out of the warning and named in the report
  (`network.headset_link`); a home Wi-Fi beside it still warns. The form of
  the name is from this one report.

**Checked:** 114 Python checks (new: the setting off and back, and what a
session says it changed; a game with it on keeps its view; the Frame's
network beside a cable and beside a home Wi-Fi). Only `tools\vr_display.py`
and `tools\run_game_vr.py` changed: no module, no protocol.

**Not run in the game.** No game was started on this PC. That the error
ends is three players' word (they ended it by turning the setting off) and
a reading of the reports, not a measurement of this build. A player with
the setting on can check today, on 0.3.3, by turning it off in the game's
graphics settings.

**Not covered:** Ray-Traced Reflections turned on in the game's settings
during a session. The view then rests again, and the next menu would end
the game.

**Open from the same reports:**

- **The fists.** Spidy closes a hand by its grip and otherwise leaves the
  fingers to the game's animation (`body_ik.cpp`: no fist, no change). So
  the game's own hands show whenever the grip is open: closed on the ground
  after a swing and open in the air, going by the description. By the code,
  nothing in that is the Frame's. A hand Spidy opens itself would end it.
  That has to be seen in the game, so it is not in this build.
- **The frame rate in those sessions.** SteamVR asks 3740 x 3740 pixels per
  eye for the Frame, and Upscaling was off: 28 million pixels a frame, 22.6
  new pictures a second on an RTX 5080, with 12.5 of 13.6 GB of video
  memory in use. The default (Quality, 67%) renders under half of that.

**Earlier build (October 12, eighth build): Steam Frame controllers get
their own buttons.** You asked: "check if people with steam frame reported
bugs on discord and fix them".

What Steam Frame players have reported:

- **Open, fixed here.** chrisdrenegade in #spidy-bugs on October 12, on
  0.3.0: "X is registering as dodge on Steam Frame Controllers. Multiple
  buttons are mapped to dodge on steam frame controller for some reason."
- **Old, left alone.** chrisdrenegade on October 8: the hands are "a bit
  janky with how they are positioned. At least for me on the Steam Frame".
  pokenate28 on October 7: web balls go "down and to the right" of the
  hand. Both are from 0.2.2 (October 7 and 8). The hands were redone in
  0.2.3, and markers now show where each hand's web goes. Neither player
  has said so since, and no report from a Frame holds its hands' poses,
  so there is nothing to measure a change against.
- **Already fixed.** pokenate28's start that stayed flat on October 7. He
  and pmapro have played on the Frame since.

The cause of the open one. Spidy told the VR runtime what its controls are
on Touch and on Index controllers, and on nothing else. For any other
controller SteamVR translates one of those. Its file for the Frame
(`drivers\frame_controller\resources\input\frame_controller_remapping.json`
in SteamVR's folder) turns Touch B into the Frame's B, X **and** Y, Touch Y
into three ways of its D-pad, Touch X into the fourth, the Touch menu
button into View, and each trigger into the trigger and the bumper.
Valve's "Steam Frame Input" page says the same in words. So B, X and Y all
dodged, as reported, and the D-pad interacted.

What changed:

- **The Steam Frame has a layout of its own** (`controller_layout.hpp`),
  given to the runtime where it has Valve's extension for that controller
  (`XR_VALVE_frame_controller_interaction`):
  - right controller: **A** jumps, **B** dodges, **X** opens the game
    menu, **Y** interacts, **Menu** pauses. Each is what the button of
    that name is on a Quest.
  - left controller: **View** is the game's View button (the game menu,
    as X), and the **D-pad** is the game's own D-pad, in play and in
    menus: down heals.
  - triggers, grips and sticks are as on every controller. The bumpers do
    nothing now; SteamVR had made them second triggers.
- **No other controller and no other runtime gets anything new.** The
  Touch and Index layouts are the same bindings in the same order, and a
  test holds them to that list. The Frame's five extra actions exist only
  where the runtime offers its extension, so Virtual Desktop and Quest
  Link get the actions they got before. No other controller's translation
  in SteamVR reads the Frame's layout (its remapping files, searched), so
  a Quest, Index, Pico or PS VR2 under SteamVR keeps its buttons.
- **If the runtime refuses the layout**, VR starts all the same and the
  Frame plays SteamVR's translation, as until now.
- **The session log says which layout is in use**, once the runtime has
  said: "Controllers: Steam Frame, with their own layout ...", or "the
  Touch layout", "the Index layout". Every sample of the report has it as
  `controllers` (left, right) and `controller_layouts` (the ones the
  runtime took). A Frame whose log says "the Touch layout" still plays the
  translation: a binding the player chose in SteamVR earlier, or a SteamVR
  without the extension. XrData is version 26, 856 bytes.
- `tools\launch-lab.ps1 -Probe` lists the layouts a runtime takes, without
  a headset.

**Checked:** 285 core tests (new: the three layouts; the View button and
the D-pad on the game's controller), the launcher's, 113 Python checks. On
this PC's runtimes without a headset, Quest Link and Virtual Desktop take
the Touch and Index layouts and are not offered the Frame's. All 24 paths
of the Frame's layout, its profile and its extension are in SteamVR's
OpenXR runtime here (build 1789498647), and each is a control of the
driver's own profile on the hand the layout names.

**Not tried on a Steam Frame, nor under SteamVR at all.** There is no
Frame here, and SteamVR answers the probe only with a headset connected.
It needs a Frame player's word and their session log. This build is in
`build\windows-ninja` only: not released, not in a play folder.

**Earlier build (October 12, ninth build): what your first session with the
eighth found.** You said, after the 21:53 session: "still strong stutters and
freezes that werent there. grabbing the web with other arm still does the
shooting sound and when moving hands while dual holding they switch grab
position. also the web shooting pose turns almost immidiatly to a fist".

- **Taking the other hand's web makes no shot's sound.** The take gave the
  second hand a rope of the game's own, and the game makes a rope the way it
  shoots a web, sound and all (that rope was the only thing a take did in the
  game, so that is where I take the sound to come from; Spidy plays none).
  Now one web is one rope, whoever holds it: taking hold makes none. The
  piece of web between your two hands is drawn by Spidy, not by the game,
  and hangs slack when your hands are closer than the web between them.
- **The web no longer changes hands as you move them.** Which hand the web
  came down to first was taken from where your wrists were, so it swapped
  whenever one hand passed the other. And both hands' holds held you, each
  from its own place, so moving one hand against the other moved you. Now it
  is as on a rope: the hand that holds the web higher up carries you; the
  lower hand holds the slack below it and moves nothing, wherever you put
  it. Which is which changes only when a hand takes a new hold (or reels).
  - Pull with the upper hand and you go up, as before. A yank of the upper
    hand zips; a yank of the lower pulls on slack.
  - Let go with the lower hand, take the web above the other, pull: hand
    over hand, as before.
  - Let go with the upper hand and the lower has the web where it is at
    that moment: you do not drop the slack's length, and you are not jerked
    up if that hand had wandered down the web (three cases in the test, all
    within 3 cm).
- **The web-shooting hand stays.** It was whole for 0.3 s and then closed
  on the web. Now the hand is the shooting hand for as long as you hold the
  web button: the web leaves your wrist and your fingers keep the trigger
  down. Let go and it opens. A web ball shows it for half a second (was
  0.3). A hand that takes the other hand's web closes into a fist on it
  instead, and gets a softer pulse than a shot's. A fast hand with an enemy
  marked or in reach is still a fist.
- **The stutters and freezes.** The 21:53 session ran as the one before it:
  on the Quest 3 at 120 pictures a second, so nothing I asked for was
  tried yet. What its report and the ones before it add (numbers in
  [VALIDATION.md](VALIDATION.md)):
  - **Every smooth session yesterday evening was on the Quest Pro** (the
    reports name the headset; 18:57 to 21:45). From 22:18 on, and all of
    today, it is the Quest 3. The Quest Pro runs at 90, the Quest 3 is set
    to 120, and at 120 the PC delivers about 70 unevenly. Same build either
    side (21:45 on the Pro, 22:18 on the 3).
  - **The freezes are the stream to the headset stopping for a tenth of a
    second**, not the game: the game goes on drawing through each one, they
    come while you stand still too, and they came with 8 GB of memory free
    as with 0.3. On the Quest 3 since last night: 1.3 to 2.4 a minute. On
    the Quest Pro: 0 to 0.5. On the Quest 3 yesterday afternoon: 0 to 0.6.
    I do not know why the Quest 3's link got worse between yesterday
    afternoon and last night; nothing in Spidy that I can find.
  - So, on the Quest 3: in Virtual Desktop (in the headset) Settings >
    Streaming, set the VR frame rate to 90. If the Quest Pro is still on
    and on the same Wi-Fi, switch it off (a guess: two headsets on one
    access point). Or play a session on the Quest Pro again. Any one of
    these tells us more than another report at 120.
- **Checked:** 293 core tests (one swapped: the rope bookkeeping for the
  strand's drawing; one new: the bearing hand letting go; four rewritten),
  the launcher's, 113 Python checks. **Not tried in the headset.**

**Earlier build (October 12, eighth build): the web-shooting hand, and one
web in both hands.** You said: "lets add hand poses like the real iconic ones
from spiderman on web shootinh. also allow to dual hand the same strand of
web."

- **The web-shooting hand.** Until now a squeezed grip was a fist, and the
  grip is what shoots: every web left a clenched hand. Now the hand that
  shoots makes Spider-Man's hand: the two middle fingers pressed into the
  palm, on the web shooter's trigger, the first finger, the little finger
  and the thumb out. The two fingers come down as the web button goes down;
  at the shot the pose is whole, and stays for 0.3 s whether the web held or
  missed; then a hand whose web holds something closes on the strand, and
  any other is what the grip says. A web ball makes the pose too. It works
  the same with WEB BUTTON: TRIGGER.
- **A punch is still a fist.** A fast hand closes as before. While it
  shoots it stays the shooting hand, unless an enemy has the ring on him or
  is within a fist's reach: then the fist wins.
- **One web in both hands.** Put your free hand on the web the other hand
  holds, within 16 cm of it, anywhere from that hand up toward where it
  hangs, and squeeze the grip: the hand takes hold of that web and shoots
  none. A light tick says the hand is there. What you can do with it:
  - pull with either hand, and you go where you pull (with PHYSICAL WEBS on;
    each hand holds the web where it took it);
  - **climb it hand over hand**: let go with the lower hand, take the web
    again above the other, pull, and so on. In the test a body went up 0.3 m
    for each 0.3 m pull, twice in a row;
  - let go with either hand and the other keeps the web;
  - reel with either trigger.
  The web is drawn as one strand: from where it hangs to the nearer hand,
  and on to the other. A web zips once, whichever hand yanks it. With the
  hand away from the web the grip shoots that hand's own web, as before
  (0.4 m off in the test). A press on the web takes hold of it whatever the
  hand points at, a prop or a thug too.
- **Left alone:** a web that holds a prop or a thug is not taken this way.
  Two hands on one target are two webs, as before.
- **In the report:** `ropes.joins` counts the times a hand took the other
  hand's web, and `ropes.shared` says whether both hold one at the end.
- **Checked:** 292 core tests (nine new: taking hold, hand over hand, a hand
  beside the web, one zip, the grab's press, the strand's drawing, the
  hand's timing twice, the pose on Spider-Man's hand and on a hand of one
  finger), the launcher's, 113 Python checks, and a picture of the pose on
  his hand's joints ([BODY.md](BODY.md#the-web-shooting-hand-october-12)).
  **Not tried in the headset, and not run in the game.** Worth watching:
  whether the pressed fingers sit right on his glove, whether 16 cm takes
  the web when you mean it and not when you shoot a second web with your
  hands close together, and how the short piece of web between your hands
  looks (the game draws it as it draws any web, so it may get an end piece
  where it meets the other wrist).
- **About "performance isnt great in the last run ... we didnt have the
  drops yesterday, was silky smooth"** (the 21:09 session; no build is
  involved, the numbers are in [VALIDATION.md](VALIDATION.md)):
  - Not the network this time: the cable was up, Wi-Fi off, and the stream
    held for 1.5 s in five minutes.
  - **Yesterday evening the headset ran at 90 Hz, since 22:18 last night it
    runs at 120 Hz.** At 90 Hz Spidy handed Virtual Desktop a picture for
    every refresh, 11.1 ms apart, 65-80% of them on the dot and 1% late. At
    120 Hz the PC does not make the 8.3 ms: about 70 pictures a second,
    anywhere from 10 to 18 ms apart, 38-55% on the dot and one in eight
    late. Same build either side of the change (21:45 and 22:18). Spidy asks
    for no refresh rate; it is Virtual Desktop's VR frame rate. **Set it
    back to 90 and tell me how it looks**: that is the one thing here I
    could not check myself.
  - The game's own frame rate did not change with the builds: on the roof
    your morning sessions start from it was 54.5 to 57.3 pictures a second
    in four builds from yesterday evening to this morning. It follows the
    place: about 55 there, 48 where this afternoon's save starts, and 37 on
    the 35 s swing 150-190 m above the city in the last run.
  - One thing to know: Windows had 0.3 GB of memory left to promise at the
    low point of that run (5.7 GB at this morning's). The game takes up to
    14.7 GB and the Claude app held 8.9 GB. It cost no frames, but at 0 the
    game dies.

**Earlier build (October 12, seventh build): one enemy at a time, called
blows that register, and KO!** You said, after the 15:42 session with the
sixth build: "i had a looot of accidetal targeting and chaining of enemies
where i just wanted to finish one. also its not clear how directional strikes
work or they dont register properly since swinging from that direction doesnt
do anything", and then: "maybe finishing an enemy need some cue since its not
clear when they're dead (maybe a light slow motion or visual)".

What the session's report shows (5.5 minutes of fighting, 113 blows):

- **Chaining.** 88 of the 113 blows were web strikes; 96 strikes opened, at
  64 different thugs, 44 of them from 4 m or more. Of the 51 strikes thrown
  within a second and a half of a blow, 16 went to the thug that blow was
  on and 24 to another one, 5.6 m off in the middle and up to 9.9 m. From
  474 to 488 s, 14 strikes went to 9 thugs. Two things made it so. The
  strike went to whoever the fist's motion pointed at best within 10 m,
  never mind the ring. And the ring itself went to whoever was nearest your
  line of sight that instant: in 18 of those 24 hops it had already moved
  to the thug you flew to.
- **Called blows.** 27 arrows, 2 answered, 1 finisher. 58 blows landed
  while an arrow stood; 42 of them were web strikes, which could not answer
  one. 32 blows landed with the finisher called and the count at 4. And the
  16 fists that landed at arm's reach under an arrow all went nearly
  straight at him (at least 0.72 of their motion), whichever way was called.
- **Knockouts.** A street thug has about 50 hit points and a strike's blow
  deals 25 or more, so two blows put most of them out; nothing said so.

What changed:

- **One enemy at a time.** The thin ring is on the enemy you look at, as
  before. Land a blow on him, open a strike at him or hold him with a web,
  and the ring **locks**: it gets four ticks and stays on him wherever you
  look, until he is down, or four seconds pass with no blow at him.
- **A strike goes to the marked enemy only.** With a ring up, a fist thrown
  his way (within 60 degrees now, was 37) flies you to him. A fist that
  points at somebody else flies you nowhere, and so does a punch that falls
  short of him at arm's length. With no ring up (no enemy within 40 degrees
  of where you look and 10 m) no strike opens at all: look at him first.
- **Changing target** is a look: squarely at another enemy for a third of a
  second, with the locked one well to the side.
- **KO!** When a thug your blow landed on runs out of hit points: "KO!" is
  lettered where he stood, both hands thump, and time eases to about half
  speed for a fifth of a second (lighter and shorter than the other
  moments, no ripple). The ring is free at that instant.
- **Called blows from across the room.** A hook, an uppercut or a blow
  down, swung the called way at the marked thug, now opens a web strike
  (before, only a fist thrown straight out at him did), and its blow lands
  as the called one: it answers the arrow, counts toward the finisher and
  throws him the way your fist went. A straight strike is still an
  ordinary blow.
- **The arrow comes in from the side.** It starts outside the ring on the
  side your fist should come from and ends at his middle, so "from there"
  and "that way" read the same: from the right for a right hook, from
  below for an uppercut, from above for a blow down.
- **Left alone:** how a plain punch at arm's reach is judged (the fist's
  direction as it lands, within 60 degrees of the called way). Your 16
  punches under an arrow do not show whether that rule is too strict or
  you were jabbing, so I did not loosen it blind; the next report counts
  strikes opened by a swing (`swung`) and those that answered
  (`strike_called`).
- **Also in the 15:42 report:** three quits to the main menu and no crash.
  The guard's counters did not get into the report (it left them out when
  the game closes by itself; fixed), so whether the guard caught the call
  or the call did not happen is still not known.
- **Your play folder** is now `C:\Spidy-0.2.12`; this build went there.
- **Checked:** 283 core tests (new: the lock, marked strikes and swings,
  the knockout's mark and its moment), the launcher's, 112 Python checks,
  and the marks drawn over your eye captures. **Not tried in the headset.**
  Worth watching: whether a look-switch of a third of a second is too slow
  or too quick in a crowd, and whether a wide swing with an arrow up ever
  flies you when you did not mean it.

**Earlier build (October 12, sixth build): the combat marks, printed.** You
said: "lets make our custom combat indicators look better and slicker and
more aligned with this while not being too over the top", with the style
system of Spidy's videos (the comic's print: ink keylines, flat colour,
lettering out of register, drawings held on twos).

The ring over a thug, the called blow's arrow, the burst where a blow lands
and the count over it are drawn anew. What they mean and when they show is
as before; only the sizes named below changed.

- **One look.** Flat colour from the style system's palette, a black (ink)
  outline round every shape, a hard offset drop where the soft shadow was,
  and nothing translucent: the dark disc under the arrow is gone, so the
  thug shows through the mark.
- **On twos.** A mark is a pose held a twelfth of a second, then the next.
  A call lands in three poses (small and turned, a little too large, a
  little too small) and is then at rest; nothing eases or fades. The arrow,
  once at rest, shifts a hair every sixth of a second, as a held drawing
  does. The ring does not.
- **The target ring** is a thin white ring with four short ticks in from
  it, a little off level, where it was a scope's crosshair.
- **A call** is a white arrow drawn as a marker's stroke, on a blue drop,
  in a blue ring that runs down a notch at a time. The ring is a quarter
  smaller than it was (20 cm across at arm's length, was 26).
- **The finisher's call** is a yellow arrow on a red drop inside a jagged
  red ring, a burst's edge: its shape tells it from a plain call, not its
  colour alone. It was gold in a gold ring.
- **A blow's burst** is a comic's star: yellow around white for a jab, red
  around yellow for a harder blow, small for one pose and whole for the
  next, then a pose of white lines thrown clear (two for a harder blow,
  three for a finisher). It was a white flash, a ring and rays. A called
  blow's star is printed out of register, a blue ghost to its left and a
  pink one to its right, where it was a blue ring.
- **The count** (X2, X3 ...) is yellow lettering with round strokes and a
  black outline, tilted, that pops in with the blow, holds, and is gone in
  two poses. It no longer rises and fades. It is a sixth smaller.
- **Left alone:** the webs' aim markers and the focus meter on your wrist.
  The style system takes the aim markers' two colours as they are.
- **Seen how:** not in the headset. A new tool draws the marks over a VR
  session's eye captures at about the headset's pixel density
  (`tools\preview-marks.ps1`); I looked at every state and pose over two
  captures of your 14:06 session (a thug 2.6 m off, and one at arm's
  length) and over a sunlit wall. What that cannot show: the marks in
  motion, in stereo, and in a real fight's pace. **If the held poses read
  as stutter, or the misprint as a lens fault, say so**: both are one line
  each to soften.
- **Checked:** 280 core tests (the two for the marks rewritten for the new
  look, one new for round lettering), the launcher's, 112 Python checks.

**Earlier build (October 12, fifth build): what your first session with the
fourth build found.** You said: "game crashes on quit to menu, something is
wonky with the target indicator - enemies can be targeted for punch but no
indicator, hold jump jumps like normal, there is a closed vent i need to go
through and cant interact with, the slowmo trigger for in air pulls feel bad
- i dont get near the enemy like ever, i intened for it be like a pull to me
in the air and have slowmo when they're close enough to punch in the air
(like dmc air combat)".

- **The crash on Quit.** Not new with the fourth build: every VR session
  that quit to the main menu since the evening of October 11 ended so
  (twelve reports; the ten quits before that were fine), always at the same
  place inside Steam. On its way to the main menu the game asks Steam for a
  login ticket and passes three arguments where Steam's function takes four;
  the fourth is whatever the game's stack holds. In a VR session that was an
  address nobody can read, and Steam's copy from it closed the game. Spidy
  now hooks that function and gives the game's call an empty fourth
  argument, as it has at the game's start. **Not seen working:** without the
  headset the game never made that call (five quits to the menu, with the
  guard in and counting: none), so the first VR session that quits is the
  test. Its report has `steam_guard`.
- **The target indicator.** Three things were wrong. The arrow belonged to
  the thug a blow had just landed on, showed 0.2 s later, and any other blow
  at him replaced it with a new one: in your fight 17 arrows were made in 24
  blows and a flurry replaced each before it showed. 13 of those blows were
  web strikes, which go straight at him and could answer no arrow, yet each
  took the arrow away. And with the finisher needing its arrow, your count
  stood at 4 for a minute with no finisher. Now:
  - A thin white **ring** is over the enemy your next punch would go to:
    the one you look at, within a web strike's 10 m.
  - The **arrow** belongs to your combo, not to a thug. From your second
    blow on it shows in that ring, at once, and it stays, pointing the same
    way, until a fist lands going that way or the combo ends. Then it points
    a new way. Its ring runs down with the combo's 3.5 seconds.
  - A web strike's blow counts as before and leaves the arrow alone.
- **Hold jump.** The game's Charge Jump is a skill, and your save does not
  have it: RT + A is a plain jump there. Holding A now charges a leap of
  Spidy's own instead, with or without the skill: 4 m up after the shortest
  hold, 11.5 m after 0.8 s, and up to 12 m/s forward with the left stick. It
  leaves the ground by the game's jump and flies as a swing's release does.
- **The vent.** You were on the auction house's roof, looking down, and
  pressed Y three times. The game offers a thing only with its own camera on
  it. Spidy turned that camera left and right to follow your head (October
  12's second build), never up and down: it stayed 10 degrees below level,
  so nothing at your feet or overhead was ever in its view. It follows your
  head's pitch now. In the game the pad's stick pitches that camera from 85
  degrees down to 68 up, as fast as it turns it. **Not tried at your vent:**
  if a prompt shows now and asks for LB + RB, that is the right stick's
  click.
- **The air pull.** All 16 of your yanks were let go a quarter of a second
  in (the flick of the wrist a yank is), so each thug was thrown at where
  your hand had been while you flew on. Now a thug yanked while you are in
  the air comes to you: he is steered to a spot an arm's length in front of
  you at chest height, at up to 28 m/s on top of your own speed, whether or
  not you still hold the grip. Time slows when he gets there, not when you
  yank (for 1.2 s), and you hang in the air for 1.6 s unless a web carries
  you. Hit him and the blow has him: an uppercut or a blow in the air keeps
  him up, and you hang on with each. Without a blow he falls after 1.6 s.
- **Your save.** My checks ran in your profile's game. One of them pressed A
  on MANUAL SAVE by mistake and the game autosaved during two others. I put
  `slot0-s.save` back as it was at 14:20 and moved the stray manual save out
  (both are in `reports/backups/saves-20261012-quit-crash/made-by-the-tests`).
- **Checked:** 279 core tests, the launcher's, 112 Python checks. **Not
  tried in the headset: all of it.**

**Earlier build (October 12, fourth build): called blows, slow motion by
itself, ropes for webs, Charge Jump, the scan, and four changes of
defaults.** You asked for eight things in one list; each is quoted below.

- **"lets add visuals cues and system to and required directional o specific
  attacks to our combat (like in batman arkham shadow)".** Called blows. From
  your second blow on, a blow that leaves a thug standing calls the next one:
  an arrow over his chest points left, right, up or down, inside a ring that
  runs down in 3.5 seconds. Throw your next fist at him that way, as you see
  him (within 60 degrees of it; a straight jab answers none), and it is a
  called blow: half again the damage, a cyan burst, and he goes the way it
  went (a hook sends him flying as a full blow does, up lifts him, down
  floors him, or slams him if he is in the air). Any other blow at him is an
  ordinary one. The finisher must be called: after four blows a gold arrow
  shows, and the next blow is the finisher only if it follows it. A blow that
  does not is a plain one that leaves him standing, the count stays at four,
  and the gold arrow comes back pointing another way. The way is never the
  same twice running.
- **"lets sprinkle slow motion to the gameplay (maybe on attacks set, when
  pulling an enemy while in the air, on perfect dodges etc)".** Time slows to
  0.3 for a moment by itself, with slow motion's look, fainter, and no focus
  spent: at a finisher, an uppercut that lifts a thug and a slam (0.3 s, after
  the blow's own stop); at a called blow (0.35 s); when your web yanks a thug
  off his feet while you are in the air (0.7 s); and at a perfect dodge
  (0.6 s). After one ends, none starts for 1.5 s, so a flurry is not one long
  slow motion; a dodge's starts all the same. The perfect dodge is read from
  the game: a B that went down with the game's time at full speed, and the
  game slowing its own time within 0.7 s of it. **That signal is a guess**: I
  have not seen the game do it, so that one may never fire, or fire for
  something else the game slows time for after a dodge.
- **"lets make the webs physical, less stretchy and allow them to curle
  around edges (instead of cutting them off)".** Webs are ropes now
  (PHYSICAL WEBS in Settings > SPIDY VR and the launcher, on unless switched
  off). Three things:
  - *Round edges.* A wall that came between you and your web's end cut the
    web off after 0.15 s. Now the web bends round the edge it met (found by
    halving the step back to where the line was still clear) and holds you
    from there, with what is left of its length; swing back and it comes off
    the edge again. Round six edges at most; where no edge is found it lets
    go as before. The game draws the web as far as its nearest bend: the rest
    lies behind the corner.
  - *In your hand.* A web's length was measured from its end to the middle of
    your body, so the line from your wrist grew and shrank with every move of
    your arm. It is measured to the hand that holds it now: a taut web keeps
    its length, and a hand pulled back pulls you after it, at the speed it
    moved (8 m/s at most). The sharp pull still zips on top of that.
  - *Paid out.* A web kept the length it was shot with while the game walked
    you. Walk 10 m on and step off a roof, and it pulled you back those 10 m
    at once, at up to your speed limit: the snap that made it feel like
    rubber. It grows as you walk now, with a hand's width to spare, and holds
    at its new length once you are in the air. A reel, a zip, a jump or an
    edge takes you off the ground as before; an arm that moves does not.
- **"lets make holding jump execute Charge Jump".** A held on the ground for
  0.18 s is the game's RT + A until you let go, and the game leaps. The
  price: a tap of A jumps as you let it go, not as it goes down. In the game
  the Charge Jump turned out to be a skill ("Hold RT + A while running or
  standing to build up charge, then release A for a huge leap": Skills >
  Webslinger), locked on the save I tried, where RT + A jumps at once. So
  after two holds that end in a plain jump Spidy takes the skill as missing
  and stops waiting for A (no delay at all), until you have been in the
  game's menus, where it is bought. **I could not try the charge itself**:
  no save here has the skill.
- **"lets add and ability to scan the environment (ingame r3) to putting my
  hand to my temple pressing trigger".** A trigger pulled with its hand
  beside your head (either hand, either side: within 19 cm of a point 15 cm
  out from the middle of your eyes) is the game's R3 for as long as you hold
  it, with a tick in both hands. That pull shoots no web ball and reels
  nothing. In the game R3 on Spidy's controller brought up the scan (the
  objective panel).
- **"lets disable the two triggers to go to virtual screen mode (instead we
  can put an option to switch to between that and full vr in the pause
  menu".** I took "the two triggers" for the two thumbstick clicks, which was
  the switch. Clicking both now does nothing. VIEW in Settings > SPIDY VR
  (VR, or FLAT SCREEN) is the switch; chosen there, the flat screen is the
  menu's own screen, where it stands. Each session starts in VR.
- **"lets set the default weight to 100%".** Done, in the game's tab, the
  launcher and the session. A launcher that still has the old default (80%)
  saved starts on 100%; one where you chose another weight keeps it.
- **"lets disable depth of field sharpening (allow users to override from
  settings)".** I read that as the game's two settings, Depth of Field and
  Sharpness. A VR session turns the first off and sets the second to its
  lowest, as it already does with frame generation and vertical sync, and
  puts both back when the game closes. "Game's depth of field" and "Game's
  sharpening" in the launcher's options keep yours.
- **Checked:** 277 core tests (11 new), the launcher's, and 112 Python
  checks. In the game without a headset: a jump, a web, a reel, a release and
  a landing with the new rope code (no fault, every hook restored); the
  Charge Jump's input and the scan's. **Not tried: anything in the headset.**
  The arrows, the moments, how a rope in the hand and a web round a corner
  feel, the temple, the VIEW row: all of it is yours to try first.

**Earlier build (October 12, third build): bodies are nobody's target; web
balls web turrets, and whatever else the game webs up.** You said: "bodies are
still targetable by hands (for webbing i guess), they shouldnt be. after
entering the door there are turrents that the game tells me to press rb
repeatedly to web but it doesnt work", and then: "can you implement a global
solution so we wont need to redo every enemy?"

- **Bodies.** The second build kept punches across the room off the knocked
  out; webs, web balls and fists still took them. Now nothing does: a thug
  with no hit points left, or in one of the game's states for one who is
  down, is no target for any hand.
- **Turrets.** In VR there is no RB: your web balls are the web shooter, and
  they webbed enemy bots only. A turret is no bot. In your session 135 balls
  hit things at the turrets and webbed nothing.
- **The general rule.** A web ball now webs anything the game itself can web
  up: any actor that carries the game's webbing tracker, whatever it is
  called. No turret is named in the code, and the next such thing needs no
  change. The aim helps a ball to one as it does to a thug. Such a thing
  takes the webbing and no damage (a turret has 3 hit points, and the game's
  own web shots leave them alone).
- **In the game, with this build:** three of Spidy's balls web a turret up
  (11, 22, 30 of 30), from 15 to 26 m, also when aimed 4 degrees beside it;
  the game then shows its LB + RB marker on it, and holding LB + RB ripped it
  off and threw it, 1.6 s into the hold.
- **How the game wants it done** (the same with a plain controller): web the
  turret, then hold LB + RB (the right stick's click, held) from within
  about 15 m (it worked from 18 m and not from 22), looking at it. A hit on
  you in the first second and a half stops the yank: web both turrets first,
  or wait until they reload. The webbing lasts about four seconds.
- **Not tried in the headset.**

**Earlier build (October 12, second build): punches go to thugs who are there, and Y opens
what you look at.** You said: "in combat 'dead' enemies and random objects are
being targeted by the lockon system", and "there was also a door that i needed
to go through to progress but i couldnt interact with at all (maybe because i
started walking on it when coming close enough?".

- **What your punches flew to.** In that hall the game keeps the thugs of
  other waves and other floors ready long before they appear: unseen, where
  they will spawn, with hit points and all. Of 22 "enemies" within 60 m of
  where the fight starts, 5 were in the room and 17 were waiting, three of
  them 1.4 m from where you stand. Fists, webs, web balls and punches across
  the room took all of them. In your session of the evening before the fight
  ended with 9 such "enemies" still in reach, and ten of your blows went to
  thugs who were not there.
- **Now only thugs in play count,** for every fist and every web. A punch
  across the room also leaves a thug alone once he is down (the game's own
  states for it, besides his hit points).
- **The door.** The game takes its Y only for a thing its own camera has in
  view and its hero faces. In VR you see neither: that camera stayed wherever
  the last run left it, and the hero faced the way he last walked (along the
  door, when you slid to its middle). In the game without a headset, at that
  door: no "Open Door" with the camera 50 degrees off it, or with the hero
  turned away, and Y did nothing there; with both on the door one tap opened
  it.
- **Now the game looks where you look.** Its camera turns to where your head
  points, all the time, unseen (within 12 degrees; from behind you in a
  second and a half). And a press of Y turns the hero to what you look at
  first, and stays down for the half second that takes. Look at the door,
  press Y. Holding Y still walks you up to it from further away.
- **Not the cause:** your blows. A fight ended by Spidy's punches alone
  finished as the game's own does (Yuri's call, then the door's prompt).
- **Not tried in the headset yet.** The targets were checked in the game with
  this build (5 of 5 in that hall, 9 waiting thugs left alone); the camera and
  the turn were worked out there with a controller, and the part that runs in
  the headset has only its own tests.
- **With the first build of the day.** Heal and the zip on your right
  thumbstick (down, up) are in this build as 0.3.1 has them. Its "look at the
  perch or the vent" is the game's camera too, which now does look there.

**Earlier build (October 12, first build): heal and vents in VR; OpenXR
Toolkit; Steam Input.** You asked: "check out the bugs section in discord,
map out the bugs you think we have enough data to handle", then "fix the
things you know how."

- **Heal and LT + RT had no control.** In VR play Spidy gave the game A, B,
  Y, Back, Start, LB + RB and the left stick, and nothing else: no D-pad
  (heal is D-pad down) and no triggers (the grips and triggers are the
  webs). The first mission asks for both: it stops until you heal, and its
  vent is entered with LT + RT. Players: "game gets stuck when the game
  forces you to heal, and you cant heal" (on 0.3.0), "How do I get past the
  heal part", "cant use Vents", "Stuck in the elevator. no buttons open
  vents", "How do you get in vents?".
- **The right stick's down and up.** That stick turns you with its left and
  right, and its click is the game's LB + RB; down and up did nothing. Now,
  pushed straight down it is the game's D-pad down (heal), in the air too;
  pushed straight up it is the game's LT + RT, not while a web carries you.
  A push counts from the stick at rest, three quarters of the way or more
  and no more than 45% aside, after 0.15 s, and holds until the stick is
  back within half-way; the right controller ticks when the game gets it.
  Turning, a turn rolled round to the top, a diagonal and a flick through
  the line do neither (`game_pad::StickPush`).
- **OpenXR Toolkit closed the game.** A player (Pimax Crystal Super): the
  game closed at the Sony logo, five starts in a row. His game log ends with
  an unhandled exception from outside the game's code 6 s after the start,
  and he traced it to OpenXR Toolkit. OpenXR loads every installed add-on
  into each program that starts it, and with Spidy that program is the game.
  Each add-on's own file names a variable that switches it off in a program
  that has it set. Spidy now reads that name and sets the variable in the
  game while OpenXR starts there, for OpenXR Toolkit and nothing else
  (`xr_layers.hpp`: a list of one), and the launcher's log says so. The
  Toolkit stays on for other games.
- **Steam Input.** A player's buttons did nothing in VR while webs and
  walking worked, and the game showed keyboard keys in its prompts; turning
  Steam Input off for the game ended it. Why Steam Input keeps Spidy's
  controller from the game is not known: it was not reproduced. So this is
  no fix: the warning for a game that does not read the VR controllers now
  names Steam Input and where to turn it off, and PLAYERS.md has an entry.
- **Reports** count both pushes: `heals` and `zips` (XrData version 23, 824
  bytes).

What ran: 264 core checks (2 new: the stick's pushes; OpenXR Toolkit's
manifest as its repository has it, beside Virtual Desktop's and ReShade's as
this PC has them), 111 Python checks, the launcher's and the graphics test.
The switching-off ran on this PC outside the game: the code as the game
module has it, given ReShade's add-on (installed here) in place of the
Toolkit's (not installed), with Spidy's headset probe started under it and
the OpenXR loader's log on. The loader wrote "Implicit layer
C:\ProgramData\ReShade\ReShade64_XR.json is disabled" and loaded Virtual
Desktop's add-on as before; the variable was gone afterwards.

Nothing ran in the game or the headset: the game was not started. Unproven:
that the game heals on the D-pad down it gets; that the vent takes LT + RT
(the prompt is LT + RT as remembered, not looked at in the game), and what
the game does with LT + RT when no point is in view; that a game with OpenXR
Toolkit installed starts now. [VALIDATION.md](VALIDATION.md) has the checks.

`spidy_stereo_probe.dll`, the launcher (two rows on its CONTROLS page),
`run_game_vr.py`, README.md and PLAYERS.md changed. In no play folder.

**Earlier build (October 11, eleventh build): fighting across the room.** You
asked: "check how Batman: Arkham Shadow combat works and reimagine our current
combat with it in mind build v1 for me to test", and while it was built: "i
think our swinging and acrobatics could allow for airal combat. also punches
have zero feedback now and feel bad". After a session with it: "i like the
direction".

- **What Arkham Shadow does.** A punch thrown at an enemy anywhere in the room
  slides you to him while time slows; each blow answers with a flash and a
  kick in the hand; a warning icon is countered by swinging an arm at it;
  blows in a row build a combo toward special moves. Its makers call it
  punching across the room.
- **The web strike.** Throw a fist at a thug up to 10 m away, in sight, and
  you fly to him; the blow lands as you arrive, 0.95 m from him. From the
  ground you slide along it; from a jump, a fall or a swing it is a dive, and
  your webs let go. The fist: 2.6 m/s or faster, going out from your body,
  within 37 degrees of him, for 40 ms. A hand drawn back, a hook, a twitch of
  the tracking and an arm thrown out as its web button goes down open none.
- **Along your web.** A hand whose web holds a thug strikes him with a punch
  toward him, and the web lets go. A sharp pull still yanks him to you.
- **In the air.** An uppercut lifts a thug off his feet. A strike at a thug
  above you leaves the ground by the game's own jump and then flies up to him,
  and after every blow you land off the ground you hang there for 0.45 s (3 s
  in all between two landings). A blow down at a thug in the air slams him.
- **Blows that carry.** The game answers its melee damage with a flinch,
  however hard the blow (measured October 8). A hard blow, an uppercut, a
  blow at a thug in the air and a finisher now go out as its kinetic blow,
  which sends him flying, and his flight is given the blow's velocity.
- **Flow.** Blows within 3.5 s of one another count up, each a tenth harder
  than the first (half again at most), and every fifth is a finisher: two and
  a half times as hard, and he flies far.
- **What a blow feels like.** It was one 25 ms pulse at half strength and
  nothing else: in your 20:20 session 29 punches landed, some at full
  strength, and felt like nothing. Now the hand kicks for 60 to 320 ms (both
  hands for a heavy blow), the game's time stops for 30 to 100 ms (0.4 s for
  a finisher) and eases back, a burst shows where the blow landed with its
  count in the flow (X2, X3...), and it sounds: four blows and a strike's
  rush, each made from a falling tone and a burst of noise, no files.
- **Clenched fists.** A hand whose web only hangs from a wall while you stand
  punches and strikes now. Before, any attached web kept its hand from
  punching: in your 14:49 session both hands moved at 4.4 m/s with their webs
  out and landed nothing. A hand whose web holds a prop or carries you
  through the air still does neither.
- **Not in it:** the counter (Spidy cannot tell yet when an attack is coming),
  a marker on the thug a punch would strike, and a switch for the strike or
  its sounds.

Your session with it (21:39, from `dist/Spidy-0.2.12`): 13 strikes opened, 12
landed; 21 blows, 2 of them heavy and 2 finishers. The strikes slid you 0.4 to
7.4 m along the ground, level within 0.11 m, at about 11 m/s and not the 26
asked for: a standing player's mover stepped in a fifth of the frame's time
there, and a request is capped at 65 m/s of that. One strike ended without a
step. No uppercut, air blow or slam happened in it, and none of the four thugs
sent flying was seen in the game's flight within 0.4 s: both are unproven.
Not run in the game without a headset: Steam was offline and asked whether to
play without the cloud saves. [VALIDATION.md](VALIDATION.md) has the numbers.

`spidy_ray_bridge.dll`, `spidy_stereo_probe.dll`, the launcher (a FIGHTING
group on its CONTROLS page), `run_game_vr.py` (the report's `strikes`) and
README.txt changed. 262 core checks (19 new) and 111 Python checks (1 new)
pass. In your play folder (`dist/Spidy-0.2.12`) since 21:26.

**Earlier build (October 11, tenth build): 0.2.13 started no session on a
Windows in another language.** A player's report: "2.13 bug, didnt happen
with 2.12", VR would not start.

- **What happened.** Since the fourth build a session asks Windows, before it
  starts the game, whether the PC is on Wi-Fi (`netsh wlan show interfaces`
  and `ipconfig`). Those two print in the console's code page. Python under
  the launcher reads text as UTF-8, and one letter outside English in what
  they printed (the player's `ipconfig` had the byte 0x84, an `ä` in a
  Western console) made the read fail and leave nothing. The next line then stopped the session: `TypeError: expected
  string or bytes-like object, got 'NoneType'`, after the headset was found
  and before the game started.
- **Who it hit.** Any PC whose Windows is in a language with such letters in
  those two commands' words, and an English Windows with one in a network
  adapter's or a Wi-Fi network's name. 0.2.12 and earlier never ran them.
- **The fix.** Their output is read in the console's code page, and a byte
  that is no letter there becomes a question mark instead of an error
  (`command_text` in `tools/run_game_vr.py`). The headset probe's answer was
  read the same strict way, where a runtime's own line could have done the
  same; it is read leniently too (`probe` in `tools/xr_runtime.py`).
- **Still so:** the Wi-Fi warning reads those commands by their English
  words. On a Windows in another language it stays silent, as before the
  fourth build; it only no longer stops anything.

Reproduced here before the fix: a command printing the byte 0x84 under
`-X utf8` gives no output and the player's error. One new Python check and one
extended one: both fail on 0.2.13's tools and pass now, with and without
`-X utf8`. Not run on a Windows in another language: I have none.

`run_game_vr.py` and `xr_runtime.py` changed, no module and no protocol. 110
Python checks pass (1 new). Not in your play folder.

**Earlier build (October 11, ninth build): cutscenes in 3D on the game
screen.** You asked: "lets keep cutscenes in a virtual screen but with proper
3d stereoscopic depth".

- **What you see.** A cutscene still shows on the screen in front of you, and
  now has depth: the game draws it once for each of your eyes, from its own
  cutscene camera set a little apart, and the headset shows the left picture
  to your left eye and the right one to your right. This is real depth from
  the game's geometry, not a guess from a flat picture. Finishers, deaths and
  the other scenes the game plays by itself get it too.
- **Where things lie.** What is far away lies as far behind the screen as a
  real horizon (your eyes look at it in parallel, never apart). What is close
  to the game's camera comes out of the screen toward you. Subtitles, prompts
  and the HUD stay on the screen itself.
- **GAME SCREEN 3D** in Settings > SPIDY VR (under GAME SCREEN SIZE; "Game
  screen 3D" in the launcher): OFF is the flat picture as before. HIGH shows
  everything as far from you as it is from the game's camera. MEDIUM (the
  default) and LOW put it all 1.4 and 2 times as far, so less comes out of the
  screen. It changes at once.
- **A close-up looks larger than life**, as on a cinema screen: a narrow lens
  gets its two cameras closer together than your eyes are. A wide shot is life
  size.
- **Flat as before:** the pause menu, hint cards, the map, loading and the
  title screen. The game draws those only into its own picture. A cutscene
  comes up flat for a moment and gains its depth when the game's first pair
  of pictures arrives.
- **Cost.** In a cutscene the game now renders two eye pictures and its own
  window, as it does in the flat-screen mode. Expect about the frame rate of
  VR play, not the higher one the flat cutscenes had.
- A session's report shows `game_screen_3d` as the `presentation` of those
  frames and counts them in `screen_3d_submitted`.

Checked in the game without a headset, on your rooftop save (the game's play
camera stood in for a cutscene's): the far shore lies 75 of the expected 77
pixels apart in the two pictures, Spider-Man 3 m ahead 47, and the HUD, a
subtitle and a prompt 0 to 1, so they sit on the screen. With the pause menu
open the game's own picture has the menu and the stereo pictures stand down.

Not checked: a real cutscene, and anything in the headset. I had no cutscene
at hand in your saves without playing past a story step. Two things in
particular are yours to see: that the depth feels right (the eyes could be
swapped only by a mistake in sign that the pixel measurement above rules
out), and that subtitles show. If a cutscene stays flat, its scene had no HUD
panel to carry the subtitles and Spidy kept the flat picture rather than lose
them: the report will say so, and that case needs another way.

A first copy of this build was in your play folder from 16:24 to 16:31 and
was withdrawn untested: it took the pause menu for a scene (the game's time
system goes on updating there) and would have hidden the menu.

`spidy_stereo_probe.dll`, `spidy_ray_bridge.dll`, the launcher,
`run_game_vr.py` and README.txt changed; XrConfig is version 23 and XrData
version 22. 243 core checks (3 new), 23 launcher checks and 109 Python checks
pass. In your play folder (`Documents\Spidy-0.2.11`) since 16:38, the files
they replaced in its
`reports\backups\play-folder-before-cutscene-3d-20261011-163800`. The
launcher that is open is still the old one: close it and start it again.

**Earlier build (October 11, eighth build): hold Y to step up to what it
opens.** You reported "b (is now both dodge and interact, making
interactions not register".

- **B and Y are two buttons, and the game got each alone.** Your 15:24
  session: 34 dodges (B) and 5 interacts (Y), never both at once. B is the
  dodge only; the interact moved to Y in the sixth build. The launcher's
  CONTROLS page still said B interacts: the launcher was open each time a
  build went into your play folder, so it was never replaced. It is now.
- **Why nothing happened at the red door.** The game takes its Y only within
  about a metre of the thing, and shows its Y prompt only there. You stood
  1.6 to 2.5 m from the door's middle: Y from 1.9 m and three times from
  2.5 m did nothing, and each B dodged you (once to within 0.95 m, and away
  again). In the plain game it is the same: no prompt and no effect from
  3.6 m, "Open Door" 1.1 m from the door's middle.
- **Now: look at the thing and hold Y.** After 0.3 s you walk on where you
  look, until the game takes the Y (it does as you come into reach: checked
  at that door in the plain game), you let go, a wall stops you, or 2 s have
  passed. A tick in the left hand says the walk began. A tap of Y is as
  before and moves nobody. Not in a fight (there Y is the web strike), in
  the air or on a wall.
- A session's report counts `interact_steps`, the walks a held Y began.
- Not done: the game's own prompts still show Xbox buttons. A, B and Y are
  those buttons on your controllers; LB + RB (a pull, or the right stick's
  click) and X (your fists) are not.

`spidy_stereo_probe.dll`, the launcher, `run_game_vr.py` and README.txt
changed; XrData is version 21. 240 core checks (1 new), 23
launcher checks and 109 Python checks pass.
In your play folder (`Documents\Spidy-0.2.11`) since 16:05: the stereo
module, `run_game_vr.py`, README.txt and this time the launcher too, the
files they replaced in its
`reports\backups\play-folder-before-interact-step-20261011-160526`. The
launcher that is open is still the old one: close it and start it again for
the new CONTROLS page.

Not run in the headset: the walk itself (its direction and feel, and the
door opening with it) is yours to try. Your save is as you left it at
15:34, in front of that door.

**Earlier build (October 11, seventh build): the first mission's crash is
fixed, and a two-hand web pull is the game's LB + RB.** You reported "game
crashes on first mission ... at some point during one of the steps it crashes
for everyone", then a door further on that "prompts me to rb+lb and i cant
target it with webs", and asked "why cant we use the existing grip
webshooting with both hands to to physically pull objects that need rb + lb".

- **The crash was Spidy's blow to a thug who had just appeared.** Your 14:49
  session's minidump ends in the game's `BotAttackResponseManager`, on the
  thug your left web had just yanked: it asks the thug's `AwarenessDB` how
  aware he is of whoever hit him, and that thug had none (a null read at
  `Spider-Man.exe+314d3e`, 1.5 s into the first fight). The thug yanked 60 ms
  before him took the same blow and flew. Punches and web balls name
  Spider-Man the same way.
- **Now:** every blow Spidy sends first looks at the thug's own handle to
  that component. Where it names nothing, the blow goes out as nobody's: the
  game's function then leaves before the lookup. He is hit all the same.
- **The door.** In VR play nothing sent the game's LB or RB: your grips shoot
  your webs. Those webs do not hold on that door (16 grip presses in a row
  missed it in your 14:20 session: the red cross), and the web grab does not
  know it.
- **Now:** point both hands at the thing, squeeze both web buttons, and yank
  either hand back as for a zip. Both controllers pulse, the console says
  "Web pull", and the game gets its LB + RB for as long as you hold both
  (0.3 s at least, 4 s at most). Only standing on the ground, with no web
  attached and both hands on one thing within 12 m that no web holds on.
  Spidy does not see the game's prompt: a pull at such a thing without one
  gives the game its LB + RB too.
- **A plain fallback:** the right thumbstick's click, or hold, is the game's
  LB + RB as well.
- **In your headset, 15:24 to 15:34 (570 s), from the autosave before that
  fight:** no crash, the game closed normally. Of 359 blows 9 went out as
  nobody's, the first 3.3 s into the first fight; 29 punches, 80 yanks, 66
  knock-downs. The door: both grips missed it once more, a yank held LB + RB
  for 1.4 s, and from there you went on up a floor. Up there LB + RB went
  down four more times (503 s to 551 s, 0.3 to 0.5 s each), each right after
  both grips missed; seven grip presses in between missed and pulled
  nothing. The report cannot tell a pull from a right-stick click there, nor
  whether you yanked: tell me if the gesture was hard to trigger.
- A session's report has `punch.unnamed` (the blows that went out as
  nobody's), and `pad_buttons` shows `0x300` while LB + RB are down.
- The aim rays (a few a step) now run with the aim markers off too: the pull
  needs to know what each hand points at.

`spidy_ray_bridge.dll`, `spidy_stereo_probe.dll`, `run_game_vr.py`, the
launcher's CONTROLS page and README.txt changed. 239 core checks (2 new), 23
launcher checks and 109 Python checks pass.
In your play folder (`Documents\Spidy-0.2.11`) since 15:23: the two modules,
`run_game_vr.py` and README.txt, the files they replaced in its
`reports\backups\play-folder-before-web-pull-20261011-152300`. Not the
launcher, which was open.

Not checked: what the game does with a nobody's blow in every later fight
(one session), a pull at a thing without a prompt, and whether every LB + RB
prompt is a thing no web holds on (where a web does hold, a yank zips you to
it: use the right stick there).

**Earlier build (October 11, sixth build): B dodges.** You asked for "a dodge
button and ability (you can remap some stuff".

- **B is the game's own dodge.** Press it and Spider-Man dodges the way the
  left stick points; with the stick at rest the game chooses. It is the
  game's B, so whatever the game does with a dodge comes with it. Not while
  a web or your own flight carries you: there the game does not have you,
  as with the interact.
- **Three buttons moved to make room.** A dodge needs the left thumb on the
  stick, so it had to be a right-hand button. Interact (web strike in a
  fight) went from B to **Y**, the game menu (map, suits, skills) from Y to
  **X**, and X no longer hides the aim markers: AIM MARKERS in Settings >
  SPIDY VR, or in the launcher's options, does. A, B and Y are now the
  buttons the game's own prompts show.
- Flips are as they were (the left stick, and A in the air). B skips the
  T-pose calibration as before and does not dodge meanwhile.
- **In the game, without a headset:** the virtual controller's B with the
  stick took the player 2.5 m to the side (off the perch he stood on), B
  alone 1.0 m, the stick alone for as long 0.2 m; the player's own camera
  kept committing through each, so VR stays on during a dodge
  (`tools/probe_dodge.py`).
- A session's report counts `dodges`: the B presses that reached the game.

`spidy_stereo_probe.dll`, the launcher (its CONTROLS page and option texts)
and `run_game_vr.py` changed. 237 core checks (1 new), 23 launcher checks
and 109 Python checks pass.
In your play folder (`Documents\Spidy-0.2.11`) since 14:42: the stereo
module, `run_game_vr.py` and README.txt, the files they replaced in its
`reports\backups\play-folder-before-dodge-20261011-144225`. Not the
launcher, which was open: its CONTROLS page names the old buttons until
`build\windows-ninja\spidy_launcher.exe` replaces it.

Not run in the headset: the path from your controller's B to the game, a
dodge in a fight, and how the leap feels are yours to try.

**Earlier build (October 11, fifth build): the reflections are back in the
eyes; the third build had taken them out.** You played on the cable:
"preformance was good again, but no ssr visible (turned it off and on in the
menu". Both halves check out.

- **The stutter was the Wi-Fi.** That session's report (12:42, the PC on its
  cable alone): no frame held by Virtual Desktop in 215 s, where the session
  before it had 77 in 100 s.
- **The third build's "fewer buffers" broke the reflections, and its check
  did not catch it.** It left out four half-size buffers (the
  `kSharedHalfResFp16Target`s) that the game's code seemed to use only for
  depth of field and ray tracing. The reflection pass needs them: without
  them it runs and adds nothing. That build's notes said the wet roofs
  mirrored the sky "again"; that came from two half-size pictures of different
  game runs side by side, and was wrong. Enlarged, the same pictures show it
  plainly: the Statue of Liberty has its mirror image in the water with the
  second build and none with the third.
- **Now:** an eye's buffers are made with all ten again, as in the second
  build. Checked on the statue's mirror image, enlarged, in the eye: there
  with the head still and with it swaying (2 cm and a degree, as a head does);
  the game's frames as even as before (19.8 ms median, 22.3 ms the longest;
  swaying 20.0 and 27.8). An eye's GPU time is 6.8 and 6.9 ms (6.5 and 6.8
  without reflections).
- The eyes still follow the game's Screen Space Reflections setting as it is
  when VR starts, so turning it off and on in the menu while playing changes
  nothing you can see until the next start (off stops them at once; on brings
  them back only if they were there). With Ray-Traced Reflections on in the
  game the eyes get neither kind, and the console says so: the game then
  takes a way through the pass that an eye's buffers cannot serve, and it was
  never run.

Not run in the headset.

**Earlier build (October 11, fourth build): the stutter was the PC's Wi-Fi,
and a session now says so.** You played the third build and found
"preformance sucked with and a without ssr, huge stutter spikes". Its report
has what the earlier ones lacked, the time each step of the headset's loop
took, and it shows where the spikes are:

- **Virtual Desktop held the frames, not the game.** In each spike the call
  that hands a frame to the headset's runtime (`xrEndFrame`) took 93 ms and
  the round 100.0 to 100.7 ms, again and again: a time-out, the runtime
  waiting for a stream that was not taking frames. 77 rounds in 100 s of VR,
  7.7 s in all. Spidy's own steps in those rounds took 0.1 ms (the copy) and
  0.1 ms (the hands), the graphics card had 0 to 2 eye pairs waiting, and the
  game made more pairs than usual (60 a second), having the card to itself.
- **The PC was on Wi-Fi.** Windows' network log for today: on its cable from
  09:59 to 10:32:50, when the cable's connection ended and the PC joined
  the Wi-Fi the headset is on; on the cable again from 12:17:43; off
  the Wi-Fi at 12:21:48. Your smooth session (10:07) was on the cable alone.
  The three with stutter (10:51, 11:00, 12:17) had the PC on that Wi-Fi: the
  last started 13 s after the cable came back, with the Wi-Fi still
  connected. A picture streamed from a PC on Wi-Fi crosses the air twice, to
  the router and from it. The reflections came into it by coincidence: the
  second build went into the play folder 18 minutes after the cable dropped.
  Not shown: that the headset was still using the Wi-Fi address in the 12:17
  session, and a session on the cable with the reflections on.
- **A session warns now.** Started with the PC on Wi-Fi and a wireless
  runtime, it prints a warning (and a different one with both a cable and
  Wi-Fi: a headset that connected over the Wi-Fi keeps using it). When the
  game closes it counts the frames the runtime held for a tenth of a second
  and says that the stream stalled, not the game. `network` and
  `stream_stalls` are in the report. `tools/probe_link.py` times the link
  (the access point answered in 0.7 ms over the cable, 4.7 ms at most in
  75 s).

The reflections stay as the third build left them.

**Earlier build (October 11, third build): the eyes' reflections follow the game's own setting, with fewer buffers, and the session report says what the
headset's loop waits for.** You played the second build twice and found it
"much more stuttering compared to my first run before the ssr fix (even with
ssr disabled its stuterry)", then asked to "fix the ssr fix". What was found,
and what was not:

- **Your reports show it.** In both sessions with the second build the loop
  that feeds the headset fell to about ten rounds a second for 0.3 to 2.3 s
  at a time (8 such stretches in each session: 3.3% and 0.8% of the half
  seconds in VR), where your first session of the day has none and October 10's
  one. The game itself went on rendering 40 to 60 eye pairs a second through
  them. They went on after you turned Screen Space Reflections off at 11:03.
- **Without a headset nothing shows it.** A new probe times every frame
  (`tools/probe_frame_pacing.py`, `probe_vr_load.py --pacing`): with the
  second build's buffers the game's frames are as even as without (15.1 ms
  median, 16.7 ms for the slowest in a hundred, none over 20.3 ms; standing,
  turning, and with the pad held on run, jump and swing). With the setting off the
  reflection pass does not run in the eyes (checked in an eye's image), so it
  is not that pass's cost. **What stalls the headset's loop is not found, and
  that the second build causes it is not shown**: three sessions, one after
  another.
- **Fewer buffers.** The second build also gave each eye four half-size
  buffers that only depth of field and ray-traced reflections use. They are
  left out again: the eyes get the six the reflections use and nothing else
  that they did not have. An eye's GPU time with reflections is now what it
  was without them (6.1 and 6.5 ms where the second build took 6.7 and 7.1
  and the first 6.5 and 6.8), 51.2 frames a second where it was 49.7, and
  video memory 90 MB lower.
- **The game's setting decides.** With Screen Space Reflections off in the
  game's graphics settings when VR starts, the eyes are made exactly as
  before the second build. The console says which it is. (An eye's buffers
  are made once: a change in the menu shows in the eyes from the next VR
  start; turning it off in the menu stops the pass at once.)
  `--no-eye-reflections` (`-NoEyeReflections`) does the same whatever the
  setting.
- **The report now has the loop's steps.** Every sample has `xr_ms` (what the
  headset's loop spent on its newest round: waiting for the headset, tracking,
  the eye images, the copy, the hands, submitting) and `gpu_waiting` (eye
  pairs the graphics card had not finished). A session that stutters again
  shows in which step.

Your game's Screen Space Reflections setting is on again (I turned it on
through the menu for the check; you had turned it off at 11:03). Not run in
the headset.

**Earlier build (October 11, second build): screen-space reflections in the
eyes.** You reported that they do not seem to work in VR while the normal
screen shows them. They did not: the game queues its ScreenSpaceReflection
command for each eye every frame, but the command (`1734a80`) returns at once
for a view whose buffer set has no `kSsrHistory` and `kSsrHistoryDepth`, and
the game makes those only for its own views' sets. An eye's set is now made
with them (`initBuffers` in `src/stereo_probe.cpp`: the offscreen kind's mask
gets bit 1 for the call, as it gets bit 4 for the anti-aliasing targets).
Checked in the game without a headset, a fresh game each, the same rooftop
(`reports/ssr/new.json`, `old.json`): `kSsrHistory` is 1032 x 1096 in each
eye's set where it was 0 x 0; in the left eye's image the Statue of Liberty
is mirrored in the water and the wet roofs mirror the sky, as in the game's
window, and neither does without. Cost at 3072 x 3264 eyes, 67%: an eye's GPU
time 6.5 and 6.8 ms to 6.7 and 7.1, 50.9 to 49.7 frames a second, video
memory 6,175 to 6,359 MB. Not run in the headset. `probe_vr_load.py
--no-eye-reflections` is the old way.

**Earlier build (October 11, first build): the monitor takes fewer of the
headset's frames, and both eyes share one set of sun shadows.** You ran
`vr_display.py --restore` and said to continue with the rest. With your own
settings back, the game runs full screen (3840 x 2400) when started outside
the launcher, and the same rooftop measurement that gave 61 frames a second
in the launcher's small window gave 43. That difference turned out to be one
thing:

- **Every picture sent to the monitor cost the headset.** The game presents
  its window once a frame, also while the window only mirrors your left eye.
  On this laptop (the screen hangs on the Intel adapter, the game renders on
  the NVIDIA one) each present of the full-screen window took 8 ms of a 23 ms
  frame. Held back, measured in one game: 43.1 frames a second with every
  present, 52.7 with every second, 58.8 with every fourth, 66.2 with none
  (turning: 42.5 and 65.5). The game needs none of them: it paces itself. The
  pose in the newest eye pair was also fresher without them (a median 44 ms
  old where it was 66), with half a pair waiting on the graphics card where
  1.4 did.
  **The monitor now gets your left eye 30 times a second**, whatever the
  game's rate; the other presents never reach Windows. The launcher's new
  **Monitor frame rate** option chooses Every frame, 60, 30, 15 or 5 a second
  (`-MonitorRate 0`..`240` for `Launch Spidy VR.cmd`, 0 every frame). Menus,
  cutscenes and the flat screen present every frame as before.
- **Each eye rendered the sun's shadow maps again.** The game has a way for
  one view to use another's: a view given another view's frame as its parent
  copies that one's shadow cascades and renders none (`19206a0`). It never
  uses it for an offscreen view. The eyes now do: the second eye takes the
  first one's. One thing had to change for that. The game fits each view's
  six cascades to the view's own lens, and the two eyes' lenses mirror each
  other: their nearest boxes sat 13.0 m apart, for boxes of 13.2 m half a
  side. So the first eye fits its cascades to the lens that covers both eyes
  (the one the game's own view has on your head) and both use those.
  Measured: a frame's render commands fell from 5.3 to 4.2 MB (8.9 to 6.5
  turning). Where the graphics card is the limit, as on this laptop, that is
  66.4 to 66.7 frames a second looking ahead and 65.7 to 67.4 turning. Where
  the processor is (small eyes, the card half idle): 98 to 104 looking ahead
  and 79 to 87 turning.
  What it costs: the covering lens is wider, its boxes 15% larger a side
  (13.24 to 15.23 m for the nearest) over shadow maps of the same 1792
  pixels, so a shadow's edge is that much coarser. Side by side at twice the
  size (`reports\perf\shadow-ab3\side-2560-1850-left.png`) the shadows have
  the same shapes and slightly softer edges. Making the maps larger to match
  crashed the game (its shadow atlas had no room for a sixth, and its render
  code reads a cascade's map without checking: `16364df`), so that is out.
  `-OwnShadows` gives each eye its own again. On the flat screen each eye
  keeps its own.

What this means for 90: with presents out of the way the eyes are the whole
frame (15.1 ms of GPU time for 15.1 ms frames at the default upscaling), and
the graphics card decides. `nvidia-smi` says why it is slow: its power limit
is 80 W by default here and 150 W at most. A higher performance mode in MSI
Center is the largest step left, and yours to take; nothing I can change from
here. The processor's own limit, measured with small eyes, is about 100
frames a second looking ahead and 87 turning with the shared shadows.

`spidy_stereo_probe.dll`, the launcher and the session tools changed;
XrConfig is version 22 (`monitorRate` where the spare word was, options bit
17 for each eye's own shadows). New in `tools/probe_vr_load.py`:
`--eye-shadows`, `--monitor-rate`, phases such as `vr_s0` and `vr_m0` that
switch them for a phase, how old the newest pair's pose is and how many pairs
wait on the GPU. 236 core checks (1 new), 23 launcher checks, 107 Python
checks (1 new) and the GPU test pass. It is in your play folder; see the end
of this entry's section in [docs/VALIDATION.md](VALIDATION.md).
Not tried: this build in the game. The measurements above are of builds
from this morning that held back every nth present and switched the shared
shadows by hand; the rate by the clock and the session's own settings for
both were built after you asked to test yourself, and only their unit checks
ran. Not the headset either, as before. And not the launcher's small window:
my shell cannot give the game that window, so how much of the 61 there was
the monitor's is for your session to show.

**Preceding build (October 10, eighth build): anti-aliased eyes, upscaling, and
two scene views instead of three.** You reported that VR performs badly for a
well-optimized game (memory, lag spikes, frame rate), asked for 90 frames a
second without losing picture quality or features, and noted that DLSS does
not work. Your 19:34 report: the headset ran at 72 Hz and got 20 to 31 new eye
pairs a second (the rest were repeats), and what Windows could still promise
fell from 11.3 to 5.1 GB.
Measured in the game on this PC without a headset (`tools/probe_vr_load.py`,
your save's rooftop, 3072 x 3264 per eye; RTX 4080 Laptop, 12 GB, drawing
74-80 W; Core Ultra 9 185H): 0.2.11 made 35.6 frames a second looking ahead
and 36.9 turning, with the GPU 97-99% busy. What it was busy with:

- **The eyes had no anti-aliasing at all.** With an upscaler chosen in the
  game's settings (yours: DLSS Quality), the game leaves its temporal
  anti-aliasing to the upscaler, and its upscaler skips every view but its
  own. So each eye was its scene exactly as rendered: 10 million pixels, every
  edge stair-stepped (what you called jagged on October 8), nothing gained
  from DLSS. The eyes' lenses also had no jitter, and their buffers no targets
  for it, so the game's anti-aliasing could not have worked on them either.
- **The game's own view cost as much as an eye.** Spidy keeps that view on
  your head because the game does shadow, culling and loading work for it;
  the game also rendered it, with every effect, for the monitor: 5.1 ms of a
  19.6 ms frame, and about three of every ten megabytes of render commands.

What changed:

- **The eyes are anti-aliased, and upscaled.** Each eye's lens now jitters
  as the game's own camera does, its buffers get the game's anti-aliasing
  targets at the eye's size, and its frames run the game's temporal
  anti-aliasing, which also upscales: by default the scene is rendered at 67%
  of the eye's size per side (2064 x 2192 for a 3072 x 3264 eye, the ratio of
  DLSS Quality) and brought to the eye's full size from the last frames'
  samples. It is the game's own and needs no RTX card. The launcher's new
  **Upscaling**
  option chooses Off (100%, anti-aliasing only), High (77%), Quality (67%),
  Balanced (58%) or Performance (50%); `-Upscale 50`..`100` for
  `Launch Spidy VR.cmd`. The HUD, subtitles and markers are drawn at the eye's
  full size as before.
- **The game no longer renders its own view while you are in VR.** Its camera
  stays on your head, so shadows, culling and loading are as before; only its
  image is gone. The monitor shows your left eye instead, cut to the window's
  shape (it was the head view, stretched). On the game screen (menus,
  cutscenes) and the flat screen nothing changes. With no view of the game's
  to measure brightness from, the right eye takes the left eye's exposure.
- **Vertical sync is off during a session**, like frame generation, and comes
  back afterwards. Your session started with it on at 120 Hz: the game then
  waits for the monitor before every frame and can only run at 120, 60, 40 or
  30 frames a second (its frames were 36-45 ms).

Measured afterwards, the same spot and eye size, a fresh game for each:

| | Looking ahead | Turning | Video memory | Committed |
|---|---|---|---|---|
| 0.2.11 | 35.6 | 36.9 | 6,378 MB | 13,785 MB |
| Upscaling off (100%) | 39.4 | 39.2 | 6,744 MB | 14,195 MB |
| Quality (67%, the default) | 61.0 | 59.6 | 5,766 MB | 13,186 MB |
| Performance (50%) | 77.1 | 69.6 | 5,367 MB | 12,810 MB |

Pictures of the left eye at one viewpoint (`reports\perf\q-*`): 0.2.11 has
stair-stepped beams and a scaffold that breaks into moire; at 67% the edges
are clean and the scaffold whole, a little softer; a frame taken while the
head turned 26 degrees a second shows no trails; 50% is visibly softer. The
HUD probe (immersive, flat screen, game screen and back) and the exposure
probe (both eyes alike within 0.1% at five head poses) pass with the new
views.

**This is not 90.** On this PC the default gives 61 and Performance 77 where
0.2.11 gave 36; in the headset Virtual Desktop's encoding takes its share
too (your session got about 25 where this measurement gives 0.2.11 36; that
session's game also ran full screen at 2560 x 1440 with vertical sync on: its
console has no "Desktop view" line and its HUD texture was 2560 x 1440. "Small
game window" is on in your launcher now).
What still limits it:

- The GPU drew 74-80 W in every run, about what this laptop gives in a quiet
  or balanced power mode. If MSI Center has a higher performance mode, that
  alone should add frames; it was not tried.
- Each eye still renders its own shadow maps: 2,800 shadow-caster commands a
  view and frame, by far the most numerous of the render thread's commands,
  for two eyes 6 cm apart. Sharing them is the next large step for both the GPU and the CPU
  (turning, the CPU holds 50% upscaling at 70 frames a second).
- DLSS itself is still not used: the game has one DLSS instance, for its own
  view. Its temporal upscaler does the same job here; whether DLSS would look
  better at the same cost is open.

`spidy_stereo_probe.dll`, the launcher and the session tools changed; XrConfig
is version 21 (656 bytes: `upscale`, a spare word, options bits 15 and 16).
`-NoEyeAntialiasing` gives the eyes as they were, and `-GameMonitorView` has
the game render its own view again. New: `tools/probe_render_commands.py`
(render commands a frame, by view), per-view GPU time in
`tools/probe_vr_load.py`, `tools/close_game.py`. 235 core checks (1 new), 23
launcher checks, 106 Python checks (3 new) and the GPU test pass.
It is in your play folder (`Documents\Spidy-0.2.11`) since 23:12: the stereo
module, `run_game_vr.py`, `vr_display.py`, the launcher and README.txt, the
files they replaced in its
`reports\backups\play-folder-before-upscaling-20261010-231248`. Nothing was
started from there.
Not tried: the headset. Nothing here ran through OpenXR, so the frame rate in
the headset, how the upscaled picture looks in motion in it, and the monitor
while you play are for you to see. With the head on its side (standing on a
wall) the picture's brightness now follows the left eye alone; if that shows,
`-GameMonitorView` is the old way. The game crashed once while it sat
between measurements with no eye views started (a null pointer in one of the
game's own threads, none of Spidy's code on it); it did not recur in the
launches after it, close to thirty.

**Preceding build (October 10, seventh build): no dive when the game runs
slowly.** You reported being pulled down hard in the middle of a swing, which
stops the web and drops you to the ground very fast, at least twice in your
last session. That session's report (19:34, `game-vr-20261010-193410.json`)
has it three times, in its last 21 seconds. Each time you held both webs and
were going up (at 9.7, 6.5 and 1.6 m/s, the third time along a wall), and
from one physics step to the next you went down at 42 m/s with no webs: 9 m to the
ground in a quarter of a second, then 24 m in half a second, then 27 m in
three quarters.
It was the game's own fall again, the one from the October 7 dives: whenever
Spidy lets go of you in midair, the game takes over at the fall speed it has
kept counting. What let go this time was a limit on how long a physics step
may be. The game's step is its frame time, smoothed; in that session it was
36 to 45 ms (22 to 28 steps a second, at 3072 x 3264 per eye) and crept up as
the game slowed. Each dive began on the first step over 50 ms: 50.9, 50.4 and
51.7 ms. Two places treated such a step as stale. The movement module ran it
without Spidy's command, so the game moved you at its fall speed; and the
swing refused to predict it, which cancelled the swing and released the webs.
So below 20 frames a second every step dropped you.
Now a step is never handed to the game for being long. The movement module
moves you by the command for the whole step, whatever its length, and 2 m at
most: a step that would go further is shortened, where it used to be refused.
The swing predicts steps up to 100 ms and counts a longer one as 100 ms, so
below 10 frames a second you slow down instead of falling. The movement
command stays valid for 216 ms (two such steps) instead of 150. One thing
you may notice where you used to fall: a command still moves you 1.75 m a
step at most, so below about 18 frames a second your top speed comes down
with the frame rate (26 m/s at 15) until the game speeds up again.
The same refusal had a second way in, which your report does not show
happening: at the speed limit a command asks for up to 1.75 m a step, and a
step a quarter longer than the one before it (your report has 16.0 then
20.1 ms, and 38.7 then 44.5) would have asked for over 2 m. That is shortened
now too, and the swing knows a shortened step is no wall it hit.
`spidy_movement_bridge.dll` and `spidy_ray_bridge.dll` changed; no protocol
changed. 234 core checks pass, 4 of them new (the steps of your report flown
through a simulated mover: the web holds, nothing jolts, no dive; a shortened
step keeps its speed; the module's rule; steps over 100 ms) and 2 changed; all
six fail on 0.2.11's logic. 103 Python checks and the launcher checks pass.
In your play folder (`Documents\Spidy-0.2.11`) since 20:02: the two modules,
the earlier files in its
`reports\backups\play-folder-before-long-steps-20261010-200244`.
Not tried: the game and the headset. Nothing here ran in the game, so that
the dives are gone is for you to see; your next report shows it either way
(`motion_samples`: status stays 2 through steps with `dt` over 0.05). Not
changed: a prop or thug on a web is still let go when the step passes 50 ms
(`game_grab.cpp`, `web_grab.cpp`), and the frame rate itself; a lower Render
resolution in the launcher raises it.

**Preceding build (October 10, sixth build): both eyes get one brightness.** You
reported that brightness and shadows differ between your eyes: standing on
the side of a building, the eye that sees more sky and the eye nearer the
city come out differently, and they swap when you turn round; you suspected
an auto brightness worked out per eye. It is exactly that.
The game sets its exposure (how bright it draws the picture) on the graphics
card, one view at a time: it averages what the view shows, lets a small
buffer of the view's follow that average, and its last pass multiplies the
picture by that buffer. Each of your eyes is a view of its own with a buffer
of its own, so each eye was exposed for what it alone saw. A headset's lenses
see further outward than inward, so the left eye's picture holds more of what
is on your left: put the sky there, and that eye is drawn darker. Spidy has
copied "the main camera's exposure" to the eyes since its first VR builds,
but those were the numbers the game reads back to the processor, which its
last pass does not use.
Now each eye's render job reads the buffer of the game's own view, the one on
the monitor, which sits on your head and covers both lenses while you are in
VR: one exposure for both eyes, worked out from everything you see.
Measured in the game without the headset (`tools/probe_exposure.py`, new),
from your save on the construction site at sunset, both eyes drawn the old
way and the new way at each head pose. The brightness of the part of the
scene both eyes see, left eye over right eye:

| Head | Each eye on its own | One exposure |
| --- | --- | --- |
| level, four directions | 1.28, 0.63, 1.42, 1.26 | 1.000, 1.000, 1.001, 1.000 |
| on its right side (right eye below) | 0.43 | 1.001 |
| on its left side | 2.22 | 1.000 |

So with a level head one eye was a quarter to a half brighter than the other,
and with your head on its side, as when you stand on a wall and look along
it, more than twice. In every pose the eye with more sky was the darker one.
`spidy_stereo_probe.dll` changed, and `run_game_vr.py` reads one new counter,
`appearance.exposure_shared` in the session report: the eye images that took
the game view's exposure, two a frame. No protocol version changed (the
counter takes an unused field). 227 core checks pass; 103 Python checks, 1
new (the probe's comparison of what both eyes see, and that it fails when the
eyes still differ). Your saves: all identical but `slot0-s.save`, which the
game saved itself after Continue, as in every probe.
In your play folder (`dist\Spidy-0.2.9`) since 13:19: the module and
`run_game_vr.py` (the folder's own, with the new counters only), the earlier
files in its `reports\backups\play-folder-before-exposure-20261010-131936`.
The module carries the fourth build below as well.
Not tried: the headset. The numbers say the two pictures now match; whether
anything else still differs between your eyes (shadows were part of your
report, and with one eye up to twice as bright they could not match) is for
you to see. If it does, say where you were looking.

**Preceding build (October 10, fifth build): the launcher finds SteamVR beside
Virtual Desktop, and the memory row is gone.** A player on Steam Link, with
Virtual Desktop's streamer running on the PC as well, could not start VR
(the chat, October 10): START VR said "No headset found (Virtual Desktop:
Headset unavailable ... (-35); Meta Quest Link: Headset unavailable ...
(-35))", and the VR runtime list held Automatic, Virtual Desktop and Meta
Quest Link, so there was no SteamVR to choose either. A player the night
before, without Virtual Desktop, had the same ("its only showing up as quest
link not steamvr").
The launcher and the session took the runtimes from Windows' OpenXR registry:
the active runtime and its `AvailableRuntimes` list. SteamVR is in that
registry only while it is the active runtime; it is not on the list on this
PC or in either player's launcher. With Virtual Desktop or the Meta Quest
Link app active, SteamVR was unknown: not listed, and never asked by
Automatic although `vrserver.exe` ran. This PC has it the same way today:
SteamVR installed, Virtual Desktop active, the list holding Virtual Desktop
and Meta's, and 0.2.9 showing those two. (Steam Link worked here on October 7
because SteamVR was the active runtime then.)
Now SteamVR is also found where OpenVR programs find it:
`%LOCALAPPDATA%\openvr\openvrpaths.vrpath` names its folder, and
`steamxr_win64.json` in that folder is its runtime. The launcher
(`openXrRuntimes`) and the session (`xr_runtime.steamvr`) both read it, so
the list shows SteamVR, and Automatic asks it after Virtual Desktop whenever
SteamVR runs. The launcher's JSON reader moved from `launcher_update.hpp` to
`launcher_text.hpp` for this, unchanged.
The Memory row, the "Low on memory" question before a start and the
session's memory warning are removed, as you asked: a player with 32 GB of
RAM never saw the row green and asked how to get it so. With everything in
order the last row now reads "Windows and Spidy's files". Session reports
still record `free_commit_mb` at the start and at its lowest.
The launcher, `tools/xr_runtime.py`, `tools/run_game_vr.py` and the players'
README changed; no module and no protocol version did. 23 launcher checks
pass, 1 new (the path file as SteamVR writes it, folders in another alphabet
written plainly and as escapes, a byte order mark, files that are not it);
103 Python checks, 1 new (SteamVR found by the file, listed, and asked by
Automatic when it runs). On this PC the launcher built from this tree lists
Automatic, Virtual Desktop, Meta Quest Link and SteamVR and has no memory
row; 0.2.9's code lists Virtual Desktop and Meta Quest Link.
Not run: a headset over Steam Link with this build (SteamVR was not started
here), so a session through the SteamVR found this way is untried; the core
checks and the game, which this build does not touch.
For players on 0.2.9 and older, SteamVR's Settings > OpenXR > "Set SteamVR
as OpenXR runtime" puts SteamVR in the launcher's list (in PLAYERS.md).

**Preceding build (October 10, fourth build): the monitor shows your head's view
again in a swing, and the HUD keeps its distance.** Players reported that
since 0.2.8 the game window shows Spider-Man from behind, without his head,
whenever they swing, so first-person footage recorded from the monitor is
spoiled (#spidy-bugs "Camera", October 9: "tested in v0.2.7 and it's
working"; a 0.2.9 clip in the chat on October 10).
The monitor shows the game's own view, which Spidy moves to your head every
frame, and, like the eyes, on with you to where the frame draws you. Since
the HUD build (October 9, fifth) the eyes, that view and the HUD panel take
one head per frame, fixed at its first use. The first use turned out to be
the HUD placing its world markers, which the game does before it moves you:
that head still had the last frame's position. The eyes work out their own
travel later and were right, so nothing showed in the headset's view of the
world. The game's view and the HUD panel were one frame of travel behind
them: nothing while you stand, most of a metre in a swing. From there the
view looks at your own back, and your body has no head, because the head is
hidden for the eyes.
Your own session reports have it. In the 0.2.9 session (October 10, 11:20)
the game's view was placed from another position than the eyes in 41,727 of
53,825 frames, up to 0.90 m behind, and the HUD panel with it
(`offset_mismatch_m` 0.90, which should be 0); in the 0.2.8 session (October
9, 19:51) 18,788 of 24,543 frames, up to 1.35 m. The sessions of October 5
and 6, before the HUD build, have none in 42,614 frames. The panel hangs 2 m
ahead, so 0.9 m of trail in a swing straight ahead put it 1.1 m from your
eyes, nearly twice as large, moving in and out with your speed.
Now the frame's head is fixed after the game has moved you, at the game
camera's submit, as before the HUD build: the game's view, the panel and the
eyes are placed from one position again. The markers, placed earlier, get a
head of their own: the newest head pose, moved to where the frame will draw
you, which is where the last frame drew you plus the travel that frame made.
A step longer than 5 m (fast travel, a respawn) is not carried on.
`spidy_stereo_probe.dll` changed, and `run_game_vr.py` reads one new counter:
`hud.marker_mismatch_m` in the session report, the furthest the markers'
head was from the frame's. No protocol version changed (the counter takes
SpidyHudData's unused field). 227 core checks pass, 1 new (the position
foreseen for the next frame: standing, moving, after a teleport or a gap, and
that a head moved there sits where the eyes are); the launcher's checks; 101
Python checks. `tools/probe_web_frames.py --views 13` now reports
`monitor_view` for a swing in the game without a headset, and `--modules`
loads another build's module for a before and after.
Not run: the game. I asked and you chose to look in the headset yourself, so
neither the probe's new report nor the fix has been in the game. To check:
swing and watch the game window, which should stay inside your head; in the
session report `active_view_lag_frames` and `hud.offset_mismatch_m` should
stay 0, and `hud.marker_mismatch_m` should be a few centimetres. Not tried
either: whether the HUD's markers sit steadier over their targets at speed.

**Preceding build (October 10, third build): walk at a wall and you are on it,
and the wall is your floor as soon as you walk it.** After your first session
with the walls you said that wall walking often does not start and the game's
own wall crawl takes over, and that walking towards a wall and up it, the
view turns only once you let go of the stick. Your session report
(`dist\Spidy-0.2.9`, 11:20, 627 s) shows both. The swing's walls took you 46
times and the game's crawl five times, 36 s in all with the view upright: all
five began on the ground, when you walked into a wall. The swing's walls
only took a body in the air; a walk into a wall was left to the game, which
turned you onto the wall 0.15 s after you met it. And the view turned only
below 1.5 m/s: at 73.5 s you walked a wall at 6 m/s for 29 s upright, until
you stopped.
Now the swing sees the left stick walk you at a wall: within 1.3 m of it
(from your middle), the stick within 45 degrees of straight at it, the wall
there above your head too, so a kerb or a car is none. From that moment the
game no longer gets your stick, which had walked you on into the wall and
into its crawl; a tenth of a second on, the game jumps for you, and the
swing's wall has you as your feet leave the ground, going up it at the walk's
6 m/s while you hold the stick. In the air the stick takes a wall beside you
the same way, with no speed into it: walk off a roof, push at the facade as
you fall, and you are on it and climbing.
Where the game's crawl begins all the same (a wall the mount does not see:
lower than your head, met at a glance), the swing has the game jump you out
of it and takes that wall, at rest.
**STAND ON WALLS** now turns your view as soon as you walk the wall (the
left stick tilted, at up to 7.5 m/s) as well as when you have stopped; a run
the stick steers stays upright as before. So a walk at a wall goes on as one
motion: a short hop, the view pitches back in 0.15 s, and the stick you were
holding forward walks you on up the wall.
In the game without a headset (`tools/probe_wall_mount.py`, new; your save,
the same brick wall; three runs, the third with this build): the probe gets
the player onto the wall as before, walks him down it to a ledge 0.79 m from
the wall, and pushes the swing's stick at the wall. The game stopped getting
the stick at once; its jump was asked for 0.16 s in; at 0.22 s he was off the
ground and on the swing's wall, the game's crawl never began, and he climbed
at 6.0 m/s, his centre 0.90 m from the wall by a world ray, every step the
swing's. Then the hand-over: from the ground the probe walks him into the
wall with the controller's stick alone, as the game walks a player. The
game's crawl began 0.06 s after he met the wall; the swing asked for the jump
0.23 s later, the game put him back on his feet at the wall's foot and jumped,
and 0.66 s after the crawl began he was on the swing's wall at rest, 0.90 m
off it, and stayed (0.42 s in the crawl, where a crawl lasted until you
pressed A). The first run found what the checks could not: as its jump begins
the game reports the player standing again for one step, and the wall that
had just taken him let go, after which the push out to the wall's clearance
came back as speed off the wall and he drifted away. The wall now takes him
again within half a second of the mount, and the stick takes a wall with no
speed off it.
The swing's data stays version 4 (its last, unused field is now `mount`);
XrConfig and XrData stay version 20. `spidy_ray_bridge.dll`,
`spidy_stereo_probe.dll` and the launcher changed (its "Stand on walls" text).
226 core checks pass, 5 new (the mount and the step in which the game stands
the body again, what mounts nothing, the crawl's hand-over, the stick taking
a wall in a fall, the view while walking); 22 launcher checks; 101 Python
checks.
Not tried: the headset. Whether the hop onto the wall and the view turning
as you go up feel right is yours to say, and whether walls take you when you
do not mean it: in a fight beside a wall, or walking up to something on one
(you now stop about a metre short of a wall you walk straight at, and hop
on). In the game the crawl's hand-over was met once, a metre above the
ground; higher up, your 11:20 session has it by your own A press (253.3 s:
out of the crawl and on the swing's wall within 0.06 s).

**Preceding build (October 10, second build): walls are yours. Fly into one and
you run along it, stop on it, walk it and jump off; stopped, your view turns so
the wall is your floor.** You said that sticking to walls, walking on them and
wall running felt bad: the stick is clunky to get on and off, only A leaves a
wall, and running along a wall in a swing should feel better; and you
suggested turning the camera to the new floor. Your two October 9 sessions
show what the game did. A swing that met a wall put the game into a wall
state of its own, in which any movement with a part into the wall moves
nothing: you hung still against the wall for a second, the game then turned
you onto it in its crawl (0.6 to 0.7 s more), and from there only A, or a web
above you, got you off, through the game's jump. A wall touch cost 2 to 3
seconds and all your speed (28 m/s to 0 in one of them).
The swing now keeps you on the wall itself. Short rays find a wall you are
coming into, and you stay 0.9 m off it, where the game's own collision never
meets it, so the game goes on as if you were in the air and none of its wall
states start. On the wall nothing pulls you down along it. What you had of
speed along the wall goes on, a little more for a hit at an angle: a wall
run. Without the left stick or a web the run slows (3 m/s a second) until you
stop and stay; head on, you stop at once. The left stick walks the wall at
6 m/s where you look along it; pushed at the wall, up it. A faster run keeps
its speed while the stick points its way, and the stick steers it. **A** jumps
off, out from the wall and up, with the speed you had along it; a web pulls
you off it or along it. Up over the wall's top edge you hop onto the roof. At
a walk you step round a corner onto the next face; a run flies off the
wall's end. A jump of the game's own into a wall lands you on it the same
way. Both hands buzz when a wall takes you and when you jump off.
**STAND ON WALLS** (on; Settings > SPIDY VR under COMFORT, or "Stand on walls"
in the launcher): once you have come to rest on a wall, your view turns in
0.15 s so that the wall is your floor, your feet on it, and stays so while
you walk it, round its corners too; when the wall lets you go it turns back as
fast and keeps the way you face. A run along a wall never turns it. Players
on Discord asked for the same, near instant, because looking up a wall
strains the neck. Off, you stay upright beside the wall.
Walking into a wall from the ground is still the game's own crawl, with the
view upright and stood off it as before: how the game takes the stick there
is not measured yet, and a turned view wants the stick to go where you look.
In the game without a headset (`tools/probe_wall_run.py`, your save, a brick
wall 17.7 m off, webbed and reeled into at 31 m/s): the swing had the player
on the wall 0.84 s after the reel began, his centre 0.90 m from it by a world
ray, at 23 m/s along the wall where the game used to stop him; for the 5.1 s
on the wall the game's mover stayed in its air mode under the swing's command
on every step and its wall states never appeared. He slowed by 3 m/s a
second, turned to the stick, walked along at 6.0 m/s and up at 6.0 m/s,
stopped and stayed (0.00 m/s), and the jump left at 6.0 m/s out and up. With
the swing's walls off, the same reel ended in the game's wall state at
2.6 m/s and a 63 m fall to the street. `tools/probe_menu.py` switched STAND ON
WALLS off in the game's own Settings among its 20 steps, all as expected (it
now reaches Settings with Up twice from RESUME: your save is in a Fisk
hideout, whose pause menu has one line more, and four presses down opened
Photo Mode).
The swing's data and settings are version 4, XrConfig and XrData version 20,
so the whole package goes together. `--no-stand-on-walls` and `--no-wall-run`
(the game's crawl as before, for comparison) on `run_game_vr.py`.
221 core checks pass, 9 new (the run, the stop, the stick, the jump, a web's
pull, the top edge, the corner, what walls leave alone; the view's turn, a
corner's, and the game's crawl); 22 launcher checks; 101 Python checks.
Not tried: the headset. How the run, the stop and above all the turning view
feel is yours to say; the view's turn has only its unit checks, since it runs
in the headset's worker. In the game the corner, the top edge and facades with
ledges were not met. Not in the play folder.

**Preceding build (October 10, first build): a game that runs as administrator
is named at once, and the launcher offers to start again as administrator.**
A player's launch (Quest 3 on a cable, Meta Quest Link, a French Windows)
printed "WARNING: render memory module unavailable ([WinError 5] Accès
refusé.)" when the game started, said nothing for three minutes, and ended
with "No loaded player within three minutes. Load a save, then run the
launcher again." Windows had refused the launcher the game's process. The
render memory module's 2.5 s of asking ended in the warning; the wait for
the game then took every refusal for a process still being set up until its
three minutes were over, and its last message is about a save this launch
never came to. The log does not say why Windows refused. The usual reason
is a game that runs as administrator (Steam started as administrator, or
Spider-Man.exe set to) with a launcher that does not; the other is a
security program guarding the game.
The session now asks Windows whether Steam, and then the game, runs as
administrator, which Windows answers for a process it otherwise keeps
closed. An administrator's Steam ends the launch before anything is changed
or started, and an administrator's game the moment its process appears. A
Steam that is not running and is set to run as administrator, which Windows
refuses to start from the launcher, ends it the same way. The message names
what runs as administrator and what to do, and the launcher's window asks "Administrator
rights needed" with *Restart as administrator*: Windows asks to allow it,
and the launcher opens again with those rights. After "Not now" the offer
stays under START VR. A game that refuses for 5 s without that explanation
ends the launch naming a security program (allow the Spidy folder in it), or
administrator rights as the first thing to try where Windows does not say
how the game runs. A launch that runs out of its three minutes says what was
missing: the game never started (look at Steam), or it could not be opened
(with Windows' error); "No loaded player" is left for a game that was open.
A second launcher started while one runs as administrator no longer opens
beside it. The startup report has `administrator` (whether the session
itself ran as administrator).
7 new launcher protocol checks: an administrator's game, before the render
memory module is tried; an administrator's Steam, before the game's settings
change; a refusal that lasts, by who runs as administrator; one that passes;
the three ways to run out of time; the question itself; the exit code. All
101 checks in the 6 Python suites pass, and 22 launcher checks (1 new: the
error line and the exit code). In the launcher's window, a stand-in session
that ran the real wait with a Steam "running as administrator" ended with
exit code 5 before starting anything, and the question, "Not now" and the
offer under START VR were as described. Not run: the restart itself
(Windows' prompt needs a click), a game that does run as administrator, and
the player's PC. The core checks were not rerun (nothing in the modules
changed). Not in the play folder.

**Preceding build (October 9, tenth build): VR starts where Windows refuses
changes to the game's settings.** A player's 0.2.8 launch (Virtual Desktop,
Quest 3) ended before the game started with "Spidy VR unavailable: [WinError
5] Access is denied" and a traceback. Before it starts the game, the launcher
sets a small window and turns off frame generation and Windows.Gaming.Input
in the game's registry key. On that PC Windows opened the key for writing,
then refused writing a value; the traceback does not establish why.
The launch now goes on with the current settings and prints a warning
(turn frame generation off in the game, and a smaller window leaves more of
the GPU for the headset). Whatever the launch had already changed is put
back where possible; a failed rollback keeps the backup for a later retry.
Restoring writes only values that differ from the registry, so a
launch whose changes were all refused has nothing to restore. A refused
restore after the game closes is a warning, and the next launch tries again.
The session report and the startup report have `game_settings_refused` (the
error, or null). 3 new launcher checks: everything refused, or only the
controllers' key; a restore that writes nothing, or keeps its backup when it
cannot write; the warning shown once. Two additional checks cover a partial
write within a key, failed rollback and retry, and continued startup in both
window modes with the error recorded. All 94 checks in the 6 Python suites
pass. Not run on the player's PC; not in the play folder.

**Preceding build (October 9, ninth build): a flip speed setting, slower by
default.** You asked for flip speeds to choose from, like the turning speed
and the weight, and for a lower default. FLIP SPEED is a new row under FLIPS
in the SPIDY VR tab of the game's Settings, and "Flip speed" in the
launcher's options under "Flips (experimental)": how fast a flip turns you at
full tilt of the left stick, 90, 120, 150, 180, 240, 300, 360 or 480 degrees
a second. The default is 180, a quarter slower than the 240 the flips had so
far; 240 is still a choice. A tap's whole flip and the turn back level go as
much faster or slower (at 180: a tap's flip about 1.4 s instead of 1.1 s,
the way back 1.33 s a turn instead of 1). Changed in the headset, it carries
to the next session as the other settings do (launcher.ini `flip_speed=`;
`--flip-speed`, `Launch Spidy VR.cmd -FlipSpeed`). RESET in the tab puts 180
back.
XrConfig and XrData went to version 19 (XrConfig's spare is now `flipSpeed`;
XrData has `flipSpeed` at 792, 800 bytes), the menu probe's structs to 5: the
launcher, the Python tools and the modules must come from the same build;
install the whole package. The session report's `vr_settings` has
`flip_speed`. 212 core checks (1 new: the stick's speed, a tap and the way
back at 180 against 240, kept through a reset, the clamps, the rig passing it
on; the tab's checks now count 17 rows), 21 launcher checks (the
`--flip-speed` argument and its range, `flip_speed=` from the headset) and the
5 Python suites pass. `tools/probe_menu.py` now steps FLIP SPEED to 240 and
expects RESET ALL to put it back (23 changes); it has not run in the game.
Not tried in the headset; not in the play folder.

**Preceding build (October 9, eighth build): with FLIPS on, the left stick flips
you without A.** You asked for the stick to flip you at all times once flips
are on, without holding A. With the experimental FLIPS switch on, the left
stick in the air now turns you head over heels by itself, as fast as you tilt
it (ahead a front flip, back a backflip, to a side a cartwheel, a whole turn
in 1.5 s at full tilt). Let go and you turn back level the short way; push
again on the way and the stick takes the turn over where it is. While it
flips you the stick no longer steers you in the air, as with A held before.
- A stick already held when you jump (a running jump) keeps steering you
  until you let it go once in the air, so running off a roof does not throw
  you into a flip. Landing, or the game sticking you to a wall or ceiling,
  levels you as before, and from the ground the stick walks again.
- A works as before: a tap is one whole flip toward the stick, and held in
  the air it keeps you turned while the stick rests (let go of A and the
  stick to come back level). A tap while the stick is turning you is no
  extra flip.
- FLIPS off: nothing changed; the stick steers you in the air.
The SPIDY VR tab's and the launcher's FLIPS help now read "In the air the
left stick turns you over; tap A for one flip, hold A to stay." No protocol
change: the stereo DLL (the flip and the tab's text) and the launcher (its
text) change in code. 211 core checks (2 new: the stick alone in the
air flips, a stick held from a jump or on a wall walks, release levels,
landing levels; its speed, A holding the angle, a flick or a tap no whole
flip), 21 launcher checks and the 5 Python suites pass. Not run in the game
or the headset; not in the play folder. (Its flips turned at 240 degrees a
second; the ninth build makes that a setting, 180 by default.)

**Preceding build (October 9, seventh build): a steadier, sharper HUD, and a HUD
setting.** You played the fifth build's HUD on a Quest 3 (Virtual Desktop,
eyes of 3840 x 4080 at 125%), found it still a bit blurry and a tad
jittery, asked for it to follow the head smoothly, and for a way to switch it
off. Your 14:37 session placed the panel in every immersive frame. Its jitter:
a quarter of the images the headset showed were earlier ones turned to your
newer head (53.6 new images a second on a 72 Hz display), and turning an
image turns a panel locked to your head with it. Its blur: the panel's
texture had 1720 pixels across its 60 degrees, fewer than your eye images
have there, so the HUD was stretched, and the game made its text for a
720-row window.
- The HUD now stays put in your room while your head looks within 2 degrees
  of it, and once you look further it glides back in front of you (63% of the
  way every 0.15 s), level with the horizon. Fixed in the room between those
  glides, it stays put on reused images too. Markers still sit on their
  targets: the game places them where the line from your head to the target
  crosses the panel, whichever way it faces.
- The VR window is 1920 x 1080 (16:9) instead of the desktop's shape (1720 x
  720 on 3440 x 1440), and at most three quarters of the desktop's height.
  The game sizes its HUD by the window's height and lays it out for 16:9, as
  on a console: the HUD has half again the pixels, and the same 60-degree
  panel shows all of it a third larger. Menus and cutscenes on the game screen
  are 1080p too. Next to your eyes it is about 3% more pixels to render.
- HUD (Settings > SPIDY VR, under COMFORT after GAME SCREEN SIZE; "HUD" in
  the launcher; `--hud`, `-Hud`): OFF, SMALL (50 degrees across), MEDIUM (60,
  the default) or LARGE (70). OFF leaves the panel and everything on it
  (markers, subtitles, prompts) out of the headset, immersive and on the flat
  screen; the game screen keeps the game's own HUD.
Measured, in the game without the headset (`tools/probe_hud.py` at the
headset's eye size, your save at the Fisk construction site):
- The window came up at 1920 x 1080, and so did the panel's texture (the game
  makes it 1932 x 1080 for that window; Spidy makes it once at the window's
  size, as before).
- At MEDIUM the panel stayed in place with the head ahead, turned 40 degrees
  and turning at 90 degrees a second. The run placed it 1,621 times, none
  rejected, with 0 m between the panel's and the eyes' travel. SMALL and LARGE
  spanned 50 and 70 degrees, with a world marker on the same spot.
- A panel turned 15 degrees from the head, as the follow leaves it mid-glide,
  showed 15 degrees to the side with its subtitles and minimap.
- OFF: no panel and no subtitles in either eye, immersive and on the flat
  screen (848 eye draws of the panel left out in 424 frames); at MEDIUM again
  it came back, and stopping VR gave the game its own texture back.
- At half the headset's resolution, subtitles that were hard to read in the
  fifth build's captures now read cleanly, and the health bar is a third
  larger.
- The follow itself (still within 2 degrees, the glide, level when the head
  tilts, straight down, a reset) is a new core test: only the XR worker drives
  it, which needs the headset.
Not tested: the headset, so neither how the follow feels nor how sharp it is
through Virtual Desktop. In your next session's report, `hud` gains
`followed_frames` (placements the follow turned) and `follow_angle`, and
`vr_settings` the `hud` value. XrConfig and XrData are v18, SpidyHudData v3
and the menu probe's structs v4: install the whole package.

**Preceding build (October 9, sixth build): heavier by default, and longer
slow motion.** You asked for the default weight to be 80 and for slow
motion to last longer.
- WEIGHT (Settings > SPIDY VR, "Weight" in the launcher) now starts at 80%
  of real gravity (7.85 m/s² while webs fly you) instead of 60%. RESET ALL
  goes back to 80%. Until now the launcher saved the weight whether or not
  you had chosen it, so every player's saved settings say 60%: the launcher
  now takes a saved 60% as the old default and starts at 80%. Any other
  saved weight is kept, and the launcher now saves it under a new name. A
  60% you chose yourself needs choosing once more.
- A full focus meter lasts 12 seconds of slow motion instead of 7.
  Refilling still takes 12 seconds from empty, starting a moment after slow
  motion ends.
Measured: the core tests (208), the launcher's tests, five Python suites and
the GPU test pass. The tests show the new default as the tab's 80% step,
falling at 7.848 m/s², and the meter running out at its 12 s. Not run in the
game; the swing's gravity code itself did not change. `probe_menu.py` now
steps the weight from 80% to 100%, and `probe_weight.py` and
`probe_slow_motion.py` fly at 80%.

**Preceding build (October 9, fifth build): the HUD stays in view, sharper, with
the game's markers and subtitles.** You asked for the HUD to stay in view, be
sharper (it was very blurry), and show what the flat game shows, like
trackers. The game draws its HUD (health, gadgets, minimap, prompts) on a
panel it hangs 20 m in front of its own third-person camera, larger than that
camera's view. In VR that camera is still behind you and does not turn with
your head, so the panel hung far ahead, mostly out of view and small. The
rest of the HUD (world markers such as objective trackers and interaction
rings, subtitles, QTE and other prompts) the game draws only into its own
view on the monitor, which the eyes never get.
Now, while you play in VR:
- The panel sits 2 m in front of your head and turns with it, centred, the
  part the flat game shows spanning 60 degrees (30 to each side). Both eyes
  see it at 2 m, so it fuses cleanly. Like the flat HUD it is drawn over
  everything, walls included.
- The rest of the HUD is drawn onto the same panel, laid out as on the
  monitor: subtitles at its bottom, prompts where the game puts them. The game
  places its world markers from your head onto the panel, so a marker sits
  over its target while the target is inside the panel; outside it, the game's
  edge indicator shows the way.
- The flat-screen view (both sticks) now shows markers and subtitles too.
- Sharper: the panel's texture is drawn at the window's size, and the VR
  window is 720 rows instead of 540 (1720 x 720 on a 3440 x 1440 desktop),
  about the headset's own detail across the panel's 60 degrees. That window is
  also what the game screen shows, so menus and cutscenes are sharper. It adds
  about 3% to the pixels the game renders for the two eyes.
How: [DEVELOPMENT.md](DEVELOPMENT.md#the-hud-in-vr).
Measured, in the game without the headset (`tools/probe_hud.py`, your save at
the Fisk construction site):
- The panel stayed at the same place in both eye images while the head looked
  ahead, turned 40 degrees, looked 25 and 70 degrees down, and turned at 90
  degrees a second, which also shows no frame of lag. Every immersive frame
  placed it (2,349 placements, none rejected), and the panel and the eyes always
  moved with the player alike (0 m apart).
- The rest of the HUD was on the panel in every immersive frame; two markers
  a frame were placed from the head. Looking down at the crates, the game's
  interaction ring sat on the crate; subtitles showed at the panel's bottom.
- With the eye views paused, as when the headset shows the game screen, the
  game's own view had the whole HUD back within about 0.35 s: the window
  showed the HUD, the ring and the subtitles as the flat game does.
- Stopping VR made the panel's texture at the game's size again and bound it.
- Text is readable in captures at half the headset's resolution.
Not tested: the headset. In your next session the report's `hud` block shows
it working (`placed_frames`, `layer_frames`, `marker_projections`; `restored`
after the session). Worth checking in the headset: whether 2 m and 60 degrees
are comfortable (both could become settings), subtitles and prompts in a
fight or a QTE, and objective markers while swinging. A new export
(`SpidyHudData`) ties this build's modules and Python together: install the
whole package.

**Preceding build (October 9, fourth build): fists, web balls and webs take
each enemy's own size.** Spidy treated every enemy as a street thug: aim at
a chest 1.15 m above his feet, catch him 0.95 m up, a fist hits within 1.8 m
of his feet, all 0.45 m wide or less. For a drone, a flyer or a heavy like
Rhino that was off. Now Spidy reads each bot's size from the game: the
capsule his mover collides with, which the game keeps in his mover manager
(a street thug's spans 0.4 m to 1.6 m above his feet, 0.45 m around).
Every height and width tuned on a street thug carries over in proportion to
that capsule. Heights stretch between the capsule's ends and widths follow
its radius, so a street thug keeps exactly today's values while a drone's
shrink and a heavy's grow. The web balls' aim point and aim assist, where a
web catches and holds an enemy, a fist's target, and what a flying enemy or
thrown prop strikes all use it. Mass, which sets how hard the web pulls,
stays as it was.
Measured: the game builds each mover's capsule (`0x1fbba90`) from fields
Spidy now reads. In the game, all 12 Fisk thugs at your save read 0.85,
1.15 and 0.45 (0.4 m to 1.6 m), and the hero 0.86, 1.3 and 0.4. A new core
check reads a body laid out like the game's, and checks that a street
thug's measures come back unchanged and a drone's and a heavy's scale. Not
seen yet: an enemy of another size in the game (none near your save).
`tools/probe_combat.py --list` now prints each bot's capsule. No protocol
change: only the ray module changed.

**Preceding build (October 9, third build): the launcher updates itself.** You
asked for an auto-updater for the launcher. When it starts, the launcher asks
GitHub for the newest release. If that is newer, a banner above the Play tab
offers it with *What's new* (the release's changes on hover, its page on
click), *Update now* and *Later*. *Update now* downloads the zip (progress on
the banner, *Cancel* stops it), checks it against the SHA-256 GitHub gives for
it, unpacks it, puts its files in place of the folder's and starts the new
launcher, which says "Spidy is updated: 0.2.6 to 0.2.7." Settings
(`%APPDATA%`), `reports\` and files beside the launcher stay. *Later* hides the
banner until the next start. About has a new UPDATES card: the last check's
result, *Check now*, *All releases*, and a switch for the check at start. The
switch is on by default, and updating always waits for a click.
How: the files are swapped by renames within the Spidy folder, which Windows
allows even for the running launcher. Old files go to a hidden
`.spidy-update\previous\`; if any file cannot move, every move so far is undone
("Nothing was changed"). It refuses while VR runs, while a program runs from
the folder (a session's Python, the headset check), or while the game has
Spidy's modules loaded. The old launcher closes and then starts the new one,
which waits for it to exit and deletes `.spidy-update\`. A checkout's launcher
(`build\windows-ninja`) neither checks at start nor installs. Details:
[DEVELOPMENT.md](DEVELOPMENT.md#updates). No protocol change; `update_check=`
is new in launcher.ini. Players on 0.2.6 or older download the first release
with the updater by hand once.
Measured: 21 launcher checks (5 new: JSON; the release read from GitHub's real
answer for v0.2.6; the notes' SHA-256 when the digest is missing, and refusals
without one, without the zip or without a version; versions; the update plan).
End to end, with this launcher in a copy of the 0.2.5 release, against the
real v0.2.6 release:
- The banner offered 0.2.6. *Update now* downloaded, checked and installed it,
  then the 0.2.6 launcher started. The folder's 68 package files were
  hash-identical to the 0.2.6 zip; a report and a file beside the launcher
  stayed, and a stray tool was retired.
- With the folder's Python running, it refused ("Close python.exe first").
- With `tools\xr_runtime.py` held open, it rolled back: all 69 files were
  hash-identical to before, the running launcher's own exe included. *Try
  again* after the lock was released succeeded.
- Started with `--updated-from` and `--wait-for`, it waited for that process,
  deleted `.spidy-update\`, and showed the updated banner.
A whole update took a few seconds in two runs; in a third, GitHub served the
12 MB zip in about 110 s.
Not tested: *Cancel* mid-download, a SHA-256 mismatch, a proxy, and updating
into a release that has the updater, since none is published yet.

**Preceding build (October 9, second build): choose the button that shoots
webs.** You asked for an option to switch the grip and trigger actions.
WEB BUTTON, the first row under WEBS in Settings > SPIDY VR, is GRIP (the
default, as before) or TRIGGER. On TRIGGER the trigger shoots and holds a
hand's web, and the grip reels it in and, on a hand without a web, shoots web
balls. The launcher's options have the same choice ("Web button"), and
`Launch Spidy VR.cmd -TriggerWebs` starts with it. A change made in the
headset carries over to your next session, like the other settings.
How: the swap happens in one place, where the controllers' buttons become the
swing's input. Everything that reads the web and reel buttons follows it:
swinging, reeling, zips, the web grab and its throw, the web shooter, and
the aim markers (the ring tightens as you press the web button). Fists still
close with the grip, the T-pose calibration still asks for both triggers,
and the game's menus keep the grips as bumpers. Changing it lets go of both
webs, so a button held at that moment fires nothing until you release it.
On TRIGGER, a fist made with the grip fires a web ball from a free hand (on
GRIP it shoots a web). A fast punch closes the fist by itself, so you can
punch with the grip open.
Protocol: XrConfig and XrData are version 17 (options bit 12, settings bit
64, `trigger_webs` in reports and in the launcher's settings line), so the
runner and the DLLs must come from the same build.
Measured: 206 core checks (1 new: the swap per hand; a change in play lets go
of the webs once; a change in a menu lets go when play resumes; reset keeps
the button). The tab test checks the new row and that RESET ALL puts GRIP
back. 16 launcher checks, 88 Python checks and the GPU test pass. Not run in
the game yet: `tools/probe_menu.py` now sets WEB BUTTON to TRIGGER first and
expects 18 changes. Tell me how webbing on the trigger feels in the headset.

**Preceding build (October 9, first build): fists, webs and web balls reach
every enemy, flying ones included.** A player reported that flying Sable
agents ignore punches and that webs pass through them. Spidy recognised a
bot by one game class, the mover manager that walks bots on foot
(`BotMoverManagerGame`), matched exactly. Any enemy moved some other way was
invisible to the fists, the web grab and the web balls' webbing. That covers
every flyer the game moves with its `HoverMoverManager`, plus Doc Ock's,
Hammerhead's and Mecha-Hammerhead's movers.
You asked for something systemic rather than a fix per enemy type. Spidy
now reads the game's own type information (RTTI) for every component it
scans, so a class counts together with everything derived from it. An actor
with a component derived from `BotMoverManager` is a bot, whatever moves it.
Every bot is an enemy except the friendly and neutral families: civilians,
the police and mission companions, birds, helicopters and Silver Sable's
aircraft. No enemy type is named, so flyers, bosses and DLC enemies are
covered without a save for each. What a blow does is the game's own
reaction, since fists, webbing and knock-backs go through its damage system.
Fists now also skip the neutral bots.
Measured: a new core check builds stand-in classes named like the game's and
runs the same walk over them. A class two levels below `HoverMoverManager`
is a flying bot, a subclass of `ThugBot` is a thug, and an unrelated class
or plain data is nothing. In the game, headless, at your save's Fisk hideout
(`tools/probe_combat.py`):
- Spidy's scan inside the game found the same 74 props and bots as the same
  rules read from outside, and no actor had a bot component without a bot
  mover manager.
- Three web balls webbed a thug up.
- A scripted fist at 5.4 m/s landed: 27.9 damage, his health from 50 to 19.
  This is the first time the fists' own path was seen hitting a thug.
- A pull flung a thug into a wall: his health from 50 to 26.

No flying enemy was loaded anywhere near your save, so a flyer's reactions
were not seen. Fists and webbing are the game's damage, as for any bot. A
pull asks the game for its flung reaction; if a flyer doesn't take it, Spidy
steers its mover, as it already does for bots the game won't fling.
No protocol change: only the ray module changed. 205 core checks pass.
Tell me, or the player, how flying agents react to a punch, three web balls
and a pull.

**Preceding build (October 8, fifteenth build): slow motion, as in Blade &
Sorcery.** You asked for a Blade & Sorcery-like slow-motion button with a
smooth transition in and out, an effect while it is on, and a "mana" that
runs down and recovers. Click the left thumbstick on its own (both together
still switch to the flat screen) and the game's time eases down to 30% in
0.4 s; click again and it eases back in 0.55 s. The ease is an S-curve in
log time, so the world decelerates evenly and never jolts. Your head and
hands keep real time. Focus, the mana, drains in real time while slow
motion lasts: a full meter lasts 7 s, and when it runs out slow motion ends
by itself. It starts refilling 1.2 s after slow motion ends and takes 12 s
from empty; a click with under 15% left is refused. The meter is a cyan
ring around an hourglass floating over the back of your left wrist. It shows
while focus is spent, empties clockwise from the top, glows and breathes in
slow motion, flashes red when it runs empty or refuses a click, and fades
1.5 s after it is full again. In slow motion the headset's image is drained
toward grey, cooler, and darker toward the rim (round about each lens), and
a ring of light bends the image slightly as it sweeps out across your view
at the start and at the end. Both controllers pulse at the start and end;
the left one buzzes when the meter runs empty or refuses a click. A menu,
the game screen or the flat screen ends it.
How: Spidy hooks the game's own `TimeScaleSystem`, the one its dodges and
gadget wheel slow the world with (update `0x19bb430`, game_time.cpp). After
each of its updates the system's scale, the clock's scale (`0x7a7fb90`) and
Havok's step (`0x609a560`) become the smaller of the game's own and Spidy's.
Before the next update the game's own values go back, so its own slow-motion
moments, channels and events work as before. Everything on the game's clock
slows: thugs, traffic, physics, effects, and Spidy's own swing, which steps
with the player's mover. Props a web holds or throws used to be kept in real
time; `native_bodies` now keeps them in the world's time (real time at the
game's time scale), so they slow too, and they also follow the game's own
slow-motion moments.
Measured in the game, headless (`tools/probe_slow_motion.py`, reports
`slow-motion-probe.json` and `-first.json`): at 30% the clock read 0.30,
each frame's game time came to 0.298 of its normal share of real time, and
Havok's step to 0.30 of its base. In a released swing flight the player's
mover stepped 0.301 game seconds per real second (1.022 at normal speed),
while the swing's gravity on game time stayed 5.89 m/s² (5.886 asked): the
swing slowed with the world. The web grab's step time fell to 0.32 of
normal. Everything came back to 1.0, every module stopped with code 0, the
hooked code was restored, and the save files stayed byte-identical.
No XrConfig/XrData change (both stay version 16), but the session runner
needs the stereo module's new `SpidySlowMotionData` export and checks the
new hook's entry, so the runner and the modules must come from the same
build. The session report has `slow_motion` (presses, starts, refusals,
empties, seconds in it, focus, the game's scale) and `slow_motion_samples`.
204 core checks (5 new), the GPU test (new: both eyes recoloured, typed and
typeless, the ring and the meter; debug layer clean), 16 launcher checks and
the 5 Python suites pass. Not tried in the headset: how the look and the
meter feel, the haptics, and the game's sound, which Spidy leaves alone (it
may or may not slow with the game's time).

**Preceding build (October 8, fourteenth build): the launcher says why a game
version is not supported.** You passed on a player's report: Steam says their
game is up to date, yet the launcher called it a different version. Steam's
current version is still the one Spidy supports (build 23986256, version
4.630.0.0, the July 8 update; Steam's app info checked today), so their
`Spider-Man.exe` is not Steam's current file, and the launcher's "a game
update needs a Spidy update" pointed the wrong way. The game line now names
the file's version against 4.630.0.0 and the fix, from Steam's app manifest
beside the game. If Steam is set to a beta (the game has two rollback betas,
`previous_version` from October 2023 and `previous_version2`, v1.1212.0.0),
it says to choose None under Properties > Betas. If an update is waiting, it
says to let Steam install it. Otherwise, for an older or changed
`Spider-Man.exe`, it says to verify the game's files in Steam, and for a copy
Steam did not install, to choose the one in the Steam library. Only a newer
version still says a game update needs a Spidy update.
`tools/inspect_game.py` keeps the version as `EXPECTED_VERSION` beside the
hash. No protocol change. 16 launcher checks pass (3 new: a block of Steam's
text, the beta and the update in an app manifest, every message). The
launcher's own scan, run on copies of the game's file set to 1.1212.0.0 on
that beta, 3.618.0.0 with an update waiting, 4.630.0.0 with one byte changed,
and that copy outside a Steam library, gave each message; the real file still
reads as supported. Not in a release yet: players see it from the next one.

**Preceding build (October 8, thirteenth build): flips are an experimental
setting, off by default.** You asked to put the flips in the settings as an
experimental option, off by default. The SPIDY VR tab in the game's Settings
has a new last section, EXPERIMENTAL, with one switch: FLIPS, OFF until you
switch it on. The launcher's options have the same switch, "Flips
(experimental)", and the next session starts with whichever you left it at
(launcher.ini `flips=`; `Launch Spidy VR.cmd -Flips`, run_game_vr.py
`--flips`). Off, A in the air does nothing, as before the flips; switched off
mid-flip, you are level at once. The flips themselves are the twelfth
build's (a tap flips you, held the left stick turns you). XrConfig and
XrData went to version 16 (options bit 11, settings bit 32: flips), so the
launcher, the Python tools and the modules must come from the same build;
the session report's `vr_settings` has `flips`. 199 core checks (1 new: off
by default, switched off mid-flip, kept through a reset; the tab's checks
now count four sections), 13 launcher checks (the `--flips` switch, `flips=`
from the headset) and the 5 Python suites pass. `tools/probe_menu.py` now
also switches FLIPS on and expects RESET ALL to turn it off (16 changes); it
has not run in the game with this build. Not tried in the headset.

**Preceding build (October 8, twelfth build): holding A turns you only with the
stick.** You said holding A just started you spinning nonstop, super fast,
the way the stick pointed. Your 17:12 session shows it: held, the flip went
round once every 0.7 s for as long as you held it, eight turns in a row at
one point. Now, held in the air, A hands the left stick the turning, the
way the Spider-Lair's hold mode does: the stick turns you head over heels as
fast as you tilt it (a whole turn in 1.5 s at full tilt, slower tilted
less), and at rest it keeps you at the angle you reached, so you can stay
upside down or lie flat. Let go of A and you turn back level the short way
(a turn's worth in a second). A quick tap (under a quarter second) is still
one flip toward the stick, now a whole turn in about 1.2 s instead of 0.8
s; tap again during it and the flip carries on to level. A long press
without the stick does nothing. Landing or a wall levels you within about a
third of a second, as before. A web catching no longer ends a held flip:
the stick is yours until you let go. Only the stereo module's turning
changed: no protocol change. 198 core checks pass (the five flip checks
rewritten for holding: the stick's speed and its resting angle, letting go,
taps with and without the stick, a tap during a flip), as do the graphics
test and the 5 Python suites. Not tried in the headset yet. Tell me whether
1.5 s a turn at full tilt is the right top speed, and whether coming back
level on letting go feels right.

**Preceding build (October 8, eleventh build): A in the air flips you.** You
asked for a flip like the Ultimate Spider-Lair's (in VRChat), just the
rotation, without changing any control you already have. Since the ninth
build A in the air did nothing, so the flip lives there. Press A in the air
and you turn head over heels toward where the left stick points: ahead (or
with the stick at rest) a front flip, back a backflip, to a side a
cartwheel. The world turns about your head, so your eyes stay where they
are. A tap is one whole turn in 0.8 s. Keep A held and you keep turning,
the left stick steering the flip instead of moving you; let go and you
finish the turn you are in and come out level (let go just past level after
a whole turn and you go back instead of round again). A web that catches
meanwhile, or one already holding you, ends it the same way, so A pressed
while swinging is one flip. Landing, or a wall or ceiling the game sticks
you to, brings you back level the short way within 0.3 s; a menu, a
cutscene or a recenter puts you level at once.
What stays the same: A on the ground, a wall or a perch is the jump, and a
jump held into the air never turns into a flip. The right stick, grips,
triggers, B, X, Y, the menu button and the stick clicks do what they did.
The left stick moves you as before, except while A is held in the air.
There is no setting for flips (you asked for only the rotation): leave A
alone in the air and you never flip.
While you are turned, your hands, aim markers and web balls follow the
turned view. A sharp pull still zips toward the anchor upside down, and a
punch is still measured from your arm: both used only your turn left and
right before, so upside down a pull would have counted backwards. The spin
itself is not a punch or a pull. Spider-Man's body turns with you about
your eyes, his legs keeping the game's pose.
The swing input and the body command carry the flip's tilt now (swing
command version 3, body command version 2). Both modules still take the
probes' older, shorter commands, so the Python tools are unchanged; the ray
and stereo modules must come from the same build. 198 core checks pass (11
new: a tap, hold and release, the stick's directions, landing and menus,
the rig's math, an upside-down pull, a flip that is no punch, the aim
markers, the swing command, the body turning), as do the graphics test, the
launcher's checks and the 5 Python suites. The rebuilt modules took the
probes' old commands and the new ones and refused a bad tilt. Not tried yet:
the game and the headset. Tell me whether 0.8 s a flip feels right, whether
flipping makes you uneasy, and whether steering a held flip with the stick
is useful.

**Preceding build (October 8, tenth build): web balls web thugs up, pulled
thugs fly and get hurt.** You said web balls at enemies and objects did
nothing, and that pulled enemies came along without reacting to the web or
taking collision damage. Your 13:50 session and a headless game at a street
crime showed why. The web balls did hit: 30 of your 58 shots went at thugs,
and in the game every one of Spidy's shots at a thug struck him. But a web
ball carries nothing of its own: its damage is empty, and the game webs an
enemy only through its own firing code, which Spidy's shots go around. Pulls
asked the game for its "flung" reaction, which it accepts and then ignores
for a thug who is fighting (his AI keeps him aiming), so the web slid him
along on his feet.
Now a web ball that strikes a thug deals him the webbing the game's own web
hits deal (a kWebImpact blow carrying 11 of webbing). A street thug is webbed
up at 30, so three quick hits web him up and he struggles in his webs, the
game's own reaction; the webbing wears off within seconds, so slow single
shots do not add up. The first pull on a thug deals him the game's kinetic
knock-back, which throws him into the flailing flight it uses for thrown
enemies, and from the next frame the web steers that flight: yanked, he
flies flailing to your hand; held, he hangs and swings on the web; thrown,
the game flies him on and lands him (a flop, then dazed on the ground). If
he comes down while still on your web, he lies there until the next pull
knocks him up again (1.5 s at the soonest). What flies now hurts what it
strikes: a thug whose flight is stopped short by a wall or a car (flying 7
m/s or faster, then keeping under 45% of it, and not because the web braked
him), or who lands at 8 m/s or faster, takes a blow by the speed he lost
(14 m/s and up knocks him down); a flung thug or a prop you throw that
strikes another thug at 6 m/s or more knocks him down, or from 9 m/s flings
him. Blows are 2 to 15 of the game's damage, which it doubles on its normal
difficulty (a street thug has 60). Civilians, and thugs the game will not
fling (scripted scenes, heavies), are moved on their feet as before.
Objects: still no. All 147 throwable props near your save are breakables
(trash cans and the like): the game's damage does not reach them (a kinetic
blow of 30 was dropped), and pushing one shows the sinking-bin glitch. A web
ball knocks only a throwable that is not breakable, and none was found.
Measured with a research DLL at a crime with 7 thugs (each tried several
ways, see VALIDATION.md): the webbing webbed thugs up; the kinetic
knock-back flung a fighting thug every time while melee or explosion
knock-backs only staggered him; steering the flight pulled a thug 7 m toward
the hero and held another 2.4 m up for 4 s. Then with Spidy's own modules:
its web balls struck a thug and a trash can, said so in their telemetry and
named what they hit. The reports gained the shots' collisions, webbing
blows, props knocked and the latest actor hit, and the grab's knock-backs,
steered flight steps, impacts and thugs struck (the shooter's and the grab's
telemetry changed: the ray and XR modules and the Python tools must come
from the same build). 185 core checks (2 new: when a flight struck
something and how hard, and what a flying body touches) and the 5 Python
suites pass. Not tried yet: Spidy's own pulls on thugs in the game (no thugs
came near on the second run) and the headset. Tell me how webbed thugs look,
whether three hits feels right, and whether pulled thugs fly, land and get
hurt the way you expect.

**Preceding build (October 8, ninth build): A in the air no longer shoots the
game's web zip.** You asked to turn off the game's own web shooting when you
press A in the air or mid-jump. That is the game's web zip: in the air its
jump button shoots a web from Spider-Man's wrist and zips him forward,
whatever your hands are doing. Spidy gave the game your A wherever you were,
even while its own webs flew you. Now a press of A that starts in the air
(swinging, flying after letting go, or in the game's own jump or fall) never
reaches the game, until you let go of it, a landing on the way included. A
press that starts on the ground, on a wall or on a perch is the game's jump
as before, for as long as you hold it. Telling the air from a perch: the
game's mover says "airborne" on some perches too, so Spidy counts you in the
air while its webs fly you, or while the mover is unsupported and in the mode
the game's air state moves it in. In the reports of your October 6-8
sessions that matched the game's own air state on 98.6% of 61,657 steps; the
rest were a jump's first steps (no web zip there now either) and single
landing steps (A jumps there). Perches and wall crawls never counted as air.
183 core checks (2 new) and the 5 Python suites pass. Not tried in the game
or the headset yet.

**Preceding build (October 8, eighth build): steadier aim markers, a colour per
hand, a new target ring.** You said the aim markers felt jerky and jittery,
asked for a different colour shade per hand, and called the square target
marker ugly. The jitter came from the controller: a hand held still still
trembles and the tracking is a little noisy, which swings the aim ray by
about 0.4 degrees peak to peak with a typical tremor, about as much as the
ring is wide in the headset. On top of that the markers' hard pixel edges
crawled as they moved, a marker swapped shape the moment the game's preview
changed, and a target's corners moved in the game's own steps.
Now the hand's aim ray is steadied before the marker goes on it, by a
speed-adaptive filter (the "1 euro filter"): it smooths hard while you hold
still and less the faster you move. Simulated with that tremor, the wobble
drops from 0.4 to 0.1 degree; aiming slowly the marker trails by about 0.1
degree, sweeping fast by about 0.3, and it settles within 30 ms of stopping.
It works in your tracking space, so snap and smooth turns do not make it
swim. A marker eases to a new distance in about 30 ms, a target's ring
slides to the next target, and a marker of another kind fades in (it shows
at once and is whole within 25 ms) while the old one fades out (80 ms), so
an edge where the aim flickers between two kinds no longer pops.
The markers have soft edges over a soft dark shadow (the overlay gained a
translucent pass), the left hand's in sky blue and the right hand's in
orange; a miss stays a red cross. The amber corners are gone: a prop or thug
you would catch gets a ring of three arcs with claws pointing in, turning
slowly (faster as you squeeze) and closing in from wider as it locks on. The
right hand's ring is a little wider and turned between the left's, so both
hands on one thug stay apart.
181 core checks (2 new: the steadying against tremor, sweeps, turns and
gaps; the fades and eases; and the marker check reworked for shadows first,
soft edges, each hand's colour, fading, and a round target ring that closes
in), the launcher's checks, 87 Python checks and the GPU test (both hands'
markers on the dark scene, a bright sky and a lit wall, each in its colour,
shadowed on the bright panels; D3D12 debug layer clean) pass. Not seen in
the headset yet: tell me if they still wobble when you hold still, lag when
you sweep, or if the colours or sizes want changing.

**Preceding build (October 8, seventh build): calibrate your body in a T-pose.**
You asked for an in-game calibration on first start, and as an option in the
VR menu, where the player stands in a T-pose and holds the triggers so the
avatar is scaled to their measurements. Until now Spider-Man's body took its
size from the highest your headset had been and kept his own arms: with
longer arms than his, your controllers ran ahead of his hands; with shorter
ones, his elbows stayed bent.
Now, the first time VR shows the game, a panel appears ahead of you: BODY
CALIBRATION, stand tall and look ahead, arms straight out to the sides, hold
both triggers. A line under it says what to fix (an arm not out to the side
or bent, the head turned, a trigger let go, moving), a figure's arms and a
ring around each controller turn green when that arm is right, and a bar
fills while you hold the pose. After a second and a half it says CALIBRATED
with your eye height and arm length, both controllers buzz, and Spider-Man
is resized at once: his height from your eye height, then each arm about its
shoulder, so his wrists reach yours when your arms are straight. B skips it.
While it shows, the triggers, grips, A and B do nothing in the game, so
holding the triggers shoots no web balls, until you let go after it.
Settings > SPIDY VR has a BODY section again, with one row, **CALIBRATE
BODY**: set it to ON RESUME and resume to calibrate again. The launcher keeps
your measurements and starts every next session with them, so the panel comes
once; its options show them, and Redo forgets them so the next session asks
again. A skip is remembered as well. `Launch Spidy VR.cmd` takes
`-EyeHeight 1630 -ArmLength 590` (the console's last settings line has your
numbers) or `-NoCalibrationPrompt`.
The panel's text is a new stroke font in Spidy's overlay; I checked how the
panel reads on the GPU test's eye image. 179 core checks (7 new: arms scaled
about their shoulders reaching farther controllers, the hero's proportions
and the scales' ranges, a T-pose measured, every panel line, the hold pausing
and starting over, the panel and the font), 13 launcher checks, 87 Python
checks and the GPU test (the panel and the result drawn) pass. Checked in
the game without a headset, on your save: the SPIDY VR tab showed the BODY
section and CALIBRATE BODY, and `tools/probe_menu.py` switched it to ON RESUME
and RESET ALL back to NO (its help then wrapped "T-pose" at the hyphen, so I
reworded it). A new T-pose phase of `tools/probe_game_body.py` posed
Spider-Man's real body as the calibration would for eyes 1.66 m up and arms
0.62 m long: with his own arms his wrists stopped 7-8.5 cm short of the
controllers, with your arm length 0.8-2.7 cm from them (his standing pose
holds the shoulders a little higher than his rest pose, which the measurement
assumes). Your save files were unchanged except `slot0-s.save`, which the
game saved itself. This build changes the VR protocol (the XR config and data
are version 15, the body's status version 2), so it can only go into your
play folder as the whole package; you said not yet. The panel itself needs
the headset. Details: [docs/VALIDATION.md](VALIDATION.md),
[docs/BODY.md](BODY.md).

**Preceding build (October 8, sixth build): five options removed.** You asked to
remove "Webs catch props and thugs", "Web shooter", "Your own body", "Punch
thugs" and "Webs drawn by" from the options. All five are gone from the
launcher's options, and the four that Settings > SPIDY VR had are gone from
there too, with its BODY heading. The tab now has two sections: WEBS (aim
markers, webs hold in open air, swing speed limit, weight) and COMFORT (snap
turn, smooth turn, controller vibration, game screen size). Catching props
and thugs, the web shooter, your body and punching are always on, and the
game draws the webs. If you had switched one of them off, the launcher
forgets that: it no longer reads, saves or passes them on, and ignores them
in the settings a session ends with. `Launch Spidy VR.cmd` keeps `-NoWebGrab`,
`-NoWebShooter`, `-NoBody`, `-NoPunch` and `-OverlayWebs` for testing, and
RESET ALL in the tab leaves those as the session started. The VR protocol is
unchanged (version 14), so only the launcher and the XR module change.
172 core checks, 12 launcher checks and 86 Python checks pass. Checked in
the game without a headset, on your save: the SPIDY VR tab showed the new
rows, and `tools/probe_menu.py` passed every step through them (12 changes;
RESET ALL left the web shooter as the probe started it). Your save files
were unchanged except `slot0-s.save`, which the game saved itself when the
probe left the pause menu. It is in your play folder (`dist\Spidy-0.2.2`):
the launcher, the XR module and README.txt. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 8, fifth build): a WEIGHT setting for swinging.** You
said moving through the air feels too floaty and asked for a weight setting
in the game's settings. While webs fly you (swinging, and after you let go,
until you land) Spidy decides how fast you fall, not the game, and since
October 5 it has pulled at 6 m/s², about 60% of real gravity. That is the
floatiness. The number kept what the October 4 builds applied in practice
(they were set to 18 but applied a third of it), the swing feel you approved
then. Settings > SPIDY VR has a new row under SWING SPEED LIMIT, **WEIGHT**:
40%, 60% (the default: the feel you have now), 80%, 100% (real gravity), 125%,
150%, 200%, 250% or 300% (about the game's own falls, which pull at 30 m/s²).
A change takes hold at once, mid-swing too. Heavier, you drop faster after
letting go, and swings run faster and shorter. Jumps and falls without a web
stay the game's own. The launcher's options have a matching "Weight" list,
`Launch Spidy VR.cmd` takes `-Weight 150`, and the next session starts with
whatever you left it at in the headset. A heavier weight reaches the swing
speed limit sooner (at 300%, a fall from rest reaches 32 m/s in about a
second), so raise that limit too if you feel it cap your falls.
Checked in the game without a headset, on your save: the SPIDY VR tab showed
the row, the left stick stepped it from 60% to 80%, and RESET ALL put it back
(`tools/probe_menu.py`). A new probe (`tools/probe_weight.py`) jumped off your
perch, swung up on a web, let go, and changed the weight mid-flight as the
tab does: Spider-Man fell at 5.88 m/s² at 60%, 14.72 at 150% and 29.53 at
300% (asked: 5.89, 14.72 and 29.43), with every physics step in the air under
Spidy's control. Your save files were unchanged except `slot0-s.save`, which
the game saved itself when the probe left the pause menu, before the jump.
172 core checks (new: the swing taking a new gravity during play, the row and
its steps), the launcher checks, 86 Python checks and the GPU test pass. This
build changes the VR protocol (the XR config and the XR data are version 14),
so it went into your play folder as the whole package, built from the
repository as it is now (the fist fix below included). How each weight feels
needs the headset. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 8, fourth build): fingers no longer twist in a
fist.** You reported that your fingers in VR look twisted and tangled in
most poses. They did whenever a hand made a fist: a squeezed grip (every
web) or a hand moving faster than 1.2 m/s, so most of play. The hand
snapshots of your October 7 23:11 session show it at 64-68 s: the right fist
a claw with its fingertips bent up out of it, the left hand's fingers bent
back out and crossing. Two faults. Each finger joint bent toward the palm
about an axis taken from the bone before it, and that axis turns over once a
bone has curled past the palm's normal: in a full fist every fingertip bent
60° backward, and a fist the game had already closed folded its middle
joints back through themselves. And the solver assumed the rig's rest pose
holds its palms down; Spider-Man's face 30° from down, toward the thumb
(measured from his knuckles, and the game's own finger bends agree), so his
fingers closed diagonally across the palm into each other, and each hand sat
rolled 30° on its controller. Now the palm comes from the knuckles, and each
finger joint turns about its finger's own hinge to a set bend (83°, 95° and
63°, part of the way for a hand closed part of the way); the thumb bends at
its two joints in one plane and swings its tip onto the index and middle
fingers. Spider-Man's own hands in the solver, before and after: the palm
29.9° off the controller's, now 0.0°; a full fist's knuckle, middle and tip
joints 82°, 96° and −60° on every finger, now 83°, 95° and 63°; no joint
bends back any more, from straight fingers, fingers bent back or the game's
own fists. Your hands now sit 30° differently in your grip: tell me how that
feels. 171 core checks (3 new, with his real hands) and 86 Python checks
pass. Not yet run in the game or the headset (`tools/probe_game_body.py
--fists 0.5,1` reports each hand's palm and joints), and not in your play
folder: the fix is in the XR module, so it needs the whole package there,
or a release. Details: [BODY.md](BODY.md#twisted-fingers-october-8).

**Preceding build (October 8, third build): render resolution above the
headset's.** You asked for a way to render above your headset's resolution:
the game looks blurry and jagged, and the launcher's "Eye resolution" went
no higher than "Headset default". Two Discord reports point the same way. A
Quest 3 player over Virtual Desktop rendered 2048 x 2048, a choice that
looked larger than "Headset default" but was below the 2496 x 2688 their
headset asked for. Another player's Virtual Desktop asks for 4032 x 3648,
just under the 4096 a side Spidy allowed. The launcher's option is now
"Render resolution": a percentage of the headset's own resolution per side,
50% to 200% in steps of 5, 100% by default. After Check in the Headset row it
shows the pixels per eye (125% of 3072 x 3264 is 3840 x 4080). The fixed
square sizes are gone, and one saved by an older launcher is dropped, so
that player starts at their headset's resolution again. Eyes may now be up
to 8192 pixels a side; a size the VR runtime does not take shrinks to its
largest with the same shape. Aim markers keep the size they have at 100%:
they are drawn so many pixels wide and would have shrunk with the scale. The
console says "Rendering 4608 x 4896 pixels per eye (150% of the headset's
3072 x 3264)", the report keeps `render_scale` and the headset's
`recommended_eye`, and the launcher's memory figure and the console's
memory warning grow with the scale.
Measured in the game without a headset (`tools/probe_vr_load.py`, your save,
eye occlusion on as in VR, a fresh game for each size): at 100% (3072 x 3264)
90 frames a second looking ahead and 103 turning, GPU 85% busy; at 150%
(4608 x 4896) 66 and 71, GPU 92%. That is about 30% fewer frames, with the
GPU setting the rate. 150% took 2.5-2.6 GB more video memory and as much more
Windows commit, about 108 bytes per extra eye pixel. The game made both
eyes' buffers and viewports 4608 x 4896, and the left eye's image is whole:
no black or cut-off part. Your October 7 session got 77 new eye pairs a
second at 100%; 30% fewer would be well under 72 Hz, so start at 120-130%.
The game screen's copy into the headset image now samples at each pixel's own
centre: at 4608 x 4896 its interpolated coordinate drifted and changed copied
values by 2 levels. XrConfig version 13 (632 bytes: `renderScale` and a spare
word); `-RenderScale 125` for `tools\launch-game-vr.ps1`, `--render-scale` for
`run_game_vr.py`. New checks: the scale's eye sizes (core, and the same cases
in Python), the launcher's arguments and headset size, the scale's limits; 86
Python checks and the GPU test pass, the GPU test now at every size from 1536
to 8192 x 8192. Not in your play folder yet: version 13 needs the whole
package there.

**Preceding build (October 8, second build): a wall the game sticks you to no
longer cuts through your view.** You reported that the player sometimes gets
attached to walls in VR, which looks and feels like clipping through the
wall, until you jump. That is the game's wall crawl: a player who flies into
a wall sticks to it. The game turns the hero onto the wall (his up along the
wall's normal) with his feet on its surface, and VR placed your eyes upright
from those feet: on the wall's plane, or inside it by as far as your head
was toward it in your room. The left eye of your October 7 22:08 session at
180 s shows it; your October 6-7 reports hold six such stretches, 1-2.4 s
each, 20 to 200 m up. Now, while the game holds you on a wall, your head
stands off it: 0.5 m out, reached in about 0.2 s. From there you can lean in
to 0.25 m; lean back and the wall stays where it is. Under a ceiling the head
goes 0.5 m below it. Back on your feet, or jumping off, the view returns over
the feet. Hands, webs and the body go with the head; crawling and the jump off
are the game's, as before (`GameTrackingRig`, `wallClearance`).
Measured in the game without a headset (`tools/probe_wall_crawl.py`, your
save, a wall 6 m from the player, reeled into on a web): the game took the
player at the wall and turned him from upright to 90° in 0.25 s, feet 0.00 m
from the wall's surface; eyes placed as before were 0.00 m from it, as now
0.50 m. On the wall the hero rocks up to 14° about the wall's normal and
snaps back several times a second, so the stand-off is level (straight down
under a ceiling) and does not bob with him. He was upright 0.3 s after the
jump off; Spidy's own flight never tilted him. Session reports carry
`surface` per sample (stretches and frames on a wall, the hero's up, the
stand-off, the head's distance from the wall without and with it; XrData
version 13). 167 core checks (5 new), 84 Python checks and the GPU test pass.
Not in your play folder yet: version 13 needs the whole package there.

**Preceding build (October 8, first build): "Game XR start: 1000" says what to do.** A
player on a Quest 3 through Virtual Desktop got "VR could not start: Game XR
start: 1000". Their game had started at 09:14:46 and the launcher attached
to it at 09:18:21 ("Using the running game."), so VR had already run once in
that game: 1000 is the XR module refusing a second session in one game
process, as every Spidy module does (AUTOMATION.md). STOP VR, or VR that
ends by itself, leaves the game running, and the next START VR attached to
it. The launcher now checks that first, before the headset: a running game
that already holds Spidy's XR module gets "VR already ran in this game, and
Spidy VR starts once per game launch. Close Spider-Man, then press START VR:
the launcher starts the game again.", and a 1000 from the XR start says the
same. PLAYERS.md says it under PLAY and IF SOMETHING IS WRONG. Starting VR
again in the same game would need every module to start more than once; not
done. 84 Python checks pass (new: the check, its message, and no headset
check before it). Only the Python tools and PLAYERS.md changed.

**Preceding build (October 7, seventh build): SteamVR headsets, and the VR
runtime found automatically.** You reported that players with SteamVR
headsets (a Steam Frame, a PSVR2) get the flat game instead of VR, and that
on your Quest 3 over Steam Link VR works but the game's menus can't be
played. Two different causes.

The menus. In your 23:11 Steam Link session the game found two Steam
virtual gamepads (Valve 28de:11ff) through Windows.Gaming.Input six seconds
after it started, logged "XInput disabled, disconnecting controller", and
never read Spidy's controller again: 0 reads in the whole session while
your presses reached Spidy, against 52,644 in your 22:08 Virtual Desktop
session. Spidy gives the game the VR controllers as an XInput controller.
The game reads a setting at startup, EnableWindowsGamingInput in its
registry key, and its code skips Windows.Gaming.Input altogether when that
is 0. The launcher now sets it to 0 for every game it starts and puts your
value back when the game closes, as it does with the small desktop window;
XInput and DualSense controllers work as before. It also turns frame
generation off for VR sessions (yours was already off) and puts it back
afterwards. If the game screen still ignores the controllers (a game started
outside the launcher, or one behind another window), the launcher says so.

The flat start. The Steam Frame player's reports folder (release 0.2.1)
holds only the launcher's first steps: the saved desktop view and two
modules, the second being the probe that finds the game's graphics queue.
No session report and no other module, so the launcher stopped while
looking for that queue and the game stayed flat. On your PC the same step
under SteamVR took about a second (VR started 7 s after the game, as with
Virtual Desktop), so something on their PC differs, and only the launcher's
window said what. The launcher used to give up after three minutes unless
it saw exactly one graphics queue; an overlay or capture tool drawing on a
queue of its own from inside the game's present, or frame generation, makes
that fail every time. Now it takes the queue the game submits on itself
(the others sit deeper in the call stack, or take a quarter of its work or
less) and gives up only with a list of the queues it saw; a call the game
never answers ends the wait at once with that reason; and the search says
what it sees every 20 seconds. Whether this was their cause is not known
yet. So every session now also writes what the launcher's window shows to
`reports\game-vr-<time>-console.log`, and a session that ends before VR
starts writes a small report saying where and why, with the game's own log
and the modules loaded into the game (overlays among them). The next
reports folder from a Steam Frame or PSVR2 player will say what stopped it.

The VR runtime. The launcher's list starts with Automatic, now the default
for everyone. Each session asks Virtual Desktop first (asking it starts
nothing), then SteamVR and Meta Quest Link if their service is running,
then Windows' active runtime, and uses the first that has a headset; a
runtime that is not running and not Windows' active one is named instead of
started. Check shows which runtime has the headset ("Connected: ... via
SteamVR"). Choosing a runtime from the list works as before, and session
reports now record the runtime and headset (`xr_runtime`).

162 core checks, 11 launcher checks (new: the headset check's line), 82
Python checks (new: Windows.Gaming.Input and frame generation off and back,
older backups restoring, the runtime order and what is never started, the
game's queue among several, the queue wait's failures, the warning for an
unread controller, the report of a session that ends before VR, the console
log) and the GPU test pass. Run without a headset, a session that found no
headset in Virtual Desktop wrote its console log and its report. Not yet
seen: the menus over Steam Link with this build (your headset), and a Steam
Frame or PSVR2 session. No VR protocol change: only the launcher and the
Python tools changed.

**Preceding build (October 7, sixth build): smooth turning.** You asked for a
smooth turning option. Settings > SPIDY VR has a new row under COMFORT,
**SMOOTH TURN**: OFF (the default, so the right stick snap turns as before),
or 60, 90, 120, 180 or 240 degrees a second. With it on, holding the right
stick left or right turns you steadily instead of in steps: the further you
tilt it, the faster, up to the chosen speed from nine tenths of the way, and
nothing inside the stick's first fifth. You turn about your head, as with a
snap turn, and Spider-Man's body turns along with you. A game hitch does not
stop a turn you are making, but a stick still held when you come back from a
menu turns nothing until you let it go. The launcher's options have a
matching "Smooth turn" list, `Launch Spidy VR.cmd` takes `-SmoothTurn 120`,
and the next session starts with whatever you left it at in the headset.
Turning also no longer counts as moving your arms for punches: a fist held
out while you turn sweeps through the world at up to 2.5 m/s at 240°/s,
faster than a punch, and it could have knocked a thug over; snap turns had
the same flaw for one frame each. Now the punch measure turns with you and
counts only what your arm does. 162 core checks (new: smooth turning's speed
by tilt and its pivot at the head, a stutter going on turning and a held stick
waiting after a break, turning with a still arm being no punch while a punch
during a turn still is, the new row and its steps), 10 launcher checks, 72
Python checks and the GPU test pass. How smooth turning feels, and the speeds
being right in a headset, need the headset. This build changes the VR
protocol (the XR config and the XR data each have a new version), so it runs
only as a whole package. Details: [docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, fifth build): no more sudden dives in midair.** You
reported being pulled down fast in the air, out of nowhere, as if diving. It
was the game's own fall: while Spidy flies you, the game's airborne state
keeps counting the time you have been in the air, and whenever Spidy let go of
you in midair the game took over at the fall speed that time gives, 36 to 48
m/s down from one frame to the next, and your webs were gone. Your last six
session reports have 14 such handoffs in about 45 minutes of retained play,
half of them straight to 36-48 m/s down. In the 14:13 session there were
three: two during game frames of about a tenth of a second (a frame over 100
ms closes the gate that tells Spidy you are playing, and Spidy took it for a
menu and let go), and one when you resumed from the pause menu in midair.
Now Spidy keeps the player it flies. A hitch keeps your input and your webs
for up to half a second, as if the frame were just late. A longer break
without input (a menu that does not pause the game, lost tracking, the switch
to the flat screen) lets go of the webs, and you glide on under Spidy's own
gravity until you land, where the game takes over as it always did. A
physics step that comes late (a long frame, the end of a pause) still gets
Spidy's last command. And nothing else hands you over in midair any more: a
physics step Spidy did not see, or a gap in the controller samples, used to.
Measured in the game without the headset, on your save, by a new probe that
jumps off your perch, swings up on a web, reels, lets go and then forces each
case. With the build in your play folder, 0.3 s without input while reeling
upward at 11.6 m/s ended the swing: 138 physics steps ran on the game's fall,
Spider-Man went down at 46.9 m/s, and the web was gone. With this build he
went on reeling with the web held, then flew on through a 0.25 s freeze of
the game, 2 s without input and a 3 s freeze like a pause: every physics step
in the air under Spidy's control, the vertical speed changing by at most 0.09
m/s from one step to the next, falling under Spidy's 6 m/s² while gliding.
Your save files were unchanged. 159 core checks (new: a stutter keeps the
input and a longer loss does not, the webs survive a stutter but not a longer
break, the first controller sample after a gap is no yank and keeps the web,
a movement command reaches a late step), 10 launcher checks, 72 Python checks
and the GPU test pass. How gliding after a long break feels, and the drops
being gone in a real session, need the headset; the session report shows them
as physics steps under Spidy's control while the input is unfocused. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, fourth build): VR settings in the game's own
Settings.** You asked for the in-game VR settings as real items in the game's
settings menu, not a separately drawn drawer. They are now a tab of the game's
own Settings: pause with the menu button, choose Settings, and **SPIDY VR** is
the last tab, after KEY MAPPING (Up from GAME reaches it in one press, since
the list wraps; the game reopens Settings on the tab you last used). The game
draws the tab with its own code, so it looks and works like its other tabs:
the sections WEBS, BODY and COMFORT, switches showing the game's ON and OFF,
the swing speed limit, snap turn, controller vibration and game screen size
as lists you step with left and right, each setting's explanation beside the
rows, X to reset the selected setting and Y to reset the whole tab (after the
game's own "Are you sure?") to Spidy's defaults. Changes apply at once, and
the launcher starts your next session with them, as before. A swing speed the
launcher set between two steps (say 33 m/s) shows as the nearest step and
stays as set until you change it. The panel beside the game screen, its tab
and pointing at it are gone, so both triggers always reach the game's menus.
The title screen's Options have no SPIDY VR tab: set things in the launcher,
or pause once your save is loaded. When the pause menu's Settings hand their
tabs to the game's Flash interface, Spidy adds one more, built by the game's
own tab builder from items copied from its settings config but numbered past
the game's 123 settings; Spidy answers the game's questions about those
numbers itself, so the game's own settings and its settings file never see
them. Measured in the game without the headset, on your save, steered with
the virtual controller's left stick as the Touch controllers do: SPIDY VR
appeared after KEY MAPPING and showed the session's values (33 m/s as 32 M/S,
the web shooter off); right on AIM MARKERS switched them off, on SWING SPEED
LIMIT stepped it to 40 M/S, on YOUR OWN BODY switched it off and X put it back,
on SNAP TURN stepped it to 45°; Y then A put back Spidy's defaults, the web
shooter on again; each change reached Spidy's values at once; backing out
resumed play. With Spidy's hooks taken out, Settings showed the game's eight
tabs only; put back in, SPIDY VR returned. Every hook was restored and your
settings file was unchanged (the game re-saved its autosave on Continue, as
it does on every load). 155 core checks (new: the tab's rows and choices,
launcher values between steps, RESET to the defaults), 10 launcher checks, 72
Python checks and the GPU test pass. How the tab reads on the headset's game
screen, and the XR worker taking its changes during a session, need the
headset. This build changes the VR protocol (the XR config and the XR data
each have a new version), so it runs only as a whole package. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, third build): the web shooter.** You asked for the
web projectiles Spider-Man shoots in the game, the web balls of his gadget
button, not the swinging webs. Pull the **trigger** of a hand whose web is not
attached and that hand shoots one: the game's own web-shooter shot, leaving
from your wrist where the controller points. Aimed close to a thug (within 7
degrees, or within his own width when he is near) with nothing in between,
the ball goes to him and the game takes him as its target; otherwise it flies
to the first thing on the hand's line and splats there, or on through open air
until it ends, about 60 m out. One ball per pull: a held trigger shoots once,
and pulls can follow each other every 0.12 s. A short tick in that hand tells
you it left. The trigger still reels in whenever that hand's web is attached,
so the other hand can shoot while one hand swings; a trigger held from a reel,
a menu or a tracking loss shoots nothing until you let go. It is **Web
shooter** under WEBS in the headset's VR settings and in the launcher's
options, on by default; `Launch Spidy VR.cmd -NoWebShooter` starts without
it. Spidy hands each pull to the hero's own web-shooter gadget on the game's
main thread, with the hand's position and the aim it worked out, the way the
gamepad's gadget button hands it a fire event: the game makes the shot, its
look and trail, and what it does to whatever it hits. Spidy's shots take no
gadget ammo and play no arm animation, since the arm is yours. Measured in the
game without a headset, on your save (perched on a lamp post in Times
Square): a scripted hand pulled its trigger 12 times through the same input
as your controllers; each pull fired exactly one shot, from exactly the hand
(0.00 m), within 0.01 degrees of its aim, at 52-60 m/s; aimed 30 degrees down
it struck the pavement 6.5 m away, sideways a wall 10 m away, level and at the
sky it flew 60 m in one second and ended; a trigger held for a second shot
once, three pulls 0.2 s apart three times; switched off and on again during
play it went on shooting; no fault, every hook restored, your save files
unchanged. Captured from the game window, the ball crosses the street as a
white streak. No thug was in free roam, so a ball hitting a thug, and the game
taking the thug you aimed at as its target, await a fight in the headset (the
session report counts both). 158 core checks (new: one shot per pull, none
from a hand whose web is busy or from a trigger held through it, the
interval, the aim assist taking the thug nearest the line but none behind a
wall, surface and open-air aims), 10 launcher checks, 72 Python checks and the
GPU test (the panel one row taller again: 840 x 1410 pixels) pass. This build
changes the VR protocol (the XR config and the XR data each have a new
version), so it runs only as a whole package. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, second build): webs in open air are a setting.**
You asked to make hooking a web at maximum distance without hitting anything
(webs on nowhere) an optional setting, on by default. It is **Webs hold in
open air**, under WEBS in the headset's VR settings and in the launcher's
options, and it starts on. On, webs work as before: a squeeze whose ray meets
nothing within 100 m holds the web in open air there, and the aim marker is
the faint dashed ring. Off, that squeeze misses, as one aimed at a car does,
and a hand aimed into open air shows no marker; webs on buildings, the ground
and props are unchanged. Switched in the headset it applies from the next
squeeze (a web already holding keeps its anchor), and the launcher starts
your next session as you left it; `Launch Spidy VR.cmd -NoAirWebs` starts
with it off. The panel is one row taller (0.9 x 1.4 m). Measured without the
headset: 153 core checks (new: switched off during play, a squeeze into open
air misses and its preview shows nothing, a web already holding keeps its
anchor, a surface still holds, and switched on again open air holds), 10
launcher checks, 71 Python checks, and the GPU test (the panel painted at
840 x 1311 pixels, the new switch showing its own value). In the game, on
your save: a hand aimed at the sky would hold in open air at 100 m; with the
switch off the same aim had nothing to hold (a squeeze would miss), and
switched on again open air at 100 m; no fault, every hook restored, your save
files unchanged. How the new row looks and clicks in the headset is pending.
This build changes the VR protocol (the settings call, the XR config and the
XR data each have a new version), so it runs only as a whole package.
Details: [docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 7, first build): VR settings beside the game's menus.** You asked
for a VR settings section in the in-game menu. The game draws its menus itself
and Spidy draws nothing into them, so the section is Spidy's own panel,
hanging beside them in the headset. Pause with the menu button and **VR
SETTINGS** opens to the right of the pause menu, turned toward you; on every
other game screen (the game menu, the main menu, loading, cutscenes) a small
VR SETTINGS tab hangs there instead, and opens it. Point a controller at a
setting and pull the trigger: switch the aim markers, webs catching props and
thugs, your own body and punching on or off, or step the swing speed limit
(10 to 65 m/s), the snap turn (off, or 15 to 90 degrees; until now always 30),
controller vibration (off, or 25 to 100%) and the size of the game screen
itself (small, medium as before, or large; the flat-mode screen too). Every
change applies at once, in the same session: switching web grabbing off lets
go of what a web holds, switching your body off hides the hero and draws
gloves, a new speed limit holds from the next swing. A dot shows where each
controller points, the line under it lights up, and a click ticks in that
hand. While a hand points at the panel its trigger goes to the panel, not the
game; every other control keeps working the game's menu. The X in the
panel's corner folds it to its tab for the rest of the session, until you open
it again. When the session ends, the launcher keeps what you set for the next
one (X for the markers counts too); its options card has the three new
settings, and `Launch Spidy VR.cmd` takes `-SnapTurn`, `-Haptics` and
`-ScreenSize`. Measured without the headset: 152 core checks (the panel's
layout, hit testing, placement and aim rays, one click per pull, a held or
off-panel pull ignored, the tab, snap turn angles), 10 launcher checks, 71
Python checks, and the GPU test, which paints the panel at your headset's
3072 x 3264 eye size (840 x 1212 pixels for a panel 0.9 m wide) and draws it
into the corner of the right eye's image with every pixel unchanged and the
rest of the image untouched. In the game, on your save: with web grabbing
started off, then switched on, off and on again during the session, a hand
aimed at a prop 51 m away would anchor its web there, catch the prop, anchor,
catch it again; punching switched on and off twice; no fault, every hook
restored, your save files unchanged. How the panel looks and handles in
the headset is pending. Details: [docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 6, evening): an interact button, and markers that
show where each web will land.** You asked for an interaction button in VR
and an optional crosshair for better aim. The game puts its context actions
(picking up a backpack, opening a door, working a console, the prompts it
shows) and, in a fight, its web strike on one button: Triangle on
PlayStation, Y on Xbox. In VR that button was out of reach, because the
controllers' Y opens the game menu. Now **B** gives the game its Y while you
stand, walk, climb or fight; not while a web carries you. A press keeps the
meaning it started with until you let go: B still held from a menu, where it
means Back, does not interact, and an interact held into a menu or scene it
opened does not go Back there. Each free hand also shows an
aim marker where a squeeze of its grip would send its web, worked out in the
game with the same rays and target picks the squeeze itself uses: a white
ring with a dot where the web would hold; a faint dashed ring where nothing
is in reach and the web would hold in open air at 100 m; a red cross where it
would miss (a car, or a wall between you and that point); amber corners
around a prop or thug it would catch instead. A marker faces you, keeps its
size on screen at any distance, has a dark outline so it reads against the
sky and lit walls, and tightens as you squeeze the grip. A hand whose web is
attached shows none, nor does a hand pointing at the floor by your feet.
**X** hides or shows the markers in VR; the launcher's "Aim markers" switch
(or `Launch Spidy VR.cmd -NoAimMarkers`) sets how a session starts. Webs
still shoot exactly as before: the squeeze and the marker now run the same
code. Measured in the game without a headset, on your save (perched on a lamp
post beside a building): a scripted hand aimed 15 ways got the building
beside you at 11-16 m, facades across the street at 36 and 60 m, the ground
below, open air at the sky and down the open street, a miss at the lamp
under your feet, and a catch on a throwable prop 51 m away, with no fault
and every hook restored. 146 core checks, the GPU test (which now draws
every marker) and 69 Python checks pass; the headset session is pending.
Details: [docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 6, sixth build): Spider-Man's body is yours, and
your fists hit.** You asked for Spider-Man's actual avatar as a full-body
presence you control, and for physical punching. In VR you are now the game's
own hero model: look down and you see his torso, legs and arms; his hands are
where your controllers are and turn as you turn them; his head is gone,
because you are inside it. The body stands upright under your head, faces
where you look once you turn more than 34 degrees (a snap turn turns it at
once), bends its knees when you crouch with its feet on the ground, and keeps
the game's leg pose in the air. A squeezed grip or a fast-moving hand closes
into a fist. Spidy turns the hero's joints right after the game's animation
writes them (the pose writer 1601290 and the rig's own joint names), so the
game skins and lights the body itself. A fist moving 2.2 m/s or faster into a
thug is a punch: Spidy hands it to the game's own damage system as a melee
blow from the hero, 10-40 damage by the fist's speed, with the game's
reaction for that strength (a flinch, a stagger, a knockdown, a flight; an
uppercut pops him up), and the controller kicks. Measured in the game without
a headset, on your rooftop save: both wrists landed 0.0 mm from scripted
controller targets and the head joint 0.0 mm from its place behind the eyes,
in 0.03 ms a frame; from the eyes, looking down, you see his arm reaching to
the controller with a closed fist, the web shooter at the wrist, and his feet
on the vent below. A scripted fist driven through a thug's chest at 6 m/s,
through the same input path as your controllers, landed one punch (5.6 m/s, a
knockdown, 29 damage) and took the thug's health from 100 to 68. The thugs in
reach were in a scripted scene and lost health without reacting; a thug in a
fight reacting to a punch still needs a crime or the headset. With the body
off, or whenever it is not on the hero, the hero stays hidden and the overlay
draws gloves, as before: `Launch Spidy VR.cmd -NoBody`; `-NoPunch` turns
punching off; Spidy Launcher has a switch for each. 143 core checks, the GPU
test and 68 Python checks pass; the headset session is pending. Details:
[docs/BODY.md](BODY.md).

**Preceding build (October 6, fifth build): a web is a rope, and what it holds
has weight.** You said webbed objects felt weightless, as if the web were a
stick (you could hold a bin up in the air with it), that they felt weightless
once let go too, and that the web you let go of stayed in the air where the
object had been. In the 10:16 session a held prop followed a point 1.35 m ahead
of your hand, held up, so a turn of the wrist swung it fast, and a throw
multiplied its speed by 2.2: two of four throws left at the 40 m/s cap. Yanks
launched props at 17-45 m/s, mostly upward, and four you let go of in flight
kept rising at 12-13 m/s. Now every web is a rope that only pulls, with at most
2400 N: 80 m/s² on a 30 kg trash can, less on anything heavier, and one web can
drag but not lift anything over 245 kg (two can). A caught prop hangs below
your hand on a short web, swings when you move and settles when you stop;
pointing the hand ahead or turning the wrist neither lifts nor swings it. A
throw keeps the speed your swing gave the prop, times 1.6 for a light prop and
less for a heavy one, up to 25 m/s: in a simulation a hard underhand swing
throws a can at 13 m/s and a flick of the wrist at 0.3 m/s. Yanks fly at 7-15
m/s by how hard you pull; where the arc falls short (up to a rooftop) the web
reels the prop in along itself, and a prop coming in is caught near your hand
instead of flying past it. The controller hums with the web's pull: faintly
for a hanging can, strongly when you swing it hard or it is too heavy to lift.
The web you let go of now stays on the prop as it dissolves, going with it.
You tried it and found it good, except that thrown bins sank into the floor.
They are breakable props of two bodies: the game draws the whole bin from its
top piece, and that is the only part Spidy's freeing lets move; the bin's own
body stays held where it stood, invisible. Measured in the game: the top piece
falls through the bin when freed and lies on the road after a throw, with the
bin drawn 0.8 m below it. Freeing a bin the way the game's own yank breaks it
off is the open fix. 125 core checks, the GPU test and 66 Python checks pass.
Details: [docs/WEB-GRAB.md](WEB-GRAB.md) and
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 6, fourth build): the VR frame rate about doubles.**
You reported that VR felt slow although the CPU and GPU showed about 20% load
and the disk 44%. In the 10:16 session the headset ran at 90 Hz, and the game
made 38 new eye images a second (41-64 frames a second in its own log); a third
of the headset's frames repeated an older image. Measured in the game without a
headset, both eyes at the headset's 3072 x 3264: one game thread, the one that
turns each frame's render commands into GPU commands, was busy 95% of the time
while the main thread waited on it and the GPU was 58-77% busy. Task Manager
averages the CPU over 16 threads, so one saturated thread shows as 6%; and the
game pauses while another window is in front, so what Task Manager shows after
an alt-tab is a paused game. That thread's work grows with what the views draw,
and Spidy's eyes drew everything in their field of view: the engine creates
offscreen views without the occlusion culling its own view has, so buildings
hidden behind nearer ones were drawn anyway: a frame carried 2.3 to 3.8 times
the render commands it carries with culled eyes. Each eye now gets the engine's
own occlusion culling when it is created. On the rooftop of your save, looking
ahead: 65 frames a second before, 125 now (render commands 31 MB a frame
before, 13 MB now); turning a full circle: 48 before, 105 now. Nothing visible changed: at four headings the eye
images with and without the culling differ only where two captures differ
anyway (moving cars and people, birds, leaves), and their average brightness
within 0.4%. The GPU, 80-88% busy now, is close to being the limit at this
resolution. `-NoEyeOcclusion` turns the culling off for comparison. The disk
load is the game streaming textures (in VR its texture budget is full, 3,868 of
3,874 MB); it is not what held the frame rate down. At 120 Hz the earlier
sessions sent the headset only 71 frames a second (Virtual Desktop's end of
frame took 13.4 ms each time), while at 90 Hz it kept up; whether 120 Hz keeps
up now needs the headset. Also fixed: a launch could end with `WinError 24`
when the launcher listed the game's modules while the game was still loading
its own; it now asks again, as Windows documents. 120 core checks, the GPU test
and 58 Python checks pass; the headset session is pending. Details:
[docs/VALIDATION.md](VALIDATION.md).

**Preceding build (October 6, second build): walking, jumping and web pulls work
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
[docs/WEB-GRAB.md](WEB-GRAB.md). 114 core checks, the GPU test and 56
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
  [Memory for a VR session](DEVELOPMENT.md#memory-for-a-vr-session).

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
[Graphics settings in VR](DEVELOPMENT.md#graphics-settings-in-vr).

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
