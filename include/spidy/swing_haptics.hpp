#pragma once
#include "reel_haptics.hpp"
#include <algorithm>
#include <cmath>

// The swing felt in the hand (GolexPrime, November 1: like the reel, "only
// weaker", "correlated with the speed on the rope": at the end of a pendulum
// swing "bup, bup, bup", slower and slower, until you hang still). While a
// hand's web holds the body taut in the air, that hand's controller ticks
// once for every `tickMetres` the body travels: a fast swing a quick purr,
// the swing's turning point a few slow ticks, a body hanging still none
// (below `leastSpeed` nothing counts). Fainter than the reel's ticks
// (reel_haptics.hpp), which take the hand's place while it reels; a little
// stronger the faster the swing. Controller vibration only, scaled by
// HAPTICS as the rest.
namespace spidy::swing_haptics {

class Ticks {
  public:
    static constexpr float tickMetres = 2, leastSpeed = 1, fastSpeed = 25, tickStrength = .12f,
                           fastStrength = .22f, tickSeconds = .012f;
    using Pulse = reel_haptics::Ticks::Pulse;
    // swinging: this hand's web is attached, taut, the body in the air, and
    // no reel; speed: the body's (m/s); seconds: since the previous update.
    Pulse update(bool swinging, float speed, float seconds) {
        if (!swinging || !std::isfinite(speed) || !std::isfinite(seconds)) {
            travelled_ = 0;
            return {};
        }
        if (speed < leastSpeed)
            return {};
        travelled_ += speed * std::clamp(seconds, 0.f, .1f);
        if (travelled_ < tickMetres)
            return {};
        travelled_ = std::fmod(travelled_, tickMetres);
        const float fast = std::clamp((speed - leastSpeed) / (fastSpeed - leastSpeed), 0.f, 1.f);
        return {tickStrength + (fastStrength - tickStrength) * fast, tickSeconds};
    }

  private:
    float travelled_{};
};

} // namespace spidy::swing_haptics
