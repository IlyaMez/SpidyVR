#pragma once
#include "tracking.hpp"

namespace spidy {
// Rate control around the viewing axis captured when both grips engage.
// A fresh engagement is neutral wherever the wrists currently point.
class WallRollGesture {
  public:
    float update(bool active, const XrFrame& frame) {
        if (!active || !frame.valid || !frame.focused || !validTrackedPose(frame.head) ||
            !std::isfinite(frame.seconds) || frame.seconds <= 0 || frame.seconds > .1f) {
            suspend();
            return angle_;
        }
        bool both = true;
        for (unsigned i = 0; i < 2; ++i) {
            const auto& hand = frame.hands[i];
            const bool valid = hand.valid && validTrackedPose(hand.grip) && std::isfinite(hand.squeeze);
            held_[i] = valid && hand.squeeze > (held_[i] ? .35f : .65f);
            both &= held_[i];
        }
        if (!both) {
            engaged_ = false;
            return angle_;
        }
        if (!engaged_) {
            for (unsigned i = 0; i < 2; ++i)
                neutral_[i] = frame.hands[i].grip.orientation;
            axis_ = frame.head.orientation.rotate({0, 0, -1});
            engaged_ = true;
            return angle_;
        }
        float angles[2]{};
        for (unsigned i = 0; i < 2; ++i) {
            const Quat delta = frame.hands[i].grip.orientation * neutral_[i].conjugate();
            angles[i] = std::remainder(2 * std::atan2(dot({delta.x, delta.y, delta.z}, axis_), delta.w),
                                       6.2831853f);
        }
        // Opposing wrists cancel, including opposite quarter-turns. A circular
        // mean is undefined there and can unexpectedly ask for a half-turn.
        const float deflection = (angles[0] + angles[1]) * .5f;
        // Ten degrees of neutral play; full speed from 45 degrees of wrist roll.
        const float amount = std::clamp((std::abs(deflection) - .17453293f) / .61086524f, 0.f, 1.f);
        angle_ = std::remainder(angle_ + std::copysign(amount * 1.5707963f * frame.seconds, deflection),
                                6.2831853f);
        return angle_;
    }
    float angle() const { return angle_; }
    bool engaged() const { return engaged_; }
    void suspend() {
        engaged_ = false;
        held_[0] = held_[1] = false;
    }
    void reset() { *this = {}; }
  private:
    Quat neutral_[2]{};
    Vec3 axis_{0, 0, -1};
    bool held_[2]{}, engaged_{};
    float angle_{};
};
} // namespace spidy
