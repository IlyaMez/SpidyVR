// Finds web-grab candidates in-process, the way game_player finds the player.
#include "spidy/game_targets.hpp"
#include "spidy/guard.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <set>
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
// Between the kinds and the traits: what a class is by itself, not what it
// makes its actor. A health component (BossHealth and ArmoredHealth derive
// from the two the scan knows by their vtables), a state of a bot's state
// machine that has him out of the fight, and one in which something else
// moves him (bit 0: no kind is 0).
constexpr uint32_t heldState = 1u << 0, downState = 1u << 5, healthClass = 1u << 6, botHealthClass = 1u << 7;
// The game's webbed-up state (game_targets::cocooned). No kind or trait has
// the top bit: the traits end at bit 24.
constexpr uint32_t webbedState = 1u << 31;
// The game's own stuck-to-a-surface state (game_targets::wallStuck), a down one.
constexpr uint32_t wallStuckState = 1u << 30, stateBits = webbedState | wallStuckState;
// Enemy types: the class's own name tells of combat (tellsCombat): its name is kept for the guard's census.
// Above every trait (trait(guardEngaged), the highest, is bit 24) and below the two state bits.
constexpr uint32_t combatClass = 1u << 29;
// The class's own name is a Behavior one (guards::isBehavior): its name is kept for the guard's census too.
constexpr uint32_t behaviorClass = 1u << 28;
// What is no trait: the state bits and the two name classes above.
constexpr uint32_t classBits = stateBits | combatClass | behaviorClass;
bool ownName(uintptr_t base, uintptr_t vtable, std::string& out);
// The guard rules the class names are read by, under classesLock.
guards::Rules guardRules;
// The guard bits a class gives its actor, by its decorated name (and the open state bit for a state class
// that has him staggered).
uint32_t guardBits(const char* decorated) {
    const auto plain = guards::plainName(decorated);
    const auto verdict = guards::classOf(plain, guardRules);
    uint32_t bits{};
    if (guards::opensGuard(plain))
        bits |= static_cast<uint32_t>(guardOpen) << 8;
    if (guards::webbedState(plain))
        bits |= static_cast<uint32_t>(guardWebbed) << 8;
    // A behavior of a fight on the actor: a sign that he has detected the hero.
    if (guards::fightBehavior(plain))
        bits |= static_cast<uint32_t>(guardEngaged) << 8;
    if (verdict.swordsman)
        bits |= static_cast<uint32_t>(guardSwordsman) << 8;
    if (verdict.exempt)
        bits |= static_cast<uint32_t>(guardExempt) << 8;
    switch (verdict.kind) {
    case guards::Kind::Weapon:
        bits |= static_cast<uint32_t>(guardWeapon) << 8;
        break;
    case guards::Kind::Brawler:
        bits |= static_cast<uint32_t>(guardBrawler) << 8;
        break;
    case guards::Kind::Shield:
        bits |= static_cast<uint32_t>(guardShield) << 8;
        break;
    default:
        break;
    }
    return bits;
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
    {".?AVBossHealth@@", trait(boss)},
    {".?AVBotStateStunned@@", trait(stunned)},
    {".?AVBotStateElectricStun@@", trait(stunned)},
    {".?AVHelicopter@@", trait(neutral)},
    {".?AVSilverSableCraftBot@@", trait(neutral)},
    {".?AVHealth@@", healthClass},
    {".?AVBotHealth@@", botHealthClass},
    {".?AVBotStateDeathAnim@@", downState},
    {".?AVBotStateRagdoll@@", downState},
    {".?AVBotStateWallStuck@@", downState | wallStuckState},
    {".?AVBotStateWebStruggle@@", webbedState},
    // Held (game_targets::held): attached to an actor (a car's seat), carried,
    // played by a cinematic, in BotStateNull, the guest of another's synced
    // animation, in a slotted interaction, frozen, or waiting for the game's
    // own takedown yank.
    {".?AVBotStateAttachToActor@@", heldState},
    {".?AVBotStateCarried@@", heldState},
    {".?AVBotStatePlayCinematic@@", heldState},
    {".?AVBotStateNull@@", heldState},
    {".?AVBotStatePlaySyncedAnimGuest@@", heldState},
    {".?AVBotStateSlottedInteract@@", heldState},
    {".?AVBotStateFrozen@@", heldState},
    {".?AVBotStateRequestTakedownYank@@", heldState},
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
            if (typeName(base + type, name)) {
                for (const auto& rule : rules)
                    if (!std::strcmp(name, rule.name))
                        own |= rule.bits;
                own |= guardBits(name);
            }
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
// The component classes that tell of combat, by vtable, with their own names (classesLock).
std::unordered_map<uintptr_t, std::string> combatNames;
// The same for the Behavior classes (guards::isBehavior).
std::unordered_map<uintptr_t, std::string> behaviorNames;
// classesLock held.
uint32_t lookup(uintptr_t base, uintptr_t vtable) {
    auto known = classes.find(vtable);
    if (known == classes.end()) {
        uint32_t bits = classify(base, vtable, types);
        std::string name;
        if (ownName(base, vtable, name)) {
            if (game_targets::tellsCombat(name.c_str())) {
                bits |= combatClass;
                combatNames[vtable] = name;
            }
            if (guards::isBehavior(name)) {
                bits |= behaviorClass;
                behaviorNames[vtable] = name;
            }
        }
        known = classes.emplace(vtable, bits).first;
    }
    return known->second;
}
// The latest scan's combat classes for each bot, joined (combatLock).
std::mutex combatLock;
std::unordered_map<uint64_t, std::string> combatNow;
// The latest scan's Behavior classes for each bot, joined (behaviorLock).
std::mutex behaviorLock;
std::unordered_map<uint64_t, std::string> behaviorNow;
// The names met for each actor record, joined with commas (a name once each, the bots' alone, at most about
// 700 characters).
std::unordered_map<uint64_t, std::string>
joinNames(const std::vector<std::pair<uint64_t, const std::string*>>& seen,
          const std::unordered_map<uint64_t, bool>& bots) {
    std::unordered_map<uint64_t, std::string> joined;
    for (const auto& [record, name] : seen) {
        if (!bots.count(record))
            continue;
        auto& list = joined[record];
        bool have{};
        for (size_t at = list.find(*name); at != std::string::npos && !have; at = list.find(*name, at + 1))
            have = (at == 0 || list[at - 1] == ',') &&
                   (at + name->size() == list.size() || list[at + name->size()] == ',');
        if (!have && list.size() + name->size() < 700)
            list += (list.empty() ? "" : ",") + *name;
    }
    return joined;
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
    std::unordered_map<uint64_t, uint64_t> physics, machines, healths;
    std::unordered_map<uint64_t, uint32_t> traits;
    // The component the game webs an actor up with, by its actor.
    struct Tracker {
        uint64_t component{}, vtable{};
        uint32_t handle{};
    };
    std::unordered_map<uint64_t, Tracker> trackers;
    std::unordered_map<uint64_t, bool> bots;
    // The combat classes met, by actor record (the guard's census), and the Behavior ones.
    std::vector<std::pair<uint64_t, const std::string*>> combatSeen, behaviorSeen;
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
        // A bot's own health goes before the plain one.
        if (head.vtable == base + botHealth) {
            healths[head.record] = entry.address;
            continue;
        }
        if (head.vtable == base + plainHealth) {
            healths.emplace(head.record, entry.address);
            continue;
        }
        if (head.vtable - base >= size)
            continue;
        const uint32_t bits = lookup(base, head.vtable);
        if (bits & botHealthClass)
            healths[head.record] = entry.address;
        else if (bits & healthClass)
            healths.emplace(head.record, entry.address);
        if ((bits & ~classBits) >> 8)
            traits[head.record] |= (bits & ~classBits) >> 8;
        if (bits & trait(webbable))
            trackers.emplace(head.record, Tracker{entry.address, head.vtable, head.handle});
        if (bits & marker(Kind::bot))
            bots[head.record] = true;
        for (unsigned k = 1; k < 5; ++k)
            if (kinds & bits & marker(static_cast<Kind>(k)))
                out.push_back({entry.address, head.record, head.handle, static_cast<Kind>(k), head.vtable});
    }
    // What the game webs up and is no bot: a device, by its tracker.
    if (kinds & marker(Kind::device))
        for (const auto& [record, tracker] : trackers)
            if (!bots.count(record))
                out.push_back({tracker.component, record, tracker.handle, Kind::device, tracker.vtable});
    for (auto& c : out) {
        if (c.kind == Kind::bot)
            sizeOf(base, c.component, c.size);
        if (const auto p = physics.find(c.record); p != physics.end())
            c.physics = p->second;
        if (const auto m = machines.find(c.record); m != machines.end())
            c.machine = m->second;
        if (const auto t = traits.find(c.record); t != traits.end())
            c.traits = t->second;
        if (const auto h = healths.find(c.record); h != healths.end())
            c.health = h->second;
    }
    // The bots' combat classes, joined, for the guard's census line (a name once each), and their Behavior
    // classes.
    auto joined = joinNames(combatSeen, bots);
    auto behaviorJoined = joinNames(behaviorSeen, bots);
    {
        std::lock_guard combat(combatLock);
        combatNow.swap(joined);
    }
    {
        std::lock_guard behavior(behaviorLock);
        behaviorNow.swap(behaviorJoined);
    }
    return true;
}
// The game's own class names, from its type information.
namespace {
// A class's own name from its vtable's complete object locator, without the decoration (".?AVBotHealth@@").
bool ownName(uintptr_t base, uintptr_t vtable, std::string& out) {
    struct Locator {
        uint32_t signature, offset, constructorOffset, type, hierarchy, self;
    } locator{};
    const auto at = pointer(vtable - 8);
    char name[64]{};
    if (!read(at, &locator, sizeof(locator)) || locator.signature != 1 ||
        !typeName(base + locator.type, name))
        return false;
    const char* begin = name;
    if (!std::strncmp(begin, ".?AV", 4) || !std::strncmp(begin, ".?AU", 4))
        begin += 4;
    out.assign(begin, std::strcspn(begin, "@"));
    return !out.empty();
}
} // namespace

bool game_targets::tellsCombat(const char* name) {
    static const char* const words[] = {"BotState", "Block", "Shield", "Armor",  "Stagger",
                                        "Weapon",   "Baton", "Guard",  "Melee",  "Brute",
                                        "Heavy",    "Stun",  "Parry",  "Counter"};
    for (const char* w : words)
        if (std::strstr(name, w))
            return true;
    return false;
}

namespace {
// A name in the game's type information that tells of a block or a reaction (not a bot state: those are kept
// whole).
bool tellsReaction(const std::string& name) {
    static const char* const words[] = {"Block",  "Guard",   "Parry", "Deflect", "Shield",
                                        "Defend", "Defense", "Brace", "Flinch",  "Stagger",
                                        "Armor",  "Cower",   "Evade"};
    for (const char* w : words)
        if (name.find(w) != std::string::npos)
            return true;
    return false;
}
// The plain class names in a stretch of memory copied out: a type descriptor is a vtable pointer, a spare 0
// and the decorated name (".?AVName@@") at +0x10, so a name starts on 8 bytes with 8 bytes of 0 before it.
void namesIn(const uint8_t* bytes, size_t from, size_t to, size_t have, uintptr_t address,
             std::set<std::string>& states, std::set<std::string>& others) {
    constexpr size_t most = 240;
    for (size_t i = (std::max<size_t>)(from, 8); i < to;) {
        const void* dot = std::memchr(bytes + i, '.', to - i);
        if (!dot)
            break;
        i = static_cast<const uint8_t*>(dot) - bytes;
        if (i + 5 < have && (address + i) % 8 == 0 && !std::memcmp(bytes + i, ".?AV", 4)) {
            uint64_t spare{};
            std::memcpy(&spare, bytes + i - 8, 8);
            if (!spare) {
                std::string name;
                for (size_t k = i + 4; k < have && k < i + 4 + 60 && bytes[k] != '@' && bytes[k] != 0; ++k) {
                    const char c = static_cast<char>(bytes[k]);
                    if (!(c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                          (c >= 'A' && c <= 'Z'))) {
                        name.clear();
                        break;
                    }
                    name += c;
                }
                if (name.size() >= 3 && !(name[0] >= '0' && name[0] <= '9')) {
                    if (name.compare(0, 8, "BotState") == 0 && name.size() > 8) {
                        if (states.size() < most)
                            states.insert(name.substr(8));
                    } else if (tellsReaction(name) && others.size() < most) {
                        others.insert(name);
                    }
                }
            }
        }
        ++i;
    }
}
std::string joined(const std::set<std::string>& names) {
    std::string out;
    for (const auto& n : names)
        out += (out.empty() ? "" : ",") + n;
    return out;
}
} // namespace

bool game_targets::imageClasses(uintptr_t base, std::string& states, std::string& others) {
    states.clear();
    others.clear();
    const auto size = imageSize(base);
    if (!base || !size || size > (1ull << 31))
        return false;
    std::set<std::string> stateSet, otherSet;
    constexpr size_t chunk = 1 << 16, margin = 8, tail = 80;
    std::vector<uint8_t> buffer(margin + chunk + tail);
    // The data sections first (the type descriptors are not constant); every readable stretch if they gave
    // little.
    for (int pass = 0; pass < 2 && stateSet.size() + otherSet.size() < 20; ++pass) {
        const uintptr_t end = base + size;
        for (uintptr_t at = base; at < end;) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) || !info.RegionSize)
                break;
            const uintptr_t from = reinterpret_cast<uintptr_t>(info.BaseAddress);
            const uintptr_t to = (std::min<uintptr_t>)(end, from + info.RegionSize);
            const bool readable = info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD));
            const bool writable = (info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                                                   PAGE_EXECUTE_WRITECOPY)) != 0;
            if (readable && (pass == 1 || writable)) {
                for (uintptr_t p = (std::max)(at, from); p < to; p += chunk) {
                    const uintptr_t start = p > from + margin ? p - margin : from;
                    const size_t lead = p - start;
                    const size_t want = (std::min<size_t>)(to - start, lead + chunk + tail);
                    if (read(start, buffer.data(), want))
                        namesIn(buffer.data(), lead, (std::min<size_t>)(want, lead + chunk), want, start,
                                stateSet, otherSet);
                }
            }
            if (to <= at)
                break;
            at = to;
        }
    }
    states = joined(stateSet);
    others = joined(otherSet);
    return !stateSet.empty() || !otherSet.empty();
}

bool game_targets::inPlay(const Candidate& c) {
    uint8_t stated{};
    return !c.machine || (read(c.machine + 0x80, &stated, 1) && stated && pointer(c.machine + 0x70));
}
bool game_targets::up(uintptr_t base, const Candidate& c) {
    float points{};
    if (c.kind != Kind::bot || !inPlay(c) || (health(c, points) && points <= 0))
        return false;
    return !c.machine || !(classOf(base, pointer(pointer(c.machine + 0x70))) & downState);
}
bool game_targets::held(uintptr_t base, const Candidate& c) {
    return c.kind == Kind::bot && c.machine && inPlay(c) &&
           (classOf(base, pointer(pointer(c.machine + 0x70))) & heldState);
}
bool game_targets::cocooned(uintptr_t base, const Candidate& c) {
    return c.kind == Kind::bot && c.machine && inPlay(c) &&
           (classOf(base, pointer(pointer(c.machine + 0x70))) & webbedState);
}
bool game_targets::wallStuck(uintptr_t base, const Candidate& c) {
    return c.kind == Kind::bot && c.machine && inPlay(c) &&
           (classOf(base, pointer(pointer(c.machine + 0x70))) & wallStuckState);
}
bool game_targets::isStunned(uintptr_t base, const Candidate& c) {
    return c.kind == Kind::bot && c.machine && inPlay(c) &&
           (classOf(base, pointer(pointer(c.machine + 0x70))) & trait(game_targets::stunned));
}
bool game_targets::inOpenState(uintptr_t base, const Candidate& c) {
    return c.kind == Kind::bot && c.machine && inPlay(c) &&
           (classOf(base, pointer(pointer(c.machine + 0x70))) & trait(game_targets::guardOpen));
}
bool game_targets::inWebbedState(uintptr_t base, const Candidate& c) {
    return c.kind == Kind::bot && c.machine && inPlay(c) &&
           (classOf(base, pointer(pointer(c.machine + 0x70))) & trait(game_targets::guardWebbed));
}
bool game_targets::stateName(uintptr_t base, const Candidate& c, std::string& out) {
    out.clear();
    if (c.kind != Kind::bot || !c.machine || !inPlay(c))
        return false;
    const auto vtable = pointer(pointer(c.machine + 0x70));
    return vtable && ownName(base, vtable, out);
}
bool game_targets::inRunningState(uintptr_t base, const Candidate& c) {
    std::string name;
    return stateName(base, c, name) && guards::runningState(name);
}
bool game_targets::standing(const Candidate& c) {
    float points{};
    return c.kind == Kind::device && !(health(c, points) && points <= 0);
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
bool game_targets::health(const Candidate& c, float& out) {
    float points{};
    if (!c.health || !read(c.health + 0x88, &points, sizeof(points)) || !std::isfinite(points))
        return false;
    out = points;
    return true;
}
bool game_targets::maxHealth(const Candidate& c, float& out) {
    float points{};
    if (!c.health || !read(c.health + 0x80, &points, sizeof(points)) || !std::isfinite(points) ||
        points <= 0 || points > 1e7f)
        return false;
    out = points;
    return true;
}
void game_targets::setGuardRules(const guards::Rules& rules) {
    std::lock_guard lock(classesLock);
    guardRules = rules;
    classes.clear();
    types.clear();
    combatNames.clear();
    behaviorNames.clear();
}
bool game_targets::combatClasses(uint64_t record, std::string& out) {
    std::lock_guard lock(combatLock);
    const auto found = combatNow.find(record);
    if (found == combatNow.end())
        return false;
    out = found->second;
    return true;
}
bool game_targets::behaviors(uint64_t record, std::string& out) {
    std::lock_guard lock(behaviorLock);
    const auto found = behaviorNow.find(record);
    if (found == behaviorNow.end())
        return false;
    out = found->second;
    return true;
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
