#include "spidy/vr_settings.hpp"
#include "spidy/recording_fov.hpp"
#include "spidy/seated.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <span>

namespace spidy::vr_settings {
namespace {
constexpr const char* swingChoices[] = {"10 M/S", "15 M/S", "20 M/S", "25 M/S", "32 M/S",
                                        "40 M/S", "48 M/S", "56 M/S", "65 M/S"};
constexpr const char* snapChoices[] = {"OFF", "15\xC2\xB0", "30\xC2\xB0", "45\xC2\xB0", "60\xC2\xB0", "90\xC2\xB0"};
constexpr const char* smoothChoices[] = {"OFF",          "60\xC2\xB0/S",  "90\xC2\xB0/S",
                                         "120\xC2\xB0/S", "180\xC2\xB0/S", "240\xC2\xB0/S"};
constexpr const char* hapticChoices[] = {"OFF", "25%", "50%", "75%", "100%"};
constexpr const char* weightChoices[] = {"40%", "60%", "80%", "100%", "125%", "150%", "200%", "250%", "300%"};
constexpr const char* screenChoices[] = {"SMALL", "MEDIUM", "LARGE"};
// CALIBRATE BODY, a switch with names of its own: ON RESUME asks for the T-pose
// as soon as play resumes.
constexpr const char* calibrateChoices[] = {"NO", "ON RESUME"};
// WEB BUTTON, a switch named by the button that webs: TRIGGER swaps it with the grip.
constexpr const char* webButtonChoices[] = {"GRIP", "TRIGGER"};
// WEB STYLE, a switch named by how webs behave: ARCADE passes thin things by.
constexpr const char* webStyleChoices[] = {"REALISTIC", "ARCADE"};
// VIEW, a switch named by what the headset shows: GAME SCREEN is the game as
// it is, its own picture on the screen ahead, as menus are, in 3D.
constexpr const char* viewChoices[] = {"VR", "GAME SCREEN"};
constexpr const char* hudChoices[] = {"OFF", "SMALL", "MEDIUM", "LARGE"};
constexpr const char* flipSpeedChoices[] = {"90\xC2\xB0/S",  "120\xC2\xB0/S", "150\xC2\xB0/S", "180\xC2\xB0/S",
                                            "240\xC2\xB0/S", "300\xC2\xB0/S", "360\xC2\xB0/S", "480\xC2\xB0/S"};
constexpr const char* depthChoices[] = {"OFF", "LOW", "MEDIUM", "HIGH"};
constexpr const char* nearClipChoices[] = {"1 CM", "1.5 CM", "2 CM", "3 CM", "5 CM", "7.5 CM", "10 CM"};
static_assert(std::size(nearClipChoices) == std::size(nearClips));
// RECORDING FOV: the least the monitor's view shows across (recording_fov.hpp).
constexpr const char* recordingFovChoices[] = {"OFF",          "70\xC2\xB0",  "90\xC2\xB0",
                                               "110\xC2\xB0", "120\xC2\xB0", "130\xC2\xB0",
                                               "140\xC2\xB0", "150\xC2\xB0", "160\xC2\xB0"};
static_assert(std::size(recordingFovChoices) == std::size(recording_fov::choices));
// UI STYLE, a list (ui_style.hpp's UiStyle in order) so that a style can
// come after them: COMIC, the printed marks, or TECH, the game's own look.
constexpr int uiStyles[] = {0, 1};
constexpr const char* uiStyleChoices[] = {"COMIC", "TECH"};
static_assert(std::size(uiStyleChoices) == std::size(uiStyles));
// BLOW SOUNDS: the game's own melee impacts, Spidy's own tones, or none.
constexpr int blowSoundSteps[] = {0, 1, 2};
constexpr const char* blowSoundChoices[] = {"GAME", "SPIDY", "OFF"};
static_assert(std::size(blowSoundChoices) == std::size(blowSoundSteps));
// EYE AIM: what the eyes aim (Values::eyeAim's bits, eyeAimShots and eyeAimZips).
constexpr int eyeAimSteps[] = {0, eyeAimShots, eyeAimZips, eyeAimShots | eyeAimZips};
constexpr const char* eyeAimChoices[] = {"OFF", "WEB BALLS", "ZIPS", "BOTH"};
static_assert(std::size(eyeAimChoices) == std::size(eyeAimSteps));
// HEIGHT: AUTO, then the player's own.
constexpr const char* heightChoices[] = {"AUTO",   "150 CM", "160 CM", "165 CM", "170 CM",
                                         "175 CM", "180 CM", "185 CM", "190 CM"};
static_assert(std::size(heightChoices) == std::size(heights));
// The realistic flips' rows (RealisticFlipSettings).
constexpr int automaticStrengths[] = {25, 50, 75, 100};
constexpr const char* automaticStrengthChoices[] = {"25%", "50%", "75%", "100%"};
constexpr int automaticSpeeds[] = {90, 120, 180, 240, 360};
constexpr const char* automaticSpeedChoices[] = {"90\xC2\xB0/S", "120\xC2\xB0/S", "180\xC2\xB0/S",
                                                 "240\xC2\xB0/S", "360\xC2\xB0/S"};
constexpr int automaticMinimumSpeeds[] = {3, 5, 8, 12, 16};
constexpr const char* automaticMinimumChoices[] = {"3 M/S", "5 M/S", "8 M/S", "12 M/S", "16 M/S"};
constexpr int automaticResponses[] = {100, 200, 350, 500, 750};
constexpr const char* automaticResponseChoices[] = {"100 MS", "200 MS", "350 MS", "500 MS", "750 MS"};
constexpr int automaticHandStrengths[] = {0, 25, 50, 75, 100, 150, 200};
constexpr const char* automaticHandStrengthChoices[] = {"0%", "25%", "50%", "75%", "100%", "150%", "200%"};
constexpr int automaticFalloffs[] = {500, 1000, 1500, 2000, 3000};
constexpr const char* automaticFalloffChoices[] = {"FAST", "QUICK", "MEDIUM", "SLOW", "VERY SLOW"};
constexpr int automaticHandThresholds[] = {50, 100, 150, 200, 300};
constexpr const char* automaticHandThresholdChoices[] = {"0.5 M/S", "1 M/S", "1.5 M/S", "2 M/S", "3 M/S"};
static_assert(std::size(automaticStrengthChoices) == std::size(automaticStrengths) &&
              std::size(automaticSpeedChoices) == std::size(automaticSpeeds) &&
              std::size(automaticMinimumChoices) == std::size(automaticMinimumSpeeds) &&
              std::size(automaticResponseChoices) == std::size(automaticResponses) &&
              std::size(automaticHandStrengthChoices) == std::size(automaticHandStrengths) &&
              std::size(automaticFalloffChoices) == std::size(automaticFalloffs) &&
              std::size(automaticHandThresholdChoices) == std::size(automaticHandThresholds));
constexpr int screenSizes[] = {0, 1, 2};
constexpr int hudSizes[] = {0, 1, 2, 3};
constexpr int depthSteps[] = {0, 1, 2, 3};
// A level: OFF, then LOW, MEDIUM and HIGH (ELASTIC WEB, SOFT RELEASE).
constexpr int levelSteps[] = {0, 1, 2, 3};
constexpr const char* levelChoices[] = {"OFF", "LOW", "MEDIUM", "HIGH"};
static_assert(std::size(swingChoices) == std::size(swingSpeeds) && std::size(snapChoices) == std::size(snapTurns) &&
              std::size(smoothChoices) == std::size(smoothTurns) &&
              std::size(hapticChoices) == std::size(hapticLevels) && std::size(screenChoices) == std::size(screenSizes) &&
              std::size(weightChoices) == std::size(weights) && std::size(hudChoices) == std::size(hudSizes) &&
              std::size(flipSpeedChoices) == std::size(flipSpeeds) &&
              std::size(depthChoices) == std::size(depthSteps) && std::size(depthSteps) == std::size(screenDepths));
static_assert(std::size(levelChoices) == std::size(levelSteps));
// SLINGSHOT SPEED: 0 keeps the launch under the swing's speed limit.
constexpr int slingshotSpeeds[] = {0, 20, 40, 50, 65, 100, 150, 200, 300};
constexpr const char* slingshotSpeedChoices[] = {"SWING LIMIT", "20 M/S",  "40 M/S",  "50 M/S", "65 M/S",
                                                 "100 M/S",     "150 M/S", "200 M/S", "300 M/S"};
static_assert(std::size(slingshotSpeedChoices) == std::size(slingshotSpeeds));
// RECORDING SMOOTHING: off, then slower and slower turns.
constexpr int recordingSmoothings[] = {0, 10, 20, 30, 40, 50, 60, 80, 100};
constexpr const char* recordingSmoothingChoices[] = {"OFF", "10", "20", "30", "40", "50", "60", "80", "100"};
static_assert(std::size(recordingSmoothingChoices) == std::size(recordingSmoothings));
} // namespace

std::span<const int> steps(Item item) {
    switch (item) {
    case Item::swingSpeed:
        return swingSpeeds;
    case Item::snapTurn:
        return snapTurns;
    case Item::smoothTurn:
        return smoothTurns;
    case Item::haptics:
    case Item::bodyHaptics:
        return hapticLevels;
    case Item::screenSize:
        return screenSizes;
    case Item::weight:
        return weights;
    case Item::hud:
        return hudSizes;
    case Item::flipSpeed:
        return flipSpeeds;
    case Item::screenDepth:
        return depthSteps;
    case Item::elasticWeb:
    case Item::softRelease:
        return levelSteps;
    case Item::slingshotSpeed:
        return slingshotSpeeds;
    case Item::recordingSmoothing:
        return recordingSmoothings;
    case Item::nearClip:
        return nearClips;
    case Item::recordingFov:
        return recording_fov::choices;
    case Item::uiStyle:
        return uiStyles;
    case Item::blowSounds:
        return blowSoundSteps;
    case Item::height:
        return heights;
    case Item::eyeAim:
        return eyeAimSteps;
    case Item::automaticStrength:
        return automaticStrengths;
    case Item::automaticSpeed:
        return automaticSpeeds;
    case Item::automaticMinimumSpeed:
        return automaticMinimumSpeeds;
    case Item::automaticResponse:
        return automaticResponses;
    case Item::automaticHandStrength:
        return automaticHandStrengths;
    case Item::automaticFalloff:
        return automaticFalloffs;
    case Item::automaticHandThreshold:
        return automaticHandThresholds;
    default:
        return {};
    }
}
namespace {
float numberOf(Item item, const Values& v) {
    switch (item) {
    case Item::swingSpeed:
        return v.swingSpeed;
    case Item::snapTurn:
        return static_cast<float>(v.snapTurn);
    case Item::smoothTurn:
        return static_cast<float>(v.smoothTurn);
    case Item::haptics:
        return static_cast<float>(v.haptics);
    case Item::bodyHaptics:
        return static_cast<float>(v.bodyHaptics);
    case Item::screenSize:
        return static_cast<float>(v.screenSize);
    case Item::weight:
        return static_cast<float>(v.weight);
    case Item::hud:
        return static_cast<float>(v.hud);
    case Item::flipSpeed:
        return static_cast<float>(v.flipSpeed);
    case Item::screenDepth:
        return static_cast<float>(v.screenDepth);
    case Item::elasticWeb:
        return static_cast<float>(v.elasticWeb);
    case Item::softRelease:
        return static_cast<float>(v.softRelease);
    case Item::slingshotSpeed:
        return static_cast<float>(v.slingshotSpeed);
    case Item::recordingSmoothing:
        return static_cast<float>(v.recordingSmoothing);
    case Item::nearClip:
        return static_cast<float>(v.nearClipMm);
    case Item::recordingFov:
        return static_cast<float>(v.recordingFov);
    case Item::uiStyle:
        return static_cast<float>(v.uiStyle);
    case Item::blowSounds:
        return static_cast<float>(v.blowSounds);
    case Item::height:
        return static_cast<float>(v.heightCm);
    case Item::eyeAim:
        return static_cast<float>(v.eyeAim);
    case Item::automaticStrength:
        return static_cast<float>(v.automaticStrength);
    case Item::automaticSpeed:
        return static_cast<float>(v.automaticSpeed);
    case Item::automaticMinimumSpeed:
        return static_cast<float>(v.automaticMinimumSpeed);
    case Item::automaticResponse:
        return static_cast<float>(v.automaticResponse);
    case Item::automaticHandStrength:
        return static_cast<float>(v.automaticHandStrength);
    case Item::automaticFalloff:
        return static_cast<float>(v.automaticFalloff);
    case Item::automaticHandThreshold:
        return static_cast<float>(v.automaticHandThreshold);
    default:
        return 0;
    }
}
// A switch setting's value (const for const values), or null for a list.
template <class V> auto switchOf(Item item, V& v) -> decltype(&v.aimMarkers) {
    switch (item) {
    case Item::webSlingshot:
        return &v.webSlingshot;
    case Item::betterClimbing:
        return &v.betterClimbing;
    case Item::smartAnchor:
        return &v.smartAnchor;
    case Item::swingAssist:
        return &v.swingAssist;
    case Item::twoHandZip:
        return &v.twoHandZip;
    case Item::ropeSteering:
        return &v.ropeSteering;
    case Item::combatVisuals:
        return &v.combatVisuals;
    case Item::aimMarkers:
        return &v.aimMarkers;
    case Item::airWebs:
        return &v.airWebs;
    case Item::calibrate:
        return &v.calibrate;
    case Item::flips:
        return &v.flips;
    case Item::controlledFlips:
        return &v.controlledFlips;
    case Item::webButton:
        return &v.triggerWebs;
    case Item::webStyle:
        return &v.arcadeWebs;
    case Item::handClimb:
        return &v.handClimb;
    case Item::wallRunCameraTilt:
        return &v.wallRunCameraTilt;
    case Item::view:
        return &v.flatScreen;
    case Item::realisticFlips:
        return &v.realisticFlips;
    case Item::automaticReleaseFlip:
        return &v.automaticReleaseFlip;
    case Item::automaticAirBrake:
        return &v.automaticAirBrake;
    case Item::automaticCombat:
        return &v.automaticCombat;
    case Item::hapticWebs:
        return &v.hapticWebs;
    case Item::hapticWind:
        return &v.hapticWind;
    case Item::hapticCombat:
        return &v.hapticCombat;
    case Item::hapticMovement:
        return &v.hapticMovement;
    case Item::seated:
        return &v.seated;
    default:
        return nullptr;
    }
}
} // namespace

Values sanitized(Values v) {
    v.swingSpeed = std::isfinite(v.swingSpeed) ? std::clamp(v.swingSpeed, 1.f, 65.f) : 32.f;
    v.snapTurn = std::clamp(v.snapTurn, 0, 90);
    v.smoothTurn = std::clamp(v.smoothTurn, 0, 360);
    v.haptics = std::clamp(v.haptics, 0, 100);
    v.bodyHaptics = std::clamp(v.bodyHaptics, 0, 100);
    v.screenSize = std::clamp(v.screenSize, 0, 2);
    v.weight = std::clamp(v.weight, 40, 300);
    v.hud = std::clamp(v.hud, 0, 3);
    v.flipSpeed = std::clamp(v.flipSpeed, 90, 480);
    v.screenDepth = std::clamp(v.screenDepth, 0, 3);
    v.elasticWeb = std::clamp(v.elasticWeb, 0, 3);
    v.softRelease = std::clamp(v.softRelease, 0, 3);
    v.slingshotSpeed = v.slingshotSpeed <= 0 ? 0 : std::clamp(v.slingshotSpeed, 20, 300);
    v.recordingSmoothing = std::clamp(v.recordingSmoothing, 0, 100);
    v.nearClipMm = std::clamp(v.nearClipMm, nearClips[0], std::end(nearClips)[-1]);
    v.recordingFov = recording_fov::sanitized(v.recordingFov);
    v.uiStyle = std::clamp(v.uiStyle, uiStyles[0], std::end(uiStyles)[-1]);
    v.blowSounds = std::clamp(v.blowSounds, blowSoundSteps[0], std::end(blowSoundSteps)[-1]);
    v.heightCm = v.heightCm <= 0 ? 0 : std::clamp(v.heightCm, seated::minHeightCm, seated::maxHeightCm);
    v.eyeAim = std::clamp(v.eyeAim, 0, eyeAimShots | eyeAimZips);
    // The realistic flips' ranges, as RealisticFlip takes them.
    v.automaticStrength = std::clamp(v.automaticStrength, 25, 100);
    v.automaticSpeed = std::clamp(v.automaticSpeed, 90, 360);
    v.automaticMinimumSpeed = std::clamp(v.automaticMinimumSpeed, 3, 16);
    v.automaticResponse = std::clamp(v.automaticResponse, 100, 750);
    v.automaticHandStrength = std::clamp(v.automaticHandStrength, 0, 200);
    v.automaticFalloff = std::clamp(v.automaticFalloff, 500, 3000);
    v.automaticHandThreshold = std::clamp(v.automaticHandThreshold, 50, 300);
    return v;
}
const std::array<Row, 60>& rows() {
    // The help fits the game's description column beside the rows.
    static const std::array<Row, 60> all{{
        {Item::none, "WEBS", nullptr, {}},
        {Item::webStyle, "WEB STYLE",
         "Arcade: webs pass lamp posts, signs and cars, and swings stay above the street.", webStyleChoices},
        {Item::webButton, "WEB BUTTON",
         "Which button shoots and holds a web; the other reels in and shoots web balls.", webButtonChoices},
        {Item::aimMarkers, "AIM MARKERS", "Show where each web would land.", {}},
        {Item::airWebs,
         "WEBS HOLD IN OPEN AIR",
         "With nothing in reach, a web still holds in open air, 100 m out.",
         {}},
        {Item::smartAnchor,
         "SMART ANCHOR",
         "With open-air webs off, a near miss catches a real surface in a small cone.",
         {}},
        {Item::none, "SWINGING", nullptr, {}},
        {Item::swingSpeed, "SWING SPEED LIMIT", "How fast a swing can carry you.", swingChoices},
        {Item::weight, "WEIGHT",
         "How heavy you are while swinging and after letting go. 100% is real gravity.", weightChoices},
        {Item::elasticWeb, "ELASTIC WEB", "Webs stretch under load for a softer catch. Higher is softer.",
         levelChoices},
        {Item::softRelease, "SOFT RELEASE",
         "A web's last pull fades briefly as you let go. Higher lasts longer.", levelChoices},
        {Item::swingAssist, "SWING ASSIST",
         "Swinging at a wall, you slow down before it. Your line stays yours.", {}},
        {Item::webSlingshot,
         "WEB SLINGSHOT",
         "No auto jump. Draw two fixed webs, hold for vibration, release both to launch.",
         {}},
        {Item::slingshotSpeed, "SLINGSHOT SPEED",
         "A full draw's launch: under the swing speed limit, or at its own speed.", slingshotSpeedChoices},
        {Item::betterClimbing,
         "BETTER CLIMBING",
         "Take one web in both hands, then pull hand over hand to keep climbing it.",
         {}},
        {Item::twoHandZip, "TWO HAND WEB ZIP",
         "Both webs on one spot: pull both arms back fast to launch at it.", {}},
        {Item::ropeSteering, "HAND ROPE STEERING",
         "Hold your web with the free hand too and pull sideways to turn your swing.", {}},
        {Item::none, "COMFORT", nullptr, {}},
        {Item::snapTurn, "SNAP TURN", "How far a flick of the right stick turns you. Off: no snap turning.",
         snapChoices},
        {Item::smoothTurn, "SMOOTH TURN", "Turn steadily while you hold the right stick. Off: it snap turns.",
         smoothChoices},
        {Item::none, "HAPTICS", nullptr, {}},
        {Item::haptics, "CONTROLLER VIBRATION", "How strongly webs, grabs and punches buzz in your hands.",
         hapticChoices},
        {Item::blowSounds, "BLOW SOUNDS",
         "What your blows sound like: the game's own hits, Spidy's tones, or none.", blowSoundChoices},
        {Item::bodyHaptics, "BODY HAPTICS",
         "How strongly bHaptics vest, sleeves and visor play webs, hits and Spider-Sense.", hapticChoices},
        {Item::hapticWebs,
         "WEB HAPTICS",
         "Body haptics for webs: shots, holds, the swing's pull, zips and grabs.",
         {}},
        {Item::hapticWind,
         "WIND HAPTICS",
         "Body haptics for the wind when you fly or fall free of webs and walls.",
         {}},
        {Item::hapticCombat,
         "COMBAT HAPTICS",
         "Body haptics for punches, hits, Spider-Sense, dodges, heals and slow motion.",
         {}},
        {Item::hapticMovement,
         "MOVEMENT HAPTICS",
         "Body haptics for landings, walls, wall climbing and charged leaps.",
         {}},
        {Item::none, "DISPLAY", nullptr, {}},
        {Item::view, "VR VIEW", "Play in VR, or the game as it is on a 3D screen, with pad controls.",
         viewChoices, Place::page},
        {Item::screenSize, "GAME SCREEN SIZE", "The screen that shows menus, cutscenes and flat mode.",
         screenChoices},
        {Item::screenDepth, "GAME SCREEN 3D",
         "Cutscenes and VIEW: GAME SCREEN get depth. Higher brings them nearer. Off: flat.", depthChoices},
        {Item::hud, "HUD", "How large the game's HUD shows in front of you. Off: no HUD in VR.", hudChoices},
        {Item::uiStyle, "UI STYLE",
         "Spidy's own marks, gadget wheel and meters: comic print, or tech like the HUD.", uiStyleChoices},
        {Item::combatVisuals,
         "COMBAT VISUALS",
         "Off: no combat effects; free uppercuts/smashes. Spider-Sense stays on.",
         {}},
        {Item::nearClip, "NEAR CLIP",
         "How near your eyes things still show. Nearer keeps your hands in view.", nearClipChoices},
        {Item::recordingFov, "RECORDING FOV",
         "A view this wide on the monitor to record. Costs frame rate; no Spidy marks.",
         recordingFovChoices},
        {Item::recordingSmoothing, "RECORDING SMOOTHING",
         "Softens how the recording view turns, not the headset's. Higher is softer.",
         recordingSmoothingChoices},
        {Item::none, "BODY", nullptr, {}},
        {Item::calibrate, "CALIBRATE BODY",
         "Fit Spider-Man to you: pick ON RESUME, then hold both triggers in a T-pose.", calibrateChoices},
        {Item::height, "HEIGHT",
         "Your height, for Spider-Man's size and SEATED. AUTO measures it in the headset.", heightChoices},
        {Item::seated,
         "SEATED",
         "Play sitting: your view rises to your standing height. Recenter after sitting.",
         {}},
        {Item::none, "EXPERIMENTAL", nullptr, {}},
        {Item::flips,
         "FLIPS",
         "Right stick flips; up zips/aims. Release holds the angle. A returns upright.",
         {}},
        {Item::controlledFlips,
         "CONTROLLED FLIPS",
         "Air: A enables left-stick flips. Release holds. A again resets and exits.",
         {}},
        {Item::flipSpeed, "FLIP SPEED", "How fast a flip turns you over at full stick tilt.",
         flipSpeedChoices},
        {Item::handClimb,
         "HAND WALL CLIMBING",
         "Grip and pull. Reach back near the wall to turn. Left stick: run. A: jump.",
         {}},
        {Item::wallRunCameraTilt,
         "WALL RUN CAMERA TILT",
         "Gently bank the view with the body on horizontal momentum wall runs.",
         {}},
        {Item::eyeAim, "EYE AIM",
         "Web balls and zips go where your eyes look. Needs a headset with eye tracking.", eyeAimChoices},
        {Item::realisticFlips,
         "REALISTIC FLIPS",
         "Momentum rotation from arms and webs. FLIPS also enables left-stick steering.",
         {}},
        {Item::automaticStrength, "ROTATION STRENGTH",
         "How strongly the body follows the web direction. Heading stays free.", automaticStrengthChoices},
        {Item::automaticSpeed, "MAX ROTATION SPEED",
         "Maximum automatic turning speed, including release flips.", automaticSpeedChoices},
        {Item::automaticMinimumSpeed, "MINIMUM FLIGHT SPEED",
         "Start automatic rotation only above this travel speed.", automaticMinimumChoices, Place::launcher},
        {Item::automaticReleaseFlip,
         "SWING MOMENTUM",
         "Carry orbital momentum when the last web is released. No preset flip.",
         {}},
        {Item::automaticResponse, "ROTATION RESPONSE",
         "Longer response gives gentler acceleration and braking.", automaticResponseChoices,
         Place::launcher},
        {Item::automaticHandStrength, "HAND MOMENTUM",
         "How much arm movement changes your angular momentum in the air.", automaticHandStrengthChoices},
        {Item::automaticFalloff, "ANGULAR DRAG", "How quickly rotation slows without more hand or web input.",
         automaticFalloffChoices, Place::launcher},
        {Item::automaticHandThreshold, "MINIMUM HAND SPEED",
         "Ignore hand movements slower than this. Faster gestures add momentum.",
         automaticHandThresholdChoices, Place::launcher},
        {Item::automaticAirBrake,
         "AIR ROTATION BRAKE",
         "Press A in the air to stop momentum and smoothly return upright.",
         {}},
        {Item::automaticCombat,
         "FLIPS IN COMBAT",
         "Off: with enemies near, arms and webs stop turning you and you come upright.",
         {}},
    }};
    return all;
}
int choice(Item item, const Values& v) {
    if (const bool* on = switchOf(item, v))
        return *on ? 1 : 0;
    const auto s = steps(item);
    if (s.empty())
        return 0;
    const float value = numberOf(item, v);
    int best = 0;
    for (int i = 1; i < static_cast<int>(s.size()); ++i)
        if (std::abs(static_cast<float>(s[i]) - value) < std::abs(static_cast<float>(s[best]) - value))
            best = i;
    return best;
}
bool choose(Item item, int index, Values& v) {
    if (bool* on = switchOf(item, v)) {
        if (index != 0 && index != 1)
            return false;
        const bool was = *on;
        *on = index == 1;
        return *on != was;
    }
    const auto s = steps(item);
    if (index < 0 || index >= static_cast<int>(s.size()))
        return false;
    const Values before = v;
    switch (item) {
    case Item::swingSpeed:
        v.swingSpeed = static_cast<float>(s[index]);
        break;
    case Item::snapTurn:
        v.snapTurn = s[index];
        break;
    case Item::smoothTurn:
        v.smoothTurn = s[index];
        break;
    case Item::haptics:
        v.haptics = s[index];
        break;
    case Item::bodyHaptics:
        v.bodyHaptics = s[index];
        break;
    case Item::screenSize:
        v.screenSize = s[index];
        break;
    case Item::weight:
        v.weight = s[index];
        break;
    case Item::hud:
        v.hud = s[index];
        break;
    case Item::flipSpeed:
        v.flipSpeed = s[index];
        break;
    case Item::screenDepth:
        v.screenDepth = s[index];
        break;
    case Item::elasticWeb:
        v.elasticWeb = s[index];
        break;
    case Item::softRelease:
        v.softRelease = s[index];
        break;
    case Item::slingshotSpeed:
        v.slingshotSpeed = s[index];
        break;
    case Item::recordingSmoothing:
        v.recordingSmoothing = s[index];
        break;
    case Item::nearClip:
        v.nearClipMm = s[index];
        break;
    case Item::recordingFov:
        v.recordingFov = s[index];
        break;
    case Item::uiStyle:
        v.uiStyle = s[index];
        break;
    case Item::blowSounds:
        v.blowSounds = s[index];
        break;
    case Item::height:
        v.heightCm = s[index];
        break;
    case Item::eyeAim:
        v.eyeAim = s[index];
        break;
    case Item::automaticStrength:
        v.automaticStrength = s[index];
        break;
    case Item::automaticSpeed:
        v.automaticSpeed = s[index];
        break;
    case Item::automaticMinimumSpeed:
        v.automaticMinimumSpeed = s[index];
        break;
    case Item::automaticResponse:
        v.automaticResponse = s[index];
        break;
    case Item::automaticHandStrength:
        v.automaticHandStrength = s[index];
        break;
    case Item::automaticFalloff:
        v.automaticFalloff = s[index];
        break;
    case Item::automaticHandThreshold:
        v.automaticHandThreshold = s[index];
        break;
    default:
        return false;
    }
    return !(v == before);
}
int defaultChoice(Item item) {
    return choice(item, Values{});
}
namespace {
// The settings tabOnlyText writes: a switch (flag) or a number.
struct TabOnly {
    std::string_view key;
    bool Values::*flag;
    int Values::*number;
};
constexpr TabOnly tabOnly[] = {
    {"controlled_flips", &Values::controlledFlips, nullptr},
    {"near_clip_mm", nullptr, &Values::nearClipMm},
    {"hand_climb", &Values::handClimb, nullptr},
    {"wall_run_camera_tilt", &Values::wallRunCameraTilt, nullptr},
    {"realistic_flips", &Values::realisticFlips, nullptr},
    {"rotation_strength", nullptr, &Values::automaticStrength},
    {"rotation_speed", nullptr, &Values::automaticSpeed},
    {"flight_speed_min", nullptr, &Values::automaticMinimumSpeed},
    {"swing_momentum", &Values::automaticReleaseFlip, nullptr},
    {"rotation_response_ms", nullptr, &Values::automaticResponse},
    {"hand_momentum", nullptr, &Values::automaticHandStrength},
    {"angular_drag_ms", nullptr, &Values::automaticFalloff},
    {"hand_speed_min_cm", nullptr, &Values::automaticHandThreshold},
    {"air_rotation_brake", &Values::automaticAirBrake, nullptr},
    {"flips_in_combat", &Values::automaticCombat, nullptr},
    {"body_haptics_webs", &Values::hapticWebs, nullptr},
    {"body_haptics_wind", &Values::hapticWind, nullptr},
    {"body_haptics_combat", &Values::hapticCombat, nullptr},
    {"body_haptics_movement", &Values::hapticMovement, nullptr},
    {"recording_fov", nullptr, &Values::recordingFov},
    {"ui_style", nullptr, &Values::uiStyle},
    {"combat_visuals", &Values::combatVisuals, nullptr},
    {"blow_sounds", nullptr, &Values::blowSounds},
    {"web_slingshot", &Values::webSlingshot, nullptr},
    {"better_climbing", &Values::betterClimbing, nullptr},
    {"height_cm", nullptr, &Values::heightCm},
    {"seated", &Values::seated, nullptr},
    {"eye_aim", nullptr, &Values::eyeAim},
    {"smart_anchor", &Values::smartAnchor, nullptr},
    {"elastic_web", nullptr, &Values::elasticWeb},
    {"soft_release", nullptr, &Values::softRelease},
    {"swing_assist", &Values::swingAssist, nullptr},
    {"slingshot_speed", nullptr, &Values::slingshotSpeed},
    {"recording_smoothing", nullptr, &Values::recordingSmoothing},
    {"two_hand_zip", &Values::twoHandZip, nullptr},
    {"rope_steering", &Values::ropeSteering, nullptr},
};
} // namespace
std::string tabOnlyText(const Values& v) {
    std::string text;
    for (const auto& s : tabOnly) {
        const int value = s.flag ? (v.*s.flag ? 1 : 0) : v.*s.number;
        text.append(s.key).append("=").append(std::to_string(value)).append("\n");
    }
    return text;
}
Values withTabOnly(Values v, std::string_view text) {
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos)
            continue;
        const std::string_view key = line.substr(0, equals), value = line.substr(equals + 1);
        int number{};
        const auto [rest, error] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (error != std::errc{} || rest != value.data() + value.size())
            continue;
        for (const auto& s : tabOnly)
            if (key == s.key) {
                if (s.flag)
                    v.*s.flag = number != 0;
                else
                    v.*s.number = number;
            }
    }
    return sanitized(v);
}
} // namespace spidy::vr_settings
