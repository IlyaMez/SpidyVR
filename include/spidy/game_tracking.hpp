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
    // How far the head, eyes and hands stand off the wall or ceiling the game
    // holds the player on (zero while it stands), and the head's height over
    // that surface as placed from the feet, before and after (while onSurface).
    Vec3 standOff{};
    bool onSurface{};
    float surfaceHeight{}, surfaceClearance{};
};
// The game sticks the player to walls and ceilings (its wall crawl): the
// feet on the surface, the actor's up along its normal. A head placed upright
// from those feet is on the wall, or inside it. It stands off the surface
// instead: wallClearance once the actor has turned onto it, then never closer
// than minWallClearance; a head moved back stays where it went.
constexpr float wallClearance = .5f, minWallClearance = .25f;
// A surface: the actor's up more than 45 degrees from the world's; standing
// again within 35 degrees.
constexpr float surfaceEnterCos = .7071068f, surfaceLeaveCos = .819152f;
// The stand-off approaches its target by 63% in standOffSeconds; the first
// surfaceSettleSeconds on a surface are the actor turning onto it.
constexpr float standOffSeconds = .06f, surfaceSettleSeconds = .5f;
// Converts one complete predicted tracking sample into world-space head, eyes,
// hand aims, and physical swing input. The game adapter still owns collision and
// movement, and must only submit while its gameplay/lifetime gate is valid.
class GameTrackingRig {
  public:
    // surfaceUp: the up of the player's actor, the world's while it stands.
    GameMotionFrame update(const XrFrame&, Vec3 playerFeet, Vec3 gameForward, bool gameplay,
                           Vec3 surfaceUp = {0, 1, 0});
    // How far a flick of the right stick turns the player (0: it does not);
    // 30 degrees until set. reset() keeps it.
    void snapTurn(float radians) {
        snap_ = std::isfinite(radians) ? std::clamp(radians, 0.f, 3.1415927f) : .5235988f;
    }
    // How fast the right stick turns the player while held over, radians a
    // second at full tilt (slower tilted less); 0 until set: it snap turns
    // instead. reset() keeps it.
    void smoothTurn(float radiansPerSecond) {
        smooth_ = std::isfinite(radiansPerSecond) ? std::clamp(radiansPerSecond, 0.f, 6.2831853f) : 0.f;
    }
    void reset();

  private:
    Rig rig_{};
    Pose lastHead_{};
    Vec3 lastFeet_{}, standOff_{};
    std::int64_t lastTime_{};
    bool initialized_{}, wasActive_{}, turnHeld_{}, pendingRecenter_{}, onSurface_{};
    float snap_ = .5235988f, smooth_{};
    // The stand-off wanted along the surface's normal, and the time on it.
    float surfaceDepth_{}, surfaceSeconds_{};
};
} // namespace spidy
