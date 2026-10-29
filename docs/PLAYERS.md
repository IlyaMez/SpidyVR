SPIDY - VR for Marvel's Spider-Man Remastered
by Ilya Mezerowsky

Support Spidy: https://ko-fi.com/ilyamezerowsky
Spidy is free. It is built with a lot of AI help, and those tokens aren't free.
If you enjoy it, a tip on Ko-fi keeps the updates coming.


WHAT YOU NEED
-------------
- Marvel's Spider-Man Remastered on Steam. An Epic Games Store copy of the
  same version is tried as well, but nobody has played Spidy on one yet: if
  VR does not start on it, the session log says why, and we'd like to see it.
- A PC VR headset and its OpenXR runtime. Spidy has been played on a Quest 3
  through Virtual Desktop (https://www.vrdesktop.net/) and through SteamVR
  (Steam Link). Other SteamVR headsets and Meta Quest Link should work too.
  The launcher finds the runtime your headset is connected to.
- A gaming PC. It need not be new: players report good performance on lower
  settings with PCs like a GTX 1080 and 16 GB of RAM.
- Windows. On Linux, Spidy runs inside the game's Steam Proton session: that is
  experimental and has its own steps, in LINUX.md in this folder.


INSTALL
-------
1. Extract this whole zip to a folder you can write to, for example
   Documents\Spidy. Do not run it from inside the zip.
2. Start "Spidy Launcher.exe". Windows may say it protected your PC, because
   Spidy is not signed by a company: choose "More info", then "Run anyway".
3. The launcher finds the game and your VR runtime and checks this PC. Fix
   anything marked red. It can install Microsoft's Visual C++ runtime for you,
   and "Add shortcuts" puts Spidy VR on your desktop and in the Start menu.
   Its tabs: Play (START VR, what this PC has, the session log), Options,
   Controls (a picture of them, then each in words) and About.

Spidy changes nothing in the game's folder. Some antivirus programs dislike
tools that attach to a running game; if files go missing after extracting,
allow the Spidy folder and extract it again.


UPDATES
-------
When it starts, the launcher asks GitHub whether a newer Spidy is out. If one
is, a bar at the top of the Play tab offers it. "Update now" downloads it, checks
it, puts its files in this folder and starts the new launcher, all in a few
seconds. Your settings and the reports folder stay. Stop VR and close the
game first. "Later" asks again at the next start. About > UPDATES checks
again on request, and its switch turns the check at start off.
The folder keeps the name it was extracted with (Spidy-0.2.7, say) through
updates; About shows the version inside.


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
   controllers. B skips it. The launcher remembers it; "Redo" on its Options
   tab, or CALIBRATE BODY in SPIDY VR (below), does it again. Rather enter
   your height? Set HEIGHT (SPIDY VR, or "Height" in the launcher's options).
   Playing seated? Switch SEATED on first (below), then calibrate sitting up.

Keep the game window in front on the desktop: the game pauses while another
window is in front of it, the launcher included. If the headset shows a still
picture, click the game window once.

Press STOP VR, or close the game, to end the session. Each session leaves a
report in the "reports" folder; send it along when you report a problem.
Asking for help on Spidy's Discord (https://discord.gg/x27ZXDpdNt)? Press
"Copy for Discord" above the launcher's session log and paste what it copied
into your post: a few lines with your Spidy version, VR runtime, headset,
game version, graphics card, memory, options and how the last session ended.
Your Windows user name is not in them.
VR starts once per game launch: to play in VR again after STOP VR, or after
VR ended by itself, close the game first, then press START VR.

For VR the launcher starts the game with a small desktop window, without
frame generation or vertical sync, and with the game's Windows.Gaming.Input
off (under SteamVR it would take the controllers away from Spidy). It turns
Ray-Traced Reflections off too: the headset cannot show them, and with them
on the game ended with its display driver error (DXGI_ERROR_DEVICE_HUNG)
when a menu, the map or a cutscene opened. Leave them off in the game's
settings while you play in VR. It puts the game's Aspect Ratio at Auto:
with a fixed shape chosen there, players' games ended as VR started. And it
turns NVIDIA Reflex off: the headset paces the game's frames, not the
monitor, and one player's game with Reflex on lost its graphics device as
VR started. The
launcher also turns the game's Depth of
Field off and its Sharpness to the lowest: in the headset the game blurs
away from where its own camera focuses, not from where you look, and its
sharpening rings on edges. "Game's depth of field" and "Game's sharpening"
in the launcher's options keep your own. Your settings come back when the
game closes.

Spidy cannot share the game with another VR mod. If the R.E.A.L. VR mod's
files are in the game's folder (dxgi.dll, RealVR.ini and others), the
launcher names them and does not start VR: with them the headset stays
black. Move them out of the game's folder; Steam's "Verify integrity of game
files" does not remove them. Other tools that load into the game from its
folder (ReShade, for one) are named in the session log with a warning, and
VR starts.

While you swing in VR, the game window on the desktop shows your left eye
as the headset shows it, with Spidy's own marks on it (the combat marks, the
aim markers, the focus meter), 30 pictures a second: every picture sent to
the monitor costs the graphics card time the headset could have. "Monitor
frame rate" in the launcher's options chooses Every frame, 60, 30, 15 or 5 a
second; 60 makes a smoother recording of the window. On menus, cutscenes
and the flat screen the window shows the game as usual. For a wider view
to record, pick an angle for RECORDING FOV (the SPIDY VR tab, DISPLAY): the
window then shows the game's own view, at least that wide across, with none
of Spidy's marks, every frame whatever the monitor frame rate, and it costs
frame rate in the headset.

The picture in each eye is anti-aliased and upscaled by the game itself:
the scene is rendered smaller and built up to the eye's full size from the
last few frames, as DLSS does (any graphics card). "Upscaling" in the
launcher's options chooses how much smaller: Quality (67%, the default),
Balanced or Performance for more frames, High or Off for the sharpest
picture. The game's own DLSS, FSR or XeSS setting does not reach the
headset, whatever it is set to.

Wet streets, water and glass mirror their surroundings in the headset when
Screen Space Reflections is on in the game's own graphics settings (Settings >
Display and Graphics > Graphics), as on the monitor. Turned off there, they
are off in the headset too; turned on there while you play, they appear in
the headset the next time you start VR. Ray-Traced Reflections never reach
the headset: the launcher turns them off for the session, as said above.


CONTROLS
--------
The grip webs and the trigger reels, as below. To shoot webs with the
trigger instead, set WEB BUTTON to TRIGGER (Settings > SPIDY VR, or "Web
button" in the launcher's options): the trigger then shoots and holds a
web, and the grip reels in and shoots web balls. Fists, the T-pose
calibration and the game's menus keep the buttons as they are.

Swinging
  Aiming ................ webs leave the web shooter on your wrist and go
                          where your hand points: along it, past the knuckles
  Squeeze a grip ........ shoot that hand's web; hold to swing
  Release the grip ...... let go
  Trigger ............... reel in while the web is attached
  Pull a hand sharply ... zip toward the web's anchor
  Pull a hand in ........ the web is a rope in your hand: hanging from it,
                          you go where you pull it, as fast as you pull
  Other hand on the web . put your free hand on the web you hold (within a
  and squeeze its grip    hand's width of it, anywhere from the hand that
                          holds it upward; it ticks when it is there) and
                          squeeze: it takes hold of the same web, with no
                          shot and no shot's sound. The hand that holds the
                          web higher up carries you, as on a rope: pull with
                          it and you go up; the lower hand holds the slack
                          and moves nothing, wherever you move it. Let go
                          with the lower one, take the web again above the
                          other, pull: hand over hand you climb it. Either
                          hand alone keeps the web when the other lets go,
                          where it is, with no drop. Away from the web, the
                          grip shoots that hand's own web as before
  A corner in the way ... the web bends round it and holds you from there;
                          swing back and it lies straight again. Round six
                          edges at most
  Walking with a web .... it pays out as you go, and holds you at its new
                          length when you step off an edge
  In the air ............ a web never goes slack under you: come nearer its
                          end (a zip, a jump, flying up) and it takes the
                          slack in, so it holds you from where you got to.
                          Slack you make reaching toward it stays
  Swinging low .......... holding a web you skim the street, feet just over
                          it, and swing on; let go to land

Grabbing
  Grip aimed at a prop or thug ... catch it with your web
  Trigger / sharp pull ........... reel it in / yank it to your hand
  Release the grip ............... throw it (swing harder to throw faster)
  A thug you pull flies off his feet, flailing, and lands hard when thrown.
  Slammed into a wall or the ground he gets hurt, and thrown into another
  thug, or hit by a prop you throw, that thug goes down too.
  "Thug" here means any enemy, flying ones included.

Web shooter
  Trigger of a hand ...... shoot a web ball where that hand points: the
  without a web            game's own, from your wrist. Aimed close to a
                           thug, it goes to him; otherwise it splats on the
                           first thing in its way. One ball per pull; the
                           other hand can shoot while one hand swings.
                           Three quick hits web a thug up.
                           It is the game's Web Shooter's shot, as its RB
                           fires it, while that gadget is the one equipped
                           and has a charge: it seals a research station's
                           leaks and vents and webs up turrets and thugs as
                           the game made them. Out of charges, the ball
                           still flies but does nothing to those until the
                           charges come back.

Aim markers (where each hand's web would go if you pressed its web button
now; the left hand's are blue, the right hand's orange)
  Ring with a dot ...... the web holds there
  Faint dashed ring .... nothing in reach: it holds in open air (with "Webs
                         hold in open air" off it would miss: no marker)
  Red cross ............ it would miss (a car, or a wall in your way)
  Ring of three arcs ... it catches that prop or thug (the ring turns, and
                         closes in as it locks on)
  AIM MARKERS .......... hides or shows the markers: Settings > SPIDY VR, or
                         "Aim markers" in the launcher's options (the next
                         session starts as you left it). X no longer
                         switches them: held, it opens the gadget wheel

Your body and fists
  Look down ............ you are Spider-Man: his body under you, his arms
                         and hands where your controllers are
  Punch a thug ......... a fist moving fast into him hits: see Fighting,
                         below
  Shoot a web .......... the hand makes Spider-Man's web-shooting hand: the
                         two middle fingers on the trigger in the palm, the
                         first and little fingers and the thumb out. They
                         come down as the web button goes down; once the web
                         holds, the hand closes into a fist on it for as
                         long as you hold the button. A web that misses
                         keeps the shooting hand while you hold the button,
                         and a web ball shows it for half a second
  Take the other hand's  that hand closes into a fist on the web
  web
  Squeeze a grip ....... otherwise closes that hand into a fist (the grip
                         when the trigger is your web button), and so does a
                         hand moving fast; with an enemy marked or within
                         reach a fast hand is a fist even while it shoots
  T-pose, both triggers  calibrates your body (when the panel asks): his
                         height and arms become yours. B skips it

Fighting (web strikes: punching across the room, as in Batman: Arkham Shadow)
  Punch a thug ......... a fist that meets him fast lands a blow, as hard as
                         it moved. Every blow kicks in your hand, sounds as
                         the game's own blows do (its hit on him, as hard as
                         yours was; BLOW SOUNDS in Settings > SPIDY VR picks
                         Spidy's own tones or none instead), bursts where it
                         landed (a comic's star: yellow for a jab, red for a
                         harder blow) and stops time for a moment
  Punch at a thug ...... web strike: you fly to him and the blow lands as
  out of reach           you arrive. He can be 10 m away, anywhere you can
                         see him: across the street, below you, in the air.
                         From the ground, a jump, a fall or a swing (your
                         webs let go). Throw the fist at him; it need not
                         be a hard punch
  Punch along your ..... the same: you go to him, and the web lets go. A
  web at the thug        sharp pull still yanks him to you instead
  it holds
  Swing, zip or fall ... a dive: come at the thug the ring marks fast (9 m/s
  into a thug            or more) off the ground - a swing, a zip, a jump, a
                         fall - and a third of a second from him you go the
                         rest of the way feet first, time slowing, and kick
                         him: a heavy blow as hard as you were fast, that
                         sends him flying the way you came. Your webs let go,
                         and in the air you hang beside him for the next
                         blow. Only at the ringed one, and only on course for
                         him: a swing past a crowd kicks nobody
  A hard punch ......... sends him flying, the harder the farther
  Uppercut ............. lifts him into the air. Strike up after him: you
                         hang beside him for a moment after every blow you
                         land in the air, long enough for the next
  Punch down at a ...... slams him into the ground
  thug in the air
  Punch down onto a .... knocks him down, stunned
  thug on his feet
  Blows in a row ....... count up (X2, X3 ... lettered over each blow): each
                         hits a little harder than the one before, the fifth
                         is a finisher that sends him flying far, once you
                         throw it as called (below). Three and a half
                         seconds without a blow and the count starts over
  A ring over a thug ... your next punch goes to him and to nobody else.
                         A thin ring is on the enemy you look at, up to
                         10 m away, and moves with your look. Hit him and
                         the ring locks on: it gets four ticks and stays on
                         him wherever you look, until he is down. A web
                         that holds a thug locks him too. While one is
                         marked no punch flies you to another: a fist that
                         falls short of him is just a miss. No ring, no web
                         strike: look at him first
  Changing your mind ... look squarely at another enemy for a third of a
                         second, with the locked one well off to the side:
                         the ring goes to him. Four seconds without a blow
                         at the locked one and the ring follows your look
                         again
  KO! .................. a thug your blow knocked out: lettered where he
                         stood, with a thump in both hands and time easing
                         for an instant. He is done, the ring is free: look
                         at the next one
  An arrow into the .... a called blow, as Arkham Shadow's. From your second
  ring                   blow on, the arrow shows how your next fist must
                         travel, as you see him. It comes in from the side
                         your fist comes from and points where it goes:
                         from your right to your left (a right hook), from
                         your left (a left hook), from below (an uppercut)
                         or from above (a blow down). Do it within arm's
                         reach and the fist that lands that way is the
                         called blow. Do it at the marked thug from across
                         the room and you fly to him and it lands as a
                         called blow all the same: swing it, do not jab. It
                         hits half again as hard and throws him that way: a
                         hook sends him flying, up lifts him, down floors
                         him or slams him out of the air. Then the arrow
                         shows a new way. A plain punch or a straight web
                         strike is an ordinary blow and the arrow stays as
                         it is. The arrow is white over a blue ring, and
                         the ring runs down as your combo's time does. A
                         called blow's burst is printed out of register, a
                         blue edge and a pink one
  The yellow arrow ..... into a jagged red ring: it calls the finisher.
                         After four blows the next one is the finisher only
                         if a fist lands, or opens a web strike, going the
                         way the yellow arrow shows; until then your blows
                         are plain ones and the count stays at four
  How the marks look ... as a comic prints them, like Spidy's videos: flat
                         colour with a black outline, popping in rather than
                         fading. They are drawn the same in both eyes, so a
                         coloured edge is print and not your lenses
  Spider-Sense ......... an enemy about to hit you: the comic's jagged
                         lines over his head, when the game itself warns
                         you (its Spider-Sense sound), inside a white ring
                         that closes on him as the blow comes. Its last
                         notch is a yellow burst: now. The controller on
                         his side tingles. Its colour says what to do:
  Blue lines ........... counter: throw a fist at him. His attack breaks
                         at once, and he is your target whoever the ring
                         was on: within reach the fist lands, out of it you
                         fly to him as a web strike does (10 m at most).
                         The blow sends him flying, time slows for a
                         moment, and it counts in your combo. A punch that
                         lands on him while his lines show counts too
  Red lines ............ dodge: B. The game says nothing breaks this attack
                         (a heavy's charge, a blow from too far to counter)
  Out of the way ....... while red or blue lines show, step or lean quickly
                         to a side (17 cm), step back (20 cm) or duck (15
                         cm): that is the game's dodge, the way you moved (a
                         duck: the way the game picks), its perfect dodge and
                         its slowed time too. A lean toward him is none (a
                         counter leans in). Not while a web carries you
  On a web ............. swinging, zipping or pulled along a web, faster than
                         a jog (3 m/s), red and blue lines' blows are
                         dodged for you just before they land: the game's
                         own dodge on the move, its Focus and its sound, a
                         whoosh across the vest. Its perfect dodge does not
                         slow time there: the swing goes on
  Yellow lines ......... under a white chevron: jump over it, A
  Out of view .......... an enemy behind you or to the side: his mark shows
                         small near the middle of your view on his side,
                         with an arrowhead pointing to him, and his lines
                         at the edge of your sight on that side. A fist
                         thrown his way counters him unseen, as in Arkham
                         Shadow
  Yank a thug from ..... he flies to you, wherever you go meanwhile and
  the air                whether or not you still hold the grip, and floats
                         an arm's length in front of you. As he arrives
                         time slows and you hang in the air (not while a
                         web carries you): hit him. Each blow in the air
                         holds you up a little longer. After a second and a
                         half without a blow he falls
  A hand whose web holds a prop, or carries you through the air, neither
  punches nor strikes. A hand whose web only hangs from a wall while you
  stand does both: clench your fists as you like.

Moving
  Left stick ........... walk and run (with REALISTIC FLIPS and FLIPS on,
                         in the air it steers rotation, below)
  Right stick .......... flick left or right to turn (30 degrees; SPIDY VR
                         in Settings sets how far, or turns it off). With
                         SMOOTH TURN on, hold it to turn steadily instead:
                         the further you tilt it, the faster. Basic FLIPS
                         uses it to rotate while airborne, below
  A .................... jump (from the ground, a wall or a perch; in the
                         air it returns a basic flip upright, below).
                         On the ground the jump is made as you let A go
  Hold A ............... a charged leap: hold A on the ground (your hands
                         buzz as it builds, for most of a second) and let
                         go. You leap 4 to 11 m high by how long you held,
                         and forward too with the left stick tilted. It is
                         Spidy's own: the game's Charge Jump is a skill a
                         save may not have
  B .................... dodge: the game's B. You dodge the way the left
                         stick points; with the stick at rest the game
                         chooses the way. Not while a web carries you: the
                         web dodges for you then
  Y .................... interact: the game's Y (backpacks, doors, prompts;
                         web strike in a fight). Look at the thing and
                         press Y, within about a metre of it: you turn to
                         it and the game takes it. While you swing too: a
                         web strike at a thug, or onto a car in a chase,
                         takes you from your webs once the game begins it
  Hold Y ............... you step up to what you look at until the game
                         takes it (2 seconds at most; let go to stop). At a
                         door or a console: look at it and hold Y. Not in a
                         fight
  Right stick down ..... heal: the game's D-pad down. Push the stick
                         straight down and hold it a moment; the right
                         controller ticks when the game gets it. In the air
                         too. It spends the game's Focus, which your
                         punches, web strikes, yanks and throws fill as
                         the game's own do (since October 22; before,
                         none of them filled it).
                         An air blow or a yank fills about three times
                         what a punch on the ground does
  Right stick up ....... the game's LT + RT: its zip to a point. Where the
                         game shows LT + RT (a perch, a vent to enter),
                         look at it, push the stick straight up and hold
                         it. While you swing, or from Spidy's wall, too:
                         the game takes you from your webs once its zip
                         begins. With no point to zip to, keep holding:
                         that is the game's aim, and time slows after a
                         moment. Look at a point and let go of the stick:
                         you zip there (the right controller ticks)
  Hand to your temple, . scan your surroundings: the game's R3. Raise either
  trigger                hand beside your head and pull its trigger there;
                         it holds for as long as you hold the trigger. No
                         web ball from that pull
  Both hands at your ... the game's suit power (its L3 + R3, which nothing
  chest, both triggers   sent in VR before): both hands before your chest,
                         pull both triggers together (within half a second
                         or so). No web balls from those pulls. The game uses
                         its power when its meter is full, as in the flat game.
                         Both hands buzz as it goes out. Builds before
                         October 23 buzzed but sent the game nothing
  Menu button .......... pause
  Hold both sticks in .. pause too, after a moment (both hands buzz). Under
                         SteamVR the Quest's menu button is SteamVR's own:
                         it opens SteamVR's dashboard, not the game's pause
  X .................... while the game asks for its X, that X (WHEN THE
                         GAME ASKS FOR A BUTTON, below). Where it shows
                         LB + RB (a prop to throw, a door or a grate to
                         tear off), X is both: press it, or hold it for as
                         long as the game wants the two held; its prompt
                         and the marker over the thing show X there. A
                         prop thrown that way goes where your other hand
                         points: at the enemy its aim marker is on, or
                         that way. Otherwise a tap of it does nothing.
                         The game menu is your phone's MENU since October 23
  Hold X ............... the gadget wheel, in front of that hand (the
                         right one on a Steam Frame): move the hand toward
                         a gadget to light it (it turns red, its name in
                         the middle) and let go of X to equip it. Your
                         free hand's trigger then fires that gadget from
                         your hand, as long as it has a charge: the dots
                         beside each gadget are its charges, yellow while
                         left, and one with none is grey. The yellow
                         gadget is the one you have now. The web shooter
                         at the top gives you web balls again. Back in
                         the middle and let go: nothing changes. Early in
                         a new game, before the game gives you a gadget,
                         the wheel has the web shooter alone and says NO
                         GADGETS YET
  Phone ................ squeeze a grip with that hand behind your hip and
                         bring it up in front, still squeezed: your phone
                         opens in that hand. Reach for your back pocket,
                         not the side of your hip, squeeze once you are
                         there (not on the way), and bring the hand up
                         within 1.5 s. You can look at the pocket; a look
                         back past about 70 degrees turns it with you.
                         Still nothing? The session's report says why
                         (`pocket`). The stick picks an app, A
                         opens it, B goes back or puts the phone away.
                         MENU puts it away and opens the game menu (map,
                         suits, skills; B closes it). CAMERA puts it away
                         and takes the game's camera out (THE GAME'S
                         CAMERA, below)
  Click both sticks .... the pad layer, as you let go: your controllers
                         become the whole Xbox controller for a moment
                         (held on, both sticks pause instead). Your grips
                         are the game's LB and RB, your triggers its LT
                         and RT, X is its X and the right stick its D-pad
                         (the gadget wheel). PAD LAYER shows over your
                         right wrist while it is on. Click both again to
                         leave it, draw the phone, or just stop pressing
                         anything for 20 seconds. Your webs are off while
                         it is on, so it does not latch while a web is
                         carrying you
  A, B and Y are the game's own A, B and Y, and X too while the game shows
  it: where the game shows one of them on screen, press that button. Your
  grips shoot webs, so they are not the game's LB and RB in VR: where the
  game shows LB + RB, X is both (its prompt says X), and where it shows a
  bumper or a trigger on its own, your triggers are those (below). The
  controllers have no D-pad: the right stick pushed down is the game's
  D-pad down, pushed up its LT + RT. A push counts from the stick at rest,
  straight down or up: turning, a diagonal and a quick flick do neither.

  WHEN THE GAME ASKS FOR A BUTTON
  Press the button of the name it shows. The game's prompts name an Xbox
  controller's buttons, and while one is on screen your controllers send
  them under those names:
    A, B, X, Y ......... the button of that letter. X is then the game's
                         X, not the gadget wheel
    LB + RB ............ X (the prompt and the marker show X for them)
    Back or View ....... your phone's MENU
    LT or LB ........... your left trigger
    RT or RB ........... your right trigger
    L3, R3 ............. a click of the left or the right stick
    D-pad up, left,
    right .............. the right stick pushed that way (down heals, as
                         always)
  With WEB BUTTON set to TRIGGER the grips stand in for the triggers here.
  "Hold LT and tap RB to fire webs and seal the leaks": hold the left
  trigger and pull the right one. "Press X for Vent Takedown": press X.
  While the game asks for its X, a punch you throw is that X (an uppercut
  for the Air Launch it holds), and with X and A asked for in the air, a
  fist thrown down is both: the Ground Strike. A button you were already
  holding as the prompt came up goes on doing what it did, and the grips
  stay your webs, so you can still swing.
  The Ground Strike also needs no prompt: in a fight, in the air, throw both
  fists down together, hard (no web in either hand, and none let go of in
  the moment before). That is the game's X and A held, and it takes you from
  your webs for its move. One fist alone stays a punch. Not with no enemy
  near (a hard pull down on both webs while swinging is no Ground Strike),
  and not with REALISTIC FLIPS on, where both arms swung down flip you.

  THE GAME'S CAMERA (the research stations' photos)
  Phone, CAMERA ........ takes the game's camera out, as its D-pad up does
  Left trigger, held ... its viewfinder
  Right trigger ........ takes the photo
  Right stick up ....... puts the camera away
  While the camera is out your triggers are its own, not web balls.

  The pad layer above is the way out if a moment still asks for something
  none of this reaches.
  The game as it is .... pause, SWITCH TO GAME SCREEN (under RESUME)
                         plays the game as it is, on a 3D screen in the
                         headset, the controllers an Xbox controller: hold
                         the menu button for the map (Back), hold the left
                         stick's click and push the right stick for the
                         D-pad (down heals). Pause, SWITCH TO VR goes back.

Steam Frame controllers (they have a gamepad's buttons, split between the
hands; everything above is the same, with these buttons)
  A, B, X, Y ........... all four are on the right controller, and each is
                         the button of that name above: A jumps, B dodges,
                         X held opens the gadget wheel, Y interacts
  Menu ................. pause (right controller)
  View ................. the game menu, as the phone's MENU (left
                         controller)
  D-pad ................ the game's own D-pad: down heals. The right stick
                         pushed down and up still does what it does above
  Bumpers .............. nothing: the triggers and grips are your webs
  The session log says "Controllers: Steam Frame, with their own layout"
  when these are in use. Versions up to 0.3.2 had no layout for them:
  SteamVR translated the Quest one, and B, X and Y all dodged.

Valve Index controllers (Spidy has a layout for them; everything above is the
same, with these buttons)
  A, right ............. A: jump
  B, right ............. B: dodge
  A, left .............. X: held, the gadget wheel; the game's LB + RB
  B, left .............. Y: interact
  Squeeze the handle ... the grip: your web
  System button ........ SteamVR's own. Pause by holding both sticks in
  Trackpads ............ nothing
  The session log says "the Index layout" when these are in use.

PS VR2 Sense controllers (under SteamVR, which plays them as Quest
controllers: Spidy has no layout of its own for them, and no player has
reported on them yet)
  Cross, right ......... A: jump
  Circle, right ........ B: dodge
  Square, left ......... X: held, the gadget wheel; the game's LB + RB
  Triangle, left ....... Y: interact
  L1, R1 ............... the grips: your webs
  L2, R2 ............... the triggers
  Pause ................ hold both sticks in: SteamVR's translation carries
                         no menu button over
  The session log says "the Touch layout" when these are in use.

Walls (every wall is Spidy's own; the game's crawl only takes you for a
vent or a door you ask for, and inside vents and ducts)
  Walk onto one ........ walk at a wall from the ground and you go up onto it.
                         What the game lets you walk through - a broken glass
                         pane - is no wall: you walk on through it
  Swing or fly into one  you stay on it, an arm's length off. At speed you run
                         along it with the speed you had, and nothing pulls
                         you down it; without the left stick or a web you
                         slow down and stop
  Left stick ........... walks the wall where you look along it, and steers a
                         run
  A .................... jumps off the wall, out and up
  A web ................ out from the wall swings you off it; one up the
                         wall holds you there for a zip or the reel up it
  The wall's top ....... go up over it and you hop onto the roof; at a walk
                         you step round a corner onto the next wall
  A vent or a door ..... point a hand at it. A turning ring round it, in that
                         hand's colour, and a tick in the hand: you can use it
                         from where you are, up to a couple of metres off. A
                         faint ring: come closer. Press Y and the game's own
                         crawl takes you to it on the wall for a moment and
                         does it - into the vent, the door open. The stick
                         waits until it has
  In a vent or a duct .. the game crawls you through it, as in the plain game;
                         stand up or jump out at its end and the walls are
                         Spidy's again
  Your view ............ walking a wall or stopped on it, your view turns so
                         that the wall is your floor, about your eyes, and
                         turns back when you leave it. A run along a wall
                         never turns it. A vent's shafts turn it the same way
  Your body ............ stands on the wall or a duct's floor: no crawl on the
                         legs or the fingers. Under a low roof it kneels, its
                         legs behind you
  Low places ........... crawling a vent or a duct, your head stays a crouch's
                         height over its floor, under the roof

Slow motion (as in Blade & Sorcery)
  Click the left stick . the world slows to a third of its speed, easing in;
                         click again and it eases back. Your head and hands
                         keep real time: you move fast in a slow world, and
                         your swing slows with it
  Focus meter .......... the cyan ring around an hourglass over the back of
                         your left wrist. It empties while slow motion lasts
                         (12 seconds from full) and ends it when empty; it
                         refills a moment after slow motion ends (12 seconds
                         from empty). With too little left a click flashes
                         it red and does nothing
  A menu, the game screen or the flat screen ends slow motion
  By itself ............ time also slows for a moment, with no press and
                         no focus spent: at a finisher, an uppercut that
                         lifts a thug and a slam; at a called blow; when a
                         thug you yanked from the air arrives in front of
                         you; and at a perfect dodge (a dodge the game
                         itself slows time for). A flurry of blows slows
                         time once, not for every blow

Flips (experimental, off until you switch FLIPS on in Settings > SPIDY VR or
in the launcher's options)
  Right stick in the air turns you head over heels about your head, as
                         fast as you tilt it (back a backflip, to a side
                         a cartwheel). Straight up prioritizes zip/aim,
                         holding the flip angle while the game aims at a
                         zip point. The left stick
                         still moves you. Let the right stick rest once
                         after takeoff before starting a rotation
  Let go of the stick .. rotation stops and your chosen angle stays
  A in the air ......... returns you upright by the short path. A no longer
                         starts a single flip. Release A and let the stick
                         rest before choosing another rotation
  Landing or a wall .... returns you upright too
  Right-stick shortcuts  straight up retains zip/aim during basic flips,
                         including the aimed zip on release. Snap/smooth
                         turn and healing remain suspended in the air.
                         Other ground controls resume after stick release
  FLIP SPEED ........... how fast all of it turns you: 180 degrees a second
                         at full tilt unless you change it (90 to 480;
                         Settings > SPIDY VR or "Flip speed" in the launcher)

Controlled flips (separate option, off until CONTROLLED FLIPS is enabled
in the launcher or Settings > SPIDY VR)
  A in the air ......... enables left-stick flip mode; A must be a new press,
                         not held from a jump or menu
  Left stick in mode .. turns you about the tracked head. Let it rest once
                         if already tilted when entering the mode
  Release the stick ... stops rotation and holds the exact angle
  A again .............. returns upright by the short path and exits mode;
                         normal left-stick travel resumes
  Right stick .......... retains turning, zip/aim/release and healing
  Landing or a wall .... exits mode and restores the normal view
  Menus or recenter .... clear the mode; another airborne A press is needed
  Other flip settings .. works independently of basic FLIPS and overrides
                         that right-stick mode when both are enabled.
                         REALISTIC FLIPS keeps priority and its own controls
  FLIP SPEED ........... adjusts this mode too

Realistic flips (experimental, off until you switch REALISTIC FLIPS on in
Settings > SPIDY VR or "Realistic flips" in the launcher's options, where
its ten settings show under it; SPIDY VR has six of them, and MINIMUM
FLIGHT SPEED, ROTATION RESPONSE, ANGULAR DRAG and MINIMUM HAND SPEED are
the launcher's alone; while it is on it takes the place of the flips above)
  On a web ............. moving faster than MINIMUM FLIGHT SPEED (3 m/s), your
                         body turns toward the web's pull, as hard as
                         ROTATION STRENGTH says; where you face stays yours
  Let go of the web .... the swing's spin goes on (SWING MOMENTUM) and dies
                         away as ANGULAR DRAG says
  Swing your arms ...... in the air, faster than MINIMUM HAND SPEED, and you
                         turn with them, as much as HAND MOMENTUM says; swing
                         them back the other way to slow down. A spin can go
                         past a whole turn
  A in the air ......... stops the spin and brings you upright (AIR ROTATION
                         BRAKE); landing does too
  Left stick in the air  with FLIPS on as well: let go of it once in the air,
                         then it steers the turn
  In a fight ........... switch FLIPS IN COMBAT off, and with enemies near
                         (and for 3 s after) nothing turns you and you come
                         upright; the stick steers your flight and both
                         fists thrown down are a Ground Strike again
  MAX ROTATION SPEED and ROTATION RESPONSE set how fast you can turn and how
  soon a turn gets there.

Hand wall climbing (experimental, off until you switch HAND WALL CLIMBING on
in Settings > SPIDY VR or "Hand wall climbing" in the launcher's options)
  On a wall ............ with the left stick at rest you stay upright beside
                         the wall, a little crouched, your eyes 25 cm from
                         it. Your body crawls on it, its feet planted,
                         stepping in turn as your hands pull
  Squeeze a grip ....... with that hand within about 18 cm of the wall and
                         pointed at it: the palm holds on, open, with a
                         short buzz. Pull it down to climb, sideways to go
                         along, up to go down; two hands share the pull. Let
                         go and squeeze again for a new hold. The grips do
                         this whichever WEB BUTTON you chose. Pointed away
                         from the wall, along it or up it, the web button
                         shoots a web as anywhere else, and a web out from
                         the wall swings you off it
  Both hands holding ... roll both controllers the same way to turn your
                         view along the wall, up to 90 degrees a second
  Left stick ........... runs along the wall, steered by turning your head
                         left or right; let go to climb again
  A .................... jumps off the wall
  Round a corner ....... your view turns onto the building's next side
  Press a palm ......... into the wall and your hand stops at it; press
                         harder and your view and body push off it, up to
                         45 cm
  Turn away ............ take one hand off the wall, turn your head the other
                         way and touch the wall with that hand again: your
                         body turns its back to the wall, its feet still on
                         it, your eyes 40 cm out. The same turns you back.
                         Looking around alone turns nothing, and the two
                         hands' roll waits until you face the wall again
  In a vent ............ the left stick crawls you through it, as with hand
                         climbing off (hand crawling in vents is off for now)

Menus and cutscenes show on a screen in the headset. There the controllers act
as an Xbox controller: left stick moves, A selects, B goes back, grips switch
tabs, the menu button is Start.

Cutscenes have depth on that screen (3D, as in a 3D cinema): the game draws
them once for each of your eyes. So do finishers and the other scenes the game
plays by itself. The far background lies far behind the screen, and what is
close to the game's camera comes out of it toward you. Subtitles and prompts
stay on the screen itself. Menus, hint cards, the map and loading are flat, as
before. GAME SCREEN 3D in Settings > SPIDY VR (or "Game screen 3D" in the
launcher's options) sets how much: LOW and MEDIUM keep the scene further
behind the screen, HIGH shows everything as far from you as it is from the
game's camera, OFF is the flat picture again.

The game's HUD (health, gadgets, minimap, prompts, subtitles and the game's
markers) floats 2 m ahead of you, about 60 degrees wide. It stays still while
your head moves a little, and glides back in front of you when you look
further away. A marker sits over its target while the target is inside that
frame; otherwise the game's edge arrow points the way. HUD in Settings > SPIDY
VR (or "HUD" in the launcher's options) makes it SMALL, MEDIUM or LARGE, or
switches it OFF.

VR settings
  They are a tab of the game's own Settings: pause with the menu button,
  choose Settings, then SPIDY VR, the last tab (Up from GAME gets there in
  one press). It works like the game's other tabs: the left stick moves and
  changes the selected setting, X resets it, Y resets the whole tab, B goes
  back. Its sections:
  WEBS ........... WEB STYLE (REALISTIC or ARCADE, below); the WEB BUTTON
                   (GRIP or TRIGGER: the one that shoots and holds a web;
                   the other reels in and shoots web balls); AIM MARKERS;
                   webs holding in open air (a web that meets nothing
                   within 100 m holds there; off, it misses).
  SWINGING ....... the swing speed limit and your weight.
  COMFORT ........ snap turn and smooth turn.
  HAPTICS ........ controller vibration; BLOW SOUNDS (GAME: your blows
                   sound as the game's own do, its hits on the one you
                   struck; SPIDY: Spidy's own tones, as before October 22;
                   OFF: none, nor a web strike's rush); BODY HAPTICS (a bHaptics vest, sleeves and
                   visor, below) and its four switches: WEB, WIND, COMBAT
                   and MOVEMENT HAPTICS.
  DISPLAY ........ the game screen's size, its 3D depth and the HUD's size
                   (OFF hides it in VR). UI STYLE: how Spidy's own marks,
                   gadget wheel and meters look: COMIC, printed as Spidy's
                   videos are, or TECH, thin lines and translucent blue
                   panels like the game's own HUD. NEAR CLIP: how near
                   your eyes things still show, 2 cm unless you change it
                   (1 to 10 cm; the game's own camera keeps 10 cm, which
                   hid hands held up before your face). RECORDING FOV (OFF
                   unless you pick 130, 140 or 150 degrees): the game window
                   on your monitor shows a wider view to record, at least
                   that wide across, instead of your left eye. It costs
                   frame rate (about 3 ms a frame on its author's PC), and
                   what Spidy draws itself (its gloves and webs, the aim
                   markers, the combat marks) is only in the headset then.
  BODY ........... CALIBRATE BODY: set it to ON RESUME and resume, and the
                   T-pose panel asks for your measurements again. HEIGHT
                   (AUTO unless you pick one, 150 to 190 cm): your height.
                   Set, it gives Spider-Man your size instead of the
                   calibration's or the headset's measure (the calibration
                   still fits his arms), and it is how high SEATED raises
                   you. SEATED (off until you switch it on): play sitting
                   down. Spidy measures how high your eyes are over your
                   first second of play, and again each time you recenter
                   the headset, and raises your view, your body and your
                   hands by the difference to your standing eye height:
                   HEIGHT's, else a calibration you made standing, else a
                   1.76 m player's. Sit as you will play before you
                   recenter. Stand up with SEATED on and you tower: switch
                   it off first.
  COMBAT VISUALS ... in DISPLAY, on by default. Off hides Spidy's punch
                   arrows, target circle, impact/combo effects, KO text and
                   automatic combat slow-motion screen effects. Spider-Sense
                   attack warnings and offscreen direction markers remain
                   visible. Off also removes directional combo requirements:
                   uppercuts launch freely, downward blows knock standing enemies
                   down or slam airborne enemies into the ground, even on a
                   finisher count. Punch speed, contact and free-hand rules
                   still apply. Sound and vibration still work. Web aim markers and the
                   game's own HUD have their existing separate controls.
  JOYSTICK WALL RUN CAMERA ... off by default: the left stick supplies wall
                   velocity while the game sees a neutral movement stick,
                   so the existing momentum wall-running gait and camera
                   placement take over. The normal momentum wall clearance
                   is retained; no new body lean or close-view pose is added.
                   On restores the old wall-floor camera transition.
                   WALL RUN CAMERA TILT controls the momentum camera bank.
  HALF PUNCH DAMAGE ... off by default. On halves VR punch and web-strike
                   health damage, including uppercuts, smashes, counters and
                   finishers. Launch and smash velocity, knockback, combo
                   rules and punch recognition do not change. Off restores
                   normal damage. This is not a global game damage setting.
  EXPERIMENTAL ... FLIPS (off until you switch it on) uses the right stick
                   in the air; release holds the angle and A returns upright.
                   CONTROLLED FLIPS is a separate A-toggle mode, above.
                   FLIP SPEED sets how fast either steering stick turns you.
                   HAND WALL CLIMBING (off until you switch it on, above)
                   uses a fixed wall-camera clearance. EYE AIM (OFF unless
                   you pick WEB BALLS, ZIPS or BOTH; below). REALISTIC FLIPS
                   (off until you switch it on, above) and the ten rows that
                   tune it.
  EYE AIM ........ for a headset whose VR runtime gives games your eyes
                   (OpenXR's eye gaze: Quest Pro, Varjo, Pimax Crystal and
                   the like; a Quest 3 has no eye tracker). The session log
                   says whether yours does. WEB BALLS: a free hand's trigger still
                   fires from that hand, but the ball goes to what you look
                   at: the enemy nearest your gaze, else the surface it
                   meets, else open air along it. ZIPS: the stick pushed up
                   zips to the point you look at - Spidy turns the game's
                   own camera onto it, which is how the game picks its zip
                   point, and holds the zip back a moment (a third of a
                   second at most) until it is there. Held into the game's
                   aim mode, your eyes aim it until you let go. Without an
                   eye tracker, or with EYE AIM off, your hands aim web
                   balls and your head zips, as before; the session log
                   says which ("Eye aim: ..."). Quest Pro over Quest Link
                   or Virtual Desktop sends its eyes to PC games only with
                   that switched on (Quest Link: Settings > Beta; Virtual
                   Desktop: its eye tracking forwarding).
  Changes apply at once. The launcher starts your next session with them;
  its options show every one of them but VR VIEW too.
  NEAR CLIP, RECORDING FOV, UI STYLE, BLOW SOUNDS, HEIGHT, SEATED, EYE AIM, the four body haptics switches and the
  CONTROLLED FLIPS and EXPERIMENTAL rows below FLIP SPEED Spidy keeps in
  %APPDATA%\Spidy\spidy_vr.ini, beside the launcher's own settings; the
  launcher shows realistic flips' ten rows while it is on, and the body
  haptics switches while it plays. Playing
  the game on its screen instead of VR is on the pause menu itself: SWITCH
  TO GAME SCREEN, under RESUME (each session starts in VR).
  WEB STYLE: REALISTIC webs are ropes. They bend round whatever comes
  between you and where they stick, lamp posts and signs too, and a swing
  can take you down to just over the street. ARCADE webs go through lamp
  posts, signs, railings, cars and other thin or loose things, as they are
  shot and as they swing, and stick to the building behind; a swing keeps
  you about 5 m over the street (let go to come down). Buildings still hold
  them and bend them round their corners.
  Catching props and thugs, the web shooter, your own body and punching are
  always on.
  The title screen's Options have no SPIDY VR tab: load your save first, or
  set them in the launcher.

Settings profiles (share your settings, or try someone else's)
  "Profile", at the top of the launcher's Options tab, keeps sets of settings
  and switches between them. A profile holds every setting of the SPIDY VR
  tab but RECORDING FOV, the ones in spidy_vr.ini too (NEAR CLIP, UI STYLE,
  BLOW SOUNDS, the body haptics switches, the EXPERIMENTAL rows), and the
  four realistic flips settings only the launcher shows. Not
  your graphics options (render resolution, upscaling, the game window, the
  monitor and its RECORDING FOV, the game's depth of field and sharpening),
  which suit your own PC, and not your body calibration: a profile leaves
  those as they are.
  Save as a profile ... keeps your settings under a name. Pick a profile in
                        the list to use it; after you change a setting,
                        Save changes puts it in that profile.
  Copy to share ....... copies your settings as text: paste it into a
                        Discord message.
  Export a file ....... saves them as a .spidy file to send.
  Paste a profile ..... takes one someone copied (from a Discord message,
                        say); Import a file takes a .spidy file, and so does
                        dropping one on the launcher's window. You see what
                        it would change first; then Save and use, or Save
                        only.
  Before a profile changes settings that no profile holds, the launcher
  keeps them as "Previous settings": pick that to go back. Profiles are
  files in %APPDATA%\Spidy\profiles; Delete a profile moves one to the
  Recycle Bin. A profile from an older Spidy, or one with only a few
  settings in it, changes those and leaves the rest. Profiles apply to the
  next VR session: change them while VR is stopped.

bHaptics vest, sleeves and visor
  With a bHaptics TactSuit, TactSleeves or a TactVisor you feel the game on
  your body: a web taking hold and its pull in your arm and shoulder,
  growing through the bottom of a swing; the wind when you fly or fall free
  of webs and walls; landings and walls; the blows you land, and the ones
  you take on a spot on the side they came from; and Spider-Sense tingling
  in the visor and at your shoulders toward the attack. The vest faces
  where your body does: a look over your shoulder does not turn it. Install
  the bHaptics Player (bhaptics.com/software/player), pair your devices in
  it, and start it before VR: Spidy finds it by itself, and the headset says
  which devices are on. BODY HAPTICS under HAPTICS (or "Body haptics" in the
  launcher's options) sets how strongly they play; OFF stops them. Below it
  (in the launcher too, while it plays) WEB HAPTICS (shots, holds, the swing's pull, zips, grabs), WIND HAPTICS,
  COMBAT HAPTICS (punches, hits, Spider-Sense, dodges, heals, slow motion)
  and MOVEMENT HAPTICS (landings, walls, wall climbing, leaps) each switch
  their group off. Without the Player nothing changes. To feel each pattern
  without the game, run
  build\windows-ninja\spidy_haptics_test.exe: it checks the Player and the
  devices, buzzes each part of each device in turn, then plays every
  pattern and says what it should feel like.


IF SOMETHING IS WRONG
---------------------
- "This game version is not supported": the launcher's game line names your
  version and the fix. Spidy supports version 4.630.0.0, Steam's since July
  8, 2026. An older version: Steam may be set to one of the game's rollback
  betas (right-click the game in Steam > Properties > Betas > None) or have
  an update waiting. Version 4.630.0.0 that still fails: a mod, a patch or a
  damaged download changed Spider-Man.exe; Properties > Installed Files >
  Verify integrity of game files puts Steam's file back. A newer version:
  Spidy reads the game's code at fixed places, so a game update needs a
  Spidy update.
- The headset is not found: connect it first (SteamVR headsets: start
  SteamVR), then press Check in the launcher. On Automatic it shows which VR
  runtime has your headset; if it picks the wrong one, choose yours in the
  list.
- Steam Link or another SteamVR headset, and SteamVR is not in the
  launcher's VR runtime list (version 0.2.9 and older, when Virtual Desktop
  or the Meta Quest Link app is installed too): in SteamVR open Settings >
  OpenXR and press "Set SteamVR as OpenXR runtime", then press Re-check in
  the launcher. Newer versions find SteamVR by themselves.
- "VR already ran in this game" (older versions: "Game XR start: 1000"):
  VR starts once per game launch. Close the game (Spider-Man.exe gone from
  Task Manager), then press START VR.
- "VR needs administrator rights": Steam or the game runs as administrator
  on your PC, and Windows keeps a program without those rights out of such
  a game. Choose "Restart as administrator" when the launcher asks (Windows
  asks you to allow it), close the game if it is running, then press START
  VR. Or have Steam start without administrator rights.
- "Windows refuses Spidy access to the game ... a security program": your
  antivirus is guarding the game. Allow the Spidy folder in it, close the
  game, then press START VR.
- "render memory module unavailable ([WinError 5] Access is denied)", then
  "No loaded player within three minutes" (version 0.2.9 and older): Windows
  kept Spidy out of the game, for one of the two reasons above. Close the
  game, right-click Spidy Launcher, choose "Run as administrator" and press
  START VR; if nothing changes, allow the Spidy folder in your antivirus.
  Newer versions say which it is.
- VR does not start and the game stays flat: send the whole "reports"
  folder. The newest game-vr-...-console.log holds everything the launcher
  showed, and the report beside it says where it stopped.
- "Spider-Man is still running with an older Spidy build in it" (or "with
  Spidy from another folder in it"): the game was open while Spidy was
  updated, or another Spidy folder started VR in it. A running game takes
  Spidy only once. The launcher offers to close the game for you (as its
  close button does: it saves as it quits) and start VR again. Up to 0.3.8
  the session log said "Different bridge build is resident" instead.
- No pause under SteamVR with a Quest: there the left menu button is
  SteamVR's own and opens its dashboard. Hold both thumbsticks in for a
  moment instead: the game pauses.
- "A problem has occurred with your display driver" as VR starts, every
  time: the session log names the code (0x887A002B is one). Try these one
  at a time, pressing START VR after each: in the launcher's options,
  Troubleshooting, "Normal camera on the monitor" on; the game's Display
  settings with Monitor set to your main display; the newest graphics
  driver. Then post the report in #spidy-bugs on Discord.
- The game's menus ignore the VR controllers: keep the game window in front
  on the desktop, and start the game from the launcher with the game closed
  (the launcher turns off what lets SteamVR's virtual gamepads take over).
- Buttons do nothing in VR (no menu, no doors, no web strike) while webs and
  walking work, and the game's prompts show keyboard keys: Steam Input is
  on for the game. In Steam, right-click the game > Properties > Controller
  > Disable Steam Input, close the game, then press START VR.
- Steam Frame controllers: B, X and Y all dodge, or the D-pad interacts.
  Versions up to 0.3.2 gave SteamVR no layout for them. Newer ones do, and
  the session log says "Controllers: Steam Frame, with their own layout".
  If it says "the Touch layout" with Frame controllers in your hands, a
  binding chosen in SteamVR earlier is in the way: in SteamVR open
  Settings > Controllers > Manage Controller Bindings, pick the game and
  choose the default binding. If that changes nothing, update SteamVR.
- The game closes at its first logo, every time, and you have OpenXR
  Toolkit installed: version 0.3.0 and older do not run with it. Newer
  versions switch it off for Spider-Man by themselves (the launcher's log
  says so) and leave it on for your other games. On an older version, turn
  it off in its companion app, or choose SteamVR under "VR runtime".
- The picture breaks up: start the game from the launcher with the game
  closed. Attached to a game that is already running, Spidy cannot give it
  the larger render memory VR needs.
- The game freezes for good in VR (the headset keeps showing the last
  picture and nothing answers): since October 23 Spidy notices after eight
  seconds and saves where each of the game's threads stood, in a
  game-vr-...-freeze.dmp file beside the session report. Close the game from
  Task Manager, then send the report with that file: it shows what the game
  was waiting for. A freeze that comes back at the same moment of a mission
  is worth playing past on the game screen (pause, SWITCH TO GAME SCREEN),
  then SWITCH TO VR again.
- The picture freezes or jumps for a second or two at a time, however fast
  the game runs: with a wireless headset (Virtual Desktop, Steam Link), connect
  the PC to the router by network cable and turn the PC's Wi-Fi off, then
  connect the headset again. A PC on Wi-Fi sends every picture through the
  air twice. Spidy warns in its window when it starts with the PC on Wi-Fi,
  and says after a session if the stream stalled.
- Low frame rate: set "Upscaling" in the launcher's options to Balanced or
  Performance first (the picture keeps its size and gets a little softer;
  on an RTX 4080 laptop, Quality gave 61 frames a second and Performance 77
  where Off gave 39). Then lower "Render resolution" (below 100% renders
  fewer pixels than your headset asks for), or your streaming quality in
  Virtual Desktop. On a laptop, plug it in and choose its highest
  performance mode. A lower "Monitor frame rate" helps a little more, most
  if the game window is large. Lower the game's own graphics settings as
  well: players report good frame rates on a GTX 1080 that way.
- Blurry edges: set "Upscaling" in the launcher's options to High or Off,
  then raise "Render resolution" above 100%; try 125% first. Both cost
  frame rate and memory: 150% gave about 30% fewer frames on an RTX 5090.
  Press Check beside your headset to see the pixels per eye. Virtual
  Desktop's quality setting raises what 100% is.
- Far buildings stay dark and get their lit windows (and the rooms behind
  them) only once you are close, or right after a menu: until October 19
  the game chose which far buildings to draw in full only for its own
  camera's view, which Spidy does not render while you play in VR, so it
  chose none. Your left eye chooses them now. The same day the eyes also got
  the flat game's distances for detail, people and textures, which the
  headset's wide lens had halved. If it still happens, send a session report
  and say where you were.
- The aim markers distract you: switch AIM MARKERS off in Settings > SPIDY VR
  (or "Aim markers" in the launcher's options).
- Webs shot at the sky hold somewhere you can't see: switch "Webs hold in
  open air" off in Settings > SPIDY VR or the launcher's options; a web then
  needs something within 100 m to hold on to.
- Web balls fly when you only meant to reel in: the trigger (the grip with
  WEB BUTTON: TRIGGER) reels only while that hand's web is attached;
  otherwise it shoots.
- You would rather shoot webs with the trigger, as in other VR games: set
  WEB BUTTON to TRIGGER in Settings > SPIDY VR (or "Web button" in the
  launcher's options). The grip then reels in and shoots web balls.
- Snap turning makes you uneasy, or the vibration is too strong: Settings >
  SPIDY VR has both.
- Your bHaptics devices stay still: start the bHaptics Player before VR and
  check that it shows them connected; the headset says "Body haptics on:"
  and names them once Spidy has them. BODY HAPTICS (Settings > SPIDY VR >
  HAPTICS) must not be OFF, nor the switch of what you expect to feel. Too
  much: turn BODY HAPTICS down, or switch a group off (the wind is WIND
  HAPTICS). A part that plays on the wrong side of you: tell us which
  (spidy_haptics_test.exe map shows each part in turn).
- Swinging feels floaty, or falls too hard: WEIGHT in Settings > SPIDY VR
  (or "Weight" in the launcher's options) sets how heavy you are while
  swinging and after letting go, until you land: 100% is real gravity and
  the default (80% until October 12; a launcher that still had 80% starts on
  100%), up to 300%. Jumps and falls without a web are the game's own.
- A web lets go at a corner, or does something odd as it bends round one:
  it bends round six edges at most, and lets go where it finds no edge to
  bend round. Tell us where it happened.
- Webs get caught on lamp posts and signs, or swings drag you down to the
  street: set WEB STYLE to ARCADE in Settings > SPIDY VR (or "Web style" in
  the launcher's options).
- Lowering your arm on a web moves you: with PHYSICAL WEBS the web's length
  is measured to your hand, so a taut web follows it. That is also how you
  pull yourself along it.
- A jump comes a moment late: on the ground Spidy waits to see whether you
  hold A for a charged leap, and jumps as you let go of a tap.
- The game closed when you chose Quit in the pause menu (every time, up to
  the fourth build of October 12): on its way to the main menu the game asks
  Steam for a ticket with one argument missing, and Steam read what happened
  to lie there. Spidy now guards that call. If it still happens, send the
  session's report: its "steam_guard" says what the game passed.
- A vent, a grate or a hatch at your feet or over your head shows no prompt
  and takes no button: the game offers a thing only when its own camera
  looks at it, and Spidy turned that camera left and right with your head
  but not up and down. It follows your head both ways now: look at the
  thing, then press what the game shows (Y; X for LB + RB; the right stick
  up for LT + RT).
- The picture is softer, or a cutscene has lost its blurred background:
  Spidy turns the game's sharpening and depth of field off for VR. "Game's
  sharpening" and "Game's depth of field" in the launcher's options keep
  yours.
- Clicking both sticks no longer shows the flat screen: it is SWITCH TO
  GAME SCREEN in the pause menu now.
- You would rather turn smoothly than in steps: SMOOTH TURN in Settings >
  SPIDY VR (or "Smooth turn" in the launcher's options). If it makes you
  uneasy, try a slower speed, or OFF for snap turning again.
- The menu screen is too small or too large: "Game screen size" in Settings
  > SPIDY VR.
- A cutscene's depth strains your eyes, or things come too far out of the
  screen: GAME SCREEN 3D in Settings > SPIDY VR, LOW or OFF. Too little
  depth: HIGH.
- The HUD is too small, too large or in the way: HUD in Settings > SPIDY VR
  (or "HUD" in the launcher's options): SMALL, MEDIUM, LARGE, or OFF.
- You would like Spidy's marks, gadget wheel and meters to look like the
  game's own HUD rather than a comic: UI STYLE in Settings > SPIDY VR, TECH.
- The game asks for a button and nothing you press does it: press the
  button of the name it shows (WHEN THE GAME ASKS FOR A BUTTON, above). X is
  the game's X while it asks for X, your triggers its LT, LB, RT and RB,
  your stick clicks its L3 and R3, the right stick its D-pad. Up to 0.4.0
  Spidy read none of the game's prompts (the reading closed the game as a
  save loaded, and was off from October 13): "press X" opened the map, and
  the Oscorp stations' leaks, vaccine and camera could only be done on the
  game screen. Since October 22 a station's leaks and vents take your
  trigger's web (the game's Web Shooter, while it has charges), the phone's
  CAMERA takes the game's camera out, and both fists slammed down in the
  air are the Ground Strike. If a moment still asks for something none of
  this reaches, click both thumbsticks for the pad layer, where your grips
  are the game's LB and RB, your triggers its LT and RT, X its X and the
  right stick its D-pad; click both again to leave it.
- A prompt shows keyboard keys ("click F to decipher") and no button works:
  the game shows the icons of the device it last saw. Press something on
  the controllers so it sees the pad again, then Y. If it keeps happening,
  send the session's report: its "prompts" section counts those texts.
- You fly at a thug you did not mean to strike: a web strike opens with a
  fist thrown out fast (2.6 m/s or more) toward an enemy within 10 m, about
  35 degrees to either side. Reaching for a web slowly, or pulling a hand
  back, opens none.
- A punch at a thug far off does nothing: he must be an enemy on his feet or
  in the air (not one already knocked out), within 10 m and in sight, and the
  fist thrown at him, out from your body. A hook, or a hand whose web holds a
  prop or swings you, opens no strike.
- The blows are too loud or the bursts too bold: tell us; this build has
  no switch for them yet.
- B no longer interacts, or Y no longer opens the game menu: the buttons
  moved to make room for the dodge. B dodges and Y interacts (as the game's
  prompts say), and the game menu is your phone's MENU.
- X no longer opens the game menu (since October 23): draw your phone (a
  grip squeezed behind your hip, brought up in front) and press A on MENU.
  Held, X still opens the gadget wheel.
- The right stick's click no longer pulls a door or a grate (since October
  24): X does where the game shows LB + RB, and its prompt shows X there,
  as does the marker over a thing you can throw. That marker no longer
  fills as you hold X.
- Holding X opened nothing early in a new game (builds before October 24):
  the game had given you no gadget yet. The wheel opens now, with the web
  shooter alone.
- The suit power did nothing (builds before October 23): the gesture
  buzzed both hands but never reached the game. Both hands before your
  chest, both triggers pulled together, now uses it when its meter is full.
- Y does nothing at a door, a console or a backpack: look at the thing as
  you press it, from within about a metre (closer than feels natural at a
  wall in VR). The game takes its Y only for what its own camera sees and
  its hero faces; Spidy turns both to where you look. From further away
  hold Y: you step up to the thing and it opens. B there dodges, away from
  it.
- A punch flew you to an empty spot, or hit nothing you could see: thugs of
  a fight's later waves wait unseen where they will appear, and until
  October 12 fists, webs and punches across the room took them for enemies.
  A thug who is knocked out is nobody's target either.
- The game says "Tap RB repeatedly to web up the turret" (Fisk's office):
  shoot web balls at the turret, three in a row. Then, for "Hold LB + RB to
  yank and throw turrets": from within about 15 m, look at the webbed turret
  and hold X until it is thrown. Being shot during the
  first second and a half stops the yank, as in the plain game: web both
  turrets first, or wait until they reload. The webbing lasts four seconds.
- The view turning onto a wall makes you uneasy: it has no switch of its own
  any more, because it is what makes a wall feel like a floor and it turns on
  every wall, whoever holds you. Ask and the switch comes back; until then a
  session started by hand takes `--no-stand-on-walls` and stays upright.
- A vent or a door on a wall does nothing: point a hand at it and press Y.
  No ring round it means the game does not offer it from where you stand -
  come closer; a faint ring means you nearly are. A ring and still nothing:
  send the session's report, whose "offers" counts every press and how it
  ended. (On a wall Spidy holds you, which the game counts as the air and
  offers nothing in; the press hands you to its crawl for that one thing.)
- A flip you did not mean, or flips make you uneasy: switch FLIPS off in
  Settings > SPIDY VR (or "Flips (experimental)" in the launcher's options).
  It is off unless you switched it on. Flips too fast or too slow: FLIP
  SPEED, under it (or "Flip speed" in the launcher).


SHARING
-------
Please share the original zip with this credit intact, so players get working
files and know where updates come from.

Spidy is Copyright (C) 2026 Ilya Mezerowsky, and free software under the GNU
General Public License, version 3 (LICENSE.txt). Its source code is at
https://github.com/IlyaMez/SpidyVR

Spidy is a free fan project, not affiliated with or endorsed by Insomniac
Games, Sony Interactive Entertainment, Marvel or Valve. Third-party licences
are in THIRD_PARTY_NOTICES.md and docs\licenses.
