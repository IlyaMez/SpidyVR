#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace spidy {
// Render copies of the two eye views, from the game's job copy (19223e0) until a
// render worker ends them, so their GPU markers carry the command serial and
// scene generation. The game can copy a job and drop it without ever making it
// the current view; that copy never ends. Copies older than maxAge generations
// are reclaimed, and a full table gives up its oldest entry, so dropped jobs
// cannot accumulate until no new eye frame can be marked.
class EyeJobTable {
  public:
    struct Job {
        const void* view{};
        uint64_t serial{}, generation{};
        unsigned eye{};
    };
    static constexpr size_t capacity = 128;
    // Live copies end within a few frames; this is far beyond that.
    static constexpr uint64_t maxAge = 16;
    // Returns how many unfinished entries were reclaimed to record this job.
    unsigned record(const Job& job) {
        if (!job.view)
            return 0;
        unsigned reclaimed{};
        Job *same{}, *empty{}, *oldest{};
        for (auto& slot : jobs_) {
            if (slot.view && slot.view != job.view && slot.generation + maxAge < job.generation) {
                slot = {};
                ++reclaimed;
            }
            if (slot.view == job.view)
                same = &slot;
            else if (!slot.view)
                empty = empty ? empty : &slot;
            else if (!oldest || slot.generation < oldest->generation)
                oldest = &slot;
        }
        auto* target = same ? same : empty;
        if (!target) {
            target = oldest;
            ++reclaimed;
        }
        *target = job;
        return reclaimed;
    }
    const Job* find(const void* view) const {
        if (view)
            for (const auto& slot : jobs_)
                if (slot.view == view)
                    return &slot;
        return nullptr;
    }
    void finish(const void* view) {
        if (view)
            for (auto& slot : jobs_)
                if (slot.view == view)
                    slot = {};
    }

  private:
    std::array<Job, capacity> jobs_{};
};
} // namespace spidy
