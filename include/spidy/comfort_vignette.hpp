#pragma once
// COMFORT VIGNETTE: the edge of the view goes dark while the game moves the player faster than they move
// themselves, so less of the world streams past at the edge of sight, where motion sickness starts. The
// player's own head moves the view as it always does; what counts is the motion of the tracking space in
// the world: the hero's speed (swinging, falling, running) and the view's turns that the head did not make
// (smooth turning, flips, a wall turning under you). D3D12Renderer::Grade::vignette draws it.
#include "math.hpp"
#include <algorithm>
#include <cmath>

namespace spidy::comfort_vignette {
// The tab's choices: OFF, LOW, MEDIUM, HIGH, and how far each closes the view at full speed
// (Grade::vignette: 0 open; at 1 dark from a little past half way to the edge).
inline constexpr int levels = 4;
inline constexpr float closures[levels] = {0, .4f, .65f, .9f};
// Speeds of the player's feet in the world, m/s: none below a jog, all from a fast swing on.
inline constexpr float slowSpeed = 6, fastSpeed = 25;
// Turns of the tracking space in the world, degrees a second: none below a slow smooth turn, all from a
// flip on.
inline constexpr float slowTurn = 30, fastTurn = 150;
// A step bigger than this in one frame is a jump, not motion: a snap turn (a comfort turn of its own), a
// checkpoint reload, a new player.
inline constexpr float jumpMetres = 5, jumpDegrees = 12;
// How fast it closes and opens again, seconds (to about two thirds of the way).
inline constexpr float closeSeconds = .15f, openSeconds = .6f;

inline float ramp(float value, float low, float high) {
    const float t = std::clamp((value - low) / (high - low), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}

class Vignette {
  public:
    // One headset frame: `seconds` since the last one, the player's feet in the world
    // (GameMotionFrame::anchor), the tracking space's turn in the world (the head's world orientation
    // times its tracked one's inverse), whether VR shows play (anything else opens it at once), and the
    // tab's level. Returns how far the view is closed (Grade::vignette).
    float update(float seconds, Vec3 feet, Quat turn, bool playing, int level) {
        level = std::clamp(level, 0, levels - 1);
        if (!playing || !level || !std::isfinite(seconds) || seconds <= 0 || seconds > .25f || !finite(feet)) {
            closure_ = 0;
            seen_ = false;
            return closure_;
        }
        float wanted = 0;
        if (seen_) {
            const float moved = length(feet - feet_);
            const Quat step = turn * turn_.conjugate();
            const float degrees = 2 * std::acos(std::clamp(std::abs(step.w), 0.f, 1.f)) * 57.29578f;
            if (moved < jumpMetres)
                wanted = std::max(wanted, ramp(moved / seconds, slowSpeed, fastSpeed));
            if (std::isfinite(degrees) && degrees < jumpDegrees)
                wanted = std::max(wanted, ramp(degrees / seconds, slowTurn, fastTurn));
        }
        feet_ = feet;
        turn_ = turn;
        seen_ = true;
        const float target = wanted * closures[level];
        const float tau = target > closure_ ? closeSeconds : openSeconds;
        closure_ += (target - closure_) * (1 - std::exp(-seconds / tau));
        return closure_;
    }
    float closure() const {
        return closure_;
    }

  private:
    Vec3 feet_{};
    Quat turn_{};
    bool seen_{};
    float closure_{};
};
} // namespace spidy::comfort_vignette
