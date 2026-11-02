#pragma once
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

// The run update: a faster run on foot (RUN CURVE, NATIVE RUN AT) and what the
// session's timeline records of running (RUN LOGS). Every piece here is off
// unless its setting is on, and then changes only what it names.
namespace spidy::run_update {
// RUN CURVE (vr_settings::Values::runCurve, off by default): on the ground the
// stick reaches the game's full run sooner. Past the dead zone its tilt is
// stretched so `snap` of it is already a full one, and bent by `gamma` (under
// 1: more of the run's speed early). Its way is kept; only how far it leans
// changes. Off, or within the dead zone, the stick is as it was.
constexpr float curveDeadZone = .1f, curveSnap = .8f, curveGamma = .7f;
inline void curveStick(float& right, float& forward, bool on, float snap = curveSnap,
                       float gamma = curveGamma) {
    if (!on || !std::isfinite(right) || !std::isfinite(forward))
        return;
    const float tilt = std::hypot(right, forward);
    snap = std::isfinite(snap) ? std::clamp(snap, curveDeadZone + .05f, 1.f) : curveSnap;
    gamma = std::isfinite(gamma) ? std::clamp(gamma, .2f, 3.f) : curveGamma;
    if (tilt <= curveDeadZone)
        return;
    const float t = std::clamp((tilt - curveDeadZone) / (snap - curveDeadZone), 0.f, 1.f);
    const float out = curveDeadZone + (1 - curveDeadZone) * std::pow(t, gamma);
    right = std::clamp(right * out / tilt, -1.f, 1.f);
    forward = std::clamp(forward * out / tilt, -1.f, 1.f);
}
// NATIVE RUN AT (vr_settings::Values::nativeRunPercent, 70 by default): the
// stick's tilt, percent, past which NATIVE RUN holds the game's RT; it lets go
// 20 points under that (70 and 50 as before).
struct Thresholds {
    float onAbove, offBelow;
};
inline Thresholds nativeRunThresholds(int percent) {
    const float on = std::clamp(percent, 50, 90) / 100.f;
    return {on, on - .2f};
}
// RUN LOGS (vr_settings::Values::runLogs, off by default): lines in the
// session's timeline (game_prompts::note) about running, for the questions the
// run update could not answer without the game: how fast the game's own run
// and sprint go (measured from where the body is), whether the swing's idea of
// the body's speed on the ground follows it, whether a body the swing drives on
// the ground goes where it is sent, and what speed a landing keeps. Nothing
// here changes how anything moves.
struct RunSample {
    float seconds{}; // since the previous sample
    bool fresh{};    // the swing's sample is recent
    bool grounded{}, owned{}, webbed{}, nativeRun{};
    Vec3 position{};       // the body, from the swing's sample
    Vec3 velocity{};       // the swing's own idea of the body's velocity
    Vec3 requested{};      // what the swing asked the game's mover for (owned)
    float stick{}, sent{}; // the stick's tilt, and as it went to the game
};
class RunLog {
  public:
    static constexpr float window = .5f;
    static constexpr size_t lineBytes = 200;
    // Returns a line to record, or nullptr.
    const char* update(const RunSample& s) {
        const float dt = std::isfinite(s.seconds) ? std::clamp(s.seconds, 0.f, .1f) : 0.f;
        if (!s.fresh || !finite(s.position) || !finite(s.velocity)) {
            started_ = false;
            return nullptr;
        }
        const char* out = nullptr;
        const float h = horizontal(s.velocity);
        if (!started_) {
            started_ = true;
            grounded_ = s.grounded;
            begin(s);
            return nullptr;
        }
        if (s.grounded != grounded_) {
            if (s.grounded) {
                std::snprintf(line_, sizeof line_,
                              "run log: touchdown at %.1f m/s across, %.1f m/s down (swing's), owned %d, "
                              "webbed %d",
                              lastAirH_, -lastAirV_, s.owned ? 1 : 0, s.webbed ? 1 : 0);
                landing_ = 0;
                landingFrom_ = s.position;
            } else {
                std::snprintf(line_, sizeof line_,
                              "run log: off the ground at %.1f m/s measured, %.1f m/s swing's, NATIVE RUN %d",
                              measured_, h, s.nativeRun ? 1 : 0);
                landing_ = -1;
            }
            out = line_;
            grounded_ = s.grounded;
            begin(s);
        }
        if (!s.grounded) {
            lastAirH_ = h;
            lastAirV_ = s.velocity.y;
            return out;
        }
        if (landing_ >= 0) {
            landing_ += dt;
            if (landing_ >= .3f && !out) {
                Vec3 d = s.position - landingFrom_;
                d.y = 0;
                std::snprintf(line_, sizeof line_,
                              "run log: 0.3 s after touchdown: %.1f m/s measured (landed at %.1f)",
                              length(d) / landing_, lastAirH_);
                out = line_;
                landing_ = -1;
            }
        }
        time_ += dt;
        stick_ += s.stick * dt;
        sent_ += s.sent * dt;
        run_ += s.nativeRun ? dt : 0;
        owned_ += s.owned ? dt : 0;
        swing_ += h * dt;
        request_ += (s.owned ? horizontal(s.requested) : 0) * dt;
        if (time_ >= window) {
            Vec3 d = s.position - from_;
            d.y = 0;
            measured_ = length(d) / time_;
            top_ = std::max(top_, measured_);
            if (!out && (measured_ > .3f || stick_ / time_ > .1f || owned_ > 0)) {
                std::snprintf(
                    line_, sizeof line_,
                    "run log: ground %.1f m/s measured (top %.1f), swing's %.1f, stick %.2f -> %.2f, "
                    "NATIVE RUN %.0f%%, mod-driven %.0f%% asking %.1f m/s",
                    measured_, top_, swing_ / time_, stick_ / time_, sent_ / time_, 100 * run_ / time_,
                    100 * owned_ / time_, owned_ > 0 ? request_ / owned_ : 0.f);
                out = line_;
            }
            begin(s);
        }
        return out;
    }
    float measured() const {
        return measured_;
    }
    float top() const {
        return top_;
    }

  private:
    static float horizontal(Vec3 v) {
        return std::hypot(v.x, v.z);
    }
    void begin(const RunSample& s) {
        from_ = s.position;
        time_ = stick_ = sent_ = run_ = owned_ = swing_ = request_ = 0;
    }
    bool started_{}, grounded_{};
    Vec3 from_{}, landingFrom_{};
    float time_{}, stick_{}, sent_{}, run_{}, owned_{}, swing_{}, request_{};
    float measured_{}, top_{}, lastAirH_{}, lastAirV_{}, landing_{-1};
    char line_[lineBytes]{};
};
// RUNNING LEAP (vr_settings::Values::momentumLeap, off by default;
// Swing::allowMomentumLeap): a charged leap from the ground that keeps the
// run's speed. The leap's way is the stick's (or, with the stick at rest, the
// run's); of the body's speed across the ground, `carry` of what goes that way
// is kept and the charge's own push (leapForward, from half to all of it with
// the charge, times the stick's tilt) is added, `addScale` of it, together at
// most `horizontalMax`. `sideKeep` of the speed across that way is kept too.
// A leap charged to longCharge or more from a run faster than longMinSpeed is
// a long jump: longForward more ahead, longUp of the height. Up as before.
// Leap{} reproduces the leap as it was: nothing kept, the charge's push alone.
struct Leap {
    float carry = 0, sideKeep = 0, addScale = 1, horizontalMax = INFINITY;
    float longCharge = INFINITY, longMinSpeed = INFINITY, longUp = 1, longForward = 1;
};
constexpr Leap momentumLeap{1, .5f, .6f, 30, .6f, 6, .8f, 1.3f};
// velocity: the body's as the leap goes (the game's run, measured); move: the
// stick in the world (Input::move); charge: 0 to 1. Returns the leap's
// velocity, at most maxSpeed.
inline Vec3 leapVelocity(const Leap& k, Vec3 velocity, Vec3 move, float charge, float leapUp,
                         float leapUpFull, float leapForward, float maxSpeed) {
    charge = std::isfinite(charge) ? std::clamp(charge, 0.f, 1.f) : 0.f;
    const Vec3 stick = finite(move) ? Vec3{move.x, 0, move.z} : Vec3{};
    const Vec3 run = finite(velocity) ? Vec3{velocity.x, 0, velocity.z} : Vec3{};
    const float tilt = std::min(length(stick), 1.f);
    Vec3 way{};
    if (tilt > .1f)
        way = stick / length(stick);
    else if (length(run) > .5f)
        way = run / length(run);
    const float push = tilt > .1f ? leapForward * (.5f + .5f * charge) * tilt : 0.f;
    const float along = dot(run, way);
    const float carried = std::max(along, 0.f) * k.carry;
    float ahead = carried + push * k.addScale;
    float up = leapUp + (leapUpFull - leapUp) * charge;
    if (charge >= k.longCharge && along > k.longMinSpeed) {
        ahead *= k.longForward;
        up *= k.longUp;
    }
    const Vec3 side = (run - way * along) * k.sideKeep;
    const Vec3 out = way * std::min(ahead, k.horizontalMax) + side + Vec3{0, up, 0};
    return finite(out) ? limited(out, maxSpeed) : Vec3{0, up, 0};
}
} // namespace spidy::run_update
