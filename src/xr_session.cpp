#include "spidy/xr_session.hpp"
#include "spidy/eye_resolution.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace spidy {
namespace {
void xr(XrResult r, const char* where) {
    if (XR_FAILED(r))
        throw std::runtime_error(std::string(where) + " XrResult=" + std::to_string(r));
}
Pose pose(XrPosef p) {
    return {{p.position.x, p.position.y, p.position.z},
            {p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w}};
}
XrPosef identity() {
    XrPosef p{};
    p.orientation.w = 1;
    return p;
}
} // namespace
XrRuntime::~XrRuntime() {
    for (auto& e : eyes_)
        if (e.swapchain)
            xrDestroySwapchain(e.swapchain);
    for (auto s : aimSpaces_)
        if (s)
            xrDestroySpace(s);
    for (auto s : gripSpaces_)
        if (s)
            xrDestroySpace(s);
    if (head_)
        xrDestroySpace(head_);
    if (space_)
        xrDestroySpace(space_);
    if (session_)
        xrDestroySession(session_);
    if (actionSet_)
        xrDestroyActionSet(actionSet_);
    if (instance_)
        xrDestroyInstance(instance_);
}
void XrRuntime::initialize(D3D12Renderer& renderer, bool probeOnly) {
    initialize(
        [&](const XrGraphicsRequirementsD3D12KHR& requirements) {
            renderer.initialize(requirements.adapterLuid, requirements.minFeatureLevel);
            return Graphics{renderer.device(), renderer.queue()};
        },
        probeOnly, false);
}
void XrRuntime::initialize(ID3D12Device* device, ID3D12CommandQueue* queue, unsigned eyeSize) {
    initialize([=](const XrGraphicsRequirementsD3D12KHR&) { return Graphics{device, queue}; }, false, true,
               eyeSize);
}
void XrRuntime::initialize(
    const std::function<Graphics(const XrGraphicsRequirementsD3D12KHR&)>& createGraphics, bool probeOnly,
    bool copyDestination, unsigned eyeSize) {
    if (instance_)
        throw std::logic_error("OpenXR runtime already initialized");
    uint32_t count{};
    xr(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr), "Enumerate XR extensions");
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    xr(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()),
       "Read XR extensions");
    if (std::none_of(extensions.begin(), extensions.end(), [](auto e) {
            return std::strcmp(e.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0;
        }))
        throw std::runtime_error("Runtime does not support OpenXR D3D12");
    const char* enabled[] = {XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName, "Spidy");
    strcpy_s(ci.applicationInfo.engineName, "Spidy");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = enabled;
    xr(xrCreateInstance(&ci, &instance_), "Create XR instance");
    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    xr(xrGetInstanceProperties(instance_, &properties), "XR runtime properties");
    std::cout << "Runtime: " << properties.runtimeName << '\n';
    if (probeOnly) {
        std::cout << "D3D12 extension available. Probe did not start a headset session.\n";
        return;
    }
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    xr(xrGetSystem(instance_, &si, &system_), "Find headset (connect Quest to Virtual Desktop first)");
    PFN_xrGetD3D12GraphicsRequirementsKHR requirementsFn{};
    xr(xrGetInstanceProcAddr(instance_, "xrGetD3D12GraphicsRequirementsKHR",
                             reinterpret_cast<PFN_xrVoidFunction*>(&requirementsFn)),
       "Get D3D12 requirements entry");
    XrGraphicsRequirementsD3D12KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    xr(requirementsFn(instance_, system_, &requirements), "Read XR GPU requirements");
    const auto graphics = createGraphics(requirements);
    if (!graphics.device || !graphics.queue)
        throw std::invalid_argument("OpenXR requires a D3D12 device and queue");
    const auto luid = graphics.device->GetAdapterLuid();
    if (luid.LowPart != requirements.adapterLuid.LowPart ||
        luid.HighPart != requirements.adapterLuid.HighPart)
        throw std::runtime_error("Game GPU does not match OpenXR GPU");
    if (graphics.queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        throw std::runtime_error("OpenXR requires a direct command queue");
    ComPtr<ID3D12Device> queueDevice;
    ComPtr<IUnknown> deviceIdentity, queueDeviceIdentity;
    if (FAILED(graphics.queue->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
        FAILED(graphics.device->QueryInterface(IID_PPV_ARGS(&deviceIdentity))) ||
        FAILED(queueDevice.As(&queueDeviceIdentity)) || deviceIdentity.Get() != queueDeviceIdentity.Get())
        throw std::runtime_error("Graphics queue belongs to another device");
    const D3D_FEATURE_LEVEL level = requirements.minFeatureLevel;
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{1, &level, D3D_FEATURE_LEVEL_11_0};
    if (FAILED(graphics.device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels))) ||
        levels.MaxSupportedFeatureLevel < level)
        throw std::runtime_error("Game GPU lacks OpenXR feature level");
    boundDevice_ = graphics.device;
    boundQueue_ = graphics.queue;
    XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    binding.device = boundDevice_.Get();
    binding.queue = boundQueue_.Get();
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &binding;
    sci.systemId = system_;
    xr(xrCreateSession(instance_, &sci, &session_), "Create XR session");
    xr(xrEnumerateReferenceSpaces(session_, 0, &count, nullptr), "Count reference spaces");
    std::vector<XrReferenceSpaceType> spaces(count);
    xr(xrEnumerateReferenceSpaces(session_, count, &count, spaces.data()), "Read reference spaces");
    XrReferenceSpaceCreateInfo sp{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    sp.poseInReferenceSpace = identity();
    sp.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (std::find(spaces.begin(), spaces.end(), sp.referenceSpaceType) == spaces.end()) {
        sp.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        sp.poseInReferenceSpace.position.y = -1.7f;
        std::cout << "STAGE unavailable: using LOCAL with 1.7 m standing offset.\n";
    }
    referenceType_ = sp.referenceSpaceType;
    xr(xrCreateReferenceSpace(session_, &sp, &space_), "Create world tracking space");
    sp.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    sp.poseInReferenceSpace = identity();
    xr(xrCreateReferenceSpace(session_, &sp, &head_), "Create head space");
    actions();
    xr(xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                        &count, nullptr),
       "Count blend modes");
    std::vector<XrEnvironmentBlendMode> modes(count);
    xr(xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count,
                                        &count, modes.data()),
       "Read blend modes");
    if (std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == modes.end())
        throw std::runtime_error("Opaque VR presentation is required");
    xr(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                         &count, nullptr),
       "Count stereo views");
    if (count != 2)
        throw std::runtime_error("Exactly two stereo views required");
    std::array<XrViewConfigurationView, 2> views{
        {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
    xr(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                                         &count, views.data()),
       "Read stereo dimensions");
    xr(xrEnumerateSwapchainFormats(session_, 0, &count, nullptr), "Count formats");
    std::vector<int64_t> formats(count);
    xr(xrEnumerateSwapchainFormats(session_, count, &count, formats.data()), "Read formats");
    const DXGI_FORMAT preferred[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                                     DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
    for (auto f : preferred)
        if ((!copyDestination || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_UNORM) &&
            std::find(formats.begin(), formats.end(), f) != formats.end()) {
            format_ = f;
            break;
        }
    if (format_ == DXGI_FORMAT_UNKNOWN)
        throw std::runtime_error("No supported color swapchain format");
    for (unsigned i = 0; i < 2; ++i) {
        auto& e = eyes_[i];
        e.width = views[i].recommendedImageRectWidth;
        e.height = views[i].recommendedImageRectHeight;
        if (eyeSize) {
            if (eyeSize < 64 || eyeSize > views[i].maxImageRectWidth || eyeSize > views[i].maxImageRectHeight)
                throw std::invalid_argument("Requested native eye size exceeds runtime limits");
            e.width = e.height = eyeSize;
        }
        if (!validEyeSize(e.width) || !validEyeSize(e.height))
            throw std::runtime_error("Runtime eye dimensions exceed supported 64..4096 range");
        if (copyDestination && i && (e.width != eyes_[0].width || e.height != eyes_[0].height))
            throw std::runtime_error("Native paired views require matching eye dimensions");
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        if (copyDestination)
            sc.usageFlags |= XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
        sc.format = format_;
        sc.sampleCount = 1;
        sc.width = e.width;
        sc.height = e.height;
        sc.faceCount = sc.arraySize = sc.mipCount = 1;
        xr(xrCreateSwapchain(session_, &sc, &e.swapchain), "Create eye swapchain");
        xr(xrEnumerateSwapchainImages(e.swapchain, 0, &count, nullptr), "Count eye images");
        e.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        xr(xrEnumerateSwapchainImages(e.swapchain, count, &count,
                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())),
           "Read eye images");
        std::cout << "Eye " << i << ": " << e.width << 'x' << e.height << " (" << count << " images)\n";
        for (unsigned j = 0; j < e.images.size(); ++j) {
            const auto d = e.images[j].texture->GetDesc();
            std::cout << "Image " << i << '/' << j << ": format=" << d.Format
                      << " requestedFormat=" << format_ << " size=" << d.Width << 'x' << d.Height
                      << " dimension=" << d.Dimension << " array=" << d.DepthOrArraySize
                      << " mips=" << d.MipLevels << " samples=" << d.SampleDesc.Count << '\n';
        }
    }
}
XrPath XrRuntime::path(const char* value) {
    XrPath p{};
    xr(xrStringToPath(instance_, value, &p), value);
    return p;
}
XrAction XrRuntime::action(const char* name, const char* label, XrActionType type, bool perHand) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    strcpy_s(info.actionName, name);
    strcpy_s(info.localizedActionName, label);
    info.actionType = type;
    if (perHand) {
        info.countSubactionPaths = 2;
        info.subactionPaths = handPaths_.data();
    }
    XrAction out{};
    xr(xrCreateAction(actionSet_, &info, &out), name);
    return out;
}
void XrRuntime::actions() {
    handPaths_ = {path("/user/hand/left"), path("/user/hand/right")};
    XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(set.actionSetName, "swinging");
    strcpy_s(set.localizedActionSetName, "Spidy Swinging");
    xr(xrCreateActionSet(instance_, &set, &actionSet_), "Create actions");
    aim_ = action("aim", "Aim web", XR_ACTION_TYPE_POSE_INPUT);
    grip_ = action("hand", "Tracked hand", XR_ACTION_TYPE_POSE_INPUT);
    trigger_ = action("trigger", "Reel web", XR_ACTION_TYPE_FLOAT_INPUT);
    squeeze_ = action("grip", "Shoot and hold web", XR_ACTION_TYPE_FLOAT_INPUT);
    stick_ = action("stick", "Move / turn", XR_ACTION_TYPE_VECTOR2F_INPUT);
    stickClick_ = action("stick_click", "Toggle VR / flat screen", XR_ACTION_TYPE_BOOLEAN_INPUT);
    jump_ = action("jump", "Jump / point launch", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    reset_ = action("reset", "Reset lab position", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    buttonB_ = action("button_b", "Back (menus)", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    buttonX_ = action("button_x", "X (menus)", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    menu_ = action("menu", "Pause menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    haptic_ = action("haptic", "Web feedback", XR_ACTION_TYPE_VIBRATION_OUTPUT);
    for (bool index : {false, true}) {
        std::vector<XrActionSuggestedBinding> bindings;
        for (const char* hand : {"left", "right"}) {
            const std::string prefix = std::string("/user/hand/") + hand;
            auto bind = [&](XrAction a, const char* suffix) {
                bindings.push_back({a, path((prefix + suffix).c_str())});
            };
            bind(aim_, "/input/aim/pose");
            bind(grip_, "/input/grip/pose");
            bind(trigger_, "/input/trigger/value");
            bind(squeeze_, index ? "/input/squeeze/force" : "/input/squeeze/value");
            bind(stick_, "/input/thumbstick");
            bind(stickClick_, "/input/thumbstick/click");
            bind(haptic_, "/output/haptic");
        }
        bindings.push_back({jump_, path("/user/hand/right/input/a/click")});
        bindings.push_back(
            {reset_, path(index ? "/user/hand/left/input/b/click" : "/user/hand/left/input/y/click")});
        bindings.push_back({buttonB_, path("/user/hand/right/input/b/click")});
        bindings.push_back(
            {buttonX_, path(index ? "/user/hand/left/input/a/click" : "/user/hand/left/input/x/click")});
        // Index controllers have no menu button; their system button is the runtime's.
        if (!index)
            bindings.push_back({menu_, path("/user/hand/left/input/menu/click")});
        XrInteractionProfileSuggestedBinding suggest{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggest.interactionProfile = path(index ? "/interaction_profiles/valve/index_controller"
                                                : "/interaction_profiles/oculus/touch_controller");
        suggest.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        suggest.suggestedBindings = bindings.data();
        const XrResult result = xrSuggestInteractionProfileBindings(instance_, &suggest);
        if (result != XR_ERROR_PATH_UNSUPPORTED)
            xr(result, "Suggest controller bindings");
    }
    for (int i = 0; i < 2; ++i) {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.subactionPath = handPaths_[i];
        info.poseInActionSpace = identity();
        info.action = aim_;
        xr(xrCreateActionSpace(session_, &info, &aimSpaces_[i]), "Create aim space");
        info.action = grip_;
        xr(xrCreateActionSpace(session_, &info, &gripSpaces_[i]), "Create grip space");
    }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actionSet_;
    xr(xrAttachSessionActionSets(session_, &attach), "Attach actions");
}
void XrRuntime::poll() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result;
    while ((result = xrPollEvent(instance_, &event)) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            state_ = reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
            if (state_ == XR_SESSION_STATE_READY && !running_) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xr(xrBeginSession(session_, &begin), "Begin session");
                running_ = true;
                previousTime_ = 0;
            }
            if (state_ == XR_SESSION_STATE_STOPPING && running_) {
                xr(xrEndSession(session_), "End session");
                running_ = false;
            }
            if (state_ == XR_SESSION_STATE_EXITING || state_ == XR_SESSION_STATE_LOSS_PENDING)
                exit_ = true;
        }
        if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            exit_ = true;
        if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const auto& change = *reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&event);
            if (change.referenceSpaceType == referenceType_)
                referenceChangeTime_ = change.changeTime;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (result != XR_EVENT_UNAVAILABLE)
        xr(result, "Poll events");
}
bool XrRuntime::locate(XrSpace source, XrTime time, Pose& out) {
    XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
    xr(xrLocateSpace(source, space_, time, &l), "Locate tracked pose");
    constexpr XrSpaceLocationFlags required =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    if ((l.locationFlags & required) != required)
        return false;
    out = pose(l.pose);
    return true;
}
XrFrame XrRuntime::input(XrTime time) {
    XrFrame frame;
    frame.focused = state_ == XR_SESSION_STATE_FOCUSED;
    frame.valid = locate(head_, time, frame.head);
    if (previousTime_)
        frame.seconds = static_cast<float>(static_cast<double>(time - previousTime_) / 1e9);
    previousTime_ = time;
    if (referenceChangeTime_ && time >= referenceChangeTime_ && frame.valid) {
        frame.recentered = true;
        referenceChangeTime_ = 0;
    }
    if (!frame.focused)
        return frame;
    XrActiveActionSet active{actionSet_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    const auto result = xrSyncActions(session_, &sync);
    if (result == XR_SESSION_NOT_FOCUSED) {
        frame.focused = false;
        return frame;
    }
    xr(result, "Sync input");
    for (int i = 0; i < 2; ++i) {
        auto& hand = frame.hands[i];
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.subactionPath = handPaths_[i];
        XrActionStatePose a{XR_TYPE_ACTION_STATE_POSE}, g{XR_TYPE_ACTION_STATE_POSE};
        get.action = aim_;
        xr(xrGetActionStatePose(session_, &get, &a), "Aim state");
        get.action = grip_;
        xr(xrGetActionStatePose(session_, &get, &g), "Grip state");
        hand.valid = a.isActive && g.isActive && locate(aimSpaces_[i], time, hand.aim) &&
                     locate(gripSpaces_[i], time, hand.grip);
        XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};
        get.action = trigger_;
        xr(xrGetActionStateFloat(session_, &get, &f), "Trigger");
        hand.trigger = f.isActive ? f.currentState : 0;
        get.action = squeeze_;
        xr(xrGetActionStateFloat(session_, &get, &f), "Squeeze");
        hand.squeeze = f.isActive ? f.currentState : 0;
        XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};
        get.action = stick_;
        xr(xrGetActionStateVector2f(session_, &get, &v), "Thumbstick");
        if (v.isActive) {
            hand.stickX = v.currentState.x;
            hand.stickY = v.currentState.y;
        }
        XrActionStateBoolean click{XR_TYPE_ACTION_STATE_BOOLEAN};
        get.action = stickClick_;
        xr(xrGetActionStateBoolean(session_, &get, &click), "Thumbstick click");
        hand.stickClick = click.isActive && click.currentState;
    }
    auto boolean = [&](XrAction action) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = action;
        XrActionStateBoolean b{XR_TYPE_ACTION_STATE_BOOLEAN};
        xr(xrGetActionStateBoolean(session_, &get, &b), "Button");
        return b.isActive && b.currentState;
    };
    frame.jump = boolean(jump_);
    frame.reset = boolean(reset_);
    frame.buttons = (frame.jump ? buttonA : 0u) | (boolean(buttonB_) ? buttonB : 0u) |
                    (boolean(buttonX_) ? buttonX : 0u) | (frame.reset ? buttonY : 0u) |
                    (boolean(menu_) ? buttonMenu : 0u);
    return frame;
}
void XrRuntime::haptic(int hand, float strength) {
    if (hand < 0 || hand > 1 || state_ != XR_SESSION_STATE_FOCUSED)
        return;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = haptic_;
    info.subactionPath = handPaths_[hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = std::clamp(strength, 0.f, 1.f);
    vibration.duration = 25000000;
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    xrApplyHapticFeedback(session_, &info, reinterpret_cast<XrHapticBaseHeader*>(&vibration));
}
bool XrRuntime::frame(const std::function<void(const XrFrame&)>& update,
                      const std::function<void(unsigned, const XrView&, ID3D12Resource*, DXGI_FORMAT,
                                               unsigned, unsigned)>& draw) {
    return frameStereo(
        [&](const XrFrame& input) {
            update(input);
            return true;
        },
        [&](const std::array<EyeTarget, 2>& targets) {
            for (unsigned i = 0; i < 2; ++i) {
                const auto& t = targets[i];
                draw(i, t.view, t.texture, t.format, t.width, t.height);
            }
            return true;
        });
}
bool XrRuntime::frameStereo(const std::function<bool(const XrFrame&)>& prepare,
                            const std::function<bool(std::array<EyeTarget, 2>&)>& draw) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto elapsed = [](auto t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); };
    frameTiming_ = {};
    lastFrameSubmitted_ = false;
    poll();
    if (exit_)
        return false;
    if (!running_) {
        XrFrame inactive;
        prepare(inactive);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return true;
    }
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    auto phase = Clock::now();
    xr(xrWaitFrame(session_, &wait, &state), "Wait frame");
    frameTiming_.wait = elapsed(phase);
    frameTiming_.period = state.predictedDisplayPeriod * 1e-6;
    phase = Clock::now();
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    xr(xrBeginFrame(session_, &begin), "Begin frame");
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = state.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool endCalled = false;
    try {
        auto tracked = input(state.predictedDisplayTime);
        tracked.focused = tracked.focused && state.shouldRender;
        std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
        uint32_t count{};
        XrViewState validity{XR_TYPE_VIEW_STATE};
        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = state.predictedDisplayTime;
        locateInfo.space = space_;
        xr(xrLocateViews(session_, &locateInfo, &validity, 2, &count, views.data()), "Locate stereo views");
        constexpr XrViewStateFlags required =
            XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        tracked.valid = tracked.valid && count == 2 && (validity.viewStateFlags & required) == required;
        tracked.predictedDisplayTime = state.predictedDisplayTime;
        if (tracked.valid)
            for (unsigned i = 0; i < 2; ++i) {
                const auto& fov = views[i].fov;
                tracked.eyes[i] = {pose(views[i].pose),
                                   {fov.angleLeft, fov.angleRight, fov.angleDown, fov.angleUp},
                                   eyes_[i].width,
                                   eyes_[i].height};
            }
        frameTiming_.tracking = elapsed(phase);
        phase = Clock::now();
        const bool ready = prepare(tracked); // one pose batch for the complete pair
        frameTiming_.prepare = elapsed(phase);
        bool drawn = false;
        std::array<XrCompositionLayerProjectionView, 2> projectionViews{
            {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = space_;
        layer.viewCount = 2;
        layer.views = projectionViews.data();
        if (state.shouldRender && tracked.valid && ready) {
            std::array<EyeTarget, 2> targets;
            std::array<bool, 2> waited{};
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            try {
                phase = Clock::now();
                for (unsigned i = 0; i < 2; ++i) {
                    auto& eye = eyes_[i];
                    uint32_t image{};
                    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    xr(xrAcquireSwapchainImage(eye.swapchain, &acquire, &image), "Acquire eye");
                    XrSwapchainImageWaitInfo iw{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    iw.timeout = XR_INFINITE_DURATION;
                    xr(xrWaitSwapchainImage(eye.swapchain, &iw), "Wait eye image");
                    waited[i] = true;
                    targets[i] = {views[i], eye.images.at(image).texture, format_, eye.width, eye.height};
                    auto& pv = projectionViews[i];
                    pv.subImage.swapchain = eye.swapchain;
                    pv.subImage.imageRect.extent = {static_cast<int32_t>(eye.width),
                                                    static_cast<int32_t>(eye.height)};
                }
                frameTiming_.acquire = elapsed(phase);
                drawn = draw(targets);
                for (unsigned i = 0; i < 2; ++i) {
                    projectionViews[i].pose = targets[i].view.pose;
                    projectionViews[i].fov = targets[i].view.fov;
                }
                phase = Clock::now();
                for (unsigned i = 0; i < 2; ++i) {
                    const auto result = xrReleaseSwapchainImage(eyes_[i].swapchain, &release);
                    waited[i] = false;
                    xr(result, "Release eye");
                }
                frameTiming_.release = elapsed(phase);
            } catch (...) {
                for (unsigned i = 0; i < 2; ++i)
                    if (waited[i])
                        xrReleaseSwapchainImage(eyes_[i].swapchain, &release);
                throw;
            }
        }
        XrCompositionLayerQuad screen{XR_TYPE_COMPOSITION_LAYER_QUAD};
        screen.space = space_;
        screen.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        screen.subImage = projectionViews[0].subImage;
        screen.pose = {{screenPose_.orientation.x, screenPose_.orientation.y, screenPose_.orientation.z,
                        screenPose_.orientation.w},
                       {screenPose_.position.x, screenPose_.position.y, screenPose_.position.z}};
        screen.size = {3.2f, 3.2f / screenAspect_};
        const XrCompositionLayerBaseHeader* layers[] = {
            flatScreen_ ? reinterpret_cast<const XrCompositionLayerBaseHeader*>(&screen)
                        : reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
        if (state.shouldRender && tracked.valid && ready && drawn) {
            end.layerCount = 1;
            end.layers = layers;
        }
        endCalled = true;
        phase = Clock::now();
        xr(xrEndFrame(session_, &end), "Submit stereo frame");
        frameTiming_.end = elapsed(phase);
        frameTiming_.total = elapsed(start);
        lastFrameSubmitted_ = end.layerCount != 0;
    } catch (...) {
        if (!endCalled) {
            end.layerCount = 0;
            end.layers = nullptr;
            xrEndFrame(session_, &end);
        }
        throw;
    }
    return true;
}
} // namespace spidy
