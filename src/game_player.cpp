// Finds the local player in-process, the way the launcher once did from
// outside before every session (tools/capture_game_state.py).
#include "spidy/game_player.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_player;
extern "C" {
__declspec(dllexport) DWORD WINAPI SpidyPlayerFind(void* output);
}
namespace {
constexpr uintptr_t registryTable = 0x7a44320, registryCount = 0x7a44340;
constexpr uintptr_t heroLocal = 0x38a93c8, heroMover = 0x38b2c98, moverStandard = 0x4f70168;
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
    read(p, &v, sizeof(v));
    return v;
}
// An actor transform: finite, inside the world, with an orthonormal basis.
bool transform(uintptr_t address) {
    float m[16]{};
    if (!read(address, m, sizeof(m)))
        return false;
    for (float v : m)
        if (!std::isfinite(v) || std::abs(v) >= 1e7f)
            return false;
    for (int i = 0; i < 3; ++i) {
        const float* a = m + i * 4;
        if (std::abs(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] - 1) > .15f)
            return false;
        for (int j = i + 1; j < 3; ++j) {
            const float* b = m + j * 4;
            if (std::abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) > .1f)
                return false;
        }
    }
    return true;
}
struct Registry {
    uintptr_t table{};
    uint32_t count{};
};
bool registry(uintptr_t base, Registry& out) {
    int32_t count{};
    out.table = pointer(base + registryTable);
    if (!out.table || !read(base + registryCount, &count, 4) || count <= 0 || count > 0x100000)
        return false;
    out.count = static_cast<uint32_t>(count);
    return true;
}
// The registered object a component handle (generation << 20 | index) names.
uintptr_t component(const Registry& r, uint32_t handle) {
    const uint32_t index = handle & 0xfffff, generation = handle >> 20;
    uintptr_t address{};
    uint32_t current{}, own{};
    if (!generation || index >= r.count || !read(r.table + index * 16, &address, 8) ||
        !read(r.table + index * 16 + 8, &current, 4) || current != generation || !read(address + 0x14, &own, 4) ||
        own != handle)
        return 0;
    return address;
}
// Still registered under its own handle, of the expected type, on the actor.
bool registered(const Registry& r, uintptr_t object, uintptr_t vtable, uintptr_t record) {
    uint32_t handle{};
    return object && read(object + 0x14, &handle, 4) && component(r, handle) == object && pointer(object) == vtable &&
           pointer(object + 8) == record;
}
} // namespace

bool game_player::find(uintptr_t base, Player& out) {
    out = {};
    Registry r;
    if (!registry(base, r))
        return false;
    Player found{};
    uint32_t heroes{};
    uintptr_t managers[4]{};
    uint32_t managerCount{};
    for (uint32_t i = 0; i < r.count; ++i) {
        uintptr_t address{};
        uint32_t generation{};
        if (!read(r.table + i * 16, &address, 8) || !read(r.table + i * 16 + 8, &generation, 4) ||
            address < 0x10000 || address % 8 || !generation || generation > 0xfff)
            continue;
        const auto vtable = pointer(address);
        if (vtable != base + heroLocal && vtable != base + heroMover)
            continue;
        uint32_t handle{};
        if (!read(address + 0x14, &handle, 4) || handle != (generation << 20 | i))
            continue;
        const auto record = pointer(address + 8);
        if (!record || !transform(pointer(record)))
            continue;
        if (vtable == base + heroLocal) {
            if (++heroes > 1)
                return false;
            found.hero = address;
            found.record = record;
        } else if (managerCount < std::size(managers)) {
            managers[managerCount++] = address;
        }
    }
    if (heroes != 1)
        return false;
    uintptr_t manager{};
    for (uint32_t i = 0; i < managerCount; ++i)
        if (pointer(managers[i] + 8) == found.record) {
            if (manager)
                return false;
            manager = managers[i];
        }
    uint32_t moverHandle{};
    if (!manager || !read(manager + 0xdb4, &moverHandle, 4))
        return false;
    found.mover = component(r, moverHandle);
    if (!registered(r, found.mover, base + moverStandard, found.record))
        return false;
    out = found;
    return true;
}

bool game_player::live(uintptr_t base, const Player& p) {
    Registry r;
    return p.hero && registry(base, r) && registered(r, p.hero, base + heroLocal, p.record) &&
           registered(r, p.mover, base + moverStandard, p.record) && transform(pointer(p.record));
}

void Watch::start(uintptr_t base) {
    stop();
    base_ = base;
    stopping_ = false;
    thread_ = std::thread([this] { run(); });
}
void Watch::stop() {
    {
        std::lock_guard lock(lock_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
Player Watch::current(uint64_t& changes) const {
    std::lock_guard lock(lock_);
    changes = changes_;
    return player_;
}
void Watch::run() {
    Player known{};
    for (;;) {
        Player next = known;
        // A live player is checked in microseconds; only a lost one is searched for.
        if (!known.hero || !live(base_, known))
            find(base_, next);
        std::unique_lock lock(lock_);
        if (!(next == player_)) {
            player_ = next;
            ++changes_;
        }
        known = next;
        // A level change replaces the player within a second or two; look
        // often until it is back, then check it at the headset's pace.
        if (wake_.wait_for(lock, std::chrono::milliseconds(known.hero ? 100 : 250), [this] { return stopping_; }))
            return;
    }
}

// Headless probes compare this with a discovery from outside the process.
// Output: Player, then the microseconds the search took.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyPlayerFind(void* output) {
    struct Result {
        Player player;
        uint64_t microseconds;
    } result{};
    LARGE_INTEGER start{}, end{}, frequency{};
    QueryPerformanceCounter(&start);
    const bool found = find(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), result.player);
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    result.microseconds = static_cast<uint64_t>((end.QuadPart - start.QuadPart) * 1000000 / frequency.QuadPart);
    __try {
        if (reinterpret_cast<uintptr_t>(output) < 0x10000)
            return 7001;
        std::memcpy(output, &result, sizeof(result));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 7001;
    }
    return found ? 0 : 7002;
}
