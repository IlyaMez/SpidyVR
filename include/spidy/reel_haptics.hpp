#pragma once
#include <cmath>

// The reel felt in the hand (GolexPrime, October 31: "there is no vibration
// when you reel the web in with the trigger while hanging somewhere"). While
// a hand's web reels in, that hand's controller ticks once for every
// `tickMetres` of rope taken in, like a winch: the faster the reel, the
// denser the ticks; a reel that takes no rope (the web already at its
// shortest) is still. The reel's start is a firmer pulse. A web that jumps
// shorter by more than `jump` in one update (a new web, a respawn) takes no
// rope. Controller vibration only, scaled by HAPTICS as the rest.
namespace spidy::reel_haptics {

class Ticks {
  public:
    static constexpr float tickMetres = .75f, jump = 5, startStrength = .45f, tickStrength = .3f,
                           startSeconds = .03f, tickSeconds = .015f;
    struct Pulse {
        float strength{}, seconds{};
        operator bool() const {
            return strength > 0;
        }
    };
    // reeling: this hand's web is attached and reels in now; length: its
    // rope's length (metres).
    Pulse update(bool reeling, float length) {
        if (!reeling || !std::isfinite(length)) {
            reeling_ = false;
            return {};
        }
        if (!reeling_) {
            reeling_ = true;
            last_ = length;
            wound_ = 0;
            return {startStrength, startSeconds};
        }
        const float taken = last_ - length;
        last_ = length;
        if (taken > 0 && taken < jump)
            wound_ += taken;
        if (wound_ < tickMetres)
            return {};
        wound_ = std::fmod(wound_, tickMetres);
        return {tickStrength, tickSeconds};
    }
    bool reeling() const {
        return reeling_;
    }

  private:
    bool reeling_{};
    float last_{}, wound_{};
};

} // namespace spidy::reel_haptics
