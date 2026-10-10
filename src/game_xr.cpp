// Bounded first-person OpenXR integration using the game's two native views.
#include "spidy/native_appearance.hpp"
#include "spidy/native_body.hpp"
// The input bridge remains a separately verified, separately stoppable module.
#include "spidy/body_calibration.hpp"
#include "spidy/eye_resolution.hpp"
#include "spidy/eye_snapshot.hpp"
#include "spidy/game_bridge_protocol.hpp"
#include "spidy/game_grab.hpp"
#include "spidy/game_menu.hpp"
#include "spidy/game_pad.hpp"
#include "spidy/game_player.hpp"
#include "spidy/game_punch.hpp"
#include "spidy/game_shooter.hpp"
#include "spidy/game_swing.hpp"
#include "spidy/game_time.hpp"
#include "spidy/game_tracking.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_eye_gpu.hpp"
#include "spidy/native_eye_history.hpp"
#include "spidy/native_hud.hpp"
#include "spidy/native_webs.hpp"
#include "spidy/presentation_gate.hpp"
#include "spidy/slow_motion.hpp"
#include "spidy/vr_settings.hpp"
#include "spidy/vr_shortcut.hpp"
#include "spidy/xr_session.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <windows.h>
using namespace spidy;
struct XrConfig {
    uint32_t magic = 0x53585243, version = 20, bytes = sizeof(XrConfig), pid{};
    // record and mover are no longer used: VR starts with the game, before
    // there is a player, and finds each new player itself (game_player).
    uint64_t base{}, queue{}, bridgeModule{}, rayModule{}, motionModule{}, record{}, mover{};
    uint32_t durationMs = 20000, eyeSize = defaultEyeSize;
    float swingSpeed = 32;
    // bit 0: CPU eye capture; bit 1: Spidy's overlay webs instead of the game's;
    // bit 2: keep the stock camera as the game's active (monitor) view in VR;
    // bit 3: no web grab (webs never catch props or thugs);
    // bit 4: no eye occlusion (each eye draws everything in its view, hidden or not);
    // bit 5: no body (the hero stays hidden in VR, the overlay draws gloves);
    // bit 6: no punching (fists pass through thugs);
    // bit 7: aim markers start hidden (X shows them in VR);
    // bit 8: no webs in open air (a web that meets nothing within reach misses);
    // bit 9: no web shooter (a free hand's trigger shoots nothing);
    // bit 10: no T-pose calibration at the first gameplay of a session without
    // one (the player skipped it before; the SPIDY VR tab still offers it);
    // bit 11: flips, experimental (A and the left stick in the air flip the player; FlipMotion);
    // bit 12: the trigger webs and the grip reels (WEB BUTTON: TRIGGER);
    // bit 13: the view stays upright on walls (STAND ON WALLS: OFF);
    // bit 14: walls are the game's (its wall crawl takes a player who flies
    // into one, as before the swing's own walls)
    uint32_t options{};
    // OpenXR runtime manifest the launcher chose; empty: Virtual Desktop's if
    // installed, else Windows' active runtime.
    wchar_t runtime[260]{};
    // The rest of the VR settings a session starts from (options bits 3, 5-9
    // and 11-13, swingSpeed, weight, hud and flipSpeed give the others):
    // degrees per snap turn (0: none), controller vibration in percent, the
    // game screen's size (0-2), degrees a second of smooth turning (0: the
    // stick snap turns).
    uint32_t snapTurn = 30, haptics = 100, screenSize = 1, smoothTurn{};
    // Eye resolution in percent of the runtime's recommendation, per side
    // (eye_resolution.hpp; with eyeSize set, 100), and the player's weight
    // while webs fly them, in percent of real gravity (40-300).
    uint32_t renderScale = 100, weight = 80;
    // The player's T-pose calibration (body_calibration.hpp): the eye height
    // and arm length their body is sized to, millimetres. Both 0: none yet.
    uint32_t eyeHeightMm{}, armLengthMm{};
    // The game's HUD in the headset: 0 off, 1 small, 2 medium, 3 large
    // (native_hud.hpp); how fast a flip turns the player at full tilt of the
    // left stick, degrees a second (90-480; FlipMotion::speed).
    uint32_t hud = 2, flipSpeed = 180;
};
static_assert(sizeof(XrConfig) == 648);
// Why the last frame had no gameplay (XrData::gate bits).
enum GateReason : uint32_t {
    gateNoPlayer = 1,     // no save loaded, or a level change in progress
    gateBridge = 2,       // the input bridge is not running
    gateNoCommit = 4,     // no camera commit for 100 ms: paused, loading, a cutscene
    gateOtherCamera = 8,  // the camera is not the player's follow or combat camera
    gateTracking = 16,    // the headset's pose or timing was not usable
};
struct XrData {
    uint32_t magic = 0x53585244, version = 20, bytes = sizeof(XrData), status{};
    int64_t sequence{};
    uint64_t frames{}, tracked{}, submitted{}, dropped{}, leftHands{}, rightHands{}, serial{}, generation{};
    uint32_t nativeKeys{}, error{};
    float head[16]{}, hands[2][16]{};
    char message[256]{};
    uint64_t uniqueSubmitted{}, reusedSubmitted{};
    uint32_t flatScreen{}, toggles{}, eyeWidth{}, eyeHeight{};
    // What the last frame showed (Presentation), and frames that showed the
    // game's presented frame on the screen because gameplay was unavailable.
    uint32_t presentation{}, reserved{};
    uint64_t screenSubmitted{};
    // GateReason bits of the last frame, and the vtable (image offset) of the
    // camera mover that last committed: which camera a scene without VR used.
    uint32_t gate{}, cameraMover{};
    // Players found so far (each loaded save, respawn or character switch),
    // and the current one's actor record.
    uint64_t players{}, playerRecord{};
    // Xbox buttons the VR controllers hold for the game, and the game's reads
    // of that virtual controller.
    uint32_t padButtons{}, padInstalled{};
    uint64_t padReads{};
    // The input bridge's camera commits, and those on the player by an
    // accepted camera. Both rising while the gate says other_camera: two
    // cameras commit each frame and the other one commits last.
    uint64_t cameraCommits{}, playerCommits{};
    // B presses that reached the game as its Y (interact, web strike); whether
    // the aim markers show now (X switches them in VR); markers drawn so far,
    // one per hand per headset frame that shows one.
    uint32_t interacts{}, aimMarkers{};
    uint64_t markers{};
    // The VR settings now: the SPIDY VR tab in the game's Settings changes
    // them during play (X the aim markers, too). settings bits: 1 web grab,
    // 2 punch, 4 body, 8 webs in open air, 16 web shooter, 32 flips, 64 the
    // trigger webs (WEB BUTTON: TRIGGER; the grip reels), 128 STAND ON WALLS.
    // Then the changes
    // made in that tab so far, the times the game built its Settings with it,
    // whether its hooks are in, and why it is missing (game_menu: 93xx-94xx
    // not hooked, 95xx not built).
    uint32_t settings{}, snapTurn{}, haptics{}, screenSize{};
    float swingSpeed{};
    uint32_t settingChanges{};
    uint64_t menuTabs{};
    uint32_t menuInstalled{}, menuStatus{};
    // Degrees a second of smooth turning now (0: the stick snap turns).
    uint32_t smoothTurn{};
    // Walls and ceilings the game held the player on (GameTrackingRig): the
    // stretches and frames of play on one, the player's up (its actor's)
    // now, the head's stand-off now (m), and on the last frame on one the
    // head's height over it from the feet, before and after the stand-off.
    uint32_t surfaceEntries{};
    uint64_t surfaceFrames{};
    float heroUp[3]{};
    float standOff{}, surfaceHeight{}, surfaceClearance{};
    // The weight now, percent of real gravity (the swing's gravity while webs
    // fly the player), and the HUD row now (0 off, 1-3 small to large).
    uint32_t weight{}, hud{};
    // The T-pose calibration (body_calibration.hpp): its phase now and what its
    // panel asks for (Phase, Hint), how far the hold is (0-1); the eye height
    // and arm length the body is sized to (mm; 0: the headset's running height
    // and the hero's own arms); calibrations finished and skipped this session;
    // calibrationFlags bit 0: a session without one asks for none at its first
    // gameplay (skipped); each arm's reach at the last one (m, 0 the left).
    uint32_t calibrationPhase{}, calibrationHint{};
    float calibrationProgress{};
    uint32_t eyeHeightMm{}, armLengthMm{}, calibrations{}, calibrationSkips{}, calibrationFlags{};
    float calibrationReach[2]{};
    // The flip speed now, degrees a second at full tilt (FLIP SPEED), and how
    // far the view leans from the world's up now, degrees: 0 level, 90 while
    // the player stands on a wall (GameMotionFrame::viewTilt).
    uint32_t flipSpeed{};
    float viewTilt{};
};
static_assert(sizeof(XrData) == 800);
// Slow motion (slow_motion.hpp, game_time.hpp): what the player did with it
// and what the game's time did. status: 1 before VR starts, then 0 with the
// game's time hooked, else why it is not (game_time::install) and slow
// motion does nothing.
struct SlowMotionData {
    uint32_t magic = 0x574f4c53, version = 1, bytes = sizeof(SlowMotionData), status = 1;
    int64_t sequence{};
    // Presses that started it, ended it, or came with too little focus; the
    // times focus ran out and play stopped it (a menu, the game screen, the
    // flat screen); whether it is on now.
    uint32_t starts{}, stops{}, refused{}, emptied{}, interrupted{}, active{};
    // Real seconds in it so far; focus (0-1) and how far into it (0-1) now;
    // the time scale asked of the game.
    float seconds{}, focus = 1, blend{}, wanted = 1;
    // The game's side in the last update of its time system: its own time
    // scale, its world's (the smaller of that and Spidy's), Havok's step (s)
    // and whether the game scales physics itself; its updates so far, and
    // those Spidy slowed.
    float gameScale = 1, worldScale = 1, physicsStep{};
    uint32_t physicsScaled{};
    uint64_t updates{}, slowedUpdates{};
};
static_assert(sizeof(SlowMotionData) == 96);
extern "C" {
__declspec(dllexport) XrData SpidyXrData;
__declspec(dllexport) SlowMotionData SpidySlowMotionData;
__declspec(dllexport) XrTimingData SpidyXrTimingData;
__declspec(dllexport) EyeSnapshot SpidyXrSnapshot;
DWORD WINAPI SpidyStart(void*);
DWORD WINAPI SpidyStop(void*);
DWORD WINAPI SpidySetEyes(void*);
float SpidyFlatAspect();
}
namespace {
using BridgeCall = DWORD(WINAPI*)(void*);
XrConfig config;
BridgeCall submitInput{}, sampleBridge{}, stopBridge{}, retargetBridge{};
BridgeCall startRays{}, submitRays{}, stopRays{};
BridgeCall startSwing{}, submitSwing{}, sampleSwing{}, stopSwing{}, retargetSwing{}, sampleGrab{}, sampleAim{};
BridgeCall startPunch{}, samplePunch{}, stopPunch{}, swingSettings{};
BridgeCall startShooter{}, sampleShooter{}, stopShooter{};
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
// Publishes a completed eye copy, or withdraws the previous one before its
// pixels are released.
void publishSnapshot(EyeSnapshot shot, const D3D12Renderer::Captured* pixels) {
    std::lock_guard lock(telemetry);
    auto& d = SpidyXrSnapshot;
    InterlockedIncrement64(&d.sequence);
    const auto sequence = d.sequence;
    const auto count = d.count + (pixels != nullptr);
    d = shot;
    d.sequence = sequence;
    d.count = count;
    if (pixels) {
        d.pixels = reinterpret_cast<uint64_t>(pixels->pixels);
        d.width = pixels->width;
        d.height = pixels->height;
        d.rowPitch = pixels->rowPitch;
    }
    InterlockedIncrement64(&d.sequence);
}
struct RuntimeSelection {
    std::wstring previous;
    bool present{};
    explicit RuntimeSelection(const wchar_t* requested) {
        const DWORD size = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr, 0);
        present = size != 0;
        if (size) {
            previous.resize(size);
            GetEnvironmentVariableW(L"XR_RUNTIME_JSON", previous.data(), size);
            previous.resize(size - 1);
        }
        constexpr auto virtualDesktop =
            L"C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr.json";
        const wchar_t* manifest = *requested ? requested : virtualDesktop;
        if (GetFileAttributesW(manifest) == INVALID_FILE_ATTRIBUTES) {
            if (*requested)
                throw std::runtime_error("The chosen OpenXR runtime is not installed");
            return; // no Virtual Desktop: the loader uses Windows' active runtime
        }
        if (!SetEnvironmentVariableW(L"XR_RUNTIME_JSON", manifest))
            throw std::runtime_error("Could not select the OpenXR runtime for this process");
    }
    ~RuntimeSelection() {
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", present ? previous.c_str() : nullptr);
    }
};
DWORD WINAPI run(void*) {
    bool nativeStarted = false, gpuStarted = false, raysStarted = false, swingStarted = false, websStarted = false,
         bodyStarted = false;
    uint64_t serial = 1, nativeDeadline{};
    // lastPresentedMs: an image was on show. lastNewImageMs: the game delivered a newer one.
    uint64_t firstImageDeadline{}, lastPresentedMs{}, lastNewImageMs{};
    bool presented{}, wasGameplay{};
    // The last frame's predicted display time; held images age against it.
    int64_t displayTime{};
    Presentation shownKind{};
    uint32_t error{};
    std::string failure;
    // VR starts with the game, before there is a player; each loaded save,
    // respawn or character switch brings a new one.
    game_player::Watch players;
    bool padInstalled{};
    try {
        auto queue = reinterpret_cast<ID3D12CommandQueue*>(config.queue);
        ComPtr<ID3D12Device> device;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))))
            throw std::runtime_error("Game graphics device unavailable");
        XrRuntime runtime;
        {
            RuntimeSelection selected(config.runtime);
            runtime.initialize(device.Get(), queue, config.eyeSize, config.renderScale);
        }
        D3D12Renderer overlay;
        overlay.initialize(device.Get(), queue);
        // The session report's copies of the left eye live in the overlay's
        // readback buffer; withdraw the last one before the overlay goes.
        struct WithdrawSnapshot {
            ~WithdrawSnapshot() {
                publishSnapshot({}, nullptr);
            }
        } withdrawSnapshot;
        EyeSnapshotSchedule snapshots;
        EyeSnapshot pendingSnapshot{};
        uint64_t snapshotTicket{}, gapGeneration{};
        message(2, 0, "Waiting for the headset");
        players.start(config.base);
        game_player::Player player{};
        uint64_t playerChanges{}, playersFound{};
        // Menus are played with a virtual Xbox controller (game_pad).
        padInstalled = !game_pad::install();
        uint64_t padRetryMs = GetTickCount64() + 1000;
        // The VR settings are a tab of the game's own Settings (game_menu).
        const uint32_t menuCode = game_menu::install(config.base);
        if (menuCode) {
            const auto text = "No SPIDY VR tab in the game's Settings (" + std::to_string(menuCode) +
                              "); the launcher's settings apply";
            message(3, 0, text.c_str());
        }
        // Slow motion slows the game's own time (game_time).
        const uint32_t timeCode = game_time::install(config.base);
        if (timeCode) {
            const auto text =
                "No slow motion: the game's time could not be hooked (" + std::to_string(timeCode) + ")";
            message(3, 0, text.c_str());
        }
        {
            std::lock_guard lock(telemetry);
            InterlockedIncrement64(&SpidySlowMotionData.sequence);
            SpidySlowMotionData.status = timeCode;
            InterlockedIncrement64(&SpidySlowMotionData.sequence);
        }
        // The last frame showed the game screen; A held from there (Resume,
        // Continue) is not a jump once gameplay is back.
        bool wasScreen{}, jumpFromScreen{};
        // A pressed in the air is not the game's web zip.
        AirJumpFilter airJump;
        // Where B went down: on the game screen it is Back, in VR the game's Y
        // (interact). Held across a switch it stays what it was until let go:
        // Back held into play is no interact, and an interact held into a menu
        // or scene the game opened is no Back there.
        enum class Origin { none, screen, play } interactOrigin{};
        bool interacting{};
        uint32_t interacts{};
        // The VR settings: the launch options, then the SPIDY VR tab in the
        // game's Settings (and X for the aim markers) during play.
        vr_settings::Values values;
        values.aimMarkers = !(config.options & 128);
        values.webGrab = !(config.options & 8);
        values.body = !(config.options & 32);
        values.punch = !(config.options & 64);
        values.airWebs = !(config.options & 256);
        values.webShooter = !(config.options & 512);
        values.flips = config.options & 2048;
        values.triggerWebs = config.options & 4096;
        values.standOnWalls = !(config.options & 8192);
        values.swingSpeed = config.swingSpeed;
        values.snapTurn = static_cast<int>(config.snapTurn);
        values.smoothTurn = static_cast<int>(config.smoothTurn);
        values.haptics = static_cast<int>(config.haptics);
        values.screenSize = static_cast<int>(config.screenSize);
        values.weight = static_cast<int>(config.weight);
        values.hud = static_cast<int>(config.hud);
        values.flipSpeed = static_cast<int>(config.flipSpeed);
        values = vr_settings::sanitized(values);
        game_menu::publish(values);
        uint32_t settingChanges{};
        // X switches the aim markers in immersive VR; it must be released
        // between switches. aims are each hand's latest preview.
        bool aimSwitchHeld = true;
        uint64_t markersDrawn{};
        game_swing::AimData aims{};
        // Each hand's aim marker, steadied from eye image to eye image.
        std::array<AimMarkerMotion, 2> aimMotion;
        GameTrackingRig rig;
        GameMotionFrame motion;
        NativeEyeHistory history;
        // Where the HUD panel faces in the room: it follows the head lazily
        // (native_hud::Follow), and starts in front of it again with play,
        // the immersive view and a recentred room.
        native_hud::Follow hudFollow;
        VrShortcut shortcut;
        GameScreen gameScreen;
        bool flatScreen{};
        Pose screenPose{};
        const auto dimensions = runtime.eyeDimensions();
        // Aim markers are drawn so many pixels wide: above the headset's own
        // resolution they keep the size they have at it.
        const auto recommended = runtime.recommendedDimensions();
        const float markerPixels =
            recommended[0] ? std::max(1.f, static_cast<float>(dimensions[0]) / static_cast<float>(recommended[0])) : 1.f;
        const auto moduleDuration = config.durationMs ? config.durationMs + 3000 : 0;
        {
            std::lock_guard lock(telemetry);
            InterlockedIncrement64(&SpidyXrData.sequence);
            SpidyXrData.eyeWidth = dimensions[0];
            SpidyXrData.eyeHeight = dimensions[1];
            InterlockedIncrement64(&SpidyXrData.sequence);
        }
        game_swing::Data swingState;
        // What each hand's web holds, when it caught a thug instead of a wall.
        game_grab::Data grabState;
        uint32_t grabPhases[2]{};
        // A grab's event pulse plays this long before the web's tension
        // takes the hand's haptics again (predicted display time, ns).
        int64_t pulseUntil[2]{};
        WebTimeline webTimes;
        bool attachedBefore[2]{};
        uint64_t zipBefore{};
        // Walls the swing took the player onto, and jumps off them, by the
        // previous frame.
        uint32_t wallsBefore{}, wallJumpsBefore{};
        // Punches each hand had landed by the previous frame.
        bool punchStarted{};
        uint64_t punchesBefore[2]{};
        // Web-shooter shots each hand had fired by the previous frame.
        bool shooterStarted{};
        uint64_t shotsBefore[2]{};
        uint64_t submittedFrames{};
        uint64_t previousPresentedGeneration{};
        // The player's standing eye height above the floor: it rises at once
        // to a higher head and sinks a centimetre a second, so crouching
        // does not shrink the body.
        float standingEye{};
        // Each hand's fist (0 open, 1 closed): closed by the grip, or by a
        // hand moving fast relative to the head (a punch), opening again in
        // a third of a second; and where each grip was relative to the head.
        float fists[2]{};
        Vec3 gripsBefore[2]{};
        bool gripsSeen[2]{};
        // Stretches and frames of play the game held the player on a wall
        // or a ceiling (the head stood off it).
        uint32_t surfaceEntries{};
        uint64_t surfaceFrames{};
        bool wasOnSurface{};
        // The player's size for the body: their T-pose calibration (the
        // launch's, or one made in this session), else the headset's running
        // height and the hero's own arms. A session with the body and without
        // a calibration asks for one at its first gameplay unless the player
        // skipped it before; the SPIDY VR tab asks again (CALIBRATE BODY: ON
        // RESUME).
        float calibratedEye = static_cast<float>(config.eyeHeightMm) / 1000;
        float calibratedArm = static_cast<float>(config.armLengthMm) / 1000;
        bool calibrationPrompt = !(config.options & 1024);
        bool calibrationWanted = calibrationPrompt && !config.eyeHeightMm && values.body;
        body_calibration::Calibration calibration;
        body_calibration::Measurements lastCalibration{};
        // The panel's place in the tracking space, ahead of where the player
        // faced as it appeared; since when VR has shown play; when the result
        // appeared.
        Pose calibrationPanel{};
        uint64_t immersiveSince{}, calibrationShownAt{};
        // From the calibration's start until each is let go after it, the
        // triggers, grips, A and B do nothing in the game (B skips it).
        bool calibrationInput{}, skipHeld{};
        uint32_t calibrations{}, calibrationSkips{};
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        // Slow motion: the left thumbstick's click (StickPress keeps it apart
        // from clicking both), its focus and the game's eased time
        // (SlowMotion). Each step sets the game's time, gives the controllers
        // a pulse at a change and reports it; without headset frames for a
        // while the loop below steps it to its end.
        SlowMotion slowMotion;
        StickPress slowPress;
        SlowMotionData slowCounts;
        uint64_t slowSteppedMs = GetTickCount64();
        auto stepSlowMotion = [&](float seconds, bool pressed, bool allowed) {
            using Event = SlowMotion::Event;
            const bool before = slowMotion.active();
            const auto event = slowMotion.update(seconds, pressed, allowed);
            game_time::slow(slowMotion.timeScale());
            slowSteppedMs = GetTickCount64();
            if (before && std::isfinite(seconds))
                slowCounts.seconds += std::clamp(seconds, 0.f, .1f);
            switch (event) {
            case Event::started:
                ++slowCounts.starts;
                runtime.haptic(0, .55f);
                runtime.haptic(1, .55f);
                break;
            case Event::stopped:
                ++slowCounts.stops;
                runtime.haptic(0, .3f);
                runtime.haptic(1, .3f);
                break;
            case Event::emptied:
                ++slowCounts.emptied;
                runtime.haptic(0, .8f);
                break;
            case Event::refused:
                ++slowCounts.refused;
                runtime.haptic(0, .6f);
                break;
            case Event::interrupted:
                ++slowCounts.interrupted;
                break;
            case Event::none:
                break;
            }
            const auto time = game_time::telemetry();
            std::lock_guard lock(telemetry);
            auto& d = SpidySlowMotionData;
            InterlockedIncrement64(&d.sequence);
            d.starts = slowCounts.starts;
            d.stops = slowCounts.stops;
            d.refused = slowCounts.refused;
            d.emptied = slowCounts.emptied;
            d.interrupted = slowCounts.interrupted;
            d.active = slowMotion.active();
            d.seconds = slowCounts.seconds;
            d.focus = slowMotion.focus();
            d.blend = slowMotion.blend();
            d.wanted = slowMotion.timeScale();
            d.gameScale = time.own;
            d.worldScale = time.world;
            d.physicsStep = time.physicsStep;
            d.physicsScaled = time.physicsScaled;
            d.updates = time.updates;
            d.slowedUpdates = time.slowed;
            InterlockedIncrement64(&d.sequence);
        };
        // Puts the settings into effect: at once, or when the module they
        // belong to starts (each start reads `values`).
        auto applySettings = [&] {
            runtime.hapticStrength(static_cast<float>(values.haptics) / 100);
            rig.snapTurn(static_cast<float>(values.snapTurn) * 3.14159265f / 180);
            rig.smoothTurn(static_cast<float>(values.smoothTurn) * 3.14159265f / 180);
            rig.flips(values.flips);
            rig.flipSpeed(static_cast<float>(values.flipSpeed) * 3.14159265f / 180);
            rig.triggerWebs(values.triggerWebs);
            rig.standOnWalls(values.standOnWalls);
            native_hud::setSize(values.hud);
            if (swingStarted && swingSettings) {
                game_swing::Settings s;
                s.grab = values.webGrab && sampleGrab;
                s.maxSpeed = values.swingSpeed;
                s.airWebs = values.airWebs;
                s.gravity = vr_settings::gravity(values.weight);
                s.walls = !(config.options & 16384);
                if (const auto code = swingSettings(&s)) {
                    const auto text = "VR settings: the swing did not take them (" + std::to_string(code) + ")";
                    message(3, 0, text.c_str());
                }
            }
            if (swingStarted && startPunch && samplePunch) {
                if (values.punch && !punchStarted) {
                    game_punch::Config punch;
                    punch.pid = config.pid;
                    punch.base = config.base;
                    punchStarted = !startPunch(&punch);
                } else if (!values.punch && punchStarted && stopPunch) {
                    stopPunch(nullptr);
                    punchStarted = false;
                }
            }
            if (swingStarted && startShooter && sampleShooter && stopShooter) {
                if (values.webShooter && !shooterStarted) {
                    game_shooter::Config shooter;
                    shooter.pid = config.pid;
                    shooter.base = config.base;
                    shooterStarted = !startShooter(&shooter);
                } else if (!values.webShooter && shooterStarted) {
                    stopShooter(nullptr);
                    shooterStarted = false;
                }
            }
            // Turned off, the body blends back to the game's pose (Command
            // flags); it is started only once.
            if (nativeStarted && values.body && !bodyStarted)
                bodyStarted = !native_body::start(config.base);
        };
        applySettings();
        auto controls = [&](uint32_t keys) {
            bridge::Control c;
            c.modes = bridge::Mode::input;
            c.serial = ++serial;
            c.leaseMs = 250;
            c.keys = keys;
            check(submitInput(&c), "Controller input");
        };
        // The game replaced its player or has none: every module that acts on
        // the player follows. A player gone again before this reaches the
        // modules is reported by the watch once more.
        auto attach = [&](const game_player::Player& next) {
            const auto follow = [&](const game_player::Player& p) {
                bridge::Config target;
                target.pid = config.pid;
                target.imageBase = config.base;
                target.hero = p.hero;
                target.actorRecord = p.record;
                if (retargetBridge(&target))
                    return false;
                if (!swingStarted)
                    return true;
                game_swing::Config swing;
                swing.pid = config.pid;
                swing.base = config.base;
                swing.record = p.record;
                swing.mover = p.mover;
                return !retargetSwing(&swing);
            };
            player = follow(next) ? next : game_player::Player{};
            if (!player.hero)
                follow({});
            native_appearance::setPlayerRecord(player.record);
            native_body::retarget(player.record);
            if (websStarted)
                native_webs::retarget(player.record);
            playersFound += player.hero != 0;
            // The tracking space aligns with the new body's camera again.
            rig.reset();
            history.clear();
        };
        while (!stopRequested && (!nativeDeadline || GetTickCount64() < nativeDeadline)) {
            if (!config.durationMs && GetTickCount64() >= keepAliveDeadline)
                break;
            double copyMs{}, overlayMs{};
            Presentation drawn{};
            // This frame shows the game's presented frame on the screen.
            bool showScreen{};
            if (!padInstalled && GetTickCount64() >= padRetryMs) {
                padInstalled = !game_pad::install();
                padRetryMs = GetTickCount64() + 1000;
            }
            if (motion.active && firstImageDeadline && !lastNewImageMs && GetTickCount64() >= firstImageDeadline)
                throw std::runtime_error("No native eye images reached OpenXR within 3 seconds");
            if (motion.active && lastNewImageMs && GetTickCount64() - lastNewImageMs >= 3000)
                throw std::runtime_error("Native eye presentation stalled for 3 seconds");
            const bool active = runtime.frameStereo(
                [&](const XrFrame& frame) {
                    uint64_t changes{};
                    if (const auto found = players.current(changes); changes != playerChanges) {
                        playerChanges = changes;
                        attach(found);
                    }
                    bridge::Data game{};
                    check(sampleBridge(&game), "Gameplay sample");
                    LARGE_INTEGER now{};
                    QueryPerformanceCounter(&now);
                    Mat4 body{}, camera{};
                    std::memcpy(body.data(), game.player, 64);
                    std::memcpy(camera.data(), game.before, 64);
                    // Gameplay: the player's own follow or combat camera
                    // committed within 100 ms (game_bridge.cpp).
                    const bool bridgeRunning = game.state == 1;
                    const bool committed = game.qpc && now.QuadPart >= static_cast<int64_t>(game.qpc) &&
                                           static_cast<double>(now.QuadPart - game.qpc) / frequency.QuadPart < .1;
                    const bool followsPlayer = native_view::validPose(body) && native_view::validPose(camera);
                    const bool gameplay = player.hero && bridgeRunning && committed && followsPlayer;
                    uint64_t moverTable{};
                    read(game.mover, &moverTable, sizeof(moverTable));
                    // The headset is worn, focused and tracked: an image can be shown.
                    const bool viewing = frame.focused && frame.valid;
                    displayTime = frame.predictedDisplayTime;
                    if (shortcut.update(viewing && gameplay, frame.hands[0].stickClick,
                                        frame.hands[1].stickClick)) {
                        flatScreen = !flatScreen;
                        rig.reset();
                        history.clear();
                        lastPresentedMs = lastNewImageMs = 0;
                        presented = false;
                        firstImageDeadline = GetTickCount64() + 3000;
                        screenPose = screenAhead(frame.head);
                        runtime.haptic(0, .35f);
                        runtime.haptic(1, .35f);
                        std::lock_guard lock(telemetry);
                        InterlockedIncrement64(&SpidyXrData.sequence);
                        ++SpidyXrData.toggles;
                        SpidyXrData.flatScreen = flatScreen;
                        InterlockedIncrement64(&SpidyXrData.sequence);
                    }
                    if (!frame.jump)
                        jumpFromScreen = false;
                    else if (wasScreen)
                        jumpFromScreen = true;
                    XrFrame controller = frame;
                    // The T-pose calibration has A as it has the other buttons.
                    controller.jump = frame.jump && !jumpFromScreen && !calibrationInput;
                    // The player's up (its actor's second row): a wall's normal
                    // while the game holds it on one. Spidy's own flight is on none.
                    const Vec3 heroUp{body[4], body[5], body[6]};
                    // The wall that holds the player, as the latest swing sample
                    // has it (no older than a tenth of a second): the swing's own
                    // wall, its floor on the wall beside the body's centre. The
                    // game's crawl (Surface::game) is not passed on: the game
                    // takes the stick there in its own way, which is not measured
                    // yet, so the view stays upright and stood off it as before.
                    const Vec3 feet{body[12], body[13], body[14]};
                    SurfaceHold hold;
                    if (swingState.status && swingState.wall == static_cast<uint32_t>(game_swing::Surface::wall) &&
                        swingState.qpc &&
                        static_cast<int64_t>(swingState.qpc) > now.QuadPart - frequency.QuadPart / 10) {
                        hold.normal = swingState.wallNormal;
                        hold.anchor = feet + Vec3{0, 1, 0} - hold.normal * swingState.wallDistance;
                        hold.speed = length(swingState.velocity);
                    }
                    // A or the left stick in the air flips the player (FlipMotion),
                    // in the air as the latest swing sample has it.
                    motion = rig.update(controller, feet, {camera[8], camera[9], camera[10]}, gameplay,
                                        swingState.owned ? Vec3{0, 1, 0} : heroUp,
                                        game_swing::airborne(swingState, now.QuadPart, frequency.QuadPart), hold);
                    if (motion.active && motion.onSurface) {
                        surfaceEntries += !wasOnSurface;
                        ++surfaceFrames;
                    }
                    wasOnSurface = motion.active && motion.onSurface;
                    // Without gameplay (menus, hint cards, cutscenes, animated
                    // cameras, loading) the game's own camera goes on the screen.
                    bool screen{};
                    if (viewing)
                        screen = gameScreen.update(motion.active, GetTickCount64());
                    else
                        gameScreen.reset();
                    wasScreen = screen;
                    if (!(frame.buttons & buttonB))
                        interactOrigin = Origin::none;
                    else if (interactOrigin == Origin::none)
                        interactOrigin = screen ? Origin::screen : Origin::play;
                    // On the screen the controllers are the game's Xbox
                    // controller; in VR it gets Start (menu), Back (Y) and
                    // the native walking and jumping (submitted below).
                    const auto mapping = !viewing ? game_pad::Mapping::none
                                         : screen ? game_pad::Mapping::menus
                                                  : game_pad::Mapping::gameplay;
                    // X shows or hides the aim markers in immersive VR. On the
                    // game screen it is the game's X; held from there, it waits
                    // for a release.
                    const bool aimSwitch = (frame.buttons & buttonX) != 0;
                    if (aimSwitch && !aimSwitchHeld && mapping == game_pad::Mapping::gameplay && motion.active &&
                        !flatScreen) {
                        values.aimMarkers = !values.aimMarkers;
                        runtime.haptic(0, .25f);
                        message(3, 0,
                                values.aimMarkers ? "Aim markers on - X hides them"
                                                  : "Aim markers off - X shows them");
                    }
                    aimSwitchHeld = aimSwitch;
                    // A change in the SPIDY VR tab takes effect at once; the tab
                    // shows what X switched.
                    if (game_menu::take(values)) {
                        ++settingChanges;
                        applySettings();
                    }
                    game_menu::publish(values);
                    const uint32_t gate =
                        (player.hero ? 0 : gateNoPlayer) | (bridgeRunning ? 0 : gateBridge) |
                        (committed ? 0 : gateNoCommit) | (followsPlayer ? 0 : gateOtherCamera) |
                        (gameplay && !motion.active ? gateTracking : 0);
                    if ((gameScreen.entered() && !flatScreen) || ((flatScreen || screen) && frame.recentered))
                        screenPose = screenAhead(frame.head, vr_settings::screenDistance);
                    // An image keeps the tracking-space poses it was rendered
                    // for, valid until the tracking space itself changes.
                    if (!viewing || frame.recentered)
                        history.clear();
                    if (!viewing || frame.recentered || !motion.active || flatScreen)
                        hudFollow.reset();
                    if (!viewing) {
                        lastPresentedMs = lastNewImageMs = firstImageDeadline = 0;
                        presented = false;
                    } else if (motion.active && !wasGameplay) {
                        // Gameplay counts its own images: the screen before it
                        // may have had none for a while (loading).
                        lastNewImageMs = 0;
                        presented = false;
                        firstImageDeadline = nativeStarted ? GetTickCount64() + 3000 : 0;
                    }
                    wasGameplay = motion.active;
                    const bool controlsVisible = recentPresentation(lastNewImageMs, GetTickCount64());
                    // The T-pose calibration: at the first gameplay of a session
                    // without one, or when the SPIDY VR tab asks for it, once VR
                    // has shown play for a moment and no web flies the player. It
                    // goes on only while VR shows play (a pause or the flat
                    // screen holds it), and B skips it.
                    using CalibrationPhase = body_calibration::Phase;
                    const bool immersive = viewing && motion.active && !flatScreen && controlsVisible;
                    const uint64_t nowMs = GetTickCount64();
                    if (!immersive)
                        immersiveSince = 0;
                    else if (!immersiveSince)
                        immersiveSince = nowMs;
                    const bool skipPressed = (frame.buttons & buttonB) && !skipHeld;
                    skipHeld = (frame.buttons & buttonB) != 0;
                    if (calibration.phase() == CalibrationPhase::idle && (calibrationWanted || values.calibrate) &&
                        immersive && nowMs - immersiveSince >= 1500 && !swingState.owned) {
                        calibration.start();
                        calibrationPanel = body_calibration::panelPose(frame.head);
                        calibrationInput = true;
                        values.calibrate = false; // the tab shows NO again
                        runtime.haptic(0, .3f);
                        runtime.haptic(1, .3f);
                        message(3, 0, "Body calibration: stand in a T-pose and hold both triggers (B skips)");
                    } else if (calibration.phase() == CalibrationPhase::done) {
                        if (nowMs - calibrationShownAt >= 3500)
                            calibration.stop();
                    } else if (calibration.phase() != CalibrationPhase::idle && immersive && skipPressed) {
                        calibration.stop();
                        ++calibrationSkips;
                        // Nor at the next sessions' first gameplay (the launcher keeps it).
                        calibrationWanted = calibrationPrompt = false;
                        runtime.haptic(1, .25f);
                        message(3, 0, "Body calibration skipped - Settings > SPIDY VR > CALIBRATE BODY does it later");
                    } else if (calibration.phase() != CalibrationPhase::idle && immersive) {
                        body_calibration::Sample sample;
                        sample.headTracked = validTrackedPose(frame.head);
                        sample.head = frame.head;
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = frame.hands[i];
                            sample.handTracked[i] = hand.valid && validTrackedPose(hand.grip);
                            sample.grips[i] = hand.grip;
                            sample.triggers[i] = hand.trigger;
                        }
                        sample.seconds = frame.seconds;
                        // The hero's rig once the body has turned it, else Spider-Man's measured one.
                        body_calibration::Proportions sizes;
                        native_body::proportions(sizes);
                        const auto before = calibration.phase();
                        if (calibration.update(sample, sizes)) {
                            lastCalibration = calibration.result();
                            calibratedEye = std::clamp(lastCalibration.eyeHeight,
                                                       body_calibration::minEyeHeightMm / 1000.f,
                                                       body_calibration::maxEyeHeightMm / 1000.f);
                            calibratedArm = std::clamp(lastCalibration.armLength,
                                                       body_calibration::minArmLengthMm / 1000.f,
                                                       body_calibration::maxArmLengthMm / 1000.f);
                            ++calibrations;
                            calibrationWanted = false;
                            calibrationShownAt = nowMs;
                            runtime.haptic(0, .7f);
                            runtime.haptic(1, .7f);
                            char text[160];
                            sprintf_s(text, "Body calibrated: eye height %.2f m, arm length %.2f m (left %.2f, right %.2f)",
                                      calibratedEye, calibratedArm, lastCalibration.reach[0], lastCalibration.reach[1]);
                            message(3, 0, text);
                        } else if (before == CalibrationPhase::waiting &&
                                   calibration.phase() == CalibrationPhase::holding) {
                            // The pose counts: a light tick in both hands.
                            runtime.haptic(0, .15f);
                            runtime.haptic(1, .15f);
                        }
                    }
                    if (calibrationInput && calibration.phase() == CalibrationPhase::idle) {
                        bool held = (frame.buttons & buttonB) || frame.jump;
                        for (const auto& hand : frame.hands)
                            held = held || hand.trigger > .2f || hand.squeeze > .2f;
                        calibrationInput = held;
                    }
                    if (calibrationInput) {
                        // The calibration has the controllers: no web, web ball or jump.
                        motion.swing.jump = false;
                        motion.nativeKeys &= ~swingJumpKey;
                        for (auto& hand : motion.swing.hands)
                            hand.trigger = hand.grip = 0;
                    }
                    // Slow motion: the left thumbstick's click, in VR play outside the calibration.
                    const bool slowAllowed = immersive && calibration.phase() == CalibrationPhase::idle;
                    stepSlowMotion(frame.seconds,
                                   slowPress.update(frame.seconds, slowAllowed, frame.hands[0].stickClick,
                                                    frame.hands[1].stickClick),
                                   slowAllowed);
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
                        swing.record = player.record;
                        swing.mover = player.mover;
                        swing.motionModule = config.motionModule;
                        swing.durationMs = moduleDuration;
                        swing.maxSpeed = values.swingSpeed;
                        swing.gravity = vr_settings::gravity(values.weight);
                        swing.grabKinds = !values.webGrab || !sampleGrab ? 0 : game_grab::movableKinds;
                        check(startSwing(&swing), "Start native swinging");
                        swingStarted = true;
                        // Fists and the web shooter take the swing's input samples;
                        // both are optional and, like the grab, can be switched on later.
                        applySettings();
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
                        input.tilt = motion.swing.tilt;
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = motion.swing.hands[i];
                            input.hands[i] = {hand.aim, hand.gripRelativeToHead, hand.tracked, hand.trigger,
                                              hand.grip};
                        }
                        check(submitSwing(&input), "Tracked swing input");
                        check(sampleSwing(&swingState), "Native swing feedback");
                        check(swingState.error, "Native swinging");
                        if (!sampleGrab || sampleGrab(&grabState))
                            grabState = {};
                        // What each free hand's web button would do: the aim markers.
                        aims = {};
                        if (values.aimMarkers && sampleAim && input.focused) {
                            game_swing::AimData sample;
                            const game_swing::AimData expected;
                            if (!sampleAim(&sample) && sample.magic == expected.magic &&
                                sample.version == expected.version && sample.bytes == expected.bytes &&
                                sample.status == 2)
                                aims = sample;
                        }
                        bool trails[2]{};
                        Vec3 trailEnds[2]{};
                        for (unsigned i = 0; i < 2; ++i) {
                            // A web that caught a thug ends on him, wherever he goes.
                            const auto& held = grabState.hands[i];
                            const bool grabbing = input.focused && held.phase != 0;
                            const bool wasGrabbing = grabPhases[i] != 0;
                            const int64_t displayAt = frame.predictedDisplayTime;
                            if (grabbing) {
                                swingState.webs[i] = {1, 0, held.end, held.length, held.taut ? 1.f : 0.f};
                                // Caught, yanked, at the hand: each its own pulse.
                                const auto phase = static_cast<GrabPhase>(held.phase);
                                if (held.phase != grabPhases[i]) {
                                    runtime.haptic(i, phase == GrabPhase::Yanked ? 1.f
                                                      : phase == GrabPhase::Held ? .4f
                                                                                 : .55f);
                                    pulseUntil[i] = displayAt + 30'000'000;
                                } else if (held.tension > .05f && displayAt >= pulseUntil[i]) {
                                    // Between pulses the hand feels the web pull: a
                                    // hanging trash can faintly, one swung hard or too
                                    // heavy to lift strongly.
                                    runtime.haptic(i, std::min(.6f, .6f * held.tension));
                                }
                            } else if (wasGrabbing) {
                                runtime.haptic(i, .6f); // thrown or let go
                            }
                            grabPhases[i] = grabbing ? held.phase : 0;
                            // The web let go of trails what it held as it dissolves.
                            trails[i] = input.focused && !grabbing && held.trailing;
                            trailEnds[i] = held.end;
                            const bool attached =
                                input.focused && swingState.status && swingState.webs[i].attached;
                            if (attached != attachedBefore[i] && !grabbing && !wasGrabbing)
                                runtime.haptic(i, attached ? .65f : .2f);
                            if (attached && swingState.zips > zipBefore && !grabbing)
                                runtime.haptic(i, 1);
                            attachedBefore[i] = attached;
                            webTimes.update(i, attached, swingState.webs[i].anchor, webWrist(motion.hands[i]),
                                            frame.predictedDisplayTime);
                        }
                        zipBefore = swingState.zips;
                        // Onto a wall, and off it with a jump: both hands feel it.
                        if (input.focused && swingState.status &&
                            (swingState.walls > wallsBefore || swingState.wallJumps > wallJumpsBefore)) {
                            const float pulse = swingState.wallJumps > wallJumpsBefore ? .6f : .45f;
                            runtime.haptic(0, pulse);
                            runtime.haptic(1, pulse);
                        }
                        wallsBefore = swingState.walls;
                        wallJumpsBefore = swingState.wallJumps;
                        game_punch::Data punch{};
                        if (punchStarted && !samplePunch(&punch))
                            for (unsigned i = 0; i < 2; ++i) {
                                // A landed punch knocks back into the hand, as hard as it was;
                                // the web's rumble waits for it.
                                if (punch.hands[i].punches > punchesBefore[i] && input.focused) {
                                    runtime.haptic(i, .5f + .5f * punch.hands[i].lastStrength);
                                    pulseUntil[i] = frame.predictedDisplayTime + 40'000'000;
                                }
                                punchesBefore[i] = punch.hands[i].punches;
                            }
                        game_shooter::Data shots{};
                        if (shooterStarted && !sampleShooter(&shots))
                            for (unsigned i = 0; i < 2; ++i) {
                                // A web ball leaving the wrist: a short, crisp tick.
                                if (shots.hands[i].shots > shotsBefore[i] && input.focused) {
                                    runtime.haptic(i, .35f);
                                    pulseUntil[i] = frame.predictedDisplayTime + 25'000'000;
                                }
                                shotsBefore[i] = shots.hands[i].shots;
                            }
                        if (websStarted && motion.active && !flatScreen) {
                            // The game draws these webs from the tracked wrists.
                            native_webs::Request webs;
                            webs.feet = motion.anchor;
                            for (unsigned i = 0; i < 2; ++i) {
                                auto& hand = webs.hands[i];
                                // A web let go of a thrown prop goes with it as it dissolves.
                                const bool trail = !attachedBefore[i] && trails[i];
                                hand.attached = attachedBefore[i] ? 1 : trail ? 2 : 0;
                                hand.tracked = motion.swing.hands[i].tracked;
                                hand.attachedAt = webTimes[i].attachedAt;
                                hand.anchor = trail ? trailEnds[i] : swingState.webs[i].anchor;
                                hand.wrist = webWrist(motion.hands[i]);
                            }
                            native_webs::submit(webs, 200);
                        }
                    }
                    const bool steering = motion.active && !flatScreen && controlsVisible;
                    motion.nativeKeys = airJump.update(
                        motion.nativeKeys, game_swing::airborne(swingState, now.QuadPart, frequency.QuadPart));
                    // Going up onto a wall (game_swing::Data::mount), the game
                    // gets the jump alone: the stick walked the player on into
                    // the wall, and into the game's own crawl.
                    const bool mounting =
                        swingState.status && swingState.mount && swingState.qpc &&
                        static_cast<int64_t>(swingState.qpc) > now.QuadPart - frequency.QuadPart / 10;
                    const auto keys = swingNativeKeys(
                        steering, swingState.owned != 0 || mounting, motion.nativeKeys,
                        static_cast<SwingTakeoff::Phase>(swingState.takeoffPhase), swingState.takeoff != 0);
                    controls(keys);
                    if (padInstalled) {
                        // The same walking and jumping on the virtual Xbox
                        // controller: after the VR menus, the game plays the
                        // player with it and ignores the bridge's keys.
                        game_pad::Walk walk;
                        if (steering && !swingState.owned) {
                            walk.right = mounting ? 0 : motion.walkRight;
                            walk.forward = mounting ? 0 : motion.walkForward;
                            // B is the game's Y: interact, or web strike in a fight.
                            // Not while a web carries the player, nor while it skips
                            // the calibration.
                            walk.interact = interactOrigin == Origin::play && !calibrationInput;
                        }
                        walk.jump = (keys & swingJumpKey) != 0;
                        auto pad = game_pad::fromControllers(frame, mapping, walk);
                        if (interactOrigin == Origin::play)
                            pad.buttons = static_cast<uint16_t>(pad.buttons & ~game_pad::b);
                        game_pad::submit(pad, 200);
                        interacts += walk.interact && !interacting;
                        interacting = walk.interact;
                    }
                    {
                        std::lock_guard lock(telemetry);
                        auto& d = SpidyXrData;
                        InterlockedIncrement64(&d.sequence);
                        ++d.frames;
                        d.tracked += motion.active;
                        d.leftHands += frame.hands[0].valid;
                        d.rightHands += frame.hands[1].valid;
                        d.nativeKeys = keys;
                        d.gate = gate;
                        d.cameraMover = moverTable > config.base && moverTable - config.base < 0x10000000
                                            ? static_cast<uint32_t>(moverTable - config.base)
                                            : 0;
                        d.players = playersFound;
                        d.playerRecord = player.record;
                        const auto pad = game_pad::telemetry();
                        d.padButtons = pad.buttons;
                        d.padInstalled = pad.installed;
                        d.padReads = pad.reads;
                        d.cameraCommits = game.cameraCalls;
                        d.playerCommits = game.matched;
                        d.interacts = interacts;
                        d.aimMarkers = values.aimMarkers;
                        d.settings = (values.webGrab ? 1u : 0u) | (values.punch ? 2u : 0u) | (values.body ? 4u : 0u) |
                                     (values.airWebs ? 8u : 0u) | (values.webShooter ? 16u : 0u) |
                                     (values.flips ? 32u : 0u) | (values.triggerWebs ? 64u : 0u) |
                                     (values.standOnWalls ? 128u : 0u);
                        d.snapTurn = static_cast<uint32_t>(values.snapTurn);
                        d.smoothTurn = static_cast<uint32_t>(values.smoothTurn);
                        d.haptics = static_cast<uint32_t>(values.haptics);
                        d.screenSize = static_cast<uint32_t>(values.screenSize);
                        d.swingSpeed = values.swingSpeed;
                        d.weight = static_cast<uint32_t>(values.weight);
                        d.hud = static_cast<uint32_t>(values.hud);
                        d.flipSpeed = static_cast<uint32_t>(values.flipSpeed);
                        d.viewTilt = motion.active ? motion.viewTilt * 180 / 3.14159265f : 0.f;
                        d.settingChanges = settingChanges;
                        const auto menu = game_menu::telemetry();
                        d.menuTabs = menu.tabs;
                        d.menuInstalled = menu.installed;
                        d.menuStatus = menuCode ? menuCode : menu.status;
                        d.surfaceEntries = surfaceEntries;
                        d.surfaceFrames = surfaceFrames;
                        std::memcpy(d.heroUp, body.data() + 4, 12);
                        d.standOff = length(motion.standOff);
                        if (motion.active && motion.onSurface) {
                            d.surfaceHeight = motion.surfaceHeight;
                            d.surfaceClearance = motion.surfaceClearance;
                        }
                        const bool calibrating = calibration.phase() != body_calibration::Phase::idle;
                        d.calibrationPhase = static_cast<uint32_t>(calibration.phase());
                        d.calibrationHint = calibrating ? static_cast<uint32_t>(calibration.hint()) : 0u;
                        d.calibrationProgress = calibrating ? calibration.progress() : 0.f;
                        d.eyeHeightMm = static_cast<uint32_t>(std::lround(calibratedEye * 1000));
                        d.armLengthMm = static_cast<uint32_t>(std::lround(calibratedArm * 1000));
                        d.calibrations = calibrations;
                        d.calibrationSkips = calibrationSkips;
                        d.calibrationFlags = calibrationPrompt ? 0u : 1u;
                        d.calibrationReach[0] = lastCalibration.reach[0];
                        d.calibrationReach[1] = lastCalibration.reach[1];
                        std::memcpy(d.head, motion.head.data(), 64);
                        const Mat4 basis = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto pose = native_view::relativePose(basis, motion.hands[i]);
                            std::memcpy(d.hands[i], pose.data(), 64);
                        }
                        InterlockedIncrement64(&d.sequence);
                    }
                    // Copies of the game's presented frame, for the screen.
                    if (gpuStarted)
                        native_gpu::wantScreen(viewing && !motion.active);
                    showScreen = screen;
                    if (!viewing)
                        return false;
                    if (!gpuStarted) {
                        // From here the headset shows the game: its screen until
                        // gameplay, then the eye views.
                        native_gpu::Config gpu;
                        gpu.pid = config.pid;
                        gpu.base = config.base;
                        gpu.queue = config.queue;
                        gpu.durationMs = moduleDuration;
                        gpu.copyToXr = 2;
                        gpu.captureImages = config.options & 1;
                        check(native_gpu::start(&gpu), "Start native GPU bridge");
                        gpuStarted = true;
                        nativeDeadline =
                            config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
                        message(3, 0, "Headset active");
                        return false; // initialization can exceed one predicted display interval
                    }
                    // Without gameplay the screen shows the game's presented
                    // frame; until it is up, the last image stays. The eye
                    // views get no command and stop rendering meanwhile.
                    if (!motion.active)
                        return screen || gameScreen.holding();
                    if (!nativeStarted) {
                        struct NativeConfig {
                            uint32_t magic, version, bytes, pid;
                            uint64_t base;
                            uint32_t duration, flags, width, height;
                        };
                        // Flag 8: while immersive, the game's active view follows the
                        // head, so work the game does only for that view fits the eyes.
                        // Flag 16: each eye culls what its own previous frame hid, as the
                        // game's views do; without it the render thread did 2-4 times the
                        // work and held VR at 40-65 frames a second.
                        const uint32_t flags = ((config.options & 4) ? 5 : 13) | ((config.options & 16) ? 0 : 16);
                        NativeConfig views{
                            0x53534346, 1, 40, config.pid, config.base, moduleDuration, flags, dimensions[0],
                            dimensions[1]};
                        check(SpidyStart(&views), "Start native views");
                        nativeStarted = true;
                        // Game-drawn webs are optional: without them the overlay draws webs.
                        websStarted = !(config.options & 2) && !native_webs::start(config.base, player.record);
                        // The player's body is optional too: without it the hero stays
                        // hidden and the overlay draws gloves, as before.
                        bodyStarted = values.body && !native_body::start(config.base);
                        firstImageDeadline = GetTickCount64() + 3000;
                        message(3, 0, "Gameplay found; waiting for the first eye images");
                        return false; // initialization can exceed one predicted display interval
                    }
                    if (const float h = frame.head.position.y; std::isfinite(h) && h > .8f && h < 2.4f)
                        standingEye = standingEye <= 0 ? h
                                      : h > standingEye
                                          ? standingEye + (h - standingEye) * std::min(1.f, frame.seconds * 4)
                                          : standingEye - std::min(standingEye - h, frame.seconds * .01f);
                    if (bodyStarted) {
                        // The hero's body follows the headset and the controllers,
                        // placed from the same feet as the eyes.
                        native_body::Command pose;
                        pose.serial = serial;
                        pose.flags = (flatScreen || !values.body ? 0u : native_body::bodyOn) | native_body::hideHead |
                                     native_body::handTurn | (swingState.owned ? native_body::airborne : 0u) |
                                     (calibratedEye > 0 ? native_body::calibrated : 0u);
                        pose.eyes = Vec3{(motion.eyes[0][12] + motion.eyes[1][12]) / 2,
                                         (motion.eyes[0][13] + motion.eyes[1][13]) / 2,
                                         (motion.eyes[0][14] + motion.eyes[1][14]) / 2} -
                                    motion.anchor;
                        pose.facing = motion.headPose.orientation;
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = frame.hands[i];
                            const bool tracked = hand.valid && validTrackedPose(hand.grip);
                            const Vec3 relative = hand.grip.position - frame.head.position;
                            const float seconds =
                                std::isfinite(frame.seconds) && frame.seconds > 0 ? frame.seconds : 1.f / 90;
                            const float speed =
                                tracked && gripsSeen[i] ? length(relative - gripsBefore[i]) / seconds : 0.f;
                            gripsBefore[i] = relative;
                            gripsSeen[i] = tracked;
                            const float wanted = std::max(std::clamp(hand.squeeze, 0.f, 1.f),
                                                          std::clamp((speed - 1.2f) / 1.3f, 0.f, 1.f));
                            fists[i] = tracked ? std::max(wanted, fists[i] - seconds * 3) : 0.f;
                            pose.hands[i] = {motion.grips[i].position - motion.anchor, motion.grips[i].orientation,
                                             tracked ? 1u : 0u, fists[i]};
                        }
                        pose.height = calibratedEye > 0 ? calibratedEye : standingEye;
                        pose.armLength = calibratedArm;
                        pose.trackingYaw = motion.swing.trackingYaw;
                        // A flip's tilt in world axes: the body turns with the player.
                        if (const Quat tilt = motion.swing.tilt; tilt.x != 0 || tilt.y != 0 || tilt.z != 0) {
                            const Quat yaw = Quat::yaw(motion.swing.trackingYaw);
                            pose.tilt = yaw * tilt * yaw.conjugate();
                        }
                        native_body::submit(pose);
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
                    // The HUD panel for this command's frame, as the head sees it:
                    // fixed in the room, so a reused image the headset turns to a
                    // newer head shows it where it was.
                    const Quat hudFacing = hudFollow.update(frame.head.orientation, frame.seconds);
                    native_hud::follow(eyes.serial, frame.head.orientation.conjugate() * hudFacing);
                    NativeEyeFrame saved;
                    saved.serial = eyes.serial;
                    saved.motion = motion;
                    saved.trackingEyes = frame.eyes;
                    saved.flatScreen = flatScreen;
                    saved.screenPose = screenPose;
                    saved.screenAspect = SpidyFlatAspect();
                    saved.slow = slowMotion.view();
                    for (unsigned i = 0; i < 2; ++i) {
                        saved.webs[i] = swingState.webs[i];
                        saved.webTimes[i] = webTimes[i];
                        saved.aims[i] = aims.hands[i];
                    }
                    if (calibration.phase() != body_calibration::Phase::idle) {
                        // The panel stays where it appeared in the room; the tracking
                        // space is where this frame's head puts it in the world.
                        auto& view = saved.calibration;
                        view.phase = calibration.phase();
                        view.hint = calibration.hint();
                        view.progress = calibration.progress();
                        view.ready = calibration.armsReady();
                        view.result = calibration.result();
                        view.result.eyeHeight = calibratedEye;
                        view.result.armLength = calibratedArm;
                        view.panel = compose(compose(motion.headPose, inverse(frame.head)), calibrationPanel);
                        for (unsigned i = 0; i < 2; ++i) {
                            view.handTracked[i] = frame.hands[i].valid && validTrackedPose(frame.hands[i].grip);
                            view.grips[i] = motion.grips[i];
                        }
                    }
                    history.remember(saved);
                    const auto code = SpidySetEyes(&eyes);
                    if (code && code != 4003)
                        check(code, "Tracked eye command");
                    return !code;
                },
                [&](std::array<XrRuntime::EyeTarget, 2>& targets) {
                    const auto copyStart = std::chrono::steady_clock::now();
                    native_gpu::Screen image;
                    if (showScreen && native_gpu::latestScreen(image, imageHoldMs)) {
                        // The game's presented frame, scaled into the image the screen shows.
                        const auto& left = targets[0];
                        overlay.blit(image.texture.Get(), image.view, image.linear,
                                     {left.texture, left.format, left.width, left.height, {}});
                        copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                           copyStart)
                                     .count();
                        runtime.presentation(true, screenPose,
                                             static_cast<float>(image.width) / static_cast<float>(image.height),
                                             vr_settings::screenWidth(values.screenSize));
                        drawn = Presentation::gameScreen;
                        D3D12Renderer::Captured pixels;
                        if (snapshotTicket && overlay.captured(snapshotTicket, pixels)) {
                            publishSnapshot(pendingSnapshot, &pixels);
                            snapshotTicket = 0;
                        }
                        if (!snapshotTicket && snapshots.due(GetTickCount64(), 0, false) &&
                            (snapshotTicket = overlay.capture(left.texture)) != 0) {
                            pendingSnapshot = {};
                            pendingSnapshot.flatScreen = 1;
                        }
                        std::lock_guard lock(telemetry);
                        InterlockedIncrement64(&SpidyXrData.sequence);
                        SpidyXrData.serial = SpidyXrData.generation = 0;
                        InterlockedIncrement64(&SpidyXrData.sequence);
                        return true;
                    }
                    uint64_t imageSerial{}, imageGeneration{};
                    bool copied = native_gpu::copyLatest(targets[0].texture, targets[1].texture, imageSerial,
                                                         imageGeneration);
                    check(native_gpu::error(), "GPU eye bridge");
                    const auto* saved = copied ? history.find(imageSerial, displayTime) : nullptr;
                    copied = copied && saved;
                    copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                       copyStart)
                                 .count();
                    if (copied) {
                        drawn = saved->flatScreen ? Presentation::flat : Presentation::immersive;
                        runtime.presentation(saved->flatScreen, saved->screenPose, saved->screenAspect,
                                             vr_settings::screenWidth(values.screenSize));
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
                        // Where the overlay puts each tracked web shooter, and
                        // where the game's rope for this image's frame starts.
                        Vec3 shooters[2]{}, ropeStarts[2]{};
                        uint32_t shooterHands{}, ropeHands{};
                        float webGap = -1;
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
                            // With the player's body on the hero, the game draws its own hands.
                            if (!native_body::drawn())
                                addTrackedHand(vertices, pose, hand, rendered.swing.hands[hand].grip,
                                               web.attached != 0);
                            shooters[hand] = webWrist(pose);
                            shooterHands |= 1u << hand;
                            if (gameWeb) {
                                if (web.attached &&
                                    native_eyes::renderWebStart(imageGeneration, hand, ropeStarts[hand])) {
                                    ropeHands |= 1u << hand;
                                    webGap = std::max(webGap, length(shooters[hand] - ropeStarts[hand]));
                                }
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
                        // Aim markers: where each free hand's web button would send its
                        // web, on the line that hand points along in this image, that
                        // line steadied from image to image. Translucent, the farthest
                        // drawn first.
                        std::vector<AimMarker> marks;
                        for (unsigned hand = 0; hand < 2; ++hand) {
                            auto& steady = aimMotion[hand];
                            const auto& input = rendered.swing.hands[hand];
                            if (saved->flatScreen) {
                                steady.reset();
                                continue;
                            }
                            steady.begin(shown);
                            if (!input.tracked) {
                                steady.markers(nullptr, {}, {}, marks);
                                continue;
                            }
                            const auto& aim = saved->aims[hand];
                            const auto kind = static_cast<game_swing::AimKind>(aim.kind);
                            // The hand moved with the player to this image, as its glove does.
                            const Vec3 origin = input.aim.position + travel;
                            const Vec3 direction =
                                steady.aim(normalized(input.aim.orientation.rotate({0, 0, -1})),
                                           trackingTurn(rendered.swing));
                            const float reach = length(aim.point - input.aim.position);
                            AimMarker marker;
                            marker.squeeze = input.grip;
                            marker.hand = hand;
                            switch (kind) {
                            case game_swing::AimKind::prop:
                            case game_swing::AimKind::character:
                                marker.kind = AimMark::target;
                                marker.point = aim.point;
                                marker.radius = aim.radius;
                                break;
                            case game_swing::AimKind::anchor:
                                marker.point = onAimLine(aim.point, aim.normal, origin, direction);
                                break;
                            case game_swing::AimKind::air:
                                marker.kind = AimMark::air;
                                marker.point = origin + direction * reach;
                                break;
                            default:
                                marker.kind = AimMark::blocked;
                                marker.point = length(aim.normal) > .5f
                                                   ? onAimLine(aim.point, aim.normal, origin, direction)
                                                   : origin + direction * reach;
                                break;
                            }
                            // Not on the floor a lowered hand points at, by the player's feet.
                            const float away = length(marker.point - origin);
                            const float nearest = marker.kind == AimMark::target    ? .5f
                                                  : marker.kind == AimMark::blocked ? 3.f
                                                                                    : 2.f;
                            const bool wanted = kind != game_swing::AimKind::none &&
                                                kind <= game_swing::AimKind::character &&
                                                !saved->webs[hand].attached && away >= nearest;
                            steady.markers(wanted ? &marker : nullptr, origin, direction, marks);
                            markersDrawn += wanted;
                        }
                        std::sort(marks.begin(), marks.end(), [&](const AimMarker& a, const AimMarker& b) {
                            return length(a.point - viewer) > length(b.point - viewer);
                        });
                        std::vector<Vertex> translucent;
                        for (const auto& mark : marks)
                            appendAimMarker(translucent, mark, viewer, pixelAngle * markerPixels);
                        // Slow motion's focus meter over the back of the left wrist, moved
                        // with the player to this image as the glove is.
                        if (!saved->flatScreen && saved->slow.meter > 0 && rendered.swing.hands[0].tracked) {
                            auto wrist = rendered.hands[0];
                            wrist.position += travel;
                            const double clock = std::fmod(static_cast<double>(shown) * 1e-9, 3600.);
                            appendSlowMotionMeter(translucent, saved->slow, wrist, viewer, pixelAngle,
                                                  static_cast<float>(clock));
                        }
                        // The T-pose calibration's panel and controller rings, moved with
                        // the player to this image as the hands are.
                        if (!saved->flatScreen && saved->calibration.phase != body_calibration::Phase::idle) {
                            auto view = saved->calibration;
                            view.panel.position += travel;
                            for (auto& grip : view.grips)
                                grip.position += travel;
                            body_calibration::appendView(vertices, view, viewer);
                        }
                        std::array<D3D12Renderer::ViewTarget, 2> overlayViews;
                        // Slow motion recolours the image under the overlay, about each eye's lens.
                        D3D12Renderer::Grade grade;
                        grade.amount = saved->slow.blend;
                        grade.ripple = saved->slow.ripple;
                        grade.rippleStrength = saved->slow.entering ? 1.f : .7f;
                        grade.entering = saved->slow.entering;
                        for (unsigned eye = 0; eye < 2; ++eye) {
                            const auto& f = rendered.fovs[eye];
                            grade.tangents[eye] = {std::tan(f.left), std::tan(f.right), std::tan(f.up),
                                                   std::tan(f.down)};
                        }
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
                            overlay.renderViews(overlayViews, vertices, true, false, translucent, &grade);
                        overlayMs = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - overlayStart)
                                        .count();
                        if (webGap >= 0 && imageGeneration != gapGeneration) {
                            native_eyes::reportWebGap(webGap);
                            gapGeneration = imageGeneration;
                        }
                        // Session report: publish a finished copy of the left
                        // eye, then queue the next when one is due.
                        D3D12Renderer::Captured pixels;
                        if (snapshotTicket && overlay.captured(snapshotTicket, pixels)) {
                            publishSnapshot(pendingSnapshot, &pixels);
                            snapshotTicket = 0;
                        }
                        const float speed = swingState.status ? length(swingState.velocity) : 0;
                        if (!snapshotTicket && snapshots.due(GetTickCount64(), speed, ropeHands != 0) &&
                            (snapshotTicket = overlay.capture(targets[0].texture)) != 0) {
                            pendingSnapshot = {};
                            pendingSnapshot.flags = ropeHands;
                            pendingSnapshot.generation = imageGeneration;
                            pendingSnapshot.serial = imageSerial;
                            pendingSnapshot.flatScreen = saved->flatScreen;
                            pendingSnapshot.speed = speed;
                            pendingSnapshot.webGap = webGap;
                            const auto& eye = overlayViews[0];
                            for (unsigned hand = 0; hand < 2; ++hand) {
                                if (shooterHands & (1u << hand))
                                    eyePixel(eye.viewProjection, shooters[hand], eye.width, eye.height,
                                             pendingSnapshot.wrist[hand][0], pendingSnapshot.wrist[hand][1]);
                                if (ropeHands & (1u << hand))
                                    eyePixel(eye.viewProjection, ropeStarts[hand], eye.width, eye.height,
                                             pendingSnapshot.ropeStart[hand][0],
                                             pendingSnapshot.ropeStart[hand][1]);
                            }
                        }
                    }
                    std::lock_guard lock(telemetry);
                    auto& d = SpidyXrData;
                    InterlockedIncrement64(&d.sequence);
                    d.dropped += !copied;
                    d.serial = imageSerial;
                    d.generation = imageGeneration;
                    d.markers = markersDrawn;
                    InterlockedIncrement64(&d.sequence);
                    return copied;
                });
            // No headset frame for a while: slow motion ends on its own, in real time.
            if (const auto now = GetTickCount64(); now - slowSteppedMs >= 200)
                stepSlowMotion(static_cast<float>(now - slowSteppedMs) / 1000, false, false);
            const auto& timing = runtime.lastFrameTiming();
            if (submittedFrames >= 30 && timing.period > 0) {
                const double stages[] = {timing.total,   timing.wait,  timing.tracking, timing.prepare,
                                         timing.acquire, copyMs,       overlayMs,       timing.release,
                                         timing.end,     timing.period};
                std::lock_guard lock(telemetry);
                InterlockedIncrement64(&SpidyXrTimingData.sequence);
                for (unsigned i = 0; i < 10; ++i)
                    SpidyXrTimingData.stages[i].add(stages[i]);
                InterlockedIncrement64(&SpidyXrTimingData.sequence);
            }
            // Count only a successful xrEndFrame containing a projection layer.
            // A GPU copy alone does not confirm that OpenXR accepted the frame.
            const bool submitted = runtime.lastFrameSubmitted();
            if (submitted) {
                ++submittedFrames;
                lastPresentedMs = GetTickCount64();
                if (!presented || drawn != shownKind) {
                    presented = true;
                    shownKind = drawn;
                    message(3, 0,
                            drawn == Presentation::gameScreen
                                ? "Game screen (menus, loading, cutscenes): the controllers work as an Xbox "
                                  "controller, VR settings are in Settings > SPIDY VR - VR resumes with gameplay"
                            : drawn == Presentation::flat
                                ? "Flat screen - click both thumbsticks for VR"
                                : "VR active - B interacts, Y opens the game menu, the menu button pauses, "
                                  "X shows or hides aim markers, the left thumbstick's click slows time; "
                                  "click both thumbsticks for flat screen");
                }
            }
            {
                std::lock_guard lock(telemetry);
                auto& d = SpidyXrData;
                InterlockedIncrement64(&d.sequence);
                d.presentation = static_cast<uint32_t>(submitted ? drawn : Presentation::none);
                if (submitted) {
                    ++d.submitted;
                    // Game-screen frames are not eye images: neither new nor reused ones.
                    if (drawn == Presentation::gameScreen) {
                        ++d.screenSubmitted;
                    } else if (d.generation != previousPresentedGeneration) {
                        ++d.uniqueSubmitted;
                        lastNewImageMs = lastPresentedMs;
                        previousPresentedGeneration = d.generation;
                    } else {
                        ++d.reusedSubmitted;
                    }
                }
                InterlockedIncrement64(&d.sequence);
            }
            if (!active)
                break;
        }
        message(6, 0, "Stopping VR and restoring game hooks");
    } catch (const std::exception& e) {
        error = 1;
        failure = e.what();
    }
    players.stop();
    if (padInstalled)
        game_pad::uninstall();
    game_menu::uninstall();
    // The game's own time back.
    game_time::uninstall();
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
    if (bodyStarted) {
        const auto code = native_body::stop();
        if (code) {
            error = code;
            failure = "The player's body did not stop cleanly";
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
        config.version != 20 || config.bytes != sizeof(config) || config.pid != GetCurrentProcessId() ||
        config.runtime[std::size(config.runtime) - 1] ||
        config.base != reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)) ||
        !GetModuleHandleW(L"Spider-Man.exe") || !config.queue || !config.bridgeModule || !config.rayModule ||
        !config.motionModule || config.options > 32767 || config.snapTurn > 90 || config.haptics > 100 ||
        config.screenSize > 2 || config.smoothTurn > 360 || config.hud > 3 || config.flipSpeed < 90 ||
        config.flipSpeed > 480 || !std::isfinite(config.swingSpeed) || config.swingSpeed < 1 ||
        config.swingSpeed > 65 || (config.durationMs && config.durationMs < 2000) || config.durationMs > 25000 ||
        (config.eyeSize && !validEyeSize(config.eyeSize)) || !validRenderScale(config.renderScale) ||
        (config.eyeSize && config.renderScale != 100) || config.weight < 40 || config.weight > 300)
        return 1001;
    // A calibration is both measurements or neither.
    if ((config.eyeHeightMm == 0) != (config.armLengthMm == 0) ||
        (config.eyeHeightMm &&
         (config.eyeHeightMm < body_calibration::minEyeHeightMm || config.eyeHeightMm > body_calibration::maxEyeHeightMm ||
          config.armLengthMm < body_calibration::minArmLengthMm || config.armLengthMm > body_calibration::maxArmLengthMm)))
        return 1001;
    auto module = reinterpret_cast<HMODULE>(config.bridgeModule);
    submitInput = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidySubmit"));
    sampleBridge = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidyBridgeSample"));
    stopBridge = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidyStop"));
    retargetBridge = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidyRetarget"));
    auto rays = reinterpret_cast<HMODULE>(config.rayModule);
    startRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRayStart"));
    submitRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRaySubmit"));
    stopRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRayStop"));
    startSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingStart"));
    submitSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingSubmit"));
    sampleSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingSample"));
    stopSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingStop"));
    retargetSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingRetarget"));
    // Optional: an older ray module has no web grab, nor punching, nor aim previews.
    sampleGrab = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyGrabSample"));
    sampleAim = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyAimSample"));
    startPunch = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyPunchStart"));
    samplePunch = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyPunchSample"));
    // Optional too: without them the VR settings' grab, speed and punch
    // switches take effect only at the next session.
    stopPunch = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyPunchStop"));
    swingSettings = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingSettings"));
    // Optional: an older ray module has no web shooter.
    startShooter = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyShooterStart"));
    sampleShooter = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyShooterSample"));
    stopShooter = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyShooterStop"));
    if (!submitInput || !sampleBridge || !stopBridge || !retargetBridge || !startRays || !submitRays ||
        !stopRays || !startSwing || !submitSwing || !sampleSwing || !stopSwing || !retargetSwing)
        return 1002;
    stopRequested = false;
    // The worker attaches each player it finds; none yet.
    native_appearance::setPlayerRecord(0);
    keepAliveDeadline = GetTickCount64() + 10000;
    message(1, 0, "Starting OpenXR");
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
