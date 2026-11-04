#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Spidy's VR settings. The launcher (or the command line) gives a session
// the values it starts with; during play the game's own Settings change
// them: pause, Settings, then SPIDY VR after the game's last tab (game_menu).
// The session reports the last ones, and the launcher starts the next
// session with them. The few no launcher.ini key carries the game module
// keeps in a file of its own (tabOnlyText), which the launcher's options
// change too.
namespace spidy::vr_settings {
// How far the eyes keep from a wall you climb by hand, and from a duct's face
// in its close view, centimetres (GameTrackingRig::wallDistance): fixed since
// WALL CAMERA DISTANCE went (October 24).
inline constexpr int fixedWallDistance = 25;
// Wall-run pose selected by the user; retired tuning keys cannot override it.
inline constexpr int fixedRunTorsoAngle = 15;
inline constexpr int fixedRunCameraDistance = 110;
inline constexpr int fixedMomentumCameraTilt = 30;
inline constexpr int fixedMomentumCameraDistance = 90;
inline constexpr int fixedMomentumBodyTilt = 50;
inline constexpr int fixedMomentumForwardLean = 30;
inline constexpr int fixedMomentumStepLength = 100;
inline constexpr int fixedMomentumStepPace = 100;
struct Values {
    bool aimMarkers = true;
    // Webs catching props and thugs, punching and your own body, like the web
    // shooter below: on unless run_game_vr.py's switches turn them off; the
    // tab and the launcher do not offer them.
    bool webGrab = true, punch = true, body = true;
    float swingSpeed = 32; // the swing's speed limit, m/s
    int snapTurn = 30;     // degrees per flick of the right stick; 0: no snap turning
    int haptics = 100;     // controller vibration, percent
    int screenSize = 1;    // the game screen: 0 small, 1 medium, 2 large
    bool airWebs = true;   // a web that meets nothing within reach holds in open air there
    // With airWebs off, a web that just misses (by up to 8 degrees) catches
    // the nearest real surface instead (Swing::shot; pull request #39).
    bool smartAnchor = false;
    bool webShooter = true; // a free hand's trigger shoots web balls
    // Degrees a second the right stick turns you while held over; 0: it snap
    // turns instead.
    int smoothTurn = 0;
    // How heavy you are while webs fly you (swinging, and after letting go
    // until you land), percent of real gravity: the swing's gravity. 100, real
    // gravity, since October 12; 80 from October 9; before, 60 (the 6 m/s^2
    // Spidy flew at from October 5).
    int weight = 100;
    // The game as it is instead of VR (the tab's VIEW: GAME SCREEN): its own
    // camera on the game screen in the headset, in 3D at GAME SCREEN 3D's
    // depth, played as on an Xbox controller, with nothing of Spidy's on the
    // hero. Each session starts in VR: the launcher does not keep it. Until
    // October 12 a click of both thumbsticks switched it; until October 15
    // it showed the game flat, played with a real controller.
    bool flatScreen = false;
    // A T-pose calibration of your body is wanted at the next gameplay (the
    // tab's CALIBRATE BODY: ON RESUME); the XR worker clears it once the
    // calibration starts (body_calibration.hpp).
    bool calibrate = false;
    // Experimental, off unless chosen: airborne basic flips use the right
    // stick, release holds the angle, and A returns upright (FlipMotion).
    // REALISTIC FLIPS retains its own controls and optional left-stick steering.
    bool flips = false;
    bool controlledFlips = false; // airborne A toggles left-stick flip mode
    // The web button (the tab's WEB BUTTON): false the grip, which shoots and
    // holds a hand's web while the trigger reels it in and shoots web balls;
    // true the trigger, the two swapped (GameTrackingRig::triggerWebs).
    bool triggerWebs = false;
    // The game's HUD in the headset (the tab's HUD): 0 off, 1 small, 2 medium,
    // 3 large (native_hud::widths).
    int hud = 2;
    // How fast a flip turns you at full tilt of the left stick, degrees a
    // second (the tab's FLIP SPEED; FlipMotion::speed): 180 since October 9,
    // a quarter slower than the flips' first 240.
    int flipSpeed = 180;
    // Walking a wall or stopped on it, your view turns so that the wall is
    // your floor (GameTrackingRig::standOnWalls); off, you stay upright
    // beside it. On unless run_game_vr.py's --no-stand-on-walls turns it off:
    // the tab and the launcher do not offer it (since October 15).
    bool standOnWalls = true;
    // Experimental physical hand pulls, solid palms and crawling body poses.
    bool handClimb = false;
    bool halfPunchDamage = false; // half health damage, unchanged punch reactions
    // The web tie (the tab's WEB TIE; web_tie.hpp, docs/WEB-TIE.md): a webbed-up
    // enemy stuck to a wall by the hands together and the right stick's click,
    // and a webbed enemy who comes to the player only on the yank
    // (GrabConfig::yankOnlyCharacters). Off by default: both as before.
    bool webTie = false;
    // The wall-floor camera transition during HAND WALL CLIMBING's joystick
    // runs (the tab's JOYSTICK WALL RUN CAMERA), as before pull request #38;
    // off, the left stick drives the momentum wall run's view and gait.
    bool joystickRunCamera = true;
    bool wallRunCameraTilt = true; // optional horizontal momentum-run camera bank
    // Scenes the game plays on the game screen (cutscenes, finishers) have
    // depth there (the tab's GAME SCREEN 3D): 0 off, the game's flat frame; 1
    // low, 2 medium, 3 high (screenDepths).
    int screenDepth = 2;
    // Arcade webs (the tab's WEB STYLE: ARCADE; SwingConfig::arcade): webs go
    // through lamp posts, signs, cars and other thin or loose things, and
    // swings stay above the street. Off, REALISTIC: webs are ropes that bend
    // round whatever comes between you and where they stick.
    bool arcadeWebs = false;
    // How near the eyes the headset still shows things, millimetres (the
    // tab's NEAR CLIP, nearClips; near_clip.hpp). The game's own camera clips
    // at 100, which cut off hands held up before the face.
    int nearClipMm = 20;
    // The monitor's view in immersive VR, degrees across (the tab's RECORDING
    // FOV; recording_fov.hpp), 0 for off: the window then mirrors the left
    // eye. An angle renders the game's own view again for the window, which
    // costs frame time and leaves Spidy's drawings out of the window.
    int recordingFov = 0;
    // Realistic flips, experimental (the tab's REALISTIC FLIPS and the ten
    // rows under it; RealisticFlipSettings, which holds what each means): the
    // swing's flight and the arms turn the player in the air, and A there
    // rights them. automaticCombat: FLIPS IN COMBAT, off keeps them out of
    // fights (the player, October 24: "lets add a toggle to disable realistic
    // flips during combat").
    bool realisticFlips = false, automaticReleaseFlip = true;
    int automaticStrength = 50, automaticSpeed = 240, automaticMinimumSpeed = 3;
    int automaticResponse = 500, automaticHandStrength = 100, automaticFalloff = 3000;
    int automaticHandThreshold = 100;
    bool automaticAirBrake = true, automaticCombat = true;
    // How strongly a bHaptics vest, sleeves and visor play (the tab's BODY
    // HAPTICS; body_haptics.hpp), percent; 0 off. And which of its groups
    // play (the tab's WEB, WIND, COMBAT and MOVEMENT HAPTICS switches;
    // body_haptics::Group): the player, October 20, after the first session
    // on the devkit, asked for "one switch per effect group".
    int bodyHaptics = 100;
    bool hapticWebs = true, hapticWind = true, hapticCombat = true, hapticMovement = true;
    // How Spidy's own marks in the headset look (the tab's UI STYLE;
    // ui_style.hpp's UiStyle): 0 comic, printed as Spidy's videos are; 1
    // tech, as the game's own HUD and menus are.
    int uiStyle = 0;
    // What Spidy's blows sound like (the tab's BLOW SOUNDS; game_sound.hpp):
    // 0 the game's own melee impacts, 1 Spidy's own tones (impact_sound.hpp),
    // as before October 22, 2 none. Players asked for both (October 13: "pls
    // an option to remove the sfx when punching", 12 reactions).
    int blowSounds = 0;
    bool webSlingshot = false; // two fixed webs drawn from a standstill, then released
    bool betterClimbing = false; // retain hand-over-hand progress along a shared web
    // Your height, centimetres (the tab's HEIGHT; seated.hpp): 0 AUTO, the
    // T-pose calibration's or the headset's; set, it sizes Spider-Man in their
    // place and is the height SEATED raises you to. And seated play (the tab's
    // SEATED): the view and the body rise from where you sit to that height.
    int heightCm = 0;
    bool seated = false;
    bool combatVisuals = true; // combat overlays and automatic combat slow-motion VFX
    // Aiming with the eyes (the tab's EYE AIM; eyeAimShots, eyeAimZips): web
    // balls go where the eyes look, from the hand that pulled; a zip goes to
    // the point the eyes look at, the game's camera being turned onto it.
    // Only where the headset tracks the eyes (XrRuntime::eyeTracking); 0, or
    // no eye tracker, the hands aim balls and the head zips as before.
    int eyeAim = 0;
    // How far webs stretch under load (the tab's ELASTIC WEB): 0 off, a hard
    // rope as before; 1 to 3 softer and softer (Swing::elasticWeb; pull
    // request #40).
    int elasticWeb = 0;
    // How long a loaded web's last pull fades after you let go (the tab's
    // SOFT RELEASE): 0 at once, as before; 1 to 3 for 120, 180 or 240 ms
    // (Swing::softRelease; pull request #41).
    int softRelease = 0;
    // While a web carries you and for a moment after, speed into what is
    // ahead is cut so you do not slam into it; your lane stays yours (the
    // tab's SWING ASSIST; Swing::allowSwingAssist; pull request #42).
    bool swingAssist = false;
    // How fast WEB SLINGSHOT launches you at full draw, m/s (the tab's
    // SLINGSHOT SPEED): 0, its launch as before, under the swing's speed
    // limit; 20 to 300 its own (Swing::setSlingshotSpeed; pull request #45).
    int slingshotSpeed = 0;
    // How smoothly RECORDING FOV's window view turns with the head (the tab's
    // RECORDING SMOOTHING): 0 off; 10 to 100, slower and slower. The
    // headset's view is never smoothed (pull request #46).
    int recordingSmoothing = 0;
    // Hand gestures on webs (pull request #47): TWO HAND WEB ZIP, both webs
    // on one spot and both arms pulled back fast launch you at it; HAND ROPE
    // STEERING, the free hand on your web too pulls sideways to turn the
    // swing (Swing::allowGestures).
    bool twoHandZip = false, ropeSteering = false;
    // How far a web reaches, open-air webs too, m (the tab's WEB RANGE; 100
    // as before), and how fast it flies there (WEB SPEED): 0 it sticks at
    // once, as before; 1 to 3 it flies at 60, 150 or 350 m/s, pulls only
    // once it has arrived and catches softly (Swing::webTravel; pull
    // request #48).
    int webRange = 100, webSpeed = 0;
    // The pair the headset shows turned by the player's own turn since it was
    // rendered (the tab's FLIP REPROJECTION; flip_reprojection.hpp): flips,
    // spins and smooth turns as steady as head turns. Off by default, to
    // compare in the headset.
    bool flipReprojection = false;
    // The HUD as a layer of its own that the headset shows at its own rate
    // (the tab's STEADY HUD; steady_hud.hpp), instead of drawn into each eye
    // pair by the game: it holds still when the headset repeats a pair. Off
    // by default, to compare in the headset.
    bool steadyHud = false;
    // The reel felt in the hand (the tab's REEL HAPTICS; reel_haptics.hpp):
    // the reeling hand's controller ticks like a winch. Off by default.
    bool reelHaptics = false;
    // The swing felt in the hand (the tab's SWING HAPTICS; swing_haptics.hpp):
    // a taut web's hand ticks with the body's travel, fainter than the reel.
    // Off by default.
    bool swingHaptics = false;
    // The phone's MUSIC plays what the PC plays (the tab's PC MEDIA;
    // phone_pc_media.hpp): Spotify, a browser. Off by default: the Music folder.
    bool pcMedia = false;
    // NATIVE RUN (the tab's NATIVE RUN; game_pad::NativeRun): on the ground,
    // the left stick past 70% holds the game's RT, its parkour run - sprint,
    // vaults, wall runs - until it falls under 50%. Off by default: the stick
    // jogs as before. Never in the air, on a web or on a wall.
    bool nativeRun = false;
    // AIR SLINGSHOT (the tab's AIR SLINGSHOT; Swing::allowAirSlingshot): the
    // slingshot's draw in the air too - both webs under tension, both grips
    // held and both arms drawn until the hands buzz, then let go: a launch
    // along the two webs. Off by default: in the air a release only lets go.
    bool airSlingshot = false;
    // WEB ANGLE, degrees (the tab's WEB ANGLE; web_aim.hpp): a hand's webs
    // tilt up (positive) or down from the controller's line, -45 to 45. And
    // WRIST SHOOTERS (the tab's WRIST SHOOTERS; redefined in v6): they leave
    // parallel to the forearm, out past the wrist toward the fingers, as the
    // comics' and the films' shooters do: along the body's forearm when the
    // body can say (as MODEL FOREARM), else along the controller's line. The
    // old palm-side quarter turn is PALM SHOOTERS. 0 and off by default:
    // along the controller, as before. The fists keep the controller's.
    int webAngle = 0;
    bool wristShooters = false;
    // WEB AIM (the tab's WEB AIM; web_aim::Source): 0 CONTROLLER, the webs
    // along the controller as before; 1 MODEL FOREARM, along the forearm of
    // the body the player sees (web_aim::ForearmAim), back to the controller's
    // line whenever the body cannot say. WEB ANGLE and WRIST SHOOTERS turn on
    // top of either. CONTROLLER by default. And FOREARM BLEND, percent (the
    // tab's FOREARM BLEND): how much of the forearm's line MODEL FOREARM takes,
    // 0 (the controller's) to 100 (the forearm's), 100 by default.
    int webAimSource = 0;
    int forearmBlend = 100;
    // DODGE TUMBLE (the tab's DODGE TUMBLE; dodge_tumble.hpp): 0 OFF, 1 LEAN,
    // 2 ROLL. The view leans or rolls with a dodge of the game's, so it reads
    // as one. OFF by default: the view stays level, as before.
    int dodgeTumble = 0;
    // RUN CURVE (the tab's RUN CURVE; run_update::curveStick): on the ground
    // the stick reaches the game's full run at 80% of its tilt, more of it
    // early. Off by default: the stick as it is, as before.
    bool runCurve = false;
    // NATIVE RUN AT, percent of the stick's tilt (the tab's NATIVE RUN AT;
    // run_update::nativeRunThresholds): past it NATIVE RUN holds the game's
    // RT, and lets go 20 points under it. 70 by default (70 and 50, as before).
    int nativeRunPercent = 70;
    // RUN LOGS (the tab's RUN LOGS; run_update::RunLog): lines in the
    // session's timeline about running, landing and the mod's ground run, to
    // send back. Off by default. Nothing moves differently either way.
    bool runLogs = false;
    // RUNNING LEAP (the tab's RUNNING LEAP; Swing::allowMomentumLeap,
    // run_update::leapVelocity): a charged leap keeps the run's speed along
    // its way and adds the charge on top; a long one from a fast run goes
    // further and flatter. Off by default: a leap starts from rest, as before.
    bool momentumLeap = false;
    // EARLY JUMP (the tab's EARLY JUMP; game_pad::ChargeJump::earlyJump): 0
    // OFF, a tap of A jumps as it is let go within 0.18 s, as before; 1 QUICK
    // TAP, within 0.1 s; 2 RUNNING, as QUICK TAP, and while NATIVE RUN holds
    // RT the game's A goes out as it is pressed (no charge while running).
    int earlyJump = 0;
    // SOFT LANDING (the tab's SOFT LANDING; Swing::allowSoftLanding,
    // run_update::softLandVertical): a fall the swing flies, with no web
    // holding, eases to 9 m/s down over its last 3 m (or 0.35 s), its speed
    // across kept. Off by default: it lands at the speed it falls, as before.
    bool softLanding = false;
    // SPIDY RUN (the tab's SPIDY RUN; Swing::allowGroundRun, run_update::
    // groundRunStep): a landing faster than the game's sprint is the mod's
    // ground run, the wall run's momentum on the ground - its speed kept, the
    // stick steering it - easing to the sprint and handed back to the game.
    // Off by default: the game takes every landing, as before.
    bool spidyRun = false;
    // SPIDY RUN FROM STANDING (the tab's RUN FROM STANDING; Swing::
    // allowGroundRunFromStanding, run_update::standRunStep): with SPIDY RUN
    // on, the stick held fully on the ground for 0.3 s starts the mod's run
    // from a stand or a walk, and it builds past the sprint while held. Off
    // by default: SPIDY RUN starts only from a fast landing, as before.
    bool spidyRunFromStanding = false;
    // SMOOTH WEB LANDING (the tab's SMOOTH WEB LANDING; Swing::
    // allowWebLandingSmooth, web_landing.hpp): a landing with a web held,
    // slower than WEB LANDING IMPACT, eases its fall over the last metres,
    // keeps its speed across the ground for a moment (decaying to a jog) and
    // no longer flips between the game and the swing on a flicker of the
    // ground contact. Off by default: such a landing is the game's at once,
    // as before.
    bool webLandingSmooth = false;
    // WEB LANDING IMPACT, m/s (the tab's WEB LANDING IMPACT): SMOOTH WEB
    // LANDING's threshold; a landing this fast or faster is the game's, heavy
    // as before. 24 by default, 12 to 40.
    int webLandingImpact = 24;
    // STEADY STATES (the tab's STEADY STATES; ground_state.hpp): NATIVE
    // RUN's RT comes back only after everything has allowed it for 0.15 s
    // (a frame's flicker of the ground, a prompt or a vent no longer drops
    // and raises it), and stays off in a fight. Off by default: as before.
    bool steadyStates = false;
    // COMBAT RESPONSIVE (the tab's COMBAT RESPONSIVE; v5): in a fight (the
    // combat camera, or an enemy in reach within 3 s) the swing starts no
    // SPIDY RUN or landing carry and ends one going on, so the game moves
    // the body for its blows and dodges; NATIVE RUN holds no RT; and A is
    // the game's jump at once, no charge. Off by default: as before.
    bool combatResponsive = false;
    // SPIDY RUN JUMP (the tab's SPIDY RUN JUMP; v6; Swing Input::runJump):
    // A while the swing runs the body on the ground (SPIDY RUN, SMOOTH WEB
    // LANDING's carry) with no web is the swing's own jump at once, the
    // run's speed kept. Off: v5's JUMP ALWAYS (the run ends and the game's A
    // waits up to 0.2 s for the game to have the body back). On by default.
    bool spidyRunJump = true;
    // FOREARM AIM LIMIT, degrees (the tab's FOREARM AIM LIMIT; v6): the most
    // MODEL FOREARM (and WRIST SHOOTERS) turns the webs off the controller's
    // line onto the forearm. 180 (no limit) by default; v3-v5 had 35.
    int forearmAimLimit = 180;
    // PALM SHOOTERS (the tab's PALM SHOOTERS; v6): the old WRIST SHOOTERS of
    // v2-v5, a quarter turn out of the palm side, square to the hand. Off by
    // default. WRIST SHOOTERS now shoots parallel to the forearm.
    bool palmShooters = false;
    // How much speed a fast one-hand web yank adds, percent (the tab's SINGLE
    // HAND YANK STRENGTH; Swing::singleHandYankStrength): 100 as before.
    int singleHandYankPercent = 100;
    bool operator==(const Values&) const = default;
};
inline constexpr int eyeAimShots = 1, eyeAimZips = 2;
// The steps the game's Settings offer. The launcher's slider sets any swing
// speed; the Settings show the step nearest to it.
inline constexpr int swingSpeeds[] = {10, 15, 20, 25, 32, 40, 48, 56, 65};
inline constexpr int snapTurns[] = {0, 15, 30, 45, 60, 90};
inline constexpr int smoothTurns[] = {0, 60, 90, 120, 180, 240};
inline constexpr int hapticLevels[] = {0, 25, 50, 75, 100};
inline constexpr int weights[] = {40, 60, 80, 100, 125, 150, 200, 250, 300};
inline constexpr int flipSpeeds[] = {90, 120, 150, 180, 240, 300, 360, 480};
inline constexpr int nearClips[] = {10, 15, 20, 30, 50, 75, 100};
// HEIGHT's steps, centimetres: AUTO, then 150 to 190 (a row takes nine
// choices at most: five apart where most players are, ten at the ends).
inline constexpr int heights[] = {0, 150, 160, 165, 170, 175, 180, 185, 190};
// Real gravity, m/s^2: a weight of 100% falls at it.
inline constexpr float realGravity = 9.81f;
// The swing's gravity for a weight, m/s^2.
inline float gravity(int weight) {
    return realGravity * static_cast<float>(weight) / 100;
}
// The game screen's width for each size, metres, at screenDistance.
inline constexpr float screenWidths[] = {2.4f, 3.2f, 4.2f};
inline float screenWidth(int size) {
    return screenWidths[size < 0 ? 0 : size > 2 ? 2 : size];
}
// The game screen's distance from the viewer it was placed for, metres
// (presentation_gate's screenAhead).
inline constexpr float screenDistance = 2.5f;
// How much of a scene's depth the game screen shows for each GAME SCREEN 3D
// choice (native_eyes::screenStereo's depth): at 1 everything is as far from
// you as it is from the game's camera, and what is nearer to that camera than
// the screen is to you comes out of the screen; lower, everything lies
// farther back.
inline constexpr float screenDepths[] = {0, .5f, .7f, 1.f};
inline float screenDepth(int choice) {
    return screenDepths[choice < 0 ? 0 : choice > 3 ? 3 : choice];
}
// The values within the session's ranges: swing speed 1-65 m/s (what
// run_game_vr.py accepts), snap turn 0-90 degrees, smooth turn 0-360 degrees
// a second, vibration and body haptics 0-100%, weight 40-300%, HUD 0-3, flip
// speed 90-480 degrees a second, the game screen's depth 0-3, near clip
// 10-100 mm, recording FOV 0 (off) or 130-150 degrees, UI style 0-1, blow
// sounds 0-2, height 0 (AUTO) or 120-220 cm, eye aim 0-3.
Values sanitized(Values);

enum class Item : uint8_t {
    none,
    aimMarkers,
    airWebs,
    swingSpeed,
    snapTurn,
    haptics,
    screenSize,
    smoothTurn,
    weight,
    calibrate,
    flips,
    webButton,
    hud,
    flipSpeed,
    screenDepth,
    view,
    webStyle,
    handClimb,
    nearClip,
    realisticFlips,
    automaticStrength,
    automaticSpeed,
    automaticMinimumSpeed,
    automaticReleaseFlip,
    automaticResponse,
    automaticHandStrength,
    automaticFalloff,
    automaticHandThreshold,
    automaticAirBrake,
    bodyHaptics,
    hapticWebs,
    hapticWind,
    hapticCombat,
    hapticMovement,
    recordingFov,
    uiStyle,
    blowSounds,
    webSlingshot,
    betterClimbing,
    height,
    seated,
    automaticCombat,
    wallRunCameraTilt,
    controlledFlips,
    combatVisuals,
    eyeAim,
    smartAnchor,
    elasticWeb,
    softRelease,
    swingAssist,
    slingshotSpeed,
    recordingSmoothing,
    twoHandZip,
    ropeSteering,
    webRange,
    webSpeed,
    joystickRunCamera,
    halfPunchDamage,
    flipReprojection,
    steadyHud,
    reelHaptics,
    swingHaptics,
    pcMedia,
    singleHandYankStrength,
    webTie,
    nativeRun,
    airSlingshot,
    webAngle,
    wristShooters,
    dodgeTumble,
    runCurve,
    nativeRunAt,
    runLogs,
    momentumLeap,
    earlyJump,
    softLanding,
    spidyRun,
    webAimSource,
    forearmBlend,
    spidyRunFromStanding,
    webLandingSmooth,
    webLandingImpact,
    steadyStates,
    combatResponsive,
    spidyRunJump,
    forearmAimLimit,
    palmShooters,
};
inline constexpr Item lastItem = Item::palmShooters;
// Where a row shows: in the tab; on the pause menu's first page instead,
// under RESUME: VR VIEW (the user, October 15: "move the VIEW: GAME SCREEN to
// the first menu (the one with resume)"); or only in the launcher's options:
// the realistic flips' finest tuning, its thresholds and how a spin builds
// and dies away (the user, October 25: "cleanup the redundant"). The tab's
// RESET ALL leaves the last two alone.
enum class Place : uint8_t { tab, page, launcher };
// The SPIDY VR tab, top to bottom: a section's heading (item none) or a
// setting. The sections: WEBS, SWINGING, COMFORT, HAPTICS, DISPLAY, BODY and
// EXPERIMENTAL (the player, October 19: "sort the options in spidy vr
// settings ingame into nice sections"; HAPTICS since October 20, the
// controllers' vibration and the bHaptics rows together). A setting without choices is an ON/OFF switch, the game's own;
// one with choices steps through them. Titles are upper case, as the game's
// own rows are; the help shows beside the rows while the setting is selected.
// A row shown elsewhere (Place) stands where it would be in the tab.
struct Row {
    Item item{};
    const char* title{};
    const char* help{};
    std::span<const char* const> choices{};
    Place place{};
};
const std::array<Row, 93>& rows();
// The most choices a row shows (game_menu's buffers): a row with more would
// lose its last ones.
inline constexpr size_t maxChoices = 9;
// The values a list setting's choices stand for, in their order; none for a
// switch.
std::span<const int> steps(Item);
// The choice a setting shows for these values: a switch 0 (off) or 1 (on), a
// list its step nearest to the value (a launcher value between two steps
// shows the nearer one, the lower on a tie).
int choice(Item, const Values&);
// Puts a choice into the values; false when it is out of range or changes nothing.
bool choose(Item, int choice, Values&);
// What the game's RESET puts back: the choice for Spidy's defaults (Values{}).
int defaultChoice(Item);
// The settings no launcher.ini key or XrConfig field carries: NEAR CLIP,
// RECORDING FOV, UI STYLE, BLOW SOUNDS, HEIGHT, SEATED, WEB SLINGSHOT, BETTER CLIMBING, HAND WALL
// CLIMBING, EYE AIM, FLIP REPROJECTION, STEADY HUD, REEL HAPTICS, SWING HAPTICS, the realistic flips'
// eleven rows and the body haptics' four groups. The game module keeps them as these key=value lines in a
// file of their own, beside the launcher's settings (game_xr.cpp, tabOnlyPath), and reads them back when
// VR starts; the launcher's options read and write the same file.
std::string tabOnlyText(const Values&);
// The values with what such text holds, sanitized. A line with an unknown
// key, or whose value is no whole number, is passed over.
Values withTabOnly(Values, std::string_view text);
} // namespace spidy::vr_settings
