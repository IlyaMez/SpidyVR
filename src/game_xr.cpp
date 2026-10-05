// Bounded first-person OpenXR integration using the game's two native views.
#include "spidy/native_appearance.hpp"
// The input bridge remains a separately verified, separately stoppable module.
#include "spidy/eye_resolution.hpp"
#include "spidy/game_bridge_protocol.hpp"
#include "spidy/game_swing.hpp"
#include "spidy/game_tracking.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_eye_gpu.hpp"
#include "spidy/native_eye_history.hpp"
#include "spidy/native_webs.hpp"
#include "spidy/presentation_gate.hpp"
#include "spidy/vr_shortcut.hpp"
#include "spidy/xr_session.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <windows.h>
using namespace spidy;
struct XrConfig {
    uint32_t magic = 0x53585243, version = 5, bytes = sizeof(XrConfig), pid{};
    uint64_t base{}, queue{}, bridgeModule{}, rayModule{}, motionModule{}, record{}, mover{};
    uint32_t durationMs = 20000, eyeSize = defaultEyeSize;
    float swingSpeed = 32;
    // bit 0: CPU eye capture; bit 1: Spidy's overlay webs instead of the game's;
    // bit 2: keep the stock camera as the game's active (monitor) view in VR
    uint32_t options{};
};
struct XrData {
    uint32_t magic = 0x53585244, version = 3, bytes = sizeof(XrData), status{};
    int64_t sequence{};
    uint64_t frames{}, tracked{}, submitted{}, dropped{}, leftHands{}, rightHands{}, serial{}, generation{};
    uint32_t nativeKeys{}, error{};
    float head[16]{}, hands[2][16]{};
    char message[256]{};
    uint64_t uniqueSubmitted{}, reusedSubmitted{};
    uint32_t flatScreen{}, toggles{}, eyeWidth{}, eyeHeight{};
};
static_assert(sizeof(XrConfig) == 88 && sizeof(XrData) == 576);
extern "C" {
__declspec(dllexport) XrData SpidyXrData;
__declspec(dllexport) XrTimingData SpidyXrTimingData;
DWORD WINAPI SpidyStart(void*);
DWORD WINAPI SpidyStop(void*);
DWORD WINAPI SpidySetEyes(void*);
float SpidyFlatAspect();
}
namespace {
using BridgeCall = DWORD(WINAPI*)(void*);
XrConfig config;
BridgeCall submitInput{}, sampleBridge{}, stopBridge{};
BridgeCall startRays{}, submitRays{}, stopRays{};
BridgeCall startSwing{}, submitSwing{}, sampleSwing{}, stopSwing{};
std::mutex lifecycle, telemetry;
HANDLE worker{};
std::atomic<bool> stopRequested{};
std::atomic<uint64_t> keepAliveDeadline{};
void message(uint32_t status, uint32_t error, const char* text) {
    std::lock_guard lock(telemetry);
    InterlockedIncrement64(&SpidyXrData.sequence);
    SpidyXrData.status = status;
    SpidyXrData.error = error;
    strncpy_s(SpidyXrData.message, text, _TRUNCATE);
    InterlockedIncrement64(&SpidyXrData.sequence);
}
bool read(uintptr_t p, void* out, size_t bytes) {
    __try {
        if (p < 0x10000)
            return false;
        std::memcpy(out, reinterpret_cast<void*>(p), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void check(DWORD code, const char* operation) {
    if (code)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(code));
}
struct RuntimeSelection {
    std::wstring previous;
    bool present{};
    RuntimeSelection() {
        const DWORD size = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr, 0);
        present = size != 0;
        if (size) {
            previous.resize(size);
            GetEnvironmentVariableW(L"XR_RUNTIME_JSON", previous.data(), size);
            previous.resize(size - 1);
        }
        constexpr auto manifest =
            L"C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr.json";
        if (GetFileAttributesW(manifest) == INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("Virtual Desktop OpenXR runtime not found");
        if (!SetEnvironmentVariableW(L"XR_RUNTIME_JSON", manifest))
            throw std::runtime_error("Could not select VDXR for this process");
    }
    ~RuntimeSelection() {
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", present ? previous.c_str() : nullptr);
    }
};
DWORD WINAPI run(void*) {
    bool nativeStarted = false, gpuStarted = false, raysStarted = false, swingStarted = false, websStarted = false;
    uint64_t serial = 1, nativeDeadline{};
    uint64_t firstImageDeadline{}, lastPresentedMs{};
    bool presented{};
    uint32_t error{};
    std::string failure;
    try {
        auto queue = reinterpret_cast<ID3D12CommandQueue*>(config.queue);
        ComPtr<ID3D12Device> device;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))))
            throw std::runtime_error("Game graphics device unavailable");
        XrRuntime runtime;
        {
            RuntimeSelection selected;
            runtime.initialize(device.Get(), queue, config.eyeSize);
        }
        D3D12Renderer overlay;
        overlay.initialize(device.Get(), queue);
        message(2, 0, "Waiting for headset tracking and gameplay");
        GameTrackingRig rig;
        GameMotionFrame motion;
        NativeEyeHistory history;
        VrShortcut shortcut;
        bool flatScreen{};
        Pose screenPose{};
        const auto dimensions = runtime.eyeDimensions();
        const auto moduleDuration = config.durationMs ? config.durationMs + 3000 : 0;
        {
            std::lock_guard lock(telemetry);
            InterlockedIncrement64(&SpidyXrData.sequence);
            SpidyXrData.eyeWidth = dimensions[0];
            SpidyXrData.eyeHeight = dimensions[1];
            InterlockedIncrement64(&SpidyXrData.sequence);
        }
        game_swing::Data swingState;
        WebTimeline webTimes;
        bool attachedBefore[2]{};
        uint64_t zipBefore{};
        uint64_t submittedFrames{};
        uint64_t previousPresentedGeneration{};
        const auto focusDeadline = GetTickCount64() + 30000;
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        auto controls = [&](uint32_t keys) {
            bridge::Control c;
            c.modes = bridge::Mode::input;
            c.serial = ++serial;
            c.leaseMs = 250;
            c.keys = keys;
            check(submitInput(&c), "Controller input");
        };
        while (!stopRequested && (!nativeDeadline || GetTickCount64() < nativeDeadline)) {
            if (!config.durationMs && GetTickCount64() >= keepAliveDeadline)
                break;
            double copyMs{}, overlayMs{};
            if (!nativeDeadline && GetTickCount64() >= focusDeadline)
                throw std::runtime_error("No focused headset gameplay within 30 seconds");
            if (motion.active && firstImageDeadline && !presented && GetTickCount64() >= firstImageDeadline)
                throw std::runtime_error("No native eye images reached OpenXR within 3 seconds");
            if (motion.active && lastPresentedMs && GetTickCount64() - lastPresentedMs >= 3000)
                throw std::runtime_error("Native eye presentation stalled for 3 seconds");
            const bool active = runtime.frameStereo(
                [&](const XrFrame& frame) {
                    bridge::Data game{};
                    check(sampleBridge(&game), "Gameplay sample");
                    LARGE_INTEGER now{};
                    QueryPerformanceCounter(&now);
                    Mat4 body{}, camera{};
                    std::memcpy(body.data(), game.player, 64);
                    std::memcpy(camera.data(), game.before, 64);
                    const bool gameplay =
                        game.state == 1 && game.qpc && now.QuadPart >= static_cast<int64_t>(game.qpc) &&
                        static_cast<double>(now.QuadPart - game.qpc) / frequency.QuadPart < .1 &&
                        native_view::validPose(body) && native_view::validPose(camera);
                    if (shortcut.update(frame.focused && frame.valid && gameplay, frame.hands[0].stickClick,
                                        frame.hands[1].stickClick)) {
                        flatScreen = !flatScreen;
                        rig.reset();
                        history.clear();
                        lastPresentedMs = 0;
                        presented = false;
                        firstImageDeadline = GetTickCount64() + 3000;
                        screenPose = frame.head;
                        screenPose.position += frame.head.orientation.rotate({0, 0, -2.5f});
                        runtime.haptic(0, .35f);
                        runtime.haptic(1, .35f);
                        std::lock_guard lock(telemetry);
                        InterlockedIncrement64(&SpidyXrData.sequence);
                        ++SpidyXrData.toggles;
                        SpidyXrData.flatScreen = flatScreen;
                        InterlockedIncrement64(&SpidyXrData.sequence);
                    }
                    if (flatScreen && frame.recentered) {
                        screenPose = frame.head;
                        screenPose.position += frame.head.orientation.rotate({0, 0, -2.5f});
                    }
                    motion = rig.update(frame, {body[12], body[13], body[14]},
                                        {camera[8], camera[9], camera[10]}, gameplay);
                    if (!motion.active || motion.releaseWebs)
                        history.clear();
                    if (!motion.active) {
                        lastPresentedMs = firstImageDeadline = 0;
                        presented = false;
                    } else if (nativeStarted && !firstImageDeadline && !presented) {
                        firstImageDeadline = GetTickCount64() + 3000;
                    }
                    const bool controlsVisible = recentPresentation(lastPresentedMs, GetTickCount64());
                    if (motion.active && !raysStarted) {
                        native_rays::Config rays;
                        rays.pid = config.pid;
                        rays.base = config.base;
                        rays.durationMs = moduleDuration;
                        check(startRays(&rays), "Start controller world rays");
                        raysStarted = true;
                    }
                    if (raysStarted) {
                        auto input = motion.swing;
                        input.focused = motion.active && !flatScreen;
                        auto aim = controllerAimRays(input, ++serial);
                        check(submitRays(&aim), "Controller world rays");
                    }
                    if (motion.active && !flatScreen && controlsVisible && !swingStarted) {
                        game_swing::Config swing;
                        swing.pid = config.pid;
                        swing.base = config.base;
                        swing.record = config.record;
                        swing.mover = config.mover;
                        swing.motionModule = config.motionModule;
                        swing.durationMs = moduleDuration;
                        swing.maxSpeed = config.swingSpeed;
                        check(startSwing(&swing), "Start native swinging");
                        swingStarted = true;
                    }
                    if (swingStarted) {
                        game_swing::Command input;
                        input.serial = ++serial;
                        input.focused = motion.active && !flatScreen && controlsVisible;
                        input.sampleTimeNs = frame.predictedDisplayTime > 0 ? frame.predictedDisplayTime : 1;
                        input.sampleSeconds =
                            std::isfinite(frame.seconds) && frame.seconds > 0 && frame.seconds <= .1f
                                ? frame.seconds
                                : 1.f / 90.f;
                        input.jump = motion.swing.jump;
                        input.move = motion.swing.move;
                        input.trackingYaw = motion.swing.trackingYaw;
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = motion.swing.hands[i];
                            input.hands[i] = {hand.aim, hand.gripRelativeToHead, hand.tracked, hand.trigger,
                                              hand.grip};
                        }
                        check(submitSwing(&input), "Tracked swing input");
                        check(sampleSwing(&swingState), "Native swing feedback");
                        check(swingState.error, "Native swinging");
                        for (unsigned i = 0; i < 2; ++i) {
                            const bool attached =
                                input.focused && swingState.status && swingState.webs[i].attached;
                            if (attached != attachedBefore[i])
                                runtime.haptic(i, attached ? .65f : .2f);
                            if (attached && swingState.zips > zipBefore)
                                runtime.haptic(i, 1);
                            attachedBefore[i] = attached;
                            webTimes.update(i, attached, swingState.webs[i].anchor, webWrist(motion.hands[i]),
                                            frame.predictedDisplayTime);
                        }
                        zipBefore = swingState.zips;
                        if (websStarted && motion.active && !flatScreen) {
                            // The game draws these webs from the tracked wrists.
                            native_webs::Request webs;
                            webs.feet = motion.anchor;
                            for (unsigned i = 0; i < 2; ++i) {
                                auto& hand = webs.hands[i];
                                hand.attached = attachedBefore[i];
                                hand.tracked = motion.swing.hands[i].tracked;
                                hand.attachedAt = webTimes[i].attachedAt;
                                hand.anchor = swingState.webs[i].anchor;
                                hand.wrist = webWrist(motion.hands[i]);
                            }
                            native_webs::submit(webs, 200);
                        }
                    }
                    const auto keys = swingNativeKeys(
                        motion.active && !flatScreen && controlsVisible, swingState.owned != 0,
                        motion.nativeKeys, static_cast<SwingTakeoff::Phase>(swingState.takeoffPhase),
                        swingState.takeoff != 0);
                    controls(keys);
                    {
                        std::lock_guard lock(telemetry);
                        auto& d = SpidyXrData;
                        InterlockedIncrement64(&d.sequence);
                        ++d.frames;
                        d.tracked += motion.active;
                        d.leftHands += frame.hands[0].valid;
                        d.rightHands += frame.hands[1].valid;
                        d.nativeKeys = keys;
                        std::memcpy(d.head, motion.head.data(), 64);
                        const Mat4 basis = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto pose = native_view::relativePose(basis, motion.hands[i]);
                            std::memcpy(d.hands[i], pose.data(), 64);
                        }
                        InterlockedIncrement64(&d.sequence);
                    }
                    if (!motion.active)
                        return false;
                    if (!nativeStarted) {
                        native_gpu::Config gpu;
                        gpu.pid = config.pid;
                        gpu.base = config.base;
                        gpu.queue = config.queue;
                        gpu.durationMs = moduleDuration;
                        gpu.copyToXr = 2;
                        gpu.captureImages = config.options & 1;
                        check(native_gpu::start(&gpu), "Start native GPU bridge");
                        gpuStarted = true;
                        struct NativeConfig {
                            uint32_t magic, version, bytes, pid;
                            uint64_t base;
                            uint32_t duration, flags, width, height;
                        };
                        // Flag 8: while immersive, the game's active view follows the
                        // head, so work the game does only for that view fits the eyes.
                        const uint32_t flags = (config.options & 4) ? 5 : 13;
                        NativeConfig views{
                            0x53534346, 1, 40, config.pid, config.base, moduleDuration, flags, dimensions[0],
                            dimensions[1]};
                        check(SpidyStart(&views), "Start native views");
                        nativeStarted = true;
                        // Game-drawn webs are optional: without them the overlay draws webs.
                        websStarted = !(config.options & 2) && !native_webs::start(config.base, config.record);
                        nativeDeadline =
                            config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
                        firstImageDeadline = GetTickCount64() + 3000;
                        message(3, 0, "Head tracking active; waiting for first native eye pair");
                        return false; // initialization can exceed one predicted display interval
                    }
                    // Keep publishing tracking while the engine renders earlier
                    // commands. The presentation loop no longer waits for this pose.
                    native_eyes::Command eyes;
                    eyes.enabled = flatScreen ? 2 : 1;
                    eyes.serial = ++serial;
                    eyes.leaseMs = 250;
                    eyes.anchor = motion.anchor;
                    eyes.anchored = 1;
                    for (unsigned i = 0; i < 2; ++i) {
                        eyes.eyes[i].world = motion.eyes[i];
                        const auto& f = motion.fovs[i];
                        eyes.eyes[i].fov[0] = f.left;
                        eyes.eyes[i].fov[1] = f.right;
                        eyes.eyes[i].fov[2] = f.down;
                        eyes.eyes[i].fov[3] = f.up;
                    }
                    NativeEyeFrame saved;
                    saved.serial = eyes.serial;
                    saved.motion = motion;
                    saved.trackingEyes = frame.eyes;
                    saved.flatScreen = flatScreen;
                    saved.screenPose = screenPose;
                    saved.screenAspect = SpidyFlatAspect();
                    for (unsigned i = 0; i < 2; ++i) {
                        saved.webs[i] = swingState.webs[i];
                        saved.webTimes[i] = webTimes[i];
                    }
                    history.remember(saved);
                    const auto code = SpidySetEyes(&eyes);
                    if (code && code != 4003)
                        check(code, "Tracked eye command");
                    return !code;
                },
                [&](std::array<XrRuntime::EyeTarget, 2>& targets) {
                    const auto copyStart = std::chrono::steady_clock::now();
                    uint64_t imageSerial{}, imageGeneration{};
                    bool copied = native_gpu::copyLatest(targets[0].texture, targets[1].texture, imageSerial,
                                                         imageGeneration);
                    check(native_gpu::error(), "GPU eye bridge");
                    const auto* saved =
                        copied ? history.find(imageSerial, motion.predictedDisplayTime) : nullptr;
                    copied = copied && saved;
                    copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                       copyStart)
                                 .count();
                    if (copied) {
                        runtime.presentation(saved->flatScreen, saved->screenPose, saved->screenAspect);
                        const auto& rendered = saved->motion;
                        for (unsigned eye = 0; eye < 2; ++eye) {
                            const auto& pose = saved->trackingEyes[eye].pose;
                            const auto& f = saved->trackingEyes[eye].fov;
                            targets[eye].view.pose = {{pose.orientation.x, pose.orientation.y,
                                                       pose.orientation.z, pose.orientation.w},
                                                      {pose.position.x, pose.position.y, pose.position.z}};
                            targets[eye].view.fov = {f.left, f.right, f.up, f.down};
                        }
                        const auto overlayStart = std::chrono::steady_clock::now();
                        // The native views moved this command's eyes with the
                        // player to the rendered frame. Hands move with them, so
                        // world-space web anchors line up with the rendered city.
                        Vec3 travel{};
                        native_eyes::renderOffset(imageGeneration, travel);
                        const Vec3 viewer =
                            Vec3{(rendered.eyes[0][12] + rendered.eyes[1][12]) / 2,
                                 (rendered.eyes[0][13] + rendered.eyes[1][13]) / 2,
                                 (rendered.eyes[0][14] + rendered.eyes[1][14]) / 2} +
                            travel;
                        const auto& lens = rendered.fovs[0];
                        const float pixelAngle =
                            (std::tan(lens.right) - std::tan(lens.left)) / static_cast<float>(targets[0].width);
                        const auto shown = saved->motion.predictedDisplayTime;
                        std::vector<Vertex> vertices;
                        for (unsigned hand = 0; !saved->flatScreen && hand < 2; ++hand) {
                            const auto& web = saved->webs[hand];
                            const auto& timing = saved->webTimes[hand];
                            // The game's rope is part of the scene image already.
                            const bool gameWeb = native_webs::drawing(hand);
                            WebLine line;
                            // The timeline keeps the anchor through the release, so
                            // the splat keeps its shape while the web snaps back.
                            line.seed =
                                static_cast<float>(hand) * 1.7f + timing.anchor.x * .37f + timing.anchor.z * .11f;
                            if (!rendered.swing.hands[hand].tracked) {
                                if (!gameWeb && WebAnimation::released((shown - timing.releasedAt) * 1e-9f,
                                                                       timing.wrist + travel, timing.anchor, line))
                                    appendWeb(vertices, line, viewer, pixelAngle);
                                continue;
                            }
                            auto pose = rendered.hands[hand];
                            pose.position += travel;
                            addTrackedHand(vertices, pose, hand, rendered.swing.hands[hand].grip,
                                           web.attached != 0);
                            if (gameWeb) {
                                continue;
                            } else if (web.attached) {
                                line.start = webWrist(pose);
                                line.end = web.anchor;
                                line.slack = web.tension > 0 ? 0.f
                                                             : std::max(web.length - length(line.end - line.start), 0.f);
                                line.extended = WebAnimation::extended((shown - timing.attachedAt) * 1e-9f);
                                appendWeb(vertices, line, viewer, pixelAngle);
                            } else if (WebAnimation::released((shown - timing.releasedAt) * 1e-9f, webWrist(pose),
                                                              timing.anchor, line)) {
                                appendWeb(vertices, line, viewer, pixelAngle);
                            }
                        }
                        std::array<D3D12Renderer::ViewTarget, 2> overlayViews;
                        for (unsigned eye = 0; eye < 2; ++eye) {
                            const auto& target = targets[eye];
                            const auto& f = rendered.fovs[eye];
                            auto pose = rendered.eyes[eye];
                            pose[12] += travel.x;
                            pose[13] += travel.y;
                            pose[14] += travel.z;
                            const auto vp = multiply(projection(f.left, f.right, f.down, f.up, .03f, 200.f),
                                                     native_view::view(pose));
                            overlayViews[eye] = {target.texture, target.format, target.width, target.height,
                                                 vp};
                        }
                        if (!saved->flatScreen)
                            overlay.renderViews(overlayViews, vertices, true, false);
                        overlayMs = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - overlayStart)
                                        .count();
                    }
                    std::lock_guard lock(telemetry);
                    auto& d = SpidyXrData;
                    InterlockedIncrement64(&d.sequence);
                    d.dropped += !copied;
                    d.serial = imageSerial;
                    d.generation = imageGeneration;
                    InterlockedIncrement64(&d.sequence);
                    return copied;
                });
            const auto& timing = runtime.lastFrameTiming();
            if (submittedFrames >= 30 && timing.period > 0) {
                const double values[] = {timing.total,   timing.wait,  timing.tracking, timing.prepare,
                                         timing.acquire, copyMs,       overlayMs,       timing.release,
                                         timing.end,     timing.period};
                std::lock_guard lock(telemetry);
                InterlockedIncrement64(&SpidyXrTimingData.sequence);
                for (unsigned i = 0; i < 10; ++i)
                    SpidyXrTimingData.stages[i].add(values[i]);
                InterlockedIncrement64(&SpidyXrTimingData.sequence);
            }
            // Count only a successful xrEndFrame containing a projection layer.
            // A GPU copy alone does not confirm that OpenXR accepted the frame.
            if (runtime.lastFrameSubmitted()) {
                ++submittedFrames;
                lastPresentedMs = GetTickCount64();
                if (!presented) {
                    presented = true;
                    message(3, 0,
                            flatScreen ? "Flat screen - click both thumbsticks for VR"
                                       : "VR active - click both thumbsticks for flat screen");
                }
                std::lock_guard lock(telemetry);
                InterlockedIncrement64(&SpidyXrData.sequence);
                ++SpidyXrData.submitted;
                if (SpidyXrData.generation != previousPresentedGeneration)
                    ++SpidyXrData.uniqueSubmitted;
                else
                    ++SpidyXrData.reusedSubmitted;
                previousPresentedGeneration = SpidyXrData.generation;
                InterlockedIncrement64(&SpidyXrData.sequence);
            }
            if (!active)
                break;
        }
        message(6, 0, "Stopping VR and restoring game hooks");
    } catch (const std::exception& e) {
        error = 1;
        failure = e.what();
    }
    bridge::Control release;
    release.serial = ++serial;
    release.leaseMs = 0;
    if (submitInput)
        submitInput(&release);
    if (swingStarted) {
        const auto code = stopSwing(nullptr);
        if (code) {
            error = code;
            failure = "Native swinging did not stop cleanly";
        }
    }
    if (websStarted) {
        const auto code = native_webs::stop();
        if (code) {
            error = code;
            failure = "Game web lines did not stop cleanly";
        }
    }
    if (nativeStarted) {
        const auto code = SpidyStop(nullptr);
        if (code) {
            error = code;
            failure = "Native views did not retire cleanly";
        }
    }
    if (gpuStarted) {
        const auto code = native_gpu::stop();
        if (code) {
            error = code;
            failure = "Native GPU bridge did not stop cleanly";
        }
    }
    if (raysStarted) {
        const auto code = stopRays(nullptr);
        if (code) {
            error = code;
            failure = "Controller world rays did not stop cleanly";
        }
    }
    if (stopBridge) {
        const auto code = stopBridge(nullptr);
        if (code) {
            error = code;
            failure = "Input bridge did not stop cleanly";
        }
    }
    message(error ? 5 : 4, error, error ? failure.c_str() : "VR session finished");
    return error;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidyXrStart(void* input) {
    std::lock_guard life(lifecycle);
    if (worker)
        return 1000; // one bounded XR session per process during validation
    if (!read(reinterpret_cast<uintptr_t>(input), &config, sizeof(config)) || config.magic != 0x53585243 ||
        config.version != 5 || config.bytes != sizeof(config) || config.pid != GetCurrentProcessId() ||
        config.base != reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)) ||
        !GetModuleHandleW(L"Spider-Man.exe") || !config.queue || !config.bridgeModule || !config.rayModule ||
        !config.motionModule || !config.record || !config.mover || config.options > 7 ||
        !std::isfinite(config.swingSpeed) || config.swingSpeed < 1 || config.swingSpeed > 65 ||
        (config.durationMs && config.durationMs < 2000) || config.durationMs > 25000 ||
        (config.eyeSize && !validEyeSize(config.eyeSize)))
        return 1001;
    auto module = reinterpret_cast<HMODULE>(config.bridgeModule);
    submitInput = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidySubmit"));
    sampleBridge = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidyBridgeSample"));
    stopBridge = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidyStop"));
    auto rays = reinterpret_cast<HMODULE>(config.rayModule);
    startRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRayStart"));
    submitRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRaySubmit"));
    stopRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRayStop"));
    startSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingStart"));
    submitSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingSubmit"));
    sampleSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingSample"));
    stopSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingStop"));
    if (!submitInput || !sampleBridge || !stopBridge || !startRays || !submitRays || !stopRays ||
        !startSwing || !submitSwing || !sampleSwing || !stopSwing)
        return 1002;
    stopRequested = false;
    native_appearance::setPlayerRecord(config.record);
    keepAliveDeadline = GetTickCount64() + 10000;
    message(1, 0, "Starting Virtual Desktop OpenXR");
    worker = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
    return worker ? 0 : 1003;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyXrKeepAlive(void*) {
    if (stopRequested)
        return 1102;
    keepAliveDeadline = GetTickCount64() + 5000;
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyXrStop(void*) {
    std::lock_guard life(lifecycle);
    stopRequested = true;
    if (!worker)
        return 0;
    const auto status = WaitForSingleObject(worker, 15000);
    return status == WAIT_OBJECT_0 ? 0 : 1101;
}
