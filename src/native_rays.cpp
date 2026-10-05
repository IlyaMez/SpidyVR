// Expiring ray batches execute inside a verified native collision query. The
// native filter and transient collision-group lease stay owned by that callback.
#include "spidy/native_rays.hpp"
#include "spidy/native_query_context.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
using namespace spidy::native_rays;
extern "C" {
__declspec(dllexport) Data SpidyRayData;
}
namespace {
using Cast = void (*)(void*, const void*, void*);
Cast original{};
void* hook{};
Config config;
Command pending;
uint64_t deadline{}, pendingDeadline{}, claimedSerial{};
bool created{};
std::atomic<bool> enabled{};
std::atomic<unsigned> active{};
std::atomic<uint64_t> calls{};
std::atomic<Visitor> visitor{};
SRWLOCK lifecycle = SRWLOCK_INIT, requests = SRWLOCK_INIT, output = SRWLOCK_INIT;
// At most one batch runs at a time, even when several game ray workers enter.
std::atomic_flag busy = ATOMIC_FLAG_INIT;
bool read(uintptr_t address, void* out, size_t size) {
    __try {
        if (address < 0x10000)
            return false;
        std::memcpy(out, reinterpret_cast<void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool write(void* out, const void* data, size_t size) {
    __try {
        if (reinterpret_cast<uintptr_t>(out) < 0x10000)
            return false;
        std::memcpy(out, data, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
uintptr_t pointer(uintptr_t p) {
    uintptr_t out{};
    read(p, &out, sizeof(out));
    return out;
}
template <class T> void put(uint8_t* bytes, size_t at, const T& value) {
    std::memcpy(bytes + at, &value, sizeof(T));
}
template <class T> T get(const uint8_t* bytes, size_t at) {
    T value{};
    std::memcpy(&value, bytes + at, sizeof(T));
    return value;
}
struct Active {
    Active() {
        ++active;
    }
    ~Active() {
        --active;
    }
};
struct Busy {
    ~Busy() {
        busy.clear();
    }
};
void bodyMetadata(uintptr_t world, Hit& hit) {
    const auto bodies = pointer(world + 0x28);
    uint8_t metadata[0x40]{};
    uint32_t bodyCapacity{};
    const auto index = hit.bodyId & 0xffffff;
    if (bodies && read(world + 0x30, &bodyCapacity, 4) && index < bodyCapacity && bodyCapacity <= 0x1000000 &&
        read(bodies + static_cast<uintptr_t>(index) * 0xc0 + 0x40, metadata, sizeof(metadata))) {
        hit.motionId = get<uint32_t>(metadata, 0);
        hit.bodyFlags = get<uint32_t>(metadata, 4);
        hit.broadPhaseId = get<uint32_t>(metadata, 0x38);
        hit.bodyMatches = get<uint32_t>(metadata, 0x30) == hit.bodyId && (hit.bodyFlags & 3);
    }
}
uint32_t queryOne(void* world, const void* nativeQuery, const Ray& ray, Hit& result) {
    result.ray = ray;
    alignas(16) uint8_t query[0x70]{};
    if (!read(reinterpret_cast<uintptr_t>(nativeQuery), query, 0x30))
        return 2101;
    const auto info = get<uint32_t>(query, 0x14);
    put(query, 0x14, (info & ~0x7ffu) | config.mask);
    const float from[] = {ray.origin.x, ray.origin.y, ray.origin.z, 0};
    const auto displacement = ray.direction * ray.distance;
    const float delta[] = {displacement.x, displacement.y, displacement.z, 1};
    float inverse[4]{};
    for (unsigned j = 0; j < 4; ++j)
        inverse[j] = delta[j] != 0 ? 1.f / delta[j] : 3.402823466e38f;
    std::memcpy(query + 0x30, from, 16);
    std::memcpy(query + 0x40, delta, 16);
    std::memcpy(query + 0x50, inverse, 16);
    alignas(16) uint8_t hits[16 * 0x90]{};
    alignas(16) uint8_t collector[0x50]{};
    put(collector, 0, config.base + 0x3d0a948);
    put(collector, 0x38, reinterpret_cast<uintptr_t>(hits));
    put(collector, 0x40, uint32_t{16});
    if (!read(config.base + 0x6b14b40, collector + 0x10, 16))
        return 2102;
    original(world, query, collector);
    result.count = get<uint32_t>(collector, 0xc);
    if (result.count >= 16)
        return 2103;
    for (unsigned h = 0; h < result.count; ++h) {
        const auto hit = hits + h * 0x90;
        const auto fraction = get<float>(hit, 0x20);
        const auto normal = get<Vec3>(hit, 0x10);
        if (!std::isfinite(fraction) || fraction < 0 || fraction > 1 || !finite(normal))
            return 2104;
        if (fraction > result.fraction)
            continue;
        result.fraction = fraction;
        result.normal = normal;
        result.position = ray.origin + displacement * fraction;
        result.bodyId = get<uint32_t>(hit, 0x48);
        result.filter = get<uint32_t>(hit, 0x78);
    }
    if (result.count)
        bodyMetadata(reinterpret_cast<uintptr_t>(world), result);
    return 0;
}
class Context final : public QueryContext {
  public:
    Context(void* world, const void* query) : world_(world), query_(query) {}
    uint64_t identity() const override {
        return reinterpret_cast<uintptr_t>(world_);
    }
    uint32_t error() const override {
        return error_;
    }
    std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float distance) const override {
        if (distance >= 0 && distance < .01f)
            return {};
        Command check;
        check.count = 1;
        check.serial = 1;
        check.rays[0] = {origin, distance, direction, 0};
        if (!valid(check) || !enabled || GetTickCount64() >= deadline) {
            error_ = 2105;
            return {};
        }
        Hit hit;
        if (const auto code = queryOne(world_, query_, check.rays[0], hit)) {
            error_ = code;
            return {};
        }
        if (!hit.count)
            return {};
        return RayHit{hit.position, hit.normal, hit.bodyId, fixedSurface(hit)};
    }
    bool exists(uint64_t id) const override {
        if (id > UINT32_MAX)
            return false;
        Hit hit;
        hit.count = 1;
        hit.bodyId = static_cast<uint32_t>(id);
        bodyMetadata(identity(), hit);
        return fixedSurface(hit);
    }

  private:
    void* world_;
    const void* query_;
    mutable uint32_t error_{};
};
void cast(void* world, const void* nativeQuery, void* nativeCollector) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    Active guard;
    original(world, nativeQuery, nativeCollector);
    if (!enabled || GetTickCount64() >= deadline)
        return;
    ++calls;
    const auto queryAddress = reinterpret_cast<uintptr_t>(nativeQuery);
    const auto address = reinterpret_cast<uintptr_t>(world);
    const bool worker = caller == config.base + 0x18180fe && queryAddress >= 0x10020 &&
                        pointer(queryAddress - 0x20) == config.base + 0x3d0a9c8;
    if ((!worker && caller != config.base + 0x18173d1) || address != pointer(config.base + 0x78939e8) ||
        pointer(queryAddress + 8) != pointer(address + 0x498) || busy.test_and_set())
        return;
    Busy batchGuard;
    if (const auto visit = visitor.load()) {
        Context context(world, nativeQuery);
        visit(context);
    }
    Command batch;
    AcquireSRWLockExclusive(&requests);
    const bool ready =
        enabled && GetTickCount64() < pendingDeadline && pending.count && pending.serial > claimedSerial;
    if (ready) {
        batch = pending;
        claimedSerial = batch.serial;
    }
    ReleaseSRWLockExclusive(&requests);
    if (!ready)
        return;
    Hit results[capacity]{};
    uint32_t error{}, count{};
    for (unsigned i = 0; !error && i < batch.count; ++i) {
        // Superseded or expired requests must not publish usable old targets.
        if (!enabled || GetTickCount64() >= deadline)
            break;
        error = queryOne(world, nativeQuery, batch.rays[i], results[i]);
        if (error)
            break;
        ++count;
    }
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    AcquireSRWLockExclusive(&requests);
    const bool current = enabled && GetTickCount64() < deadline && GetTickCount64() < pendingDeadline &&
                         pending.serial == batch.serial && count == batch.count;
    if (current || error) {
        AcquireSRWLockExclusive(&output);
        auto& d = SpidyRayData;
        InterlockedIncrement64(&d.sequence);
        d.status = error ? 4 : 2;
        d.error = error;
        d.serial = batch.serial;
        d.qpc = qpc.QuadPart;
        d.calls = calls.load();
        ++d.batches;
        d.world = address;
        d.count = count;
        d.thread = GetCurrentThreadId();
        std::memcpy(d.hits, results, sizeof(results));
        InterlockedIncrement64(&d.sequence);
        ReleaseSRWLockExclusive(&output);
    }
    ReleaseSRWLockExclusive(&requests);
}
} // namespace
void spidy::native_rays::setVisitor(Visitor value) {
    visitor = value;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyRayStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        if (created) {
            result = 1000;
            break;
        }
        if (!read(reinterpret_cast<uintptr_t>(input), &config, sizeof(config)) ||
            config.magic != 0x53525943 || config.version != 1 || config.bytes != sizeof(config) ||
            config.pid != GetCurrentProcessId() ||
            config.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) ||
            !GetModuleHandleW(L"Spider-Man.exe") || (config.durationMs && config.durationMs < 500) ||
            config.durationMs > 30000 || config.mask != 0x410) {
            result = 1001;
            break;
        }
        const uint8_t expected[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
        uint8_t actual[sizeof(expected)]{};
        if (!read(config.base + 0x2e67010, actual, sizeof(actual)) ||
            std::memcmp(actual, expected, sizeof(actual))) {
            result = 1002;
            break;
        }
        auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            result = 1100 + status;
            break;
        }
        hook = reinterpret_cast<void*>(config.base + 0x2e67010);
        status = MH_CreateHook(hook, reinterpret_cast<void*>(cast), reinterpret_cast<void**>(&original));
        if (status != MH_OK) {
            result = 1200 + status;
            break;
        }
        created = true;
        deadline = config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
        enabled = true;
        status = MH_EnableHook(hook);
        if (status != MH_OK) {
            enabled = false;
            result = 1300 + status;
        }
    } while (false);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyRaySubmit(void* input) {
    Command c;
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || !valid(c))
        return 2001;
    AcquireSRWLockExclusive(&requests);
    DWORD result{};
    if (!enabled || GetTickCount64() >= deadline)
        result = 2002;
    else if (c.serial <= pending.serial)
        result = 2003;
    else {
        pending = c;
        pendingDeadline = GetTickCount64() + c.leaseMs;
    }
    ReleaseSRWLockExclusive(&requests);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyRaySample(void* out) {
    AcquireSRWLockShared(&requests);
    AcquireSRWLockShared(&output);
    Data sample = SpidyRayData;
    // Expiry, cancellation, or a newer pending command invalidates an old result.
    if (!enabled || GetTickCount64() >= deadline || GetTickCount64() >= pendingDeadline || !pending.count ||
        sample.serial != pending.serial) {
        sample.status = 0;
        sample.count = 0;
    }
    ReleaseSRWLockShared(&output);
    ReleaseSRWLockShared(&requests);
    return write(out, &sample, sizeof(sample)) ? 0 : 2201;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyRayStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    enabled = false;
    DWORD result{};
    if (created) {
        const auto status = MH_DisableHook(hook);
        if (status != MH_OK && status != MH_ERROR_DISABLED)
            result = 1400 + status;
    }
    const auto until = GetTickCount64() + 2000;
    while (active && GetTickCount64() < until)
        Sleep(1);
    if (active)
        result = 1501;
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidyRayData.sequence);
    SpidyRayData.status = result ? 4 : 3;
    SpidyRayData.error = result;
    InterlockedIncrement64(&SpidyRayData.sequence);
    ReleaseSRWLockExclusive(&output);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
