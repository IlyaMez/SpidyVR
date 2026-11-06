#pragma once
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// DODGE TUMBLE (vr_settings::Values::dodgeTumble, OFF by default): the view
// tumbles with a dodge of the game's, so the player sees it was a dodge.
//
// The view turns about the head, the eyes alone (GameTrackingRig::viewTurn):
// the head pose the aims, the look and the body read stays where it is, and
// so do the hands and the HUD. A dodge to a side rolls about the head's level
// forward (its right side going down for a dodge to the right); one ahead or
// back pitches about its level right, as a front or a back flip does.
// LEAN: a short lean that comes back, 24 degrees at most, in 0.45 s. ROLL: a
// whole turn, 0.6 s, eased in and out, ending upright where it began.
//
// No head bone: Spidy reads none of the game's skeleton, so the view cannot be
// locked to the character's head as its dodge animation moves it. The tumble
// is the headset's own, started when the game's dodge manager starts a dodge
// the player asked for (game_dodge's SpidyDodgeLive, game_xr).
namespace spidy::dodge_tumble {
enum class Style : int { off = 0, lean = 1, roll = 2 };
constexpr float leanSeconds = .45f, rollSeconds = .6f, leanAngle = .41887902f, // 24 degrees
    fadeSeconds = .12f;
class Tumble {
  public:
    // A dodge began: `axis`, the world axis to turn about (unit; the head's
    // level forward for a roll, its level right for a flip), `side` +1 or -1.
    void start(Style style, Vec3 axis, float side) {
        if (style == Style::off || !finite(axis) || length(axis) < .5f || !std::isfinite(side)) {
            reset();
            return;
        }
        style_ = style;
        axis_ = normalized(axis);
        side_ = side < 0 ? -1.f : 1.f;
        t_ = 0;
        fade_ = 1;
        hurry_ = false;
        active_ = true;
    }
    // One frame of `seconds`. allowed false: the tumble ends soon, never
    // snapping - a lean fades out in fadeSeconds, a roll finishes at three
    // times its speed (upright is where a roll ends).
    void update(float seconds, bool allowed) {
        if (!active_)
            return;
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        if (!allowed)
            hurry_ = true;
        const float duration = style_ == Style::roll ? rollSeconds : leanSeconds;
        if (hurry_ && style_ == Style::lean)
            fade_ = std::max(0.f, fade_ - dt / fadeSeconds);
        t_ += dt * (hurry_ && style_ == Style::roll ? 3.f : 1.f);
        if (t_ >= duration || fade_ <= 0)
            reset();
    }
    void reset() {
        active_ = false;
        t_ = 0;
        fade_ = 1;
        hurry_ = false;
    }
    bool active() const {
        return active_;
    }
    // The turn now, radians about axis(): 0 at the start and at the end.
    float angle() const {
        if (!active_)
            return 0;
        if (style_ == Style::roll) {
            const float u = std::clamp(t_ / rollSeconds, 0.f, 1.f);
            const float eased = u * u * u * (u * (u * 6 - 15) + 10); // smootherstep
            return side_ * 6.2831853f * eased;
        }
        const float u = std::clamp(t_ / leanSeconds, 0.f, 1.f);
        return side_ * leanAngle * std::sin(3.14159265f * u) * fade_;
    }
    Vec3 axis() const {
        return axis_;
    }
    // The world turn the eyes take about the head; the identity when idle.
    Quat turn() const {
        const float a = angle();
        return active_ && a != 0 ? Quat::around(axis_, a) : Quat{};
    }

  private:
    Style style_{};
    Vec3 axis_{0, 0, -1};
    float side_ = 1, t_{}, fade_ = 1;
    bool hurry_{}, active_{};
};
// Which way a dodge tumbles, from the stick the game got with it (Walk's
// right, forward) and the head's level axes: a roll to the side the stick
// points, or with the stick mostly ahead or back a flip that way. No stick:
// the game's dodge goes back, a back flip.
// pitch: a flip ahead or back, which DODGE TUMBLE's ROLL only leans (a
// whole flip would turn the game camera's look over, and the stick the game
// reads goes along it).
struct Way {
    Vec3 axis;
    float side;
    bool pitch;
};
inline Way way(float right, float forward, Vec3 levelForward, Vec3 levelRight) {
    if (!std::isfinite(right) || !std::isfinite(forward))
        right = forward = 0;
    if (std::abs(right) >= std::abs(forward) && std::abs(right) > .2f)
        return {levelForward, right > 0 ? 1.f : -1.f, false};
    // Around the right: a positive turn takes the look up (a back flip).
    return {levelRight, forward > .2f ? -1.f : 1.f, true};
}
// The game's dodges that tumble (game_dodge.hpp's kinds): 4 the ground's
// spline dodge and 5 the dodge on the move. Not 8 (the web rush) or 14 (the
// zip to a target), and never Spidy's own on a web.
inline bool tumbles(uint32_t kind) {
    return kind == 4 || kind == 5;
}
// A pose turned by `turn` about `pivot` (world).
inline Pose turnedAbout(Pose p, Vec3 pivot, Quat turn) {
    return {pivot + turn.rotate(p.position - pivot), turn * p.orientation};
}
} // namespace spidy::dodge_tumble
