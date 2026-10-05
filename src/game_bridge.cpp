// Game-specific integration experiments. All functions are signature checked;
// controls require a short renewable lease, and camera writes target only the
// camera whose target record matches the independently discovered local player.
#include "spidy/game_bridge_protocol.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <windows.h>

using namespace spidy::bridge;
extern "C" {
__declspec(dllexport) Data SpidyBridgeData;
}

namespace {
uintptr_t base{}, record{}, hero{};
SRWLOCK lifecycle = SRWLOCK_INIT, controls = SRWLOCK_INIT, telemetry = SRWLOCK_INIT;
Control control;
uint64_t deadline{};
std::atomic<bool> enabled{false};
std::atomic<uint64_t> inputFrames{}, digitalQueries{}, analogQueries{}, inputServed{}, expired{};
std::atomic<uint32_t> keyStates[15]{};
std::atomic<uint32_t> queriedKeys[8]{};
std::atomic<uintptr_t> inputSelf{};
using Commit = void (*)(void*, void*);
using ClearInput = void (*)(void*, uint64_t);
using Digital = uint32_t (*)(void*, uint32_t, uint32_t);
using Analog = float (*)(void*, uint32_t, uint32_t);
using SetPosition = void (*)(void*, const float*);
Commit originalCommit{};
ClearInput originalClear{};
Digital originalDigital{};
Analog originalAnalog{};
std::array<void*, 4> hooks{};
bool created{};

bool read(uintptr_t address, void* out, size_t bytes) {
    __try {
        if (address < 0x10000)
            return false;
        std::memcpy(out, reinterpret_cast<void*>(address), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
uintptr_t pointer(uintptr_t address) {
    uintptr_t out{};
    read(address, &out, sizeof(out));
    return out;
}
bool transform(uintptr_t address, float* out) {
    if (!read(address, out, 64))
        return false;
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(out[i]) || std::abs(out[i]) > 1e6f)
            return false;
    for (int row = 0; row < 3; ++row) {
        float norm{};
        for (int col = 0; col < 3; ++col)
            norm += out[row * 4 + col] * out[row * 4 + col];
        if (std::abs(norm - 1) > .05f)
            return false;
    }
    return true;
}
Control current() {
    AcquireSRWLockShared(&controls);
    Control out = control;
    if (!enabled.load() || GetTickCount64() >= deadline)
        out.modes = out.keys = 0;
    ReleaseSRWLockShared(&controls);
    return out;
}
bool livePlayer() {
    return pointer(hero) == base + 0x38a93c8 && pointer(hero + 8) == record && pointer(record) != 0;
}
void commit(void* self, void* target) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    originalCommit(self, target);
    const auto mover = reinterpret_cast<uintptr_t>(self);
    const auto targetAddress = reinterpret_cast<uintptr_t>(target);
    const auto vt = pointer(mover);
    const auto camera = pointer(pointer(mover + 8));
    const auto player = pointer(record);
    float before[16]{}, after[16]{}, body[16]{};
    const bool valid = livePlayer() && pointer(targetAddress + 8) == record &&
                       (vt == base + 0x3871fd8 || vt == base + 0x38720d0) && camera != player &&
                       transform(camera, before) && transform(player, body);
    const auto command = current();
    bool wrote = false;
    if (valid && (command.modes & Mode::cameraOffset)) {
        float position[3]{};
        for (int i = 0; i < 3; ++i)
            position[i] = before[12 + i] + before[i] * command.offset[0] + before[4 + i] * command.offset[1] +
                          before[8 + i] * command.offset[2];
        // Native setter maintains actor dirty flags and change stamp.
        reinterpret_cast<SetPosition>(base + 0x191c590)(reinterpret_cast<void*>(camera), position);
        wrote = true;
    }
    if (valid)
        transform(camera, after);
    if (TryAcquireSRWLockExclusive(&telemetry)) {
        auto& d = SpidyBridgeData;
        InterlockedIncrement64(&d.sequence);
        ++d.cameraCalls;
        d.matched += valid;
        d.cameraWrites += wrote;
        d.inputFrames = inputFrames.load();
        d.digitalQueries = digitalQueries.load();
        d.analogQueries = analogQueries.load();
        d.inputServed = inputServed.load();
        d.inputSelf = inputSelf.load();
        for (int i = 0; i < 8; ++i)
            d.queriedKeys[i] = queriedKeys[i].load();
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        d.qpc = qpc.QuadPart;
        d.thread = GetCurrentThreadId();
        d.mover = mover;
        d.target = targetAddress;
        d.caller = caller;
        d.cameraTransform = camera;
        d.playerTransform = player;
        d.serial = command.serial;
        d.expired = expired.load();
        std::memcpy(d.before, before, 64);
        std::memcpy(d.after, after, 64);
        std::memcpy(d.player, body, 64);
        InterlockedIncrement64(&d.sequence);
        ReleaseSRWLockExclusive(&telemetry);
    }
}
void clearInput(void* self, uint64_t keepHeld) {
    originalClear(self, keepHeld);
    inputSelf = reinterpret_cast<uintptr_t>(self);
    ++inputFrames;
    const auto command = current();
    const uint32_t desired = (command.modes & Mode::input) && (keepHeld & 0xff) && livePlayer() ? command.keys : 0;
    // Native code clears transition bits once per poll. Our transitions use the
    // same boundary, so multiple bindings see one coherent press/release frame.
    for (unsigned i = 0; i < std::size(keyCodes); ++i) {
        const bool previous = (keyStates[i].load() & 4) != 0;
        const bool next = (desired & (1u << i)) != 0;
        keyStates[i] = next ? (previous ? 4u : 6u) : (previous ? 9u : 0u);
    }
}
uint32_t digital(void* self, uint32_t code, uint32_t user) {
    const auto stock = originalDigital(self, code, user);
    ++digitalQueries;
    if (code < 256)
        queriedKeys[code / 32].fetch_or(1u << (code % 32));
    if (user != 0 || !enabled.load())
        return stock;
    for (unsigned i = 0; i < std::size(keyCodes); ++i)
        if (code == keyCodes[i]) {
            const auto state = keyStates[i].load();
            if (state) {
                ++inputServed;
                return (stock & 4) ? stock : state;
            }
        }
    return stock;
}
float analog(void* self, uint32_t code, uint32_t user) {
    const float stock = originalAnalog(self, code, user);
    ++analogQueries;
    if (user != 0 || !enabled.load())
        return stock;
    for (unsigned i = 0; i < std::size(keyCodes); ++i)
        if (code == keyCodes[i] && (keyStates[i].load() & 4)) {
            ++inputServed;
            return 1.f;
        }
    return stock;
}
bool matches(uintptr_t rva, const unsigned char* bytes, size_t size) {
    unsigned char actual[32]{};
    return size <= sizeof(actual) && read(base + rva, actual, size) && !std::memcmp(actual, bytes, size);
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI SpidySubmit(void* value) {
    Control next{};
    if (!read(reinterpret_cast<uintptr_t>(value), &next, sizeof(next)) || next.magic != controlMagic ||
        next.protocol != version || next.bytes != sizeof(next) || (next.modes & ~3u) || next.leaseMs > 500 ||
        (next.keys >> std::size(keyCodes)))
        return 2001;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(next.offset[i]) || std::abs(next.offset[i]) > 2)
            return 2002;
    AcquireSRWLockExclusive(&controls);
    if (next.serial <= control.serial) {
        ReleaseSRWLockExclusive(&controls);
        return 2003;
    }
    control = next;
    deadline = GetTickCount64() + next.leaseMs;
    ReleaseSRWLockExclusive(&controls);
    return 0;
}

// The in-process VR thread needs a coherent gameplay sample. External probes
// use the seqlock; this local API uses the writer's lock and avoids torn poses.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyBridgeSample(void* output) {
    if(!output)return 2101;
    AcquireSRWLockShared(&telemetry);
    std::memcpy(output,&SpidyBridgeData,sizeof(SpidyBridgeData));
    ReleaseSRWLockShared(&telemetry);
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* value) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result = 0;
    do {
        if (enabled.load())
            break;
        Config config{};
        base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!GetModuleHandleW(L"Spider-Man.exe") ||
            !read(reinterpret_cast<uintptr_t>(value), &config, sizeof(config)) ||
            config.magic != configMagic || config.protocol != version || config.bytes != sizeof(config) ||
            config.pid != GetCurrentProcessId() || config.imageBase != base) {
            result = 1001;
            break;
        }
        hero = config.hero;
        record = config.actorRecord;
        if (!livePlayer()) {
            result = 1002;
            break;
        }
        const unsigned char commitBytes[] = {0x48, 0x83, 0xec, 0x38, 0xf2, 0x0f,
                                             0x10, 0x81, 0x3c, 0x03, 0,    0};
        const unsigned char queryBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                                            0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};
        const unsigned char clearBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18};
        const unsigned char setterBytes[] = {0x0f, 0x10, 0x51, 0x30, 0x4c, 0x8b, 0xc1, 0x8b, 0x02};
        if (!matches(0x1e1d600, commitBytes, sizeof(commitBytes)) ||
            !matches(0x1cdfa70, queryBytes, sizeof(queryBytes)) ||
            !matches(0x1cdf840, queryBytes, sizeof(queryBytes)) ||
            !matches(0x1ce2f40, clearBytes, sizeof(clearBytes)) ||
            !matches(0x191c590, setterBytes, sizeof(setterBytes))) {
            result = 1003;
            break;
        }
        if (!created) {
            auto status = MH_Initialize();
            if (status != MH_OK) {
                result = 1100 + status;
                break;
            }
            const uintptr_t rvas[] = {0x1e1d600, 0x1ce2f40, 0x1cdfa70, 0x1cdf840};
            void* detours[] = {reinterpret_cast<void*>(commit), reinterpret_cast<void*>(clearInput),
                               reinterpret_cast<void*>(digital), reinterpret_cast<void*>(analog)};
            void** originals[] = {
                reinterpret_cast<void**>(&originalCommit), reinterpret_cast<void**>(&originalClear),
                reinterpret_cast<void**>(&originalDigital), reinterpret_cast<void**>(&originalAnalog)};
            for (unsigned i = 0; i < hooks.size(); ++i) {
                hooks[i] = reinterpret_cast<void*>(base + rvas[i]);
                status = MH_CreateHook(hooks[i], detours[i], originals[i]);
                if (status != MH_OK) {
                    result = 1200 + status;
                    break;
                }
            }
            if (result) {
                MH_Uninitialize();
                hooks = {};
                break;
            }
            created = true;
        }
        enabled = true;
        for (auto hook : hooks) {
            auto status = MH_EnableHook(hook);
            if (status != MH_OK) {
                result = 1300 + status;
                break;
            }
        }
        if (result) {
            enabled = false;
            for (auto hook : hooks)
                MH_DisableHook(hook);
        }
    } while (false);
    SpidyBridgeData.state = result ? 3 : 1;
    SpidyBridgeData.error = result;
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    enabled = false;
    for (auto& state : keyStates)
        state = 0;
    DWORD result{};
    if (created)
        for (auto hook : hooks) {
            const auto status = MH_DisableHook(hook);
            if (status != MH_OK && status != MH_ERROR_DISABLED)
                result = 1400 + status;
        }
    SpidyBridgeData.state = result ? 3 : 2;
    SpidyBridgeData.error = result;
    ReleaseSRWLockExclusive(&lifecycle);
    return result; // Resident until exit: never unmap an in-flight trampoline.
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
