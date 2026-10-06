// Finds web-grab candidates in-process, the way game_player finds the player.
#include "spidy/game_targets.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_targets;
namespace {
constexpr uintptr_t registryTable = 0x7a44320, registryCount = 0x7a44340;
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
uint64_t marker(Kind kind) {
    switch (kind) {
    case Kind::throwable:
        return throwableHelper;
    case Kind::bot:
        return botMoverManager;
    case Kind::pedestrian:
        return pedestrianMover;
    }
    return 0;
}
} // namespace

bool game_targets::scan(uintptr_t base, uint32_t kinds, std::vector<Candidate>& out) {
    out.clear();
    const auto table = pointer(base + registryTable);
    int32_t count{};
    if (!table || !read(base + registryCount, &count, 4) || count <= 0 || count > 0x100000)
        return false;
    uintptr_t vtables[4]{};
    for (unsigned k = 1; k < 4; ++k)
        if (kinds & (1u << k))
            vtables[k] = base + marker(static_cast<Kind>(k));
    std::unordered_map<uint64_t, uint64_t> physics, machines;
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        struct Entry {
            uintptr_t address;
            uint32_t generation, pad;
        } entry{};
        if (!read(table + i * 16ull, &entry, sizeof(entry)) || entry.address < 0x10000 || entry.address % 8 ||
            !entry.generation || entry.generation > 0xfff)
            continue;
        struct Head {
            uintptr_t vtable, record;
            uint32_t pad, handle;
        } head{};
        if (!read(entry.address, &head, sizeof(head)) || head.handle != (entry.generation << 20 | i) ||
            !head.record)
            continue;
        if (head.vtable == base + physicsComponent)
            physics[head.record] = entry.address;
        else if (head.vtable == base + syncStaticStateMachine)
            machines[head.record] = entry.address;
        for (unsigned k = 1; k < 4; ++k)
            if (vtables[k] && head.vtable == vtables[k])
                out.push_back({entry.address, head.record, head.handle, static_cast<Kind>(k)});
    }
    for (auto& c : out) {
        if (const auto p = physics.find(c.record); p != physics.end())
            c.physics = p->second;
        if (const auto m = machines.find(c.record); m != machines.end())
            c.machine = m->second;
    }
    return true;
}
bool game_targets::live(uintptr_t base, const Candidate& c) {
    const auto table = pointer(base + registryTable);
    int32_t count{};
    const uint32_t index = c.handle & 0xfffff;
    uintptr_t address{};
    uint32_t generation{}, own{};
    return table && read(base + registryCount, &count, 4) && index < static_cast<uint32_t>(count) &&
           read(table + index * 16ull, &address, 8) && address == c.component &&
           read(table + index * 16ull + 8, &generation, 4) && generation == c.handle >> 20 &&
           read(c.component + 0x14, &own, 4) && own == c.handle && pointer(c.component + 8) == c.record &&
           pointer(c.component) == base + marker(c.kind);
}
uint64_t game_targets::resolve(uintptr_t base, uint32_t handle) {
    const auto table = pointer(base + registryTable);
    int32_t count{};
    const uint32_t index = handle & 0xfffff;
    uintptr_t address{};
    uint32_t generation{}, own{};
    if (!table || !(handle >> 20) || !read(base + registryCount, &count, 4) ||
        index >= static_cast<uint32_t>(count) || !read(table + index * 16ull, &address, 8) ||
        !read(table + index * 16ull + 8, &generation, 4) || generation != handle >> 20 ||
        !read(address + 0x14, &own, 4) || own != handle)
        return 0;
    return address;
}
uint64_t game_targets::botMover(uintptr_t base, const Candidate& c) {
    constexpr uintptr_t moverStandard = 0x4f70168;
    uint32_t handle{};
    if (c.kind != Kind::bot || !read(c.component + 0xdb4, &handle, 4))
        return 0;
    const auto mover = resolve(base, handle);
    return mover && pointer(mover) == base + moverStandard && pointer(mover + 8) == c.record ? mover : 0;
}
bool game_targets::position(const Candidate& c, Vec3& out) {
    float m[16]{};
    if (!read(pointer(c.record), m, sizeof(m)))
        return false;
    out = {m[12], m[13], m[14]};
    return finite(out) && length(out) < 1e6f;
}
void Watch::start(uintptr_t base, uint32_t kinds) {
    stop();
    base_ = base;
    kinds_ = kinds;
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
uint64_t Watch::current(std::vector<Candidate>& out) const {
    std::lock_guard lock(lock_);
    out = list_;
    return scans_;
}
void Watch::run() {
    std::vector<Candidate> next;
    for (;;) {
        scan(base_, kinds_, next);
        std::unique_lock lock(lock_);
        list_.swap(next);
        ++scans_;
        // Props stream in and out with the city and bots spawn with crimes:
        // a quarter of a second is soon enough for a web shot at one.
        if (wake_.wait_for(lock, std::chrono::milliseconds(250), [this] { return stopping_; }))
            return;
    }
}
