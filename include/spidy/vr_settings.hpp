#pragma once
#include <array>
#include <cstdint>
#include <span>

// Spidy's VR settings. The launcher (or the command line) gives a session
// the values it starts with; during play the game's own Settings change
// them: pause, Settings, then SPIDY VR after the game's last tab (game_menu).
// The session reports the last ones, and the launcher starts the next
// session with them.
namespace spidy::vr_settings {
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
    // Scenes the game plays on the game screen (cutscenes, finishers) have
    // depth there (the tab's GAME SCREEN 3D): 0 off, the game's flat frame; 1
    // low, 2 medium, 3 high (screenDepths).
    int screenDepth = 2;
    // Arcade webs (the tab's WEB STYLE: ARCADE; SwingConfig::arcade): webs go
    // through lamp posts, signs, cars and other thin or loose things, and
    // swings stay above the street. Off, REALISTIC: webs are ropes that bend
    // round whatever comes between you and where they stick.
    bool arcadeWebs = false;
    int nearClipMm = 20;
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
// a second, vibration 0-100%, weight 40-300%, HUD 0-3, flip speed 90-480
// degrees a second, the game screen's depth 0-3.
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
    nearClip,
};
inline constexpr Item lastItem = Item::nearClip;
// The SPIDY VR tab, top to bottom: a section's heading (item none) or a
// setting. The sections: WEBS, SWINGING, COMFORT, DISPLAY, BODY and
// EXPERIMENTAL (the player, October 19: "sort the options in spidy vr
// settings ingame into nice sections"). A setting without choices is an ON/OFF switch, the game's own;
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
const std::array<Row, 23>& rows();
// The choice a setting shows for these values: a switch 0 (off) or 1 (on), a
// list its step nearest to the value (a launcher value between two steps
// shows the nearer one, the lower on a tie).
int choice(Item, const Values&);
// Puts a choice into the values; false when it is out of range or changes nothing.
bool choose(Item, int choice, Values&);
// What the game's RESET puts back: the choice for Spidy's defaults (Values{}).
int defaultChoice(Item);
} // namespace spidy::vr_settings
