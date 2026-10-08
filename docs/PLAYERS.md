SPIDY - VR for Marvel's Spider-Man Remastered
by Ilya Mezerowsky

Support Spidy: https://ko-fi.com/ilyamezerowsky
Spidy is free. It is built with a lot of AI help, and those tokens aren't free.
If you enjoy it, a tip on Ko-fi keeps the updates coming.


WHAT YOU NEED
-------------
- Marvel's Spider-Man Remastered on Steam. Spidy works with the Steam version
  only; the Epic Games Store version is a different build.
- A PC VR headset and its OpenXR runtime. Spidy has been played on a Quest 3
  through Virtual Desktop (https://www.vrdesktop.net/) and through SteamVR
  (Steam Link). Other SteamVR headsets and Meta Quest Link should work too.
  The launcher finds the runtime your headset is connected to.
- About 19 GB of memory Windows can give programs (RAM plus page file). The
  launcher shows how much you have.


INSTALL
-------
1. Extract this whole zip to a folder you can write to, for example
   Documents\Spidy. Do not run it from inside the zip.
2. Start "Spidy Launcher.exe". Windows may say it protected your PC, because
   Spidy is not signed by a company: choose "More info", then "Run anyway".
3. The launcher finds the game and your VR runtime and checks this PC. Fix
   anything marked red. It can install Microsoft's Visual C++ runtime for you,
   and "Add shortcuts" puts Spidy VR on your desktop and in the Start menu.

Spidy changes nothing in the game's folder. Some antivirus programs dislike
tools that attach to a running game; if files go missing after extracting,
allow the Spidy folder and extract it again.


PLAY
----
1. Close the game if it is running.
2. Put on the headset and connect it to the PC (Quest 3: open Virtual Desktop
   and connect, or Steam Link; SteamVR headsets: start SteamVR).
3. Press START VR. Steam starts the game. Its intro and menus show on a screen
   in the headset; pick your save with the VR controllers. VR takes over as
   soon as you play.
4. The first time, a panel asks you to calibrate your body: stand tall, look
   ahead, stretch both arms straight out to the sides (a T-pose) and hold
   both triggers until the bar fills, about a second and a half. Spider-Man
   then has your height and arm length, so his hands sit on your
   controllers. B skips it. The launcher remembers it; "Redo" under its
   options, or CALIBRATE BODY in SPIDY VR (below), does it again.

Keep the game window in front on the desktop: the game pauses while another
window is in front of it, the launcher included. If the headset shows a still
picture, click the game window once.

Press STOP VR, or close the game, to end the session. Each session leaves a
report in the "reports" folder; send it along when you report a problem.
VR starts once per game launch: to play in VR again after STOP VR, or after
VR ended by itself, close the game first, then press START VR.

For VR the launcher starts the game with a small desktop window, without
frame generation, and with the game's Windows.Gaming.Input off (under
SteamVR it would take the controllers away from Spidy). Your settings come
back when the game closes.


CONTROLS
--------
Swinging
  Squeeze a grip ........ shoot that hand's web; hold to swing
  Release the grip ...... let go
  Trigger ............... reel in while the web is attached
  Pull a hand sharply ... zip toward the web's anchor

Grabbing
  Grip aimed at a prop or thug ... catch it with your web
  Trigger / sharp pull ........... reel it in / yank it to your hand
  Release the grip ............... throw it (swing harder to throw faster)
  A thug you pull flies off his feet, flailing, and lands hard when thrown.
  Slammed into a wall or the ground he gets hurt, and thrown into another
  thug, or hit by a prop you throw, that thug goes down too.

Web shooter
  Trigger of a hand ...... shoot a web ball where that hand points: the
  without a web            game's own, from your wrist. Aimed close to a
                           thug, it goes to him; otherwise it splats on the
                           first thing in its way. One ball per pull; the
                           other hand can shoot while one hand swings.
                           Three quick hits web a thug up.

Aim markers (where each hand's web would go if you squeezed its grip now;
the left hand's are blue, the right hand's orange)
  Ring with a dot ...... the web holds there
  Faint dashed ring .... nothing in reach: it holds in open air (with "Webs
                         hold in open air" off it would miss: no marker)
  Red cross ............ it would miss (a car, or a wall in your way)
  Ring of three arcs ... it catches that prop or thug (the ring turns, and
                         closes in as it locks on)
  X .................... hide or show the markers (SPIDY VR in the game's
                         Settings has the same switch; the next session
                         starts as you left it)

Your body and fists
  Look down ............ you are Spider-Man: his body under you, his arms
                         and hands where your controllers are
  Punch a thug ......... a fist moving fast into him hits: the faster, the
                         harder he is knocked back (an uppercut lifts him)
  Squeeze a grip ....... closes that hand into a fist (it shoots a web too)
  T-pose, both triggers  calibrates your body (when the panel asks): his
                         height and arms become yours. B skips it

Moving
  Left stick ........... walk and run
  Right stick .......... flick left or right to turn (30 degrees; SPIDY VR
                         in Settings sets how far, or turns it off). With
                         SMOOTH TURN on, hold it to turn steadily instead:
                         the further you tilt it, the faster
  A .................... jump (from the ground, a wall or a perch; in the
                         air it does nothing, no web zip from the wrist)
  B .................... interact: the game's Y (backpacks, doors, prompts;
                         web strike in a fight)
  Menu button .......... pause
  Y .................... game menu (map, suits, skills)
  Click both sticks .... switch between VR and a flat game screen

Menus and cutscenes show on a screen in the headset. There the controllers act
as an Xbox controller: left stick moves, A selects, B goes back, grips switch
tabs, the menu button is Start.

VR settings
  They are a tab of the game's own Settings: pause with the menu button,
  choose Settings, then SPIDY VR, the last tab (Up from GAME gets there in
  one press). It works like the game's other tabs: the left stick moves and
  changes the selected setting, X resets it, Y resets the whole tab, B goes
  back.
  Switch the aim markers and webs holding in open air (a web that meets
  nothing within 100 m holds there; off, it misses) on or off, or step the
  swing speed limit, your weight, snap turn, smooth turn, controller
  vibration and the game screen's size. Changes apply at once. The launcher
  starts your next session with them; its options show them too.
  CALIBRATE BODY: set it to ON RESUME and resume, and the T-pose panel asks
  for your measurements again.
  Catching props and thugs, the web shooter, your own body and punching are
  always on.
  The title screen's Options have no SPIDY VR tab: load your save first, or
  set them in the launcher.


IF SOMETHING IS WRONG
---------------------
- "This game version is not supported": the game was updated, or it is not
  the Steam version. Spidy reads the game's code at fixed places, so a game
  update needs a Spidy update.
- The headset is not found: connect it first (SteamVR headsets: start
  SteamVR), then press Check in the launcher. On Automatic it shows which VR
  runtime has your headset; if it picks the wrong one, choose yours in the
  list.
- "VR already ran in this game" (older versions: "Game XR start: 1000"):
  VR starts once per game launch. Close the game (Spider-Man.exe gone from
  Task Manager), then press START VR.
- VR does not start and the game stays flat: send the whole "reports"
  folder. The newest game-vr-...-console.log holds everything the launcher
  showed, and the report beside it says where it stopped.
- The game's menus ignore the VR controllers: keep the game window in front
  on the desktop, and start the game from the launcher with the game closed
  (the launcher turns off what lets SteamVR's virtual gamepads take over).
- The picture breaks up: start the game from the launcher with the game
  closed. Attached to a game that is already running, Spidy cannot give it
  the larger render memory VR needs.
- Low frame rate: lower "Render resolution" in the launcher's options (below
  100% renders fewer pixels than your headset asks for), or your streaming
  quality in Virtual Desktop.
- Blurry or jagged edges: raise "Render resolution" in the launcher's
  options above 100%; try 125% first. It costs frame rate and memory: 150%
  gave about 30% fewer frames on an RTX 5090. Press Check beside your
  headset to see the pixels per eye. Virtual Desktop's quality setting
  raises what 100% is.
- The aim markers distract you: press X in VR to hide them, or switch "Aim
  markers" off in Settings > SPIDY VR.
- Webs shot at the sky hold somewhere you can't see: switch "Webs hold in
  open air" off in Settings > SPIDY VR or the launcher's options; a web then
  needs something within 100 m to hold on to.
- Web balls fly when you only meant to reel in: the trigger reels only while
  that hand's web is attached; otherwise it shoots.
- Snap turning makes you uneasy, or the vibration is too strong: Settings >
  SPIDY VR has both.
- Swinging feels floaty, or falls too hard: WEIGHT in Settings > SPIDY VR
  (or "Weight" in the launcher's options) sets how heavy you are while
  swinging and after letting go, until you land: 100% is real gravity, 60%
  the default, up to 300%. Jumps and falls without a web are the game's own.
- You would rather turn smoothly than in steps: SMOOTH TURN in Settings >
  SPIDY VR (or "Smooth turn" in the launcher's options). If it makes you
  uneasy, try a slower speed, or OFF for snap turning again.
- The menu screen is too small or too large: "Game screen size" in Settings
  > SPIDY VR.
- The game asks for Y (Triangle) to interact: in VR that is B.


SHARING
-------
Please share the original zip with this credit intact, so players get working
files and know where updates come from.

Spidy is a free fan project, not affiliated with or endorsed by Insomniac
Games, Sony Interactive Entertainment, Marvel or Valve. Third-party licences
are in THIRD_PARTY_NOTICES.md and docs\licenses.
