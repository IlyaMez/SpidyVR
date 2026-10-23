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
inline constexpr int fixedWallDistance = 25;
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
    // Experimental, off unless chosen: the left stick and A in the air flip
    // you (game_tracking's FlipMotion); off, A does nothing there.
    bool flips = false;
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
    // your floor (the tab's STAND ON WALLS; GameTrackingRig::standOnWalls);
    // off, you stay upright beside it.
    bool standOnWalls = true;
    // Experimental physical hand pulls, solid palms and crawling body poses.
    bool handClimb = false;
    int wallDistance = fixedWallDistance; // retained for stored-profile compatibility
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
    // Realistic flips, experimental (the tab's REALISTIC FLIPS and the nine
    // rows under it; RealisticFlipSettings, which holds what each means): the
    // swing's flight and the arms turn the player in the air, and A there
    // rights them.
    bool realisticFlips = false, automaticReleaseFlip = true;
    int automaticStrength = 50, automaticSpeed = 240, automaticMinimumSpeed = 3;
    int automaticResponse = 500, automaticHandStrength = 100, automaticFalloff = 3000;
    int automaticHandThreshold = 100;
    bool automaticAirBrake = true;
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
    bool operator==(const Values&) const = default;
};
// The steps the game's Settings offer. The launcher's slider sets any swing
// speed; the Settings show the step nearest to it.
inline constexpr int swingSpeeds[] = {10, 15, 20, 25, 32, 40, 48, 56, 65};
inline constexpr int snapTurns[] = {0, 15, 30, 45, 60, 90};
inline constexpr int smoothTurns[] = {0, 60, 90, 120, 180, 240};
inline constexpr int hapticLevels[] = {0, 25, 50, 75, 100};
inline constexpr int weights[] = {40, 60, 80, 100, 125, 150, 200, 250, 300};
inline constexpr int flipSpeeds[] = {90, 120, 150, 180, 240, 300, 360, 480};
inline constexpr int nearClips[] = {10, 15, 20, 30, 50, 75, 100};
// Nearer than 20 cm would do nothing: the eyes keep that far from any surface
// (HeadClearance::margin).
inline constexpr int wallDistances[] = {20, 25, 30, 40, 50, 60, 75, 100};
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
// 10-100 mm, recording FOV 0 (off) or 130-150 degrees, UI style 0-1.
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
    standOnWalls,
    screenDepth,
    view,
    webStyle,
    handClimb,
    wallDistance,
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
};
inline constexpr Item lastItem = Item::uiStyle;
// The SPIDY VR tab, top to bottom: a section's heading (item none) or a
// setting. The sections: WEBS, SWINGING, COMFORT, HAPTICS, DISPLAY, BODY and
// EXPERIMENTAL (the player, October 19: "sort the options in spidy vr
// settings ingame into nice sections"; HAPTICS since October 20, the
// controllers' vibration and the bHaptics rows together). A setting without choices is an ON/OFF switch, the game's own;
// one with choices steps through them. Titles are upper case, as the game's
// own rows are; the help shows beside the rows while the setting is selected.
struct Row {
    Item item{};
    const char* title{};
    const char* help{};
    std::span<const char* const> choices{};
    // On the pause menu's first page, under RESUME, instead of the tab: VR
    // VIEW (the user, October 15: "move the VIEW: GAME SCREEN to the first
    // menu (the one with resume)").
    bool page{};
};
const std::array<Row, 42>& rows();
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
// RECORDING FOV, UI STYLE, HAND WALL CLIMBING, WALL CAMERA DISTANCE, the
// realistic flips' ten rows and the body haptics' four groups. The game
// module keeps them as these key=value lines in a file of their own, beside
// the launcher's settings (game_xr.cpp, tabOnlyPath), and reads them back
// when VR starts; the launcher's options read and write the same file.
std::string tabOnlyText(const Values&);
// The values with what such text holds, sanitized. A line with an unknown
// key, or whose value is no whole number, is passed over.
Values withTabOnly(Values, std::string_view text);
} // namespace spidy::vr_settings
