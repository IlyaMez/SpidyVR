// Tests the engine's own secondary scene views on its view-maintenance thread.
// The original scene pass remains owned by the game. No desktop capture is used.
#include "spidy/eye_job_table.hpp"
#include "spidy/eye_resolution.hpp"
#include "spidy/native_appearance.hpp"
#include "spidy/native_body.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_eye_gpu.hpp"
#include "spidy/native_hud.hpp"
#include "spidy/native_stream.hpp"
#include "spidy/native_view.hpp"
#include "spidy/native_webs.hpp"
#include "spidy/near_clip.hpp"
#include "spidy/recording_fov.hpp"
#include "spidy/spectator_smoothing.hpp"
#include "spidy/steady_hud.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
using native_view::Descriptor;
// NEAR CLIP's near plane for the head's and the eyes' lenses in immersive VR
// (near_clip.hpp), metres: the XR worker sets it, each frame's lenses read it.
std::atomic<float> nearClip{.02f};
void spidy::near_clip::set(int millimetres) {
    nearClip.store(static_cast<float>(std::clamp(millimetres, 10, 100)) / 1000, std::memory_order_relaxed);
}
float spidy::near_clip::metres() {
    return nearClip.load(std::memory_order_relaxed);
}
// RECORDING FOV's angle (recording_fov.hpp), degrees, 0 for off: the XR
// worker sets it, each frame's rest and the game's view's lens read it.
std::atomic<int> recordingFov{}, recordingSmoothLevel{};
SpectatorSmoothing recordingSmoothing;
uint64_t recordingTick{};
void spidy::recording_fov::smoothing(int level) {
    recordingSmoothLevel.store(std::clamp(level, 0, 100), std::memory_order_relaxed);
}
void spidy::recording_fov::set(int degrees) {
    recordingFov.store(recording_fov::sanitized(degrees), std::memory_order_relaxed);
}
struct Config {
    uint32_t magic, version, bytes, pid;
    uint64_t base;
    uint32_t durationMs, createViews, width, height;
};
struct EyeSample {
    uint64_t view{}, texture{}, textureObject{}, updates{};
    // occlusion: the eye has its own occlusion object (view +1ba0).
    uint32_t flags{}, width{}, height{}, occlusion{};
    float pose[16]{};
    uint64_t readback{};
    uint32_t readbackBytes{}, readbackCompleted{};
    uint32_t renderWidth{}, renderHeight{}, viewportWidth{}, viewportHeight{};
};
struct Data {
    uint32_t magic = 0x53535042, version = 4, bytes = sizeof(Data), state{};
    volatile LONG64 sequence{};
    uint64_t frames{}, created{}, retired{}, primary{};
    uint32_t thread{}, error{};
    EyeSample eyes[2];
};
static_assert(sizeof(Config) == 40 && sizeof(EyeSample) == 144 && sizeof(Data) == 352);
// Diagnostic (SpidyCommandTrace): render commands queued, by command and by
// the view whose list took them (0 none of these, 1 the game's view, 2 and 3
// the eyes).
struct CommandData {
    uint32_t magic = 0x53434d44, version = 1, bytes = sizeof(CommandData), tracing{};
    uint64_t frames{};
    uint64_t counts[4][256]{};
};
// The eyes' shadow sets (SpidyEyeShadows): what each eye's scene submit made or
// took, how far the eyes' own key-light cascades were apart when last
// compared (the largest difference of a transform's and a half size's number),
// and how many cascade sets were fitted to the lens covering both eyes. An
// eye's `slot` is its nearest cascade's shadow map size.
struct ShadowEye {
    uint64_t submits{}, taken{};
    uint32_t flags{}, thread{};
    uint8_t cascades[2]{}, mayTake{}, order{};
    uint16_t lights{}, slot{};
    float extent[4][3]{};
    float pose[4][16]{};
};
struct ShadowData {
    uint32_t magic = 0x53534844, version = 1, bytes = sizeof(ShadowData), mode{};
    volatile LONG64 sequence{};
    uint64_t passes{}, compared{}, covered{};
    float poseDelta{}, extentDelta{}, poseDeltaMost{}, extentDeltaMost{};
    ShadowEye eyes[2];
};
static_assert(sizeof(ShadowEye) == 336 && sizeof(ShadowData) == 736);
extern "C" {
__declspec(dllexport) CommandData SpidyCommandData;
__declspec(dllexport) ShadowData SpidyShadowData;
__declspec(dllexport) Data SpidyStereoData;
__declspec(dllexport) native_eyes::FrameData SpidyStereoFrames;
__declspec(dllexport) native_appearance::Data SpidyAppearanceData;
// The far buildings' cone and the texture budget (native_stream.hpp).
__declspec(dllexport) native_stream::Data SpidyStreamData;
// Frames in which the eyes marked what they drew for the game (rested, below).
__declspec(dllexport) volatile LONG64 SpidyDrawnFrames;
}
namespace {
using Maintain = void (*)(void*);
using Create = void* (*)(void*, const char*, float, float, float, uint32_t, uint32_t, uint32_t, void*);
using Submit = void (*)(void*, const Descriptor*);
using SetPose = void (*)(Descriptor*, const float*);
using Lens = void (*)(Descriptor*, float, float, float, float, float, float, float, float, float, float, bool,
                      bool);
using Tone = uint64_t (*)(void*, void*, void*, void*, void*, uint64_t);
using RenderOffscreen = uint8_t (*)();
Maintain originalMaintain{}, originalUpdate{}, originalCopyFinal{};
Tone originalTone{};
Submit originalSubmit{};
RenderOffscreen originalRenderOffscreen{};
void* submitHook{};
void* renderOffscreenHook{};
// The stock camera's latest submit was replaced by the tracked head view.
std::atomic<bool> activeAligned{};
using RenderActor = void (*)(void*, void*, uint8_t);
using SetupDisplay = void (*)(void*, void*, void*, uint32_t);
using Visibility = void (*)(void*);
RenderActor originalRenderActor{};
SetupDisplay originalSetupDisplay{};
void* renderActorHook{};
void* setupDisplayHook{};
std::atomic<uint64_t> playerRecord{}, localActor{};
// latchedHide: immersive, and the eyes must not draw the hero, because the
// body (native_body) is not turning its joints with its head shrunk.
std::atomic<bool> latchedImmersive{}, latchedHide{};
// The hero's render instance and world transform for the frame the main
// thread just prepared. Render jobs read the newest of several slots.
struct HeroFrame {
    uintptr_t instance{};
    float transform[16]{};
};
std::array<HeroFrame, 4> heroFrames{};
std::atomic<uint32_t> heroSlot{};
// Instance whose native visibility this module switched off, and its handle.
std::atomic<uintptr_t> hiddenHero{};
uint32_t hiddenHandle{};
struct RenderOffset {
    uint64_t generation{};
    Vec3 offset{};
    // Game rope starts of the same frame (bit per hand in `ropes`).
    Vec3 ropeStarts[2]{};
    uint32_t ropes{};
};
std::array<RenderOffset, 64> offsets{};
SRWLOCK offsetLock = SRWLOCK_INIT;
// Hero position the active view was placed from this frame (main thread).
Vec3 activeHeroPosition{};
bool activeHeroPlaced{};
// Frames end with view maintenance (main thread). placedFrame is the frame
// whose eye poses were placed last; a job copied in a later frame got a late pose.
uint64_t frameIndex{}, placedFrame{};
// Diagnostic comparison only (SpidyEyePlacement): place eyes in maintenance,
// as builds before October 5 did.
std::atomic<bool> lateEyes{};
// Diagnostic comparison only (SpidyEyeExposure): each eye is exposed from its
// own adapted luminance, as builds before October 10's sixth did.
std::atomic<bool> ownExposure{};
using InitBuffers = bool (*)(void*, const void*, const char*);
InitBuffers originalInitBuffers{};
void* initBuffersHook{};
// The eyes' temporal anti-aliasing and upsampling (eyeTemporal below).
// 1874f40 sizes a buffer set: the render size (+12bf0, +12bf4) from its
// arguments, and the size its anti-aliased image has (+12bf8 to +12c04, where
// kTaaTarget0/1, kResolved and their kind are made) from the game's display
// (7a2ef00), whatever the view. An eye's set gets the eye's own size there.
using SizeBuffers = void (*)(void*, const char*, uint32_t, uint32_t, uint32_t, uint8_t);
SizeBuffers originalSizeBuffers{};
void* sizeBuffersHook{};
// The eye whose buffers 1873470 is sizing on this thread: its image's size.
thread_local uint32_t sizingEye[2]{};
// Percent of the eye's size, per side, its scene is rendered at (100: all of
// it), and what the eyes get (bits): 1 a jittered lens, 2 the engine's
// temporal anti-aliasing (which upsamples a smaller scene to the eye's size),
// 4 its render command where the game's settings leave it out, 8 no targets
// for screen-space reflections (as before October 11's second build: the game
// has them off, or a comparison). The percent and bits 2 and 8 are read when
// an eye's buffers are made. (Clearing the job's context +710 in copyJob does
// not stop the reflection pass: tried on October 11, the eyes kept them.)
std::atomic<uint32_t> eyeRenderPercent{100}, eyeTemporal{};
// A view's scene submit, 19206a0(view, parent, flags, last), returns what it
// made for the frame (0x58 bytes: +18 the view's light set with its shadow
// records, +28 its KeyLightContext, the key light's shadow cascades, +30,
// ...). Given another view's as parent, a view whose +120a is set (every
// view's is, from 189d3d2) copies the parent's KeyLightContext and takes its
// light set instead of making its own (1920eeb, 19212e9): it then renders no
// shadow maps and samples the parent's. The game passes no such parent to an
// offscreen view (186d72a), so each eye rendered every cascade again, 6 cm
// from the other eye's. eyeShadows (bits): the eye submitted second in a
// frame takes from the first 1 the key light's cascades, 2 the light set,
// 4 the part at +30.
// A view's cascades are fitted to the view: 1716270 calls 170fbc0(context,
// view, ...), which reads the pose and the lens from the view's descriptor
// (the view's first 0x440 bytes) and places each cascade's box about that
// lens's part of the scene. Two eyes with mirrored lenses got boxes most of
// a half size apart (13.0 m of 13.2 for the nearest). So the eye submitted
// first fits its cascades to shadowLens, the head's lens covering both eyes
// (placeEyes), and the second takes those.
// The covering lens is wider than an eye's, and its boxes larger (15% a side
// for the load probe's lenses, 54 degrees out and 44 in) over shadow maps of
// the same size: 170fbc0 asks the atlas (774c4d0) for each cascade's map with
// 1740890(atlas, key, size), the size the atlas's first word (1792 here). A
// texel of the shared set so covers 15% more a side than one of an eye's own.
// Asking for larger maps was tried on October 11: the atlas had no room for
// all six, and the game's render code reads a cascade's map without checking
// that it has one (16364df).
using SubmitScene = void* (*)(void*, void*, uint32_t, uint8_t);
using FitCascades = void (*)(void*, void*, void*, void*, void*);
SubmitScene originalSubmitScene{};
FitCascades originalFitCascades{};
void* submitSceneHook{};
void* fitCascadesHook{};
std::atomic<uint32_t> eyeShadows{};
// This frame's offscreen job setup (main thread), and the eye submitted first in it.
uint64_t offscreenPass{};
struct ShadowDonor {
    uint64_t pass{};
    void* view{};
    bool covers{};
    uintptr_t part[11]{};
} shadowDonor;
Descriptor shadowLens{};
uint64_t shadowLensPass = UINT64_MAX;
// Both eyes have one lens and pose this frame (the flat screen): each keeps
// its own set there (sharing it is untried).
bool shadowLensSame{};
// The eye whose scene submit is fitting its cascades to shadowLens.
void* shadowFitting{};
// Diagnostic (SpidyCommandTrace): the newest render job of the game's view and of each eye.
std::atomic<uintptr_t> tracedJobs[3]{};
// The game's own view while this module keeps it from rendering (restGameView).
std::atomic<uintptr_t> rested{};
// What the player sees, for the game. Each scene submit clears its view's
// list of drawn instances (1920e54: view +1b80, +1b88 entries) and the view's
// model jobs mark every instance they draw (1799a7d, through job +2b0).
// Gameplay reads the lists to know what is on screen: the
// ViewportUpdateSystem's enter and leave events from the active view's
// (214f150), and "drawn in any view" from the pool views' (18a1890; for an
// instance 159e940: pedestrians, targeting, about 60 callers in all).
// Offscreen views get no list (189ce20 skips kind 2), so the eyes marked
// nothing, and a resting game view submits no scene, so its list kept the last
// frame that view drew: from 0.2.12 on nothing the player saw in VR counted as
// on screen. While the view rests, placeEyes clears its list each frame and
// the eyes' model jobs mark into it (setupModels); SpidyDrawnFrames counts
// those frames.
// The eyes' distances (createViews bit 64 off). A wide lens makes the engine
// draw a model's detail, and the model itself, only within a shorter distance
// and ask for its textures only nearer (native_view::distanceScale): an eye's
// 98 degrees gave 0.59 where the game's camera has 1.06, so windows came onto
// buildings, people into the streets and textures into focus at about half
// the flat game's distance. Each eye gets the 68-degree lens's distances
// (matchDistances, view +1cc8), and its model jobs ask for textures as for its
// image, not for the smaller scene its upscaling renders (setupModels, +2fc).
using SetupModels = uint64_t (*)(void*, void*, void*, uint64_t, uint64_t);
SetupModels originalSetupModels{};
void* setupModelsHook{};
bool distances{};
// The far buildings' full models (createViews bit 128 off). Far buildings are
// impostors, and only the engine's active view (189daa0: the view at
// ViewContextManager +f8) has the impostors it sees draw their full models,
// with the lit windows and the rooms behind them: ImpostorHQModelRegJob
// (16fb630, 16fb750) applies that view's wanted state per impostor, from its
// visibility queries (16faf10), and the impostor submit job (16f8afa) marks
// the view. A resting game view (restGameView) submits no scene, so in VR the
// buildings beyond the nearest stayed dark cards until a menu, which renders
// that view again, lit them. While the game view rests the left eye stands in
// for it at those five checks (isActiveView). One view only: the
// registration applies a view's wanted states over everyone's, and two views
// would undo each other's every frame.
using IsView = bool (*)(void*);
IsView originalIsActiveView{};
void* isActiveViewHook{};
bool impostors{};
// How far away an impostor draws its full model then: the eyes' visibility
// queries (16faf10) carry the game's distance in their result (+88: 145 m from
// 7a89170 or 7a89174, which region changes rewrite), and the impostor jobs
// read it after the queries return. While an eye stands in, both eyes' results
// get impostorReach percent of it (impostorQueries), so both draw alike;
// SpidyImpostorReach sets it.
using ImpostorQueries = void* (*)(void*, void*);
ImpostorQueries originalImpostorQueries{};
void* impostorQueriesHook{};
std::atomic<uint32_t> impostorReach{150};
// Which impostors that query returns is the cone about the head while the left
// eye stands in (native_stream.hpp): the lens covering both eyes, widened by
// a margin each way, placed with the eyes for the frame's offscreen pass
// (placeEyes) and turned into the 0x100-byte cull 187de80 makes, as 189fea0
// does for the eye. SpidyBuildingCone sets the margin; 0 keeps the eye's own
// lens. easedMargin is this frame's, by how full the scene's slot pool is.
bool cones{};
std::atomic<uint32_t> coneDegrees{static_cast<uint32_t>(native_stream::coneMargin)};
Descriptor impostorCone{};
uint64_t impostorConePass = UINT64_MAX;
float easedMargin{};
// The texture streamer's budget while the eyes render (keepTextureBudget):
// the game's, and the one the eyes got (0 none). SpidyTextureBudget(0) keeps
// the game's. Given back for the session where the card ran short.
bool budgets{};
std::atomic<bool> textureRaise{true};
std::atomic<uint64_t> textureGame{}, textureRaised{};
uint64_t textureChecked{};
bool textureShortened{};
// The HUD panel (native_hud.hpp): PlayerModelHudFollower's placement 73ab80,
// on the main thread after the camera submits and before 1920240. It ends in
// SetTransform 191c0e0 on the panel's render instance, which writes the
// instance's world transform (+0) and marks it changed for the renderer.
using HudPlace = void (*)(void*);
using SetTransform = void (*)(void*, const float*, void*);
HudPlace originalHudPlace{};
void* hudPlaceHook{};
// The game's placement in this frame: the panel's instance, the scales it
// got, and its distance from the game's camera (follower +48).
struct HudPlacement {
    uintptr_t instance{};
    Vec3 scale{};
    float distance{};
};
// Half the width of the stock camera's view, as a tangent: what the game's
// own view shows of the HUD panel (submit(), main thread).
float stockHalfWidth{};
// The head of one frame (main thread): the eye command the frame's views
// render, the player's travel to the frame, and the head view and lens that
// gives (pose moved by the travel). Latched at the frame's first camera submit
// (activeView), or later at the HUD's placement without one: both come after
// the frame's gameplay has moved the player. The engine's active view, the HUD
// panel and the eyes all take it, so they agree on the head in every frame: a
// transform set any later than the game's own HUD placement (in placeEyes, at
// 1920240) did not reach the frame's render, so the panel is placed in that
// placement's hook, before the eyes.
// The HUD projects its world markers earlier, before the player has moved.
// They take earlyHead(): the newest command's head, moved to where the frame
// will draw the player (heroTravel). They latched the frame's head themselves
// from October 9's fifth build to October 10's third, with the last frame's
// player: the active view, which the monitor shows, and the panel trailed the
// eyes by a frame of travel, up to 1.35 m in a swing.
struct FrameHead {
    uint64_t frame = UINT64_MAX;
    native_eyes::Command command{};
    bool controlled{}, viewed{};
    Vec3 offset{};
    Mat4 pose{};
    native_eyes::Bounds lens{};
    // The head turned the way the HUD panel faces (native_hud::Follow, sent by
    // the XR worker with the command), and whether the worker sent one.
    Mat4 panel{};
    bool followed{};
};
FrameHead frameHead, earlyFrameHead;
// The HUD panel's render instance while the HUD is off and the eyes are shown:
// renderActor leaves it out of the eye views (main thread sets it).
std::atomic<uintptr_t> hiddenHud{};
// The game's main thread (view maintenance's), the only one the frame's head
// is latched and read on.
std::atomic<DWORD> mainThread{};
thread_local bool creatingEye{};
using CopyJob = void (*)(void*, void*, void*, void*, bool);
CopyJob originalCopyJob{};
Maintain originalBeginJob{}, originalEndJob{};
uintptr_t base{};
void* maintainHook{};
void* updateHook{};
void* copyFinalHook{};
void* toneHook{};
void* copyJobHook{};
void* beginJobHook{};
void* endJobHook{};
std::atomic<void*> eyes[2]{};
std::atomic<uint64_t> updates[2]{};
std::atomic<bool> enabled{}, stopRequested{};
std::atomic<uint32_t> live{};
SRWLOCK lifecycle = SRWLOCK_INIT, telemetry = SRWLOCK_INIT, creation = SRWLOCK_INIT;
Config config{};
uint64_t deadline{};
bool hooked{}, attempted{}, retired[2]{};
SRWLOCK commandLock = SRWLOCK_INIT, jobLock = SRWLOCK_INIT;
native_eyes::Command command{};
std::atomic<uint64_t> commandDeadline{};
std::atomic<float> flatAspect{16.f / 9};
std::atomic<uint64_t> latchedSerial{}, generation{};
EyeJobTable jobs; // jobLock
// Hero rope-update samples already used by eye placement and the active view.
native_eyes::FrameHero placedHero, activeHero;
// Hero positions the frames' heads were placed from (main thread).
native_eyes::HeroTravel heroTravel;
bool read(uintptr_t p, void* out, size_t n) {
    __try {
        if (p < 0x10000)
            return false;
        std::memcpy(out, reinterpret_cast<void*>(p), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
uintptr_t pointer(uintptr_t p) {
    uintptr_t v{};
    read(p, &v, 8);
    return v;
}
bool owned(void* view) {
    if (!view)
        return false;
    for (unsigned slot = 0; slot < 8; ++slot)
        if (pointer(base + 0x7a34e00 + slot * 8) == reinterpret_cast<uintptr_t>(view))
            return true;
    return false;
}
bool entry(uintptr_t rva, const unsigned char* expected, size_t n) {
    unsigned char b[32]{};
    return n <= sizeof(b) && read(base + rva, b, n) && !std::memcmp(b, expected, n);
}
// Offscreen views are made without the occlusion culling the game's views
// have: their setup (186c1f0) passes 1 as the pool initializer's skip argument,
// so the culling (ModelOcclJob 17935a0 and a dozen other readers of +1208 and
// +1ba0) found no occlusion object and kept every instance in an eye's
// frustum, hidden behind buildings or not. 189fed0 builds the object from the
// view's render buffers (+1640), as 189ce20 does for game views; each eye's
// render job then samples the eye's own depth into it (19206a0). It stores the
// object before constructing it, so it may only run on a new eye that no
// render job has copied yet.
void giveOcclusion(void* view) {
    const auto address = reinterpret_cast<uintptr_t>(view);
    if (!pointer(address + 0x1ba0) && pointer(address + 0x1640))
        reinterpret_cast<void (*)(void*)>(base + 0x189fed0)(view);
}
void appearanceEvent(unsigned eye, bool avatar) {
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyAppearanceData;
    InterlockedIncrement64(&data.sequence);
    if (avatar)
        ++data.hiddenAvatar[eye];
    else
        ++data.srgbOverlay[eye];
    data.playerActor = localActor.load();
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
bool unitTransform(const float* m) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i]) || std::abs(m[i]) > 1e6f)
            return false;
    for (int row = 0; row < 3; ++row)
        if (std::abs(m[row * 4] * m[row * 4] + m[row * 4 + 1] * m[row * 4 + 1] + m[row * 4 + 2] * m[row * 4 + 2] -
                     1) > .02f)
            return false;
    return true;
}
// 191ba30 resolves an instance handle (index in bits 0..23, generation in
// 24..31) through the 16-byte table at 7a436e8. A live instance is its own
// table entry. DrawOff/DrawOn mark the same index in the table at 7a43710.
bool registered(uintptr_t instance, uint32_t& handle) {
    const auto table = pointer(base + 0x7a436e8);
    int32_t count{};
    uint8_t slotGeneration{};
    if (!instance || !table || !read(instance + 0x64, &handle, 4) || !(handle >> 24) ||
        !read(base + 0x7a43704, &count, 4))
        return false;
    const uint64_t index = handle & 0xffffff;
    return static_cast<int64_t>(index) < count && pointer(table + index * 16) == instance &&
           read(table + index * 16 + 8, &slotGeneration, 1) && slotGeneration == handle >> 24;
}
// Child pieces of a character share its root transform; world props do not.
bool sameRoot(const float* m, const float* hero) {
    const float dx = m[12] - hero[12], dy = m[13] - hero[13], dz = m[14] - hero[14];
    if (dx * dx + dy * dy + dz * dz > .03f * .03f)
        return false;
    for (int row = 0; row < 3; ++row)
        if (m[row * 4] * hero[row * 4] + m[row * 4 + 1] * hero[row * 4 + 1] + m[row * 4 + 2] * hero[row * 4 + 2] <
            .998f)
            return false;
    return true;
}
int eyeView(uintptr_t view) {
    if (!view)
        return -1;
    // 1920e54 passes the persistent view to both 19223e0 and 1793be0. Render
    // copies keep their owner at +1f40, as in the display-setup hook.
    const auto owner = pointer(view + 0x1f40);
    for (unsigned eye = 0; eye < 2; ++eye)
        if (const auto e = reinterpret_cast<uintptr_t>(eyes[eye].load()); e && (view == e || owner == e))
            return static_cast<int>(eye);
    return -1;
}
bool headMirror(uintptr_t view) {
    const auto primary = pointer(base + 0x7a34dd0);
    return activeAligned.load(std::memory_order_relaxed) && primary && view &&
           (view == primary || pointer(view + 0x1f40) == primary);
}
void nearInstance(unsigned eye, uintptr_t instance, const float* m, const float* hero) {
    // Diagnostic only: identify small pieces still drawn within the body,
    // measured from a point 0.9 m above the hero's feet.
    const float dx = m[12] - hero[12], dy = m[13] - hero[13] - .9f, dz = m[14] - hero[14];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    float radius{};
    if (distance > 1.1f || !read(instance + 0x4c, &radius, 4) || !std::isfinite(radius) || radius > 2.5f)
        return;
    uint32_t handle{}, flags{};
    read(instance + 0x64, &handle, 4);
    read(instance + 0x5c, &flags, 4);
    const auto model = pointer(instance + 0x88);
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyAppearanceData;
    InterlockedIncrement64(&data.sequence);
    ++data.nearInstances[eye];
    data.nearInstance = instance;
    data.nearModel = model;
    data.nearDistance = distance;
    data.nearRadius = radius;
    data.nearHandle = handle;
    data.nearFlags = flags;
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
void renderActor(void* context, void* actor, uint8_t visibility) {
    // The HUD off: its panel stays out of the eyes (the game's own view, which
    // the game screen shows, keeps it).
    if (const auto hud = hiddenHud.load(std::memory_order_relaxed);
        enabled && hud && reinterpret_cast<uintptr_t>(actor) == hud &&
        eyeView(pointer(reinterpret_cast<uintptr_t>(context) + 8)) >= 0) {
        native_hud::hiddenDraw();
        return;
    }
    // 1796d20 copies the scene view into context +8. The primary hide uses the
    // game's actor visibility switch in maintain(); this view check removes
    // anything still drawn as the hero or on the hero's root transform. It
    // changes no actor flags, animation state, or gameplay visibility. With
    // the player's body on the hero (native_body), the eyes draw it. A desktop
    // view on the tracked head needs the same fallback as the eyes.
    if (enabled && actor && latchedHide.load(std::memory_order_relaxed)) {
        const int eye = eyeView(pointer(reinterpret_cast<uintptr_t>(context) + 8));
        const auto& hero = heroFrames[heroSlot.load(std::memory_order_acquire) % heroFrames.size()];
        const auto instance = reinterpret_cast<uintptr_t>(actor);
        if ((eye >= 0 || headMirror(pointer(reinterpret_cast<uintptr_t>(context) + 8))) && hero.instance) {
            float m[16]{};
            const bool placed = instance != hero.instance && read(instance, m, sizeof(m));
            // The game's web tube and end cone are separate instances; never
            // treat them as avatar pieces.
            const bool web = placed && native_webs::webInstance(instance);
            if (instance == hero.instance || (placed && !web && sameRoot(m, hero.transform))) {
                if (eye >= 0)
                    appearanceEvent(static_cast<unsigned>(eye), true);
                return;
            }
            if (eye >= 0 && placed && !web)
                nearInstance(static_cast<unsigned>(eye), instance, m, hero.transform);
        }
    }
    originalRenderActor(context, actor, visibility);
}
// 1796d20 sets up a view's model job (the view at context +8): its distance
// scale (+2f8, view +1cc4 x +1cc8; +310, 16 times that, for texture requests)
// and how strongly it asks for textures (+2fc, requestScale of the view's
// rendered size). An upscaled eye renders its scene smaller than its image, so
// it asked for textures as for the smaller one: two thirds of the detail the
// headset shows at the default 67%. Its requests are sized for its image, as
// upscalers are meant to have them; what the job leaves out as too small to
// see (+300) stays at the rendered size. While the game's view rests, an eye's
// job marks what it draws in that view's list (+2b0; see rested above).
uint64_t setupModels(void* context, void* job, void* third, uint64_t fourth, uint64_t fifth) {
    const auto result = originalSetupModels(context, job, third, fourth, fifth);
    if (const auto resting = rested.load(std::memory_order_relaxed);
        enabled && resting && eyeView(pointer(reinterpret_cast<uintptr_t>(context) + 8)) >= 0) {
        auto& marks = *reinterpret_cast<uintptr_t*>(static_cast<uint8_t*>(context) + 0x2b0);
        if (const auto drawn = pointer(resting + 0x1b80); drawn && !marks)
            marks = drawn;
    }
    const auto view = enabled && distances ? pointer(reinterpret_cast<uintptr_t>(context) + 8) : 0;
    float half[2]{};
    if (eyeView(view) < 0 || !read(view + 0x420, half, sizeof(half)) || !(half[0] > 0) || !(half[1] > 0))
        return result;
    const float wanted = native_view::requestScale(static_cast<float>(config.width),
                                                   static_cast<float>(config.height), half[0], half[1]);
    auto& asked = *reinterpret_cast<float*>(static_cast<uint8_t*>(context) + 0x2fc);
    if (!std::isfinite(wanted) || !(asked > 0) || !(wanted > asked))
        return result;
    AcquireSRWLockExclusive(&telemetry);
    InterlockedIncrement64(&SpidyAppearanceData.sequence);
    ++SpidyAppearanceData.requestJobs;
    SpidyAppearanceData.requestRaise = wanted / asked;
    InterlockedIncrement64(&SpidyAppearanceData.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    asked = wanted;
    return result;
}
// 189daa0, "is this the active view", where the impostor code asks it (the
// return addresses after its five calls there; impostors above).
bool isActiveView(void* view) {
    if (view && view == eyes[0].load(std::memory_order_relaxed) && impostors && enabled &&
        rested.load(std::memory_order_relaxed)) {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
        if (caller == 0x16f8bb5 || caller == 0x16fb69b || caller == 0x16fb6ab || caller == 0x16fb7af ||
            caller == 0x16fb7bf) {
            if (caller == 0x16fb69b) {
                AcquireSRWLockExclusive(&telemetry);
                InterlockedIncrement64(&SpidyAppearanceData.sequence);
                ++SpidyAppearanceData.impostorFrames;
                InterlockedIncrement64(&SpidyAppearanceData.sequence);
                ReleaseSRWLockExclusive(&telemetry);
            }
            return true;
        }
    }
    return originalIsActiveView(view);
}
// Runs on the main thread in maintain(), after gameplay updated the hero and
// before render jobs read it. Uses DrawOff 191afb0 / DrawOn 191b880, the
// handlers of the game's ActorDrawAction script node (15a3460). `hide`:
// immersive without the player's body on the hero.
void updateHero(bool immersive, bool hide) {
    HeroFrame frame;
    uint32_t state = native_appearance::heroNoInstance, handle{}, flags{};
    const auto record = playerRecord.load();
    const auto instance = record ? pointer(record) : 0;
    bool drawable{};
    if (instance) {
        state = native_appearance::heroBadTransform;
        if (read(instance, frame.transform, sizeof(frame.transform)) && unitTransform(frame.transform)) {
            // Per-eye hiding and re-anchoring need only this transform. The
            // native visibility switch also requires a registered instance.
            frame.instance = instance;
            drawable = registered(instance, handle) && read(instance + 0x5c, &flags, 4);
            state = drawable ? native_appearance::heroValid : native_appearance::heroUnregistered;
        }
    }
    bool hid{}, restored{};
    if (const auto previous = hiddenHero.load(); previous && (previous != frame.instance || !hide)) {
        uint32_t current{}, previousFlags{};
        // Restore only the same live instance this module switched off.
        if (registered(previous, current) && current == hiddenHandle && read(previous + 0x5c, &previousFlags, 4) &&
            (previousFlags & 0x20)) {
            reinterpret_cast<Visibility>(base + 0x191b880)(reinterpret_cast<void*>(previous));
            restored = true;
        }
        hiddenHero = 0;
        hiddenHandle = 0;
        if (previous == frame.instance)
            read(previous + 0x5c, &flags, 4);
    }
    if (hide && drawable && !(flags & 0x20)) {
        // A hero already hidden by the game stays owned by the game.
        reinterpret_cast<Visibility>(base + 0x191afb0)(reinterpret_cast<void*>(frame.instance));
        hiddenHero = frame.instance;
        hiddenHandle = handle;
        hid = true;
        read(frame.instance + 0x5c, &flags, 4);
    }
    const auto slot = (heroSlot.load(std::memory_order_relaxed) + 1) % heroFrames.size();
    heroFrames[slot] = immersive ? frame : HeroFrame{};
    heroSlot.store(static_cast<uint32_t>(slot), std::memory_order_release);
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyAppearanceData;
    InterlockedIncrement64(&data.sequence);
    data.heroState = state;
    data.heroHandle = handle;
    data.heroFlags = flags;
    data.playerActor = instance;
    data.nativeHides += hid;
    data.nativeRestores += restored;
    data.nativeHiddenFrames += hide && drawable && (flags & 0x20);
    const auto webs = native_webs::status();
    data.webState = webs.state;
    data.webLive = webs.live;
    data.webCreates = webs.creates;
    data.webReleases = webs.releases;
    data.webFailures = webs.failures;
    data.webUpdates = webs.updates;
    data.webStartLast = webs.startError;
    data.webStartMax = webs.startErrorMax;
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
bool heroPosition(Vec3& position);
bool retiring();
// The newest eye command's head for this frame, not yet moved with the player.
FrameHead commandHead() {
    FrameHead head;
    head.frame = frameIndex;
    AcquireSRWLockShared(&commandLock);
    head.command = command;
    head.controlled = head.command.enabled && GetTickCount64() < commandDeadline;
    ReleaseSRWLockShared(&commandLock);
    if (enabled && head.controlled && head.command.enabled == 1 && !retiring() &&
        native_eyes::headView(head.command, head.pose, head.lens)) {
        head.viewed = true;
        Quat relative{};
        head.followed = native_hud::followed(head.command.serial, relative);
        head.panel = native_hud::turned(head.pose, relative);
    }
    return head;
}
// Moves a head with the player to the frame that draws him at `hero`: the
// same render-frame correction placeEyes() gives the eyes.
void moveHead(FrameHead& head, Vec3 hero) {
    native_eyes::reanchorOffset(head.command.anchor, hero, head.offset);
    for (Mat4* m : {&head.pose, &head.panel}) {
        (*m)[12] += head.offset.x;
        (*m)[13] += head.offset.y;
        (*m)[14] += head.offset.z;
    }
}
// This frame's head (FrameHead), latched at its first use in the frame: after
// the frame's gameplay has moved the player.
const FrameHead& latchHead() {
    if (frameHead.frame == frameIndex)
        return frameHead;
    // This frame's rope-update sample of the hero, if one ran since the last
    // frame (taken once a frame, immersive or not, as FrameHero requires).
    uint64_t ropeSamples{};
    Vec3 ropeHero{}, rendered{};
    native_webs::heroSample(ropeSamples, ropeHero);
    const bool shared = activeHero.fresh(ropeSamples, ropeHero);
    frameHead = commandHead();
    // The player is where that sample has him, else at the hero's render transform.
    if (frameHead.viewed && frameHead.command.anchored && (shared || heroPosition(rendered))) {
        activeHeroPosition = shared ? ropeHero : rendered;
        activeHeroPlaced = true;
        moveHead(frameHead, activeHeroPosition);
        heroTravel.placed(frameIndex, activeHeroPosition);
    }
    if (frameHead.viewed && earlyFrameHead.frame == frameIndex && earlyFrameHead.viewed) {
        // This frame's markers went to the head earlyHead() foresaw.
        const auto& early = earlyFrameHead.pose;
        const auto& pose = frameHead.pose;
        const float apart = length(Vec3{early[12] - pose[12], early[13] - pose[13], early[14] - pose[14]});
        native_hud::publish([&](native_hud::Data& d) { d.markerMismatch = std::max(d.markerMismatch, apart); });
    }
    return frameHead;
}
// This frame's head for a use that may come before the frame's gameplay has
// moved the player (the HUD's marker projections): the frame's own once it is
// latched; until then the newest command's, moved to where the frame will draw
// him, or where he is now when the last frame did not place one.
const FrameHead& earlyHead() {
    if (frameHead.frame == frameIndex)
        return frameHead;
    if (earlyFrameHead.frame == frameIndex)
        return earlyFrameHead;
    earlyFrameHead = commandHead();
    Vec3 hero{};
    if (earlyFrameHead.viewed && earlyFrameHead.command.anchored &&
        (heroTravel.ahead(frameIndex, hero) || heroPosition(hero)))
        moveHead(earlyFrameHead, hero);
    return earlyFrameHead;
}
// While immersive, the HUD panel goes in front of this frame's head, facing
// the way the XR worker's Follow turned it.
void placeHud(const HudPlacement& placed, const FrameHead& head) {
    native_hud::Shape shape;
    shape.halfWidth = native_hud::halfWidth(native_hud::size());
    Mat4 transform{};
    uint32_t handle{};
    const bool done = shape.halfWidth > 0 && registered(placed.instance, handle) &&
                      native_hud::panel(head.panel, placed.scale, placed.distance, stockHalfWidth, shape, transform);
    if (done)
        reinterpret_cast<SetTransform>(base + 0x191c0e0)(reinterpret_cast<void*>(placed.instance),
                                                         transform.data(),
                                                         reinterpret_cast<void*>(placed.instance + 0x70));
    const float turn = std::acos(std::clamp(head.pose[8] * head.panel[8] + head.pose[9] * head.panel[9] +
                                                head.pose[10] * head.panel[10],
                                            -1.f, 1.f)) *
                       180 / 3.14159265f;
    native_hud::publish([&](native_hud::Data& d) {
        if (!shape.halfWidth)
            return;
        ++(done ? d.placedFrames : d.rejectedFrames);
        if (done) {
            d.distance = shape.distance;
            d.halfWidth = shape.halfWidth;
            d.followedFrames += head.followed;
            d.followAngle = turn;
            d.followAngleMax = std::max(d.followAngleMax, turn);
        }
    });
}
// The frame whose HUD panel went onto the game screen in stereo (main thread).
uint64_t screenHudFrame = UINT64_MAX;
// On the game screen in stereo the panel goes where both eyes see it in one
// place, at the screen's own distance: toward the game's camera from where the
// game put it, until its face is at the distance the two eyes' images agree at
// (native_eyes::stereoEyes), and smaller by as much, so the camera sees it as
// before. The HUD, the subtitles and the prompts then lie on the screen.
// The panel's face is not at the instance's position: the model's bounding
// sphere (+40: its centre in the model's own axes, then its radius) lies 0.405
// of the model's units behind it, 4.46 m at the game's scale of 11 and 20 m
// (October 11: placed by its position, the HUD showed 9 to 10 of the horizon's
// 51 pixels behind the screen).
void placeScreenHud(const HudPlacement& placed, const native_eyes::Command& command) {
    Descriptor main{};
    float m[16]{}, sphere[4]{};
    uint32_t handle{}, flags{};
    // Not a panel the game has switched off (+5c bit 0x20: DrawOff).
    bool done = read(pointer(base + 0x7a34dd0), &main, sizeof(main)) && native_view::valid(main) &&
                registered(placed.instance, handle) && read(placed.instance, m, sizeof(m)) &&
                read(placed.instance + 0x5c, &flags, 4) && !(flags & 0x20);
    const auto pose = main.pose();
    const auto eyes = native_eyes::stereoEyes(
        native_eyes::stereo(command),
        {main.values[0x400 / 4], main.values[0x404 / 4], main.values[0x408 / 4], main.values[0x40c / 4]});
    const Vec3 camera{pose[12], pose[13], pose[14]}, forward{pose[8], pose[9], pose[10]};
    const Vec3 panel{m[12], m[13], m[14]};
    Vec3 face = panel;
    if (read(placed.instance + 0x40, sphere, sizeof(sphere)) && std::isfinite(sphere[0] + sphere[1] + sphere[2]) &&
        std::abs(sphere[0]) < 100 && std::abs(sphere[1]) < 100 && std::abs(sphere[2]) < 100)
        for (int row = 0; row < 3; ++row)
            face += Vec3{m[row * 4], m[row * 4 + 1], m[row * 4 + 2]} * sphere[row];
    const float depth = dot(face - camera, forward), scale = eyes.distance / depth;
    done = done && depth > .1f && std::isfinite(scale) && scale > 1e-4f && scale < 1e4f;
    if (done) {
        for (int i = 0; i < 12; ++i)
            m[i] *= scale;
        const Vec3 moved = camera + (panel - camera) * scale;
        m[12] = moved.x;
        m[13] = moved.y;
        m[14] = moved.z;
        reinterpret_cast<SetTransform>(base + 0x191c0e0)(reinterpret_cast<void*>(placed.instance), m,
                                                         reinterpret_cast<void*>(placed.instance + 0x70));
        screenHudFrame = frameIndex;
        native_hud::onScreen();
    }
    native_hud::publish([&](native_hud::Data& d) {
        ++(done ? d.placedFrames : d.rejectedFrames);
        if (done) {
            d.distance = eyes.distance;
            d.halfWidth = (main.values[0x404 / 4] - main.values[0x400 / 4]) / 2;
        }
    });
}
// After the game placed the HUD panel in front of its camera: while immersive,
// move it in front of this frame's head.
void hudPlace(void* follower) {
    originalHudPlace(follower);
    if (!enabled || !(config.createViews & 4) || lateEyes) {
        hiddenHud = 0;
        return;
    }
    const auto address = reinterpret_cast<uintptr_t>(follower);
    native_hud::follower(address);
    HudPlacement placed;
    float m[16]{};
    if (pointer(address) != base + 0x38929c8 || !(placed.instance = pointer(pointer(address + 8))) ||
        !read(placed.instance, m, sizeof(m)) || !read(address + 0x48, &placed.distance, 4)) {
        hiddenHud = 0;
        return;
    }
    placed.scale = {length(Vec3{m[0], m[1], m[2]}), length(Vec3{m[4], m[5], m[6]}), length(Vec3{m[8], m[9], m[10]})};
    native_hud::publish([&](native_hud::Data& d) {
        ++d.gameFrames;
        d.follower = address;
        d.instance = placed.instance;
        d.gameScale[0] = placed.scale.x;
        d.gameScale[1] = placed.scale.y;
        d.gameScale[2] = placed.scale.z;
        d.gameDistance = placed.distance;
        d.viewHalfWidth = stockHalfWidth;
    });
    const auto& head = latchHead();
    // The game screen in stereo shows the HUD whatever its setting, as the
    // game's own view does on the flat one.
    const bool screen = head.controlled && head.command.enabled == 3 && !retiring();
    // The HUD off: the eyes (immersive or the flat screen) leave the panel out.
    // STEADY HUD: the immersive eyes do too while the headset shows it as a
    // layer of its own (steady_hud.hpp).
    const bool layered = head.controlled && head.command.enabled == 1 && native_hud::size() &&
                         native_hud::layered(steady_hud::holdMs);
    const bool hide = (!native_hud::size() || layered) && head.controlled && !screen && !retiring();
    hiddenHud = hide ? placed.instance : 0;
    if (hide)
        native_hud::publish([](native_hud::Data& d) { ++d.hiddenFrames; });
    if (head.viewed)
        placeHud(placed, head);
    else if (screen)
        placeScreenHud(placed, head.command);
}
// `lag` is the hero's render-transform distance from the shared rope-update
// sample the eyes used, or negative when they used the render transform.
// `activeLag` is the distance from the hero position the active view used
// this frame, or negative when that view was not placed.
void recordOffset(const RenderOffset& placed, bool anchored, bool rejected, float lag, float activeLag) {
    AcquireSRWLockExclusive(&offsetLock);
    offsets[placed.generation % offsets.size()] = placed;
    ReleaseSRWLockExclusive(&offsetLock);
    if (!anchored && !rejected)
        return;
    const float distance = length(placed.offset);
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyAppearanceData;
    InterlockedIncrement64(&data.sequence);
    if (anchored) {
        ++data.anchoredFrames;
        data.anchorLast = distance;
        data.anchorMax = std::max(data.anchorMax, distance);
        data.anchorSum += distance;
        if (lag >= 0) {
            ++data.sharedHeroFrames;
            data.heroLagLast = lag;
            data.heroLagMax = std::max(data.heroLagMax, lag);
            data.heroLagSum += lag;
        }
        if (activeLag > .001f) {
            ++data.activeLagFrames;
            data.activeLagLast = activeLag;
            data.activeLagMax = std::max(data.activeLagMax, activeLag);
        }
    }
    data.anchorRejected += rejected;
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
void setupDisplay(void* view, void* settings, void* profile, uint32_t flags) {
    originalSetupDisplay(view, settings, profile, flags);
    if (!enabled || !profile || !(config.createViews & 4))
        return;
    const auto address = reinterpret_cast<uintptr_t>(view);
    const auto owner = pointer(address + 0x1f40);
    int32_t viewIndex{};
    if (!read(address + 0xf90, &viewIndex, sizeof(viewIndex)) || viewIndex >= 0)
        return; // The game already queues this command for primary views.
    for (unsigned eye = 0; eye < 2; ++eye)
        if (auto ownedEye = eyes[eye].load();
            ownedEye && (view == ownedEye || owner == reinterpret_cast<uintptr_t>(ownedEye))) {
            // 19203a2 omits PrepareSrgbOverlay (0x88) for negative view IDs.
            // Its native handler is 18490d0. Restore the same setup bucket
            // immediately before GUI geometry (+15c0), after tone mapping.
            // This retains the HUD and avoids applying scene HDR gain to it.
            using Enqueue = void (*)(void*, uint32_t, uint64_t, void*);
            reinterpret_cast<Enqueue>(base + 0x187e130)(reinterpret_cast<void*>(address + 0x15b0), 0x88, 0,
                                                        nullptr);
            appearanceEvent(eye, false);
            // 19203c2 queues TemporalAa (0x8a) unless the profile's +68c is set.
            uint8_t left{};
            if ((eyeTemporal.load() & 4) && read(reinterpret_cast<uintptr_t>(profile) + 0x68c, &left, 1) && left)
                reinterpret_cast<Enqueue>(base + 0x187e130)(reinterpret_cast<void*>(address + 0x1630), 0x8a, 1,
                                                            nullptr);
            break;
        }
}
// The size an eye's scene is rendered at: the eye's own, or eyeRenderPercent of
// it per side, in multiples of 8 as the engine's own scaled sizes are.
std::array<uint32_t, 2> eyeRenderSize() {
    const uint32_t percent = std::clamp<uint32_t>(eyeRenderPercent.load(), 25, 100);
    return spidy::eyeRenderSize(config.width, config.height, (eyeTemporal.load() & 2) ? percent : 100);
}
void sizeBuffers(void* buffers, const char* name, uint32_t width, uint32_t height, uint32_t kind, uint8_t align) {
    originalSizeBuffers(buffers, name, width, height, kind, align);
    if (!sizingEye[0])
        return;
    // +12bf0: render width, height; the anti-aliased image's width, height;
    // those in multiples of 8; ... flags at +12c2c, 2 among them where the two
    // sizes are equal.
    const auto sizes = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(buffers) + 0x12bf0);
    sizes[2] = sizingEye[0];
    sizes[3] = sizingEye[1];
    sizes[4] = (sizingEye[0] + 7) & ~7u;
    sizes[5] = (sizingEye[1] + 7) & ~7u;
    if (sizes[0] == sizes[2] && sizes[1] == sizes[3])
        sizes[15] |= 2;
    else
        sizes[15] &= ~2u;
}
bool initBuffers(void* buffers, const void* description, const char* name) {
    const auto view = reinterpret_cast<uintptr_t>(buffers) - 0x1f80;
    if (enabled && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x186c325 &&
        (creatingEye || view == reinterpret_cast<uintptr_t>(eyes[0].load()) ||
         view == reinterpret_cast<uintptr_t>(eyes[1].load()))) {
        // The offscreen constructor clamps both dimensions to the desktop
        // buffers. Its parent then clamps them again in 1874f40. Each eye
        // needs private scene buffers at its own resolution.
        alignas(16) uint8_t local[32]{};
        if (read(reinterpret_cast<uintptr_t>(description), local, sizeof(local))) {
            const auto render = eyeRenderSize();
            std::memcpy(local, &render[0], 4);
            std::memcpy(local + 4, &render[1], 4);
            const uint64_t noParent{};
            std::memcpy(local + 0x18, &noParent, 8);
            // An offscreen view's buffer set (kind 2) has no targets for
            // temporal anti-aliasing or screen-space reflections: 1875270
            // makes a target with a kind only where the kind's mask (3d11b18)
            // has it. Bit 4: kTaaTarget0/1 and kTaaDepth0/1, made at the eye's
            // size (sizeBuffers). Bit 1: kSsrHistory, kSsrHistoryDepth,
            // kSsrFinalColor0/1, kSsrFinalConfidence0/1 and the four
            // kSharedHalfResFp16Target (kind 0x11); without the first two the
            // ScreenSpaceReflection command (0x43, 1734a80), queued for every
            // view, returns at once. The shared ones are needed as well: a set
            // made without them (October 11's third build) had the command
            // run and no reflection in the eye's image.
            const uint32_t temporal = eyeTemporal.load();
            const uint32_t kinds = ((temporal & 2) ? 4 : 0) | ((temporal & 8) ? 0 : 1);
            if (!kinds)
                return originalInitBuffers(buffers, local, name);
            if (temporal & 2) {
                sizingEye[0] = config.width;
                sizingEye[1] = config.height;
            }
            const auto mask = reinterpret_cast<uint32_t*>(base + 0x3d11b18 + 2 * 4);
            DWORD protection{};
            const bool writable = VirtualProtect(mask, 4, PAGE_READWRITE, &protection);
            const uint32_t stock = *mask;
            if (writable)
                *mask = stock | kinds;
            const bool made = originalInitBuffers(buffers, local, name);
            if (writable) {
                *mask = stock;
                VirtualProtect(mask, 4, protection, &protection);
            }
            sizingEye[0] = sizingEye[1] = 0;
            return made;
        }
    }
    return originalInitBuffers(buffers, description, name);
}
// The game adapts exposure on the GPU, one view at a time. A view's luminance
// pass (18449f0) averages the view's own image, moves the view's "Adapted Lum"
// buffer (+16f0: the buffer, +16f8 its shader view, +1700 its writable view)
// towards it, and binds the shader view for the view's later draws (t67); the
// tone mapper multiplies the image by that buffer (189c710, t11). Each eye had
// its own, so each eye was exposed for what it alone saw: the eye with more
// sky in its lens came out darker than the other (a quarter to a half with a
// level head, twice with the head on its side), and they swapped when the
// head turned round. A render job works on the frame's copy of its view
// (19223e0), so each eye's copy now carries the main view's shader view: both
// eyes take the exposure of the game's own view, which is on the head and
// covers both lenses while immersive. The eye still adapts its own buffer,
// which nothing reads.
bool shareExposure(uintptr_t job, unsigned eye) {
    uint64_t pool{};
    const auto primary = pointer(base + 0x7a34dd0);
    if (ownExposure || !(config.createViews & 4) || !read(base + 0x7a34dd8, &pool, 8) || !(pool & 1) ||
        !primary || !pointer(job + 0x16f0))
        return false;
    // A resting game view (restGameView) adapts nothing: the left eye keeps
    // its own exposure and the right eye takes the left's.
    auto source = primary;
    if (rested.load()) {
        source = reinterpret_cast<uintptr_t>(eyes[0].load());
        if (!eye || !source)
            return false;
    }
    // As 189c710 picks it: the view's own buffer, else its render buffers'.
    const auto buffers = pointer(source + 0x1640);
    const auto shared =
        pointer(source + 0x16f0) ? pointer(source + 0x16f8) : buffers ? pointer(buffers + 0x12ae8) : 0;
    if (!shared)
        return false;
    std::memcpy(reinterpret_cast<void*>(job + 0x16f8), &shared, 8);
    return true;
}
void copyJob(void* view, void* settings, void* a, void* b, bool c) {
    originalCopyJob(view, settings, a, b, c);
    if (!enabled)
        return;
    if (SpidyCommandData.tracing) {
        const auto job = pointer(reinterpret_cast<uintptr_t>(settings) + 0x4b0);
        if (reinterpret_cast<uintptr_t>(view) == pointer(base + 0x7a34dd0))
            tracedJobs[0] = job;
        for (unsigned i = 0; i < 2; ++i)
            if (view == eyes[i].load())
                tracedJobs[i + 1] = job;
    }
    for (unsigned i = 0; i < 2; ++i)
        if (view == eyes[i].load()) {
            const auto job = reinterpret_cast<void*>(pointer(reinterpret_cast<uintptr_t>(settings) + 0x4b0));
            if (!job)
                return;
            // The job's render context (0x7a0 bytes, copied to 7a2e200 while its
            // commands run): +40c is its anti-aliasing, 1 the engine's temporal
            // one, the only value at which TemporalAa (1842e10) runs. With an
            // upscaler chosen in the game's settings it is 0 for every view,
            // and the upscaler (1849e80) leaves offscreen views out: the eyes
            // then had no anti-aliasing at all.
            if (eyeTemporal.load() & 2) {
                const uint32_t temporal = 1;
                std::memcpy(static_cast<uint8_t*>(settings) + 0x40c, &temporal, 4);
            }
            if (shareExposure(reinterpret_cast<uintptr_t>(job), i)) {
                AcquireSRWLockExclusive(&telemetry);
                InterlockedIncrement64(&SpidyAppearanceData.sequence);
                ++SpidyAppearanceData.exposureShared;
                InterlockedIncrement64(&SpidyAppearanceData.sequence);
                ReleaseSRWLockExclusive(&telemetry);
            }
            AcquireSRWLockExclusive(&jobLock);
            auto& d = SpidyStereoFrames;
            InterlockedIncrement64(&d.sequence);
            // A copy the game dropped never ends. A fixed table that kept those
            // filled after about 10,000 frames, and every later eye frame went
            // unmarked until presentation stalled. Old copies are reclaimed.
            d.reclaimed += jobs.record({job, latchedSerial.load(), generation.load(), i});
            float rendered[3]{}, previous[3]{};
            if (read(reinterpret_cast<uintptr_t>(view) + 0x30, rendered, sizeof(rendered)) &&
                read(reinterpret_cast<uintptr_t>(view) + 0x560, previous, sizeof(previous)))
                ++(std::memcmp(rendered, previous, sizeof(rendered)) ? d.historyMoved : d.historyStill)[i];
            ++(placedFrame == frameIndex ? d.sameFramePoses : d.lateFramePoses)[i];
            uint64_t ropeSamples{};
            Vec3 ropeHero{}, ropeStarts[2]{};
            native_webs::heroSample(ropeSamples, ropeHero);
            if (i == 0 && (native_webs::ropeStarts(ropeSamples, ropeStarts) & 1)) {
                ++d.ropeFrames;
                d.ropeFromEye[0] = ropeStarts[0].x - rendered[0];
                d.ropeFromEye[1] = ropeStarts[0].y - rendered[1];
                d.ropeFromEye[2] = ropeStarts[0].z - rendered[2];
            }
            ++d.jobCopies[i];
            d.copiedSerial[i] = latchedSerial;
            d.copiedJob[i] = reinterpret_cast<uint64_t>(job);
            InterlockedIncrement64(&d.sequence);
            ReleaseSRWLockExclusive(&jobLock);
        }
}
void jobEvent(void* view, bool ended) {
    if (!enabled)
        return;
    AcquireSRWLockExclusive(&jobLock);
    if (const auto found = jobs.find(view)) {
        const auto job = *found;
        auto& d = SpidyStereoFrames;
        InterlockedIncrement64(&d.sequence);
        if (ended) {
            ++d.jobEnds[job.eye];
            d.endedSerial[job.eye] = job.serial;
            jobs.finish(view);
        } else {
            ++d.jobBegins[job.eye];
            d.begunSerial[job.eye] = job.serial;
        }
        InterlockedIncrement64(&d.sequence);
    }
    ReleaseSRWLockExclusive(&jobLock);
}
void markJob(void* view, bool ended) {
    AcquireSRWLockShared(&jobLock);
    if (const auto job = jobs.find(view))
        native_gpu::enterMarker(job->eye, job->serial, job->generation, ended);
    ReleaseSRWLockShared(&jobLock);
}
void beginJob(void* view) {
    jobEvent(view, false);
    markJob(view, false);
    originalBeginJob(view);
    native_gpu::leaveMarker();
}
void endJob(void* view) {
    markJob(view, true);
    originalEndJob(view);
    native_gpu::leaveMarker();
    jobEvent(view, true);
}
uint64_t tone(void* a, void* b, void* settings, void* view, void* e, uint64_t genericView) {
    // The native caller derives this switch from view +0xf90 being negative.
    // Our secondary views use the active scene profile and a display target;
    // keep the native display treatment used by the main camera as well.
    const auto owner = pointer(reinterpret_cast<uintptr_t>(view) + 0x1f40);
    if (enabled && (config.createViews & 4) && owner &&
        (owner == reinterpret_cast<uintptr_t>(eyes[0].load()) ||
         owner == reinterpret_cast<uintptr_t>(eyes[1].load())))
        genericView = 0;
    return originalTone(a, b, settings, view, e, genericView);
}
void update(void* view) {
    for (unsigned i = 0; i < 2; ++i)
        if (view == eyes[i].load())
            ++updates[i];
    originalUpdate(view);
    if (enabled && (view == eyes[0].load() || view == eyes[1].load()) && owned(view)) {
        // +1708 and +170c are the processor's copy of the view's adapted and
        // measured luminance: 18a01c0 reads the "Adapted Lum" buffer back into
        // +1720 and +1724, and 189bd30 (above) moves these two towards them.
        // The tone mapper's constants and the game's own code read them. The
        // eyes take the main view's, as their images do (shareExposure); what
        // each eye measured for itself stays in +1720 and +1724.
        // (With the game's view resting, the left eye's own, for the right eye.)
        auto source = pointer(base + 0x7a34dd0);
        if (rested.load())
            source = view == eyes[1].load() ? reinterpret_cast<uintptr_t>(eyes[0].load()) : 0;
        float exposure[2]{};
        if (read(source + 0x1708, exposure, sizeof(exposure)) && std::isfinite(exposure[0]) &&
            std::isfinite(exposure[1]) && exposure[0] > 0 && exposure[1] > 0 && exposure[0] < 1e6f &&
            exposure[1] < 1e6f)
            std::memcpy(static_cast<uint8_t*>(view) + 0x1708, exposure, sizeof(exposure));
    }
}
void copyFinal(void* view) {
    originalCopyFinal(view);
    if (!enabled || config.createViews != 7 || (view != eyes[0].load() && view != eyes[1].load()) ||
        !owned(view))
        return;
    const auto address = reinterpret_cast<uintptr_t>(view);
    uint32_t flags{}, bytes{};
    read(address + 0x14e7c, &flags, 4);
    read(address + 0x14e70, &bytes, 4);
    const auto texture = pointer(address + 0x1f70), buffer = pointer(address + 0x14e68);
    if ((flags & 7) || !(flags & 0x10) || !texture || !buffer || bytes != config.width * config.height * 4)
        return;
    // Display mode bypasses the stock offscreen copy/readback function. Schedule
    // its existing readback command after CopyToDisplay in the final bucket.
    // The native frame allocator keeps this payload alive for worker execution.
    struct Payload {
        uint64_t staging, source, pixels;
        uint32_t bytes, padding;
        uint64_t completed, reserved;
    };
    static_assert(sizeof(Payload) == 0x30);
    using Allocate = void* (*)(void*, uint32_t, const char*);
    using Enqueue = void (*)(void*, uint32_t, uint64_t, void*);
    auto payload = static_cast<Payload*>(reinterpret_cast<Allocate>(base + 0x1872860)(
        reinterpret_cast<void*>(base + 0x7938880), sizeof(Payload), "Spidy display readback"));
    if (!payload)
        return;
    *payload = {address + 0x14e40, texture + 0x40, buffer, bytes, 0, address + 0x14e74, 0};
    reinterpret_cast<Enqueue>(base + 0x187e130)(reinterpret_cast<void*>(address + 0x1630), 0xad, 0x41a,
                                                payload);
}
// Player position from the hero's render transform, as updateHero() reads it.
bool heroPosition(Vec3& position) {
    const auto record = playerRecord.load();
    const auto instance = record ? pointer(record) : 0;
    float m[16]{};
    if (!instance || !read(instance, m, sizeof(m)) || !unitTransform(m))
        return false;
    position = {m[12], m[13], m[14]};
    return true;
}
// The lens of the game's own view on the head: the head's lens covering both
// eyes, or, while RECORDING FOV has that view render for the window
// (restGameView), the wider one of the window's shape that holds it. The
// frame's first submit (activeView) and its last (placeEyes) both take it, so
// what the view culls for and what it renders agree.
native_eyes::Bounds viewLens(const native_eyes::Bounds& head) {
    const int degrees = recordingFov.load(std::memory_order_relaxed);
    return degrees && live.load() == 2 && !native_gpu::screenRequested()
               ? recording_fov::covering(head, degrees, flatAspect.load())
               : head;
}
// The engine's active view (ViewContextManager +f8) is its first pool view: the
// stock camera. Offscreen eyes are never active, and the "drawn in any view"
// (18a1890) and "sphere in any view" (18a12a0) queries visit pool views only.
// Shaders get the active view's position from the frame-wide GlobalWorldCBuffer
// (m_ActiveViewCtxPos, +330), which also carries the key-light shadow setup.
// All of that was done for the stock camera behind the hero, not the headset.
// While immersive, move the view to the tracked head with a lens covering both
// eyes. The monitor shows this view; the gameplay camera object is unchanged.
bool activeView(const Descriptor* incoming, Descriptor& out) {
    // Every submit of the frame takes the frame's head (latchHead), which also
    // keeps the frame's rope-update sample.
    const auto& head = latchHead();
    uint64_t mask{};
    Descriptor stock{};
    if (!read(base + 0x7a34dd8, &mask, 8) || !(mask & 1) ||
        !read(reinterpret_cast<uintptr_t>(incoming), &stock, sizeof(stock)) || !native_view::valid(stock))
        return false;
    // The flat screen keeps the stock camera's shape whichever view is drawn.
    flatAspect = (stock.values[0x404 / 4] - stock.values[0x400 / 4]) /
                 (stock.values[0x40c / 4] - stock.values[0x408 / 4]);
    // What that camera shows of the HUD panel sizes the panel in VR.
    stockHalfWidth = (stock.values[0x404 / 4] - stock.values[0x400 / 4]) / 2;
    if (!(config.createViews & 8) || stopRequested || GetTickCount64() >= deadline || live.load() != 2)
        return false;
    if (!head.controlled || head.command.enabled != 1)
        return false;
    const auto& pose = head.pose;
    const auto lens = viewLens(head.lens);
    bool placed = head.viewed;
    if (placed) {
        out = stock;
        // Keep the stock camera's far plane and temporal jitter. Its near
        // plane, 10 cm, suits a camera behind the hero; on the head it cut
        // off hands held up before the face (NEAR CLIP).
        reinterpret_cast<Lens>(base + 0x187bb00)(&out, near_clip::metres(), stock.farZ(), lens.left,
                                                 lens.right, lens.top, lens.bottom, 0, 0,
                                                 stock.values[0x428 / 4], stock.values[0x42c / 4], false,
                                                 false);
        reinterpret_cast<SetPose>(base + 0x187ca10)(&out, pose.data());
        placed = native_view::valid(out);
    }
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyAppearanceData;
    InterlockedIncrement64(&data.sequence);
    if (placed) {
        ++data.activeAligned;
        data.activeBounds[0] = lens.left;
        data.activeBounds[1] = lens.right;
        data.activeBounds[2] = lens.top;
        data.activeBounds[3] = lens.bottom;
        data.activeShift = length(
            Vec3{pose[12] - stock.values[12], pose[13] - stock.values[13], pose[14] - stock.values[14]});
    } else {
        ++data.activeRejected;
    }
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    return placed;
}
// 1646e10 submits the stock camera's descriptor here (return 1647044). Replacing
// it at the source, not in maintain(), gives every later reader the head view;
// the frame dispatch reads the active view for occlusion and 1722b10 before it
// runs view maintenance.
void submit(void* view, const Descriptor* incoming) {
    if (enabled && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x1647044 &&
        reinterpret_cast<uintptr_t>(view) == pointer(base + 0x7a34dd0)) {
        Descriptor aligned{};
        const bool replaced = activeView(incoming, aligned);
        activeAligned = replaced;
        if (replaced) {
            originalSubmit(view, &aligned);
            return;
        }
    }
    originalSubmit(view, incoming);
}
// Stop, expiry, or a lapsed untimed lease: the eyes leave the render list.
bool retiring() {
    return stopRequested || GetTickCount64() >= deadline ||
           (!config.durationMs && GetTickCount64() >= commandDeadline);
}
// This frame's lens jitter for both eyes, as the game's camera gives its own
// view (descriptor +428, +42c): a Halton (2, 3) point within the pixel, twice
// the offset over the render size. Temporal anti-aliasing gathers the samples;
// a scene rendered smaller than the eye needs more of them per eye pixel.
std::array<float, 2> eyeJitter() {
    if (!(eyeTemporal.load() & 1))
        return {0, 0};
    const auto render = eyeRenderSize();
    const float ratio = static_cast<float>(config.width) / render[0];
    const uint32_t phases = std::clamp<uint32_t>(static_cast<uint32_t>(std::ceil(8 * ratio * ratio)), 8, 32);
    const auto halton = [](uint32_t index, uint32_t radix) {
        float value = 0, fraction = 1.f / radix;
        for (; index; index /= radix, fraction /= radix)
            value += fraction * (index % radix);
        return value;
    };
    const uint32_t phase = static_cast<uint32_t>(frameIndex % phases) + 1;
    return {2 * (halton(phase, 2) - .5f) / render[0], 2 * (halton(phase, 3) - .5f) / render[1]};
}
// While immersive the game renders three scene views: the eyes and its own,
// which Spidy moved to the head (activeView) for the work the game does for
// its active view alone. That work needs the view's camera, not its image:
// the image only went to the monitor, and cost as much as an eye (5.1 of a
// 19.6 ms frame on the GPU, and three of every ten render commands). 19206a0
// (SubmitScene) leaves at once for a view whose +1208 is clear, as the engine
// itself has it for a view that is not ready; the frame then has no job for
// the view, and everything queued on the view itself still runs (the HUD's
// Scaleform command, effects' readback, streaming video). The view's camera
// stays where activeView put it. The game still presents its window every
// frame, so the GPU bridge draws the left eye there (native_gpu::mirror).
// `rested` (above) is the view whose +1208 this module cleared.
bool writeByte(uintptr_t p, uint8_t value) {
    __try {
        *reinterpret_cast<volatile uint8_t*>(p) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool writeFloat(uintptr_t p, float value) {
    __try {
        *reinterpret_cast<volatile float*>(p) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool writeQword(uintptr_t p, uint64_t value) {
    __try {
        *reinterpret_cast<volatile uint64_t*>(p) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Clears a view's list of drawn instances, as its scene submit does (1920e54).
bool clearDrawn(uintptr_t view) {
    __try {
        const auto drawn = *reinterpret_cast<uint8_t* volatile*>(view + 0x1b80);
        const auto count = *reinterpret_cast<volatile uint32_t*>(view + 0x1b88);
        if (!drawn || !count || count >= (1u << 26))
            return false;
        std::memset(drawn, 0, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// An eye's level-of-detail multiplier for the lens it was just given
// (distances); the engine multiplies it into the eye's scale in the eye's
// scene submit (189e4b0 sets +1cc4 there, 1796d20 reads both).
void matchDistances(void* eye, const Descriptor& lens, unsigned index) {
    if (!distances)
        return;
    const float detail = native_view::eyeDetail(lens);
    if (!writeFloat(reinterpret_cast<uintptr_t>(eye) + 0x1cc8, detail) || index)
        return;
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyAppearanceData;
    InterlockedIncrement64(&data.sequence);
    data.eyeScale = native_view::distanceScale(lens);
    data.eyeDetail = detail;
    ++data.detailFrames;
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
// A view's impostor visibility queries (impostorReach and the cone above).
// 191ecd0 copies the cull before its job reads it, so the cone's may live here.
void* impostorQueries(void* view, void* cull) {
    const bool standing = impostors && enabled && rested.load(std::memory_order_relaxed) &&
                          view == eyes[0].load(std::memory_order_relaxed);
    const bool coned = standing && cones && impostorConePass == offscreenPass;
    alignas(16) uint8_t cone[0x100];
    if (coned) {
        reinterpret_cast<void (*)(const Descriptor*, void*)>(base + 0x187de80)(&impostorCone, cone);
        cull = cone;
    }
    if (standing) {
        AcquireSRWLockExclusive(&telemetry);
        InterlockedIncrement64(&SpidyStreamData.sequence);
        ++(coned ? SpidyStreamData.coneFrames : SpidyStreamData.ownFrames);
        InterlockedIncrement64(&SpidyStreamData.sequence);
        ReleaseSRWLockExclusive(&telemetry);
    }
    const auto result = originalImpostorQueries(view, cull);
    float reach{};
    if (result && impostors && enabled && rested.load(std::memory_order_relaxed) &&
        eyeView(reinterpret_cast<uintptr_t>(view)) >= 0 &&
        read(reinterpret_cast<uintptr_t>(result) + 0x88, &reach, 4) && std::isfinite(reach) && reach > 0)
        writeFloat(reinterpret_cast<uintptr_t>(result) + 0x88, reach * impostorReach.load() / 100.f);
    return result;
}
void restGameView(uintptr_t primary, bool rest) {
    if (const auto was = rested.load(); was && (was != primary || !rest)) {
        // A load replaces the pool's views: only the same view is given back.
        if (was == primary)
            writeByte(was + 0x1208, 1);
        rested = 0;
    }
    uint8_t renders{};
    if (rest && primary && !rested.load() && read(primary + 0x1208, &renders, 1) && renders == 1 &&
        writeByte(primary + 0x1208, 0))
        rested = primary;
    native_gpu::mirror(rested.load() != 0);
}
// The scene's slot pool (native_stream::poolMargin): its size and the slots in
// use (7a42f80 +1c8, +1cc), into SpidyStreamData. False where it does not read.
bool slotPool(int32_t& capacity, int32_t& used) {
    int32_t pool[2]{};
    if (!read(base + 0x7a43148, pool, sizeof(pool)))
        return false;
    capacity = pool[0];
    used = pool[1];
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyStreamData;
    InterlockedIncrement64(&data.sequence);
    data.poolCapacity = capacity;
    data.poolUsed = used;
    data.poolMost = std::max(data.poolMost, used);
    if (capacity > 0 && used >= capacity * .85f)
        ++data.poolFullFrames;
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    return true;
}
// This frame's margin for the cone, degrees: SpidyBuildingCone's as the slot
// pool allows it, eased by at most a degree a frame, so buildings at the
// cone's edge do not come and go with each frame's count.
float coneMargin(int32_t capacity, int32_t used) {
    const float wanted = native_stream::poolMargin(used, capacity, static_cast<float>(coneDegrees.load()));
    easedMargin += std::clamp(wanted - easedMargin, -1.f, 1.f);
    return easedMargin;
}
// The texture streamer's budget while the eyes render: the game's, raised as
// far as the card has room for it (native_stream::textureBudget), looked at
// every two seconds. The game's own comes back if the card runs short (for the
// session) or SpidyTextureBudget(0) asks for it. A Texture Quality change in
// the game's menu writes the game's new budget (1891174), raised in its turn.
void keepTextureBudget() {
    const auto now = GetTickCount64();
    if (now - textureChecked < 2000)
        return;
    textureChecked = now;
    uint64_t current{}, videoBudget{}, videoUsage{};
    if (!read(base + 0x7a793c0, &current, sizeof(current)) || !current ||
        !native_gpu::videoMemory(videoBudget, videoUsage))
        return;
    if (const auto raised = textureRaised.load(); raised && current != raised)
        textureRaised = 0;
    if (const auto raised = textureRaised.load()) {
        const bool shortened = native_stream::textureShort(videoBudget, videoUsage);
        if ((shortened || !textureRaise.load()) && writeQword(base + 0x7a793c0, textureGame.load())) {
            textureRaised = 0;
            current = textureGame.load();
            textureShortened = textureShortened || shortened;
            if (shortened) {
                AcquireSRWLockExclusive(&telemetry);
                InterlockedIncrement64(&SpidyStreamData.sequence);
                ++SpidyStreamData.textureDrops;
                InterlockedIncrement64(&SpidyStreamData.sequence);
                ReleaseSRWLockExclusive(&telemetry);
            }
        }
    } else {
        textureGame = current;
        if (const auto budget = native_stream::textureBudget(current, videoBudget, videoUsage);
            !textureShortened && textureRaise.load() && budget > current &&
            writeQword(base + 0x7a793c0, budget)) {
            textureRaised = budget;
            current = budget;
        }
    }
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyStreamData;
    InterlockedIncrement64(&data.sequence);
    data.textureGame = static_cast<uint32_t>(textureGame.load() >> 20);
    data.textureBudget = static_cast<uint32_t>(current >> 20);
    data.videoBudget = static_cast<uint32_t>(videoBudget >> 20);
    data.videoUsage = static_cast<uint32_t>(videoUsage >> 20);
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
// The game's own budget back as the eyes stop (SpidyStop), unless the game
// has set another since.
void restoreTextureBudget() {
    uint64_t current{};
    if (const auto raised = textureRaised.exchange(0);
        raised && read(base + 0x7a793c0, &current, sizeof(current)) && current == raised)
        writeQword(base + 0x7a793c0, textureGame.load());
}
// Diagnostic: 187e130 queues a render command (rdx) on a view's list (rcx).
using EnqueueCommand = void (*)(void*, uint32_t, uint64_t, void*);
EnqueueCommand originalEnqueue{};
void* enqueueHook{};
void enqueueCommand(void* list, uint32_t command, uint64_t order, void* payload) {
    if (SpidyCommandData.tracing && command < 256) {
        const auto at = reinterpret_cast<uintptr_t>(list);
        const uintptr_t views[3] = {pointer(base + 0x7a34dd0), reinterpret_cast<uintptr_t>(eyes[0].load()),
                                    reinterpret_cast<uintptr_t>(eyes[1].load())};
        unsigned owner = 0;
        for (unsigned i = 0; i < 3 && !owner; ++i) {
            const auto job = tracedJobs[i].load(std::memory_order_relaxed);
            if ((views[i] && at >= views[i] && at < views[i] + 0x1f70) || (job && at >= job && at < job + 0x1f70))
                owner = i + 1;
        }
        InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&SpidyCommandData.counts[owner][command]));
    }
    originalEnqueue(list, command, order, payload);
}
// Places both eyes for the frame whose render jobs are about to be set up.
// The frame function 175a060 shifts every view's camera history (18a13c0 ->
// 189ee00: previous = current), runs gameplay, sets up the offscreen views'
// jobs (1920240 -> 186d050 -> 19206a0, which copies each view in 19223e0),
// then the game views', and only then runs view maintenance (187e320 ->
// 18a0bb0). Until October 5 the eyes were placed in maintenance, so a pose was
// rendered one frame later. Each eye then trailed its frame's scene by one
// step of player travel, which drew the game's web lines that far ahead of
// the tracked hands (0.7 m at 32 m/s), and the next frame's history shift made
// each eye's previous camera equal to its current one, so motion vectors and
// temporal anti-aliasing saw no camera motion.
void placeEyes() {
    Descriptor main{};
    const auto primary = pointer(base + 0x7a34dd0);
    const bool render = read(primary, &main, sizeof(main)) && native_view::valid(main) && !retiring();
    // This frame's hero rope update, if one ran since the previous frame.
    uint64_t ropeSamples{};
    Vec3 ropeHero{};
    native_webs::heroSample(ropeSamples, ropeHero);
    const bool shared = placedHero.fresh(ropeSamples, ropeHero);
    const bool activePlaced = activeHeroPlaced;
    activeHeroPlaced = false;
    native_eyes::Command desired{};
    bool controlled{};
    if (render && frameHead.frame == frameIndex) {
        // The active view and the HUD went to this command's head (latchHead).
        desired = frameHead.command;
        controlled = frameHead.controlled;
    } else if (render) {
        AcquireSRWLockShared(&commandLock);
        desired = command;
        controlled = desired.enabled && GetTickCount64() < commandDeadline;
        ReleaseSRWLockShared(&commandLock);
    }
    latchedImmersive = render && enabled && controlled && desired.enabled == 1;
    // The game's view is on the head and both eyes render: it need not, unless
    // RECORDING FOV wants it in the window.
    restGameView(primary, latchedImmersive && (config.createViews & 32) && live.load() == 2 &&
                              !native_gpu::screenRequested() &&
                              !recordingFov.load(std::memory_order_relaxed));
    // The eyes' jobs of this frame mark into the resting view's list (see rested).
    if (const auto resting = rested.load(); resting && clearDrawn(resting))
        InterlockedIncrement64(&SpidyDrawnFrames);
    // While both eyes render: the texture budget, and the slot pool the cone's
    // margin is eased by.
    int32_t poolCapacity{}, poolUsed{};
    const bool pooled = render && enabled && live.load() == 2 && cones && slotPool(poolCapacity, poolUsed);
    if (render && enabled && live.load() == 2 && budgets)
        keepTextureBudget();
    // The HUD's second movie goes on its panel while the eyes are shown.
    // On the game screen in stereo only in a frame whose panel went onto
    // that screen (placeScreenHud, earlier in the frame): without the panel
    // (a pause menu: the game places none) the movie stays in the game's own
    // frame, which the headset then shows.
    if (render && enabled && controlled && (config.createViews & 4) &&
        (desired.enabled != 3 || screenHudFrame == frameIndex))
        native_hud::eyesShown(desired.enabled == 3);
    latchedHide = latchedImmersive && !native_body::drawn();
    if ((config.createViews & 4) && (latchedImmersive || hiddenHero.load()))
        updateHero(latchedImmersive, latchedHide);
    if (!render)
        return;
    latchedSerial = controlled ? desired.serial : 0;
    localActor = playerRecord.load() ? pointer(playerRecord.load()) : 0;
    if (!activeAligned) // otherwise main carries the head lens; submit() kept the stock shape
        flatAspect = (main.values[0x404 / 4] - main.values[0x400 / 4]) /
                     (main.values[0x40c / 4] - main.values[0x408 / 4]);
    RenderOffset placed{};
    placed.generation = ++generation;
    placedFrame = frameIndex;
    AcquireSRWLockExclusive(&jobLock);
    InterlockedIncrement64(&SpidyStereoFrames.sequence);
    SpidyStereoFrames.latched = latchedSerial;
    InterlockedIncrement64(&SpidyStereoFrames.sequence);
    ReleaseSRWLockExclusive(&jobLock);
    // Tracking sampled the player before this frame's gameplay moved it. Move
    // both eyes with the player to the frame being rendered, so the camera,
    // world, and body come from one simulation state. Use the position the
    // game's web lines started from this frame; they then leave the wrists.
    bool anchored{}, rejected{};
    float lag = -1, activeLag = -1;
    if (latchedImmersive && desired.anchored) {
        const auto& hero = heroFrames[heroSlot.load(std::memory_order_acquire) % heroFrames.size()];
        const Vec3 transform{hero.transform[12], hero.transform[13], hero.transform[14]};
        const Vec3 rendered = shared ? ropeHero : transform;
        anchored = hero.instance && native_eyes::reanchorOffset(desired.anchor, rendered, placed.offset);
        rejected = !anchored;
        if (shared) {
            lag = length(transform - ropeHero);
            placed.ropes = native_webs::ropeStarts(ropeSamples, placed.ropeStarts);
        }
        if (activePlaced)
            activeLag = length(activeHeroPosition - rendered);
    }
    recordOffset(placed, anchored, rejected, lag, activeLag);
    if (latchedImmersive && frameHead.frame == frameIndex && frameHead.viewed) {
        // The HUD and the eyes should have moved with the player alike.
        const float mismatch = length(placed.offset - frameHead.offset);
        native_hud::publish([&](native_hud::Data& d) { d.offsetMismatch = std::max(d.offsetMismatch, mismatch); });
    }
    // The early submit sets up culling and lighting. Gameplay can still move
    // the hero after it: use the eyes' final render-frame placement for the
    // monitor too, before either view's render job is copied. Otherwise the
    // desktop can look into the back while the eyes are already at the head.
    // Do not replace the deliberately selected stock monitor or flat screen.
    shadowLensSame = controlled && desired.enabled >= 2;
    if (latchedImmersive) {
        Mat4 pose{};
        native_eyes::Bounds lens{};
        if (native_eyes::headView(desired, pose, lens)) {
            pose[12] += placed.offset.x;
            pose[13] += placed.offset.y;
            pose[14] += placed.offset.z;
            const auto onHead = [&](const native_eyes::Bounds& b) {
                auto view = main;
                reinterpret_cast<Lens>(base + 0x187bb00)(
                    &view, near_clip::metres(), main.farZ(), b.left, b.right, b.top, b.bottom, 0, 0,
                    main.values[0x428 / 4], main.values[0x42c / 4], false, false);
                reinterpret_cast<SetPose>(base + 0x187ca10)(&view, pose.data());
                return view;
            };
            const auto aligned = onHead(lens);
            if (native_view::valid(aligned)) {
                // Covers both eyes: what the first eye fits its shadow cascades to.
                shadowLens = aligned;
                shadowLensPass = offscreenPass;
                // The game's view takes RECORDING FOV's wider lens (viewLens), as
                // at the frame's first submit. The eyes' shadows keep this one:
                // rendering, the view fits cascades of its own to its own lens.
                const auto viewed =
                    recordingFov.load(std::memory_order_relaxed) ? onHead(viewLens(lens)) : aligned;
                if ((config.createViews & 8) && native_view::valid(viewed)) {
                    originalSubmit(reinterpret_cast<void*>(primary), &viewed);
                    activeAligned = true;
                }
            }
            // The far buildings' cone for the left eye's query this pass
            // (impostorCone): the lens over both eyes, widened by this frame's
            // margin.
            if (pooled && coneDegrees.load(std::memory_order_relaxed) && rested.load()) {
                constexpr float radians = .0174532925f;
                const float margin = coneMargin(poolCapacity, poolUsed);
                const auto wide =
                    native_stream::widened(lens, margin * radians, native_stream::coneMost * radians);
                if (const auto cone = onHead(wide); native_view::valid(cone)) {
                    impostorCone = cone;
                    impostorConePass = offscreenPass;
                    AcquireSRWLockExclusive(&telemetry);
                    auto& data = SpidyStreamData;
                    InterlockedIncrement64(&data.sequence);
                    data.margin = margin;
                    data.across = std::atan(std::max(-wide.left, wide.right)) / radians;
                    data.upDown = std::atan(std::max(-wide.top, wide.bottom)) / radians;
                    InterlockedIncrement64(&data.sequence);
                    ReleaseSRWLockExclusive(&telemetry);
                }
            }
        }
    }
    for (auto& slot : eyes)
        // A load can replace the native view pool between frames. maintain()
        // forgets an eye that left it; until then, never write to one.
        if (auto eye = slot.load(); eye && owned(eye)) {
            auto descriptor = main;
            const unsigned index = eye == eyes[0].load() ? 0 : 1;
            if (controlled && desired.enabled >= 2) {
                // The game screen in stereo: the game's camera set apart
                // sideways, each lens shifted the other way (stereoEyes).
                const bool stereo = desired.enabled == 3;
                // The stock camera's jitter is in its own render size's pixels.
                if (stereo || (eyeTemporal.load() & 1)) {
                    const auto jitter = (eyeTemporal.load() & 1)
                                            ? eyeJitter()
                                            : std::array<float, 2>{main.values[0x428 / 4], main.values[0x42c / 4]};
                    auto pose = main.pose();
                    float shift{};
                    if (stereo) {
                        const auto apart = native_eyes::stereoEyes(
                            native_eyes::stereo(desired), {main.values[0x400 / 4], main.values[0x404 / 4],
                                                           main.values[0x408 / 4], main.values[0x40c / 4]});
                        const float side = index == 0 ? -apart.separation / 2 : apart.separation / 2;
                        for (int i = 0; i < 3; ++i)
                            pose[12 + i] += pose[i] * side;
                        shift = index == 0 ? apart.shift : -apart.shift;
                    }
                    reinterpret_cast<Lens>(base + 0x187bb00)(
                        &descriptor, main.nearZ(), main.farZ(), main.values[0x400 / 4] + shift,
                        main.values[0x404 / 4] + shift, main.values[0x408 / 4], main.values[0x40c / 4], 0, 0,
                        jitter[0], jitter[1], false, false);
                    reinterpret_cast<SetPose>(base + 0x187ca10)(&descriptor, pose.data());
                }
                reinterpret_cast<Submit>(base + 0x1899ab0)(eye, &descriptor);
                matchDistances(eye, descriptor, index);
                continue;
            }
            auto pose = controlled
                            ? desired.eyes[index].world
                            : native_view::relativePose(main.pose(), {{index == 0 ? -.032f : .032f, 0, 0}, {}});
            pose[12] += placed.offset.x;
            pose[13] += placed.offset.y;
            pose[14] += placed.offset.z;
            // A square diagnostic image needs a square 90-degree frustum.
            // The main camera has an ultrawide desktop projection. Rebuild all
            // derived matrices through the verified native lens constructor.
            const float aspect = static_cast<float>(config.width) / config.height;
            const auto& fov = desired.eyes[index].fov;
            const auto jitter = eyeJitter();
            const float nearZ = controlled && desired.enabled == 1 ? near_clip::metres() : main.nearZ();
            reinterpret_cast<Lens>(base + 0x187bb00)(
                &descriptor, nearZ, main.farZ(), controlled ? std::tan(fov[0]) : -aspect,
                controlled ? std::tan(fov[1]) : aspect, controlled ? -std::tan(fov[3]) : -1,
                controlled ? -std::tan(fov[2]) : 1, 0, 0, jitter[0], jitter[1], false, false);
            reinterpret_cast<SetPose>(base + 0x187ca10)(&descriptor, pose.data());
            reinterpret_cast<Submit>(base + 0x1899ab0)(eye, &descriptor);
            matchDistances(eye, descriptor, index);
        }
    // RECORDING FOV's own lens for the window (pull request #46): submitted
    // last, after both eyes took their poses, just the angle across and turned
    // as RECORDING SMOOTHING says. The view still culls with the head's lens
    // covering both eyes (viewLens, at the frame's first submit), so the
    // headset sees all it did; a view smoothed far behind a fast turn can show
    // the window less than it should at its edge.
    const int degrees = recordingFov.load(std::memory_order_relaxed);
    if (!degrees || !(config.createViews & 8) || !latchedImmersive || live.load() != 2 ||
        native_gpu::screenRequested()) {
        recordingSmoothing.reset();
        recordingTick = 0;
    } else {
        Mat4 pose{};
        native_eyes::Bounds headLens{};
        if (native_eyes::headView(desired, pose, headLens)) {
            pose[12] += placed.offset.x;
            pose[13] += placed.offset.y;
            pose[14] += placed.offset.z;
            const int level = recordingSmoothLevel.load(std::memory_order_relaxed);
            if (level > 0) {
                static const double frequency = [] {
                    LARGE_INTEGER f{};
                    QueryPerformanceFrequency(&f);
                    return static_cast<double>(f.QuadPart);
                }();
                LARGE_INTEGER tick{};
                QueryPerformanceCounter(&tick);
                const auto now = static_cast<uint64_t>(tick.QuadPart);
                const float seconds =
                    recordingTick ? static_cast<float>((now - recordingTick) / frequency) : 0;
                recordingTick = now;
                pose = recordingSmoothing.update(pose, seconds, true, level * 5);
            } else {
                recordingSmoothing.reset();
                recordingTick = 0;
            }
            float aspect = flatAspect.load();
            if (!std::isfinite(aspect) || aspect < .2f || aspect > 6)
                aspect = 16.f / 9;
            const float halfWidth = std::tan(static_cast<float>(degrees) * .00872664626f);
            const float halfHeight = halfWidth / aspect;
            auto desktop = main;
            reinterpret_cast<Lens>(base + 0x187bb00)(
                &desktop, near_clip::metres(), main.farZ(), -halfWidth, halfWidth, -halfHeight, halfHeight, 0,
                0, main.values[0x428 / 4], main.values[0x42c / 4], false, false);
            reinterpret_cast<SetPose>(base + 0x187ca10)(&desktop, pose.data());
            if (native_view::valid(desktop))
                originalSubmit(reinterpret_cast<void*>(primary), &desktop);
        }
    }
}
// A KeyLightContext's cascades: +d and +f count them, +150 holds one each
// 0x3e0 bytes, with its box's half sizes at +170 and its transform at +198.
unsigned cascades(uintptr_t context, uint8_t counts[2]) {
    return context && read(context + 0xd, &counts[0], 1) && read(context + 0xf, &counts[1], 1)
               ? std::min<unsigned>(counts[0] + counts[1], 15)
               : 0;
}
void fitCascades(void* context, void* view, void* third, void* fourth, void* fifth) {
    if (!view || view != shadowFitting) {
        originalFitCascades(context, view, third, fourth, fifth);
        return;
    }
    Descriptor own;
    std::memcpy(&own, view, sizeof(own));
    std::memcpy(view, &shadowLens, sizeof(shadowLens));
    originalFitCascades(context, view, third, fourth, fifth);
    std::memcpy(view, &own, sizeof(own));
    ++SpidyShadowData.covered;
}
void* submitScene(void* view, void* parent, uint32_t flags, uint8_t last) {
    const int eye = !view ? -1 : view == eyes[0].load() ? 0 : view == eyes[1].load() ? 1 : -1;
    if (eye < 0)
        return originalSubmitScene(view, parent, flags, last);
    const auto mode = eyeShadows.load();
    const bool second = shadowDonor.pass == offscreenPass && shadowDonor.view != view;
    const bool cover = (mode & 1) && (flags & 2) && shadowDonor.pass != offscreenPass && !shadowLensSame &&
                       shadowLensPass == offscreenPass;
    uintptr_t given[11]{};
    const bool take = (mode & 7) && (flags & 2) && second && shadowDonor.part[5] && shadowDonor.covers &&
                      (!parent || read(reinterpret_cast<uintptr_t>(parent), given, sizeof(given)));
    if (take) {
        if (mode & 1)
            given[5] = shadowDonor.part[5];
        if (mode & 2)
            given[3] = shadowDonor.part[3];
        if (mode & 4)
            given[6] = shadowDonor.part[6];
    }
    shadowFitting = cover ? view : nullptr;
    const auto result = originalSubmitScene(view, take ? given : parent, flags, last);
    shadowFitting = nullptr;
    uintptr_t made[11]{};
    const bool have = result && read(reinterpret_cast<uintptr_t>(result), made, sizeof(made));
    auto& data = SpidyShadowData;
    auto& sample = data.eyes[eye];
    InterlockedIncrement64(&data.sequence);
    ++sample.submits;
    sample.taken += take;
    sample.flags = flags;
    sample.thread = GetCurrentThreadId();
    sample.order = second;
    read(reinterpret_cast<uintptr_t>(view) + 0x120a, &sample.mayTake, 1);
    uint16_t lights{}, slot{};
    sample.lights = have && made[3] && read(made[3] + 0x34, &lights, 2) ? lights : 0;
    const auto count = have ? cascades(made[5], sample.cascades) : 0;
    const auto map = count ? pointer(made[5] + 0x150 + 0x3c8) : 0;
    sample.slot = map && read(map + 0x10, &slot, 2) ? slot & 0x7fff : 0;
    for (unsigned i = 0; i < 4 && i < count; ++i) {
        const auto entry = made[5] + 0x150 + i * 0x3e0;
        read(entry + 0x170, sample.extent[i], sizeof(sample.extent[i]));
        read(entry + 0x198, sample.pose[i], sizeof(sample.pose[i]));
    }
    if (have && !take && second && shadowDonor.part[5] && !shadowDonor.covers) {
        // Both eyes made their own: how far apart are their cascades?
        uint8_t counts[2]{};
        const auto both = std::min(count, cascades(shadowDonor.part[5], counts));
        float poseDelta{}, extentDelta{};
        for (unsigned i = 0; i < both; ++i) {
            float mine[19]{}, other[19]{};
            const auto a = made[5] + 0x150 + i * 0x3e0, b = shadowDonor.part[5] + 0x150 + i * 0x3e0;
            if (!read(a + 0x198, mine, 64) || !read(b + 0x198, other, 64) || !read(a + 0x170, mine + 16, 12) ||
                !read(b + 0x170, other + 16, 12))
                continue;
            for (unsigned k = 0; k < 19; ++k)
                (k < 16 ? poseDelta : extentDelta) =
                    std::max(k < 16 ? poseDelta : extentDelta, std::fabs(mine[k] - other[k]));
        }
        ++data.compared;
        data.poseDelta = poseDelta;
        data.extentDelta = extentDelta;
        data.poseDeltaMost = std::max(data.poseDeltaMost, poseDelta);
        data.extentDeltaMost = std::max(data.extentDeltaMost, extentDelta);
    }
    InterlockedIncrement64(&data.sequence);
    if (have && !take && !second && (flags & 2)) {
        shadowDonor.pass = offscreenPass;
        shadowDonor.view = view;
        shadowDonor.covers = cover;
        std::memcpy(shadowDonor.part, made, sizeof(made));
    }
    return result;
}
// 1920240 sets up this frame's render jobs for the offscreen views.
uint8_t renderOffscreen() {
    ++offscreenPass;
    ++SpidyShadowData.passes;
    native_body::renderFrame();
    if (!lateEyes && (enabled || live.load() || hiddenHero.load()))
        placeEyes();
    return originalRenderOffscreen();
}
void maintain(void* manager) {
    const bool ours = reinterpret_cast<uintptr_t>(manager) == base + 0x7a34dd0;
    Descriptor main{};
    auto primary = pointer(base + 0x7a34dd0);
    const bool valid = ours && read(primary, &main, sizeof(main)) && native_view::valid(main);
    AcquireSRWLockExclusive(&creation);
    // A load can replace the native view pool. Check ownership before any write.
    if (ours)
        for (unsigned i = 0; i < 2; ++i)
            if (auto eye = eyes[i].load(); eye && !owned(eye)) {
                eyes[i] = nullptr;
                if (!retired[i])
                    --live;
                retired[i] = false;
                stopRequested = true;
                AcquireSRWLockExclusive(&telemetry);
                InterlockedIncrement64(&SpidyStereoData.sequence);
                SpidyStereoData.error = 2003;
                InterlockedIncrement64(&SpidyStereoData.sequence);
                ReleaseSRWLockExclusive(&telemetry);
            }
    if (ours && enabled && !stopRequested && (config.createViews & 1) &&
        (!attempted || retired[0] || retired[1]) && valid && GetTickCount64() < deadline &&
        (config.durationMs || GetTickCount64() < commandDeadline)) {
        attempted = true;
        static const char* names[] = {"Spidy left eye test", "Spidy right eye test"};
        for (unsigned i = 0; i < 2; ++i) {
            if (!eyes[i]) {
                creatingEye = true;
                eyes[i] = reinterpret_cast<Create>(base + 0x18a09c0)(manager, names[i], main.nearZ(),
                                                                     main.farZ(), 90.f, config.width,
                                                                     config.height, 28, nullptr);
                creatingEye = false;
                // Render jobs copy views from the next frame on (1920240).
                if (eyes[i] && (config.createViews & 16))
                    giveOcclusion(eyes[i].load());
                AcquireSRWLockExclusive(&telemetry);
                InterlockedIncrement64(&SpidyStereoData.sequence);
                if (eyes[i]) {
                    ++live;
                    ++SpidyStereoData.created;
                    if (config.createViews & 4)
                        // 0x80 selects the active postprocessing profile in
                        // 183e9a0. Secondary views otherwise receive defaults,
                        // including a different exposure mode. 0x400 sends the
                        // display conversion into our output target at +0xfa0.
                        InterlockedOr(reinterpret_cast<volatile LONG*>(
                                          reinterpret_cast<uintptr_t>(eyes[i].load()) + 0x14e84),
                                      0x480);
                    // Use the engine's own asynchronous GPU readback path.
                    // This is enabled once, before deferred view initialization.
                    if (config.createViews & 2)
                        reinterpret_cast<Maintain>(base + 0x186c670)(eyes[i].load());
                } else {
                    SpidyStereoData.error = 2001 + i;
                    stopRequested = true;
                }
                InterlockedIncrement64(&SpidyStereoData.sequence);
                ReleaseSRWLockExclusive(&telemetry);
            } else if (retired[i]) {
                InterlockedAnd(
                    reinterpret_cast<volatile LONG*>(reinterpret_cast<uintptr_t>(eyes[i].load()) + 0x14e7c),
                    ~1L);
                retired[i] = false;
                ++live;
            }
        }
    }
    ReleaseSRWLockExclusive(&creation);
    if (ours && retiring())
        for (unsigned i = 0; i < 2; ++i)
            if (auto eye = eyes[i].load(); eye && !retired[i]) {
                // Exclude from new render jobs but retain storage for already queued
                // work. The game's normal shutdown owns final resource destruction.
                // Immediate native deletion caused a worker to use a null resource.
                InterlockedOr(reinterpret_cast<volatile LONG*>(reinterpret_cast<uintptr_t>(eye) + 0x14e7c),
                              1);
                retired[i] = true;
                --live;
                AcquireSRWLockExclusive(&telemetry);
                InterlockedIncrement64(&SpidyStereoData.sequence);
                ++SpidyStereoData.retired;
                InterlockedIncrement64(&SpidyStereoData.sequence);
                ReleaseSRWLockExclusive(&telemetry);
            }
    // The engine initializes queued resources here. Eye poses are placed in
    // placeEyes(), before the next frame's render jobs copy the views.
    if (ours && lateEyes)
        placeEyes();
    originalMaintain(manager);
    if (!ours)
        return;
    ++frameIndex;
    if (SpidyCommandData.tracing)
        ++SpidyCommandData.frames;
    AcquireSRWLockExclusive(&telemetry);
    auto& d = SpidyStereoData;
    InterlockedIncrement64(&d.sequence);
    ++d.frames;
    d.primary = primary;
    d.thread = GetCurrentThreadId();
    mainThread = d.thread;
    for (unsigned i = 0; i < 2; ++i) {
        if (!eyes[i])
            continue;
        bool present = false;
        for (unsigned slot = 0; slot < 8; ++slot)
            present |= pointer(base + 0x7a34e00 + slot * 8) == reinterpret_cast<uintptr_t>(eyes[i].load());
        if (!present) {
            eyes[i] = nullptr;
            if (!retired[i])
                --live;
            retired[i] = false;
            d.error = 2003;
            continue;
        }
        auto& e = d.eyes[i];
        e.view = reinterpret_cast<uintptr_t>(eyes[i].load());
        e.texture = pointer(e.view + 0x1f70);
        e.textureObject = pointer(e.texture + 0x40);
        // Register before the render worker records this frame. GPU state is
        // learned from actual submitted transitions, never an assumed UAV state.
        if (e.textureObject && pointer(e.textureObject) == base + 0x4e56c80)
            native_gpu::registerEye(
                i, reinterpret_cast<ID3D12Resource*>(pointer(pointer(e.textureObject + 0x38))));
        e.updates = updates[i].load();
        read(e.view + 0x14e7c, &e.flags, 4);
        e.occlusion = pointer(e.view + 0x1ba0) != 0;
        read(e.view + 0x14e28, &e.width, 4);
        read(e.view + 0x14e2c, &e.height, 4);
        read(e.view + 0x1f80 + 0x12bf0, &e.renderWidth, 4);
        read(e.view + 0x1f80 + 0x12bf4, &e.renderHeight, 4);
        read(e.view + 0x460, &e.viewportWidth, 4);
        read(e.view + 0x464, &e.viewportHeight, 4);
        read(e.view, e.pose, sizeof(e.pose));
        e.readback = pointer(e.view + 0x14e68);
        read(e.view + 0x14e70, &e.readbackBytes, 4);
        read(e.view + 0x14e74, &e.readbackCompleted, 4);
    }
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidySetEyes(void* input) {
    native_eyes::Command incoming{};
    if (!read(reinterpret_cast<uintptr_t>(input), &incoming, sizeof(incoming)) ||
        !native_eyes::valid(incoming))
        return 4001;
    if (!enabled || stopRequested || GetTickCount64() >= deadline)
        return 4003;
    AcquireSRWLockExclusive(&commandLock);
    if (incoming.serial <= command.serial) {
        ReleaseSRWLockExclusive(&commandLock);
        return 4002;
    }
    command = incoming;
    commandDeadline = GetTickCount64() + incoming.leaseMs;
    ReleaseSRWLockExclusive(&commandLock);
    AcquireSRWLockExclusive(&jobLock);
    InterlockedIncrement64(&SpidyStereoFrames.sequence);
    SpidyStereoFrames.accepted = incoming.serial;
    InterlockedIncrement64(&SpidyStereoFrames.sequence);
    ReleaseSRWLockExclusive(&jobLock);
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* value) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        if (enabled)
            break;
        base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        Config incoming{};
        if (!read(reinterpret_cast<uintptr_t>(value), &incoming, sizeof(incoming))) {
            result = 1001;
            break;
        }
        if ((eyes[0] || eyes[1]) && (incoming.width != config.width || incoming.height != config.height ||
                                     incoming.createViews != config.createViews)) {
            result = 1003;
            break;
        }
        config = incoming;
        // createViews bits: 1 eye views, 2 native readback, 4 display/VR path,
        // 8 (VR path only) move the engine's active view to the head while immersive,
        // 16 (VR path only) occlusion culling for each eye (giveOcclusion),
        // 32 (with 8) the game's own view is not rendered while immersive
        // (restGameView): the left eye goes to the game's window instead,
        // 64 the eyes keep the distances their own lenses give (no
        // matchDistances or setupModels; builds before October 19),
        // 128 no eye stands in for the resting game view's impostors (no
        // isActiveView; builds before October 19's second).
        const auto views = config.createViews & ~(16u | 32u | 64u | 128u);
        if (!GetModuleHandleW(L"Spider-Man.exe") || config.magic != 0x53534346 || config.version != 1 ||
            config.bytes != sizeof(config) || config.pid != GetCurrentProcessId() || config.base != base ||
            config.durationMs > 30000 || (config.durationMs && config.durationMs < 500) ||
            (views != 0 && views != 1 && views != 3 && views != 5 && views != 7 && views != 13) ||
            ((config.createViews & 16) && !(views & 4)) || ((config.createViews & 32) && !(views & 8)) ||
            !validEyeSize(config.width) ||
            !validEyeSize(config.height)) {
            result = 1001;
            break;
        }
        const unsigned char maintainBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10};
        const unsigned char createBytes[] = {0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x5b, 0x10};
        const unsigned char updateBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x7c, 0x24, 0x10};
        const unsigned char readbackBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10};
        const unsigned char copyFinalBytes[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0xf6,
                                                0x81, 0x7c, 0x4e, 0x01, 0x00, 0x20};
        const unsigned char toneBytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10};
        const unsigned char lensBytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x55};
        const unsigned char copyJobBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57};
        const unsigned char beginJobBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30};
        const unsigned char initBuffersBytes[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x44, 0x8b, 0x4a, 0x04};
        // A buffer set's sizes; the test for which targets its kind gets, and
        // the kinds' masks it reads (3d11b18: an offscreen view's is 0x20);
        // TemporalAa up to its test of the render context's anti-aliasing.
        const unsigned char sizeBuffersBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
                                                  0x10, 0x48, 0x89, 0x7c, 0x24, 0x18, 0x55, 0x41, 0x54};
        const unsigned char targetKindBytes[] = {0x44, 0x0f, 0xb6, 0xd2, 0x41, 0xb1, 0x01, 0x0f, 0xba, 0xe2, 0x0e};
        const unsigned char kindMaskBytes[] = {0xff, 0, 0, 0, 0xff, 0, 0, 0, 0x20, 0, 0, 0, 0x28, 0, 0, 0};
        const unsigned char temporalBytes[] = {0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0xa8, 0xf8, 0xfe, 0xff, 0xff};
        const unsigned char renderActorBytes[] = {0x48, 0x8b, 0xc4, 0x44, 0x88, 0x40, 0x18, 0x48,
                                                  0x89, 0x50, 0x10, 0x48, 0x89, 0x48, 0x08};
        // A view's model job setup; there, its reads of the view's +1cc8 x
        // +1cc4 and of its rendered size and half tangents into +2fc and +300;
        // the lens scale's read of +41c and store to +1cc4 (189e4b0) with its
        // two numerators and 16:9.
        const unsigned char setupModelsBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
                                                  0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x40,
                                                  0x48, 0x8b, 0x02, 0x48, 0x8b, 0xf9, 0x48, 0x89, 0x01};
        const unsigned char modelScaleBytes[] = {0xf3, 0x41, 0x0f, 0x10, 0x8a, 0xc8, 0x1c, 0x00, 0x00,
                                                 0xf3, 0x41, 0x0f, 0x59, 0x8a, 0xc4, 0x1c, 0x00, 0x00};
        const unsigned char modelRequestBytes[] = {0xf3, 0x41, 0x0f, 0x10, 0x8a, 0x4c, 0x04, 0x00, 0x00,
                                                   0xf3, 0x41, 0x0f, 0x5e, 0x8a, 0x24, 0x04, 0x00, 0x00,
                                                   0xf3, 0x41, 0x0f, 0x10, 0x82, 0x48, 0x04, 0x00, 0x00,
                                                   0xf3, 0x41, 0x0f, 0x5e, 0x82, 0x20, 0x04, 0x00, 0x00};
        const unsigned char modelRequestStoreBytes[] = {0xf3, 0x0f, 0x11, 0x8f, 0xfc, 0x02, 0x00, 0x00,
                                                        0xf3, 0x0f, 0x11, 0x97, 0x00, 0x03, 0x00, 0x00};
        const unsigned char lensAspectBytes[] = {0xf3, 0x0f, 0x10, 0x81, 0x1c, 0x04, 0x00, 0x00};
        const unsigned char lensScaleBytes[] = {0xf3, 0x0f, 0x11, 0xb3, 0xc4, 0x1c, 0x00, 0x00};
        const unsigned char numeratorBytes[] = {0x29, 0x42, 0xc2, 0x3e, 0x97, 0xac, 0x2c, 0x3f};
        const unsigned char widescreenBytes[] = {0x39, 0x8e, 0xe3, 0x3f};
        // "Is this the active view" (view == ViewContextManager +f8), and its
        // five calls in the impostor code (the impostor submit job, both
        // ImpostorHQModelRegJob launchers).
        const unsigned char isActiveViewBytes[] = {0x48, 0x3b, 0x0d, 0x21, 0x74, 0x19, 0x06, 0x0f, 0x94, 0xc0, 0xc3};
        const struct {
            uintptr_t rva;
            unsigned char bytes[5];
        } impostorCalls[] = {{0x16f8bb0, {0xe8, 0xeb, 0x4e, 0x1a, 0x00}}, {0x16fb696, {0xe8, 0x05, 0x24, 0x1a, 0x00}},
                             {0x16fb6a6, {0xe8, 0xf5, 0x23, 0x1a, 0x00}}, {0x16fb7aa, {0xe8, 0xf1, 0x22, 0x1a, 0x00}},
                             {0x16fb7ba, {0xe8, 0xe1, 0x22, 0x1a, 0x00}}};
        // The impostor visibility queries; their result's distance, set to 145 m
        // first and to the lens's last, and the result returned.
        const unsigned char impostorQueriesBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
                                                      0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57};
        const unsigned char reachFirstBytes[] = {0xc7, 0x80, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x43};
        const unsigned char reachLastBytes[] = {0xf3, 0x0f, 0x11, 0xbb, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xc3};
        // The cone: the cull builder up to its read of a lens's flags, the
        // slot pool's room check, and the full models' and the hibernate
        // zones' loads of the scene registry (7a42f80) they check it on.
        const unsigned char cullBytes[] = {0x48, 0x83, 0xec, 0x68, 0xf6, 0x81, 0x38,
                                           0x04, 0x00, 0x00, 0x02, 0x4c, 0x8b, 0xca};
        const unsigned char poolBytes[] = {0x03, 0x91, 0xcc, 0x01, 0x00, 0x00, 0x3b, 0x91,
                                           0xc8, 0x01, 0x00, 0x00, 0x0f, 0x9e, 0xc0, 0xc3};
        const unsigned char registryBytes[] = {0x48, 0x8d, 0x0d, 0x86, 0xaf, 0x34, 0x06};
        const unsigned char hibernateRegistryBytes[] = {0x48, 0x8d, 0x0d, 0xe3, 0xff, 0x01, 0x06};
        // The texture budget: the streamer's room (the budget at 7a793c0, less
        // what it holds and loads and the reserve at 7a793c8), the Texture
        // Quality's store of the budget, and the heap's 16 MB reserve.
        const unsigned char roomBytes[] = {0x48, 0x8b, 0x05, 0x29, 0x66, 0x0c, 0x06, 0x48, 0x2b, 0x81,
                                           0x50, 0x01, 0x00, 0x00, 0x48, 0x2b, 0x81, 0x28, 0x01, 0x00,
                                           0x00, 0x48, 0x2b, 0x05, 0x1c, 0x66, 0x0c, 0x06, 0xc3};
        const unsigned char budgetStoreBytes[] = {0x48, 0x89, 0x05, 0xc5, 0x81, 0x1e, 0x06};
        const unsigned char reserveBytes[] = {0x48, 0xc7, 0x43, 0x08, 0x00, 0x00, 0x00, 0x01};
        const unsigned char setupDisplayBytes[] = {0x4d, 0x85, 0xc0, 0x0f, 0x84, 0x7f, 0x03, 0, 0};
        const unsigned char enqueueBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10};
        // ActorDrawAction's DrawOff/DrawOn: toggle flag 0x20 at +5c and mark
        // the instance for the renderer. Called only from maintain().
        const unsigned char drawOffBytes[] = {0x8b, 0x41, 0x5c, 0xa8, 0x20, 0x75, 0x29,
                                              0x83, 0xc8, 0x20, 0x89, 0x41, 0x5c};
        const unsigned char drawOnBytes[] = {0x8b, 0x41, 0x5c, 0xa8, 0x20, 0x74, 0x29,
                                             0x83, 0xe0, 0xdf, 0x89, 0x41, 0x5c};
        // View submit, the stock camera's call to it in 1646e10, and the pose setter.
        const unsigned char submitBytes[] = {0x48, 0x3b, 0xd1, 0x74, 0x7c, 0x48, 0x8b, 0xc1};
        const unsigned char cameraSubmitBytes[] = {0xe8, 0x6c, 0x2a, 0x25, 0x00};
        const unsigned char poseBytes[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x60, 0x0f, 0x10, 0x02};
        // Offscreen render setup, its only call (in the frame function 175a060,
        // after the history shift 18a13c0 and before view maintenance), and the
        // history shift's per-view copy 189ee00 that placeEyes() relies on.
        const unsigned char renderOffscreenBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
                                                      0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x40, 0xb7, 0x01};
        const unsigned char frameOffscreenBytes[] = {0xe8, 0x92, 0x57, 0x1c, 0x00};
        const unsigned char submitSceneBytes[] = {0x44, 0x88, 0x4c, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x48,
                                                  0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54};
        const unsigned char fitCascadesBytes[] = {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x48, 0x20, 0x4c, 0x89, 0x40, 0x18, 0x48,
                                                  0x89, 0x50, 0x10, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x56, 0x57};
        const unsigned char historyBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20,
                                              0x48, 0x8b, 0xf9, 0x41, 0xb8, 0x30, 0x05, 0x00, 0x00};
        // A view's occlusion builder, up to its test of +1ba0.
        const unsigned char occlusionBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
                                                0x18, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xf9,
                                                0x48, 0x83, 0xb9, 0xa0, 0x1b, 0x00, 0x00, 0x00};
        // The HUD follower's placement and the render instance's transform setter.
        const unsigned char hudPlaceBytes[] = {0x40, 0x55, 0x53, 0x48, 0x8d, 0xac, 0x24, 0xc8, 0xfe,
                                               0xff, 0xff, 0x48, 0x81, 0xec, 0x38, 0x02, 0x00, 0x00};
        const unsigned char setTransformBytes[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x40, 0x0f, 0x10, 0x02, 0x33, 0xc0};
        // A view's adapted luminance for shaders: its own buffer at +16f0.
        const unsigned char adaptedLumBytes[] = {0x48, 0x8d, 0x81, 0xf0, 0x16, 0x00, 0x00, 0x48, 0x8b, 0x10};
        if (!entry(0x18a0bb0, maintainBytes, sizeof(maintainBytes)) ||
            !entry(0x189c710, adaptedLumBytes, sizeof(adaptedLumBytes)) ||
            !entry(0x1899ab0, submitBytes, sizeof(submitBytes)) ||
            !entry(0x164703f, cameraSubmitBytes, sizeof(cameraSubmitBytes)) ||
            !entry(0x187ca10, poseBytes, sizeof(poseBytes)) ||
            !entry(0x1920240, renderOffscreenBytes, sizeof(renderOffscreenBytes)) ||
            !entry(0x19206a0, submitSceneBytes, sizeof(submitSceneBytes)) ||
            !entry(0x170fbc0, fitCascadesBytes, sizeof(fitCascadesBytes)) ||
            !entry(0x175aaa9, frameOffscreenBytes, sizeof(frameOffscreenBytes)) ||
            !entry(0x189ee00, historyBytes, sizeof(historyBytes)) ||
            !entry(0x18a09c0, createBytes, sizeof(createBytes)) ||
            !entry(0x189bd30, updateBytes, sizeof(updateBytes)) ||
            ((config.createViews & 2) && !entry(0x186c670, readbackBytes, sizeof(readbackBytes))) ||
            ((config.createViews & 16) && !entry(0x189fed0, occlusionBytes, sizeof(occlusionBytes))) ||
            !entry(0x186cc00, copyFinalBytes, sizeof(copyFinalBytes)) ||
            !entry(0x1846c20, toneBytes, sizeof(toneBytes)) ||
            !entry(0x187bb00, lensBytes, sizeof(lensBytes)) ||
            !entry(0x19223e0, copyJobBytes, sizeof(copyJobBytes)) ||
            !entry(0x189e310, beginJobBytes, sizeof(beginJobBytes)) ||
            !entry(0x189e3a0, beginJobBytes, sizeof(beginJobBytes)) ||
            !entry(0x1873470, initBuffersBytes, sizeof(initBuffersBytes)) ||
            !entry(0x1874f40, sizeBuffersBytes, sizeof(sizeBuffersBytes)) ||
            !entry(0x1875270, targetKindBytes, sizeof(targetKindBytes)) ||
            !entry(0x3d11b18, kindMaskBytes, sizeof(kindMaskBytes)) ||
            !entry(0x1842e10, temporalBytes, sizeof(temporalBytes)) ||
            !entry(0x17991a0, renderActorBytes, sizeof(renderActorBytes)) ||
            !entry(0x1796d20, setupModelsBytes, sizeof(setupModelsBytes)) ||
            !entry(0x189daa0, isActiveViewBytes, sizeof(isActiveViewBytes)) ||
            !entry(0x16faf10, impostorQueriesBytes, sizeof(impostorQueriesBytes)) ||
            !entry(0x1920310, setupDisplayBytes, sizeof(setupDisplayBytes)) ||
            !entry(0x187e130, enqueueBytes, sizeof(enqueueBytes)) ||
            !entry(0x191afb0, drawOffBytes, sizeof(drawOffBytes)) ||
            !entry(0x191b880, drawOnBytes, sizeof(drawOnBytes)) ||
            !entry(0x73ab80, hudPlaceBytes, sizeof(hudPlaceBytes)) ||
            !entry(0x191c0e0, setTransformBytes, sizeof(setTransformBytes))) {
            result = 1002;
            break;
        }
        // The eyes' distances need the job's and the lens scale's layout as
        // read here; another game build keeps the eye lenses' own.
        distances = !(config.createViews & 64) && (config.createViews & 4) &&
                    entry(0x1796e5d, modelScaleBytes, sizeof(modelScaleBytes)) &&
                    entry(0x1796f7f, modelRequestBytes, 18) && entry(0x1796f91, modelRequestBytes + 18, 18) &&
                    entry(0x1796fbd, modelRequestStoreBytes, sizeof(modelRequestStoreBytes)) &&
                    entry(0x189e4c7, lensAspectBytes, sizeof(lensAspectBytes)) &&
                    entry(0x189e558, lensScaleBytes, sizeof(lensScaleBytes)) &&
                    entry(0x3d1d738, numeratorBytes, sizeof(numeratorBytes)) &&
                    entry(0x387a7d8, widescreenBytes, sizeof(widescreenBytes));
        impostors = !(config.createViews & 128) && (config.createViews & 32);
        for (const auto& call : impostorCalls)
            impostors = impostors && entry(call.rva, call.bytes, sizeof(call.bytes));
        impostors = impostors && entry(0x16faf60, reachFirstBytes, sizeof(reachFirstBytes)) &&
                    entry(0x16fb113, reachLastBytes, sizeof(reachLastBytes));
        cones = impostors && entry(0x187de80, cullBytes, sizeof(cullBytes)) &&
                entry(0x190cb80, poolBytes, sizeof(poolBytes)) &&
                entry(0x16f7ff3, registryBytes, sizeof(registryBytes)) &&
                entry(0x1a22f96, hibernateRegistryBytes, sizeof(hibernateRegistryBytes));
        budgets = (config.createViews & 4) && entry(0x19b2d90, roomBytes, sizeof(roomBytes)) &&
                  entry(0x18911f4, budgetStoreBytes, sizeof(budgetStoreBytes)) &&
                  entry(0x19b2ed3, reserveBytes, sizeof(reserveBytes));
        SpidyStreamData.features = (cones ? 1u : 0u) | (budgets ? 2u : 0u);
        if (!hooked) {
            auto s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
                result = 1100 + s;
                break;
            }
            maintainHook = reinterpret_cast<void*>(base + 0x18a0bb0);
            updateHook = reinterpret_cast<void*>(base + 0x189bd30);
            copyFinalHook = reinterpret_cast<void*>(base + 0x186cc00);
            toneHook = reinterpret_cast<void*>(base + 0x1846c20);
            copyJobHook = reinterpret_cast<void*>(base + 0x19223e0);
            beginJobHook = reinterpret_cast<void*>(base + 0x189e310);
            endJobHook = reinterpret_cast<void*>(base + 0x189e3a0);
            initBuffersHook = reinterpret_cast<void*>(base + 0x1873470);
            sizeBuffersHook = reinterpret_cast<void*>(base + 0x1874f40);
            renderActorHook = reinterpret_cast<void*>(base + 0x17991a0);
            setupModelsHook = reinterpret_cast<void*>(base + 0x1796d20);
            isActiveViewHook = reinterpret_cast<void*>(base + 0x189daa0);
            impostorQueriesHook = reinterpret_cast<void*>(base + 0x16faf10);
            setupDisplayHook = reinterpret_cast<void*>(base + 0x1920310);
            submitHook = reinterpret_cast<void*>(base + 0x1899ab0);
            renderOffscreenHook = reinterpret_cast<void*>(base + 0x1920240);
            hudPlaceHook = reinterpret_cast<void*>(base + 0x73ab80);
            submitSceneHook = reinterpret_cast<void*>(base + 0x19206a0);
            fitCascadesHook = reinterpret_cast<void*>(base + 0x170fbc0);
            s = MH_CreateHook(maintainHook, reinterpret_cast<void*>(maintain),
                              reinterpret_cast<void**>(&originalMaintain));
            if (s == MH_OK)
                s = MH_CreateHook(updateHook, reinterpret_cast<void*>(update),
                                  reinterpret_cast<void**>(&originalUpdate));
            if (s == MH_OK)
                s = MH_CreateHook(copyFinalHook, reinterpret_cast<void*>(copyFinal),
                                  reinterpret_cast<void**>(&originalCopyFinal));
            if (s == MH_OK)
                s = MH_CreateHook(toneHook, reinterpret_cast<void*>(tone),
                                  reinterpret_cast<void**>(&originalTone));
            if (s == MH_OK)
                s = MH_CreateHook(copyJobHook, reinterpret_cast<void*>(copyJob),
                                  reinterpret_cast<void**>(&originalCopyJob));
            if (s == MH_OK)
                s = MH_CreateHook(beginJobHook, reinterpret_cast<void*>(beginJob),
                                  reinterpret_cast<void**>(&originalBeginJob));
            if (s == MH_OK)
                s = MH_CreateHook(endJobHook, reinterpret_cast<void*>(endJob),
                                  reinterpret_cast<void**>(&originalEndJob));
            if (s == MH_OK)
                s = MH_CreateHook(initBuffersHook, reinterpret_cast<void*>(initBuffers),
                                  reinterpret_cast<void**>(&originalInitBuffers));
            if (s == MH_OK)
                s = MH_CreateHook(sizeBuffersHook, reinterpret_cast<void*>(sizeBuffers),
                                  reinterpret_cast<void**>(&originalSizeBuffers));
            if (s == MH_OK)
                s = MH_CreateHook(renderActorHook, reinterpret_cast<void*>(renderActor),
                                  reinterpret_cast<void**>(&originalRenderActor));
            if (s == MH_OK)
                s = MH_CreateHook(setupModelsHook, reinterpret_cast<void*>(setupModels),
                                  reinterpret_cast<void**>(&originalSetupModels));
            if (s == MH_OK)
                s = MH_CreateHook(isActiveViewHook, reinterpret_cast<void*>(isActiveView),
                                  reinterpret_cast<void**>(&originalIsActiveView));
            if (s == MH_OK)
                s = MH_CreateHook(impostorQueriesHook, reinterpret_cast<void*>(impostorQueries),
                                  reinterpret_cast<void**>(&originalImpostorQueries));
            if (s == MH_OK)
                s = MH_CreateHook(setupDisplayHook, reinterpret_cast<void*>(setupDisplay),
                                  reinterpret_cast<void**>(&originalSetupDisplay));
            if (s == MH_OK)
                s = MH_CreateHook(submitHook, reinterpret_cast<void*>(submit),
                                  reinterpret_cast<void**>(&originalSubmit));
            if (s == MH_OK)
                s = MH_CreateHook(renderOffscreenHook, reinterpret_cast<void*>(renderOffscreen),
                                  reinterpret_cast<void**>(&originalRenderOffscreen));
            if (s == MH_OK)
                s = MH_CreateHook(hudPlaceHook, reinterpret_cast<void*>(hudPlace),
                                  reinterpret_cast<void**>(&originalHudPlace));
            if (s == MH_OK)
                s = MH_CreateHook(submitSceneHook, reinterpret_cast<void*>(submitScene),
                                  reinterpret_cast<void**>(&originalSubmitScene));
            if (s == MH_OK)
                s = MH_CreateHook(fitCascadesHook, reinterpret_cast<void*>(fitCascades),
                                  reinterpret_cast<void**>(&originalFitCascades));
            if (s != MH_OK) {
                result = 1200 + s;
                for (auto hook : {maintainHook, updateHook, copyFinalHook, toneHook, copyJobHook, beginJobHook,
                                  endJobHook, initBuffersHook, sizeBuffersHook, renderActorHook, setupModelsHook,
                                  isActiveViewHook, impostorQueriesHook, setupDisplayHook, submitHook,
                                  renderOffscreenHook, hudPlaceHook, submitSceneHook, fitCascadesHook})
                    MH_RemoveHook(hook);
                break;
            }
            hooked = true;
        }
        attempted = false;
        stopRequested = false;
        deadline = config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
        // All under one freeze of the game's threads: MinHook lists every
        // thread on the PC for each freeze, and on a PC with some 10,000 of
        // them each took 300 ms, so 19 freezes one by one stalled the eyes
        // for 8 s (LOST_INVAD3, October 23).
        const std::initializer_list<void*> eyeHooks{
            updateHook, copyFinalHook, toneHook, copyJobHook, beginJobHook, endJobHook, initBuffersHook,
            sizeBuffersHook, renderActorHook, setupModelsHook, isActiveViewHook, impostorQueriesHook,
            setupDisplayHook, submitHook, renderOffscreenHook, hudPlaceHook, submitSceneHook, fitCascadesHook,
            maintainHook};
        auto s = MH_OK;
        for (auto hook : eyeHooks)
            if (s == MH_OK)
                s = MH_QueueEnableHook(hook);
        if (s == MH_OK)
            s = MH_ApplyQueued();
        if (s != MH_OK) {
            for (auto hook : eyeHooks)
                MH_QueueDisableHook(hook);
            MH_ApplyQueued();
            result = 1300 + s;
            break;
        }
        enabled = true;
        // The rest of the HUD on the panel (native_hud.cpp). Without it the
        // panel still goes in front of the head; SpidyHudData says why not.
        if (config.createViews & 4)
            native_hud::start(base);
    } while (false);
    AcquireSRWLockExclusive(&telemetry);
    InterlockedIncrement64(&SpidyStereoData.sequence);
    SpidyStereoData.state = result ? 3 : 1;
    SpidyStereoData.error = result;
    InterlockedIncrement64(&SpidyStereoData.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    stopRequested = true;
    DWORD result{};
    AcquireSRWLockExclusive(&commandLock);
    commandDeadline = 0;
    ReleaseSRWLockExclusive(&commandLock);
    AcquireSRWLockExclusive(&creation);
    enabled = false;
    ReleaseSRWLockExclusive(&creation);
    hiddenHud = 0;
    // Keep maintenance installed until both owned views are excluded from jobs
    // and the main thread has restored the hero's native visibility.
    const auto limit = GetTickCount64() + 5000;
    while ((live || hiddenHero.load()) && GetTickCount64() < limit)
        Sleep(10);
    // The game's own view renders again (placeEyes no longer runs), and the
    // texture streamer has the game's own budget.
    restGameView(pointer(base + 0x7a34dd0), false);
    restoreTextureBudget();
    // The HUD's texture goes back to the game's size while frames still run
    // (SpidyHudData's restored says whether it did).
    native_hud::stop(1000);
    if (live)
        result = 1401;
    else if (hooked) {
        enabled = false;
        for (auto hook : {maintainHook, updateHook, copyFinalHook, toneHook, copyJobHook, beginJobHook,
                          endJobHook, initBuffersHook, sizeBuffersHook, renderActorHook, setupModelsHook,
                          isActiveViewHook, impostorQueriesHook, setupDisplayHook, submitHook, renderOffscreenHook,
                          hudPlaceHook, submitSceneHook, fitCascadesHook}) {
            auto s = MH_DisableHook(hook);
            if (s != MH_OK && s != MH_ERROR_DISABLED)
                result = 1500 + s;
        }
    }
    // Maintenance normally restores the avatar. If the game stopped running
    // frames (for example during a load), restore the same live instance here.
    if (const auto previous = hiddenHero.load()) {
        Sleep(50); // let an in-flight maintenance call leave the detour
        uint32_t handle{}, flags{};
        if (registered(previous, handle) && handle == hiddenHandle && read(previous + 0x5c, &flags, 4) &&
            (flags & 0x20)) {
            reinterpret_cast<Visibility>(base + 0x191b880)(reinterpret_cast<void*>(previous));
            AcquireSRWLockExclusive(&telemetry);
            InterlockedIncrement64(&SpidyAppearanceData.sequence);
            ++SpidyAppearanceData.nativeRestores;
            InterlockedIncrement64(&SpidyAppearanceData.sequence);
            ReleaseSRWLockExclusive(&telemetry);
        }
        hiddenHero = 0;
        hiddenHandle = 0;
    }
    AcquireSRWLockExclusive(&telemetry);
    InterlockedIncrement64(&SpidyStereoData.sequence);
    SpidyStereoData.state = result ? 3 : 2;
    SpidyStereoData.error = result;
    InterlockedIncrement64(&SpidyStereoData.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
// Diagnostic: 1 places the eyes in view maintenance, as before October 5; 0
// restores placement before the frame's render jobs. tools/probe_eye_frames.py
// compares the two in the running game.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyEyePlacement(void* late) {
    lateEyes = late != nullptr;
    return 0;
}
// Diagnostic: 1 exposes each eye from its own adapted luminance, as before
// October 10's sixth build; 0 shares the main view's again.
// tools/probe_exposure.py compares the two in the running game.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyEyeExposure(void* own) {
    ownExposure = own != nullptr;
    return 0;
}
// Diagnostic: 1 counts the render commands queued from now on (SpidyCommandData,
// cleared first); 0 stops. The hook stays in place once made.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyCommandTrace(void* on) {
    if (!base)
        return 4201;
    if (on && !enqueueHook) {
        enqueueHook = reinterpret_cast<void*>(base + 0x187e130);
        auto s = MH_CreateHook(enqueueHook, reinterpret_cast<void*>(enqueueCommand),
                               reinterpret_cast<void**>(&originalEnqueue));
        if (s == MH_OK)
            s = MH_EnableHook(enqueueHook);
        if (s != MH_OK) {
            enqueueHook = nullptr;
            return 4200 + s;
        }
    }
    SpidyCommandData.tracing = 0;
    if (on) {
        std::memset(SpidyCommandData.counts, 0, sizeof(SpidyCommandData.counts));
        SpidyCommandData.frames = 0;
        SpidyCommandData.tracing = 1;
    }
    return 0;
}
// The eyes' render size and temporal anti-aliasing: bits 0-7 the percent of
// the eye's size per side its scene is rendered at (25-100; taken when the
// eyes are made, 0 leaves it), bits 8-15 eyeTemporal's bits. The VR worker
// sets them before it starts the views, as tools/probe_vr_load.py does.
// The eyes' shadow sets (eyeShadows above): 0 each eye makes its own, as
// before October 11; bits 1, 2 and 4 give the eye submitted second the first
// one's key-light cascades, light set and +30 part. Takes effect at the next
// frame. SpidyShadowData has what each eye made or took.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyEyeShadows(void* mode) {
    const auto value = reinterpret_cast<uintptr_t>(mode);
    if (value > 7)
        return 4301;
    eyeShadows = static_cast<uint32_t>(value);
    SpidyShadowData.mode = static_cast<uint32_t>(value);
    return 0;
}
// How far away the far buildings draw their full models while an eye stands in
// for the resting game view (impostorReach above): percent of the game's own
// distance, 50-400; 150 unless set. Takes effect at the next frame.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyImpostorReach(void* percent) {
    const auto value = reinterpret_cast<uintptr_t>(percent);
    if (value < 50 || value > 400)
        return 4401;
    impostorReach = static_cast<uint32_t>(value);
    return 0;
}
// The far buildings' cone (native_stream.hpp): its margin each way, degrees,
// 0-60; 0 keeps the left eye's own lens, as before October 27; 30 unless set.
// Takes effect at the next frame.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyBuildingCone(void* degrees) {
    const auto value = reinterpret_cast<uintptr_t>(degrees);
    if (value > 60)
        return 4501;
    coneDegrees = static_cast<uint32_t>(value);
    return 0;
}
// The texture budget while the eyes render (keepTextureBudget): 1 raised where
// the card has room for it (unless set), 0 the game's own, back within two
// seconds.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyTextureBudget(void* raise) {
    const auto value = reinterpret_cast<uintptr_t>(raise);
    if (value > 1)
        return 4601;
    textureRaise = value != 0;
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyEyeTemporal(void* packed) {
    const auto value = reinterpret_cast<uintptr_t>(packed);
    const uint32_t percent = value & 0xff;
    if (percent && (percent < 25 || percent > 100))
        return 4101;
    if (percent)
        eyeRenderPercent = percent;
    eyeTemporal = (value >> 8) & 0xff;
    return 0;
}
extern "C" float SpidyFlatAspect() {
    return flatAspect.load();
}
uint32_t spidy::native_hud::eyes(Head& out) {
    if (!enabled || !(config.createViews & 4) || lateEyes || GetCurrentThreadId() != mainThread.load())
        return 0;
    // The game projects its markers before it moves the player (earlyHead).
    const auto& head = earlyHead();
    if (!head.controlled)
        return 0;
    if (head.command.enabled >= 2)
        return 2;
    if (!head.viewed)
        return 0;
    out.pose = head.pose;
    out.panel = head.panel;
    out.halfWidth = halfWidth(size());
    return 1;
}
void spidy::native_appearance::setPlayerRecord(uint64_t record) {
    playerRecord = record;
}
bool spidy::native_eyes::renderOffset(uint64_t frame, Vec3& offset) {
    AcquireSRWLockShared(&offsetLock);
    const auto entry = offsets[frame % offsets.size()];
    ReleaseSRWLockShared(&offsetLock);
    offset = entry.generation == frame ? entry.offset : Vec3{};
    return frame && entry.generation == frame;
}
bool spidy::native_eyes::renderWebStart(uint64_t frame, unsigned hand, Vec3& start) {
    AcquireSRWLockShared(&offsetLock);
    const auto entry = offsets[frame % offsets.size()];
    ReleaseSRWLockShared(&offsetLock);
    if (!frame || entry.generation != frame || hand >= 2 || !(entry.ropes & (1u << hand)))
        return false;
    start = entry.ropeStarts[hand];
    return true;
}
void spidy::native_eyes::reportWebGap(float metres) {
    if (!std::isfinite(metres) || metres < 0)
        return;
    AcquireSRWLockExclusive(&telemetry);
    auto& data = SpidyAppearanceData;
    InterlockedIncrement64(&data.sequence);
    ++data.webGapFrames;
    data.webGapLast = metres;
    data.webGapMax = std::max(data.webGapMax, metres);
    data.webGapSum += metres;
    InterlockedIncrement64(&data.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
