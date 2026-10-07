# Spidy

By **Ilya Mezerowsky**. Spidy is free; if you enjoy it,
[support it on Ko-fi](https://ko-fi.com/ilyamezerowsky).
Players: download the release zip and start `Spidy Launcher.exe`; see
[Share Spidy](#share-spidy-the-launcher-and-the-release-zip).

An in-development VR mod project for **Marvel's Spider-Man Remastered**, targeting
**Quest 3 + Virtual Desktop**. First milestone: 6DoF head/controller tracking,
native stereo, and physically controlled web swinging.

**Latest build (October 7, second build): webs in open air are a setting.**
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
Details: [docs/VALIDATION.md](docs/VALIDATION.md).

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
the headset is pending. Details: [docs/VALIDATION.md](docs/VALIDATION.md).

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
Details: [docs/VALIDATION.md](docs/VALIDATION.md).

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
[docs/BODY.md](docs/BODY.md).

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
Details: [docs/WEB-GRAB.md](docs/WEB-GRAB.md) and
[docs/VALIDATION.md](docs/VALIDATION.md).

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
[docs/VALIDATION.md](docs/VALIDATION.md).

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
[docs/WEB-GRAB.md](docs/WEB-GRAB.md). 114 core checks, the GPU test and 56
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
  [Memory for a VR session](#memory-for-a-vr-session).

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
[Graphics settings in VR](#graphics-settings-in-vr).

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

## Share Spidy: the launcher and the release zip

`Spidy Launcher.exe` (source in `apps/launcher`, built as
`build\windows-ninja\spidy_launcher.exe`) lets other people play without the
development setup. It is credited to Ilya Mezerowsky in its header, its About
tab, its file properties and the zip's README, and carries a Ko-fi button. On
start it checks the PC and shows what it finds:

- **The game:** in every Steam library (Steam's registry entries, then
  `libraryfolders.vdf` and the app manifest), else where you point it with
  *Change...*. It compares `Spider-Man.exe` with the supported build's SHA-256
  (read from `tools/inspect_game.py`) and explains when the copy is another
  build or the Epic Games Store version.
- **The VR runtime:** every registered OpenXR runtime. Virtual Desktop is
  chosen when installed (the tested one), else Windows' active runtime. The
  choice reaches both the headset check and the game's XR worker, which used
  to open Virtual Desktop's runtime only (`XrConfig` version 7 carries the
  manifest path).
- **The headset:** *Check* runs `spidy_headset_probe.exe` against that runtime.
- **The Visual C++ runtime:** 14.40 or newer, which the modules need. *Install*
  downloads Microsoft's installer, runs it only if Microsoft signed it, and
  checks again. The launcher itself is linked statically and needs nothing.
- **Memory** Windows can still promise programs, against `VR_COMMIT_MB` in
  `tools/run_game_vr.py`; below that, START VR asks before it starts.
- **Spidy's own files:** the modules, Python, and a writable folder.

START VR runs `tools/run_game_vr.py` with the chosen options and shows its
output live; STOP VR (or closing the window, after a question) stops the
session through a named event that `run_game_vr.py --stop-event` treats as
Ctrl+C, so the hooks come out and the report is written as before. Options
and the game's hash are remembered in `%APPDATA%\Spidy\launcher.ini`. A session
that ends with other VR settings than it began with (the headset's settings
panel, or X for the aim markers) prints them on its last line, "VR settings
from the headset: ...", and the launcher saves them as its options. The
first start offers desktop and Start menu shortcuts.

Make the zip with:

```powershell
.\tools\bootstrap.ps1 -Observer   # also fetches Dear ImGui for the launcher
.\tools\build.ps1 -Observer
.\tools\package.ps1               # dist\Spidy-<version>-win64.zip
```

It holds the launcher, the seven game modules, the Python tools a session
imports, the Windows embeddable Python 3.12.10 (hash-pinned), the notices and
[docs/PLAYERS.md](docs/PLAYERS.md) as `README.txt`. Players extract it anywhere
they can write and start `Spidy Launcher.exe`. The version comes from
`project(Spidy VERSION ...)` in `CMakeLists.txt`. The supported game build is
one Steam build: a game update needs new addresses in Spidy before the
launcher accepts it. Other runtimes than Virtual Desktop and other controllers
than Quest Touch (Index bindings are also suggested) are untested.

### Publish a release

GitHub Actions builds releases on a clean machine and puts them under the
repository's **Releases**, zip attached. Publish from GitHub: **Actions >
Release > Run workflow** on `main`, choose the part of the version to raise
(patch 0.1.1 -> 0.1.2, minor 0.2.0, major 1.0.0), and run it. From a terminal:

```powershell
gh workflow run release.yml -f bump=patch                    # or minor, major
gh workflow run release.yml -f bump=patch -f publish=false   # a test run that changes nothing
```

[.github/workflows/release.yml](.github/workflows/release.yml) runs on GitHub's
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
  [automation and its verified controls](docs/AUTOMATION.md).
- Optional `spidy_observer.dll` and `tools/observe_game.py`: camera-update timing
  and transform diagnostics in the supported game process, with automatic hook
  disable/restore after capture. This is a diagnostic, not the VR game adapter.
- Experimental `spidy_bridge.dll` and `spidy_view.dll`: bounded native input,
  final camera pose, and lens tests. Commands expire without renewal.
- `Launch Spidy VR.cmd`: checks the headset, starts Steam Spider-Man with its
  launcher skipped, and attaches VR when your save has loaded. It stays running
  until game exit or Ctrl+C in its console. Eye resolution follows the active
  runtime recommendation. Click both thumbsticks to switch between VR and a
  flat game screen inside the headset. These new launch/toggle features are
  built and locally tested; headset validation is pending.
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
- `tools/probe_vr_load.py`: renders the VR views at the headset's resolution
  where the player stands and reports, per phase, the game's frame rate, its
  render commands per frame, GPU use, and the CPU time of each game thread;
  `--profile-threads` samples where chosen threads spend their time, and
  `--gpu capture` with the `shots` phase saves eye images with the eyes'
  occlusion culling on and off. No headset is required.
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
[docs/WEB-GRAB.md](docs/WEB-GRAB.md).

Each hand without a web shows an aim marker where its grip would send the
web now: a white ring where the web would hold, a faint dashed ring where it
would hold in open air (no marker there with webs in open air switched off),
a red cross where it would miss, amber corners around
a prop or thug it would catch. It tightens as you squeeze. **X** hides or
shows the markers; `Launch Spidy VR.cmd -NoAimMarkers` starts with them hidden.

**VR settings** hang beside the game screen: they open with the pause menu
(the menu button), and on every other game screen a VR SETTINGS tab at the
screen's right edge opens them. Point a controller and pull the trigger to
switch the aim markers, web grabbing, webs in open air, your body and punching,
or to step the swing speed limit, snap turn, controller vibration and the game
screen's size.
Changes apply at once; Spidy Launcher starts your next session with them.
A hand pointing at the panel keeps its trigger from the game.

You are Spider-Man's body: look down to see it, your arms and hands follow the
controllers, and the body turns with you once you look far enough to the side.
A squeezed grip, or a hand moving fast, closes into a fist. Punch a thug with a
fist moving 2.2 m/s or more and he takes the game's melee damage and reacts as
hard as you hit him; the controller kicks. `-NoBody` hides the hero and draws
gloves instead; `-NoPunch` lets fists pass through. See
[docs/BODY.md](docs/BODY.md).

The intro, menus, loading, the map, hint cards, cutscenes, finishers and death
appear on a screen in front of you, as the game shows them on the monitor; VR
resumes when play does. On that screen the VR controllers are the game's Xbox
controller: the left thumbstick moves through menus, A selects, B goes back,
X and Y are the game's X and Y, the grips are the bumpers (menu tabs), the
triggers are the triggers, the right thumbstick and the stick clicks are the
same on both, and the menu button is Start. The game shows Xbox prompts, which
sit where the Touch controller buttons are. In immersive VR the menu button
pauses the game, Y opens the game menu (map, suits, skills), and B is the
game's Y, its interact button (backpacks, doors, consoles, prompts; web strike
in a fight), except while a web carries you. Every other control stays with
VR, which walks and jumps through the same virtual controller. A held from the
screen (Resume, Continue) does not jump, nor B held from it interact. A real
gamepad keeps working. The keyboard does not: once the virtual controller has
pressed a button, the game ignores the keyboard for the player. The screen has the size of the
game's desktop window, small when the launcher starts the game;
`-FullDesktopView` makes it sharper.

Click **both thumbsticks** to switch between immersive VR and a flat screen
inside the headset. Release both before clicking again. Flat mode uses the
stock game camera and the game's own controls on a real gamepad (not the
keyboard, as above; the Touch controllers pass only the menu button and Y);
VR movement and web input are released. In immersive VR the monitor shows your head view, horizontally
stretched; in flat mode it shows the normal game view. Add `-StockMonitorView`
to keep the normal game view on the monitor in immersive VR too, at the cost of
culling and shading that follow the stock camera instead of your head.

When the launcher starts the game, the game opens in a small window with your
desktop's shape, 540 pixels tall, centred. The engine still runs culling, shadow
setup, and auto-exposure for that view; a small window keeps that work and skips
most of its rendering. The launcher saves the game's window settings in
`reports\desktop-view-before-vr.json` and writes them back after the game
closes. If the launcher stops while the game is still running, they are written
back the next time it starts the game, or run `python tools\vr_display.py
--restore` after closing the game. A game that is already running keeps its
size. Add `-FullDesktopView` to start the game with your own settings.

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
not a fixed 1536-square image. The launcher prints the selected dimensions.
For an explicit square override, use `tools/launch-game-vr.ps1 -Size 2048`.

The new launcher, automatic dimensions, and thumbstick toggle are built. Their
combined headset check was deferred at the user's request. Returning to VR
after stopping the launcher is not yet seamless. Restart the game before
reattaching after a stopped session or a rebuilt DLL.

## Memory for a VR session

Two memory limits matter, and neither of them is RAM running out.

**Render memory.** The game keeps two frames of draw lists and render commands
in a 128 MB ring. Three scene views need more, so the launcher gives the game a
512 MB ring while the game starts (see the latest build above). A few seconds
into VR the launcher prints either `Render memory: 512 MB for two frames (the
game's own: 128 MB).` or a warning that the game is on its own ring. After the
warning, close the game and start it with `Launch Spidy VR.cmd`. When the
session ends the launcher prints the most two frames used and how many frames
did not fit; the report has the same under `render_memory`.

**Windows commit.** Windows promises each program the memory it asks for, out
of RAM plus page file, whether or not the program then uses it. The game's GPU
allocations count as well. In VR at 3072 x 3264 per eye the game takes about
17 GB of promises. On October 5 this PC had 61.6 GB of RAM with half of it
free, but other programs held about 50 GB of promises and the page file was
4 GB, which left 14-15 GB. The game went past that. Windows enlarges a system-managed page file when this happens, and while
it does, requests for memory can stall or fail. When less than about 19 GB is
left, the launcher prints a warning and waits for Enter before it starts the
game, and the report records `free_commit_mb` at the start and at its lowest.
Either of these makes room:

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
(windowed, size, position) for that session and writes them back after the game
closes; see [Start Spider-Man in VR](#start-spider-man-in-vr).

The two eyes are extra native scene views. They inherit the game's own quality
settings, including textures, shadows, level of detail, crowds, ray-traced
reflections, ambient occlusion, anti-aliasing, and motion blur strength. For
each eye, Spidy sets only:

- the runtime-recommended resolution (3072 x 3264 in the latest session), with
  private scene buffers at that size;
- the tracked eye pose and asymmetric headset lens, with zero lens jitter;
- the active post-processing profile and the main camera's display path;
- the main camera's current exposure, so both eyes share auto-exposure;
- the HUD's sRGB overlay setup pass, which the game skips for secondary views;
- avatar visibility while immersive VR is active (this also hides the avatar
  and its shadow on the monitor).

While immersive, Spidy also moves the game's own camera view, the one on the
monitor, to your tracked head with a lens covering both eyes. The game does some
per-frame work only for that view: occlusion, "drawn in any view" checks, and the
shading inputs shared by every view of a frame, which carry the key-light shadow
setup and a camera position available to every shader. Moving the view lines
that work up with the headset. That view now covers a wider field of view than
the stock camera, which may cost some frame rate; compare with `-StockMonitorView`.

The desktop view renders at the game window's size. Before the small VR window,
it rendered the full 3440 x 1440 desktop, about a fifth of the frame's pixels;
at 1290 x 540 it is about 3%. The October 5 morning session averaged 44 new
stereo pairs per second at
3072 x 3264 per eye with the headset at 120 Hz; about 40% of the frames sent to
the headset repeated an earlier pair, which doubles fast-moving scenery. The
largest cost is eye resolution: lower Virtual Desktop's quality preset, or
use `tools\launch-game-vr.ps1 -Size 2048` for a square override. Ray-traced
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

## Run the lab

Connect Quest 3 to Virtual Desktop on this PC, then double-click
`Launch Spidy Lab.cmd`, or run:

```powershell
.\tools\launch-lab.ps1
```

Use `-Probe` to check the runtime without starting a headset session. The default
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
in [AUTOMATION.md](docs/AUTOMATION.md). A 30-second free-roam run captured 3,362
valid camera/player observations; its hook was disabled and original entry
bytes restored afterward. Native eye rendering is now demonstrated in a bounded
diagnostic; the playable VR game adapter is still in development.

## Development map

See [validation results](docs/VALIDATION.md) for what passed locally and what
still requires a running game or headset.

| Path | Role |
|---|---|
| `include/spidy`, `src/swing.cpp` | Engine-independent physics and control logic |
| `src/xr_session.cpp` | OpenXR timing, tracking, actions, stereo submission |
| `src/d3d12_renderer.cpp` | Lab renderer, tracked hand overlay, diagnostic readback |
| `src/native_webs.cpp` | The game's own web lines, started at the tracked wrists |
| `src/web_grab.cpp`, `src/lab_props.cpp` | Web grab: catching, yanking, carrying and throwing props and characters; the lab's props |
| `src/body_ik.cpp`, `src/native_body.cpp` | The player's body: the solver, and the hero's joints turned after the game's pose writer |
| `src/punch.cpp`, `src/game_punch.cpp` | Punching: fists against characters, and the game's own melee damage for each punch |
| `src/game_grab.cpp`, `src/native_bodies.cpp`, `src/game_targets.cpp` | Web grab in the game: candidates, freed Havok props, flung bots |
| `src/native_render_memory.cpp`, `tools/vr_launcher.py` | A larger per-frame render memory ring, installed while the game starts |
| `src/web_visual.cpp` | Fallback game-style web strands for the headset overlay and lab |
| `src/game_xr.cpp`, `src/game_tracking.cpp` | Native eye presentation and Quest controls |
| `src/native_rays.cpp`, `src/native_movement.cpp`, `src/game_swing.cpp` | Native world queries and collision-controlled swing requests |
| `src/lab_world.cpp`, `apps/xr_lab.cpp` | Synthetic collision world and VR lab |
| `tools/inspect_game.py`, `tools/discover_render_types.py` | Offline game research |
| `src/game_observer.cpp`, `tools/observe_game.py` | Opt-in native camera callback diagnostic |
| `tools/game_driver.mjs` | Observed game launch, screenshots, and input workflow |
| `tools/vr_display.py` | Small desktop window for VR launches; restores the game's window settings |
| `docs/MILESTONE-1.md` | In-game acceptance criteria and remaining integration work |
| `docs/REFERENCE.md` | Reference mechanics, executable evidence, source attribution |

The bounded adapter connects `spidy_openxr` to the game's D3D12 device and direct
queue, then copies matching native eyes after their submitted GPU markers.
See [milestone details](docs/MILESTONE-1.md) for its remaining validation.
