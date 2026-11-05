// Expiring ray batches execute inside a verified native collision query. The
// native filter and transient collision-group lease stay owned by that callback.
#include "spidy/native_rays.hpp"
#include "spidy/native_query_context.hpp"
#include <MinHook.h>
#include <algorithm>
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
// Visitors' rays the world did not take as asked (usable()); Data::malformed.
std::atomic<uint32_t> malformed{};
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
// The player's body (native_rays::BodyFilter): his MoverManager (the registry's
// component of that table on his record), the game's collision profiles and
// what was last read of his. Packed as categories | types << 32.
constexpr uintptr_t registryTable = 0x7a44320, registryCount = 0x7a44340, moverManagerTable = 0x38b2c98,
                    profilesRva = 0x7813120;
constexpr uintptr_t profileIndexAt = 0xd9c, profileBytes = 32, profileTypesAt = 8, profileCategoriesAt = 0xc;
SRWLOCK bodyLock = SRWLOCK_INIT;
uint64_t bodyRecord{};
uintptr_t bodyManager{};
ULONGLONG bodyReadAt{}, bodySearchAt{};
std::atomic<uint64_t> bodyWord{};
uintptr_t managerOf(uint64_t record) {
    const auto table = pointer(config.base + registryTable);
    int32_t count{};
    if (!table || !read(config.base + registryCount, &count, 4) || count <= 0 || count > 0x100000)
        return 0;
    for (int32_t i = 0; i < count; ++i) {
        uintptr_t entry[2]{};
        if (!read(table + 16ull * i, entry, sizeof(entry)) || entry[0] < 0x10000 || !(entry[1] & 0xffffffffu))
            continue;
        if (pointer(entry[0]) == config.base + moverManagerTable && pointer(entry[0] + 8) == record)
            return entry[0];
    }
    return 0;
}
BodyFilter currentBody() {
    const uint64_t word = bodyWord.load(std::memory_order_relaxed);
    return {static_cast<uint32_t>(word), static_cast<uint32_t>(word >> 32)};
}
uint32_t queryOne(void* world, const void* nativeQuery, const Ray& ray, Hit& result, bool bodyOnly = false) {
    result.ray = ray;
    const BodyFilter body = bodyOnly ? currentBody() : BodyFilter{};
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
    // A full collector holds the nearest hits: past its capacity the game's
    // collector (vtable 3d0a948, add 1804200) replaces its farthest hit with a
    // nearer one (1803fd0 finds the farthest). A 100 m ray across a city block
    // meets 10-15 bodies; treating a 16th as an error ended the October 6
    // session in flat screen.
    result.count = std::min(get<uint32_t>(collector, 0xc), 16u);
    // For a ray of the player's body, the hits his body passes (meets()) are
    // as good as none: the nearest it meets is the hit, and with none it is a
    // miss.
    uint32_t taken{};
    for (unsigned h = 0; h < result.count; ++h) {
        const auto hit = hits + h * 0x90;
        const auto fraction = get<float>(hit, 0x20);
        const auto normal = get<Vec3>(hit, 0x10);
        if (!std::isfinite(fraction) || fraction < 0 || fraction > 1 || !finite(normal))
            return 2104;
        if (!meets(body, get<uint32_t>(hit, 0x78)))
            continue;
        ++taken;
        if (fraction > result.fraction)
            continue;
        result.fraction = fraction;
        result.normal = normal;
        result.position = ray.origin + displacement * fraction;
        result.bodyId = get<uint32_t>(hit, 0x48);
        result.filter = get<uint32_t>(hit, 0x78);
    }
    result.count = taken;
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
        return castOne(origin, direction, distance, false);
    }
    std::optional<RayHit> bodyRaycast(Vec3 origin, Vec3 direction, float distance) const override {
        return castOne(origin, direction, distance, true);
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
    std::optional<RayHit> castOne(Vec3 origin, Vec3 direction, float distance, bool body) const {
        if (distance >= 0 && distance < .01f)
            return {};
        if (!enabled || GetTickCount64() >= deadline) {
            error_ = 2105;
            return {};
        }
        Command check;
        check.count = 1;
        check.serial = 1;
        check.rays[0] = {origin, distance, direction, 0};
        // A ray the world does not take as asked is cut to what it takes, or
        // missed: counted, never the end of VR (usable()).
        if (!valid(check)) {
            malformed.fetch_add(1, std::memory_order_relaxed);
            const auto ray = usable(origin, direction, distance);
            if (!ray)
                return {};
            check.rays[0] = *ray;
        }
        Hit hit;
        if (const auto code = queryOne(world_, query_, check.rays[0], hit, body)) {
            error_ = code;
            return {};
        }
        if (!hit.count)
            return {};
        return RayHit{hit.position, hit.normal, hit.bodyId, fixedSurface(hit)};
    }
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
        error = queryOne(world, nativeQuery, batch.rays[i], results[i], (batch.bodyRays >> i) & 1);
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
        d.malformed = malformed.load(std::memory_order_relaxed);
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
void spidy::native_rays::setPlayer(uint64_t record) {
    if (!config.base)
        return;
    AcquireSRWLockExclusive(&bodyLock);
    const ULONGLONG now = GetTickCount64();
    if (record != bodyRecord) {
        bodyRecord = record;
        bodyManager = 0;
        bodyReadAt = bodySearchAt = 0;
        bodyWord = 0;
    }
    if (record && now - bodyReadAt >= 250) {
        bodyReadAt = now;
        // His MoverManager, still his; else found again, no more often than
        // every two seconds (the registry is long).
        if (bodyManager && (pointer(bodyManager) != config.base + moverManagerTable ||
                            pointer(bodyManager + 8) != record))
            bodyManager = 0;
        if (!bodyManager && (!bodySearchAt || now - bodySearchAt >= 2000)) {
            bodySearchAt = now;
            bodyManager = managerOf(record);
        }
        // The profile it queries with: a live entry of the game's table
        // (each starts with -1), its types and categories.
        BodyFilter found{};
        uint32_t index{}, first{}, categories{};
        uint8_t types{};
        const auto profiles = pointer(config.base + profilesRva);
        if (bodyManager && profiles && read(bodyManager + profileIndexAt, &index, 4) && index < 64) {
            const auto entry = profiles + index * profileBytes;
            if (read(entry, &first, 4) && first == 0xffffffffu && read(entry + profileTypesAt, &types, 1) &&
                read(entry + profileCategoriesAt, &categories, 4))
                found = {categories & 0x7ff, types};
        }
        bodyWord = found.categories | static_cast<uint64_t>(found.types) << 32;
    }
    ReleaseSRWLockExclusive(&bodyLock);
}
native_rays::BodyFilter spidy::native_rays::body() {
    return currentBody();
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyRayStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        // Running already: as before, a second start is refused.
        if (created && enabled) {
            result = 1000;
            break;
        }
        Config next{};
        if (!read(reinterpret_cast<uintptr_t>(input), &next, sizeof(next)) ||
            next.magic != 0x53525943 || next.version != 1 || next.bytes != sizeof(next) ||
            next.pid != GetCurrentProcessId() ||
            next.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) ||
            !GetModuleHandleW(L"Spider-Man.exe") || (next.durationMs && next.durationMs < 500) ||
            next.durationMs > 30000 || next.mask != 0x410) {
            result = 1001;
            break;
        }
        // Stopped earlier in this game process. MinHook still holds the hook,
        // only switched off - SpidyRayStop never forgot it - so every start
        // after the first answered 1000 and left the rays dead: a second VR
        // session in one game, or a second probe. Switch it on again instead,
        // as the pad does (game_pad::install).
        malformed = 0; // counted for each start: one session's report
        if (created) {
            config = next;
            deadline = config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
            enabled = true;
            if (const auto again = MH_EnableHook(hook); again != MH_OK && again != MH_ERROR_ENABLED) {
                enabled = false;
                result = 1300 + again;
            }
            break;
        }
        config = next;
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
// For a probe: the player's body as the rays that stand in for it see it
// (BodyFilter), for the actor record the 8 bytes in name, written back over
// them (categories, types). 0, 2202 a bad pointer, 2203 the module not
// started (its image base unknown).
extern "C" __declspec(dllexport) DWORD WINAPI SpidyRayBody(void* inout) {
    uint64_t record{};
    if (!read(reinterpret_cast<uintptr_t>(inout), &record, sizeof(record)))
        return 2202;
    if (!config.base)
        return 2203;
    native_rays::setPlayer(record);
    const BodyFilter found = native_rays::body();
    return write(inout, &found, sizeof(found)) ? 0 : 2202;
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
    SpidyRayData.malformed = malformed.load(std::memory_order_relaxed);
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
