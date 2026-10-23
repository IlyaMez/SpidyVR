#pragma once
#include "crawl_hand_steps.hpp"
#include "game_tracking.hpp"

namespace spidy {
// Visual stride only. Native feet travel advances the cycle; camera offsets,
// physical head movement and elapsed time at rest cannot make the legs step.
class WallCrawlBody {
  public:
    bool update(const GameMotionFrame& frame, const SurfaceHold& hold, bool enabled, float seconds,
                unsigned wallHands = 0) {
        const Vec3 n = normalized(hold.normal);
        const bool active = enabled && frame.active && !frame.ventCrawl && !frame.standing &&
            !frame.swing.jump && finite(n) && length(n) > .9f && n.y < surfaceEnterCos &&
            finite(hold.anchor) && finite(frame.anchor) && std::isfinite(seconds) &&
            seconds > 0 && seconds <= .1f;
        if (!active) { *this = {}; return false; }
        // Swing momentum can carry a wall run with a neutral stick. Leave its
        // legs alone until it slows, or an actual sticky palm catches it.
        // Hysteresis avoids alternating poses as the run loses speed.
        const float speed = std::isfinite(hold.speed) ? std::max(0.f, hold.speed) : 0.f;
        running_ = !wallHands && speed > (running_ ? wallStandSpeed : 3.f);
        if (running_) {
            handSteps_ = {};
            active_ = false;
            motion_ = 0;
            return false;
        }
        const Quat tracking = Quat::yaw(frame.swing.trackingYaw) * frame.bodyTilt();
        Vec3 up = tracking.rotate({0, 1, 0});
        up -= n * dot(up, n);
        if (length(up) < .3f) {
            up = tracking.rotate({0, 0, -1});
            up -= n * dot(up, n);
        }
        if (length(up) < .3f) { *this = {}; return false; }
        handSteps_.update(wallClimbInput(frame), wallHands, n, seconds);
        const Vec3 ahead = normalized(up);
        float moving = 0;
        if (!active_) step_ = ahead;
        else {
            const Quat corner = arc(normal_, n);
            const Vec3 previous = corner.rotate(ahead_);
            const float turn = std::atan2(dot(cross(previous, ahead), n), dot(previous, ahead));
            step_ = Quat::around(n, turn).rotate(corner.rotate(step_));
            Vec3 travel = frame.anchor - feet_;
            travel -= n * dot(travel, n);
            const float distance = length(travel);
            if (distance > .0001f && distance < std::max(.15f, 6.f * seconds)) {
                phase_ = std::fmod(phase_ + distance * (6.2831853f / .7f), 6.2831853f);
                moving = std::clamp((distance / seconds - .03f) / .5f, 0.f, 1.f);
                const Vec3 direction = normalized(travel);
                const float towardStep = std::atan2(dot(cross(step_, direction), n), dot(step_, direction));
                step_ = Quat::around(n, std::clamp(towardStep, -8.f * seconds, 8.f * seconds)).rotate(step_);
            }
        }
        motion_ += std::clamp(moving - motion_, -4.f * seconds, 6.f * seconds);
        ahead_ = ahead;
        step_ = normalized(step_ - n * dot(step_, n));
        normal_ = n;
        floor_ = hold.anchor - frame.anchor;
        feet_ = frame.anchor;
        active_ = true;
        return true;
    }
    Vec3 normal() const { return normal_; }
    Vec3 ahead() const { return ahead_; }
    Vec3 step() const { return step_; }
    Vec3 floor() const { return floor_; }
    float phase() const { return phase_; }
    float motion() const { return motion_; }
    const std::array<uint32_t, 2>& handSteps() const { return handSteps_.strokes(); }
  private:
    CrawlHandSteps handSteps_;
    bool active_{}, running_{};
    Vec3 normal_{}, ahead_{}, step_{}, floor_{}, feet_{};
    float phase_{}, motion_{};
};
} // namespace spidy
