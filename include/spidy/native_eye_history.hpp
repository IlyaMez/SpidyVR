#pragma once
#include "body_calibration.hpp"
#include "gadget_wheel.hpp"
#include "game_swing.hpp"
#include "game_tracking.hpp"
#include "offer_pointing.hpp"
#include "phone.hpp"
#include "presentation_gate.hpp"
#include "web_visual.hpp"

namespace spidy {
struct NativeEyeFrame {
    uint64_t serial{};
    GameMotionFrame motion;
    std::array<TrackedEye, 2> trackingEyes;
    std::array<game_swing::WebState, 2> webs;
    std::array<WebTimeline::Hand, 2> webTimes{};
    game_swing::WebFlightData webFlight;WebFlightVisual flightVisual[2];
    // The game rope that draws each hand's web (WebRopes; -1: none). One web
    // in both hands: the hand its strand comes to first, the other hand's
    // part running from wrist to wrist; -1 otherwise.
    std::array<int, 2> webSlots{-1, -1};
    int strandUpper = -1;
    // What each hand's grip press would do (game_swing::AimKind none while
    // the aim markers are off).
    std::array<game_swing::Aim, 2> aims{};
    // The game's interaction each hand points at on Spidy's wall (handle 0:
    // none), drawn instead of its aim marker.
    std::array<offer_pointing::Pointed, 2> offers{};
    // The T-pose calibration's panel and controller rings (phase idle: none).
    body_calibration::View calibration{};
    // Slow motion: the image's recolouring and the focus meter (none at rest).
    SlowMotionView slow{};
    // The pad layer is on (game_pad::PadLayer): its sign over the right wrist.
    bool padLayer{};
    // The gadget wheel, open while X is held (gadget_wheel.hpp).
    GadgetWheelView gadgetWheel{};
    // The phone, open from a pocket draw until B: whether the image has it,
    // and the image's way into its world (phone.hpp).
    PhoneImage phone{};
    // How Spidy's marks look in this image (the tab's UI STYLE).
    UiStyle uiStyle{};
    bool combatVisuals{}; // overlay choice when this image was submitted
    bool flatScreen{};
    // The game screen in stereo (eye command 3): the pair goes on the screen,
    // the left image for the left eye and the right one for the right.
    bool stereoScreen{};
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
                       now - captured <= static_cast<int64_t>(imageHoldMs) * 1000000
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
