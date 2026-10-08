// The game's time for Spidy's slow motion (game_time.hpp). Addresses are
// image offsets in the supported Spider-Man.exe; docs/REFERENCE.md lists them.
#include "spidy/game_time.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_time;

namespace {
constexpr uintptr_t updateRva = 0x19bb430;
// The system's scale and physics scale, and its flag: physics follows.
constexpr uintptr_t scaleAt = 0x18, physicsAt = 0x1c, physicsFlagAt = 0x7d0;
// What install checks: the update's entry, and the instructions in it whose
// offsets and addresses this module relies on.
struct Check {
    uintptr_t rva;
    unsigned char bytes[16];
    size_t count;
};
const Check checks[] = {
    {updateRva, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56}, 16},
    // movss [rbx+0x18], xmm0: the blended scale.
    {0x19bb6ca, {0xf3, 0x0f, 0x11, 0x43, 0x18}, 5},
    // movsd [rip+0x60c44b5], xmm0: its copy in the clock's double at 7a7fb90.
    {0x19bb6d3, {0xf2, 0x0f, 0x11, 0x05, 0xb5, 0x44, 0x0c, 0x06}, 8},
    // test byte [rbx+0x7d0], 1: physics follows the scale.
    {0x19bb6db, {0xf6, 0x83, 0xd0, 0x07, 0x00, 0x00, 0x01}, 7},
    // movss [rbx+0x1c], xmm0: the blended physics scale.
    {0x19bb70f, {0xf3, 0x0f, 0x11, 0x43, 0x1c}, 5},
    // lea rcx, [rip+0x46dee42]: Havok's step at 609a560, which 1822670 sets.
    {0x19bb717, {0x48, 0x8d, 0x0d, 0x42, 0xee, 0x6d, 0x04}, 7},
    // 1822670: step = scale * base; owner = the system.
    {0x1822670, {0xf3, 0x0f, 0x59, 0x49, 0x04, 0x4c, 0x89, 0x41, 0x08, 0xf3, 0x0f, 0x11, 0x09, 0xc3}, 14},
};
using Update = void(__fastcall*)(uintptr_t system);
Update original{};
uintptr_t imageBase{};
bool created{};
std::atomic<bool> hooked{};
std::atomic<float> wanted{1};
// Kept by the game's thread, inside the hook, and by uninstall once no
// update runs: the game's own values from the last update, what Spidy
// changed after it, and Havok's step as the game had left it while the game
// does not set the step itself.
uintptr_t lastSystem{};
float ownScale = 1, ownPhysics = 1, savedStep{};
bool scaleChanged{}, physicsChanged{}, stepSaved{};
std::atomic<uint64_t> updates{}, slowedUpdates{}, systems{};
std::atomic<float> lastOwn{1}, lastOwnPhysics{1}, lastWorld{1}, lastStep{}, lastBase{};
std::atomic<uint32_t> lastFlag{};
std::atomic<bool> restored{true};

template <class T> T get(uintptr_t at) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v));
    return v;
}
template <class T> void put(uintptr_t at, T v) {
    std::memcpy(reinterpret_cast<void*>(at), &v, sizeof(v));
}
bool matches(uintptr_t at, const unsigned char* bytes, size_t count) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(at), bytes, count) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The game's own values back where Spidy changed them.
void putBack(uintptr_t system) {
    const uintptr_t step = imageBase + physicsStepRva;
    if (scaleChanged) {
        put<float>(system + scaleAt, ownScale);
        put<double>(imageBase + clockScaleRva, ownScale);
    }
    if (physicsChanged) {
        put<float>(system + physicsAt, ownPhysics);
        put<float>(step, ownPhysics * get<float>(step + 4));
    }
    if (stepSaved)
        put<float>(step, savedStep);
    scaleChanged = physicsChanged = stepSaved = false;
}
void __fastcall update(uintptr_t system) {
    // The game blends from its own values and sends its events from them.
    if (system == lastSystem) {
        if (scaleChanged)
            put<float>(system + scaleAt, ownScale);
        if (physicsChanged)
            put<float>(system + physicsAt, ownPhysics);
        scaleChanged = physicsChanged = false;
    } else {
        // A new system (a level change) starts from its own values; Havok's
        // step is the game's, saved or not.
        if (lastSystem)
            systems.fetch_add(1, std::memory_order_relaxed);
        scaleChanged = physicsChanged = false;
    }
    original(system);
    lastSystem = system;
    ownScale = get<float>(system + scaleAt);
    ownPhysics = get<float>(system + physicsAt);
    const bool physicsScaled = get<uint8_t>(system + physicsFlagAt) & 1;
    const float factor = wanted.load(std::memory_order_relaxed);
    const bool slowing = factor < 1;
    const uintptr_t step = imageBase + physicsStepRva;
    float world = ownScale;
    if (slowing && ownScale > factor) {
        world = factor;
        put<float>(system + scaleAt, world);
        put<double>(imageBase + clockScaleRva, world);
        scaleChanged = true;
    }
    if (physicsScaled) {
        // The game set Havok's step from its own physics scale just now.
        stepSaved = false;
        if (slowing && ownPhysics > factor) {
            put<float>(system + physicsAt, factor);
            put<float>(step, factor * get<float>(step + 4));
            physicsChanged = true;
        }
    } else if (slowing) {
        // The game leaves Havok's step alone: Spidy scales the one it found.
        if (!stepSaved) {
            savedStep = get<float>(step);
            stepSaved = true;
        }
        put<float>(step, savedStep * factor);
    } else if (stepSaved) {
        put<float>(step, savedStep);
        stepSaved = false;
    }
    lastOwn.store(ownScale, std::memory_order_relaxed);
    lastOwnPhysics.store(ownPhysics, std::memory_order_relaxed);
    lastWorld.store(world, std::memory_order_relaxed);
    lastStep.store(get<float>(step), std::memory_order_relaxed);
    lastBase.store(get<float>(step + 4), std::memory_order_relaxed);
    lastFlag.store(physicsScaled, std::memory_order_relaxed);
    const bool changed = scaleChanged || physicsChanged || stepSaved;
    if (changed)
        slowedUpdates.fetch_add(1, std::memory_order_relaxed);
    restored.store(!changed, std::memory_order_release);
    updates.fetch_add(1, std::memory_order_release);
}
} // namespace

uint32_t game_time::install(uintptr_t base) {
    if (hooked)
        return 0;
    if (!created) {
        for (size_t i = 0; i < std::size(checks); ++i)
            if (!matches(base + checks[i].rva, checks[i].bytes, checks[i].count))
                return 9701 + static_cast<uint32_t>(i);
        imageBase = base;
        auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
            return 9800 + status;
        if ((status = MH_CreateHook(reinterpret_cast<void*>(base + updateRva), reinterpret_cast<void*>(&update),
                                    reinterpret_cast<void**>(&original))) != MH_OK)
            return 9820 + status;
        created = true;
    }
    wanted = 1;
    if (const auto status = MH_EnableHook(reinterpret_cast<void*>(imageBase + updateRva)); status != MH_OK)
        return 9840 + status;
    hooked = true;
    return 0;
}
void game_time::slow(float scale) {
    wanted.store(std::isfinite(scale) ? std::clamp(scale, .05f, 1.f) : 1.f, std::memory_order_relaxed);
}
void game_time::uninstall(unsigned waitMs) {
    if (!hooked)
        return;
    wanted = 1;
    // An update that started after the request puts the game's own values back.
    const auto seen = updates.load(std::memory_order_acquire);
    const auto deadline = GetTickCount64() + waitMs;
    while (GetTickCount64() < deadline &&
           !(updates.load(std::memory_order_acquire) > seen + 1 && restored.load(std::memory_order_acquire)))
        Sleep(2);
    MH_DisableHook(reinterpret_cast<void*>(imageBase + updateRva));
    hooked = false;
    // No update ran (a paused game): nothing else touches these meanwhile.
    if (!restored.load(std::memory_order_acquire) && lastSystem) {
        __try {
            putBack(lastSystem);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        restored = true;
    }
    lastSystem = 0;
}
Telemetry game_time::telemetry() {
    Telemetry t;
    t.installed = hooked;
    t.physicsScaled = lastFlag.load(std::memory_order_relaxed);
    t.wanted = wanted.load(std::memory_order_relaxed);
    t.own = lastOwn.load(std::memory_order_relaxed);
    t.ownPhysics = lastOwnPhysics.load(std::memory_order_relaxed);
    t.world = lastWorld.load(std::memory_order_relaxed);
    t.physicsStep = lastStep.load(std::memory_order_relaxed);
    t.physicsBase = lastBase.load(std::memory_order_relaxed);
    t.updates = updates.load(std::memory_order_relaxed);
    t.slowed = slowedUpdates.load(std::memory_order_relaxed);
    t.systems = systems.load(std::memory_order_relaxed);
    t.restored = restored.load(std::memory_order_relaxed);
    return t;
}

namespace {
// Headless probes (tools/probe_slow_motion.py) slow the game without a VR
// session: start hooks, set asks for a scale, sample reads it back with the
// game's clock, stop puts the game's time back.
struct ProbeSet {
    uint32_t magic = 0x454d4954, version = 1, bytes = sizeof(ProbeSet);
    float scale = 1;
};
static_assert(sizeof(ProbeSet) == 16);
struct ProbeSample {
    uint32_t magic = 0x454d4954, version = 1, bytes = sizeof(ProbeSample), installed{};
    uint64_t updates{}, slowed{}, systems{}, system{};
    float wanted{}, own{}, ownPhysics{}, world{}, physicsStep{}, physicsBase{};
    uint32_t physicsScaled{}, restored{};
    // The clock's double, and the last frame's game and real time (seconds).
    double clockScale{}, gameFrame{}, realFrame{};
};
static_assert(sizeof(ProbeSample) == 104);
bool copyIn(void* input, void* out, size_t bytes) {
    __try {
        if (reinterpret_cast<uintptr_t>(input) < 0x10000)
            return false;
        std::memcpy(out, input, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidyTimeStart(void*) {
    return install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyTimeSet(void* input) {
    ProbeSet s;
    if (!copyIn(input, &s, sizeof(s)))
        return 9601;
    const ProbeSet expected;
    if (s.magic != expected.magic || s.version != expected.version || s.bytes != expected.bytes ||
        !std::isfinite(s.scale) || s.scale < .05f || s.scale > 1)
        return 9602;
    if (!hooked)
        return 9603;
    slow(s.scale);
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyTimeSample(void* output) {
    ProbeSample s;
    const auto t = telemetry();
    s.installed = t.installed;
    s.updates = t.updates;
    s.slowed = t.slowed;
    s.systems = t.systems;
    s.system = lastSystem;
    s.wanted = t.wanted;
    s.own = t.own;
    s.ownPhysics = t.ownPhysics;
    s.world = t.world;
    s.physicsStep = t.physicsStep;
    s.physicsBase = t.physicsBase;
    s.physicsScaled = t.physicsScaled;
    s.restored = t.restored;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    s.clockScale = get<double>(base + clockScaleRva);
    s.gameFrame = get<double>(base + gameFrameRva);
    s.realFrame = get<double>(base + realFrameRva);
    __try {
        if (reinterpret_cast<uintptr_t>(output) < 0x10000)
            return 9601;
        std::memcpy(output, &s, sizeof(s));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9601;
    }
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyTimeStop(void*) {
    uninstall();
    return 0;
}
