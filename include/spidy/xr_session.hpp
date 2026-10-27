#pragma once
#include "controller_layout.hpp"
#include "d3d12_renderer.hpp"
#include "tracking.hpp"
#include "xr_timing.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace spidy {
class XrRuntime {
  public:
    ~XrRuntime();
    void initialize(D3D12Renderer& renderer, bool probeOnly = false);
    // The game adapter supplies its actual device and direct queue. The runtime
    // validates the OpenXR GPU/feature requirements and retains COM references.
    // eyeSize: a square size instead of the runtime's recommendation; otherwise
    // renderScale percent of it per side (eye_resolution.hpp).
    void initialize(ID3D12Device* device, ID3D12CommandQueue* queue, unsigned eyeSize = 0,
                    unsigned renderScale = 100);
    bool frame(const std::function<void(const XrFrame&)>& update,
               const std::function<void(unsigned, const XrView&, ID3D12Resource*, DXGI_FORMAT, unsigned,
                                        unsigned)>& draw);
    struct EyeTarget {
        XrView view{XR_TYPE_VIEW};
        ID3D12Resource* texture{};
        DXGI_FORMAT format{};
        unsigned width{}, height{};
    };
    // Acquire both targets before drawing and release them after one paired
    // submission. Returning false from prepare submits no projection layer;
    // an asynchronous game renderer can reject a missing or stale eye pair.
    // draw may replace view pose/FOV with those belonging to a cached rendered
    // pair. Projection layers must describe the images' actual render poses.
    bool frameStereo(const std::function<bool(const XrFrame&)>& prepare,
                     const std::function<bool(std::array<EyeTarget, 2>&)>& draw);
    bool lastFrameSubmitted() const {
        return lastFrameSubmitted_;
    }
    // Diagnostic only: why the last frameStereo call did or didn't submit a
    // projection layer, so a healthy native pipeline (a valid, drawn pair)
    // can be told apart from OpenXR itself declining to render the frame.
    struct LastGate {
        bool shouldRender{}, trackedValid{}, ready{}, drawn{};
    };
    LastGate lastGate() const {
        return lastGate_;
    }
    const XrFrameTiming& lastFrameTiming() const {
        return frameTiming_;
    }
    // One pulse: 25 ms unless `seconds` says longer (up to half a second: a
    // landed blow's kick).
    void haptic(int hand, float strength, float seconds = .025f);
    // Scales every haptic pulse: 0 none, 1 as asked (the VR settings' controller vibration).
    void hapticStrength(float scale) {
        hapticScale_ = std::isfinite(scale) ? std::clamp(scale, 0.f, 1.f) : 1.f;
    }
    std::array<unsigned, 2> eyeDimensions() const {
        return {eyes_[0].width, eyes_[0].height};
    }
    // What the runtime recommended, which the render scale multiplies.
    std::array<unsigned, 2> recommendedDimensions() const {
        return {eyes_[0].recommendedWidth, eyes_[0].recommendedHeight};
    }
    // The layout the runtime uses for each hand's controller now (left,
    // right): one of Spidy's (controller_layout.hpp) or one of its own. And
    // the layouts it took Spidy's controls for, a bit each (1 << ControllerKind).
    std::array<ControllerKind, 2> controllers() const {
        return controllers_;
    }
    uint32_t layoutsTaken() const {
        return layouts_;
    }
    // Eye tracking (XR_EXT_eye_gaze_interaction): none, the runtime does not
    // offer it; noTracker, it does, but not for this headset (a Quest 3, say),
    // or it took no binding for the eyes; tracker, the headset has one. Gaze
    // is read only while wanted (EYE AIM on): XrFrame::gaze.
    enum class EyeTracking : uint32_t { none, noTracker, tracker };
    EyeTracking eyeTracking() const {
        return eyeTracking_;
    }
    void wantGaze(bool on) {
        gazeWanted_ = on;
    }
    // width: the screen's width in metres. stereo: the screen shows the left
    // eye's image to the left eye and the right one's to the right; without
    // it both eyes see the left one's.
    void presentation(bool flat, Pose screenPose = {}, float aspect = 16.f / 9, float width = 3.2f,
                      bool stereo = false) {
        flatScreen_ = flat;
        stereoScreen_ = flat && stereo;
        screenPose_ = screenPose;
        screenAspect_ = std::isfinite(aspect) && aspect > .2f && aspect < 5 ? aspect : 16.f / 9;
        screenWidth_ = std::isfinite(width) && width > .5f && width < 10 ? width : 3.2f;
    }

  private:
    struct Graphics {
        ID3D12Device* device;
        ID3D12CommandQueue* queue;
    };
    void initialize(const std::function<Graphics(const XrGraphicsRequirementsD3D12KHR&)>& graphics,
                    bool probeOnly, bool copyDestination, unsigned eyeSize = 0, unsigned renderScale = 100);
    void actions();
    void createActions();
    void createGaze();
    bool offered(const ControllerLayout& layout) const;
    void suggest(const ControllerLayout& layout);
    XrAction actionFor(ControllerInput input) const;
    void readControllers();
    XrPath path(const char* value);
    XrAction action(const char* name, const char* label, XrActionType type, bool perHand = true);
    void poll();
    XrFrame input(XrTime predicted);
    bool sharedGaze(const Pose& head, Pose& out);
    bool locate(XrSpace space, XrTime time, Pose& out);
    XrInstance instance_{};
    XrSystemId system_{};
    XrSession session_{};
    XrSpace space_{}, head_{};
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    XrActionSet actionSet_{};
    XrAction aim_{}, grip_{}, trigger_{}, squeeze_{}, stick_{}, stickClick_{}, jump_{}, reset_{}, haptic_{};
    // The other face buttons and the menu button, for the game's menus.
    XrAction buttonB_{}, buttonX_{}, menu_{};
    // The Steam Frame's own: its View button and its D-pad (up, down, left,
    // right). None where the runtime does not know that controller.
    XrAction view_{};
    std::array<XrAction, 4> dpad_{};
    // The eyes' gaze, in an action set of its own that is synced only while
    // gaze is wanted: with EYE AIM off the runtime is never asked for it.
    XrActionSet gazeSet_{};
    XrAction gazeAction_{};
    XrSpace gazeSpace_{};
    EyeTracking eyeTracking_{};
    bool gazeOffered_{}, gazeWanted_{};
    // Virtual Desktop's runtime: its Streamer's shared eyes where it gives
    // no gaze (vd_eyes.hpp), mapped the first time they are wanted.
    bool virtualDesktop_{};
    void* vdMapping_{};
    const uint8_t* vdState_{};
    // The extensions this instance has on, the profiles of the layouts the
    // runtime took, and what controllers() and layoutsTaken() give.
    std::vector<const char*> extensions_;
    std::vector<std::pair<XrPath, ControllerKind>> profiles_;
    std::array<ControllerKind, 2> controllers_{};
    uint32_t layouts_{}, inputs_{};
    std::array<XrPath, 2> handPaths_{};
    std::array<XrSpace, 2> aimSpaces_{}, gripSpaces_{};
    struct Eye {
        XrSwapchain swapchain{};
        unsigned width{}, height{}, recommendedWidth{}, recommendedHeight{};
        std::vector<XrSwapchainImageD3D12KHR> images;
    };
    std::array<Eye, 2> eyes_{};
    DXGI_FORMAT format_{};
    XrTime previousTime_{};
    bool running_{}, exit_{};
    bool lastFrameSubmitted_{};
    LastGate lastGate_{};
    bool flatScreen_{}, stereoScreen_{};
    Pose screenPose_{};
    float screenAspect_ = 16.f / 9, screenWidth_ = 3.2f;
    float hapticScale_ = 1;
    XrFrameTiming frameTiming_{};
    XrTime referenceChangeTime_{};
    XrReferenceSpaceType referenceType_ = XR_REFERENCE_SPACE_TYPE_STAGE;
    ComPtr<ID3D12Device> boundDevice_;
    ComPtr<ID3D12CommandQueue> boundQueue_;
};
} // namespace spidy
