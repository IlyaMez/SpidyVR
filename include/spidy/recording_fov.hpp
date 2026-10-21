#pragma once
#include "native_eye_frame.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

// RECORDING FOV (vr_settings::Values::recordingFov; pull request 21, iplusdvr):
// the monitor's view in immersive VR, for recording. Off, the window mirrors
// the left eye the headset showed, Spidy's drawings and all. An angle has the
// game render its own view again (stereo_probe.cpp, restGameView), which costs
// frame time, and the window shows that view, without Spidy's drawings. The
// view is the engine's active view, whose culling, occlusion and far
// buildings' full models the eyes rely on, so its lens never covers less than
// the head's lens covering both eyes: the angle is the least the window shows
// across, and a taller headset or a wider window gets more. A Quest 3 (54
// degrees out, 44 in, 48 up and down) in a 16:9 window needs about 128.
namespace spidy::recording_fov {
// The tab's choices, degrees across; 0 is off.
inline constexpr int choices[] = {0, 130, 140, 150};
// 0 for off; any other angle within the choices'.
inline int sanitized(int degrees) {
    return degrees <= 0 ? 0 : std::clamp(degrees, choices[1], std::end(choices)[-1]);
}
// The lens (tangents, as native_eyes::headView gives them) of a window
// `aspect` times as wide as it is high that holds `eyes` and shows at least
// `degrees` across: centred, so the window's picture is not stretched.
inline native_eyes::Bounds covering(const native_eyes::Bounds& eyes, int degrees, float aspect) {
    if (!std::isfinite(aspect) || aspect < .2f || aspect > 6.f)
        aspect = 16.f / 9;
    const float across = std::tan(static_cast<float>(sanitized(degrees)) * .00872664626f);
    const float halfHeight =
        std::max({across / aspect, -eyes.top, eyes.bottom, -eyes.left / aspect, eyes.right / aspect});
    return {-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight};
}
// From the VR settings (game_xr.cpp), as they start and whenever they change.
void set(int degrees);
} // namespace spidy::recording_fov
