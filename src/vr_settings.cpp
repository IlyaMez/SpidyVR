#include "spidy/vr_settings.hpp"
#include <algorithm>
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
constexpr int automaticResponses[] = {100,200,350,500,750};
constexpr int automaticHandStrength[] = {25,50,75,100,150,200};
constexpr int automaticHandThresholds[] = {50,100,150,200,300};
constexpr const char* automaticHandThresholdChoices[] = {"0.5 M/S","1 M/S","1.5 M/S","2 M/S","3 M/S"};
constexpr int automaticFalloffs[] = {500,1000,1500,2000,3000};
constexpr const char* automaticResponseChoices[] = {"100 MS","200 MS","350 MS","500 MS","750 MS"};
constexpr const char* automaticHandStrengthChoices[] = {"25%","50%","75%","100%","150%","200%"};
constexpr const char* automaticFalloffChoices[] = {"FAST","QUICK","MEDIUM","SLOW","VERY SLOW"};
constexpr int automaticStrengths[] = {25,50,75,100};
constexpr int automaticSpeeds[] = {90,120,180,240,360};
constexpr int automaticMinimumSpeeds[] = {3,5,8,12,16};
constexpr const char* automaticStrengthChoices[] = {"25%","50%","75%","100%"};
constexpr const char* automaticSpeedChoices[] = {"90 DEG/S","120 DEG/S","180 DEG/S","240 DEG/S","360 DEG/S"};
constexpr const char* automaticMinimumChoices[] = {"3 M/S","5 M/S","8 M/S","12 M/S","16 M/S"};
constexpr int screenSizes[] = {0, 1, 2};
constexpr int hudSizes[] = {0, 1, 2, 3};
constexpr int depthSteps[] = {0, 1, 2, 3};
static_assert(std::size(swingChoices) == std::size(swingSpeeds) && std::size(snapChoices) == std::size(snapTurns) &&
              std::size(smoothChoices) == std::size(smoothTurns) &&
              std::size(hapticChoices) == std::size(hapticLevels) && std::size(screenChoices) == std::size(screenSizes) &&
              std::size(weightChoices) == std::size(weights) && std::size(hudChoices) == std::size(hudSizes) &&
              std::size(flipSpeedChoices) == std::size(flipSpeeds) &&
              std::size(depthChoices) == std::size(depthSteps) && std::size(depthSteps) == std::size(screenDepths));
// A list setting's steps, or none for a switch.
std::span<const int> steps(Item item) {
    switch (item) {
    case Item::automaticHandThreshold: return automaticHandThresholds;
    case Item::automaticResponse: return automaticResponses;
    case Item::automaticHandStrength: return automaticHandStrength;
    case Item::automaticFalloff: return automaticFalloffs;
    case Item::automaticStrength: return automaticStrengths;
    case Item::automaticSpeed: return automaticSpeeds;
    case Item::automaticMinimumSpeed: return automaticMinimumSpeeds;
    case Item::swingSpeed:
        return swingSpeeds;
    case Item::snapTurn:
        return snapTurns;
    case Item::smoothTurn:
        return smoothTurns;
    case Item::haptics:
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
    default:
        return {};
    }
}
float numberOf(Item item, const Values& v) {
    switch (item) {
    case Item::automaticHandThreshold: return static_cast<float>(v.automaticHandThreshold);
    case Item::automaticResponse: return static_cast<float>(v.automaticResponse);
    case Item::automaticHandStrength: return static_cast<float>(v.automaticHandStrength);
    case Item::automaticFalloff: return static_cast<float>(v.automaticFalloff);
    case Item::automaticStrength: return static_cast<float>(v.automaticStrength);
    case Item::automaticSpeed: return static_cast<float>(v.automaticSpeed);
    case Item::automaticMinimumSpeed: return static_cast<float>(v.automaticMinimumSpeed);
    case Item::swingSpeed:
        return v.swingSpeed;
    case Item::snapTurn:
        return static_cast<float>(v.snapTurn);
    case Item::smoothTurn:
        return static_cast<float>(v.smoothTurn);
    case Item::haptics:
        return static_cast<float>(v.haptics);
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
    default:
        return 0;
    }
}
// A switch setting's value (const for const values), or null for a list.
template <class V> auto switchOf(Item item, V& v) -> decltype(&v.aimMarkers) {
    switch (item) {
    case Item::automaticAirBrake: return &v.automaticAirBrake;
    case Item::realisticFlips: return &v.realisticFlips;
    case Item::automaticReleaseFlip: return &v.automaticReleaseFlip;
    case Item::aimMarkers:
        return &v.aimMarkers;
    case Item::airWebs:
        return &v.airWebs;
    case Item::calibrate:
        return &v.calibrate;
    case Item::flips:
        return &v.flips;
    case Item::webButton:
        return &v.triggerWebs;
    case Item::webStyle:
        return &v.arcadeWebs;
    case Item::standOnWalls:
        return &v.standOnWalls;
    case Item::view:
        return &v.flatScreen;
    default:
        return nullptr;
    }
}
} // namespace

Values sanitized(Values v) {
    v.automaticResponse = std::clamp(v.automaticResponse,100,750);
    v.automaticHandStrength = std::clamp(v.automaticHandStrength,25,200);
    v.automaticHandThreshold = std::clamp(v.automaticHandThreshold,50,300);
    v.automaticFalloff = std::clamp(v.automaticFalloff,500,3000);
    v.automaticStrength = std::clamp(v.automaticStrength,25,100);
    v.automaticSpeed = std::clamp(v.automaticSpeed,90,360);
    v.automaticMinimumSpeed = std::clamp(v.automaticMinimumSpeed,3,16);
    v.swingSpeed = std::isfinite(v.swingSpeed) ? std::clamp(v.swingSpeed, 1.f, 65.f) : 32.f;
    v.snapTurn = std::clamp(v.snapTurn, 0, 90);
    v.smoothTurn = std::clamp(v.smoothTurn, 0, 360);
    v.haptics = std::clamp(v.haptics, 0, 100);
    v.screenSize = std::clamp(v.screenSize, 0, 2);
    v.weight = std::clamp(v.weight, 40, 300);
    v.hud = std::clamp(v.hud, 0, 3);
    v.flipSpeed = std::clamp(v.flipSpeed, 90, 480);
    v.screenDepth = std::clamp(v.screenDepth, 0, 3);
    return v;
}
const std::array<Row, 32>& rows() {
    // The help fits the game's description column beside the rows.
    static const std::array<Row, 32> all{{
        {Item::realisticFlips, "REALISTIC FLIPS", "Momentum rotation from arms and webs. FLIPS also enables left-stick steering.", {}},
        {Item::automaticStrength, "ROTATION STRENGTH", "How strongly the body follows the web direction. Heading stays free.", automaticStrengthChoices},
        {Item::automaticSpeed, "MAX ROTATION SPEED", "Maximum automatic turning speed, including release flips.", automaticSpeedChoices},
        {Item::automaticMinimumSpeed, "MINIMUM FLIGHT SPEED", "Start automatic rotation only above this travel speed.", automaticMinimumChoices},
        {Item::automaticReleaseFlip, "SWING MOMENTUM", "Carry orbital momentum when the last web is released. No preset flip.", {}},
        {Item::automaticResponse, "ROTATION RESPONSE", "Longer response gives gentler acceleration and braking.", automaticResponseChoices},
        {Item::automaticHandStrength, "HAND MOMENTUM", "How much arm movement changes your angular momentum in the air.", automaticHandStrengthChoices},
        {Item::automaticFalloff, "ANGULAR DRAG", "How quickly rotation slows without more hand or web input.", automaticFalloffChoices},
        {Item::automaticHandThreshold, "MINIMUM HAND SPEED", "Ignore hand movements slower than this. Faster gestures add momentum.", automaticHandThresholdChoices},
        {Item::automaticAirBrake, "AIR ROTATION BRAKE", "Press A in the air to stop momentum and smoothly return upright.", {}},
        {Item::none, "WEBS", nullptr, {}},
        {Item::webStyle, "WEB STYLE",
         "Arcade: webs pass lamp posts, signs and cars, and swings stay above the street.", webStyleChoices},
        {Item::webButton, "WEB BUTTON",
         "Which button shoots and holds a web; the other reels in and shoots web balls.", webButtonChoices},
        {Item::aimMarkers, "AIM MARKERS", "Show where each web would land.", {}},
        {Item::airWebs, "WEBS HOLD IN OPEN AIR",
         "With nothing in reach, a web still holds in open air, 100 m out.", {}},
        {Item::none, "SWINGING", nullptr, {}},
        {Item::swingSpeed, "SWING SPEED LIMIT", "How fast a swing can carry you.", swingChoices},
        {Item::weight, "WEIGHT", "How heavy you are while swinging and after letting go. 100% is real gravity.",
         weightChoices},
        {Item::none, "COMFORT", nullptr, {}},
        {Item::snapTurn, "SNAP TURN", "How far a flick of the right stick turns you. Off: no snap turning.",
         snapChoices},
        {Item::smoothTurn, "SMOOTH TURN", "Turn steadily while you hold the right stick. Off: it snap turns.",
         smoothChoices},
        {Item::haptics, "CONTROLLER VIBRATION", "How strongly webs, grabs and punches buzz in your hands.",
         hapticChoices},
        {Item::none, "DISPLAY", nullptr, {}},
        {Item::view, "VR VIEW", "Play in VR, or the game as it is on a 3D screen, with pad controls.", viewChoices,
         true},
        {Item::screenSize, "GAME SCREEN SIZE", "The screen that shows menus, cutscenes and flat mode.",
         screenChoices},
        {Item::screenDepth, "GAME SCREEN 3D",
         "Cutscenes and VIEW: GAME SCREEN get depth. Higher brings them nearer. Off: flat.",
         depthChoices},
        {Item::hud, "HUD", "How large the game's HUD shows in front of you. Off: no HUD in VR.", hudChoices},
        {Item::none, "BODY", nullptr, {}},
        {Item::calibrate, "CALIBRATE BODY", "Fit Spider-Man to you: pick ON RESUME, then hold both triggers in a T-pose.",
         calibrateChoices},
        {Item::none, "EXPERIMENTAL", nullptr, {}},
        {Item::flips, "FLIPS", "Left-stick flips. With REALISTIC FLIPS: stick steering and A rotation brake.", {}},
        {Item::flipSpeed, "FLIP SPEED", "How fast a flip turns you over, at full tilt of the left stick.",
         flipSpeedChoices},
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
    case Item::automaticHandThreshold: v.automaticHandThreshold=s[index]; break;
    case Item::automaticResponse: v.automaticResponse=s[index]; break;
    case Item::automaticHandStrength: v.automaticHandStrength=s[index]; break;
    case Item::automaticFalloff: v.automaticFalloff=s[index]; break;
    case Item::automaticStrength: v.automaticStrength=s[index]; break;
    case Item::automaticSpeed: v.automaticSpeed=s[index]; break;
    case Item::automaticMinimumSpeed: v.automaticMinimumSpeed=s[index]; break;
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
    default:
        return false;
    }
    return !(v == before);
}
int defaultChoice(Item item) {
    return choice(item, Values{});
}
} // namespace spidy::vr_settings
