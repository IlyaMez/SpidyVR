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
// WEB AIM: MODEL FOREARM (vr_settings::Values::webAimSource 1; off by
// default): the webs leave along the forearm of the hero the player sees, the
// elbow to the wrist the body's solve placed (native_body::Forearms), rather
// than along the controller. The forearm is kept as a turn in the controller's
// own frame (forearmTurn) and put on the controller's current pose, so the
// line moves with the controller at once and only the elbow's bend is a solve
// behind. It is smoothed (forearmSmoothSeconds), never more than
// forearmClampDegrees off the controller's line, and goes back to the
// controller's line over forearmFallbackSeconds whenever the sample is not to
// be trusted (ForearmAim::update with valid false): the body off or -NoBody,
// a solve that failed, a wrist forearmMiss or more from its controller, a
// sample no longer new, the game's camera, a flip's tilt. WEB ANGLE and WRIST
// SHOOTERS turn on top (shooterAim's four-argument form): the tilt about the
// forearm line's own side, the shooters a quarter turn about the forearm.
// Where a web leaves from does not change.
enum class Source : int { controller = 0, forearm = 1 };
constexpr float forearmClampDegrees = 35, forearmSmoothSeconds = .08f, forearmFallbackSeconds = .15f,
                forearmMiss = .1f;
// Why a hand's sample is not used (the log's reason).
enum class Fallback : int { none, off, untracked, notSolved, wristMiss, stale, gameCamera, tilted };
inline const char* fallbackName(Fallback f) {
    switch (f) {
    case Fallback::none:
        return "none";
    case Fallback::off:
        return "body off";
    case Fallback::untracked:
        return "untracked";
    case Fallback::notSolved:
        return "not solved";
    case Fallback::wristMiss:
        return "wrist miss";
    case Fallback::stale:
        return "stale";
    case Fallback::gameCamera:
        return "game camera";
    case Fallback::tilted:
        return "flip tilt";
    }
    return "?";
}
inline Quat unitOr(Quat q, Quat fallback = {}) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(n) || n < 1e-6f)
        return fallback;
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}
// From a toward b by t (0 a, 1 b), the short way.
inline Quat slerp(Quat a, Quat b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0)
        b = {-b.x, -b.y, -b.z, -b.w}, d = -d;
    if (d > .9995f)
        return unitOr(
            {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}, a);
    const float theta = std::acos(std::clamp(d, -1.f, 1.f)), s = std::sin(theta);
    const float wa = std::sin((1 - t) * theta) / s, wb = std::sin(t * theta) / s;
    return unitOr({a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb}, a);
}
// How far a unit turn turns, radians (0 to pi).
inline float turnAngle(Quat q) {
    return 2 * std::acos(std::clamp(std::abs(q.w), 0.f, 1.f));
}
// The turn, at most `maxRadians` of it.
inline Quat clampTurn(Quat q, float maxRadians) {
    q = unitOr(q);
    if (q.w < 0)
        q = {-q.x, -q.y, -q.z, -q.w};
    const float angle = turnAngle(q);
    return angle <= maxRadians || angle < 1e-6f ? q : slerp({}, q, maxRadians / angle);
}
// The forearm, elbow to wrist, in the frame of the controller grip the solve
// was given: zero when there is none.
inline Vec3 forearmInGrip(Vec3 elbow, Vec3 wrist, Quat gripAtSolve) {
    if (!finite(elbow) || !finite(wrist))
        return {};
    return normalized(unitOr(gripAtSolve).conjugate().rotate(wrist - elbow));
}
// The turn of the current aim pose (aimNow) that takes its line (-Z) onto
// that forearm, carried on the current grip (gripNow), clamped; the identity
// when there is no forearm.
inline Quat forearmTurn(Vec3 inGrip, Quat gripNow, Quat aimNow) {
    if (!finite(inGrip) || length(inGrip) < .5f)
        return {};
    const Vec3 world = unitOr(gripNow).rotate(inGrip);
    const Vec3 local = normalized(unitOr(aimNow).conjugate().rotate(world));
    if (!finite(local) || length(local) < .5f)
        return {};
    return clampTurn(arc({0, 0, -1}, local), forearmClampDegrees * 3.14159265f / 180);
}
// One hand's forearm turn, smoothed, and how much of it is used (weight: 0
// the controller's line, 1 the forearm's).
struct ForearmAim {
    Quat smoothed{};
    float weight{};
    bool primed{};
    void reset() {
        *this = {};
    }
    void update(bool valid, Quat target, float dt) {
        if (!std::isfinite(dt) || dt < 0)
            dt = 0;
        dt = std::min(dt, .1f);
        if (valid) {
            target = clampTurn(target, forearmClampDegrees * 3.14159265f / 180);
            smoothed = primed ? slerp(smoothed, target, 1 - std::exp(-dt / forearmSmoothSeconds)) : target;
            primed = true;
            weight = std::min(1.f, weight + dt / forearmFallbackSeconds);
        } else {
            weight = std::max(0.f, weight - dt / forearmFallbackSeconds);
            if (weight <= 0)
                primed = false, smoothed = {};
        }
    }
    // The turn to use, FOREARM BLEND percent of it (0 the controller's line).
    Quat turn(int blendPercent) const {
        const float share = weight * static_cast<float>(std::clamp(blendPercent, 0, 100)) / 100;
        return share <= 0 ? Quat{} : slerp({}, smoothed, share);
    }
};
// The aim pose a hand's webs take with a source turn (forearmTurn, through
// ForearmAim::turn) under WEB ANGLE and WRIST SHOOTERS: the same place.
inline Pose shooterAim(Pose aim, int hand, Shooters s, Quat source) {
    aim.orientation = aim.orientation * source * turn(hand, s);
    return aim;
}
} // namespace spidy::web_aim
