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
constexpr int nearClips[] = {10, 15, 20, 30, 50, 75, 100};
constexpr const char* nearClipChoices[] = {"1 CM", "1.5 CM", "2 CM", "3 CM", "5 CM", "7.5 CM", "10 CM"};
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
    case Item::nearClip:
        return nearClips;
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
    case Item::nearClip:
        return static_cast<float>(v.nearClipMm);
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
    v.nearClipMm = std::clamp(v.nearClipMm, 10, 100);
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
const std::array<Row, 23>& rows() {
    // The help fits the game's description column beside the rows.
    static const std::array<Row, 23> all{{
        {Item::nearClip, "NEAR CLIP",
         "Closest distance before geometry is hidden. Lower keeps nearby arms visible.", nearClipChoices},
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
        {Item::flips, "FLIPS", "In the air the left stick turns you over; tap A for one flip, hold A to stay.", {}},
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
    case Item::nearClip:
        v.nearClipMm = s[index];
        break;
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
