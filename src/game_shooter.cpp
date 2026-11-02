// The web shooter in the game: pulls from the swing's input samples, the
// game's own web-shooter shots fired through the hero's gadget on the main
// thread, and what they do where they land (game_shooter.hpp).
#include "spidy/game_shooter.hpp"
#include "spidy/game_focus.hpp"
#include "spidy/game_sound.hpp"
#include "spidy/game_warp.hpp"
#include "spidy/game_targets.hpp"
#include "spidy/native_bodies.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_shooter;
extern "C" {
__declspec(dllexport) Data SpidyShooterData;
}
namespace {
// The camera's update, once a frame on the main thread in play and in fights
// (where shots go out), and the muzzle every weapon asks its emitters for.
constexpr uintptr_t frameRva = 0x897d30, muzzleRva = 0x2150c40;
// The weapon's fire event, its own part of one, the shot it spawns, the
// game's shot ids and actor references.
constexpr uintptr_t weaponEventRva = 0xe3bd80, fillEventRva = 0xe55310, spawnShotRva = 0x2150830,
                    newShotIdRva = 0x215f040, makeRefRva = 0x1f7b8e0;
constexpr uintptr_t webShooter = 0x391c950, shotTable = 0x656d9f0, registryTable = 0x7a44320,
                    registryCount = 0x7a44340, actorRecords = 0x7a44380, actorRecordCount = 0x7a4439c;
// The shot's event handler (ShotWebShooter vtable +0xd8) and the readers it
// takes a collision's fields with: a field's index by its name hash, the
// field, and the actor record an actor reference names.
constexpr uintptr_t shotWebShooter = 0x3907d30, shotEventRva = 0xd2a570, findFieldRva = 0x1bcf3e0,
                    readFieldRva = 0x1f9db60, refRecordRva = 0x1f7b760;
constexpr uint32_t collisionEvent = 0xd930bcb2, hitActorField = 0x21eb297b, hitPositionField = 0x568973e3,
                   hitNormalField = 0xc3272bc7;
// The player's gadgets. A component of an actor by its type (as
// native_bodies finds one); the hero's weapon manager, by its type and its
// table; a loaded weapon by its id (21633c0); a weapon's charges and the most
// it holds, for a trigger (vtable +0x188 and +0x190, every gadget's own:
// float (weapon, index)). Every gadget's weapon class by its table, in the
// order of spidy::Gadget. The Impact Web is WeaponWebBlast: the game's own
// text has "the Web Blast gadget (aka Impact Web)" (Spider-Man.exe 3abd870).
// WeaponImpactWeb, for all its name, is the gadget the game calls the Web
// Shooter - its charged web ball, its HUD's "Web Shooter" once equipped
// (reports/grab-probe/gadget-impact_web.png, October 15) - and the wheel
// does not list it: its top place is the web shooter already. Until October
// 18 the two were the other way round, so IMPACT WEB fired web balls.
constexpr uintptr_t findInTableRva = 0x16799a0, findInListRva = 0x167ab40, weaponManagerType = 0x6dd84e0,
                    weaponManagerTable = 0x38b59b8, weaponByIdRva = 0x21633c0, ammoNowRva = 0xe3b710,
                    ammoMostRva = 0xe3b790;
constexpr uintptr_t gadgetTables[] = {0x391c950, 0x391bb10, 0x391bfc0, 0x3919110, 0x391ace0,
                                      0x391b1a0, 0x391b658, 0x39186a0, 0x3919a80};
// Each gadget's own part of its fire event (vtable +0x100), in that order.
constexpr uintptr_t gadgetFills[] = {0xe55310, 0xe52eb0, 0xe535d0, 0xe4d960, 0xe51950,
                                     0xe51f90, 0xe52620, 0xe490a0, 0xe4e6f0};
// Equipping one, as the game's own wheel does (791c20, 793320): the hero's
// GameInventoryManager (by its type), its GameEquipInventory (bbfba0), whose
// items (its vtable +0x3a0 the count, +0x1b0 one by index) are weapons at
// +0x88 3, their weapon id at +0x184 (the item's own config at +0x148, the
// id at its +0x3c); one the wheel offers (bbc3d0 0), equipped while the
// inventory allows it (+0x30), through a request (bbb8d0, its byte 4 set)
// that the game carries out the next frame (bbb9a0). The game's own call
// with a GUID (bbc2d0) makes the item when it finds none: never used here.
constexpr uintptr_t inventoryManagerType = 0x6e096f0, equipInventoryTable = 0x38e2440, inventoryOfRva = 0xbbfba0,
                    itemCountRva = 0x1f60060, itemAtRva = 0x1f55ed0, canEquipRva = 0x1f56900,
                    offeredRva = 0xbbc3d0, requestRva = 0xbbb8d0, equipRva = 0xbbb9a0;
// A weapon's charge to fire (vtable +0x180: bool (weapon, cost, trigger)).
constexpr uintptr_t hasAmmoRva = 0xe3b650;
using FindComponent = uintptr_t (*)(void*, const void*);
using WeaponById = uintptr_t (*)(void*, uint32_t);
using Ammo = float (*)(void*, uint32_t);
using Frame = uint64_t (*)(void*, float, uint64_t, uint64_t);
using Muzzle = float* (*)(void*, float*, uint32_t);
using ShotEvent = void* (*)(void*, uint64_t, void*, uint64_t);
using FindField = uint8_t* (*)(void*, uint32_t);
using ReadField = bool (*)(void*, uint32_t, void*, uint32_t);
using RefRecord = uint64_t (*)(const uint64_t*);
using FillEvent = uint32_t (*)(void*, uint8_t*);
using FireEvent = void* (*)(void*, uint8_t*);
using NewShotId = uint32_t (*)(void*);
using MakeRef = uint64_t* (*)(uint64_t*, void*);
// The header of the game's own fire events (+0 to +9; the weapon reads
// none of it).
constexpr uint8_t eventHeader[10] = {0x35, 0x20, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x49, 0x9c};
// A pull waits this long for the main thread at most; a paused game drops it.
constexpr uint64_t leaseMs = 250;
// A street thug's aim point above his feet, and how wide he is to the aim
// assist; other bots' by their size (game_targets::Size).
constexpr float chest = 1.15f, botRadius = .45f;
// A device's housing about its transform, for the aim: the line to its
// middle meets the housing first, which is no wall in the ball's way (with
// 0.6 m the aim took no gun turret from 22 m, in the game, October 12).
constexpr float deviceRadius = 1.5f;
// The gadget belongs to the player: its actor is this close to the player's.
constexpr float gadgetReach = 10;
// A shot flies at most a second (60 m); one older is gone.
constexpr uint64_t flightMs = 1500;

uintptr_t base{};
std::atomic<bool> enabled{};
std::atomic<unsigned> active{};
std::atomic<uint64_t> frames{};
SRWLOCK lifecycle = SRWLOCK_INIT, updating = SRWLOCK_INIT, queueLock = SRWLOCK_INIT, output = SRWLOCK_INIT;
bool hooked{};
void* hooks[3]{};
Frame originalFrame{};
Muzzle originalMuzzle{};
ShotEvent originalShotEvent{};
// The player's actor record, as the latest input sample named it: the
// damager of the webbing a shot deals.
std::atomic<uint64_t> heroRecord{};
// Spidy's shots that may still be flying: the ShotWebShooter, its registry
// handle and shot id (+0x104), what it was aimed at, along which line, and
// when it left. A collision consumes its entry.
struct Flying {
    uint64_t shot{}, target{}, firedMs{};
    uint32_t handle{}, id{};
    Vec3 direction{};
};
constexpr unsigned flyingSize = 16;
Flying flying[flyingSize];
unsigned flyingNext{};
SRWLOCK flyingLock = SRWLOCK_INIT;
// Spidy's webbing on each actor his balls struck lately (webbedUp()).
struct Webbing {
    uint64_t record{}, at{}, upAt{}; // GetTickCount64 of the latest hit, and when it webbed him up
    float amount{};
};
constexpr size_t webbingSlots = 32;
Webbing webbings[webbingSlots]{};
SRWLOCK webbingLock = SRWLOCK_INIT;
void addWebbing(uint64_t record) {
    const auto now = GetTickCount64();
    AcquireSRWLockExclusive(&webbingLock);
    Webbing* slot{};
    for (auto& w : webbings)
        if (w.record == record)
            slot = &w;
    if (!slot) {
        slot = &webbings[0];
        for (auto& w : webbings)
            if (w.at < slot->at)
                slot = &w; // the oldest, or a free one
        *slot = {record};
    }
    const float since = slot->at ? static_cast<float>(now - slot->at) / 1000.f : 0.f;
    slot->amount = std::max(0.f, slot->amount - since * game_shooter::webbingDecay) + game_shooter::webbingPerHit;
    slot->at = now;
    if (slot->amount >= game_shooter::webbedThreshold)
        slot->upAt = now;
    ReleaseSRWLockExclusive(&webbingLock);
}
// The gadget wheel: whether the hand fires the equipped gadget, a weapon id
// to equip at the next frame, and whether this game's code for it is the
// build Spidy reads (checked at start; without it the web shooter fires as
// before and nothing is listed or equipped).
std::atomic<bool> gadgetFire{}, gadgetsReady{};
std::atomic<uint32_t> equipWanted{};
// The weapon manager and the equipped weapon id the latest listing read
// (main thread).
uintptr_t listedManager{};
uint16_t listedEquipped{};
// The shot the main thread fires now: that weapon's muzzle, asked on that
// thread, is the hand's.
std::atomic<uint64_t> firingWeapon{};
std::atomic<DWORD> firingThread{};
float firingMatrix[16]{};

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
template <class T> T value(uintptr_t p) {
    T v{};
    read(p, &v, sizeof(v));
    return v;
}
bool entry(uintptr_t rva, std::initializer_list<uint8_t> expected) {
    uint8_t actual[16]{};
    return read(base + rva, actual, expected.size()) && std::memcmp(actual, expected.begin(), expected.size()) == 0;
}
// A function another module may have hooked first: its own bytes, or a
// hook's jump over its first `covered` bytes (whole instructions) and its own
// bytes after them. The movement bridge hooks the event field reader for its
// air sync (native_movement); called through that hook it reads as before.
// Its check of the whole entry kept the shooter from starting (9601) in the
// session of October 8, 15:36.
bool entryOrHooked(uintptr_t rva, size_t covered, std::initializer_list<uint8_t> expected) {
    uint8_t actual[16]{};
    return entry(rva, expected) ||
           (covered >= 5 && covered < expected.size() && read(base + rva, actual, expected.size()) &&
            actual[0] == 0xe9 &&
            std::memcmp(actual + covered, expected.begin() + covered, expected.size() - covered) == 0);
}
// The component a registry handle names, if it is still registered.
uintptr_t resolve(uint32_t handle) {
    const auto table = pointer(base + registryTable);
    const auto count = value<int32_t>(base + registryCount);
    const uint32_t index = handle & 0xfffff;
    if (!table || !(handle >> 20) || count <= 0 || index >= static_cast<uint32_t>(count) ||
        value<uint32_t>(table + index * 16ull + 8) != handle >> 20)
        return 0;
    const auto address = pointer(table + index * 16ull);
    return value<uint32_t>(address + 0x14) == handle ? address : 0;
}
// Still registered under its own handle, a web shooter.
bool liveGadget(uintptr_t weapon) {
    return weapon && pointer(weapon) == base + webShooter && resolve(value<uint32_t>(weapon + 0x14)) == weapon;
}
// An actor record that is still the live one its index names.
bool liveRecord(uintptr_t record) {
    const auto index = value<uint32_t>(record + 0xc) & 0xfffff;
    const auto generation = value<uint16_t>(record + 0x10) & 0x7ff;
    const auto records = pointer(base + actorRecords);
    return record && generation && records && index < value<uint32_t>(base + actorRecordCount) &&
           records + index * 0xc0ull == record;
}
// The actor record an actor handle names, as the game resolves one (15a0560): 0 for none.
uintptr_t actorOf(uint32_t handle) {
    const uint32_t generation = handle >> 20 & 0x7ff, index = handle & 0xfffff;
    const auto records = pointer(base + actorRecords);
    if (!generation || !records || index >= value<uint32_t>(base + actorRecordCount))
        return 0;
    const auto record = records + index * 0xc0ull;
    return value<uint16_t>(record + 0x10) == generation ? record : 0;
}
bool positionOf(uintptr_t record, Vec3& out) {
    float m[16]{};
    if (!read(pointer(record), m, sizeof(m)))
        return false;
    out = {m[12], m[13], m[14]};
    return finite(out) && length(out) < 1e6f;
}

// Finds the hero's gadget on its own thread: a registry scan takes
// milliseconds. The one nearest the player, whose actor travels with it.
class GadgetWatch {
  public:
    ~GadgetWatch() {
        stop();
    }
    void start() {
        stop();
        stopping_ = false;
        weapon_ = 0;
        thread_ = std::thread([this] { run(); });
    }
    void stop() {
        {
            std::lock_guard lock(lock_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }
    uint64_t current() const {
        return weapon_.load(std::memory_order_relaxed);
    }
    void around(Vec3 feet) {
        std::lock_guard lock(lock_);
        feet_ = feet;
        known_ = true;
    }
    // The main thread found it gone: look again now.
    void lost() {
        {
            std::lock_guard lock(lock_);
            rescan_ = true;
            weapon_ = 0;
        }
        wake_.notify_all();
    }

  private:
    uint64_t find() const {
        Vec3 feet{};
        bool known{};
        {
            std::lock_guard lock(lock_);
            feet = feet_;
            known = known_;
        }
        const auto table = pointer(base + registryTable);
        const auto count = value<int32_t>(base + registryCount);
        if (!table || count <= 0 || count > 0x100000)
            return 0;
        uint64_t best{}, only{};
        unsigned found{};
        float nearest = gadgetReach;
        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            struct Entry {
                uintptr_t address;
                uint32_t generation, pad;
            } e{};
            if (!read(table + i * 16ull, &e, sizeof(e)) || e.address < 0x10000 || e.address % 8 || !e.generation ||
                e.generation > 0xfff)
                continue;
            struct Head {
                uintptr_t vtable, record;
                uint32_t pad, handle;
            } head{};
            Vec3 at{};
            if (!read(e.address, &head, sizeof(head)) || head.vtable != base + webShooter ||
                head.handle != (e.generation << 20 | i) || !positionOf(head.record, at))
                continue;
            ++found;
            only = e.address;
            if (known && length(at - feet) < nearest) {
                nearest = length(at - feet);
                best = e.address;
            }
        }
        return known ? best : found == 1 ? only : 0;
    }
    void run() {
        for (;;) {
            const auto found = find();
            std::unique_lock lock(lock_);
            weapon_ = found;
            // A respawn or a load replaces the gadget within a second or two.
            wake_.wait_for(lock, std::chrono::milliseconds(found ? 1000 : 250),
                           [this] { return stopping_ || rescan_; });
            if (stopping_)
                return;
            rescan_ = false;
        }
    }
    mutable std::mutex lock_;
    std::condition_variable wake_;
    std::thread thread_;
    bool stopping_{}, known_{}, rescan_{};
    Vec3 feet_{};
    std::atomic<uint64_t> weapon_{};
};

GadgetWatch gadgets;
game_targets::Watch watch;
std::vector<game_targets::Candidate> bots;
// The targets a collision is matched against, taken on the thread the
// collision comes in on (the main thread).
std::vector<game_targets::Candidate> struck;
std::vector<ShooterTarget> targets;
Shooter shooter;
struct Pending {
    ShotRequest shot;
    uint64_t record{}; // the player's actor, and where it stood for the sample
    Vec3 feet{};
    uint64_t queuedMs{};
};
constexpr unsigned queueSize = 8;
Pending queue[queueSize];
unsigned queued{};

template <class F> void publish(F change) {
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidyShooterData.sequence);
    change(SpidyShooterData);
    SpidyShooterData.weapon = gadgets.current();
    SpidyShooterData.frames = frames.load(std::memory_order_relaxed);
    InterlockedIncrement64(&SpidyShooterData.sequence);
    ReleaseSRWLockExclusive(&output);
}

uint64_t frame(void* self, float seconds, uint64_t a, uint64_t b);
float* muzzle(void* self, float* out, uint32_t index) {
    const auto result = originalMuzzle(self, out, index);
    if (reinterpret_cast<uint64_t>(self) == firingWeapon.load(std::memory_order_relaxed) &&
        GetCurrentThreadId() == firingThread.load(std::memory_order_relaxed))
        std::memcpy(out, firingMatrix, sizeof(firingMatrix));
    return result;
}
// The shot's muzzle as the game's matrices are laid out: side, up and
// forward rows (the forward one along the shot), then the position.
void muzzleMatrix(Vec3 origin, Vec3 forward, float out[16]) {
    Vec3 side = normalized(cross({0, 1, 0}, forward));
    if (length(side) < .5f)
        side = {1, 0, 0};
    const Vec3 up = cross(forward, side);
    const float m[16] = {side.x,    side.y,    side.z,    0, up.x,     up.y,     up.z,     0,
                         forward.x, forward.y, forward.z, 0, origin.x, origin.y, origin.z, 1};
    std::memcpy(out, m, sizeof(m));
}
// The game's calls, which may fault on a gadget that went away in between.
bool newId(uint32_t& id) {
    __try {
        id = reinterpret_cast<NewShotId>(base + newShotIdRva)(reinterpret_cast<void*>(base + shotTable));
        return id != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool reference(uintptr_t record, uint64_t& ref) {
    __try {
        reinterpret_cast<MakeRef>(base + makeRefRva)(&ref, reinterpret_cast<void*>(record));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ref = 0;
        return false;
    }
}
bool fill(uintptr_t weapon, uint8_t* event, uintptr_t rva = fillEventRva) {
    __try {
        reinterpret_cast<FillEvent>(base + rva)(reinterpret_cast<void*>(weapon), event);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Writes the aim where the game's own fire puts it (e3c150) and every
// gadget's event reads it (e3bcf0): the target, the aim point, the facing,
// and which of them counts.
bool aimWeapon(uintptr_t weapon, uint64_t ref, Vec3 aimPoint, Vec3 facing) {
    __try {
        auto* w = reinterpret_cast<uint8_t*>(weapon);
        std::memcpy(w + 0x670, &ref, 8);
        std::memset(w + 0x678, 0, 12);
        std::memcpy(w + 0x684, &aimPoint, 12);
        std::memcpy(w + 0x690, &facing, 12);
        // Bit 2: the aim point counts; 3: resolve the target; 5 set as the
        // game sets it (the event's +0x38, +0x39, +0x3b).
        w[0x6a0] = static_cast<uint8_t>((w[0x6a0] & 0xc3) | 4 | (ref ? 8 : 0) | 0x20);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool fire(uintptr_t weapon, uint8_t* event) {
    firingThread = GetCurrentThreadId();
    firingWeapon = weapon;
    bool done{};
    __try {
        reinterpret_cast<FireEvent>(base + weaponEventRva)(reinterpret_cast<void*>(weapon), event);
        done = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        done = false;
    }
    firingWeapon = 0;
    return done;
}
// Below, with the player's gadgets.
template <class R, class... A> bool callGame(uintptr_t rva, R& out, A... args);
uintptr_t equippedGadget();
uintptr_t gameWebShooter();
// One shot through the gadget, as its R1 fires one. False when there is no
// gadget or no player to fire it, or the game refused it.
struct Fired {
    uint64_t shot{};
    bool resolved{}, gadget{}, empty{};
};
// The equipped gadget fired from the hand: with a charge to fire, as the
// game asks before its own; its own event from the aim written into it; the
// shot from the hand (the muzzle). The game takes the charge itself.
bool fireGadget(uintptr_t weapon, const Pending& p, Vec3 origin, Vec3 forward, Fired& out) {
    const auto setup = pointer(weapon + 0x48);
    const auto triggers = value<uint32_t>(setup + 0xd0);
    if (!setup || !triggers || triggers > 8)
        return false;
    const uint32_t index = std::min<uint32_t>(p.shot.hand == 1 ? 0 : 1, triggers - 1);
    bool charged{};
    if (!callGame(hasAmmoRva, charged, reinterpret_cast<void*>(weapon), 1.f, index))
        return false;
    if (!charged) {
        out.empty = true;
        return false;
    }
    uint64_t ref{};
    if (p.shot.target && liveRecord(p.shot.target))
        reference(p.shot.target, ref);
    Vec3 facing = normalized({forward.x, 0, forward.z});
    if (length(facing) < .5f)
        facing = {0, 0, 1};
    alignas(16) uint8_t event[0x100]{};
    uint32_t id{};
    const auto own = pointer(pointer(weapon) + 0x100) - base;
    if (!aimWeapon(weapon, ref, p.shot.aimPoint, facing) || !fill(weapon, event, own) || !newId(id))
        return false;
    std::memcpy(event, eventHeader, sizeof(eventHeader));
    const auto weaponId = value<uint16_t>(weapon + 0x5bc);
    std::memcpy(event + 8, &weaponId, 2);
    event[0xa] = static_cast<uint8_t>(index);
    event[0xb] = 1;
    std::memcpy(event + 0xc, &id, 4);
    muzzleMatrix(origin, forward, firingMatrix);
    if (!fire(weapon, event))
        return false;
    out.gadget = true;
    out.resolved = ref && (value<uint32_t>(weapon + 0x68) & 1);
    out.shot = resolve(value<uint32_t>(weapon + 0x518 + 4ull * index));
    return true;
}
bool fireNow(const Pending& p, Fired& out) {
    // The hand where it is now, as far from the player as it was in the sample.
    Vec3 origin = p.shot.origin, feet{};
    if (p.record && liveRecord(p.record) && positionOf(p.record, feet) && length(feet - p.feet) < 20)
        origin += feet - p.feet;
    const Vec3 forward = normalized(p.shot.aimPoint - origin);
    if (!finite(origin) || !finite(p.shot.aimPoint) || length(forward) < .5f)
        return false;
    if (const auto gadget = equippedGadget())
        return fireGadget(gadget, p, origin, forward, out);
    // The game's own Web Shooter, where it is the gadget the game has equipped
    // (what its RB fires: every player's, unless a wheel chose another). Its
    // shot is the game's web, which seals a leak, webs up a vent, a turret or a
    // thug as the game made them to be webbed. The ball below carries nothing
    // (game_shooter.hpp): in hantop's session at the research stations on
    // October 22 (Under Pressure, Chemical Leak, Ventilate the Problem) about
    // 190 of 723 balls struck the same three actors in two bursts, and not one
    // sealed anything. With no charge left the hand fires the ball, so a pull
    // is never left unanswered.
    if (const auto own = gameWebShooter()) {
        if (fireGadget(own, p, origin, forward, out))
            return true;
        const bool empty = out.empty;
        out = {};
        out.empty = empty;
    }
    const auto weapon = gadgets.current();
    if (!liveGadget(weapon)) {
        gadgets.lost();
        return false;
    }
    // The fire reads the weapon's setup (+0x48) and SpawnShot the actor at
    // +0x5b0 without asking whether there is one.
    if (!pointer(weapon + 0x48) || !actorOf(value<uint32_t>(weapon + 0x5b0)))
        return false;
    alignas(16) uint8_t event[0x80]{};
    uint32_t id{};
    if (!fill(weapon, event) || !newId(id))
        return false;
    std::memcpy(event, eventHeader, sizeof(eventHeader));
    const uint8_t emitter = p.shot.hand == 1 ? 0 : 1; // right wrist 0, left 1
    event[0xa] = emitter;
    event[0xb] = 1;
    std::memcpy(event + 0xc, &id, 4);
    uint64_t ref{};
    if (p.shot.target && liveRecord(p.shot.target))
        reference(p.shot.target, ref);
    std::memcpy(event + 0x10, &ref, 8);
    std::memset(event + 0x18, 0, 8);
    std::memcpy(event + 0x20, &p.shot.aimPoint, 12);
    Vec3 facing = normalized({forward.x, 0, forward.z});
    if (length(facing) < .5f)
        facing = {0, 0, 1};
    std::memcpy(event + 0x2c, &facing, 12);
    event[0x38] = 1;
    event[0x39] = ref ? 1 : 0;
    event[0x3a] = 0;
    event[0x3b] = 1;
    muzzleMatrix(origin, forward, firingMatrix);
    if (!fire(weapon, event))
        return false;
    out.resolved = ref && (value<uint32_t>(weapon + 0x68) & 1);
    const auto handle = value<uint32_t>(weapon + 0x518 + 4ull * emitter);
    out.shot = resolve(handle);
    if (out.shot) {
        AcquireSRWLockExclusive(&flyingLock);
        flying[flyingNext++ % flyingSize] = {out.shot, p.shot.target, GetTickCount64(), handle, id, forward};
        ReleaseSRWLockExclusive(&flyingLock);
    }
    return true;
}
// The player's gadgets, read on the main thread.
uintptr_t componentOf(uintptr_t record, uintptr_t type) {
    const bool hashed = value<uint16_t>(record + 0x88) != 0;
    __try {
        return reinterpret_cast<FindComponent>(base + (hashed ? findInTableRva : findInListRva))(
            reinterpret_cast<void*>(record + (hashed ? 0x80 : 0x58)), reinterpret_cast<void*>(base + type));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
uintptr_t weaponById(uintptr_t manager, uint16_t id) {
    __try {
        return reinterpret_cast<WeaponById>(base + weaponByIdRva)(reinterpret_cast<void*>(manager), id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
float ammo(uintptr_t rva, uintptr_t weapon) {
    __try {
        return reinterpret_cast<Ammo>(base + rva)(reinterpret_cast<void*>(weapon), 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
uint8_t charges(float v) {
    return static_cast<uint8_t>(std::isfinite(v) ? std::clamp(std::lround(v), 0l, 255l) : 0);
}
// A call into the game that may fault on an object that went away.
template <class R, class... A> bool callGame(uintptr_t rva, R& out, A... args) {
    __try {
        out = reinterpret_cast<R (*)(A...)>(base + rva)(args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The gadgets the game's own wheel offers, walked as it walks them (793320):
// its inventory's weapon items that it offers, each with its weapon id.
// Calls `each`(item, id) until it returns true. 0, or why not: 2 no
// inventory, 5 a fault.
template <class F> uint16_t wheelItems(uintptr_t record, uintptr_t& inventory, F each) {
    const auto manager = componentOf(record, inventoryManagerType);
    if (!manager || !callGame(inventoryOfRva, inventory, reinterpret_cast<void*>(manager)) || !inventory ||
        pointer(inventory) != base + equipInventoryTable)
        return 2;
    void* const inv = reinterpret_cast<void*>(inventory);
    int32_t count{};
    if (!callGame(itemCountRva, count, inv))
        return 5;
    for (int32_t i = 0; i < std::min(count, 512); ++i) {
        uintptr_t item{};
        if (!callGame(itemAtRva, item, inv, static_cast<uint32_t>(i)) || !item || value<uint32_t>(item + 0x88) != 3)
            continue;
        int32_t offered = -1;
        if (!callGame(offeredRva, offered, inv, reinterpret_cast<void*>(item)) || offered != 0)
            continue;
        if (each(item, value<uint16_t>(item + 0x184)))
            break;
    }
    return 0;
}
// Equips the player's own gadget whose weapon id is `wanted` (Data::equipResult).
uint16_t equipNow(uint16_t wanted) {
    const auto record = heroRecord.load(std::memory_order_relaxed);
    if (!record || !liveRecord(record))
        return 1;
    uintptr_t inventory{}, found{};
    if (const auto code = wheelItems(record, inventory, [&](uintptr_t item, uint16_t id) {
            found = id == wanted ? item : 0;
            return found != 0;
        }))
        return code;
    if (!found)
        return 3;
    bool allowed{};
    if (!callGame(canEquipRva, allowed, reinterpret_cast<void*>(inventory)) || !allowed)
        return 4;
    alignas(16) uint8_t request[16]{};
    void* made{};
    if (!callGame(requestRva, made, static_cast<void*>(request)))
        return 5;
    request[4] = 1;
    uint64_t done{};
    return callGame(equipRva, done, reinterpret_cast<void*>(inventory), reinterpret_cast<void*>(found),
                    static_cast<void*>(request))
               ? 0
               : 5;
}
// The player's gadgets: the weapons the game's wheel offers, one of each
// gadget, at its place round the wheel in their order. The weapon manager's
// own list (+0x1a8) has more: the built-in web shooter, which no wheel item
// names. The game's charged Web Shooter gadget (gadgetTables' last) is not
// listed: the wheel's top place gives web balls already.
void listGadgets() {
    const auto record = heroRecord.load(std::memory_order_relaxed);
    Owned found[8]{};
    uint32_t count{}, error{};
    uint16_t equipped{};
    uintptr_t manager{};
    if (!gadgetsReady.load(std::memory_order_relaxed))
        error = 3;
    else if (!record || !liveRecord(record))
        error = 1;
    else if (manager = componentOf(record, weaponManagerType); !manager || pointer(manager) != base + weaponManagerTable)
        error = 2, manager = 0;
    else {
        equipped = value<uint16_t>(manager + 0x6c); // slot 0
        uintptr_t inventory{};
        if (wheelItems(record, inventory, [&](uintptr_t, uint16_t id) {
                const auto weapon = id ? weaponById(manager, id) : 0;
                const auto table = weapon ? pointer(weapon) : 0;
                uint8_t kind{};
                for (size_t k = 0; k < 8; ++k)
                    if (table == base + gadgetTables[k])
                        kind = static_cast<uint8_t>(k + 1);
                if (kind && std::none_of(found, found + count, [&](const Owned& o) { return o.kind == kind; })) {
                    const uint8_t most = charges(ammo(ammoMostRva, weapon));
                    found[count++] = {id, kind, static_cast<uint8_t>(kind - 1),
                                      most ? std::min(charges(ammo(ammoNowRva, weapon)), most) : uint8_t{}, most};
                }
                return count >= 8;
            }))
            error = 4;
    }
    listedManager = manager;
    listedEquipped = equipped;
    publish([&](Data& d) {
        d.manager = manager;
        d.gadgetCount = count;
        d.gadgetError = error;
        d.equipped = equipped;
        std::copy(found, found + 8, d.gadgets);
    });
}
// The weapon the game has equipped (what its RB fires) if it is the gadget
// of gadgetTables[k], with that gadget's own event fill; 0 otherwise.
uintptr_t equippedAs(size_t k) {
    if (!gadgetsReady.load(std::memory_order_relaxed) || !listedManager || !listedEquipped ||
        pointer(listedManager) != base + weaponManagerTable)
        return 0;
    const auto weapon = weaponById(listedManager, listedEquipped);
    const auto table = weapon ? pointer(weapon) : 0;
    return table == base + gadgetTables[k] && pointer(table + 0x100) == base + gadgetFills[k] ? weapon : 0;
}
// The gadget the game has equipped, for the hand to fire: its weapon; 0 for
// the web shooter (its own way, below) or with the wheel's fire off.
uintptr_t equippedGadget() {
    if (!gadgetFire.load(std::memory_order_relaxed))
        return 0;
    for (size_t k = 1; k < 8; ++k)
        if (const auto weapon = equippedAs(k))
            return weapon;
    return 0;
}
// The game's own Web Shooter (WeaponImpactWeb, gadgetTables' last) while the
// game has it equipped, whatever the wheel's fire: the web balls' first way.
uintptr_t gameWebShooter() {
    return equippedAs(std::size(gadgetTables) - 1);
}
// One of Spidy's own shots, still flying: its entry, taken off the list.
bool ours(uintptr_t shot, Flying& out) {
    const auto now = GetTickCount64();
    bool found{};
    AcquireSRWLockExclusive(&flyingLock);
    for (auto& f : flying)
        if (f.shot && f.shot == shot && now - f.firedMs < flightMs && value<uint32_t>(shot + 0x14) == f.handle &&
            value<uint32_t>(shot + 0x104) == f.id) {
            out = f;
            f = {};
            found = true;
            break;
        }
    ReleaseSRWLockExclusive(&flyingLock);
    return found;
}
// A collision event's field, as the shot's own handler reads it.
bool field(uintptr_t event, uint32_t name, void* out, uint32_t size) {
    __try {
        const auto descriptor = pointer(event);
        if (!descriptor)
            return false;
        const auto index =
            reinterpret_cast<FindField>(base + findFieldRva)(reinterpret_cast<void*>(descriptor + 0x10), name);
        return index && reinterpret_cast<ReadField>(base + readFieldRva)(reinterpret_cast<void*>(event), *index,
                                                                          out, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
uint64_t recordOf(uint64_t reference) {
    __try {
        return reinterpret_cast<RefRecord>(base + refRecordRva)(&reference);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
// What one of Spidy's shots struck: an enemy the game webs up takes a web
// hit's webbing, a prop that is no breakable is knocked along the shot.
void collided(uintptr_t shot, uintptr_t event) {
    Flying f{};
    if (!ours(shot, f))
        return;
    uint64_t reference{};
    Vec3 point{}, normal{};
    const uint64_t record = field(event, hitActorField, &reference, sizeof(reference)) ? recordOf(reference) : 0;
    const bool placed = field(event, hitPositionField, &point, sizeof(point)) && finite(point);
    if (!field(event, hitNormalField, &normal, sizeof(normal)) || !finite(normal) || length(normal) < .5f)
        normal = f.direction * -1.f;
    bool webbed{}, pushed{};
    if (record) {
        watch.current(struck);
        for (const auto& c : struck) {
            if (c.record != record)
                continue;
            Vec3 at{};
            if (!placed && game_targets::position(c, at))
                point = at + Vec3{0, c.size.height(chest), 0};
            // An enemy in the fight, or a device that stands (never the
            // player's own actor): the game's own web shots web both up.
            if ((game_targets::fighting(base, c) ||
                 (game_targets::standing(c) && record != heroRecord.load(std::memory_order_relaxed))) &&
                (c.traits & game_targets::webbable)) {
                native_bodies::Damage d;
                d.victim = record;
                d.damager = heroRecord.load(std::memory_order_relaxed);
                d.point = point;
                d.direction = f.direction;
                d.normal = normalized(normal);
                // A gun turret has 3 hit points and the game's own web shots
                // take none of them: it is to be webbed up and thrown, not shot
                // down. So a device takes the webbing alone.
                d.amount = c.kind == game_targets::Kind::device ? 0 : webDamage;
                d.type = 20;     // kWebImpact
                d.knockback = 1; // kTwitch
                d.knockbackAmount = 1;
                d.statusType = 19; // kWebEncase
                d.statusAmount = webbingPerHit;
                webbed = native_bodies::damage(d) != 0;
                if (webbed && c.kind == game_targets::Kind::bot)
                    addWebbing(record);
            } else if (c.kind == game_targets::Kind::throwable && c.physics &&
                       !(c.traits & game_targets::breakable)) {
                // As a throw launches it: once, along the shot, tumbling.
                TargetCommand push;
                push.id = record;
                push.kind = TargetKind::Object;
                push.mode = TargetCommand::Mode::Launch;
                push.thrown = true;
                push.velocity =
                    normalized(Vec3{f.direction.x, 0, f.direction.z}) * pushSpeed + Vec3{0, pushLift, 0};
                push.spins = true;
                push.spin = cross(Vec3{0, 1, 0}, f.direction) * 3.f;
                pushed = native_bodies::drive(pointer(record), c.physics, push, 150);
            }
            break;
        }
    }
    publish([&](Data& d) {
        ++d.collisions;
        d.webbed += webbed;
        d.pushed += pushed;
        d.lastHit = record;
    });
}
void* shotEvent(void* shot, uint64_t hash, void* event, uint64_t r9) {
    // Before the shot's own handler, which may end the shot.
    if (static_cast<uint32_t>(hash) == collisionEvent && enabled.load(std::memory_order_relaxed)) {
        ++active;
        if (enabled)
            collided(reinterpret_cast<uintptr_t>(shot), reinterpret_cast<uintptr_t>(event));
        --active;
    }
    return originalShotEvent(shot, hash, event, r9);
}
// The pulls waiting for the main thread, in order.
void fireQueued() {
    Pending batch[queueSize];
    unsigned count{};
    AcquireSRWLockExclusive(&queueLock);
    std::copy(queue, queue + queued, batch);
    count = queued;
    queued = 0;
    ReleaseSRWLockExclusive(&queueLock);
    if (!count)
        return;
    const auto now = GetTickCount64();
    for (unsigned i = 0; i < count; ++i) {
        const auto& p = batch[i];
        Fired fired;
        const bool ok = now - p.queuedMs < leaseMs && fireNow(p, fired);
        publish([&](Data& d) {
            // A gadget without a charge: its pull dropped, or for the game's
            // Web Shooter answered with the ball.
            d.emptyPulls += fired.empty;
            if (!ok) {
                ++d.dropped;
                return;
            }
            ++d.fired;
            d.gadgetShots += fired.gadget;
            auto& h = d.hands[p.shot.hand];
            ++h.shots;
            h.lastTarget = p.shot.target;
            d.targeted += p.shot.target != 0;
            d.resolved += fired.resolved;
            d.lastOrigin = p.shot.origin;
            d.lastAimPoint = p.shot.aimPoint;
            d.lastShot = fired.shot;
        });
    }
}
uint64_t frame(void* self, float seconds, uint64_t a, uint64_t b) {
    const auto result = originalFrame(self, seconds, a, b);
    // The hero's own transitions Spidy asked for (game_warp), the Focus its
    // blows earned (game_focus) and their sounds (game_sound): this is the
    // main thread, after the hero's update.
    game_warp::frame();
    game_focus::frame();
    game_sound::frame();
    if (!enabled.load(std::memory_order_relaxed))
        return result;
    ++active;
    if (enabled) {
        // The gadgets every half second (at 60 frames a second), and one
        // the wheel chose, equipped.
        if (++frames % 30 == 1)
            listGadgets();
        if (const auto wanted = equipWanted.exchange(0))
            if (gadgetsReady.load(std::memory_order_relaxed)) {
                const auto code = equipNow(static_cast<uint16_t>(wanted));
                publish([&](Data& d) {
                    d.equipResult = code;
                    ++d.equips;
                });
            }
        fireQueued();
    }
    --active;
    return result;
}
bool enqueue(const Pending& p) {
    AcquireSRWLockExclusive(&queueLock);
    const bool room = queued < queueSize;
    if (room)
        queue[queued++] = p;
    ReleaseSRWLockExclusive(&queueLock);
    return room;
}
} // namespace

uint32_t game_shooter::start(uintptr_t gameBase) {
    AcquireSRWLockExclusive(&lifecycle);
    uint32_t result{};
    do {
        if (enabled)
            break;
        base = gameBase;
        // This game build's functions, and the gadget's own table naming them.
        const auto slot = [](uintptr_t offset, uintptr_t rva) {
            return pointer(base + webShooter + offset) == base + rva;
        };
        if (!entry(frameRva, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x44, 0x88, 0x48, 0x20, 0x44, 0x88, 0x40,
                              0x18, 0x55}) ||
            !entry(muzzleRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x60, 0x0f, 0x28, 0x05, 0x4f,
                               0x00, 0x6f}) ||
            !entry(weaponEventRva, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x41, 0x56, 0x48,
                                    0x81, 0xec, 0xb0}) ||
            !entry(fillEventRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa,
                                  0x48, 0x8b, 0xd9}) ||
            !entry(newShotIdRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83,
                                  0xec, 0x20, 0x48}) ||
            !entry(makeRefRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x33, 0xc0, 0x48, 0x8b,
                                0xfa, 0x48}) ||
            !entry(spawnShotRva, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
                                  0x56, 0x41, 0x57}) ||
            !slot(0x100, fillEventRva) || !slot(0x108, weaponEventRva) || !slot(0x110, spawnShotRva) ||
            !slot(0x160, muzzleRva) ||
            !entry(shotEventRva, {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x50, 0x49, 0x8b, 0xf0, 0x8b, 0xda,
                                  0x48, 0x8b, 0xf9}) ||
            pointer(base + shotWebShooter + 0xd8) != base + shotEventRva ||
            !entry(findFieldRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x44, 0x8b, 0xd2, 0x4c, 0x8b, 0xc9, 0x85, 0xd2,
                                  0x74, 0x62, 0x4c}) ||
            // sub rsp, 28h; movsxd rax, edx: the 7 bytes a hook's jump covers.
            !entryOrHooked(readFieldRva, 7,
                           {0x48, 0x83, 0xec, 0x28, 0x48, 0x63, 0xc2, 0x4d, 0x8b, 0xd0, 0x80, 0xbc, 0x08, 0x10,
                            0x04, 0x00}) ||
            !entry(refRecordRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x59, 0x04, 0x8b, 0x01, 0x48, 0xc1,
                                  0xe3, 0x20, 0x48})) {
            result = 9601;
            break;
        }
        // The gadget wheel's own functions: without them the web shooter
        // fires as before, and nothing is listed or equipped.
        {
            const auto table = [](uintptr_t vtable, uintptr_t offset, uintptr_t rva) {
                return pointer(base + vtable + offset) == base + rva;
            };
            bool ready =
                entry(findInTableRva, {0x44, 0x0f, 0xb7, 0x49, 0x08, 0x4c, 0x8b, 0xc2, 0x4c, 0x8b, 0x11, 0x41, 0xff,
                                       0xc9, 0x49, 0xc1}) &&
                entry(findInListRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda,
                                      0x48, 0x8b, 0xf9}) &&
                entry(weaponByIdRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x01,
                                      0x8b, 0xfa, 0x48}) &&
                entry(ammoNowRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0xfa, 0x48, 0x8b,
                                   0xd9, 0x33}) &&
                entry(ammoMostRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0xda, 0xe8, 0x03, 0x85, 0x31, 0x01,
                                    0x48, 0x85, 0xc0}) &&
                entry(hasAmmoRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30, 0x0f, 0x29, 0x74,
                                   0x24, 0x20, 0x41}) &&
                entry(inventoryOfRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x81, 0xc1, 0x18, 0x01, 0x00, 0x00,
                                       0xe8, 0x3e, 0x9d}) &&
                entry(offeredRva, {0x48, 0x83, 0xec, 0x28, 0x83, 0xba, 0x88, 0x00, 0x00, 0x00, 0x03, 0x75, 0x13,
                                   0x48, 0x8b, 0xca}) &&
                entry(requestRva, {0xc7, 0x41, 0x08, 0x4c, 0xab, 0xac, 0x6d, 0x48, 0x8b, 0xc1, 0xc7, 0x01, 0xff,
                                   0xff, 0xff, 0xff}) &&
                entry(equipRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
                                 0x18, 0x48}) &&
                entry(itemCountRva, {0x8b, 0x81, 0x14, 0x01, 0x00, 0x00, 0xc3}) &&
                entry(itemAtRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x70, 0x48, 0x8b, 0xf9, 0x8b,
                                  0xda, 0x48}) &&
                entry(canEquipRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x83, 0xc1, 0x50,
                                    0xe8, 0x4e, 0x9c}) &&
                table(equipInventoryTable, 0x30, canEquipRva) && table(equipInventoryTable, 0x1b0, itemAtRva) &&
                table(equipInventoryTable, 0x3a0, itemCountRva);
            for (size_t k = 0; k < std::size(gadgetTables); ++k)
                ready = ready && table(gadgetTables[k], 0x188, ammoNowRva) && table(gadgetTables[k], 0x190, ammoMostRva) &&
                        table(gadgetTables[k], 0x180, hasAmmoRva) && table(gadgetTables[k], 0x100, gadgetFills[k]) &&
                        table(gadgetTables[k], 0x108, weaponEventRva) && table(gadgetTables[k], 0x160, muzzleRva);
            gadgetsReady = ready;
        }
        // The webbing and the pushes go out through the damage system's and
        // the props' main-thread hook; the web grab or the fists may have
        // started it already.
        if (const auto bodies = native_bodies::start(base)) {
            result = bodies;
            break;
        }
        if (!hooked) {
            const auto s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
                result = 9700 + s;
                break;
            }
            hooks[0] = reinterpret_cast<void*>(base + frameRva);
            hooks[1] = reinterpret_cast<void*>(base + muzzleRva);
            if (const auto c = MH_CreateHook(hooks[0], reinterpret_cast<void*>(frame),
                                             reinterpret_cast<void**>(&originalFrame));
                c != MH_OK) {
                result = 9710 + c;
                break;
            }
            if (const auto c = MH_CreateHook(hooks[1], reinterpret_cast<void*>(muzzle),
                                             reinterpret_cast<void**>(&originalMuzzle));
                c != MH_OK) {
                MH_RemoveHook(hooks[0]);
                result = 9740 + c;
                break;
            }
            hooks[2] = reinterpret_cast<void*>(base + shotEventRva);
            if (const auto c = MH_CreateHook(hooks[2], reinterpret_cast<void*>(shotEvent),
                                             reinterpret_cast<void**>(&originalShotEvent));
                c != MH_OK) {
                MH_RemoveHook(hooks[1]);
                MH_RemoveHook(hooks[0]);
                result = 9750 + c;
                break;
            }
            hooked = true;
        }
        AcquireSRWLockExclusive(&updating);
        shooter.reset();
        ReleaseSRWLockExclusive(&updating);
        AcquireSRWLockExclusive(&queueLock);
        queued = 0;
        ReleaseSRWLockExclusive(&queueLock);
        AcquireSRWLockExclusive(&flyingLock);
        for (auto& f : flying)
            f = {};
        ReleaseSRWLockExclusive(&flyingLock);
        gadgets.start();
        // Thugs and devices to aim at and to web; props a shot may knock.
        watch.start(base, 1u << static_cast<unsigned>(game_targets::Kind::bot) |
                              1u << static_cast<unsigned>(game_targets::Kind::throwable) |
                              1u << static_cast<unsigned>(game_targets::Kind::device));
        // All three at once, so a frame that fires finds the muzzle and the
        // collisions hooked; under one freeze of the game's threads
        // (stereo_probe.cpp says why).
        for (auto* hook : hooks)
            if (const auto s = MH_QueueEnableHook(hook); s != MH_OK && !result)
                result = 9770 + s;
        if (const auto s = result ? MH_OK : MH_ApplyQueued(); s != MH_OK)
            result = 9770 + s;
        if (result) {
            for (auto* hook : hooks)
                MH_QueueDisableHook(hook);
            MH_ApplyQueued();
            watch.stop();
            gadgets.stop();
            break;
        }
        enabled = true;
    } while (false);
    publish([&](Data& d) {
        const auto sequence = d.sequence;
        if (!enabled || result) {
            d = {};
            d.sequence = sequence;
        }
        d.status = result ? 3 : 1;
        d.error = result;
    });
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
uint32_t game_shooter::stop() {
    AcquireSRWLockExclusive(&lifecycle);
    enabled = false;
    uint32_t result{};
    if (hooked)
        for (auto* hook : hooks)
            if (const auto s = MH_DisableHook(hook); s != MH_OK && s != MH_ERROR_DISABLED && !result)
                result = 9800 + s;
    // A frame in flight finishes its shots first.
    const auto until = GetTickCount64() + 2000;
    while (active && GetTickCount64() < until)
        Sleep(1);
    if (active && !result)
        result = 9901;
    // An update in flight finishes first.
    AcquireSRWLockExclusive(&updating);
    shooter.reset();
    ReleaseSRWLockExclusive(&updating);
    AcquireSRWLockExclusive(&queueLock);
    queued = 0;
    ReleaseSRWLockExclusive(&queueLock);
    watch.stop();
    gadgets.stop();
    publish([](Data& d) { d.status = 0; });
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
bool game_shooter::running() {
    return enabled.load(std::memory_order_relaxed);
}
void game_shooter::update(float seconds, const Input& in, uint32_t busy, uint64_t record, Vec3 feet,
                          const WorldQueries& world) {
    if (!enabled.load(std::memory_order_relaxed) || !TryAcquireSRWLockShared(&updating))
        return;
    struct Release {
        ~Release() {
            ReleaseSRWLockShared(&updating);
        }
    } release;
    if (!enabled || !std::isfinite(seconds) || seconds <= 0)
        return;
    if (finite(feet))
        gadgets.around(feet);
    if (record)
        heroRecord.store(record, std::memory_order_relaxed);
    std::array<ShooterHand, 2> hands{};
    for (int i = 0; i < 2; ++i) {
        const auto& h = in.hands[static_cast<size_t>(i)];
        hands[static_cast<size_t>(i)] = {in.focused && h.tracked, h.aim, h.trigger, ((busy >> i) & 1) != 0};
    }
    const auto pulls = shooter.update(seconds, hands);
    uint64_t requested{}, dropped{};
    if (pulls) {
        // The enemies a pull may aim at, where they stand now: never a
        // civilian or the police, nor a thug who is down. And the devices
        // that stand, at their own transforms (a turret's: its guns' middle).
        watch.current(bots);
        targets.clear();
        for (const auto& b : bots) {
            Vec3 at{};
            if (!game_targets::position(b, at))
                continue;
            if (game_targets::fighting(base, b))
                targets.push_back(
                    {b.record, at + Vec3{0, b.size.height(chest), 0}, b.size.width(botRadius)});
            else if (game_targets::standing(b) && (b.traits & game_targets::webbable) && b.record != record)
                targets.push_back({b.record, at, deviceRadius});
        }
        // EYE AIM: the eyes pick what a ball goes to, from the hand that pulled.
        const ShooterGaze eyes{in.gazeOrigin, in.gaze};
        const ShooterGaze* gaze = in.gazeShots && length(in.gaze) > .5f ? &eyes : nullptr;
        for (int i = 0; i < 2; ++i) {
            if (!((pulls >> i) & 1))
                continue;
            const auto& aim = in.hands[static_cast<size_t>(i)].aim;
            auto shot = shooter.aim(i, aim, targets, world, gaze);
            if (shot.target) {
                // One the game has let go of meanwhile is no target.
                bool live{};
                for (const auto& b : bots)
                    live |= b.record == shot.target && game_targets::live(base, b);
                if (!live)
                    shot = shooter.aim(i, aim, {}, world, gaze);
            }
            ++requested;
            if (!enqueue({shot, record, feet, GetTickCount64()}))
                ++dropped;
        }
    }
    publish([&](Data& d) {
        d.status = 2;
        ++d.samples;
        d.requested += requested;
        d.dropped += dropped;
        if (pulls)
            d.bots = static_cast<uint32_t>(targets.size());
    });
}
void game_shooter::gadgetWheel(bool fire, uint16_t equip) {
    gadgetFire = fire;
    if (equip)
        equipWanted = equip;
}
bool game_shooter::webbedUp(uint64_t record) {
    if (!record)
        return false;
    const auto now = GetTickCount64();
    AcquireSRWLockShared(&webbingLock);
    bool up{};
    for (const auto& w : webbings)
        up |= w.record == record && w.upAt && now - w.upAt <= static_cast<uint64_t>(webbedSeconds * 1000);
    ReleaseSRWLockShared(&webbingLock);
    return up;
}
void game_shooter::cancel() {
    if (!enabled.load(std::memory_order_relaxed) || !TryAcquireSRWLockShared(&updating))
        return;
    shooter.reset();
    ReleaseSRWLockShared(&updating);
}
Data game_shooter::data() {
    AcquireSRWLockShared(&output);
    auto out = SpidyShooterData;
    ReleaseSRWLockShared(&output);
    return out;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyShooterStart(void* input) {
    Config c{};
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x53484f43 || c.version != 1 ||
        c.bytes != sizeof(c) || c.pid != GetCurrentProcessId() ||
        c.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)))
        return 9501;
    return game_shooter::start(c.base);
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyShooterStop(void*) {
    return game_shooter::stop();
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyShooterSample(void* out) {
    const auto sample = game_shooter::data();
    __try {
        std::memcpy(out, &sample, sizeof(sample));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9503;
    }
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyShooterGadgets(void* input) {
    GadgetCommand g{};
    if (!read(reinterpret_cast<uintptr_t>(input), &g, sizeof(g)) || g.magic != 0x53484f47 || g.version != 1 ||
        g.bytes != sizeof(g) || g.fire > 1)
        return 9508;
    game_shooter::gadgetWheel(g.fire != 0, g.equip);
    return 0;
}
// One shot from a probe, fired at the next frame: 0 once the game fired it.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyShooterTest(void* input) {
    Test t{};
    if (!read(reinterpret_cast<uintptr_t>(input), &t, sizeof(t)) || t.magic != 0x53484f54 || t.version != 1 ||
        t.bytes != sizeof(t) || t.hand > 1 || !finite(t.origin) || !finite(t.aimPoint) ||
        length(t.aimPoint - t.origin) < .5f)
        return 9502;
    if (!game_shooter::running())
        return 9504;
    const auto before = game_shooter::data();
    ShotRequest shot;
    shot.hand = static_cast<int>(t.hand);
    shot.origin = t.origin;
    shot.aimPoint = t.aimPoint;
    shot.direction = normalized(t.aimPoint - t.origin);
    shot.target = t.target;
    shot.surface = true;
    if (!enqueue({shot, 0, {}, GetTickCount64()}))
        return 9505;
    const auto limit = GetTickCount64() + 1000;
    for (;;) {
        const auto now = game_shooter::data();
        if (now.fired > before.fired)
            return 0;
        if (now.dropped > before.dropped)
            return 9506; // no gadget, no player, or the game refused it
        if (GetTickCount64() >= limit)
            return 9507; // no frame: paused, or loading
        Sleep(5);
    }
}
