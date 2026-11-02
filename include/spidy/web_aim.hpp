#pragma once
#include "math.hpp"
#include <algorithm>
#include <cmath>

// WEB ANGLE and WRIST SHOOTERS (vr_settings::Values::webAngle, wristShooters):
// which way a hand's webs leave it. The controller's aim pose points its -Z
// along the controller, +Y up out of its top, +X to its right, for either
// hand. As before (WEB ANGLE 0, WRIST SHOOTERS off) a web goes along -Z.
//
// WRIST SHOOTERS turns the line a quarter turn onto the palm side, square to
// the hand and forearm, as the comics' shooters on the inside of the wrist
// fire: the palm of a controller held in the left hand faces its +X, in the
// right hand its -X. WEB ANGLE then tilts it up (positive, toward the top of
// the controller, the thumb) or down, degrees, either way.
//
// Only a web's line turns, never where it leaves from: the aim pose's position
// stays, so a web and its marker start at the hand as before. game_swing turns
// the hands its swing, grab, web balls and aim previews take; the fists, the
// walls and the vents keep the controller's own pose (game_punch's knuckles
// lie along the controller, not along the web).
namespace spidy::web_aim {
constexpr int maxDegrees = 45;
struct Shooters {
    int degrees{};
    bool wrist{};
    bool asBefore() const {
        return degrees == 0 && !wrist;
    }
};
// The turn of the aim pose that gives hand `hand` (0 left, 1 right) its webs'
// line; the identity as before.
inline Quat turn(int hand, Shooters s) {
    if (s.asBefore())
        return {};
    const float tilt = static_cast<float>(std::clamp(s.degrees, -maxDegrees, maxDegrees)) * 3.14159265f / 180;
    // Rx(tilt) lifts -Z toward +Y; Ry(-90 degrees) then takes -Z onto +X (the
    // left palm), Ry(+90) onto -X (the right).
    const Quat lift = Quat::around({1, 0, 0}, tilt);
    if (!s.wrist)
        return lift;
    return Quat::around({0, 1, 0}, hand == 0 ? -1.5707963f : 1.5707963f) * lift;
}
// The aim pose a hand's webs take: the same place, its line turned.
inline Pose shooterAim(Pose aim, int hand, Shooters s) {
    if (s.asBefore())
        return aim;
    aim.orientation = aim.orientation * turn(hand, s);
    return aim;
}
} // namespace spidy::web_aim
