#pragma once
#include "native_rays.hpp"
#include "native_view.hpp"
#include "swing.hpp"
#include "tracking.hpp"
#include <algorithm>
#include <cmath>

namespace spidy {
Input trackedSwingInput(const XrFrame& frame, const Rig& rig);
native_rays::Command controllerAimRays(const Input&, uint64_t serial);
struct GameMotionFrame {
    bool active{}, releaseWebs = true;
    std::int64_t predictedDisplayTime{};
    Input swing{};
    Mat4 head{};
    std::array<Mat4, 2> eyes{};
    std::array<Pose, 2> hands{};
    // The headset and the controllers' OpenXR grip poses, in the world: the
    // player's body follows them (native_body). A hand's grip keeps the
    // identity orientation while it is not tracked.
    Pose headPose{};
    std::array<Pose, 2> grips{};
    std::array<EyeFov, 2> fovs{};
    uint32_t nativeKeys{};
    // The left stick in the stock camera's horizontal axes, -1..1: native
    // walking on the virtual Xbox controller, where nativeKeys has W/A/S/D.
    float walkRight{}, walkForward{};
    Vec3 anchor{}; // Player feet that placed this frame's head, eyes, and hands.
};
// Converts one complete predicted tracking sample into world-space head, eyes,
// hand aims, and physical swing input. The game adapter still owns collision and
// movement, and must only submit while its gameplay/lifetime gate is valid.
class GameTrackingRig {
  public:
    GameMotionFrame update(const XrFrame&, Vec3 playerFeet, Vec3 gameForward, bool gameplay);
    // How far a flick of the right stick turns the player (0: it does not);
    // 30 degrees until set. reset() keeps it.
    void snapTurn(float radians) {
        snap_ = std::isfinite(radians) ? std::clamp(radians, 0.f, 3.1415927f) : .5235988f;
    }
    void reset();

  private:
    Rig rig_{};
    Pose lastHead_{};
    Vec3 lastFeet_{};
    std::int64_t lastTime_{};
    bool initialized_{}, wasActive_{}, snapHeld_{}, pendingRecenter_{};
    float snap_ = .5235988f;
};
} // namespace spidy
