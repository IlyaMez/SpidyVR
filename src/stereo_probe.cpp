// Tests the engine's own secondary scene views on its view-maintenance thread.
// The original scene pass remains owned by the game. No desktop capture is used.
#include "spidy/eye_job_table.hpp"
#include "spidy/eye_resolution.hpp"
#include "spidy/native_appearance.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_eye_gpu.hpp"
#include "spidy/native_view.hpp"
#include "spidy/native_webs.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
using native_view::Descriptor;
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
extern "C" {
__declspec(dllexport) Data SpidyStereoData;
__declspec(dllexport) native_eyes::FrameData SpidyStereoFrames;
__declspec(dllexport) native_appearance::Data SpidyAppearanceData;
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
std::atomic<bool> latchedImmersive{};
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
using InitBuffers = bool (*)(void*, const void*, const char*);
InitBuffers originalInitBuffers{};
void* initBuffersHook{};
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
    // 1796d20 copies the scene view into context +8. The primary hide uses the
    // game's actor visibility switch in maintain(); this per-eye check removes
    // anything still drawn as the hero or on the hero's root transform. It
    // changes no actor flags, animation state, or gameplay visibility.
    if (enabled && actor && latchedImmersive.load(std::memory_order_relaxed)) {
        const int eye = eyeView(pointer(reinterpret_cast<uintptr_t>(context) + 8));
        const auto& hero = heroFrames[heroSlot.load(std::memory_order_acquire) % heroFrames.size()];
        const auto instance = reinterpret_cast<uintptr_t>(actor);
        if (eye >= 0 && hero.instance) {
            float m[16]{};
            const bool placed = instance != hero.instance && read(instance, m, sizeof(m));
            // The game's web tube and end cone are separate instances; never
            // treat them as avatar pieces.
            const bool web = placed && native_webs::webInstance(instance);
            if (instance == hero.instance || (placed && !web && sameRoot(m, hero.transform))) {
                appearanceEvent(static_cast<unsigned>(eye), true);
                return;
            }
            if (placed && !web)
                nearInstance(static_cast<unsigned>(eye), instance, m, hero.transform);
        }
    }
    originalRenderActor(context, actor, visibility);
}
// Runs on the main thread in maintain(), after gameplay updated the hero and
// before render jobs read it. Uses DrawOff 191afb0 / DrawOn 191b880, the
// handlers of the game's ActorDrawAction script node (15a3460).
void updateHero(bool immersive) {
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
    if (const auto previous = hiddenHero.load(); previous && (previous != frame.instance || !immersive)) {
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
    if (immersive && drawable && !(flags & 0x20)) {
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
    data.nativeHiddenFrames += immersive && drawable && (flags & 0x20);
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
            break;
        }
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
            std::memcpy(local, &config.width, 4);
            std::memcpy(local + 4, &config.height, 4);
            const uint64_t noParent{};
            std::memcpy(local + 0x18, &noParent, 8);
            return originalInitBuffers(buffers, local, name);
        }
    }
    return originalInitBuffers(buffers, description, name);
}
void copyJob(void* view, void* settings, void* a, void* b, bool c) {
    originalCopyJob(view, settings, a, b, c);
    if (!enabled)
        return;
    for (unsigned i = 0; i < 2; ++i)
        if (view == eyes[i].load()) {
            const auto job = reinterpret_cast<void*>(pointer(reinterpret_cast<uintptr_t>(settings) + 0x4b0));
            if (!job)
                return;
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
        // The main view's exposure history is fed by its luminance readback.
        // Secondary views start with zero history. Share the current exposure
        // values between eyes, while keeping their buffers and histories owned
        // separately. Native 18a01c0 and 189bd30 identify these four scalars.
        const auto primary = pointer(base + 0x7a34dd0);
        float exposure[2]{};
        if (read(primary + 0x1708, exposure, sizeof(exposure)) && std::isfinite(exposure[0]) &&
            std::isfinite(exposure[1]) && exposure[0] > 0 && exposure[1] > 0 && exposure[0] < 1e6f &&
            exposure[1] < 1e6f) {
            auto bytes = static_cast<uint8_t*>(view);
            std::memcpy(bytes + 0x1708, exposure, sizeof(exposure));
            std::memcpy(bytes + 0x1720, exposure, sizeof(exposure));
        }
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
// The engine's active view (ViewContextManager +f8) is its first pool view: the
// stock camera. Offscreen eyes are never active, and the "drawn in any view"
// (18a1890) and "sphere in any view" (18a12a0) queries visit pool views only.
// Shaders get the active view's position from the frame-wide GlobalWorldCBuffer
// (m_ActiveViewCtxPos, +330), which also carries the key-light shadow setup.
// All of that was done for the stock camera behind the hero, not the headset.
// While immersive, move the view to the tracked head with a lens covering both
// eyes. The monitor shows this view; the gameplay camera object is unchanged.
bool activeView(const Descriptor* incoming, Descriptor& out) {
    uint64_t ropeSamples{};
    Vec3 ropeHero{};
    native_webs::heroSample(ropeSamples, ropeHero);
    const bool shared = activeHero.fresh(ropeSamples, ropeHero);
    uint64_t mask{};
    Descriptor stock{};
    if (!read(base + 0x7a34dd8, &mask, 8) || !(mask & 1) ||
        !read(reinterpret_cast<uintptr_t>(incoming), &stock, sizeof(stock)) || !native_view::valid(stock))
        return false;
    // The flat screen keeps the stock camera's shape whichever view is drawn.
    flatAspect = (stock.values[0x404 / 4] - stock.values[0x400 / 4]) /
                 (stock.values[0x40c / 4] - stock.values[0x408 / 4]);
    if (!(config.createViews & 8) || stopRequested || GetTickCount64() >= deadline || live.load() != 2)
        return false;
    AcquireSRWLockShared(&commandLock);
    const auto desired = command;
    const bool immersive = desired.enabled == 1 && GetTickCount64() < commandDeadline;
    ReleaseSRWLockShared(&commandLock);
    if (!immersive)
        return false;
    Mat4 pose{};
    native_eyes::Bounds lens{};
    bool placed = native_eyes::headView(desired, pose, lens);
    if (placed) {
        // The same render-frame correction placeEyes() gives the eyes.
        Vec3 offset{}, rendered = ropeHero;
        if (desired.anchored && (shared || heroPosition(rendered))) {
            native_eyes::reanchorOffset(desired.anchor, rendered, offset);
            activeHeroPosition = rendered;
            activeHeroPlaced = true;
        }
        pose[12] += offset.x;
        pose[13] += offset.y;
        pose[14] += offset.z;
        out = stock;
        // Keep the stock camera's clip planes and temporal jitter.
        reinterpret_cast<Lens>(base + 0x187bb00)(&out, stock.nearZ(), stock.farZ(), lens.left, lens.right,
                                                 lens.top, lens.bottom, 0, 0, stock.values[0x428 / 4],
                                                 stock.values[0x42c / 4], false, false);
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
    if (render) {
        AcquireSRWLockShared(&commandLock);
        desired = command;
        controlled = desired.enabled && GetTickCount64() < commandDeadline;
        ReleaseSRWLockShared(&commandLock);
    }
    latchedImmersive = render && enabled && controlled && desired.enabled == 1;
    if ((config.createViews & 4) && (latchedImmersive || hiddenHero.load()))
        updateHero(latchedImmersive);
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
    for (auto& slot : eyes)
        // A load can replace the native view pool between frames. maintain()
        // forgets an eye that left it; until then, never write to one.
        if (auto eye = slot.load(); eye && owned(eye)) {
            auto descriptor = main;
            const unsigned index = eye == eyes[0].load() ? 0 : 1;
            if (controlled && desired.enabled == 2) {
                reinterpret_cast<Submit>(base + 0x1899ab0)(eye, &descriptor);
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
            reinterpret_cast<Lens>(base + 0x187bb00)(
                &descriptor, main.nearZ(), main.farZ(), controlled ? std::tan(fov[0]) : -aspect,
                controlled ? std::tan(fov[1]) : aspect, controlled ? -std::tan(fov[3]) : -1,
                controlled ? -std::tan(fov[2]) : 1, 0, 0, 0, 0, false, false);
            reinterpret_cast<SetPose>(base + 0x187ca10)(&descriptor, pose.data());
            reinterpret_cast<Submit>(base + 0x1899ab0)(eye, &descriptor);
        }
}
// 1920240 sets up this frame's render jobs for the offscreen views.
uint8_t renderOffscreen() {
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
    AcquireSRWLockExclusive(&telemetry);
    auto& d = SpidyStereoData;
    InterlockedIncrement64(&d.sequence);
    ++d.frames;
    d.primary = primary;
    d.thread = GetCurrentThreadId();
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
        // 16 (VR path only) occlusion culling for each eye (giveOcclusion).
        const auto views = config.createViews & ~16u;
        if (!GetModuleHandleW(L"Spider-Man.exe") || config.magic != 0x53534346 || config.version != 1 ||
            config.bytes != sizeof(config) || config.pid != GetCurrentProcessId() || config.base != base ||
            config.durationMs > 30000 || (config.durationMs && config.durationMs < 500) ||
            (views != 0 && views != 1 && views != 3 && views != 5 && views != 7 && views != 13) ||
            ((config.createViews & 16) && !(views & 4)) || !validEyeSize(config.width) ||
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
        const unsigned char renderActorBytes[] = {0x48, 0x8b, 0xc4, 0x44, 0x88, 0x40, 0x18, 0x48,
                                                  0x89, 0x50, 0x10, 0x48, 0x89, 0x48, 0x08};
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
        const unsigned char historyBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20,
                                              0x48, 0x8b, 0xf9, 0x41, 0xb8, 0x30, 0x05, 0x00, 0x00};
        // A view's occlusion builder, up to its test of +1ba0.
        const unsigned char occlusionBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
                                                0x18, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xf9,
                                                0x48, 0x83, 0xb9, 0xa0, 0x1b, 0x00, 0x00, 0x00};
        if (!entry(0x18a0bb0, maintainBytes, sizeof(maintainBytes)) ||
            !entry(0x1899ab0, submitBytes, sizeof(submitBytes)) ||
            !entry(0x164703f, cameraSubmitBytes, sizeof(cameraSubmitBytes)) ||
            !entry(0x187ca10, poseBytes, sizeof(poseBytes)) ||
            !entry(0x1920240, renderOffscreenBytes, sizeof(renderOffscreenBytes)) ||
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
            !entry(0x17991a0, renderActorBytes, sizeof(renderActorBytes)) ||
            !entry(0x1920310, setupDisplayBytes, sizeof(setupDisplayBytes)) ||
            !entry(0x187e130, enqueueBytes, sizeof(enqueueBytes)) ||
            !entry(0x191afb0, drawOffBytes, sizeof(drawOffBytes)) ||
            !entry(0x191b880, drawOnBytes, sizeof(drawOnBytes))) {
            result = 1002;
            break;
        }
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
            renderActorHook = reinterpret_cast<void*>(base + 0x17991a0);
            setupDisplayHook = reinterpret_cast<void*>(base + 0x1920310);
            submitHook = reinterpret_cast<void*>(base + 0x1899ab0);
            renderOffscreenHook = reinterpret_cast<void*>(base + 0x1920240);
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
                s = MH_CreateHook(renderActorHook, reinterpret_cast<void*>(renderActor),
                                  reinterpret_cast<void**>(&originalRenderActor));
            if (s == MH_OK)
                s = MH_CreateHook(setupDisplayHook, reinterpret_cast<void*>(setupDisplay),
                                  reinterpret_cast<void**>(&originalSetupDisplay));
            if (s == MH_OK)
                s = MH_CreateHook(submitHook, reinterpret_cast<void*>(submit),
                                  reinterpret_cast<void**>(&originalSubmit));
            if (s == MH_OK)
                s = MH_CreateHook(renderOffscreenHook, reinterpret_cast<void*>(renderOffscreen),
                                  reinterpret_cast<void**>(&originalRenderOffscreen));
            if (s != MH_OK) {
                result = 1200 + s;
                for (auto hook :
                     {maintainHook, updateHook, copyFinalHook, toneHook, copyJobHook, beginJobHook,
                      endJobHook, initBuffersHook, renderActorHook, setupDisplayHook, submitHook, renderOffscreenHook})
                    MH_RemoveHook(hook);
                break;
            }
            hooked = true;
        }
        attempted = false;
        stopRequested = false;
        deadline = config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
        auto s = MH_EnableHook(updateHook);
        if (s == MH_OK)
            s = MH_EnableHook(copyFinalHook);
        if (s == MH_OK)
            s = MH_EnableHook(toneHook);
        for (auto hook : {copyJobHook, beginJobHook, endJobHook, initBuffersHook, renderActorHook,
                          setupDisplayHook, submitHook, renderOffscreenHook})
            if (s == MH_OK)
                s = MH_EnableHook(hook);
        if (s == MH_OK)
            s = MH_EnableHook(maintainHook);
        if (s != MH_OK) {
            for (auto hook : {updateHook, copyFinalHook, toneHook, copyJobHook, beginJobHook, endJobHook,
                              initBuffersHook, renderActorHook, setupDisplayHook, submitHook, renderOffscreenHook})
                MH_DisableHook(hook);
            result = 1300 + s;
            break;
        }
        enabled = true;
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
    // Keep maintenance installed until both owned views are excluded from jobs
    // and the main thread has restored the hero's native visibility.
    const auto limit = GetTickCount64() + 5000;
    while ((live || hiddenHero.load()) && GetTickCount64() < limit)
        Sleep(10);
    if (live)
        result = 1401;
    else if (hooked) {
        enabled = false;
        for (auto hook : {maintainHook, updateHook, copyFinalHook, toneHook, copyJobHook, beginJobHook,
                          endJobHook, initBuffersHook, renderActorHook, setupDisplayHook, submitHook, renderOffscreenHook}) {
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
extern "C" float SpidyFlatAspect() {
    return flatAspect.load();
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
