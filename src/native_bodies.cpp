// Frees props the web holds and moves them, and flings bots, inside
// hknpWorld::preCollide on the main thread (see native_bodies.hpp).
#include "spidy/native_bodies.hpp"
#include "spidy/game_time.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <windows.h>
using namespace spidy;
using namespace spidy::native_bodies;
namespace {
using PreCollide = void (*)(void*, const void*);
using SetFreebody = void (*)(void*);
using SetMode = void (*)(void*, uint32_t, uint16_t, int);
using Rebuild = void (*)(void*, void*);
using RebuildAsset = void (*)(void*, void*, uintptr_t);
using ModelOverride = uintptr_t (*)(void*);
using SetVelocity = void (*)(void*, uint32_t, const float*, int);
using SetMatrix = void (*)(void*, const float*, void*);
using RequestState = bool (*)(void*, void*, void*);
using MakeParams = void* (*)(void*);
using StateType = void* (*)();
constexpr uintptr_t preCollideRva = 0x2e54300, setFreebodyRva = 0x1810530, setModeRva = 0x1835780,
                    rebuildRva = 0x1830930, rebuildAssetRva = 0x1834b70, modelOverrideRva = 0x1776050,
                    setVelocityRva = 0x2e48b80, setSpinRva = 0x2e48830, setMatrixRva = 0x191c0e0,
                    requestStateRva = 0x20e51c0, flungParamsRva = 0x556040, flungTypeRva = 0x300440;
constexpr uintptr_t worldGlobal = 0x78939e8, physicsGlobal = 0x609a570, recordTable = 0x1ea98,
                    physicsComponent = 0x3d0a460, actorTable = 0x7a436e8, actorCount = 0x7a43704,
                    registryTable = 0x7a44320, registryCount = 0x7a44340, stateMachine = 0x4f843d0,
                    flungState = 0x3832fd8, flungDriver = 0x386c448;
// The DamageSystem, its direct request (target, hit direction, point,
// normal), the request it hands out when its pool is full, and the actor
// table actor handles index.
constexpr uintptr_t damageSystem = 0x62a4ec0, directDamageRva = 0x1eb6d60, spareRequest = 0x123868,
                    actorRecords = 0x7a44380, actorRecordCount = 0x7a4439c;
using DirectDamage = uint8_t* (*)(void*, const uint32_t*, const Vec3*, const Vec3*, const Vec3*);
// A status entry in a request's StatusData (request, amount, type, duration,
// action count; -1 leaves the count unset), as the game's shot damage adds one.
constexpr uintptr_t addStatusRva = 0x1ed20e0;
using AddStatus = void (*)(void*, float, int32_t, float, float);
constexpr uint16_t debris = 3; // the mode the game's own throws use
constexpr int activate = 0;    // hknpActivationMode::ACTIVATE
constexpr unsigned maxSystemBodies = 16;
// A freed prop stays watched this long after the web last moved it.
constexpr uint64_t followMs = 30000;
// A prop let go of is kept in real time until it has lain still this long
// (slower than restSpeed, turning slower than restSpin), or the game puts
// it to sleep, or it has flown for maxFlight.
constexpr float restSeconds = .3f, restSpeed = .3f, restSpin = 1, maxFlight = 20;
// No body Spidy moves goes faster than this.
constexpr float maxSpeed = 45;
PreCollide original{};
void* hook{};
bool hooked{};
uintptr_t base{};
std::atomic<bool> enabled{};
std::atomic<unsigned> active{};
SRWLOCK lifecycle = SRWLOCK_INIT, lock = SRWLOCK_INIT;
// The web on an actor: a Rope or Follow command while its lease lasts, and a
// Launch to apply once.
struct Slot {
    uint64_t actor{}, component{};
    TargetCommand control{}, launch{};
    uint64_t deadline{};
    bool controlling{}, launching{};
};
Slot table[slots];
// A bot whose flight to steer at the next step, or to fling when `request`.
struct Fling {
    uint64_t machine{}, record{};
    Vec3 velocity{};
    bool pending{}, request{};
};
Fling flings[slots];
// Damage to issue at the next step, in order, with its ticket.
struct PendingDamage {
    Damage damage{};
    uint64_t ticket{};
};
constexpr unsigned damageSlots = 16;
PendingDamage pendingDamage[damageSlots];
unsigned pendingDamages{};
uint64_t nextTicket{}, issuedTicket{};
// A prop the web freed: its root body as the latest step found it.
struct Followed {
    uint64_t actor{}, component{}, until{};
    bool freed{};
    uint64_t step{};       // the snapshot's physics step, 0 before any
    Vec3 centre{};         // before that step
    Vec3 measured{};       // velocity in the world's time before that step
    Vec3 next{};           // velocity in the world's time through that step, contacts aside
    Vec3 gravity{};        // its motion's gravity, metres per second squared
    float dt{}, real{};    // the step's physics length and the world's time in it
    bool dynamic{}, driven{};
    // Spidy keeps it in the world's time: the web moves it, or it flies free
    // after the web, until it rests. Seconds flying free, and lying still.
    bool owned{};
    float flying{}, resting{};
    // Steps the rebuilt prop has rested since the game started moving its
    // instance after its body. The game takes the offset between the two
    // once, at its first sync: moved before then, the prop was drawn 5 m
    // from where its body was held.
    uint32_t settled{};
};
Followed followed[16];
Counters totals{};
// Real time per physics step, smoothed over a few steps; the world's time in
// it (real time at the game's time scale: its own slow motions, and Spidy's);
// and physics time per second of the world's (the physics step over it), for
// this step and the one before: a body Spidy moved in that one has its
// velocity in that step's time.
float realStep{}, worldStep{}, scale = 1, previousScale = 1;
LARGE_INTEGER lastStep{}, frequency{};

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
bool write(uintptr_t p, const void* data, size_t n) {
    __try {
        if (p < 0x10000)
            return false;
        std::memcpy(reinterpret_cast<void*>(p), data, n);
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
// The game's time per real second: the double its clock multiplies each
// frame's real time by (game_time.hpp). 1 at normal speed, less while the
// game or Spidy slows it; 1 if it cannot be read.
float worldTime() {
    double clock{};
    if (!read(base + game_time::clockScaleRva, &clock, sizeof(clock)) || !std::isfinite(clock) || clock <= 0)
        return 1;
    return static_cast<float>(std::clamp(clock, .01, 4.));
}
template <class T> T value(uintptr_t p) {
    T v{};
    read(p, &v, sizeof(v));
    return v;
}
// The actor still holds its handle, and the handle still names it.
bool liveActor(uintptr_t actor, uint32_t& handle) {
    handle = value<uint32_t>(actor + 0x64);
    const uint32_t index = handle & 0xffffff;
    const auto entries = pointer(base + actorTable);
    const auto count = value<int32_t>(base + actorCount);
    return actor && (handle >> 24) && entries && count > 0 && index < static_cast<uint32_t>(count) &&
           pointer(entries + index * 16ull) == actor &&
           value<uint8_t>(entries + index * 16ull + 8) == handle >> 24 && !value<uint8_t>(actor + 0x5f);
}
// An actor record's handle, if the record is still the live one its index
// names in the actor table; 0 otherwise.
uint32_t actorHandle(uintptr_t record) {
    const auto index = value<uint32_t>(record + 0xc) & 0xfffff;
    const auto generation = value<uint16_t>(record + 0x10) & 0x7ff;
    const auto records = pointer(base + actorRecords);
    const auto count = value<uint32_t>(base + actorRecordCount);
    if (!record || !generation || !records || index >= count || records + index * 0xc0ull != record)
        return 0;
    return static_cast<uint32_t>(generation) << 20 | index;
}
// One direct damage request, filled with the fields asked for.
bool damageNow(const Damage& d) {
    const uint32_t victim = actorHandle(d.victim);
    const Vec3 direction = normalized(d.direction), normal = normalized(d.normal);
    if (!victim || !finite(d.point) || length(direction) < .5f || !std::isfinite(d.amount))
        return false;
    const auto system = reinterpret_cast<void*>(base + damageSystem);
    uint8_t* request{};
    __try {
        request = reinterpret_cast<DirectDamage>(base + directDamageRva)(
            system, &victim, &direction, &d.point, length(normal) > .5f ? &normal : &direction);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!request || reinterpret_cast<uintptr_t>(request) == base + damageSystem + spareRequest)
        return false; // the pool was full: the system handed out its spare
    uint64_t fields{};
    const auto set = [&](uint32_t offset, const void* v, int bit) {
        std::memcpy(request + offset, v, 4);
        fields |= 1ull << bit;
    };
    if (const uint32_t damager = d.damager ? actorHandle(d.damager) : 0)
        set(0x138, &damager, 8);
    set(0x13c, &d.type, 9);
    set(0x144, &d.amount, 11);
    if (d.knockback >= 0)
        set(0x150, &d.knockback, 14);
    if (d.knockbackAmount >= 0)
        set(0x154, &d.knockbackAmount, 15);
    if (d.impulse >= 0)
        set(0x190, &d.impulse, 23);
    if (d.hash)
        set(0x19c, &d.hash, 26);
    *reinterpret_cast<uint64_t*>(request + 8) |= fields;
    *reinterpret_cast<uint64_t*>(request + 0x18) |= fields;
    if (d.statusType >= 0 && d.statusAmount > 0 && std::isfinite(d.statusAmount) && std::isfinite(d.statusDuration)) {
        __try {
            reinterpret_cast<AddStatus>(base + addStatusRva)(request, d.statusAmount, d.statusType,
                                                             d.statusDuration, -1.f);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // The request goes out without its status.
        }
    }
    return true;
}
// A registered component of this vtable whose record is `record`.
bool registered(uintptr_t component, uintptr_t vtable, uintptr_t record) {
    const auto handle = value<uint32_t>(component + 0x14);
    const auto entries = pointer(base + registryTable);
    const auto count = value<int32_t>(base + registryCount);
    const uint32_t index = handle & 0xfffff;
    return component && entries && count > 0 && index < static_cast<uint32_t>(count) &&
           pointer(entries + index * 16ull) == component &&
           value<uint32_t>(entries + index * 16ull + 8) == handle >> 20 &&
           pointer(component) == base + vtable && pointer(component + 8) == record;
}
// The actor's PhysicsComponent, still registered on it.
bool liveComponent(uintptr_t component, uintptr_t actor) {
    const auto record = pointer(component + 8);
    return record && pointer(record) == actor && registered(component, physicsComponent, record);
}
struct Body {
    uint32_t id{}, motion{}, flags{};
    uint64_t user{};
    bool valid{};
};
Body body(uintptr_t world, uint32_t id) {
    Body b{id};
    const auto bodies = pointer(world + 0x28);
    const auto capacity = value<uint32_t>(world + 0x30);
    const auto at = bodies + static_cast<uintptr_t>(id & 0xffffff) * 0xc0;
    uint8_t raw[0x60]{};
    if (!bodies || (id & 0xffffff) >= capacity || capacity > 0x1000000 || !read(at + 0x40, raw, sizeof(raw)))
        return b;
    std::memcpy(&b.motion, raw, 4);
    std::memcpy(&b.flags, raw + 4, 4);
    std::memcpy(&b.user, raw + 0x58, 8);
    uint32_t own{}, broadPhase{};
    std::memcpy(&own, raw + 0x30, 4);
    std::memcpy(&broadPhase, raw + 0x38, 4);
    b.valid = own == id && (b.flags & 3) && broadPhase != 0xffffffff;
    return b;
}
// Simulated: a motion of its own, neither static nor keyframed.
bool dynamic(const Body& b) {
    return b.valid && (b.flags & 7) == 2 && b.motion;
}
// The gravity a step gives this motion: the world's, times its motion
// properties' gravity factor (hknpMotionProperties +8).
Vec3 gravityOf(uintptr_t world, uintptr_t motion) {
    Vec3 g{};
    read(world + 0xa00, &g, sizeof(g));
    const auto properties = value<uint16_t>(motion + 0x38);
    const auto entries = pointer(pointer(world + 0xa20) + 0x30);
    float factor = 1;
    if (entries && properties != 0xffff)
        read(entries + properties * 0x70ull + 8, &factor, sizeof(factor));
    return std::isfinite(factor) && finite(g) ? g * factor : Vec3{};
}
// A motion's angular velocity about world axes: it keeps it about its body's
// axes (+0x50), turned by its orientation (+0x10, x y z w), as
// setBodyAngularVelocity (2e48830) reads and writes it.
Vec3 spinOf(uintptr_t motion) {
    Quat orientation{};
    Vec3 local{};
    if (!read(motion + 0x10, &orientation, sizeof(orientation)) || !read(motion + 0x50, &local, sizeof(local)))
        return {};
    const Vec3 spin = orientation.rotate(local);
    return finite(spin) ? spin : Vec3{};
}
uintptr_t records() {
    return pointer(base + physicsGlobal) + recordTable;
}
// The game's PostStep moves the instance only of an actor with a keyframe
// record: the s16 at +0x5e of its physics record (indexed by bytes 4-5 of a
// body's userData), -1 for none.
bool keyframed(const Body& b) {
    const auto index = static_cast<int16_t>(b.user >> 32);
    const auto entries = pointer(records() + 0x28);
    return index >= 0 && entries && value<int16_t>(entries + index * 0x130ull + 0x5e) >= 0;
}
// The body the game draws the actor from: its keyframe record's root (+0x9c,
// an index into the physics system's bodies; records at [M + 0x23468], 0xa0
// each). A prop's bodies are not joined: they knock into each other and drift
// apart, and the primary one was drawn 1.4 m from where the web held it.
// The actor's keyframe record (pool entry), or 0 for none.
uintptr_t keyframeRecord(const Body& primary) {
    const auto index = static_cast<int16_t>(primary.user >> 32);
    const auto entries = pointer(records() + 0x28);
    const auto kf = index >= 0 && entries ? value<int16_t>(entries + index * 0x130ull + 0x5e) : int16_t{-1};
    const auto pool = pointer(pointer(base + physicsGlobal) + 0x23468);
    return kf >= 0 && pool ? pool + kf * 0xa0ull : 0;
}
Body rootBody(uintptr_t world, uintptr_t actor, const Body& primary) {
    const auto record = keyframeRecord(primary);
    const auto system = pointer(actor + 0xe0);
    const auto count = value<uint32_t>(system + 0x30);
    if (!record || !system)
        return primary;
    // The game reads the root as an s16 (182bd60).
    const auto root = value<int16_t>(record + 0x9c);
    if (root < 0 || static_cast<uint32_t>(root) >= count || count > maxSystemBodies)
        return primary;
    const auto b = body(world, value<uint32_t>(pointer(system + 0x28) + root * 4ull));
    return b.valid ? b : primary;
}
// As the game's own throw: free, then debris. Then build it again in that
// mode, so it gets the keyframe record its instance follows by. All three are
// record changes the next flush applies, before the next step.
void freeProp(Followed& f, uint32_t handle) {
    const auto physics = reinterpret_cast<void*>(records());
    reinterpret_cast<SetFreebody>(base + setFreebodyRva)(reinterpret_cast<void*>(f.component));
    reinterpret_cast<SetMode>(base + setModeRva)(physics, handle, debris, -1);
    const auto actor = reinterpret_cast<void*>(f.actor);
    if (value<int16_t>(f.actor + 0xb6) < 0)
        reinterpret_cast<Rebuild>(base + rebuildRva)(physics, actor);
    else
        reinterpret_cast<RebuildAsset>(base + rebuildAssetRva)(
            physics, actor, reinterpret_cast<ModelOverride>(base + modelOverrideRva)(actor));
    f.freed = true;
    ++totals.frees;
    ++totals.rebuilds;
}
// The machine's driver (layer 1) when it is the local driver of BotStateFlung.
uintptr_t flight(uintptr_t machine) {
    const auto driver = pointer(machine + 0x98);
    return value<uint8_t>(machine + 0xa8) && driver && pointer(driver) == base + flungDriver ? driver : 0;
}
// Fling a bot, or steer the flight it is already on.
void flingNow(const Fling& f) {
    if (!registered(f.machine, stateMachine, f.record) ||
        pointer(pointer(f.machine) + 13 * 8) != base + requestStateRva) {
        ++totals.rejected;
        return;
    }
    if (const auto driver = flight(f.machine)) {
        write(driver + 0x94, &f.velocity, sizeof(Vec3));
        ++totals.steers;
        return;
    }
    if (!f.request)
        return;
    alignas(16) uint8_t params[0x120]{};
    reinterpret_cast<MakeParams>(base + flungParamsRva)(params);
    std::memcpy(params + 0x44, &f.velocity, sizeof(Vec3));
    void* type = reinterpret_cast<StateType>(base + flungTypeRva)();
    const auto request = reinterpret_cast<RequestState>(base + requestStateRva);
    if (type && request(reinterpret_cast<void*>(f.machine), type, params))
        ++totals.flings;
    else
        ++totals.rejected;
}
void apply(uintptr_t world, const void* input) {
    // Damage first: the DamageSystem is gameplay's, not the physics world's,
    // and the game runs its own code in it, so it is issued outside the lock.
    PendingDamage blows[damageSlots]{};
    AcquireSRWLockExclusive(&lock);
    const unsigned blowCount = pendingDamages;
    std::copy(pendingDamage, pendingDamage + blowCount, blows);
    pendingDamages = 0;
    ReleaseSRWLockExclusive(&lock);
    if (blowCount) {
        uint64_t issued{}, dropped{}, last{};
        for (unsigned i = 0; i < blowCount; ++i) {
            (damageNow(blows[i].damage) ? issued : dropped)++;
            last = blows[i].ticket;
        }
        AcquireSRWLockExclusive(&lock);
        totals.damages += issued;
        totals.damageDropped += dropped;
        issuedTicket = std::max(issuedTicket, last);
        ReleaseSRWLockExclusive(&lock);
    }
    float dt{};
    if (!read(reinterpret_cast<uintptr_t>(input), &dt, sizeof(dt)) || !std::isfinite(dt) || dt <= 0 ||
        dt > .2f)
        return;
    // The world API may only change the world while it is idle (stage 1).
    if (value<uint32_t>(world + 0x9f8) != 1)
        return;
    Fling due[slots]{};
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < slots; ++i) {
        due[i] = flings[i];
        flings[i].pending = false;
    }
    ReleaseSRWLockExclusive(&lock);
    // State requests outside the lock: the game runs its own code in them.
    for (const auto& f : due)
        if (f.pending)
            flingNow(f);
    const auto motions = pointer(world + 0x148);
    const auto motionCount = value<uint32_t>(world + 0x150);
    const auto setVelocity = reinterpret_cast<SetVelocity>(base + setVelocityRva);
    const auto setSpin = reinterpret_cast<SetVelocity>(base + setSpinRva);
    const auto setMatrix = reinterpret_cast<SetMatrix>(base + setMatrixRva);
    const auto bodies = pointer(world + 0x28);
    const auto now = GetTickCount64();
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    AcquireSRWLockExclusive(&lock);
    ++totals.steps;
    // The real time this step stands for: since the previous one, smoothed;
    // a pause or a first step counts as one physics step of real time.
    const double since = lastStep.QuadPart ? static_cast<double>(qpc.QuadPart - lastStep.QuadPart) /
                                                 static_cast<double>(frequency.QuadPart)
                                           : 0;
    lastStep = qpc;
    if (since > 1e-4 && since < .1)
        realStep = realStep > 0 ? realStep + (static_cast<float>(since) - realStep) * .2f
                                : static_cast<float>(since);
    else if (!(realStep > 0))
        realStep = dt;
    worldStep = realStep * worldTime();
    previousScale = scale;
    scale = std::clamp(dt / worldStep, .05f, 20.f);
    for (auto& slot : table) {
        if (slot.actor && slot.controlling && now >= slot.deadline) {
            slot.controlling = false;
            ++totals.expired;
        }
        if (slot.actor && !slot.controlling && !slot.launching)
            slot = {};
    }
    uint64_t flying{};
    for (auto& f : followed) {
        uint32_t handle{};
        if (!f.actor)
            continue;
        if (!liveActor(f.actor, handle)) {
            f = {};
            continue;
        }
        Slot* slot{};
        for (auto& s : table)
            if (s.actor == f.actor)
                slot = &s;
        auto primary = body(world, value<uint32_t>(f.actor + 0xe8));
        if (!f.freed && !dynamic(primary)) {
            if (slot && liveComponent(f.component, f.actor))
                freeProp(f, handle);
            f.step = 0;
            continue;
        }
        f.freed = true;
        const bool followedByGame = dynamic(primary) && keyframed(primary);
        f.settled = followedByGame ? f.settled + 1 : 0;
        // Rebuilt but not yet followed by the game: set the instance from
        // the body, where the last step left it.
        alignas(16) float pose[16]{}, shown[16]{};
        if (dynamic(primary) && !keyframed(primary) &&
            read(bodies + static_cast<uintptr_t>(primary.id & 0xffffff) * 0xc0, pose, sizeof(pose)) &&
            read(f.actor, shown, sizeof(shown)) && finite({pose[12], pose[13], pose[14]})) {
            float change{};
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 3; ++column)
                    change = std::max(change, std::abs(pose[row * 4 + column] - shown[row * 4 + column]));
            if (change > 1e-3f) {
                setMatrix(reinterpret_cast<void*>(f.actor), pose, reinterpret_cast<void*>(f.actor + 0x70));
                ++totals.follows;
            }
        }
        // The snapshot: the body the game draws it from, before this step.
        const auto drawn = rootBody(world, f.actor, primary);
        f.dynamic = dynamic(drawn) && drawn.motion < motionCount;
        f.driven = false;
        // Still until the game's first syncs are done, unless the game never
        // will and Spidy draws it itself.
        const bool ready = !keyframed(primary) || f.settled > 3;
        if (f.dynamic) {
            const auto motion = motions + drawn.motion * 0x80ull;
            Vec3 velocity{};
            read(motion, &f.centre, sizeof(Vec3));
            read(motion + 0x40, &velocity, sizeof(Vec3));
            f.gravity = gravityOf(world, motion);
            // Its real velocity: in the time of the step that gave it.
            f.measured = velocity * previousScale;
            const bool web = slot && ready && (slot->controlling || slot->launching);
            f.owned |= web;
            const TargetCommand* law = !web ? nullptr : slot->launching ? &slot->launch : &slot->control;
            const Vec3 next = limited(law ? advance(*law, f.centre, f.measured, worldStep, f.gravity)
                                          : f.measured + f.gravity * worldStep,
                                      maxSpeed);
            if (f.owned && ready && finite(next) && finite(f.measured)) {
                // What the web, or gravity alone, does to it in this step's
                // real time, from where the step before left it. Every body
                // of the prop changes alike; bodies sharing a motion change
                // once, and a body at rest that no web holds stays asleep.
                const Vec3 change = next - f.measured;
                if (web && slot->launching)
                    slot->launching = false;
                const auto system = pointer(f.actor + 0xe0);
                const auto count = std::min(value<uint32_t>(system + 0x30), maxSystemBodies);
                uint32_t ids[maxSystemBodies]{}, moved[maxSystemBodies]{};
                unsigned done{};
                if (system && count && read(pointer(system + 0x28), ids, count * 4ull))
                    for (uint32_t i = 0; i < count; ++i) {
                        const auto b = body(world, ids[i]);
                        if (!dynamic(b) || b.motion >= motionCount || (!web && !(b.flags & 8)) ||
                            std::find(moved, moved + done, b.motion) != moved + done)
                            continue;
                        moved[done++] = b.motion;
                        const auto m = motions + b.motion * 0x80ull;
                        Vec3 v{};
                        read(m + 0x40, &v, sizeof(v));
                        // Real velocity to physics time; the step then adds
                        // gravity in physics time, which this takes out.
                        const Vec3 linear =
                            limited(v * previousScale + change, maxSpeed) / scale - gravityOf(world, m) * dt;
                        const Vec3 spin = spinOf(m);
                        const Vec3 turned =
                            (law ? advanceSpin(*law, spin * previousScale, worldStep) : spin * previousScale) /
                            scale;
                        if (!finite(linear) || !finite(turned))
                            continue;
                        alignas(16) const float to[4] = {linear.x, linear.y, linear.z, 0};
                        setVelocity(reinterpret_cast<void*>(world), b.id, to, activate);
                        if (length(turned - spin) > 1e-3f) {
                            alignas(16) const float around[4] = {turned.x, turned.y, turned.z, 0};
                            setSpin(reinterpret_cast<void*>(world), b.id, around, activate);
                        }
                        ++totals.writes;
                    }
                f.next = next;
                f.driven = true;
                f.until = now + followMs;
                if (web) {
                    f.flying = f.resting = 0;
                } else {
                    ++flying;
                    f.flying += worldStep;
                    const bool still = length(f.measured) < restSpeed &&
                                       length(spinOf(motion) * previousScale) < restSpin;
                    f.resting = still ? f.resting + worldStep : 0;
                    if (f.resting >= restSeconds || f.flying >= maxFlight || !(drawn.flags & 8)) {
                        f.owned = false;
                        f.flying = f.resting = 0;
                        ++totals.rested;
                    }
                }
            } else {
                // The game's own physics, in its own time.
                f.next = (velocity + f.gravity * dt) * scale;
            }
        }
        f.dt = dt;
        f.real = worldStep;
        f.step = totals.steps;
        // Long still and asleep: no longer watched.
        if (!f.owned && now >= f.until && !(primary.flags & 8))
            f = {};
    }
    totals.flying = flying;
    ReleaseSRWLockExclusive(&lock);
}
void preCollide(void* world, const void* input) {
    struct Guard {
        Guard() {
            ++active;
        }
        ~Guard() {
            --active;
        }
    } guard;
    const auto address = reinterpret_cast<uintptr_t>(world);
    if (enabled && address && address == pointer(base + worldGlobal))
        apply(address, input);
    original(world, input);
}
bool entry(uintptr_t rva, std::initializer_list<uint8_t> expected) {
    uint8_t bytes[16]{};
    return expected.size() <= sizeof(bytes) && read(base + rva, bytes, expected.size()) &&
           !std::memcmp(bytes, expected.begin(), expected.size());
}
} // namespace

uint32_t native_bodies::start(uintptr_t gameBase) {
    AcquireSRWLockExclusive(&lifecycle);
    uint32_t result{};
    do {
        if (enabled)
            break;
        base = gameBase;
        if (!entry(preCollideRva, {0x41, 0x56, 0x48, 0x83, 0xec, 0x50, 0x48, 0x89, 0x5c, 0x24, 0x60, 0x48,
                                   0x89, 0x6c, 0x24, 0x68}) ||
            !entry(setFreebodyRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x81, 0xa1, 0x5c, 0x03, 0x00, 0x00,
                                    0x19, 0xfc, 0xff, 0xff}) ||
            !entry(setModeRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74,
                                0x24, 0x18, 0x57}) ||
            !entry(rebuildRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x42, 0x64,
                                0x48, 0x8b, 0xfa}) ||
            !entry(rebuildAssetRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x42,
                                     0x64, 0x49, 0x8b, 0xf8}) ||
            !entry(modelOverrideRva, {0x66, 0x83, 0xb9, 0xb6, 0x00, 0x00, 0x00, 0x00, 0x7d, 0x03, 0x33, 0xc0,
                                      0xc3, 0x48, 0x8b, 0xd1}) ||
            !entry(setVelocityRva, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x57, 0x48,
                                    0x83, 0xec, 0x20, 0x8b}) ||
            !entry(setSpinRva, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83,
                                0xec, 0x40, 0x8b}) ||
            !entry(setMatrixRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x40, 0x0f, 0x10, 0x02, 0x33, 0xc0, 0x4c,
                                  0x8b, 0xca, 0x0f, 0x10}) ||
            !entry(requestStateRva, {0x40, 0x55, 0x56, 0x57, 0x48, 0x81, 0xec, 0x70, 0x02, 0x00, 0x00, 0x49,
                                     0x8b, 0xf0, 0x48, 0x8b}) ||
            !entry(flungParamsRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
                                    0x83, 0xec, 0x20, 0x48}) ||
            !entry(flungTypeRva, {0x48, 0x83, 0xec, 0x28, 0x8b, 0x0d, 0x56, 0xad, 0xb1, 0x07, 0x65, 0x48,
                                  0x8b, 0x04, 0x25, 0x58}) ||
            !entry(directDamageRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89,
                                     0x74, 0x24, 0x18, 0x48}) ||
            !entry(addStatusRva, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x0f, 0x29, 0x74,
                                  0x24, 0x30, 0x41}) ||
            pointer(base + damageSystem) != base + 0x4f5db58) {
            result = 8001;
            break;
        }
        if (!hooked) {
            const auto s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
                result = 8100 + s;
                break;
            }
            hook = reinterpret_cast<void*>(base + preCollideRva);
            if (const auto c = MH_CreateHook(hook, reinterpret_cast<void*>(preCollide),
                                             reinterpret_cast<void**>(&original));
                c != MH_OK) {
                result = 8200 + c;
                break;
            }
            hooked = true;
        }
        AcquireSRWLockExclusive(&lock);
        for (auto& s : table)
            s = {};
        for (auto& f : flings)
            f = {};
        for (auto& f : followed)
            f = {};
        pendingDamages = 0;
        issuedTicket = nextTicket;
        totals = {};
        realStep = worldStep = 0;
        scale = previousScale = 1;
        lastStep = {};
        QueryPerformanceFrequency(&frequency);
        ReleaseSRWLockExclusive(&lock);
        enabled = true;
        if (const auto s = MH_EnableHook(hook); s != MH_OK) {
            enabled = false;
            result = 8300 + s;
        }
    } while (false);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
uint32_t native_bodies::stop() {
    AcquireSRWLockExclusive(&lifecycle);
    enabled = false;
    uint32_t result{};
    if (hooked)
        if (const auto s = MH_DisableHook(hook); s != MH_OK && s != MH_ERROR_DISABLED)
            result = 8400 + s;
    const auto until = GetTickCount64() + 2000;
    while (active && GetTickCount64() < until)
        Sleep(1);
    if (active)
        result = 8501;
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
bool native_bodies::drive(uint64_t actor, uint64_t component, const TargetCommand& command, uint32_t leaseMs) {
    if (!enabled || !actor)
        return false;
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64();
    Slot* slot{};
    for (auto& s : table)
        if (s.actor == actor)
            slot = &s;
    for (auto& s : table)
        if (!slot && !s.actor)
            slot = &s;
    if (slot) {
        if (slot->actor != actor)
            *slot = {actor, component};
        slot->component = component;
        if (command.mode == TargetCommand::Mode::Launch) {
            slot->launch = command;
            slot->launching = true;
            if (command.thrown)
                slot->controlling = false; // thrown: no web after it
        } else {
            slot->control = command;
            slot->controlling = true;
            slot->deadline = now + leaseMs;
        }
    }
    // Watched from now on: freed at the next step, and its body followed.
    Followed* f{};
    for (auto& candidate : followed)
        if (candidate.actor == actor)
            f = &candidate;
    for (auto& candidate : followed)
        if (!f && !candidate.actor)
            f = &candidate;
    if (!f) { // the longest unmoved gives way
        f = &followed[0];
        for (auto& candidate : followed)
            if (candidate.until < f->until)
                f = &candidate;
        *f = {};
    }
    if (!f->actor)
        *f = {actor, component};
    f->until = now + followMs;
    ReleaseSRWLockExclusive(&lock);
    return slot != nullptr;
}
void native_bodies::release(uint64_t actor) {
    AcquireSRWLockExclusive(&lock);
    for (auto& s : table)
        if (s.actor == actor) {
            s.controlling = false;
            if (!s.launching)
                s = {};
        }
    ReleaseSRWLockExclusive(&lock);
}
bool native_bodies::predicted(uint64_t actor, Vec3& centre, Vec3& velocity, Vec3& measured) {
    AcquireSRWLockShared(&lock);
    bool found{};
    for (const auto& f : followed)
        if (f.actor == actor && f.step && f.dynamic) {
            measured = f.measured;
            // Through the step in flight, in real time, as the web or
            // gravity moves it (contacts are not foreseen).
            velocity = f.next;
            centre = f.centre + velocity * f.real;
            found = finite(centre) && finite(velocity) && finite(measured);
        }
    ReleaseSRWLockShared(&lock);
    return found;
}
bool native_bodies::fling(uint64_t machine, uint64_t record, Vec3 velocity, bool request) {
    if (!enabled || !machine || !record || !finite(velocity))
        return false;
    AcquireSRWLockExclusive(&lock);
    Fling* slot{};
    for (auto& f : flings)
        if (f.machine == machine)
            slot = &f;
    for (auto& f : flings)
        if (!slot && !f.pending)
            slot = &f;
    if (slot)
        *slot = {machine, record, limited(velocity, 45), true, request};
    ReleaseSRWLockExclusive(&lock);
    return slot != nullptr;
}
uint64_t native_bodies::damage(const Damage& d) {
    if (!enabled || !d.victim || !finite(d.point) || !finite(d.direction) || !std::isfinite(d.amount) ||
        d.amount < 0 || d.knockback > 9)
        return 0;
    AcquireSRWLockExclusive(&lock);
    uint64_t ticket{};
    if (pendingDamages < damageSlots) {
        ticket = ++nextTicket;
        pendingDamage[pendingDamages++] = {d, ticket};
    }
    ReleaseSRWLockExclusive(&lock);
    return ticket;
}
uint64_t native_bodies::damageIssued() {
    AcquireSRWLockShared(&lock);
    const auto out = issuedTicket;
    ReleaseSRWLockShared(&lock);
    return out;
}
bool native_bodies::flung(uint64_t machine) {
    const auto state = pointer(machine + 0x70);
    return value<uint8_t>(machine + 0x80) && state && pointer(state) == base + flungState;
}
Counters native_bodies::counters() {
    AcquireSRWLockShared(&lock);
    const auto out = totals;
    ReleaseSRWLockShared(&lock);
    return out;
}
uint64_t native_bodies::physicsSteps(float& dt) {
    AcquireSRWLockShared(&lock);
    const auto steps = totals.steps;
    dt = worldStep;
    ReleaseSRWLockShared(&lock);
    return steps;
}
float native_bodies::timeScale() {
    AcquireSRWLockShared(&lock);
    const auto out = scale;
    ReleaseSRWLockShared(&lock);
    return out;
}
