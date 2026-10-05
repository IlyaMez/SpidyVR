// Leased velocity requests enter MoverStandard before its native body queries.
// Actor transforms, collision results, and native movement flags are never written directly.
// Owned steps also discard the native mover's duplicate gravity accumulator.
#include "spidy/native_movement.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
using namespace spidy::native_movement;
extern "C" {
__declspec(dllexport) Data SpidyMotionData;
}
namespace {
using Method = uintptr_t (*)(void*);
using Request = uintptr_t (*)(void*, const Vec3*, const Vec3*, uint8_t);
using SetTarget = uintptr_t (*)(void*, const Vec3*);
using Timestep = float (*)(void*);
using AirEvent = uintptr_t (*)(void*, void*);
using EventRead = bool (*)(void*, int, void*, unsigned);
Method prequery{}, gravity{};
AirEvent airEvent{};
EventRead eventRead{};
Request request{};
SetTarget setTarget{};
Timestep timestep{};
Config config{};
Command command{};
void* hooks[4]{};
unsigned hookCount{};
uint32_t handle{};
std::atomic<bool> enabled{};
std::atomic<unsigned> active{};
std::atomic<uint64_t> corrections{};
uint64_t deadline{}, commandDeadline{};
SRWLOCK lifecycle = SRWLOCK_INIT, control = SRWLOCK_INIT, telemetry = SRWLOCK_INIT;
// A collision step is synchronous up to dispatch of its body-query job. Gravity
// correction is scoped to this exact native invocation, never another actor/thread.
struct Override {
    void* self;
    Vec3 target;
};
thread_local Override* current{};
struct AirOverride {
    Vec3 delta;
    uint64_t applied{};
};
thread_local AirOverride* currentAir{};
struct Guard {
    Guard() {
        ++active;
    }
    ~Guard() {
        --active;
    }
};
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
bool live() {
    const auto table = pointer(config.base + 0x7a44320);
    const auto index = handle & 0xfffff;
    uint32_t count{}, generation{}, own{};
    return table && read(config.base + 0x7a44340, &count, 4) && index < count && count <= 0x100000 &&
           read(table + index * 16 + 8, &generation, 4) && generation == (handle >> 20) && generation &&
           pointer(table + index * 16) == config.mover && pointer(config.mover) == config.base + 0x4f70168 &&
           pointer(config.mover + 8) == config.record && read(config.mover + 0x14, &own, 4) && own == handle;
}
Command leased() {
    Command c{};
    AcquireSRWLockShared(&control);
    const auto now = GetTickCount64();
    if (enabled && now < deadline && now < commandDeadline)
        c = command;
    ReleaseSRWLockShared(&control);
    return c;
}
bool readEvent(void* event, int field, void* out, unsigned bytes) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Guard guard;
    const bool result = eventRead(event, field, out, bytes);
    if (result && currentAir && caller == config.base + 0xa7b489 && bytes == sizeof(Vec3)) {
        // Only the local airborne event's displacement field. Its native handler
        // derives both velocity and gravity history from this returned value.
        std::memcpy(out, &currentAir->delta, sizeof(Vec3));
        ++currentAir->applied;
    }
    return result;
}
uintptr_t handleAir(void* self, void* event) {
    Guard guard;
    const auto address = reinterpret_cast<uintptr_t>(self);
    const auto table = pointer(address);
    if (!enabled || GetTickCount64() >= deadline || !live() || pointer(address + 8) != config.record ||
        (table != config.base + 0x38c1340 && table != config.base + 0x38c9090))
        return airEvent(self, event);
    const auto c = leased();
    const float dt = timestep(self);
    uint32_t flags{}, collision{};
    const bool drive = config.syncAirVelocity && c.enabled && !currentAir &&
                       read(config.mover + 0x750, &flags, 4) && !(flags & 0x80000000u) &&
                       read(config.mover + 0x144, &collision, 4) && collisionEnabled(collision) &&
                       std::isfinite(dt) && dt > 0 && dt <= .05f && length(c.velocity) * dt <= 2;
    AirOverride override{c.velocity * dt};
    auto* previous = currentAir;
    if (drive)
        currentAir = &override;
    const auto result = airEvent(self, event);
    currentAir = previous;
    Vec3 velocity{};
    float vertical{}, acceleration{};
    if (read(address + 0x3f8, &velocity, 12) && read(address + 0x3cc, &vertical, 4) &&
        read(address + 0x3a8, &acceleration, 4)) {
        AcquireSRWLockExclusive(&telemetry);
        auto& d = SpidyMotionData;
        InterlockedIncrement64(&d.sequence);
        ++d.airEvents;
        d.airOverrides += override.applied;
        d.airState = table;
        d.airVelocity = velocity;
        d.airVertical = vertical;
        d.airGravity = acceleration;
        d.airDt = dt;
        InterlockedIncrement64(&d.sequence);
        ReleaseSRWLockExclusive(&telemetry);
    }
    return result;
}
uintptr_t applyGravity(void* self) {
    Guard guard;
    const auto result = gravity(self);
    if (current && current->self == self) {
        // 1fbda50 integrates fall speed at +6f0 even while our target replaces
        // its displacement. The solver already includes gravity. Keeping both
        // integrators caused a sudden dive when the movement lease ended.
        const float zero{};
        std::memcpy(static_cast<uint8_t*>(self) + 0x6f0, &zero, sizeof(zero));
        setTarget(self, &current->target);
        ++corrections;
    }
    return result;
}
uintptr_t query(void* self) {
    Guard guard;
    if (!enabled || GetTickCount64() >= deadline || reinterpret_cast<uintptr_t>(self) != config.mover ||
        !live())
        return prequery(self);
    Vec3 position{};
    uint32_t flags{}, collision{};
    uint8_t contact{};
    if (!read(pointer(config.record) + 0x30, &position, 12) || !finite(position) ||
        !read(config.mover + 0x750, &flags, 4) || !read(config.mover + 0x144, &collision, 4) ||
        !read(config.mover + 0x6ee, &contact, 1))
        return prequery(self);
    const float dt = timestep(self);
    const auto c = leased();
    const bool drive = c.enabled && collisionEnabled(collision) && !(flags & 0x80000000u) &&
                       std::isfinite(dt) && dt > 0 && dt <= .05f && length(c.velocity) * dt <= 2.f &&
                       !current;
    Override step{self, position};
    Override* previous = current;
    if (drive) {
        const Vec3 delta = c.velocity * dt, keepDirection{};
        // This verified native function sets start/target and prepares its own bookkeeping.
        request(self, &delta, &keepDirection, 0);
        step.target = position + delta;
        current = &step;
    }
    const auto result = prequery(self);
    current = previous;
    Vec3 requested{};
    read(config.mover + 0x11c, &requested, 12);
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    AcquireSRWLockExclusive(&telemetry);
    auto& d = SpidyMotionData;
    InterlockedIncrement64(&d.sequence);
    // The previous step's collision result is available at the next prequery.
    d.achievedVelocity = d.steps && d.dt > 0 ? (position - d.position) / d.dt : Vec3{};
    d.position = position;
    d.requested = requested;
    d.dt = dt;
    d.moverFlags = flags;
    d.collisionFlags = collision;
    d.grounded = groundedContact(contact);
    d.contact = contact;
    d.qpc = qpc.QuadPart;
    ++d.steps;
    d.controlled += drive;
    d.serial = drive ? c.serial : 0;
    d.gravityCorrections = corrections;
    d.thread = GetCurrentThreadId();
    d.status = drive ? 2 : 1;
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    return result;
}
bool signature(uintptr_t rva, const uint8_t* expected, size_t n) {
    uint8_t bytes[16]{};
    return n <= sizeof(bytes) && read(config.base + rva, bytes, n) && !std::memcmp(bytes, expected, n);
}
DWORD disable() {
    enabled = false;
    DWORD result{};
    for (unsigned i = 0; i < hookCount; ++i) {
        const auto s = MH_DisableHook(hooks[i]);
        if (s != MH_OK && s != MH_ERROR_DISABLED)
            result = 1400 + s;
    }
    const auto until = GetTickCount64() + 2000;
    while (active && GetTickCount64() < until)
        Sleep(1);
    return active ? 1501 : result;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMotionStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        if (hookCount) {
            result = 1000;
            break;
        }
        if (!read(reinterpret_cast<uintptr_t>(input), &config, sizeof(config)) ||
            config.magic != 0x534d5643 || config.version != 2 || config.bytes != sizeof(config) ||
            config.pid != GetCurrentProcessId() ||
            config.base != reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)) ||
            !GetModuleHandleW(L"Spider-Man.exe") || (config.durationMs && config.durationMs < 1000) ||
            config.durationMs > 30000 || !std::isfinite(config.maxSpeed) || config.maxSpeed <= 0 ||
            config.maxSpeed > 65 || config.syncAirVelocity > 1 || config.reserved) {
            result = 1001;
            break;
        }
        if (!read(config.mover + 0x14, &handle, 4) || !live()) {
            result = 1002;
            break;
        }
        constexpr uint8_t a[] = {0x40, 0x55, 0x53, 0x48, 0x8d, 0x6c, 0x24, 0xb1,
                                 0x48, 0x81, 0xec, 0x98, 0,    0,    0};
        constexpr uint8_t b[] = {0x40, 0x53, 0x48, 0x81, 0xec, 0x80, 0, 0, 0};
        constexpr uint8_t c[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30};
        constexpr uint8_t d[] = {0xf3, 0x0f, 0x10, 0x4a, 0x08, 0x0f, 0x16, 0x0a};
        constexpr uint8_t e[] = {0xf2, 0x0f, 0x10, 0x05, 0xe0, 0x91, 0x40, 0x06, 0x66, 0x0f, 0x5a, 0xc0};
        constexpr uint8_t f[] = {0x40, 0x55, 0x53, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x60};
        constexpr uint8_t g[] = {0x48, 0x83, 0xec, 0x28, 0x48, 0x63, 0xc2, 0x4d, 0x8b, 0xd0};
        if (!signature(0x1fbe360, a, sizeof(a)) || !signature(0x1fbda50, b, sizeof(b)) ||
            !signature(0x1fc2e10, c, sizeof(c)) || !signature(0x1fb88d0, d, sizeof(d)) ||
            !signature(0x16769f0, e, sizeof(e)) || !signature(0xa7b3a0, f, sizeof(f)) ||
            !signature(0x1f9db60, g, sizeof(g))) {
            result = 1003;
            break;
        }
        auto s = MH_Initialize();
        if (s != MH_OK) {
            result = 1100 + s;
            break;
        }
        const uintptr_t rvas[] = {0x1fbe360, 0x1fbda50, 0xa7b3a0, 0x1f9db60};
        void* detours[] = {reinterpret_cast<void*>(query), reinterpret_cast<void*>(applyGravity),
                           reinterpret_cast<void*>(handleAir), reinterpret_cast<void*>(readEvent)};
        void** originals[] = {reinterpret_cast<void**>(&prequery), reinterpret_cast<void**>(&gravity),
                              reinterpret_cast<void**>(&airEvent), reinterpret_cast<void**>(&eventRead)};
        for (unsigned i = 0; i < (config.syncAirVelocity ? 4u : 2u); ++i) {
            hooks[i] = reinterpret_cast<void*>(config.base + rvas[i]);
            s = MH_CreateHook(hooks[i], detours[i], originals[i]);
            if (s != MH_OK) {
                result = 1200 + s;
                break;
            }
            ++hookCount;
        }
        if (result)
            break;
        request = reinterpret_cast<Request>(config.base + 0x1fc2e10);
        setTarget = reinterpret_cast<SetTarget>(config.base + 0x1fb88d0);
        timestep = reinterpret_cast<Timestep>(config.base + 0x16769f0);
        deadline = config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
        enabled = true;
        for (unsigned i = 0; i < hookCount; ++i) {
            s = MH_EnableHook(hooks[i]);
            if (s != MH_OK) {
                result = 1300 + s;
                break;
            }
        }
    } while (false);
    if (result && hookCount)
        disable();
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMotionSubmit(void* input) {
    Command c{};
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x534d564d || c.version != 1 ||
        c.bytes != sizeof(c) || !c.serial || c.enabled > 1 || c.leaseMs > 250 || (c.enabled && !c.leaseMs) ||
        !finite(c.velocity) || length(c.velocity) > config.maxSpeed || c.reserved || c.reserved2)
        return 2001;
    AcquireSRWLockExclusive(&control);
    DWORD result{};
    if (!enabled || GetTickCount64() >= deadline)
        result = 2002;
    else if (c.serial <= command.serial)
        result = 2003;
    else {
        command = c;
        commandDeadline = GetTickCount64() + c.leaseMs;
    }
    ReleaseSRWLockExclusive(&control);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMotionStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    const auto result = disable();
    AcquireSRWLockExclusive(&telemetry);
    auto& d = SpidyMotionData;
    InterlockedIncrement64(&d.sequence);
    d.status = 3;
    d.error = result;
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
namespace {
bool copySample(void* out, const Data& sample) {
    __try {
        if (reinterpret_cast<uintptr_t>(out) < 0x10000)
            return false;
        std::memcpy(out, &sample, sizeof(sample));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMotionSample(void* out) {
    AcquireSRWLockShared(&telemetry);
    Data sample = SpidyMotionData;
    ReleaseSRWLockShared(&telemetry);
    if (!enabled || GetTickCount64() >= deadline)
        sample.status = 0;
    return copySample(out, sample) ? 0 : 2201;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
