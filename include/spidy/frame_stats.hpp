// perf: lines in the session's diagnostics (v5, always on; changes nothing):
// how the VR loop's frames went, and how long the mod's own input work took
// in each, every few seconds, and at each start and end of a fight, so a
// session that feels slow or sticky says whether frames, the mod or neither.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace spidy::frame_stats {
class FrameStats {
  public:
    static constexpr float everySeconds = 5, slowFrame = 1.f / 30;
    // seconds: since the last frame. work: the mod's own time in it (seconds).
    // fighting: a fight now. Returns a line to write, or null.
    const char* update(float seconds, float work, bool fighting) {
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > 1)
            return nullptr;
        work = std::isfinite(work) && work > 0 ? work : 0.f;
        const bool changed = fighting != fighting_;
        if (!changed) {
            ++frames_;
            time_ += seconds;
            longest_ = std::max(longest_, seconds);
            slow_ += seconds > slowFrame;
            work_ += work;
            workLongest_ = std::max(workLongest_, work);
        }
        if (!changed && time_ < everySeconds)
            return nullptr;
        const char* why = changed ? (fighting ? "before the fight" : "in the fight") : "";
        if (frames_)
            std::snprintf(line_, sizeof line_,
                          "perf: %.1f s%s%s, %u frames (%.1f fps), longest %d ms, %u over 33 ms; the mod's "
                          "input work %.2f ms a frame (longest %.2f ms); fight %d",
                          time_, changed ? " " : "", why, frames_, frames_ / time_,
                          static_cast<int>(longest_ * 1000 + .5f), slow_, work_ * 1000 / frames_,
                          workLongest_ * 1000, fighting_ ? 1 : 0);
        const bool wrote = frames_ > 0;
        fighting_ = fighting;
        frames_ = slow_ = 0;
        time_ = longest_ = work_ = workLongest_ = 0;
        return wrote ? line_ : nullptr;
    }

  private:
    bool fighting_{};
    uint32_t frames_{}, slow_{};
    float time_{}, longest_{}, work_{}, workLongest_{};
    char line_[240]{};
};
} // namespace spidy::frame_stats
