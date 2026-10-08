#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace spidy {
inline constexpr uint32_t defaultEyeSize = 1536;
// Headsets already ask for up to about 4000 pixels a side (Virtual Desktop's
// 4032 x 3648); twice 4096 leaves room to render above that.
inline constexpr uint32_t maximumEyeSize = 8192;
inline bool validEyeSize(uint64_t size) {
    return size >= 64 && size <= maximumEyeSize;
}
// The render scale: percent of the headset's recommended eye size, per side.
// 125 makes each side 1.25 times as long, about 1.56 times the pixels.
inline constexpr uint32_t minimumRenderScale = 50, maximumRenderScale = 200;
inline bool validRenderScale(uint64_t percent) {
    return percent >= minimumRenderScale && percent <= maximumRenderScale;
}
// The eye size for `percent` of the runtime's recommended `width` x `height`:
// the recommendation itself at 100, otherwise rounded to multiples of 8 like
// the runtimes' own. Too large for the runtime's largest (`limitWidth` x
// `limitHeight`) or maximumEyeSize, it shrinks to fit, keeping its shape.
inline std::array<uint32_t, 2> scaledEyeSize(uint32_t width, uint32_t height, uint32_t percent,
                                             uint32_t limitWidth = maximumEyeSize,
                                             uint32_t limitHeight = maximumEyeSize) {
    if (!width || !height)
        return {0, 0};
    double w = width, h = height;
    if (percent != 100) {
        w = std::max(64.0, std::round(w * percent / 800) * 8);
        h = std::max(64.0, std::round(h * percent / 800) * 8);
    }
    const double fit = std::min({1.0, std::min(limitWidth, maximumEyeSize) / w,
                                 std::min(limitHeight, maximumEyeSize) / h});
    if (fit < 1) {
        // The side that meets its limit lands on it, not 8 short of it by rounding error.
        w = std::floor(w * fit / 8 + 1e-6) * 8;
        h = std::floor(h * fit / 8 + 1e-6) * 8;
    }
    return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
}
} // namespace spidy
