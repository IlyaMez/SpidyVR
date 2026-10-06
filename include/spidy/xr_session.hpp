#pragma once
#include "d3d12_renderer.hpp"
#include "tracking.hpp"
#include "xr_timing.hpp"
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
    void initialize(ID3D12Device* device, ID3D12CommandQueue* queue, unsigned eyeSize = 0);
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
    const XrFrameTiming& lastFrameTiming() const {
        return frameTiming_;
    }
    void haptic(int hand, float strength);
    std::array<unsigned, 2> eyeDimensions() const {
        return {eyes_[0].width, eyes_[0].height};
    }
    void presentation(bool flat, Pose screenPose = {}, float aspect = 16.f / 9) {
        flatScreen_ = flat;
        screenPose_ = screenPose;
        screenAspect_ = std::isfinite(aspect) && aspect > .2f && aspect < 5 ? aspect : 16.f / 9;
    }

  private:
    struct Graphics {
        ID3D12Device* device;
        ID3D12CommandQueue* queue;
    };
    void initialize(const std::function<Graphics(const XrGraphicsRequirementsD3D12KHR&)>& graphics,
                    bool probeOnly, bool copyDestination, unsigned eyeSize = 0);
    void actions();
    XrPath path(const char* value);
    XrAction action(const char* name, const char* label, XrActionType type, bool perHand = true);
    void poll();
    XrFrame input(XrTime predicted);
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
    std::array<XrPath, 2> handPaths_{};
    std::array<XrSpace, 2> aimSpaces_{}, gripSpaces_{};
    struct Eye {
        XrSwapchain swapchain{};
        unsigned width{}, height{};
        std::vector<XrSwapchainImageD3D12KHR> images;
    };
    std::array<Eye, 2> eyes_{};
    DXGI_FORMAT format_{};
    XrTime previousTime_{};
    bool running_{}, exit_{};
    bool lastFrameSubmitted_{};
    bool flatScreen_{};
    Pose screenPose_{};
    float screenAspect_ = 16.f / 9;
    XrFrameTiming frameTiming_{};
    XrTime referenceChangeTime_{};
    XrReferenceSpaceType referenceType_ = XR_REFERENCE_SPACE_TYPE_STAGE;
    ComPtr<ID3D12Device> boundDevice_;
    ComPtr<ID3D12CommandQueue> boundQueue_;
};
} // namespace spidy
