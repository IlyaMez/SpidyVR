// Runs the web grab against the game's actors, inside the swing's world-query
// callback: picks come from the candidate watch, props move by their Havok
// bodies (native_bodies), bots fly the game's own flung reaction and are
// steered through their movers, and the game's rays keep a steered bot out of
// walls and the ground.
#include "spidy/game_grab.hpp"
#include "spidy/native_bodies.hpp"
#include "spidy/native_movement.hpp"
#include <algorithm>
#include <cstring>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_grab;
// Read by probes like SpidySwingData: odd `sequence` while it changes.
extern "C" {
__declspec(dllexport) Data SpidyGrabData;
}
namespace {
using game_targets::Candidate;
using game_targets::Kind;
// A target's centre above its actor transform (feet, or the base of a prop),
// its radius and its mass for the web.
struct Shape {
    float lift, radius, mass;
    TargetKind core;
};
Shape shape(Kind kind) {
    switch (kind) {
    case Kind::throwable:
        return {.45f, .45f, 30, TargetKind::Object};
    case Kind::bot:
        return {.95f, .45f, 80, TargetKind::Character};
    case Kind::pedestrian:
        return {.95f, .4f, 70, TargetKind::Character};
    }
    return {.5f, .4f, 50, TargetKind::Object};
}
// A command lasts long enough for one slow frame, like the swing's.
constexpr uint32_t leaseMs = 150;
constexpr float maxDriveSpeed = 40, landingSpeed = 1.5f;
struct Tracked {
    Candidate who{};
    uint64_t actor{}, mover{};
    Vec3 position{}, velocity{}; // centre, at the latest step
    bool seen{}, gone{};
    // Props: the web gave it a command in the latest step.
    bool driven{};
    // Bots. steering: its mover takes the web's velocity. flingAsked: the
    // game was asked to fling it. falling: the game would not fling it, so
    // Spidy brings it down along `flight`.
    bool steering{}, flingAsked{}, falling{};
    Vec3 flight{};
    float airborne{};
};
uintptr_t base{};
Call driveCall{}, drivenCall{};
uint32_t offered{};
game_targets::Watch watch;
std::vector<Candidate> candidates;
std::vector<Tracked> tracked;
std::vector<TargetCommand> commands;
WebGrab core{GrabConfig{}};
uint64_t driveSerial{}, stepped{}, botsLanded{};
size_t counted{};
// The target each hand's web last let go of, until when its web is drawn
// trailing it (GetTickCount64). Left where the target was when let go, the
// web hung in the air there as if still attached.
struct Trail {
    uint64_t record{}, until{};
};
Trail trails[2];
SRWLOCK output = SRWLOCK_INIT;

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
bool offers(Kind kind) {
    return offered & (1u << static_cast<unsigned>(kind));
}
Tracked* track(uint64_t record) {
    for (auto& t : tracked)
        if (t.who.record == record)
            return &t;
    return nullptr;
}
uintptr_t pointer(uintptr_t p) {
    uintptr_t v{};
    read(p, &v, sizeof(v));
    return v;
}
// An actor's handle: the actor is what its record's first field points to.
uint32_t actorHandle(uint64_t record) {
    uint32_t handle{};
    read(pointer(record) + 0x64, &handle, sizeof(handle));
    return handle;
}
// Everything this kind needs to be moved is there.
bool movable(const Candidate& c) {
    switch (c.kind) {
    case Kind::throwable:
        return c.physics != 0;
    case Kind::bot:
        return c.machine && game_targets::botMover(base, c);
    default:
        return false;
    }
}
Tracked* follow(const Candidate& c, Vec3 centre) {
    if (auto* t = track(c.record))
        return t;
    Tracked fresh{c};
    read(c.record, &fresh.actor, sizeof(fresh.actor));
    fresh.mover = game_targets::botMover(base, c);
    fresh.position = centre;
    fresh.seen = true;
    tracked.push_back(fresh);
    return &tracked.back();
}
class Targets final : public TargetQueries {
  public:
    std::optional<GrabTarget> pick(Vec3 origin, Vec3 direction, float distance, float cone) const override {
        const Candidate* best{};
        Vec3 bestCentre{};
        float bestMiss = cone, bestDistance = 1e9f;
        for (const auto& c : candidates) {
            if (!offers(c.kind))
                continue;
            Vec3 at{};
            if (!game_targets::position(c, at))
                continue;
            const auto s = shape(c.kind);
            at += Vec3{0, s.lift, 0};
            // A standing person is a tall target: its sphere covers head to knees.
            const float radius = s.core == TargetKind::Character ? s.radius * 2 : s.radius;
            const float miss = rayMiss(origin, direction, distance, at, radius);
            const float away = length(at - origin);
            if (miss > bestMiss + 1e-4f || (miss > bestMiss - 1e-4f && away >= bestDistance) || !movable(c))
                continue;
            best = &c;
            bestCentre = at;
            bestMiss = miss;
            bestDistance = away;
        }
        if (!best || !game_targets::live(base, *best))
            return {};
        const auto* t = follow(*best, bestCentre);
        const auto s = shape(best->kind);
        return GrabTarget{best->record, s.core, t->position, t->velocity, s.mass, s.radius};
    }
    std::optional<GrabTarget> find(std::uint64_t id) const override {
        const auto* t = track(id);
        if (!t) {
            // A web struck it directly (owner): take it up like a pick.
            for (const auto& c : candidates) {
                Vec3 at{};
                if (c.record == id && offers(c.kind) && movable(c) && game_targets::live(base, c) &&
                    game_targets::position(c, at)) {
                    t = follow(c, at + Vec3{0, shape(c.kind).lift, 0});
                    break;
                }
            }
        }
        if (!t || t->gone || !t->seen)
            return {};
        const auto s = shape(t->who.kind);
        return GrabTarget{id, s.core, t->position, t->velocity, s.mass, s.radius};
    }
    // Body userData (+0x98) carries its actor's handle in the low 32 bits.
    std::optional<std::uint64_t> owner(std::uint64_t surface) const override {
        const auto world = pointer(base + 0x78939e8);
        const auto bodies = pointer(world + 0x28);
        uint32_t capacity{}, id{}, handle{};
        const uint32_t index = surface & 0xffffff;
        if (surface > UINT32_MAX || !bodies || !read(world + 0x30, &capacity, 4) || index >= capacity ||
            !read(bodies + index * 0xc0ull + 0x70, &id, 4) || id != surface ||
            !read(bodies + index * 0xc0ull + 0x98, &handle, 4) || !(handle >> 24))
            return {};
        for (const auto& t : tracked)
            if (!t.gone && actorHandle(t.who.record) == handle)
                return t.who.record;
        for (const auto& c : candidates)
            if (offers(c.kind) && actorHandle(c.record) == handle)
                return c.record;
        return {};
    }
    void characters(std::vector<GrabTarget>& out) const override {
        for (const auto& c : candidates) {
            const auto s = shape(c.kind);
            Vec3 at{};
            if (s.core == TargetKind::Character && offers(c.kind) && game_targets::position(c, at))
                out.push_back({c.record, s.core, at + Vec3{0, s.lift, 0}, {}, s.mass, s.radius});
        }
    }
} targets;

// Rays stand in for the collision a non-sweeping mover skips: the velocity
// loses what goes into a wall ahead, and the centre stays its lift above the
// ground. Returns whether the target stands on the ground.
bool guard(const Tracked& t, Vec3& v, float dt, const WorldQueries& world) {
    const auto s = shape(t.who.kind);
    const float travel = length(v) * dt;
    if (travel > 1e-4f) {
        const Vec3 direction = v / length(v);
        if (const auto hit = world.raycast(t.position, direction, travel + s.radius); hit && hit->fixed) {
            const Vec3 n = normalized(hit->normal);
            const float into = dot(v, n);
            if (into < 0)
                v -= n * into;
        }
    }
    const float reach = s.lift + std::max(0.f, -v.y * dt) + .1f;
    const auto ground = world.raycast(t.position, {0, -1, 0}, reach);
    if (!ground || !ground->fixed)
        return false;
    const float height = t.position.y - ground->point.y;
    if (height + v.y * dt < s.lift)
        v.y = (s.lift - height) / dt;
    return height <= s.lift + .05f;
}
// A bot's mover: the web's velocity for its next steps, or let go.
void steer(const Tracked& t, bool enabled, Vec3 velocity) {
    if (!driveCall || !t.mover)
        return;
    native_movement::Drive d;
    d.enabled = enabled;
    d.mover = t.mover;
    d.record = t.who.record;
    d.serial = ++driveSerial;
    d.leaseMs = enabled ? leaseMs : 0;
    d.options = 1; // a bot standing about does not sweep
    d.velocity = enabled ? limited(velocity, maxDriveSpeed) : Vec3{};
    if (driveCall(&d)) {
        AcquireSRWLockExclusive(&output);
        ++SpidyGrabData.driveFailures;
        ReleaseSRWLockExclusive(&output);
    }
}
// The web stops steering a bot: the game's flight takes over at the bot's
// own velocity, or, when the game would not fling it, Spidy brings it down.
void handOff(Tracked& t, Vec3 velocity) {
    steer(t, false, {});
    t.steering = false;
    native_bodies::fling(t.who.machine, t.who.record, velocity);
    t.falling = !native_bodies::flung(t.who.machine);
    t.flight = velocity;
    t.airborne = 0;
}
void count(size_t from) {
    const auto& events = core.events();
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidyGrabData.sequence);
    for (auto i = from; i < events.size(); ++i) {
        const auto& e = events[i];
        SpidyGrabData.grabs += e.kind == GrabEventKind::Grab;
        SpidyGrabData.yanks += e.kind == GrabEventKind::Yank;
        SpidyGrabData.catches += e.kind == GrabEventKind::Catch;
        SpidyGrabData.throws += e.kind == GrabEventKind::Throw;
        SpidyGrabData.releases += e.kind == GrabEventKind::Release;
        SpidyGrabData.lost += e.kind == GrabEventKind::Lost;
        if (e.kind == GrabEventKind::Throw)
            SpidyGrabData.lastThrow = e.velocity;
    }
    InterlockedIncrement64(&SpidyGrabData.sequence);
    ReleaseSRWLockExclusive(&output);
    // A web let go of trails what it held; a new grab takes the hand's web.
    for (auto i = from; i < events.size(); ++i) {
        const auto& e = events[i];
        if (e.hand < 0 || e.hand > 1)
            continue;
        if (e.kind == GrabEventKind::Throw || e.kind == GrabEventKind::Release || e.kind == GrabEventKind::Lost)
            trails[e.hand] = {e.target, GetTickCount64() + static_cast<uint64_t>(trailSeconds * 1000)};
        else if (e.kind == GrabEventKind::Grab)
            trails[e.hand] = {};
    }
}
bool trailing(uint64_t record) {
    const auto now = GetTickCount64();
    return std::any_of(std::begin(trails), std::end(trails),
                       [&](const Trail& t) { return t.record == record && now < t.until; });
}
// Where each target is now, and how fast it goes: a freed prop's body or a
// steered bot's mover says; anything else, its last two positions.
void observe(float dt) {
    native_movement::DrivenData driven{};
    const bool haveDriven = drivenCall && !drivenCall(&driven);
    for (auto& t : tracked) {
        Vec3 at{};
        if (!game_targets::live(base, t.who) || !game_targets::position(t.who, at)) {
            t.gone = true;
            continue;
        }
        at += Vec3{0, shape(t.who.kind).lift, 0};
        Vec3 velocity = t.seen ? (at - t.position) / dt : Vec3{};
        Vec3 centre{}, moving{}, measured{};
        if (t.who.kind == Kind::throwable && native_bodies::predicted(t.actor, centre, moving, measured)) {
            // Freed, it moves by physics: its body, as the main thread saw it
            // before the step in flight, carried through that step.
            at = centre;
            velocity = moving;
        } else if (haveDriven) {
            for (const auto& m : driven.movers)
                if (t.mover && m.mover == t.mover && m.steps && finite(m.achievedVelocity))
                    velocity = m.achievedVelocity;
        }
        t.position = at;
        t.velocity = finite(velocity) ? velocity : Vec3{};
        t.seen = true;
    }
}
} // namespace

uint32_t game_grab::start(uintptr_t gameBase, Call drive, Call driven, uint32_t kinds) {
    stop();
    base = gameBase;
    driveCall = drive;
    drivenCall = driven;
    offered = kinds & movableKinds;
    if (!drive || !driven)
        offered &= ~(1u << static_cast<unsigned>(Kind::bot));
    uint32_t result{};
    if (offered && (result = native_bodies::start(base)))
        offered = 0;
    core = WebGrab(GrabConfig{});
    if (offered)
        watch.start(base, offered);
    stepped = botsLanded = 0;
    AcquireSRWLockExclusive(&output);
    SpidyGrabData = {};
    SpidyGrabData.status = offered ? 1 : 0;
    SpidyGrabData.kinds = offered;
    SpidyGrabData.error = result;
    ReleaseSRWLockExclusive(&output);
    return result;
}
uint32_t game_grab::stop() {
    cancel();
    watch.stop();
    const auto result = offered ? native_bodies::stop() : 0u;
    offered = 0;
    AcquireSRWLockExclusive(&output);
    SpidyGrabData.status = 3;
    if (result)
        SpidyGrabData.error = result;
    ReleaseSRWLockExclusive(&output);
    return result;
}
void game_grab::cancel() {
    core.releaseAll();
    trails[0] = trails[1] = {};
    for (auto& t : tracked) {
        if (t.who.kind == Kind::throwable)
            native_bodies::release(t.actor);
        else if (t.steering || t.falling)
            handOff(t, t.velocity); // the game's flight brings it down now
    }
    tracked.clear();
    commands.clear();
    AcquireSRWLockExclusive(&output);
    for (auto& hand : SpidyGrabData.hands)
        hand = {};
    ReleaseSRWLockExclusive(&output);
}
Input game_grab::claim(float inputSeconds, const Input& in, const WorldQueries& world, const Body& player) {
    if (!offered)
        return in;
    // Picks and owners look the targets up in this copy of the watch's list.
    if (inputSeconds > 0)
        watch.current(candidates);
    auto out = core.claim(inputSeconds, in, world, targets, player);
    count(0);
    counted = core.events().size();
    return out;
}
bool game_grab::due(float& dt) {
    if (!offered)
        return false;
    float last{};
    const auto steps = native_bodies::physicsSteps(last);
    if (!stepped || steps < stepped) {
        stepped = steps; // the first look, or the module started again
        return false;
    }
    const auto passed = steps - stepped;
    stepped = steps;
    dt = last * static_cast<float>(std::min<uint64_t>(passed, 4));
    return passed && std::isfinite(dt) && dt > 0 && dt <= .05f;
}
Input game_grab::forSwing(const Input& in) {
    return offered ? core.forSwing(in) : in;
}
void game_grab::step(float dt, const WorldQueries& world) {
    if (!offered || !std::isfinite(dt) || dt <= 0 || dt > .05f)
        return;
    observe(dt);
    commands.clear();
    core.step(dt, world, targets, commands);
    count(counted);
    counted = 0;
    const Vec3 gravity{0, -core.config().gravity, 0};
    for (auto& t : tracked) {
        if (t.gone)
            continue;
        const TargetCommand* command{};
        for (const auto& c : commands)
            if (c.id == t.who.record)
                command = &c;
        if (t.who.kind == Kind::throwable) {
            // The main thread moves the prop by the web's law, from its
            // bodies' actual motion, and keeps one the web let go of flying,
            // bouncing and sliding by the game's physics in real time until
            // it comes to rest (native_bodies).
            if (command) {
                native_bodies::drive(t.actor, t.who.physics, *command, leaseMs);
                t.driven = true;
                Vec3 centre{}, next{}, measured{};
                if (native_bodies::predicted(t.actor, centre, next, measured)) {
                    AcquireSRWLockExclusive(&output);
                    SpidyGrabData.commanded = next;
                    SpidyGrabData.observed = measured;
                    ReleaseSRWLockExclusive(&output);
                }
            } else if (t.driven) {
                native_bodies::release(t.actor); // slack or let go: physics has it
                t.driven = false;
            }
            continue;
        }
        if (command && !command->thrown) {
            // The web's law, from the bot as the mover last moved it.
            Vec3 v = advance(*command, t.position, t.velocity, dt, gravity);
            // The first pull flings the bot: the game plays it flying.
            if (!t.flingAsked) {
                native_bodies::fling(t.who.machine, t.who.record, v);
                t.flingAsked = true;
            }
            guard(t, v, dt, world);
            steer(t, true, v);
            t.steering = true;
            t.falling = false;
            continue;
        }
        if (command) {
            handOff(t, command->velocity); // thrown: the game's flight from here
            continue;
        }
        if (t.steering) {
            handOff(t, t.velocity); // a slack web or let go: the game's flight
            continue;
        }
        if (t.falling) {
            // The game would not fling it: down under Spidy's rays.
            t.flight.y -= core.config().gravity * dt;
            t.airborne += dt;
            Vec3 v = t.flight;
            const bool grounded = guard(t, v, dt, world);
            if (grounded) {
                const float keep = std::exp(-5 * dt);
                v.x *= keep;
                v.z *= keep;
            }
            if ((grounded && v.y <= .1f && length(Vec3{v.x, 0, v.z}) < landingSpeed + 4) || t.airborne > 6) {
                steer(t, false, {});
                t.falling = false;
                ++botsLanded;
                continue;
            }
            t.flight = v;
            steer(t, true, v);
            continue;
        }
        // On the ground again: the next pull flings it anew.
        if (!native_bodies::flung(t.who.machine))
            t.flingAsked = false;
    }
    // Forget targets nothing acts on any longer, nor a web trails.
    tracked.erase(std::remove_if(tracked.begin(), tracked.end(),
                                 [](const Tracked& t) {
                                     const bool held = std::any_of(
                                         core.grabs().begin(), core.grabs().end(), [&](const Grab& g) {
                                             return g.phase != GrabPhase::None && g.target == t.who.record;
                                         });
                                     return t.gone ||
                                            (!held && !t.steering && !t.falling && !trailing(t.who.record));
                                 }),
                  tracked.end());
    const auto bodies = native_bodies::counters();
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidyGrabData.sequence);
    SpidyGrabData.status = 2;
    SpidyGrabData.tickDt = dt;
    SpidyGrabData.timeScale = native_bodies::timeScale();
    SpidyGrabData.stepDt = SpidyGrabData.timeScale * dt;
    ++SpidyGrabData.steps;
    SpidyGrabData.commands += commands.size();
    SpidyGrabData.flights = bodies.flying + static_cast<uint64_t>(std::count_if(
                                                tracked.begin(), tracked.end(),
                                                [](const Tracked& t) { return t.steering || t.falling; }));
    SpidyGrabData.landed = botsLanded + bodies.rested;
    SpidyGrabData.candidates = static_cast<uint32_t>(candidates.size());
    SpidyGrabData.bodySteps = bodies.steps;
    SpidyGrabData.frees = bodies.frees;
    SpidyGrabData.rebuilds = bodies.rebuilds;
    SpidyGrabData.writes = bodies.writes;
    SpidyGrabData.follows = bodies.follows;
    SpidyGrabData.flings = bodies.flings;
    SpidyGrabData.steers = bodies.steers;
    SpidyGrabData.refused = bodies.rejected;
    SpidyGrabData.expired = bodies.expired;
    const auto now = GetTickCount64();
    for (unsigned i = 0; i < 2; ++i) {
        const auto& g = core.grabs()[i];
        auto& hand = SpidyGrabData.hands[i];
        if (g.phase != GrabPhase::None) {
            const auto* t = track(g.target);
            hand = {static_cast<uint32_t>(g.phase), t ? static_cast<uint32_t>(t->who.kind) : 0u,
                    g.target, t ? t->position : g.end, g.length, g.taut, g.tension};
            continue;
        }
        // Let go of: the web trails its target while it is there to trail.
        const auto* t = trails[i].record && now < trails[i].until ? track(trails[i].record) : nullptr;
        if (!t || t->gone)
            trails[i] = {};
        hand = t && !t->gone ? game_grab::Hand{0, static_cast<uint32_t>(t->who.kind), t->who.record, t->position,
                                               0, 0, 0, 1}
                             : game_grab::Hand{};
    }
    InterlockedIncrement64(&SpidyGrabData.sequence);
    ReleaseSRWLockExclusive(&output);
}
Data game_grab::data() {
    AcquireSRWLockShared(&output);
    auto out = SpidyGrabData;
    ReleaseSRWLockShared(&output);
    return out;
}
