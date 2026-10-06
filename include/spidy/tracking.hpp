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
// Face and menu buttons of the VR controllers (XrFrame::buttons). On Touch
// controllers A and B are on the right, X, Y and the menu button on the left.
enum ControllerButton : uint32_t { buttonA = 1, buttonB = 2, buttonX = 4, buttonY = 8, buttonMenu = 16 };
struct XrFrame {
    Pose head{};
    std::array<TrackedHand, 2> hands{};
    std::array<TrackedEye, 2> eyes{};
    std::int64_t predictedDisplayTime{};
    float seconds = 1.f / 90;
    bool focused{}, valid{}, jump{}, reset{}, recentered{};
    uint32_t buttons{};
};
inline bool validTrackedPose(Pose p) {
    const auto q = p.orientation;
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return finite(p.position) && std::isfinite(n) && std::abs(n - 1) < .001f;
}
} // namespace spidy
