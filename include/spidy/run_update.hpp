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
        // The mod driving the body on the ground (SPIDY RUN, or a web strike's
        // moment there): when it begins, and on its end how far the body went
        // against what was asked - whether the game's mover takes the command.
        const bool mod = s.grounded && s.owned && !s.webbed;
        if (mod && !mod_) {
            modTime_ = modAsked_ = 0;
            modFrom_ = s.position;
            if (!out) {
                std::snprintf(line_, sizeof line_, "run log: mod-driven run on the ground began at %.1f m/s",
                              h);
                out = line_;
            }
        } else if (mod) {
            modTime_ += dt;
            modAsked_ += horizontal(s.requested) * dt;
        } else if (mod_ && !out) {
            Vec3 d = s.position - modFrom_;
            d.y = 0;
            const float went = modTime_ > 0 ? length(d) / modTime_ : 0.f;
            const float asked = modTime_ > 0 ? modAsked_ / modTime_ : 0.f;
            std::snprintf(line_, sizeof line_,
                          "run log: mod-driven run on the ground ended after %.2f s: went %.1f m/s, asked "
                          "%.1f m/s (%.0f%%), now %s",
                          modTime_, went, asked, asked > 0 ? 100 * went / asked : 0.f,
                          s.grounded ? "the game's" : "in the air");
            out = line_;
        }
        mod_ = mod;
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
    bool started_{}, grounded_{}, mod_{};
    Vec3 modFrom_{};
    float modTime_{}, modAsked_{};
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
// SOFT LANDING (vr_settings::Values::softLanding, off by default; Swing::
// allowSoftLanding): a fall the swing flies with no web holding eases to
// softFall down once the ground is within softHeight below the feet, or
// softSeconds of the fall away, at softRate of the difference a second. The
// speed across the ground is not touched; nothing slower than softFall is.
// feetAbove: the feet's height over the ground below, under 0 for none found.
constexpr float softFall = 9, softHeight = 3, softSeconds = .35f, softRate = 8;
inline float softLandVertical(float vy, float feetAbove, float dt) {
    if (!(feetAbove >= 0) || !std::isfinite(vy) || !(vy < -softFall) || !std::isfinite(dt) || !(dt > 0))
        return vy;
    if (feetAbove > softHeight && feetAbove / -vy > softSeconds)
        return vy;
    return vy + (-softFall - vy) * std::min(1.f, softRate * dt);
}
// How far below the body's centre the ground matters for it: the larger of
// softHeight and what the fall covers in softSeconds, and a step more.
inline float softLandReach(float falling, float seconds) {
    return 1 + std::max(softHeight, falling * softSeconds) + falling * std::max(seconds, 0.f) + .5f;
}
// SPIDY RUN (vr_settings::Values::spidyRun, off by default; Swing::
// allowGroundRun): the wall run's momentum (Swing's wall entry and its step)
// on the ground. A landing faster than enterAbove across keeps that speed
// (grazing the ground loses nothing; of the speed that went into it, carry
// of it at most comes out ahead, as wallCarry does on a wall), entryMax at
// most. Each step the stick steers it: its way turns toward the stick's at
// turnRate (turnSlow at rest down to turnFast at entryMax), its speed kept;
// a stick held further than the run goes takes it up to sprint at
// acceleration; one held more than 90 degrees from the run's way brakes it at
// 3 brake while it turns. Faster than sprint it
// decays by decay a second, and by brake with no stick. At sprint it is the
// game's run again (Swing ends it). It goes along the ground plane only: the
// caller presses the body down at `press` to keep it on slopes and kerbs.
struct GroundRun {
    float sprint = 9, enterAbove = 10, carry = .15f, entryMax = 30, acceleration = 24, brake = 6, decay = 4;
    float turnSlow = 9.42478f /* 540 degrees */, turnFast = 3.14159f /* 180 degrees */, press = 2;
    // SPIDY RUN FROM STANDING (vr_settings::Values::spidyRunFromStanding,
    // off by default; Swing::allowGroundRunFromStanding): on the ground, the
    // stick held past standTilt for standHold seconds starts the run from
    // whatever speed the body has (a stand, a walk, the game's run). While
    // the stick stays past standTilt the run builds to the sprint at
    // acceleration and on past it at standAcceleration to standTop, and does
    // not decay; let go under standTilt it is the run as from a landing
    // (standRunStep). After a run from standing ends, the stick must come
    // back under standRearm before the next.
    float standTilt = .9f, standHold = .3f, standRearm = .5f, standAcceleration = 3, standTop = 14;
};
constexpr GroundRun groundRun{};
// The run a landing at `velocity` begins with, across the ground (y 0).
inline Vec3 groundRunEntry(Vec3 velocity, const GroundRun& k = groundRun) {
    if (!finite(velocity))
        return {};
    Vec3 along{velocity.x, 0, velocity.z};
    const float into = std::max(-velocity.y, 0.f), speed = length(velocity), across = length(along);
    if (into > 0 && across > 1e-3f && speed > 1e-3f)
        along = along * std::min(speed / across, 1 + k.carry * into / speed);
    return limited(along, k.entryMax);
}
// One step of the run: `along` its velocity across the ground, `move` the
// stick in the world (Input::move), dt the step. Returns the new velocity.
inline Vec3 groundRunStep(Vec3 along, Vec3 move, float dt, const GroundRun& k = groundRun) {
    along.y = 0;
    if (!finite(along) || !std::isfinite(dt) || !(dt > 0))
        return finite(along) ? along : Vec3{};
    const Vec3 stick = finite(move) ? limited(Vec3{move.x, 0, move.z}, 1) : Vec3{};
    const float tilt = length(stick);
    float speed = length(along);
    Vec3 way = speed > .1f ? along / speed : Vec3{};
    if (tilt > .05f) {
        const Vec3 to = stick / tilt;
        if (speed <= .1f) {
            way = to;
        } else {
            // Turn toward the stick, no faster than the turn rate at this speed.
            const float rate = k.turnSlow + (k.turnFast - k.turnSlow) *
                                                std::clamp(speed / std::max(k.entryMax, 1.f), 0.f, 1.f);
            const float angle = std::atan2(cross(way, to).y, dot(way, to));
            const float turn = std::clamp(angle, -rate * dt, rate * dt);
            const float c = std::cos(turn), s = std::sin(turn);
            // About y: x' = x c + z s, z' = -x s + z c turns way toward `to` by +turn.
            way = normalized(Vec3{way.x * c + way.z * s, 0, -way.x * s + way.z * c});
            if (std::abs(angle - turn) < 1e-4f)
                way = to;
        }
        if (dot(way, to) < 0)
            speed = std::max(0.f, speed - 3 * k.brake * dt);
        else if (speed < k.sprint * tilt)
            speed = std::min(speed + k.acceleration * dt, k.sprint * tilt);
    } else if (speed > k.sprint) {
        speed -= k.brake * dt;
    }
    if (speed > k.sprint)
        speed = std::max(k.sprint, speed - k.decay * dt);
    return way * speed;
}
// One step of a run from standing: groundRunStep, but while the stick is
// held past standTilt (and not against the run) the speed rises to the sprint
// at acceleration and beyond it at standAcceleration to standTop, no decay.
inline Vec3 standRunStep(Vec3 along, Vec3 move, float dt, const GroundRun& k = groundRun) {
    const Vec3 next = groundRunStep(along, move, dt, k);
    along.y = 0;
    if (!finite(along) || !std::isfinite(dt) || !(dt > 0) || !finite(move))
        return next;
    const Vec3 stick = limited(Vec3{move.x, 0, move.z}, 1);
    const float tilt = length(stick), nextSpeed = length(next);
    if (tilt < k.standTilt || nextSpeed < 1e-4f || dot(next, stick) <= 0)
        return next;
    const Vec3 way = next / nextSpeed;
    float speed = std::min(length(along), std::max(k.standTop, k.sprint));
    speed = speed < k.sprint ? std::min(speed + k.acceleration * dt, k.sprint)
                             : std::min(speed + k.standAcceleration * dt, std::max(k.standTop, k.sprint));
    return way * speed;
}
// Whether the stick holds a run from standing (past standTilt).
inline bool standHeld(Vec3 move, const GroundRun& k = groundRun) {
    return finite(move) && std::hypot(move.x, move.z) >= k.standTilt;
}
} // namespace spidy::run_update
