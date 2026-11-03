// STEADY STATES (vr_settings::Values::steadyStates, off by default) and the
// state-change diagnostics: how Spider-Man leaves and lands on the ground, in
// and out of combat, as the mod's pad injection sees it.
//
// The pad injection (NATIVE RUN's RT, game_pad::NativeRun; SPIDY RUN's and
// SOFT LANDING's stick forwarding) is gated each frame on what the swing last
// said (game_swing::gameOwnsGround: fresh, not owned, grounded, no wall, no
// web, no mount, no takeoff, not airborne) and on the hands and prompts
// (game_pad::nativeRunAllowed). Any of those that flickers for a frame - the
// game's ground contact over a kerb, a prompt shown and hidden, a vent's
// crawl flag, the swing's `owned` as it hands a landing over - drops RT and
// raises it again the next frame. With STEADY STATES on, RT comes back only
// once everything has allowed it for riseSeconds (RiseDelay; it still goes at
// once, as RT held off the ground is the game's web swing), and not at all in
// a fight (the combat camera, or an enemy in reach within 3 s), where the
// game's RT is no run. Off: as before.
//
// StateLog writes a line to the session's diagnostics (diag_log) at each
// change of the states that matter to a landing or a take-off, with what it
// changed from and to, why where that can be told, and the speeds - so the
// next test says exactly what happened at each leave and land.
#pragma once
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace spidy::ground_state {
// A permission that goes at once and comes back only after it has held for
// `rise` seconds without a break.
class RiseDelay {
  public:
    static constexpr float riseSeconds = .15f;
    bool update(float dt, bool allowed, float rise = riseSeconds) {
        dt = std::isfinite(dt) ? std::clamp(dt, 0.f, .1f) : 0.f;
        if (!allowed) {
            if (on_)
                ++drops_;
            on_ = false;
            held_ = 0;
            return false;
        }
        held_ += dt;
        if (!on_ && held_ >= rise - 1e-6f)
            on_ = true;
        return on_;
    }
    bool on() const {
        return on_;
    }
    unsigned drops() const {
        return drops_;
    }

  private:
    bool on_{};
    float held_{};
    unsigned drops_{};
};
// What one frame says about the player.
struct Sample {
    bool fresh{};          // the swing's sample is recent
    bool grounded{};       // the game's ground contact (SpidySwingData)
    bool owned{};          // the swing drives the body
    bool webbed{};         // a web holds
    unsigned wall{};       // game_swing::Surface
    unsigned mount{};      // the swing's mount (1 on its way, 2 from the crawl)
    unsigned takeoff{};    // the takeoff's phase (0 idle)
    bool gameGround{};     // game_swing::gameOwnsGround
    bool runRt{};          // NATIVE RUN holds the game's RT
    bool fighting{};       // a fight (combat camera, or an enemy in reach lately)
    bool dodging{};        // a dodge asked for
    Vec3 velocity{};       // the swing's idea of the body's
};
class StateLog {
  public:
    static constexpr size_t lineBytes = 220;
    // At most this many lines a second; a state that flaps more is summed up.
    static constexpr unsigned perSecond = 12;
    // Returns a line to record, or nullptr. Call each frame; seconds since the last.
    const char* update(float seconds, const Sample& s) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .25f) : 0.f;
        // The second just ended, summed up if it flapped or lost lines.
        bool summary = false;
        unsigned flaps = 0, dropped = 0, groundFlips = 0, ownedFlips = 0;
        window_ += dt;
        if (window_ >= 1) {
            summary = dropped_ || flaps_ >= 4;
            flaps = flaps_, dropped = dropped_, groundFlips = groundFlips_, ownedFlips = ownedFlips_;
            window_ = 0;
            lines_ = dropped_ = flaps_ = groundFlips_ = ownedFlips_ = 0;
        }
        const char* change = s.fresh ? changed(s) : nullptr;
        if (summary) {
            if (change)
                ++dropped_;
            std::snprintf(line_, sizeof line_,
                          "state: %u changes of grounded or owned in the last second (%u lines not written): "
                          "grounded %u, owned %u flips",
                          flaps, dropped, groundFlips, ownedFlips);
            return line_;
        }
        if (!change)
            return nullptr;
        if (lines_ >= perSecond) {
            ++dropped_;
            return nullptr;
        }
        ++lines_;
        return change;
    }

  private:
    const char* changed(const Sample& s) {
        if (!primed_) {
            primed_ = true;
            last_ = s;
            return nullptr;
        }
        std::string changes;
        const auto flag = [&](const char* name, bool from, bool to, const char* why) {
            if (from == to)
                return;
            char part[96];
            std::snprintf(part, sizeof part, "%s%s %d->%d%s%s%s", changes.empty() ? "" : ", ", name, from ? 1 : 0,
                          to ? 1 : 0, why && *why ? " (" : "", why ? why : "", why && *why ? ")" : "");
            changes += part;
        };
        const auto number = [&](const char* name, unsigned from, unsigned to) {
            if (from == to)
                return;
            char part[64];
            std::snprintf(part, sizeof part, "%s%s %u->%u", changes.empty() ? "" : ", ", name, from, to);
            changes += part;
        };
        const char* landWhy = !s.grounded ? (s.takeoff ? "takeoff" : s.webbed ? "web" : "off the ground")
                              : s.owned   ? "touchdown, the swing driving"
                              : s.webbed  ? "touchdown with a web held"
                                          : "touchdown";
        flag("grounded", last_.grounded, s.grounded, landWhy);
        flag("owned", last_.owned, s.owned,
             s.owned ? (s.grounded ? "the swing drives on the ground" : "the swing took the body")
                     : (s.grounded ? "the game's on the ground" : "the game's in the air"));
        flag("webbed", last_.webbed, s.webbed, nullptr);
        number("wall", last_.wall, s.wall);
        number("mount", last_.mount, s.mount);
        number("takeoff", last_.takeoff, s.takeoff);
        flag("game ground", last_.gameGround, s.gameGround, nullptr);
        flag("RT", last_.runRt, s.runRt, nullptr);
        flag("fight", last_.fighting, s.fighting, nullptr);
        flag("dodge", last_.dodging, s.dodging, nullptr);
        groundFlips_ += last_.grounded != s.grounded;
        ownedFlips_ += last_.owned != s.owned;
        flaps_ += (last_.grounded != s.grounded) + (last_.owned != s.owned);
        last_ = s;
        if (changes.empty())
            return nullptr;
        std::snprintf(line_, sizeof line_, "state: %s; %.1f m/s across, %.1f m/s up; owned %d, webbed %d, fight %d",
                      changes.c_str(), std::hypot(s.velocity.x, s.velocity.z), s.velocity.y, s.owned ? 1 : 0,
                      s.webbed ? 1 : 0, s.fighting ? 1 : 0);
        return line_;
    }
    bool primed_{};
    Sample last_{};
    float window_{};
    unsigned lines_{}, dropped_{}, flaps_{}, groundFlips_{}, ownedFlips_{};
    char line_[lineBytes]{};
};
} // namespace spidy::ground_state
