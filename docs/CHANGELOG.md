# Spidy changelog

Every build, newest first, with what changed, why, and what was measured.
Per-check results are in [VALIDATION.md](VALIDATION.md).

**Latest build (October 8, fourth build): fingers no longer twist in a
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

R.E.A.L. VR's local installation was removed from the game folder at the user's
request. Its 21 files are backed up under `backups/realvr-removed-20261004`, with
a SHA-256 manifest. The game executable is unchanged.

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
