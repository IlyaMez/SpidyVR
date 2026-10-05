#pragma once
#include "game_swing.hpp"
#include "game_tracking.hpp"
#include "web_visual.hpp"

namespace spidy {
struct NativeEyeFrame {
    uint64_t serial{};
    GameMotionFrame motion;
    std::array<TrackedEye, 2> trackingEyes;
    std::array<game_swing::WebState, 2> webs;
    std::array<WebTimeline::Hand, 2> webTimes{};
    bool flatScreen{};
    Pose screenPose{};
    float screenAspect = 16.f / 9;
};
class NativeEyeHistory {
  public:
    void remember(const NativeEyeFrame& frame) {
        frames_[frame.serial % frames_.size()] = frame;
    }
    const NativeEyeFrame* find(uint64_t serial, int64_t now) const {
        const auto& frame = frames_[serial % frames_.size()];
        const auto captured = frame.motion.predictedDisplayTime;
        return serial && frame.serial == serial && captured > 0 && now >= captured &&
                       now - captured <= 150000000
                   ? &frame
                   : nullptr;
    }
    void clear() {
        for (auto& frame : frames_)
            frame.serial = 0;
    }

  private:
    std::array<NativeEyeFrame, 256> frames_{};
};
} // namespace spidy
