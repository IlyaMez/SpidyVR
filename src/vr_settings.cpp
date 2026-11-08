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
// WEB RANGE, m, and WEB SPEED (off, then slow to fast).
constexpr int webRanges[] = {10, 25, 50, 100, 150, 200, 300, 500, 1000};
constexpr const char* webRangeChoices[] = {"10 M",  "25 M",  "50 M",  "100 M", "150 M",
                                           "200 M", "300 M", "500 M", "1000 M"};
static_assert(std::size(webRangeChoices) == std::size(webRanges));
constexpr int webSpeeds[] = {0, 1, 2, 3};
constexpr const char* webSpeedChoices[] = {"OFF", "SLOW", "MEDIUM", "FAST"};
static_assert(std::size(webSpeedChoices) == std::size(webSpeeds));
constexpr int singleHandYankStrengths[] = {50, 75, 100, 125, 150, 200, 250, 300};
constexpr const char* singleHandYankStrengthChoices[] = {"50%",  "75%",  "100%", "125%",
                                                         "150%", "200%", "250%", "300%"};
static_assert(std::size(singleHandYankStrengthChoices) == std::size(singleHandYankStrengths));
constexpr int webAngles[] = {-45, -30, -15, 0, 15, 30, 45};
constexpr const char* webAngleChoices[] = {"DOWN 45", "DOWN 30", "DOWN 15", "0", "UP 15", "UP 30", "UP 45"};
static_assert(std::size(webAngleChoices) == std::size(webAngles));
constexpr int dodgeTumbles[] = {0, 1, 2};
constexpr const char* dodgeTumbleChoices[] = {"OFF", "LEAN", "ROLL"};
static_assert(std::size(dodgeTumbleChoices) == std::size(dodgeTumbles));
constexpr int nativeRunPercents[] = {50, 60, 70, 80, 90};
constexpr const char* nativeRunPercentChoices[] = {"50%", "60%", "70%", "80%", "90%"};
static_assert(std::size(nativeRunPercentChoices) == std::size(nativeRunPercents));
constexpr int earlyJumps[] = {0, 1, 2};
constexpr const char* earlyJumpChoices[] = {"OFF", "QUICK TAP", "RUNNING"};
static_assert(std::size(earlyJumpChoices) == std::size(earlyJumps));
constexpr int webAimSources[] = {0, 1};
constexpr const char* webAimSourceChoices[] = {"CONTROLLER", "MODEL FOREARM"};
static_assert(std::size(webAimSourceChoices) == std::size(webAimSources));
constexpr int forearmAimLimits[] = {35, 60, 90, 180};
constexpr const char* forearmAimLimitChoices[] = {"35 DEG", "60 DEG", "90 DEG", "NO LIMIT"};
static_assert(std::size(forearmAimLimitChoices) == std::size(forearmAimLimits));
constexpr int webLandingImpactSteps[] = {12, 16, 20, 24, 28, 32, 40};
constexpr const char* webLandingImpactChoices[] = {"12 M/S", "16 M/S", "20 M/S", "24 M/S",
                                                   "28 M/S", "32 M/S", "40 M/S"};
static_assert(std::size(webLandingImpactChoices) == std::size(webLandingImpactSteps));
constexpr int forearmBlends[] = {0, 25, 50, 75, 100};
constexpr const char* forearmBlendChoices[] = {"0%", "25%", "50%", "75%", "100%"};
static_assert(std::size(forearmBlendChoices) == std::size(forearmBlends));
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
    case Item::webRange:
        return webRanges;
    case Item::webSpeed:
        return webSpeeds;
    case Item::singleHandYankStrength:
        return singleHandYankStrengths;
    case Item::webAngle:
        return webAngles;
    case Item::dodgeTumble:
        return dodgeTumbles;
    case Item::earlyJump:
        return earlyJumps;
    case Item::webAimSource:
        return webAimSources;
    case Item::forearmBlend:
        return forearmBlends;
    case Item::forearmAimLimit:
        return forearmAimLimits;
    case Item::webLandingImpact:
        return webLandingImpactSteps;
    case Item::nativeRunAt:
        return nativeRunPercents;
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
    case Item::webRange:
        return static_cast<float>(v.webRange);
    case Item::webSpeed:
        return static_cast<float>(v.webSpeed);
    case Item::singleHandYankStrength:
        return static_cast<float>(v.singleHandYankPercent);
    case Item::webAngle:
        return static_cast<float>(v.webAngle);
    case Item::dodgeTumble:
        return static_cast<float>(v.dodgeTumble);
    case Item::earlyJump:
        return static_cast<float>(v.earlyJump);
    case Item::webAimSource:
        return static_cast<float>(v.webAimSource);
    case Item::forearmBlend:
        return static_cast<float>(v.forearmBlend);
    case Item::forearmAimLimit:
        return static_cast<float>(v.forearmAimLimit);
    case Item::webLandingImpact:
        return static_cast<float>(v.webLandingImpact);
    case Item::nativeRunAt:
        return static_cast<float>(v.nativeRunPercent);
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
    case Item::joystickRunCamera:
        return &v.joystickRunCamera;
    case Item::halfPunchDamage:
        return &v.halfPunchDamage;
    case Item::webTie:
        return &v.webTie;
    case Item::webLines:
        return &v.webLines;
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
    case Item::reelHaptics:
        return &v.reelHaptics;
    case Item::swingHaptics:
        return &v.swingHaptics;
    case Item::pcMedia:
        return &v.pcMedia;
    case Item::nativeRun:
        return &v.nativeRun;
    case Item::airSlingshot:
        return &v.airSlingshot;
    case Item::wristShooters:
        return &v.wristShooters;
    case Item::spidyRun:
        return &v.spidyRun;
    case Item::spidyRunFromStanding:
        return &v.spidyRunFromStanding;
    case Item::steadyStates:
        return &v.steadyStates;
    case Item::combatResponsive:
        return &v.combatResponsive;
    case Item::spidyRunJump:
        return &v.spidyRunJump;
    case Item::palmShooters:
        return &v.palmShooters;
    case Item::webLandingSmooth:
        return &v.webLandingSmooth;
    case Item::softLanding:
        return &v.softLanding;
    case Item::momentumLeap:
        return &v.momentumLeap;
    case Item::runLogs:
        return &v.runLogs;
    case Item::runCurve:
        return &v.runCurve;
    case Item::seated:
        return &v.seated;
    case Item::flipReprojection:
        return &v.flipReprojection;
    case Item::steadyHud:
        return &v.steadyHud;
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
    v.webRange = std::clamp(v.webRange, 10, 1000);
    v.webSpeed = std::clamp(v.webSpeed, 0, 3);
    v.singleHandYankPercent = std::clamp(v.singleHandYankPercent, 50, 300);
    v.webAngle = std::clamp(v.webAngle, -45, 45);
    v.dodgeTumble = std::clamp(v.dodgeTumble, 0, 2);
    v.earlyJump = std::clamp(v.earlyJump, 0, 2);
    v.webAimSource = std::clamp(v.webAimSource, 0, 1);
    v.forearmBlend = std::clamp(v.forearmBlend, 0, 100);
    v.forearmAimLimit = std::clamp(v.forearmAimLimit, 35, 180);
    v.webLandingImpact = std::clamp(v.webLandingImpact, 12, 40);
    v.nativeRunPercent = std::clamp(v.nativeRunPercent, 50, 90);
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
const std::array<Row, 94>& rows() {
    // The help fits the game's description column beside the rows.
    static const std::array<Row, 94> all{{
        {Item::none, "WEBS", nullptr, {}},
        {Item::webStyle, "WEB STYLE",
         "Arcade: webs pass lamp posts, signs and cars, and swings stay above the street.", webStyleChoices},
        {Item::webButton, "WEB BUTTON",
         "Which button shoots and holds a web; the other reels in and shoots web balls.", webButtonChoices},
        {Item::aimMarkers, "AIM MARKERS", "Show where each web would land.", {}},
        {Item::airWebs,
         "WEBS HOLD IN OPEN AIR",
         "With nothing in reach, a web still holds in open air, as far out as WEB RANGE.",
         {}},
        {Item::smartAnchor,
         "SMART ANCHOR",
         "With open-air webs off, a near miss catches a real surface in a small cone.",
         {}},
        {Item::webRange, "WEB RANGE", "How far a web reaches; an open-air web holds this far out.",
         webRangeChoices},
        {Item::webSpeed, "WEB SPEED", "Webs fly to where they stick, then catch you softly. Off: at once.",
         webSpeedChoices},
        {Item::none, "SWINGING", nullptr, {}},
        {Item::swingSpeed, "SWING SPEED LIMIT", "How fast a swing can carry you.", swingChoices},
        {Item::weight, "WEIGHT",
         "How heavy you are while swinging and after letting go. 100% is real gravity.", weightChoices},
        {Item::elasticWeb, "ELASTIC WEB", "Webs stretch under load for a softer catch. Higher is softer.",
         levelChoices},
        {Item::softRelease, "SOFT RELEASE",
         "A web's last pull fades briefly as you let go. Higher lasts longer.", levelChoices},
        {Item::swingAssist,
         "SWING ASSIST",
         "Swinging at a wall, you slow down before it. Your line stays yours.",
         {}},
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
        {Item::singleHandYankStrength, "SINGLE HAND YANK STRENGTH",
         "How much speed a fast one-hand yank adds. 100% as before; two-hand zips stay.",
         singleHandYankStrengthChoices},
        {Item::twoHandZip,
         "TWO HAND WEB ZIP",
         "Both webs on one spot: pull both arms back fast to launch at it.",
         {}},
        {Item::ropeSteering,
         "HAND ROPE STEERING",
         "Hold your web with the free hand too and pull sideways to turn your swing.",
         {}},
        {Item::none, "COMFORT", nullptr, {}},
        {Item::snapTurn, "SNAP TURN", "How far the right stick snaps. Used only when SMOOTH TURN is OFF.",
         snapChoices},
        {Item::smoothTurn, "SMOOTH TURN", "Turn steadily while you hold the right stick. Off: it snap turns.",
         smoothChoices},
        {Item::none, "HAPTICS", nullptr, {}},
        {Item::haptics, "CONTROLLER VIBRATION", "How strongly webs, grabs and punches buzz in your hands.",
         hapticChoices},
        {Item::reelHaptics, "REEL HAPTICS", "Reeling a web in ticks in that hand like a winch.", {}},
        {Item::swingHaptics,
         "SWING HAPTICS",
         "Swinging on a taut web ticks softly in that hand, faster as you speed up.",
         {}},
        {Item::blowSounds, "BLOW SOUNDS",
         "What your blows sound like: the game's own hits, Spidy's tones, or none.", blowSoundChoices},
        {Item::pcMedia,
         "PC MEDIA",
         "The phone plays what the PC plays (Spotify, a browser). Off: the Music folder.",
         {}},
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
         "A view this wide on the monitor to record. Costs frame rate; no Spidy marks.", recordingFovChoices},
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
         "Basic stick flips; with REALISTIC FLIPS, adds left-stick steering instead.",
         {}},
        {Item::controlledFlips,
         "CONTROLLED FLIPS",
         "A toggles left-stick flips. Overrides FLIPS; REALISTIC FLIPS takes priority.",
         {}},
        {Item::flipSpeed, "FLIP SPEED", "How fast a flip turns you over at full stick tilt.",
         flipSpeedChoices},
        {Item::handClimb,
         "HAND WALL CLIMBING",
         "Grip and pull. Reach back near the wall to turn. Left stick: run. A: jump.",
         {}},
        {Item::joystickRunCamera,
         "JOYSTICK WALL RUN CAMERA",
         "On: old wall-floor view. Off: joystick drives the existing momentum wall run.",
         {}},
        {Item::wallRunCameraTilt,
         "WALL RUN CAMERA TILT",
         "Gently bank the view with the body on horizontal momentum wall runs.",
         {}},
        {Item::halfPunchDamage,
         "HALF PUNCH DAMAGE",
         "Punches and web strikes deal 50% damage. Launches and smash force stay the same.",
         {}},
        {Item::webTie,
         "WEB TIE",
         "Web an enemy up, web a wall: hands together, click the right stick. He sticks.",
         {}},
        {Item::webLines,
         "WEB LINES",
         "Webs on two walls, hands together, click right stick: a line. Hold it to zip.",
         {}},
        {Item::eyeAim, "EYE AIM",
         "Web balls and zips go where your eyes look. Needs a headset with eye tracking.", eyeAimChoices},
        {Item::flipReprojection,
         "FLIP REPROJECTION",
         "Flips, spins and smooth turns shown as steadily as head turns.",
         {}},
        {Item::steadyHud,
         "STEADY HUD",
         "The HUD shown by the headset itself: it holds still when the game falls behind.",
         {}},
        {Item::nativeRun,
         "NATIVE RUN",
         "Ground sprint, vaults and wall runs. The threshold is set by NATIVE RUN AT.",
         {}},
        {Item::airSlingshot,
         "AIR SLINGSHOT",
         "In the air: two taut webs, draw both arms until the buzz, let go to launch.",
         {}},
        {Item::webAngle, "WEB ANGLE", "Tilts the webs up or down from the controller's line. 0 as before.",
         webAngleChoices},
        {Item::wristShooters,
         "WRIST SHOOTERS",
         "Full forearm aim. WEB AIM: MODEL FOREARM already does this, with blend control.",
         {}},
        {Item::dodgeTumble, "DODGE TUMBLE",
         "The view leans or rolls with the game's dodge, so you see it was one.", dodgeTumbleChoices},
        {Item::runCurve,
         "RUN CURVE",
         "Ground only: full run at 80% of the stick, faster sooner. Off as before.",
         {}},
        {Item::nativeRunAt, "NATIVE RUN AT", "The stick's tilt past which NATIVE RUN sprints. 70% as before.",
         nativeRunPercentChoices},
        {Item::runLogs,
         "RUN LOGS",
         "Run, jump and frame logs, in the console and a -diag.log file, to send back.",
         {}},
        {Item::momentumLeap,
         "RUNNING LEAP",
         "Charged leaps keep run speed. EARLY JUMP: RUNNING skips charging in a sprint.",
         {}},
        {Item::earlyJump, "EARLY JUMP",
         "A jumps sooner: a quicker tap, or at once while running. OFF as before.", earlyJumpChoices},
        {Item::softLanding,
         "SOFT LANDING",
         "A fall with no web eases to 9 m/s down just before the ground. Off as before.",
         {}},
        {Item::spidyRun,
         "SPIDY RUN",
         "Land fast and keep running at that speed, steering, until it eases off.",
         {}},
        {Item::spidyRunFromStanding,
         "RUN FROM STANDING",
         "With SPIDY RUN: hold the stick fully to start it from a stand. Off as before.",
         {}},
        {Item::webAimSource, "WEB AIM",
         "MODEL FOREARM: webs follow the body's forearm. CONTROLLER as before.", webAimSourceChoices},
        {Item::forearmBlend, "FOREARM BLEND",
         "With WEB AIM: MODEL FOREARM, blend from the controller (0%) to forearm (100%).", forearmBlendChoices},
        {Item::webLandingSmooth,
         "SMOOTH WEB LANDING",
         "Holding a web, a landing under WEB LANDING IMPACT is soft and keeps its speed.",
         {}},
        {Item::webLandingImpact, "WEB LANDING IMPACT",
         "SMOOTH WEB LANDING: at or over this speed a landing is the game's heavy one.",
         webLandingImpactChoices},
        {Item::steadyStates,
         "STEADY STATES",
         "NATIVE RUN's RT waits out flickers and stays off in fights. Off as before.",
         {}},
        {Item::combatResponsive,
         "COMBAT RESPONSIVE",
         "In a fight: no SPIDY RUN or carry, no RT, A jumps at once. Off as before.",
         {}},
        {Item::spidyRunJump,
         "SPIDY RUN JUMP",
         "A in SPIDY RUN or the landing carry jumps at once, keeping the speed.",
         {}},
        {Item::forearmAimLimit, "FOREARM AIM LIMIT",
         "How far MODEL FOREARM and WRIST SHOOTERS turn off the controller's line.", forearmAimLimitChoices},
        {Item::palmShooters,
         "PALM SHOOTERS",
         "Adds a palm-side quarter turn to your chosen web aim direction.",
         {}},
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
const Row* rowFor(Item item) {
    if (item == Item::none)
        return nullptr;
    for (const auto& row : rows())
        if (row.item == item)
            return &row;
    return nullptr;
}
const std::array<Screen, categoryCount>& screens() {
    static constexpr Item setup[] = {
        Item::smoothTurn, Item::snapTurn, Item::calibrate, Item::height,
        Item::seated,
    };
    static constexpr Item webs[] = {
        Item::webStyle, Item::webButton, Item::aimMarkers, Item::airWebs,
        Item::smartAnchor, Item::webRange, Item::webSpeed, Item::eyeAim,
        Item::webAimSource, Item::forearmBlend, Item::forearmAimLimit, Item::wristShooters,
        Item::palmShooters, Item::webAngle, Item::webTie, Item::webLines,
    };
    static constexpr Item swinging[] = {
        Item::swingSpeed, Item::weight, Item::elasticWeb, Item::softRelease,
        Item::swingAssist, Item::webSlingshot, Item::airSlingshot, Item::slingshotSpeed,
        Item::betterClimbing, Item::singleHandYankStrength, Item::twoHandZip, Item::ropeSteering,
    };
    static constexpr Item movement[] = {
        Item::nativeRun, Item::nativeRunAt, Item::runCurve, Item::steadyStates,
        Item::spidyRun, Item::spidyRunFromStanding, Item::spidyRunJump, Item::momentumLeap,
        Item::earlyJump, Item::softLanding, Item::webLandingSmooth, Item::webLandingImpact,
        Item::handClimb, Item::joystickRunCamera, Item::wallRunCameraTilt, Item::combatResponsive,
        Item::combatVisuals, Item::halfPunchDamage, Item::runLogs,
    };
    static constexpr Item flips[] = {
        Item::realisticFlips, Item::flips, Item::controlledFlips, Item::flipSpeed,
        Item::automaticStrength, Item::automaticSpeed, Item::automaticMinimumSpeed, Item::automaticReleaseFlip,
        Item::automaticResponse, Item::automaticHandStrength, Item::automaticFalloff, Item::automaticHandThreshold,
        Item::automaticAirBrake, Item::automaticCombat, Item::dodgeTumble, Item::flipReprojection,
    };
    static constexpr Item feedback[] = {
        Item::haptics, Item::reelHaptics, Item::swingHaptics, Item::bodyHaptics,
        Item::hapticWebs, Item::hapticWind, Item::hapticCombat, Item::hapticMovement,
        Item::blowSounds, Item::pcMedia,
    };
    static constexpr Item display[] = {
        Item::screenSize, Item::screenDepth, Item::hud, Item::steadyHud,
        Item::uiStyle, Item::nearClip, Item::recordingFov, Item::recordingSmoothing,
    };
    static const std::array<Screen, categoryCount> all{{
        {"Setup", "VR SETUP", setup},
        {"Webs", "VR WEBS", webs},
        {"Swinging", "VR SWINGING", swinging},
        {"Movement", "VR MOVEMENT", movement},
        {"Flips", "VR FLIPS", flips},
        {"Haptics", "VR HAPTICS", feedback},
        {"Display", "VR DISPLAY", display},
    }};
    return all;
}
const char* inactiveReason(Item item, const Values& v) {
    switch (item) {
    case Item::snapTurn:
        return v.smoothTurn > 0 ? "Turn Smooth turn off to use Snap turn." : nullptr;
    case Item::smartAnchor:
        return v.airWebs ? "Turn Webs hold in open air off to use Smart anchor." : nullptr;
    case Item::wristShooters:
        return v.webAimSource == 1 ? "Web aim already follows the model forearm." : nullptr;
    case Item::forearmBlend:
        return v.webAimSource != 1 ? "Set Web aim to Model forearm to adjust the blend." : nullptr;
    case Item::forearmAimLimit:
        return v.webAimSource != 1 && !v.wristShooters ? "Enable Model forearm or Wrist shooters first." : nullptr;
    case Item::slingshotSpeed:
        return !v.webSlingshot && !v.airSlingshot ? "Enable Web slingshot or Air slingshot first." : nullptr;
    case Item::nativeRunAt:
    case Item::steadyStates:
        return !v.nativeRun ? "Enable Native run first." : nullptr;
    case Item::spidyRunFromStanding:
        return !v.spidyRun ? "Enable Spidy run first." : nullptr;
    case Item::spidyRunJump:
        return !v.spidyRun && !v.webLandingSmooth ? "Enable Spidy run or Smooth web landing first." : nullptr;
    case Item::webLandingImpact:
        return !v.webLandingSmooth ? "Enable Smooth web landing first." : nullptr;
    case Item::controlledFlips:
        return v.realisticFlips ? "Realistic flips takes priority over Controlled flips." : nullptr;
    case Item::flips:
        return v.controlledFlips && !v.realisticFlips ? "Controlled flips takes priority over basic Flips." : nullptr;
    case Item::flipSpeed:
        return !v.flips && (!v.controlledFlips || v.realisticFlips) ? "Enable stick flips to use Flip speed." : nullptr;
    case Item::automaticStrength:
    case Item::automaticSpeed:
    case Item::automaticMinimumSpeed:
    case Item::automaticReleaseFlip:
    case Item::automaticResponse:
    case Item::automaticHandStrength:
    case Item::automaticFalloff:
    case Item::automaticHandThreshold:
    case Item::automaticAirBrake:
    case Item::automaticCombat:
        return !v.realisticFlips ? "Enable Realistic flips first." : nullptr;
    case Item::reelHaptics:
    case Item::swingHaptics:
        return v.haptics == 0 ? "Raise Controller vibration above zero first." : nullptr;
    case Item::hapticWebs:
    case Item::hapticWind:
    case Item::hapticCombat:
    case Item::hapticMovement:
        return v.bodyHaptics == 0 ? "Raise Body haptics above zero first." : nullptr;
    case Item::steadyHud:
        return v.hud == 0 ? "Enable the HUD first." : nullptr;
    case Item::recordingSmoothing:
        return v.recordingFov == 0 ? "Enable Recording FOV first." : nullptr;
    default:
        return nullptr;
    }
}
Values resetCategory(Category category, Values v) {
    const auto index = static_cast<size_t>(category);
    if (index >= categoryCount)
        return v;
    for (Item item : screens()[index].items)
        if (const Row* row = rowFor(item); row && row->place == Place::tab)
            choose(item, defaultChoice(item), v);
    return v;
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
    case Item::webRange:
        v.webRange = s[index];
        break;
    case Item::webSpeed:
        v.webSpeed = s[index];
        break;
    case Item::singleHandYankStrength:
        v.singleHandYankPercent = s[index];
        break;
    case Item::webAngle:
        v.webAngle = s[index];
        break;
    case Item::dodgeTumble:
        v.dodgeTumble = s[index];
        break;
    case Item::earlyJump:
        v.earlyJump = s[index];
        break;
    case Item::webAimSource:
        v.webAimSource = s[index];
        break;
    case Item::forearmBlend:
        v.forearmBlend = s[index];
        break;
    case Item::forearmAimLimit:
        v.forearmAimLimit = s[index];
        break;
    case Item::webLandingImpact:
        v.webLandingImpact = s[index];
        break;
    case Item::nativeRunAt:
        v.nativeRunPercent = s[index];
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
    {"joystick_wall_run_camera", &Values::joystickRunCamera, nullptr},
    {"wall_run_camera_tilt", &Values::wallRunCameraTilt, nullptr},
    {"half_punch_damage", &Values::halfPunchDamage, nullptr},
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
    {"web_range_m", nullptr, &Values::webRange},
    {"web_speed", nullptr, &Values::webSpeed},
    {"flip_reprojection", &Values::flipReprojection, nullptr},
    {"steady_hud", &Values::steadyHud, nullptr},
    {"reel_haptics", &Values::reelHaptics, nullptr},
    {"swing_haptics", &Values::swingHaptics, nullptr},
    {"pc_media", &Values::pcMedia, nullptr},
    {"single_hand_yank_percent", nullptr, &Values::singleHandYankPercent},
    {"web_tie", &Values::webTie, nullptr},
    {"native_run", &Values::nativeRun, nullptr},
    {"air_slingshot", &Values::airSlingshot, nullptr},
    {"web_angle_degrees", nullptr, &Values::webAngle},
    {"wrist_shooters", &Values::wristShooters, nullptr},
    {"dodge_tumble", nullptr, &Values::dodgeTumble},
    {"run_curve", &Values::runCurve, nullptr},
    {"native_run_at_percent", nullptr, &Values::nativeRunPercent},
    {"run_logs", &Values::runLogs, nullptr},
    {"running_leap", &Values::momentumLeap, nullptr},
    {"early_jump", nullptr, &Values::earlyJump},
    {"soft_landing", &Values::softLanding, nullptr},
    {"spidy_run", &Values::spidyRun, nullptr},
    {"spidy_run_from_standing", &Values::spidyRunFromStanding, nullptr},
    {"web_aim_source", nullptr, &Values::webAimSource},
    {"forearm_blend", nullptr, &Values::forearmBlend},
    {"web_landing_smooth", &Values::webLandingSmooth, nullptr},
    {"web_landing_impact_speed", nullptr, &Values::webLandingImpact},
    {"steady_states", &Values::steadyStates, nullptr},
    {"combat_responsive", &Values::combatResponsive, nullptr},
    {"spidy_run_jump", &Values::spidyRunJump, nullptr},
    {"forearm_aim_limit", nullptr, &Values::forearmAimLimit},
    {"palm_shooters", &Values::palmShooters, nullptr},
    {"web_lines", &Values::webLines, nullptr},
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
