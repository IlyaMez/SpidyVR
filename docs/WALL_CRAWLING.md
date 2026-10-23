# Hand wall crawling

These additions extend the experimental **HAND WALL CLIMBING** setting.
The setting remains off by default. Enable it in Settings > SPIDY VR >
EXPERIMENTAL. Turn it off to use the ordinary game movement.

## Controls and movement

- Squeeze a grip with that palm near a wall, then pull the hand to move.
  Release to reposition it. Touching alone does not start climbing.
- Palms stay open and vibrate on attachment. Solid hand contact stops the
  visible hand at the surface; pressing farther moves the viewpoint and
  upper body away from it.
- Move the left stick on a wall to enter the wall-running view. Release
  it to return to the close crawling view. Head yaw steers the run;
  looking down cannot reverse forward travel.
- Hold both grips and roll the controllers to orient the wall-climbing
  view. Releasing either grip retains that orientation. The gesture is
  suspended in the back-facing stance and resumes with a fresh neutral
  when facing the wall again.
- To turn the body away from the wall, withdraw one hand, turn your head
  toward the other side, and touch that hand against the wall again.
  Looking around or holding an unchanged contact does not request a turn.
  Use the same departure/recontact gesture to turn back.
- A jumps away. Existing web-button rules still apply while climbing.

## Body and surfaces

Outdoor crawling uses alternating steps with planted toe contacts,
outward-facing feet, and knees below the pelvis. Each foot stays on its
own side of the body. Back-facing crawling uses a narrower stance and
alternates a stretched leg with a bending leg. Looking around at rest
does not reposition the waist or feet.

Camera clearance is fixed at 25 cm facing the wall and 40 cm facing
away. The old distance value remains readable in stored profiles but
does not change these clearances or appear as an adjustable row.

Corner handovers transport the view onto the next face and advance the
body around projecting trim. Vents keep the game's native crawling
animation and collision, including floor crawling. Hand pulls, solid
palms, upper-body reach and camera transitions work across vent floors,
walls and ceilings; outdoor back-facing turns and wall running do not
activate inside vents.

Vent entry requires enclosing geometry. Native floor flags also occur
during ordinary ground turns and object handling, so flags or elapsed
time alone cannot start the prone view. Confirmed vents retain their
support through scripted surfaces whose collision rays may be missing.

## Validation

Player-tested on Quest 3 through Virtual Desktop, including the merged
Spidy 0.4.1 build. The player confirmed the known camera bugs were fixed,
the animation worked well and the merged build was working well.

Automated regressions cover physical pulls, corner handovers, planted
steps, knee and foot limits, stance changes, stable head/waist alignment,
hand pressure, vent edges, ground camera isolation and protocol validation.
The public 0.4.1 swing command stays version 5/240 bytes. Extended crawl
telemetry is version 2/72 bytes, and the body command is version 10/272
bytes; use matching modules and tools together.

## Implementation map

- `body_ik` fits the body to tracked poses. `CrawlFeet`, `CrawlHandSteps`
  and `CrawlTurn` handle visual foot contacts, stride cues and supported
  stance changes; they do not move the game's collision body.
- `HandCollision` and `GameTrackingRig` constrain visible palms and place
  the camera, including pressure displacement and vent transitions.
- `CrawlHands` supplies hand-pull intent to the game's native vent crawl.
  `VentCrawlMode` requires enclosing geometry before acquiring that mode.
- `game_swing`, `native_body` and their Python probes exchange versioned
  commands and feedback. Stored settings profiles retain their keys and
  normalize old distance values to the fixed clearance.
