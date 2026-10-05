#pragma once
#include "math.hpp"
#include <cstdint>

namespace spidy {
struct TrackedHand {
    Pose aim, grip;
    bool valid{};
    float trigger{}, squeeze{}, stickX{}, stickY{};
    bool stickClick{};
};
struct EyeFov {
    float left{}, right{}, down{}, up{};
};
struct TrackedEye {
    Pose pose{};
    EyeFov fov{};
    unsigned width{}, height{};
};
struct XrFrame {
    Pose head{};
    std::array<TrackedHand, 2> hands{};
    std::array<TrackedEye, 2> eyes{};
    std::int64_t predictedDisplayTime{};
    float seconds = 1.f / 90;
    bool focused{}, valid{}, jump{}, reset{}, recentered{};
};
inline bool validTrackedPose(Pose p) {
    const auto q = p.orientation;
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return finite(p.position) && std::isfinite(n) && std::abs(n - 1) < .001f;
}
} // namespace spidy
