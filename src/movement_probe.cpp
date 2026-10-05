// Bounded observation of the native MoverStandard displacement request.
// An optional command adds one small delta before the existing collision path.
#include "spidy/math.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
struct Config {
    uint32_t magic, version, bytes, pid;
    uint64_t base, record, mover;
    uint32_t durationMs, reserved;
};
struct Command {
    uint32_t magic, version, bytes, leaseMs;
    uint64_t serial;
    float delta[3];
    uint32_t reserved;
};
struct Sample {
    uint64_t qpc, caller, serial;
    float position[3], before[3], after[3], forward[3];
    uint32_t thread, flags;
    float requested[3];
    uint32_t moverFlags, groundFlags, ready;
    uint64_t result;
};
struct Data {
    uint32_t magic = 0x534d5044, version = 2, bytes = sizeof(Data), status{};
    int64_t sequence{};
    uint64_t calls{}, matched{}, applied{}, lastSerial{};
    uint32_t count{}, error{};
    Sample samples[512]{};
};
static_assert(sizeof(Config) == 48 && sizeof(Command) == 40 && sizeof(Sample) == 112 &&
              sizeof(Data) == 57408);
extern "C" {
__declspec(dllexport) Data SpidyMovementData;
}
namespace {
using Move = uintptr_t (*)(void*, const float*, const float*, uint8_t);
Move original{};
void* hook{};
uintptr_t base{}, record{}, mover{};
uint32_t moverHandle{};
std::atomic<bool> enabled{};
std::atomic<unsigned> active{};
std::atomic<uint64_t> calls{}, matched{}, applied{};
uint64_t deadline{}, commandDeadline{};
std::atomic<uint64_t> consumed{};
Command command{};
SRWLOCK lifecycle = SRWLOCK_INIT, control = SRWLOCK_INIT, telemetry = SRWLOCK_INIT;
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
    read(address, &out, 8);
    return out;
}
bool live() {
    const auto table = pointer(base + 0x7a44320);
    const auto index = moverHandle & 0xfffff, generation = moverHandle >> 20;
    uint32_t count{}, current{}, selfHandle{};
    if (!read(base + 0x7a44340, &count, 4) || index >= count || count > 0x100000)
        return false;
    return generation && read(table + index * 16 + 8, &current, 4) && current == generation &&
           pointer(table + index * 16) == mover && pointer(mover) == base + 0x4f70168 &&
           read(mover + 0x14, &selfHandle, 4) && selfHandle == moverHandle && pointer(mover + 8) == record;
}
uintptr_t move(void* self, const float* delta, const float* forward, uint8_t flags) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    struct Guard {
        Guard() {
            ++active;
        }
        ~Guard() {
            --active;
        }
    } guard;
    if (!enabled || GetTickCount64() >= deadline)
        return original(self, delta, forward, flags);
    ++calls;
    Vec3 before{}, direction{}, position{};
    const auto actor = pointer(record);
    if (reinterpret_cast<uintptr_t>(self) != mover || !live() ||
        !read(reinterpret_cast<uintptr_t>(delta), &before, 12) ||
        !read(reinterpret_cast<uintptr_t>(forward), &direction, 12) || !read(actor + 0x30, &position, 12) ||
        !finite(before) || !finite(direction) || !finite(position))
        return original(self, delta, forward, flags);
    ++matched;
    uint32_t moverFlags{};
    if (!read(mover + 0x750, &moverFlags, 4))
        return original(self, delta, forward, flags);
    Vec3 after = before;
    uint64_t serial{};
    AcquireSRWLockExclusive(&control);
    if (enabled && GetTickCount64() < deadline && command.serial > consumed &&
        GetTickCount64() < commandDeadline && !(moverFlags & 0x80000000u) && length(before) < .25f) {
        const Vec3 extra{command.delta[0], command.delta[1], command.delta[2]};
        after += extra;
        serial = consumed = command.serial;
        ++applied;
    }
    ReleaseSRWLockExclusive(&control);
    const auto result = original(self, serial ? &after.x : delta, forward, flags);
    Sample sample{};
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    sample.qpc = qpc.QuadPart;
    sample.caller = caller;
    sample.serial = serial;
    std::memcpy(sample.position, &position, 12);
    std::memcpy(sample.before, &before, 12);
    std::memcpy(sample.after, &after, 12);
    std::memcpy(sample.forward, &direction, 12);
    sample.thread = GetCurrentThreadId();
    sample.flags = flags;
    sample.moverFlags = moverFlags;
    sample.result = result;
    read(mover + 0x11c, sample.requested, 12);
    read(mover + 0x144, &sample.groundFlags, 4);
    uint8_t ready{};
    read(mover + 0x140, &ready, 1);
    sample.ready = ready;
    AcquireSRWLockExclusive(&telemetry);
    auto& d = SpidyMovementData;
    InterlockedIncrement64(&d.sequence);
    d.calls = calls;
    d.matched = matched;
    d.applied = applied;
    d.lastSerial = consumed;
    if (d.count < 512)
        d.samples[d.count++] = sample;
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    return result;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    Config c{};
    do {
        if (created) {
            result = 1000;
            break;
        }
        base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x534d5043 ||
            c.version != 1 || c.bytes != sizeof(c) || c.pid != GetCurrentProcessId() || c.base != base ||
            !GetModuleHandleW(L"Spider-Man.exe") || c.durationMs < 1000 || c.durationMs > 10000) {
            result = 1001;
            break;
        }
        record = c.record;
        mover = c.mover;
        if (!read(mover + 0x14, &moverHandle, 4) || !live()) {
            result = 1002;
            break;
        }
        const uint8_t signature[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30};
        uint8_t actual[sizeof(signature)]{};
        if (!read(base + 0x1fc2e10, actual, sizeof(actual)) ||
            std::memcmp(actual, signature, sizeof(actual))) {
            result = 1003;
            break;
        }
        auto s = MH_Initialize();
        if (s != MH_OK) {
            result = 1100 + s;
            break;
        }
        hook = reinterpret_cast<void*>(base + 0x1fc2e10);
        s = MH_CreateHook(hook, reinterpret_cast<void*>(move), reinterpret_cast<void**>(&original));
        if (s != MH_OK) {
            result = 1200 + s;
            break;
        }
        created = true;
        deadline = GetTickCount64() + c.durationMs;
        enabled = true;
        s = MH_EnableHook(hook);
        if (s != MH_OK) {
            enabled = false;
            result = 1300 + s;
        }
    } while (false);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySubmitMovement(void* input) {
    Command c{};
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x534d4344 || c.version != 1 ||
        c.bytes != sizeof(c) || !c.serial || c.leaseMs < 1 || c.leaseMs > 250 ||
        !finite(Vec3{c.delta[0], c.delta[1], c.delta[2]}) ||
        length(Vec3{c.delta[0], c.delta[1], c.delta[2]}) > .05f)
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
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    enabled = false;
    DWORD result{};
    if (created) {
        const auto s = MH_DisableHook(hook);
        if (s != MH_OK && s != MH_ERROR_DISABLED)
            result = 1400 + s;
    }
    const auto until = GetTickCount64() + 2000;
    while (active && GetTickCount64() < until)
        Sleep(1);
    if (active)
        result = 1501;
    AcquireSRWLockExclusive(&telemetry);
    auto& d = SpidyMovementData;
    InterlockedIncrement64(&d.sequence);
    d.calls = calls;
    d.matched = matched;
    d.applied = applied;
    d.error = result;
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&telemetry);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
