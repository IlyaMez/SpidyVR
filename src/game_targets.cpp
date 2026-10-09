// Finds web-grab candidates in-process, the way game_player finds the player.
#include "spidy/game_targets.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
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
// What a component's class says about its actor, from the game's own type
// information (MSVC RTTI): the class or any class it derives from, by its
// decorated name. A component marks its actor as a kind (1 << Kind) or gives
// it a trait (Trait << 8).
struct Rule {
    const char* name;
    uint32_t bits;
};
constexpr uint32_t marker(Kind kind) {
    return 1u << static_cast<unsigned>(kind);
}
constexpr uint32_t trait(Trait t) {
    return static_cast<uint32_t>(t) << 8;
}
constexpr Rule rules[] = {
    {".?AVThrowableHelper@@", marker(Kind::throwable)},
    {".?AVBotMoverManager@@", marker(Kind::bot)},
    {".?AVPedestrianMover@@", marker(Kind::pedestrian)},
    {".?AVHoverMoverManager@@", trait(hover)},
    {".?AVThugBot@@", trait(thug)},
    {".?AVCivilianBot@@", trait(civilian)},
    {".?AVAllyBot@@", trait(ally)},
    {".?AVMissionFollowBot@@", trait(ally)},
    {".?AVStatusEffectTrackerWebbed@@", trait(webbable)},
    {".?AVBreakableSystemComponent@@", trait(breakable)},
    {".?AVBirdBot@@", trait(neutral)},
    {".?AVHelicopter@@", trait(neutral)},
    {".?AVSilverSableCraftBot@@", trait(neutral)},
};
// A type descriptor's decorated name (at +0x10), its first 63 characters.
bool typeName(uintptr_t descriptor, char (&out)[64]) {
    for (size_t n = sizeof(out) - 1; n >= 16; n /= 2)
        if (read(descriptor + 0x10, out, n)) {
            out[n] = 0;
            return true;
        }
    return false;
}
// What a class is: its own name and those of all the classes it derives
// from, in its class hierarchy, which the complete object locator before its
// vtable names (x64: every link an image offset). 0 for a vtable without one.
uint32_t classify(uintptr_t base, uintptr_t vtable, std::unordered_map<uint32_t, uint32_t>& types) {
    struct Locator {
        uint32_t signature, offset, constructorOffset, type, hierarchy, self;
    } locator{};
    struct Hierarchy {
        uint32_t signature, attributes, count, bases;
    } hierarchy{};
    const auto at = pointer(vtable - 8);
    if (!read(at, &locator, sizeof(locator)) || locator.signature != 1 || locator.offset ||
        base + locator.self != at || !read(base + locator.hierarchy, &hierarchy, sizeof(hierarchy)) ||
        !hierarchy.count || hierarchy.count > 64)
        return 0;
    uint32_t bits{};
    for (uint32_t i = 0; i < hierarchy.count; ++i) {
        uint32_t descriptor{}, type{};
        if (!read(base + hierarchy.bases + 4ull * i, &descriptor, 4) || !read(base + descriptor, &type, 4))
            return 0;
        auto known = types.find(type);
        if (known == types.end()) {
            char name[64]{};
            uint32_t own{};
            if (typeName(base + type, name))
                for (const auto& rule : rules)
                    if (!std::strcmp(name, rule.name))
                        own |= rule.bits;
            known = types.emplace(type, own).first;
        }
        bits |= known->second;
    }
    return bits;
}
// The game image's size, from its PE header: every class's vtable lies in it.
uint64_t imageSize(uintptr_t base) {
    int32_t header{};
    uint32_t size{};
    return read(base + 0x3c, &header, 4) && read(base + header + 0x50, &size, 4) ? size : 0;
}
// The classes seen so far, by vtable, and the type descriptors by image
// offset: every watch's scan reads the same game.
std::mutex classesLock;
std::unordered_map<uintptr_t, uint32_t> classes;
std::unordered_map<uint32_t, uint32_t> types;
// classesLock held.
uint32_t lookup(uintptr_t base, uintptr_t vtable) {
    auto known = classes.find(vtable);
    if (known == classes.end())
        known = classes.emplace(vtable, classify(base, vtable, types)).first;
    return known->second;
}
} // namespace

uint32_t game_targets::classOf(uintptr_t base, uintptr_t vtable) {
    std::lock_guard lock(classesLock);
    return lookup(base, vtable);
}

bool game_targets::scan(uintptr_t base, uint32_t kinds, std::vector<Candidate>& out) {
    out.clear();
    const auto table = pointer(base + registryTable);
    int32_t count{};
    if (!table || !read(base + registryCount, &count, 4) || count <= 0 || count > 0x100000)
        return false;
    std::unordered_map<uint64_t, uint64_t> physics, machines;
    std::unordered_map<uint64_t, uint32_t> traits;
    const auto size = imageSize(base);
    std::lock_guard lock(classesLock);
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
        // The web moves an actor through these two classes themselves.
        if (head.vtable == base + physicsComponent) {
            physics[head.record] = entry.address;
            continue;
        }
        if (head.vtable == base + syncStaticStateMachine) {
            machines[head.record] = entry.address;
            continue;
        }
        if (head.vtable - base >= size)
            continue;
        const uint32_t bits = lookup(base, head.vtable);
        if (bits >> 8)
            traits[head.record] |= bits >> 8;
        for (unsigned k = 1; k < 4; ++k)
            if (kinds & bits & marker(static_cast<Kind>(k)))
                out.push_back({entry.address, head.record, head.handle, static_cast<Kind>(k), head.vtable});
    }
    for (auto& c : out) {
        if (c.kind == Kind::bot)
            sizeOf(base, c.component, c.size);
        if (const auto p = physics.find(c.record); p != physics.end())
            c.physics = p->second;
        if (const auto m = machines.find(c.record); m != machines.end())
            c.machine = m->second;
        if (const auto t = traits.find(c.record); t != traits.end())
            c.traits = t->second;
    }
    return true;
}
bool game_targets::sizeOf(uintptr_t base, uint64_t moverManager, Size& out) {
    struct Body {
        float low, high, radius;
    } body{};
    float scale{};
    if (pointer(moverManager + 0xdb8) != base + moverBodySize ||
        !read(moverManager + 0xdc0, &body, sizeof(body)) || !read(moverManager + 0xdf8, &scale, sizeof(scale)))
        return false;
    const Size size{body.low - body.radius, body.high * scale + body.radius, body.radius};
    if (!std::isfinite(size.low) || !std::isfinite(size.high) || !(body.radius > .02f && body.radius < 10) ||
        !(size.high - size.low > .05f && size.high - size.low < 20) || std::abs(size.low) > 20)
        return false;
    out = size;
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
           pointer(c.component) == c.vtable;
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
