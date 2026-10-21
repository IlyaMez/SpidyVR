#pragma once
#include "native_eye_frame.hpp"
#include <algorithm>
#include <cmath>

namespace spidy::spectator_fov {
// Zero keeps the existing left-eye mirror and its overlays. On, the active
// game view renders again; its recording lens must still cover BOTH eyes.
inline constexpr int choices[] = {0, 80, 90, 100, 110, 120, 130, 140, 150};
inline native_eyes::Bounds covering(const native_eyes::Bounds& eyes, int degrees, float aspect) {
    if (!std::isfinite(aspect) || aspect < .2f || aspect > 6.f)
        aspect = 16.f / 9;
    const float requested = std::tan(static_cast<float>(std::clamp(degrees, 80, 150)) * .00872664626f);
    const float halfHeight = std::max({requested / aspect, -eyes.top, eyes.bottom,
                                      -eyes.left / aspect, eyes.right / aspect});
    const float halfWidth = halfHeight * aspect;
    return {-halfWidth, halfWidth, -halfHeight, halfHeight};
}
// The XR worker publishes a sanitized setting; the render thread only reads it.
void set(int degrees);
}
