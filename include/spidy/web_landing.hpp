// SMOOTH WEB LANDING (vr_settings::Values::webLandingSmooth, off by default;
// Swing::allowWebLandingSmooth): landing while a web is held. Before it, such a
// landing went to the game in the step the feet touched: the swing let go at
// once (game_swing.cpp's `drive` needs the body off the ground), the game's
// mover took the body at the fall speed the swing had flown it at (its air
// handler reads that speed from the swing's own displacement), with its own
// ground speed (0 to a jog) and its hard-land lock, and the swing's ground
// prediction braked what was left at exp(-5 t). A contact that flickered for
// a step handed the body to the game and back. That is the "janky and sticky"
// landing. With the setting on, below `impact` (m/s, the tab's WEB LANDING
// IMPACT) a landing with a web held:
//  - eases its fall to fallTarget over the last `height` metres (or `seconds`
//    of the fall), as SOFT LANDING does for a fall with no web, so the game's
//    landing is a light one;
//  - carries its speed across the ground (carryStep: it decays toward `floor`
//    with time constant `tau`, for carryMax seconds at most) with the swing
//    still driving, then hands the body to the game at about a jog;
//  - takes the ground or the air only once the game's contact has said so for
//    contactGround / contactAir seconds (Contact), so a one-step flicker no
//    longer flips the body's owner.
// At `impact` or faster nothing of this happens: the landing is the game's,
// heavy as before.
#pragma once
#include "math.hpp"
#include <algorithm>
#include <cmath>

namespace spidy::web_landing {
struct Tuning {
    float fallTarget = 4, height = 3, seconds = .35f, rate = 10;
    float floor = 5, tau = .3f, carryMax = .6f;
    float contactGround = .05f, contactAir = .08f;
};
constexpr Tuning tuning{};
// The tab's WEB LANDING IMPACT, m/s: 24 by default.
constexpr int impactDefault = 24;
// Below the impact speed: the landing is the smooth one.
inline bool gentle(Vec3 velocity, float impact) {
    return finite(velocity) && std::isfinite(impact) && length(velocity) < impact;
}
// The fall's speed (vy, up positive) at feetAbove metres over the ground (under
// 0: none found), one step of dt later.
inline float easeFall(float vy, float feetAbove, float dt, const Tuning& k = tuning) {
    if (!(feetAbove >= 0) || !std::isfinite(vy) || !(vy < -k.fallTarget) || !std::isfinite(dt) || !(dt > 0))
        return vy;
    if (feetAbove > k.height && feetAbove / -vy > k.seconds)
        return vy;
    return vy + (-k.fallTarget - vy) * std::min(1.f, k.rate * dt);
}
// How far below the body's centre to look for that ground.
inline float reach(float falling, float seconds, const Tuning& k = tuning) {
    return 1 + std::max(k.height, falling * k.seconds) + falling * std::max(seconds, 0.f) + .5f;
}
// One step of the carry across the ground: over `floor` the speed decays
// toward it, its way kept; at or under it nothing changes.
inline Vec3 carryStep(Vec3 along, float dt, const Tuning& k = tuning) {
    along.y = 0;
    if (!finite(along) || !std::isfinite(dt) || !(dt > 0))
        return finite(along) ? along : Vec3{};
    const float speed = length(along);
    if (speed <= k.floor)
        return along;
    const float next = k.floor + (speed - k.floor) * std::exp(-dt / k.tau);
    return along * (next / speed);
}
// The game's ground contact, steadied: while `active`, a change counts only
// once it has lasted contactGround (onto the ground) or contactAir (off it);
// inactive, it is the contact as it is. `flickers` counts the changes that
// did not last.
class Contact {
  public:
    bool update(float dt, bool contact, bool active, const Tuning& k = tuning) {
        dt = std::isfinite(dt) ? std::clamp(dt, 0.f, .1f) : 0.f;
        if (!active || !primed_) {
            if (pending_ > 0 && contact == steady_)
                ++flickers_;
            primed_ = true;
            steady_ = contact;
            pending_ = 0;
            return steady_;
        }
        if (contact == steady_) {
            if (pending_ > 0)
                ++flickers_;
            pending_ = 0;
            return steady_;
        }
        pending_ += dt;
        if (pending_ >= (contact ? k.contactGround : k.contactAir) - 1e-6f) {
            steady_ = contact;
            pending_ = 0;
        }
        return steady_;
    }
    bool steady() const {
        return steady_;
    }
    unsigned flickers() const {
        return flickers_;
    }

  private:
    bool primed_{}, steady_{};
    float pending_{};
    unsigned flickers_{};
};
} // namespace spidy::web_landing
