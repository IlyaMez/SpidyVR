#pragma once
#include "math.hpp"
#include <cstdint>

namespace spidy {
// When the session report takes a copy of the left eye image as submitted to
// the headset. A routine copy every few seconds shows what was displayed; a
// fast swing holding a game web is copied sooner, because that is where a web
// and the hand holding it can separate.
class EyeSnapshotSchedule {
  public:
    static constexpr uint64_t routineMs = 5000, eventMs = 1500;
    static constexpr float eventSpeed = 15;
    static bool event(float speed, bool holdingWeb) {
        return holdingWeb && std::isfinite(speed) && speed >= eventSpeed;
    }
    bool due(uint64_t nowMs, float speed, bool holdingWeb) {
        if (taken_ && nowMs - last_ < (event(speed, holdingWeb) ? eventMs : routineMs))
            return false;
        last_ = nowMs;
        taken_ = true;
        return true;
    }

  private:
    uint64_t last_{};
    bool taken_{};
};
// Pixel of a world point in an eye image whose row-major view-projection maps
// column vectors to D3D clip space. False for points behind the eye.
inline bool eyePixel(const Mat4& vp, Vec3 p, unsigned width, unsigned height, float& x, float& y) {
    const float cx = vp[0] * p.x + vp[1] * p.y + vp[2] * p.z + vp[3];
    const float cy = vp[4] * p.x + vp[5] * p.y + vp[6] * p.z + vp[7];
    const float cw = vp[12] * p.x + vp[13] * p.y + vp[14] * p.z + vp[15];
    if (!std::isfinite(cx) || !std::isfinite(cy) || !(cw > 1e-4f))
        return false;
    x = (cx / cw * .5f + .5f) * static_cast<float>(width);
    y = (.5f - cy / cw * .5f) * static_cast<float>(height);
    return std::isfinite(x) && std::isfinite(y);
}
// The newest completed copy, read by the session tool from the game process.
// `pixels` are RGBA8 rows of `rowPitch` bytes that stay unchanged until the
// next copy; `count` grows with each. Positions are left-eye pixels, negative
// when the hand is untracked or holds no game web.
struct EyeSnapshot {
    uint32_t magic = 0x53455353, version = 1, bytes = sizeof(EyeSnapshot), flags{}; // bit per hand: game web
    int64_t sequence{};
    uint64_t count{}, pixels{}, generation{}, serial{};
    uint32_t width{}, height{}, rowPitch{}, flatScreen{};
    float speed{}, webGap = -1; // player speed m/s; shooter-to-rope distance in metres
    float wrist[2][2]{{-1, -1}, {-1, -1}}, ropeStart[2][2]{{-1, -1}, {-1, -1}};
};
static_assert(sizeof(EyeSnapshot) == 112);
} // namespace spidy
