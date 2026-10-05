// Diagnostic camera hook for the explicitly supported executable. The observer
// calls the original function once, then copies transforms. It never writes a
// camera/player transform, submits input, or changes rendering.
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <windows.h>

namespace {
constexpr uintptr_t cameraUpdateRva = 0x897d30;
constexpr uintptr_t cameraVtableRva = 0x38b1dd0;
constexpr unsigned char signature[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x44,
                                       0x88, 0x48, 0x20, 0x44, 0x88, 0x40, 0x18, 0x55};
// Disassembly: RCX=this, XMM1=dt, R8B/R9B=flags. Preserve the full
// integer argument/return registers while the two flag meanings remain unknown.
using Update = uint64_t (*)(void*, float, uint64_t, uint64_t);
Update original = nullptr;
void* target = nullptr;
std::atomic<uintptr_t> imageBase{0};
std::atomic<uintptr_t> observedManager{0};
std::atomic_flag writer = ATOMIC_FLAG_INIT;
SRWLOCK lifecycle = SRWLOCK_INIT;
bool created = false;

bool readBytes(uintptr_t address, void* output, size_t size) {
    __try {
        if (address < 0x10000)
            return false;
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool validTransform(const float* value) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(value[i]) || std::abs(value[i]) > 1e7f)
            return false;
    for (int i = 0; i < 3; ++i) {
        float norm = 0;
        for (int j = 0; j < 3; ++j)
            norm += value[i * 4 + j] * value[i * 4 + j];
        if (std::abs(norm - 1) > .15f)
            return false;
        for (int k = i + 1; k < 3; ++k) {
            float dot = 0;
            for (int j = 0; j < 3; ++j)
                dot += value[i * 4 + j] * value[k * 4 + j];
            if (std::abs(dot) > .1f)
                return false;
        }
    }
    return true;
}
} // namespace

// Fixed layout read with ReadProcessMemory by tools/observe_game.py. That reader
// brackets the payload read with sequence reads, rejecting an overlapping write.
// No pointers from a previous game run are accepted by the loader.
struct alignas(8) ObserverData {
    uint32_t magic = 0x5350594f;
    uint32_t version = 2;
    uint32_t bytes = 256;
    uint32_t pid = 0;
    int64_t frequency = 0;
    volatile LONG64 sequence = 0;
    uint64_t calls = 0;
    uint64_t rejected = 0;
    uint64_t overlapping = 0;
    uint64_t qpc = 0;
    uint64_t manager = 0;
    uint64_t record = 0;
    uint64_t transform = 0;
    uint32_t thread = 0;
    uint32_t state = 0; // 0=not started, 1=observing, 2=stopped, 3=failed
    float dt = 0;
    uint32_t flags = 0;
    uint64_t updateObject = 0;
    uint64_t updateVtable = 0;
    float camera[16] = {};
    float player[16] = {};
    uint32_t error = 0;
    uint32_t validation = 0;
};
static_assert(sizeof(ObserverData) == 256);
static_assert(offsetof(ObserverData, camera) == 120);

struct ObserverConfig {
    uint32_t magic, version, bytes, pid;
    uint64_t imageBase, manager;
};
static_assert(sizeof(ObserverConfig) == 32);

extern "C" {
__declspec(dllexport) ObserverData SpidyObserverData;
}

namespace {
std::atomic<uint64_t> overlapCount{0};

uint64_t cameraUpdate(void* self, float dt, uint64_t flag0, uint64_t flag1) {
    const uint64_t result = original(self, dt, flag0, flag1);
    if (writer.test_and_set(std::memory_order_acquire)) {
        overlapCount.fetch_add(1, std::memory_order_relaxed);
        return result;
    }
    const uintptr_t manager = observedManager;
    uintptr_t table = 0, record = 0, transform = 0;
    float camera[16]{}, player[16]{};
    uint32_t validation = 0;
    if (!std::isfinite(dt) || dt < 0 || dt > 1)
        validation |= 1;
    if (!readBytes(manager, &table, 8) || table != imageBase + cameraVtableRva)
        validation |= 2;
    if (!readBytes(manager + 8, &record, 8) || !readBytes(record, &transform, 8))
        validation |= 4;
    if (!readBytes(manager + 0x744, camera, sizeof(camera)) || !validTransform(camera))
        validation |= 8;
    if (!readBytes(transform, player, sizeof(player)) || !validTransform(player))
        validation |= 16;
    const bool valid = validation == 0;
    auto& data = SpidyObserverData;
    InterlockedIncrement64(&data.sequence);
    ++data.calls;
    data.overlapping = overlapCount.load(std::memory_order_relaxed);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    data.qpc = now.QuadPart;
    data.manager = manager;
    data.record = record;
    data.transform = transform;
    data.thread = GetCurrentThreadId();
    data.dt = dt;
    data.flags = static_cast<uint32_t>((flag0 & 0xff) | ((flag1 & 0xff) << 8));
    data.updateObject = reinterpret_cast<uintptr_t>(self);
    data.updateVtable = 0;
    readBytes(data.updateObject, &data.updateVtable, 8);
    data.validation = validation;
    if (valid) {
        std::memcpy(data.camera, camera, sizeof(camera));
        std::memcpy(data.player, player, sizeof(player));
    } else {
        ++data.rejected;
        std::memset(data.camera, 0, sizeof(data.camera));
        std::memset(data.player, 0, sizeof(data.player));
    }
    InterlockedIncrement64(&data.sequence);
    writer.clear(std::memory_order_release);
    return result;
}

DWORD fail(uint32_t error) {
    SpidyObserverData.error = error;
    SpidyObserverData.state = 3;
    return error;
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* parameter) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD status = 0;
    do {
        if (SpidyObserverData.state == 1)
            break;
        imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase.load());
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
        target = reinterpret_cast<void*>(imageBase + cameraUpdateRva);
        unsigned char actual[sizeof(signature)]{};
        if (!GetModuleHandleW(L"Spider-Man.exe") || nt->OptionalHeader.SizeOfImage != 140496896 ||
            !readBytes(reinterpret_cast<uintptr_t>(target), actual, sizeof(actual)) ||
            std::memcmp(actual, signature, sizeof(signature))) {
            status = fail(1001);
            break;
        }
        ObserverConfig config{};
        uintptr_t vtable = 0;
        if (!readBytes(reinterpret_cast<uintptr_t>(parameter), &config, sizeof(config)) ||
            config.magic != 0x53505943 || config.version != 1 || config.bytes != sizeof(config) ||
            config.pid != GetCurrentProcessId() || config.imageBase != imageBase ||
            !readBytes(config.manager, &vtable, 8) || vtable != imageBase + cameraVtableRva) {
            status = fail(1002);
            break;
        }
        observedManager = config.manager;
        if (!created) {
            const auto init = MH_Initialize();
            if (init != MH_OK) {
                status = fail(1100 + init);
                break;
            }
            const auto hook = MH_CreateHook(target, reinterpret_cast<void*>(&cameraUpdate),
                                            reinterpret_cast<void**>(&original));
            if (hook != MH_OK) {
                status = fail(1200 + hook);
                MH_Uninitialize();
                break;
            }
            created = true;
        }
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        SpidyObserverData.frequency = frequency.QuadPart;
        SpidyObserverData.pid = GetCurrentProcessId();
        SpidyObserverData.error = 0;
        SpidyObserverData.state = 1;
        const auto enabled = MH_EnableHook(target);
        if (enabled != MH_OK)
            status = fail(1300 + enabled);
    } while (false);
    ReleaseSRWLockExclusive(&lifecycle);
    return status;
}

extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result = 0;
    if (created) {
        const auto status = MH_DisableHook(target);
        if (status == MH_OK || status == MH_ERROR_DISABLED)
            SpidyObserverData.state = 2;
        else
            result = fail(1400 + status);
    }
    // Keep this DLL and its trampoline mapped until game exit. A function call
    // already in flight must be able to return through them after disabling.
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE; // All initialization is explicit and outside the loader lock.
}
