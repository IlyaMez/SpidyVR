#pragma once
#include "math.hpp"
#include <cmath>
#include <cstdint>

// STEADY HUD (the tab's row under EXPERIMENTAL; off by default): the game's
// HUD panel shown by the headset as a quad layer of its own over the eye
// views, instead of drawn by the game into each eye pair.
//
// Drawn into the pair, the panel is part of the picture: when the headset
// shows a pair again (the game slower than the display), the runtime turns it
// to the newer head, and the panel with it: it judders (the user, October 30:
// "die Arme, das UI ... haben einen Stutter"). As a layer of its own it is
// placed every headset frame, where Follow holds it in the room.
//
// The panel's texture (native_hud.cpp) is copied with each new eye pair, on
// the game's queue, in the state the game's barriers left it in
// (native_eye_gpu.cpp); the XR worker copies the newest copy into the layer's
// own image. While the layer shows, the eyes leave the panel out, as with the
// HUD off; when the copies stop the panel goes back into the eyes.
namespace spidy::steady_hud {
// The panel spans what the game's view showed of it: `halfWidth` (a tangent)
// each side at `distance` metres (native_hud::panel), as high as the texture's
// shape says.
struct Quad {
    Pose pose{}; // in the tracking space: the centre, facing the head
    float width{}, height{};
    bool valid{};
};
// In front of the head at `head` (tracking space), the way the panel faces
// (Follow's orientation there; OpenXR axes, -z ahead).
inline Quad quad(Vec3 head, const Quat& facing, float halfWidth, float distance, uint32_t textureWidth,
                 uint32_t textureHeight) {
    Quad q;
    if (!(halfWidth > 0 && distance > 0 && textureWidth && textureHeight) ||
        !std::isfinite(head.x + head.y + head.z + halfWidth + distance) ||
        !std::isfinite(facing.x + facing.y + facing.z + facing.w))
        return q;
    const float n = std::sqrt(facing.x * facing.x + facing.y * facing.y + facing.z * facing.z + facing.w * facing.w);
    if (!(n > .5f && n < 2))
        return q;
    const Quat f{facing.x / n, facing.y / n, facing.z / n, facing.w / n};
    q.pose = {head + f.rotate({0, 0, -distance}), f};
    q.width = 2 * halfWidth * distance;
    q.height = q.width * static_cast<float>(textureHeight) / static_cast<float>(textureWidth);
    q.valid = std::isfinite(q.width + q.height) && q.width < 100 && q.height < 100;
    return q;
}
// A copy of the panel older than this is not shown; nor is the layer, so the
// eyes draw the panel again (the game's frames stopped, a load, a menu).
inline constexpr uint64_t holdMs = 250;
inline bool fresh(uint64_t nowMs, uint64_t copiedMs, bool copied) {
    return copied && nowMs >= copiedMs && nowMs - copiedMs <= holdMs;
}
} // namespace spidy::steady_hud
