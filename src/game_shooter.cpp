// The web shooter in the game: pulls from the swing's input samples, the
// game's own web-shooter shots fired through the hero's gadget on the main
// thread (game_shooter.hpp).
#include "spidy/game_shooter.hpp"
#include "spidy/game_targets.hpp"
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
using Frame = uint64_t (*)(void*, float, uint64_t, uint64_t);
using Muzzle = float* (*)(void*, float*, uint32_t);
using FillEvent = uint32_t (*)(void*, uint8_t*);
using FireEvent = void* (*)(void*, uint8_t*);
using NewShotId = uint32_t (*)(void*);
using MakeRef = uint64_t* (*)(uint64_t*, void*);
// The header of the game's own fire events (+0 to +9; the weapon reads
// none of it).
constexpr uint8_t eventHeader[10] = {0x35, 0x20, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x49, 0x9c};
// A pull waits this long for the main thread at most; a paused game drops it.
constexpr uint64_t leaseMs = 250;
// A thug's aim point above his feet, and how wide he is to the aim assist.
constexpr float chest = 1.15f, botRadius = .45f;
// The gadget belongs to the player: its actor is this close to the player's.
constexpr float gadgetReach = 10;

uintptr_t base{};
std::atomic<bool> enabled{};
std::atomic<unsigned> active{};
std::atomic<uint64_t> frames{};
SRWLOCK lifecycle = SRWLOCK_INIT, updating = SRWLOCK_INIT, queueLock = SRWLOCK_INIT, output = SRWLOCK_INIT;
bool hooked{};
void* hooks[2]{};
Frame originalFrame{};
Muzzle originalMuzzle{};
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
bool fill(uintptr_t weapon, uint8_t* event) {
    __try {
        reinterpret_cast<FillEvent>(base + fillEventRva)(reinterpret_cast<void*>(weapon), event);
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
// One shot through the gadget, as its R1 fires one. False when there is no
// gadget or no player to fire it, or the game refused it.
struct Fired {
    uint64_t shot{};
    bool resolved{};
};
bool fireNow(const Pending& p, Fired& out) {
    const auto weapon = gadgets.current();
    if (!liveGadget(weapon)) {
        gadgets.lost();
        return false;
    }
    // The fire reads the weapon's setup (+0x48) and SpawnShot the actor at
    // +0x5b0 without asking whether there is one.
    if (!pointer(weapon + 0x48) || !actorOf(value<uint32_t>(weapon + 0x5b0)))
        return false;
    // The hand where it is now, as far from the player as it was in the sample.
    Vec3 origin = p.shot.origin, feet{};
    if (p.record && liveRecord(p.record) && positionOf(p.record, feet) && length(feet - p.feet) < 20)
        origin += feet - p.feet;
    const Vec3 forward = normalized(p.shot.aimPoint - origin);
    if (!finite(origin) || !finite(p.shot.aimPoint) || length(forward) < .5f)
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
    out.shot = resolve(value<uint32_t>(weapon + 0x518 + 4ull * emitter));
    return true;
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
            if (!ok) {
                ++d.dropped;
                return;
            }
            ++d.fired;
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
    if (!enabled.load(std::memory_order_relaxed))
        return result;
    ++active;
    if (enabled) {
        ++frames;
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
            !slot(0x160, muzzleRva)) {
            result = 9601;
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
            hooked = true;
        }
        AcquireSRWLockExclusive(&updating);
        shooter.reset();
        ReleaseSRWLockExclusive(&updating);
        AcquireSRWLockExclusive(&queueLock);
        queued = 0;
        ReleaseSRWLockExclusive(&queueLock);
        gadgets.start();
        watch.start(base, 1u << static_cast<unsigned>(game_targets::Kind::bot));
        // The muzzle first: a frame may fire as soon as it is hooked.
        for (auto* hook : {hooks[1], hooks[0]})
            if (const auto s = MH_EnableHook(hook); s != MH_OK && !result)
                result = 9770 + s;
        if (result) {
            for (auto* hook : hooks)
                MH_DisableHook(hook);
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
    std::array<ShooterHand, 2> hands{};
    for (int i = 0; i < 2; ++i) {
        const auto& h = in.hands[static_cast<size_t>(i)];
        hands[static_cast<size_t>(i)] = {in.focused && h.tracked, h.aim, h.trigger, ((busy >> i) & 1) != 0};
    }
    const auto pulls = shooter.update(seconds, hands);
    uint64_t requested{}, dropped{};
    if (pulls) {
        // The thugs a pull may aim at, where they stand now.
        watch.current(bots);
        targets.clear();
        for (const auto& b : bots)
            if (Vec3 at{}; game_targets::position(b, at))
                targets.push_back({b.record, at + Vec3{0, chest, 0}, botRadius});
        for (int i = 0; i < 2; ++i) {
            if (!((pulls >> i) & 1))
                continue;
            const auto& aim = in.hands[static_cast<size_t>(i)].aim;
            auto shot = shooter.aim(i, aim, targets, world);
            if (shot.target) {
                // One the game has let go of meanwhile is no target.
                bool live{};
                for (const auto& b : bots)
                    live |= b.record == shot.target && game_targets::live(base, b);
                if (!live)
                    shot = shooter.aim(i, aim, {}, world);
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
