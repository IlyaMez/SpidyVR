// Swing prediction runs inside a native world-query lease. MoverStandard remains
// responsible for body collision and committing the resulting position.
#include "spidy/game_swing.hpp"
#include "spidy/native_movement.hpp"
#include "spidy/native_query_context.hpp"
#include <atomic>
#include <cstring>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_swing;
extern "C" {
__declspec(dllexport) Data SpidySwingData;
}
namespace {
using Call = DWORD(WINAPI*)(void*);
Call startMotion{}, submitMotion{}, sampleMotion{}, stopMotion{};
Config config;
Command command;
Swing solver;
InputSampleClock inputClock;
SRWLOCK lifecycle = SRWLOCK_INIT, control = SRWLOCK_INIT, output = SRWLOCK_INIT, simulation = SRWLOCK_INIT;
std::atomic<bool> enabled{};
bool started{}, owned{}, initialized{};
uint64_t deadline{}, inputDeadline{}, motionSerial{}, lastStep{}, worldIdentity{};
SwingTakeoff takeoffTransition;
LARGE_INTEGER frequency{};
bool copy(void* destination, const void* source, size_t bytes) {
    __try {
        if (reinterpret_cast<uintptr_t>(source) < 0x10000 ||
            reinterpret_cast<uintptr_t>(destination) < 0x10000)
            return false;
        std::memcpy(destination, source, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void relinquishMotion() {
    if (owned) {
        native_movement::Command c;
        c.serial = ++motionSerial;
        submitMotion(&c);
    }
    owned = false;
}
void cancel() {
    relinquishMotion();
    takeoffTransition.reset();
    solver.releaseAll();
    inputClock.reset();
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidySwingData.sequence);
    SpidySwingData.owned = SpidySwingData.takeoff = 0;
    SpidySwingData.takeoffPhase = SpidySwingData.takeoffAttempts = 0;
    for (auto& web : SpidySwingData.webs)
        web = {};
    InterlockedIncrement64(&SpidySwingData.sequence);
    ReleaseSRWLockExclusive(&output);
}
void fault(uint32_t error) {
    cancel();
    enabled = false;
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidySwingData.sequence);
    SpidySwingData.status = 4;
    SpidySwingData.owned = 0;
    SpidySwingData.error = error;
    InterlockedIncrement64(&SpidySwingData.sequence);
    ReleaseSRWLockExclusive(&output);
}
void visit(const native_rays::QueryContext& world) {
    // Stop waits for this lock before stopping the movement module.
    AcquireSRWLockExclusive(&simulation);
    struct Unlock {
        ~Unlock() {
            ReleaseSRWLockExclusive(&simulation);
        }
    } unlock;
    if (!enabled)
        return;
    if (GetTickCount64() >= deadline) {
        cancel();
        enabled = false;
        return;
    }
    Command c;
    AcquireSRWLockShared(&control);
    c = command;
    if (GetTickCount64() >= inputDeadline)
        c.focused = 0;
    ReleaseSRWLockShared(&control);
    native_movement::Data motion;
    if (sampleMotion(&motion)) {
        fault(3001);
        return;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const bool fresh = motion.qpc && now.QuadPart >= static_cast<int64_t>(motion.qpc) &&
                       static_cast<double>(now.QuadPart - motion.qpc) / frequency.QuadPart < .05;
    if (!c.focused || !fresh || (motion.status != 1 && motion.status != 2) ||
        motion.moverFlags & 0x80000000u || (worldIdentity && worldIdentity != world.identity())) {
        cancel();
        worldIdentity = world.identity();
        return;
    }
    if (motion.steps == lastStep)
        return;
    // A skipped observation would otherwise apply several missing physics steps
    // using one collision sample. Relinquish control instead of extrapolating.
    if (owned && lastStep && motion.steps > lastStep + 2) {
        cancel();
        lastStep = motion.steps;
        return;
    }
    lastStep = motion.steps;
    worldIdentity = world.identity();
    // The actor transform is at the feet. Rope constraints/visibility need a
    // harness above the supporting surface, while native motion stays in feet coordinates.
    Body body{motion.position + Vec3{0, 1, 0}, motion.achievedVelocity, motion.grounded != 0};
    if (!initialized) {
        solver.reset(body);
        initialized = true;
    }
    const float inputSeconds = inputClock.consume(c);
    const auto predicted = solver.predictNativeStep(motion.dt, input(c), world, body, inputSeconds);
    if (world.error()) {
        fault(world.error());
        return;
    }
    if (!predicted.valid) {
        cancel();
        return;
    }
    const bool attached = solver.webs()[0].attached || solver.webs()[1].attached;
    bool pointLaunched{}, newAttachment{}, zipped{};
    for (const auto& event : solver.events()) {
        pointLaunched |= event.kind == EventKind::PointLaunch;
        newAttachment |= event.kind == EventKind::Attach;
        zipped |= event.kind == EventKind::Zip;
    }
    // Keep released flight and the short post-zip landing window. This lets
    // the solver issue a point launch before handing ordinary walking back.
    // Native gravity history is not a reliable midair handoff source.
    const bool collidable = native_movement::collisionEnabled(motion.collisionFlags);
    const Vec3 delta = predicted.target - body.position;
    if (!finite(delta)) {
        fault(3002);
        return;
    }
    // Constraint correction and world-coordinate float rounding can make the
    // requested displacement slightly exceed the solver's velocity limit.
    // Cap the actual native request instead of relinquishing control midair.
    Vec3 requested = limited(delta / motion.dt, std::min(config.maxSpeed * (1.f - 1e-6f), 1.75f / motion.dt));
    bool overhead{};
    for (const auto& web : solver.webs())
        overhead |= web.attached && web.anchor.y > body.position.y + .5f;
    const bool wantsLift = requested.y > .25f || (newAttachment && overhead) || c.jump || pointLaunched;
    const auto transition =
        takeoffTransition.update(GetTickCount64(), attached, body.grounded, collidable, wantsLift,
                                 zipped || pointLaunched || newAttachment, body.position, body.velocity,
                                 (zipped || pointLaunched) ? predicted.velocity : Vec3{}, pointLaunched);
    if (transition.resumed)
        requested = limited(requested + transition.launchVelocity - body.velocity,
                            std::min(config.maxSpeed * (1.f - 1e-6f), 1.75f / motion.dt));
    // Walking/landing and takeoff belong to the native state machine, even if
    // we owned flight on the preceding frame. Preserve held webs across this handoff.
    const bool drive =
        collidable && !body.grounded && !transition.waiting && (attached || owned || transition.resumed);
    if (drive) {
        native_movement::Command request;
        request.enabled = 1;
        request.serial = ++motionSerial;
        request.leaseMs = 50;
        request.velocity = requested;
        if (const auto error = submitMotion(&request)) {
            fault(error);
            return;
        }
        owned = true;
    } else if (owned) {
        relinquishMotion();
    }
    AcquireSRWLockExclusive(&output);
    auto& d = SpidySwingData;
    InterlockedIncrement64(&d.sequence);
    d.status = drive ? 2 : 1;
    d.qpc = now.QuadPart;
    ++d.steps;
    d.controlled += drive;
    d.serial = c.serial;
    d.world = world.identity();
    d.sourceStep = motion.steps;
    d.position = motion.position;
    d.velocity = body.velocity;
    d.requested = requested;
    d.dt = motion.dt;
    d.owned = owned;
    d.grounded = motion.grounded;
    d.collisionFlags = motion.collisionFlags;
    d.takeoff = transition.jump;
    d.takeoffPhase = takeoffTransition.phase();
    d.takeoffAttempts = takeoffTransition.attempts();
    d.takeoffTimeouts += transition.timedOut;
    d.nativeContact = motion.contact;
    for (auto event : solver.events()) {
        d.attaches += event.kind == EventKind::Attach;
        d.releases += event.kind == EventKind::Release || event.kind == EventKind::TrackingLost ||
                      event.kind == EventKind::Obstructed;
        d.zips += event.kind == EventKind::Zip;
        d.misses += event.kind == EventKind::Miss;
        d.obstructed += event.kind == EventKind::Obstructed;
        d.trackingLost += event.kind == EventKind::TrackingLost;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const auto& w = solver.webs()[i];
        d.webs[i] = {w.attached, static_cast<uint32_t>(w.surface), w.anchor, w.length, w.tension};
    }
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&output);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        if (started) {
            result = 1000;
            break;
        }
        if (!copy(&config, input, sizeof(config)) || config.magic != 0x53574346 || config.version != 1 ||
            config.bytes != sizeof(config) || config.pid != GetCurrentProcessId() ||
            config.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) || config.reserved ||
            (config.durationMs && config.durationMs < 1000) || config.durationMs > 30000 ||
            !std::isfinite(config.maxSpeed) || config.maxSpeed <= 0 || config.maxSpeed > 65 ||
            !std::isfinite(config.gravity) || config.gravity < 0 || config.gravity > 30) {
            result = 1001;
            break;
        }
        const auto module = reinterpret_cast<HMODULE>(config.motionModule);
        startMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionStart"));
        submitMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionSubmit"));
        sampleMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionSample"));
        stopMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionStop"));
        if (!startMotion || !submitMotion || !sampleMotion || !stopMotion) {
            result = 1002;
            break;
        }
        native_movement::Config motion;
        motion.pid = config.pid;
        motion.base = config.base;
        motion.record = config.record;
        motion.mover = config.mover;
        motion.durationMs = config.durationMs;
        motion.maxSpeed = config.maxSpeed;
        motion.syncAirVelocity = 1;
        if ((result = startMotion(&motion)))
            break;
        started = true;
        solver = Swing(physicsConfig(config));
        QueryPerformanceFrequency(&frequency);
        deadline = config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
        enabled = true;
        native_rays::setVisitor(visit);
    } while (false);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingSubmit(void* input) {
    Command c;
    if (!copy(&c, input, sizeof(c)) || !valid(c))
        return 2001;
    AcquireSRWLockExclusive(&control);
    DWORD result{};
    if (!enabled || GetTickCount64() >= deadline)
        result = 2002;
    else if (c.serial <= command.serial)
        result = 2003;
    else {
        command = c;
        inputDeadline = GetTickCount64() + c.leaseMs;
    }
    ReleaseSRWLockExclusive(&control);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingSample(void* out) {
    AcquireSRWLockShared(&control);
    AcquireSRWLockShared(&output);
    auto sample = SpidySwingData;
    ReleaseSRWLockShared(&output);
    if (!enabled || GetTickCount64() >= deadline || GetTickCount64() >= inputDeadline || !command.focused) {
        sample.status = 0;
        sample.owned = 0;
        sample.takeoff = 0;
        sample.takeoffPhase = sample.takeoffAttempts = 0;
        for (auto& web : sample.webs)
            web = {};
    }
    ReleaseSRWLockShared(&control);
    return copy(out, &sample, sizeof(sample)) ? 0 : 2201;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    native_rays::setVisitor(nullptr);
    AcquireSRWLockExclusive(&simulation);
    enabled = false;
    cancel();
    ReleaseSRWLockExclusive(&simulation);
    const DWORD result = started ? stopMotion(nullptr) : 0;
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidySwingData.sequence);
    if (result)
        SpidySwingData.error = result;
    SpidySwingData.status = SpidySwingData.error ? 4 : 3;
    SpidySwingData.owned = 0;
    InterlockedIncrement64(&SpidySwingData.sequence);
    ReleaseSRWLockExclusive(&output);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
