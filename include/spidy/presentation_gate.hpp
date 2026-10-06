#pragma once
#include "math.hpp"
#include <cstdint>

namespace spidy {
// How long the last eye image stays on show while the game delivers no newer
// one. A frame submitted without an image is a black flash in the headset;
// with 150 ms, stretches where the game rendered the eyes a quarter as often
// flashed black up to 19 times a second (October 5). The runtime reprojects a
// held image by head rotation, as it does for any late frame.
constexpr uint64_t imageHoldMs = 1000;
// Movement requires a recent new image. A session that once displayed an
// image must not keep driving controls after the game stops delivering them,
// but a stutter must not drop the webs either.
constexpr uint64_t controlHoldMs = 500;
inline bool recentPresentation(uint64_t lastNewImageMs, uint64_t nowMs) {
    return lastNewImageMs && nowMs >= lastNewImageMs && nowMs - lastNewImageMs <= controlHoldMs;
}
// What a submitted headset frame showed.
enum class Presentation : uint32_t { none = 0, immersive = 1, flat = 2, gameScreen = 3 };
// Pause menus and hint cards, cutscenes, finishers, death and loading give the
// tracked rig no gameplay camera. The headset then shows the game's presented
// frame on a virtual screen; before the sixth build of October 5 it showed
// nothing, which is black. The last immersive image stays up for
// screenDelayMs first, so a late camera sample does not flash the screen.
constexpr uint64_t screenDelayMs = 250;
class GameScreen {
  public:
    // Once per headset frame: true while the game's camera belongs on the screen.
    bool update(bool gameplay, uint64_t nowMs) {
        entered_ = false;
        if (gameplay) {
            closed_ = showing_ = false;
            return false;
        }
        if (!closed_) {
            closed_ = true;
            closedMs_ = nowMs;
        }
        if (!showing_ && nowMs >= closedMs_ && nowMs - closedMs_ >= screenDelayMs)
            showing_ = entered_ = true;
        return showing_;
    }
    // The update that put the screen up: place it in front of the viewer.
    bool entered() const {
        return entered_;
    }
    // Gameplay ended and the screen is not up yet: keep the last image.
    bool holding() const {
        return closed_ && !showing_;
    }
    void reset() {
        *this = {};
    }

  private:
    uint64_t closedMs_{};
    bool closed_{}, showing_{}, entered_{};
};
// A level virtual screen `distance` ahead of the head, facing it. Only the
// head's heading counts, so a tilted head does not tilt the screen.
inline Pose screenAhead(const Pose& head, float distance = 2.5f) {
    const Vec3 forward = head.orientation.rotate({0, 0, -1}), up = head.orientation.rotate({0, 1, 0});
    Vec3 heading{forward.x, 0, forward.z};
    // Looking straight up or down, the top of the head points along the heading.
    if (length(heading) < .1f)
        heading = forward.y < 0 ? Vec3{up.x, 0, up.z} : Vec3{-up.x, 0, -up.z};
    heading = normalized(heading);
    if (length(heading) < .5f)
        heading = {0, 0, -1};
    return {head.position + heading * distance, Quat::yaw(std::atan2(-heading.x, -heading.z))};
}
} // namespace spidy
