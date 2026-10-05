#pragma once
#include <cstdint>

namespace spidy {
// Movement requires recent accepted image submission. A session that once
// displayed an image must not keep driving controls after presentation stalls.
inline bool recentPresentation(uint64_t lastSubmittedMs, uint64_t nowMs) {
    return lastSubmittedMs && nowMs >= lastSubmittedMs && nowMs - lastSubmittedMs <= 250;
}
} // namespace spidy
