#pragma once
#include <algorithm>
#include <cstdint>

namespace spidy {
struct XrFrameTiming {
    double total{}, wait{}, tracking{}, prepare{}, acquire{}, release{}, end{}, period{};
};
struct TimingMetric {
    uint64_t count{};
    double totalMs{}, maxMs{}, lastMs{};
    void add(double ms) {
        ++count;
        totalMs += ms;
        maxMs = std::max(maxMs, ms);
        lastMs = ms;
    }
};
struct XrTimingData {
    uint32_t magic = 0x5358544d, version = 1, bytes = sizeof(XrTimingData), reserved{};
    int64_t sequence{};
    // frame, wait-frame, tracking, prepare, acquire, copy submission wait,
    // hand overlay CPU, release, end-frame, runtime display period.
    TimingMetric stages[10];
};
static_assert(sizeof(TimingMetric) == 32 && sizeof(XrTimingData) == 344);
} // namespace spidy
