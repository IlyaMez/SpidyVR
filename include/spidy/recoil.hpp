#pragma once
#include "math.hpp"
#include <algorithm>
#include <cmath>

// A blow a guard stopped knocks the player's view back a little, for the feel of it.
//
// A critically damped spring that a kick pulls out: the view leaves at once, is farthest after 1/rate seconds
// (36 ms), is back within a fifth of a second, and never goes past where it started. It is placement only,
// the kind of offset the hands' push off a wall already is (HandCollision, added to the tracking space's
// origin in GameTrackingRig::update): the hero himself does not move, so there is no physics, no collision
// with anything and no effect on the fight - the way the view stands is all that changes.
namespace spidy {
class Recoil {
  public:
    // The spring's rate, 1/s. A kick of m metres takes the view m metres out after 1/rate seconds, and it is
    // back to 6% of that after a fifth of a second and to 0.5% after 0.3.
    static constexpr float rate = 28.f;
    // However many kicks come, the view is never further than this from home, metres.
    static constexpr float farthest = .1f;

    // Knocks the view back the way `away` points: `metres` out at the farthest (a kick alone never takes it
    // past `farthest`). `away` is levelled and made a unit; no way (zero, not finite, straight up) or no
    // size: no kick. A kick while one is on adds to it.
    void kick(Vec3 away, float metres) {
        away.y = 0;
        if (!finite(away) || !std::isfinite(metres) || !(metres > 0))
            return;
        const float n = length(away);
        if (!(n > 1e-4f))
            return;
        // The speed that takes a critically damped spring m out: it peaks at speed / (rate * e).
        constexpr float e = 2.7182818f;
        v_ += away * ((std::min)(metres, farthest) * rate * e / n);
    }
    // The offset after `seconds` more (0 to a tenth; a longer step is a tenth). `allowed` false: whatever is
    // pulling the view further out stops pulling - the player is held on a surface the view stands on, where
    // "back" is not the world's - and the offset eases home on its own, the way a kick's return goes (only
    // the speed that goes outward is taken; the speed home stays).
    Vec3 update(float seconds, bool allowed = true) {
        const float t = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        if (!allowed) {
            const float out = length(x_);
            if (!(out > 1e-6f))
                v_ = {};
            else if (const float outward = dot(v_, x_ / out); outward > 0)
                v_ -= x_ / out * outward;
        }
        if (t > 0 && (active())) {
            // x(t) = (x0 + (v0 + rate x0) t) e^(-rate t), and its slope: exact, however long the step.
            const Vec3 b = v_ + x_ * rate;
            const float fade = std::exp(-rate * t);
            x_ = (x_ + b * t) * fade;
            v_ = (v_ - b * (rate * t)) * fade;
            x_ = limited(x_, farthest);
            if (length(x_) < 1e-5f && length(v_) < 1e-4f)
                x_ = v_ = {};
        }
        return x_;
    }
    Vec3 offset() const {
        return x_;
    }
    // The view is out of place, or being pulled.
    bool active() const {
        return length(x_) > 0 || length(v_) > 0;
    }
    void reset() {
        x_ = v_ = {};
    }

  private:
    Vec3 x_{}, v_{};
};
} // namespace spidy
