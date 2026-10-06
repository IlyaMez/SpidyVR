// The game's XInput reads, answered with the VR controllers for controller 0.
#include "spidy/game_pad.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <windows.h>
#include <Xinput.h>
using namespace spidy;
using namespace spidy::game_pad;

namespace {
using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using GetCapabilities = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
// The DLLs the game tries, in its own order.
constexpr const wchar_t* modules[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
constexpr unsigned moduleCount = 3;
GetState originalStates[moduleCount]{};
GetCapabilities originalCapabilities[moduleCount]{};
void* targets[moduleCount * 2]{};
unsigned targetCount{};
SRWLOCK lock = SRWLOCK_INIT;
State submitted{}, served{};
uint64_t deadline{};
bool connected{};
uint32_t packet{};
std::atomic<uint64_t> reads{}, active{};
std::atomic<bool> hooked{};

int16_t larger(int16_t first, int16_t second) {
    return std::abs(static_cast<int>(first)) >= std::abs(static_cast<int>(second)) ? first : second;
}
DWORD serve(GetState original, DWORD user, XINPUT_STATE* out) {
    const DWORD real = original(user, out);
    if (user != 0 || !out)
        return real;
    State mine;
    uint32_t number{};
    AcquireSRWLockExclusive(&lock);
    const bool serving = connected;
    if (serving) {
        mine = GetTickCount64() < deadline ? submitted : State{};
        // A new packet number tells the game the state changed.
        if (!(mine == served)) {
            served = mine;
            ++packet;
        }
        number = packet;
    }
    ReleaseSRWLockExclusive(&lock);
    if (!serving)
        return real;
    ++reads;
    if (!(mine == State{}))
        ++active;
    if (real != ERROR_SUCCESS)
        std::memset(out, 0, sizeof(*out));
    auto& g = out->Gamepad;
    g.wButtons |= mine.buttons;
    g.bLeftTrigger = std::max(g.bLeftTrigger, mine.leftTrigger);
    g.bRightTrigger = std::max(g.bRightTrigger, mine.rightTrigger);
    g.sThumbLX = larger(g.sThumbLX, mine.thumbLX);
    g.sThumbLY = larger(g.sThumbLY, mine.thumbLY);
    g.sThumbRX = larger(g.sThumbRX, mine.thumbRX);
    g.sThumbRY = larger(g.sThumbRY, mine.thumbRY);
    out->dwPacketNumber = (real == ERROR_SUCCESS ? out->dwPacketNumber : 0) + number;
    return ERROR_SUCCESS;
}
DWORD describe(GetCapabilities original, DWORD user, DWORD flags, XINPUT_CAPABILITIES* out) {
    const DWORD real = original(user, flags, out);
    AcquireSRWLockShared(&lock);
    const bool serving = connected;
    ReleaseSRWLockShared(&lock);
    if (user != 0 || real == ERROR_SUCCESS || !out || !serving)
        return real;
    std::memset(out, 0, sizeof(*out));
    out->Type = XINPUT_DEVTYPE_GAMEPAD;
    out->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    out->Gamepad.wButtons = 0xf3ff;
    out->Gamepad.bLeftTrigger = out->Gamepad.bRightTrigger = 0xff;
    out->Gamepad.sThumbLX = out->Gamepad.sThumbLY = out->Gamepad.sThumbRX = out->Gamepad.sThumbRY =
        static_cast<SHORT>(0xffc0);
    return ERROR_SUCCESS;
}
template <unsigned I> DWORD WINAPI stateHook(DWORD user, XINPUT_STATE* out) {
    return serve(originalStates[I], user, out);
}
template <unsigned I> DWORD WINAPI capabilitiesHook(DWORD user, DWORD flags, XINPUT_CAPABILITIES* out) {
    return describe(originalCapabilities[I], user, flags, out);
}
constexpr GetState stateHooks[] = {stateHook<0>, stateHook<1>, stateHook<2>};
constexpr GetCapabilities capabilitiesHooks[] = {capabilitiesHook<0>, capabilitiesHook<1>, capabilitiesHook<2>};
} // namespace

uint32_t game_pad::install() {
    if (hooked)
        return 0;
    auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        return 8100 + status;
    uint32_t result = 8001;
    for (unsigned i = 0; i < moduleCount; ++i) {
        const auto module = GetModuleHandleW(modules[i]);
        if (!module || originalStates[i])
            continue;
        void* state = reinterpret_cast<void*>(GetProcAddress(module, "XInputGetState"));
        void* capabilities = reinterpret_cast<void*>(GetProcAddress(module, "XInputGetCapabilities"));
        if (!state || !capabilities)
            continue;
        if ((status = MH_CreateHook(state, reinterpret_cast<void*>(stateHooks[i]),
                                    reinterpret_cast<void**>(&originalStates[i]))) != MH_OK)
            return 8200 + status;
        if ((status = MH_CreateHook(capabilities, reinterpret_cast<void*>(capabilitiesHooks[i]),
                                    reinterpret_cast<void**>(&originalCapabilities[i]))) != MH_OK) {
            MH_RemoveHook(state);
            originalStates[i] = nullptr;
            return 8200 + status;
        }
        targets[targetCount++] = state;
        targets[targetCount++] = capabilities;
        result = 0;
    }
    if (result)
        return result;
    for (unsigned i = 0; i < targetCount; ++i)
        if ((status = MH_EnableHook(targets[i])) != MH_OK)
            return 8300 + status;
    hooked = true;
    return 0;
}
void game_pad::submit(const State& state, uint32_t leaseMs) {
    AcquireSRWLockExclusive(&lock);
    submitted = state;
    deadline = GetTickCount64() + leaseMs;
    connected = true;
    ReleaseSRWLockExclusive(&lock);
}
void game_pad::uninstall() {
    for (unsigned i = 0; i < targetCount; ++i)
        MH_DisableHook(targets[i]);
    AcquireSRWLockExclusive(&lock);
    connected = false;
    ReleaseSRWLockExclusive(&lock);
    hooked = false;
}
Telemetry game_pad::telemetry() {
    Telemetry t;
    t.reads = reads;
    t.active = active;
    t.installed = hooked;
    AcquireSRWLockShared(&lock);
    t.buttons = GetTickCount64() < deadline ? submitted.buttons : 0;
    ReleaseSRWLockShared(&lock);
    return t;
}

// Headless probes play the menus with this. Input: State, then lease in ms.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyPadSubmit(void* input) {
    struct Command {
        State state;
        uint32_t leaseMs;
    } c{};
    __try {
        if (reinterpret_cast<uintptr_t>(input) < 0x10000)
            return 8501;
        std::memcpy(&c, input, sizeof(c));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 8501;
    }
    if (c.leaseMs > 2000)
        return 8502;
    if (const auto code = install())
        return code;
    submit(c.state, c.leaseMs);
    return 0;
}
// Output: Telemetry.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyPadSample(void* output) {
    const auto t = telemetry();
    __try {
        if (reinterpret_cast<uintptr_t>(output) < 0x10000)
            return 8501;
        std::memcpy(output, &t, sizeof(t));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 8501;
    }
    return 0;
}
