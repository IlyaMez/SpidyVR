// Gives the game a larger per-frame render allocator, and reports how much of
// it frames use. See native_render_memory.hpp for why three scene views need
// one, and why it has to be in place before the game creates its own.
#include "spidy/native_render_memory.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <windows.h>
using namespace spidy;
using namespace spidy::native_render_memory;
extern "C" {
__declspec(dllexport) Data SpidyRenderMemoryData;
}
namespace {
using Create = uint8_t (*)(void*);
using Rollover = void (*)(void*);
constexpr uintptr_t allocatorRva = 0x7938880, createRva = 0x1872d90, rolloverRva = 0x1872b90;
Create originalCreate{};
Rollover originalRollover{};
void *createHook{}, *rolloverHook{};
uintptr_t base{};
bool hooked{};
std::atomic<bool> enabled{};
uint32_t wantedBytes{};
std::atomic<uint64_t> installed{}; // the ring put in place of the game's; it stays for the game's life
uint32_t previousFrame{};          // under `telemetry`
SRWLOCK lifecycle = SRWLOCK_INIT, telemetry = SRWLOCK_INIT;
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
bool entry(uintptr_t rva, const unsigned char* expected, size_t n) {
    unsigned char b[32]{};
    return n <= sizeof(b) && read(base + rva, b, n) && !std::memcmp(b, expected, n);
}
template <class F> void publish(F&& change) {
    AcquireSRWLockExclusive(&telemetry);
    InterlockedIncrement64(&SpidyRenderMemoryData.sequence);
    change(SpidyRenderMemoryData);
    InterlockedIncrement64(&SpidyRenderMemoryData.sequence);
    ReleaseSRWLockExclusive(&telemetry);
}
// 1872d90 reserves and commits the ring and resets the allocator (called from
// the renderer's start, 188a19c). No frame exists yet, so nothing refers to
// the ring it made: that one is released and a larger one takes its place.
uint8_t create(void* allocator) {
    const uint8_t created = originalCreate(allocator);
    const auto address = reinterpret_cast<uintptr_t>(allocator);
    if (!enabled || address != base + allocatorRva || !created)
        return created;
    auto* live = reinterpret_cast<Fields*>(address + 0x10);
    const Fields made = *live;
    uint32_t status = 2, error{}, ringKb = made.committed >> 10;
    if (!untouched(made)) {
        error = 7002;
    } else if (wantedBytes > made.committed) {
        if (const auto ring = VirtualAlloc(nullptr, wantedBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
            *live = replaced(made, reinterpret_cast<uint64_t>(ring), wantedBytes);
            VirtualFree(reinterpret_cast<void*>(made.ring), 0, MEM_RELEASE);
            installed = reinterpret_cast<uint64_t>(ring);
            status = 3;
            ringKb = wantedBytes >> 10;
        } else {
            error = 7003;
        }
    }
    publish([&](Data& d) {
        d.status = status;
        d.gameRingKb = made.committed >> 10;
        d.ringKb = ringKb;
        d.error = error;
    });
    return created;
}
// 1872b90, called once at the end of each frame (187e623): accounts for the
// frame that ended and lays out the free regions of the next.
void rollover(void* allocator) {
    const auto address = reinterpret_cast<uintptr_t>(allocator);
    if (!enabled || address != base + allocatorRva) {
        originalRollover(allocator);
        return;
    }
    // The game clears the flag of the frame that ended in this call.
    const bool overflowed = *reinterpret_cast<const uint8_t*>(address + 0x41) != 0;
    originalRollover(allocator);
    const Fields f = *reinterpret_cast<const Fields*>(address + 0x10);
    const uint32_t frame = f.lastFrame;
    publish([&](Data& d) {
        ++d.frames;
        d.overflowFrames += overflowed;
        d.ringKb = f.committed >> 10;
        d.lastFrameKb = frame >> 10;
        // A frame with a request that did not fit counts the refused bytes too.
        if (!overflowed) {
            d.worstFrameKb = std::max(d.worstFrameKb, frame >> 10);
            d.worstPairKb = std::max(d.worstPairKb, (frame >> 10) + (previousFrame >> 10));
        }
        previousFrame = overflowed ? 0 : frame;
    });
}
} // namespace
// `megabytes`: the ring to give the game, 0 to keep the game's own and only
// report. Returns 0 also when the game has created its ring already; the
// status then says so.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyRenderMemoryStart(void* megabytes) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        if (enabled)
            break;
        const auto wanted = reinterpret_cast<uintptr_t>(megabytes);
        base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // The allocator's creation and rollover, and the only call of each.
        const unsigned char createBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                             0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
                                             0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30};
        const unsigned char createCall[] = {0x48, 0x8d, 0x0d, 0xe4, 0xe6, 0x0a,
                                            0x06, 0xe8, 0xef, 0x8b, 0xfe, 0xff};
        const unsigned char rolloverBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                                               0xec, 0x30, 0x83, 0x79, 0x28, 0x00, 0x48, 0x8b,
                                               0xd9, 0x8b, 0x41, 0x1c, 0xc6, 0x41, 0x42, 0x00};
        const unsigned char rolloverCall[] = {0x48, 0x8d, 0x0d, 0x56, 0xa2, 0x0b,
                                              0x06, 0xe8, 0x61, 0x45, 0xff, 0xff};
        Fields f{};
        if (!GetModuleHandleW(L"Spider-Man.exe") || wanted > 1024 || (wanted && wanted < 128) ||
            !entry(createRva, createBytes, sizeof(createBytes)) ||
            !entry(0x188a195, createCall, sizeof(createCall)) ||
            !entry(rolloverRva, rolloverBytes, sizeof(rolloverBytes)) ||
            !entry(0x187e623, rolloverCall, sizeof(rolloverCall)) ||
            !read(base + allocatorRva + 0x10, &f, sizeof(f)) || (f.ring && !usable(f))) {
            result = 7001;
            break;
        }
        wantedBytes = static_cast<uint32_t>(wanted) << 20;
        if (!hooked) {
            auto s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
                result = 7100 + s;
                break;
            }
            createHook = reinterpret_cast<void*>(base + createRva);
            rolloverHook = reinterpret_cast<void*>(base + rolloverRva);
            s = MH_CreateHook(createHook, reinterpret_cast<void*>(create),
                              reinterpret_cast<void**>(&originalCreate));
            if (s == MH_OK)
                s = MH_CreateHook(rolloverHook, reinterpret_cast<void*>(rollover),
                                  reinterpret_cast<void**>(&originalRollover));
            if (s != MH_OK) {
                MH_RemoveHook(createHook);
                result = 7200 + s;
                break;
            }
            hooked = true;
        }
        // Each start counts its own frames. A second session on the same game
        // finds the ring of the first.
        publish([&](Data& d) {
            d.status = ringStatus(f, installed);
            d.ringKb = f.committed >> 10;
            if (d.status != 3)
                d.gameRingKb = d.ringKb;
            d.frames = d.overflowFrames = 0;
            d.lastFrameKb = d.worstFrameKb = d.worstPairKb = 0;
            d.error = 0;
            previousFrame = 0;
        });
        enabled = true;
        auto s = MH_EnableHook(createHook);
        if (s == MH_OK)
            s = MH_EnableHook(rolloverHook);
        if (s != MH_OK) {
            enabled = false;
            MH_DisableHook(createHook);
            result = 7300 + s;
        }
    } while (false);
    if (result)
        publish([&](Data& d) { d.error = result; });
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
// Ends the reporting. The game keeps the ring it has.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyRenderMemoryStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    enabled = false;
    if (hooked)
        for (auto hook : {createHook, rolloverHook})
            if (const auto s = MH_DisableHook(hook); s != MH_OK && s != MH_ERROR_DISABLED)
                result = 7400 + s;
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
