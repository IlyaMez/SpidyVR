#pragma once
#include <cstdint>

namespace spidy {
inline constexpr uint32_t defaultEyeSize = 1536;
inline constexpr uint32_t maximumEyeSize = 4096;
inline bool validEyeSize(uint64_t size) {
    return size >= 64 && size <= maximumEyeSize;
}
} // namespace spidy
