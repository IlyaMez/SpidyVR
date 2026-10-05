#pragma once
#include <array>
#include <cstdint>

namespace spidy {
// Applied in GPU submission order, not CPU recording order. A new begin makes
// the previous image unusable even when the new job uses the same head pose.
class EyePairState {
  public:
    struct Stamp {
        uint64_t serial{}, generation{};
        bool operator==(const Stamp&) const = default;
    };
    void begin(unsigned eye, Stamp stamp) {
        active_[eye] = stamp;
        finished_[eye] = {};
    }
    void end(unsigned eye, Stamp stamp) {
        if (active_[eye] == stamp)
            finished_[eye] = stamp;
        else
            finished_[eye] = {};
    }
    Stamp ready() const {
        if (finished_[0].serial && finished_[0].generation && finished_[0] == finished_[1])
            return finished_[0];
        return {};
    }

  private:
    std::array<Stamp, 2> active_{}, finished_{};
};
} // namespace spidy
