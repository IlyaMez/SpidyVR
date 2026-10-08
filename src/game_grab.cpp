// Runs the web grab against the game's actors, inside the swing's world-query
// callback: picks come from the candidate watch, props move by their Havok
// bodies (native_bodies), an enemy a web pulls in is knocked into the game's
// own flung reaction, launched toward the hand and steered there, and what
// flies hurts what it strikes. An enemy the web only holds, or one the game
// would not fling, is steered through his mover, the game's rays keeping him
// out of walls and the ground.
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
// The kinetic blow that knocks an enemy into the game's flight (measured
// October 8: kKinetic, kFlyBack, KnockbackAmount 10, ImpactImpulse 30 gave
// BotStateFlung every time; kMelee and kExplosion only a stagger). A web that
// pulls him in deals it, and once he came down, a pull reknockSeconds after
// the last blow. It lands within a step or two; one that has not flung him by
// knockWait never will (a heavy, a scripted scene), and his mover is steered
// instead.
constexpr float knockDamage = 2, knockAmount = 10, knockImpulse = 30, knockWait = .25f, reknockSeconds = 1.5f;
// Which webs pull him in, and the launch his flight starts with (web_grab's
// PullConfig). In the session of October 8, 15:36, the first taut step
// knocked thugs while the hand began its yank, and the flight took the web's
// pull from his standing pace instead of a launch: they dropped where they
// stood and lay there. The launch waits launchSeconds for its flight. A throw
// of more than throwKnockSpeed knocks one lying where the web laid him into
// the flight the throw gives.
constexpr float launchSeconds = .6f, throwKnockSpeed = 4;
// A new launch reverses the motion the flight had (the game's own blow sends
// him away from the player): for settleSeconds after one, a change of pace is
// no impact and a flight that ends is no landing.
constexpr float settleSeconds = .15f;
// A bot let go of is watched for its landing for this long; a thrown prop
// for what it strikes.
constexpr float freeFlightSeconds = 6, thrownSeconds = 4;
struct Tracked {
    Candidate who{};
    uint64_t actor{}, mover{};
    Vec3 position{}, velocity{}; // centre, at the latest step
    bool seen{}, gone{};
    // Props: the web gave it a command in the latest step.
    bool driven{};
    // Bots. flying: the web steers the game's own flight (BotStateFlungLocal);
    // flew: it did since the web caught it. steering: its mover takes the
    // web's velocity (on his feet on a web that only holds him, or a bot the
    // game would not fling); grounded: standing on the ground at the latest
    // such step. flingAsked: the game was asked to fling it. falling: let go
    // of off the ground on its mover, Spidy brings it down along `flight`.
    bool flying{}, flew{}, steering{}, grounded{}, flingAsked{}, falling{};
    Vec3 flight{};
    float airborne{};
    // Knocked off his feet, his flight starts with `launch` (a yank's arc, an
    // arc to the hand, a throw) while `launching`, for up to launchSeconds
    // (launchAge so far). settle: seconds since a launch or a throw last set
    // the flight's velocity anew.
    Vec3 launch{};
    bool launching{};
    float launchAge{}, settle{};
    // Seconds since the web knocked it into a flight (negative: never), since
    // the game took a flight the web let go of (negative: none), since a prop
    // was let go of (negative: held, or never).
    float knocked = -1, free = -1, thrown = -1;
    // What it struck. `expected`: the velocity it was given for this step (by
    // the web, or its flight under gravity). The web's velocity reaches the
    // game a frame later, and the game draws a bot a frame late or early now
    // and then, so a flight is measured over two steps (`paced`, from where it
    // was two steps before), and struck something when it stops short of the
    // fastest it went in the three steps before (`cruise`).
    Vec3 expected{}, paced{}, back1{}, back2{};
    float dt1{}, cruise{};
    float speeds[3]{};
    uint32_t seenSteps{};
};
// Bots a strike hurt lately: no other blow before `until` (GetTickCount64).
struct Shield {
    uint64_t record{}, until{};
};
uintptr_t base{};
Call driveCall{}, drivenCall{};
uint32_t offered{};
// The VR settings' switch (allow()); offered stays as started.
bool allowed = true;
game_targets::Watch watch;
std::vector<Candidate> candidates;
std::vector<Tracked> tracked;
std::vector<TargetCommand> commands;
std::vector<Shield> shields;
WebGrab core{GrabConfig{}};
const StrikeConfig strikes{};
uint64_t driveSerial{}, stepped{}, botsLanded{};
// The player's actor record at the latest step: the one who deals the web's
// blows.
uint64_t hero{};
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
// What the webs catch: props, and the enemies among the bots. On October 8,
// 15:36, pulls took civilians: every bot was offered.
bool takes(const Candidate& c) {
    return offers(c.kind) && (c.kind != Kind::bot || game_targets::enemy(c));
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
// The candidate a web along the ray takes (TargetQueries::pick), and its centre.
const Candidate* nearest(Vec3 origin, Vec3 direction, float distance, float cone, Vec3& centre) {
    const Candidate* best{};
    float bestMiss = cone, bestDistance = 1e9f;
    for (const auto& c : candidates) {
        if (!takes(c))
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
        centre = at;
        bestMiss = miss;
        bestDistance = away;
    }
    return best && game_targets::live(base, *best) ? best : nullptr;
}
GrabTarget described(const Candidate& c, Vec3 centre, Vec3 velocity) {
    const auto s = shape(c.kind);
    return GrabTarget{c.record, s.core, centre, velocity, s.mass, s.radius};
}
class Targets final : public TargetQueries {
  public:
    std::optional<GrabTarget> pick(Vec3 origin, Vec3 direction, float distance, float cone) const override {
        Vec3 centre{};
        const auto* best = nearest(origin, direction, distance, cone, centre);
        if (!best)
            return {};
        const auto* t = follow(*best, centre);
        return described(*best, t->position, t->velocity);
    }
    std::optional<GrabTarget> find(std::uint64_t id) const override {
        const auto* t = track(id);
        if (!t) {
            // A web struck it directly (owner): take it up like a pick.
            for (const auto& c : candidates) {
                Vec3 at{};
                if (c.record == id && takes(c) && movable(c) && game_targets::live(base, c) &&
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
            if (takes(c) && actorHandle(c.record) == handle)
                return c.record;
        return {};
    }
    // The enemies a throw may be aimed at.
    void characters(std::vector<GrabTarget>& out) const override {
        for (const auto& c : candidates) {
            const auto s = shape(c.kind);
            Vec3 at{};
            if (s.core == TargetKind::Character && takes(c) && game_targets::position(c, at))
                out.push_back({c.record, s.core, at + Vec3{0, s.lift, 0}, {}, s.mass, s.radius});
        }
    }
} targets;
// The same targets for the aim markers: what a press would take, without
// taking it up. Only a press starts following its target.
class Glance final : public TargetQueries {
  public:
    std::optional<GrabTarget> pick(Vec3 origin, Vec3 direction, float distance, float cone) const override {
        Vec3 centre{};
        const auto* best = nearest(origin, direction, distance, cone, centre);
        if (!best)
            return {};
        const auto* t = track(best->record);
        return t && t->seen && !t->gone ? described(*best, t->position, t->velocity) : described(*best, centre, {});
    }
    std::optional<GrabTarget> find(std::uint64_t id) const override {
        if (const auto* t = track(id))
            return t->seen && !t->gone ? std::optional{described(t->who, t->position, t->velocity)} : std::nullopt;
        for (const auto& c : candidates) {
            Vec3 at{};
            if (c.record == id && takes(c) && movable(c) && game_targets::live(base, c) &&
                game_targets::position(c, at))
                return described(c, at + Vec3{0, shape(c.kind).lift, 0}, {});
        }
        return {};
    }
    std::optional<std::uint64_t> owner(std::uint64_t surface) const override {
        return targets.owner(surface);
    }
} glance;

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
bool shielded(uint64_t record) {
    const auto now = GetTickCount64();
    return std::any_of(shields.begin(), shields.end(),
                       [&](const Shield& s) { return s.record == record && now < s.until; });
}
void shield(uint64_t record) {
    const auto now = GetTickCount64();
    shields.erase(std::remove_if(shields.begin(), shields.end(), [&](const Shield& s) { return now >= s.until; }),
                  shields.end());
    shields.push_back({record, now + static_cast<uint64_t>(strikes.cooldown * 1000)});
}
// One blow through the game's damage system, `damager` the actor it comes
// from (the game knocks the one it hits away from it). Kinetic: what a
// thrown body deals.
bool blow(uint64_t victim, uint64_t damager, Vec3 point, Vec3 direction, const StrikeBlow& b) {
    if (!victim || shielded(victim))
        return false;
    native_bodies::Damage d;
    d.victim = victim;
    d.damager = damager;
    d.point = point;
    d.direction = length(direction) > .5f ? normalized(direction) : Vec3{0, 0, 1};
    d.normal = d.direction * -1.f;
    d.amount = b.damage;
    d.type = 7; // kKinetic
    d.knockback = b.knockback;
    d.knockbackAmount = b.knockbackAmount;
    if (b.fling)
        d.impulse = knockImpulse;
    if (!native_bodies::damage(d))
        return false;
    shield(victim);
    return true;
}
// An enemy off his feet into the game's own flight, which the web then steers.
void knock(Tracked& t, Vec3 pull) {
    native_bodies::Damage d;
    d.victim = t.who.record;
    d.damager = hero;
    d.point = t.position;
    d.direction = length(pull) > .1f ? normalized(pull) : Vec3{0, 0, 1};
    d.normal = d.direction * -1.f;
    d.amount = knockDamage;
    d.type = 7;      // kKinetic
    d.knockback = 5; // kFlyBack
    d.knockbackAmount = knockAmount;
    d.impulse = knockImpulse;
    t.knocked = 0;
    if (native_bodies::damage(d)) {
        AcquireSRWLockExclusive(&output);
        ++SpidyGrabData.launches;
        ReleaseSRWLockExclusive(&output);
    }
}
// A pull that is no yank launches him at the reel's speed, for no longer than
// a yank's flight.
PullConfig pulling() {
    PullConfig p;
    p.launchSpeed = core.config().reelSpeed;
    p.maxLaunchTime = core.config().maxYankTime;
    return p;
}
// Off his feet onto `velocity`: the blow knocks him into the game's flight,
// which takes the launch at its first step.
void launchInto(Tracked& t, Vec3 velocity) {
    if (t.steering) {
        steer(t, false, {});
        t.steering = false;
    }
    knock(t, velocity);
    t.launch = velocity;
    t.launching = true;
    t.launchAge = 0;
    t.flingAsked = false;
}
// The velocity of the flight the game carries him on, from the next step:
// `anew` for a launch or a throw, not the web's law carrying it on.
void fly(Tracked& t, Vec3 v, bool anew) {
    native_bodies::fling(t.who.machine, t.who.record, v, false);
    t.expected = v;
    if (anew)
        t.settle = 0;
    AcquireSRWLockExclusive(&output);
    ++SpidyGrabData.flown;
    ReleaseSRWLockExclusive(&output);
}
// An enemy on the web's command `c` (a throw is release()'s): his flight
// steered, a pull knocking him into one, or his mover.
void pull(Tracked& t, const TargetCommand& c, float dt, const WorldQueries& world, bool flung, Vec3 gravity) {
    t.free = -1;
    if (flung) {
        if (t.steering) {
            steer(t, false, {});
            t.steering = false;
        }
        // The flight's first step takes the launch the web knocked him into
        // it with; from then on the web's law runs from the velocity it gave
        // him, which the flight keeps. From his measured pace, which lags the
        // flight by a step or two, the law lost the launch.
        if (t.launching)
            fly(t, t.launch, true);
        else if (c.mode == TargetCommand::Mode::Launch)
            fly(t, c.velocity, true);
        else
            fly(t, advance(c, t.position, t.flying ? t.expected : t.paced, dt, gravity), false);
        t.launching = false;
        t.flying = t.flew = true;
        t.falling = false;
        return;
    }
    t.flying = false;
    if (pullsIn(c, t.position, pulling()) && (t.knocked < 0 || t.knocked > reknockSeconds)) {
        launchInto(t, limited(pullLaunch(c, t.position, gravity, pulling()), maxDriveSpeed));
        return;
    }
    // The blow lands within a step or two; meanwhile he keeps still.
    if (t.knocked >= 0 && t.knocked < knockWait)
        return;
    // He flew and came down: he lies where he fell (the game flops and stuns
    // him) until a pull may knock him off his feet again.
    if (t.flew && t.knocked <= reknockSeconds)
        return;
    // On his feet on a web that only holds him, or one the game would not
    // fling (a heavy, a scripted scene): his mover, as the web steered every
    // bot before October 8. One the blow did not fling is asked for the
    // flight once, at the launch's velocity: asked for at the web's own, a
    // fling dropped him where he stood.
    if (t.launching && !t.flingAsked) {
        native_bodies::fling(t.who.machine, t.who.record, t.launch);
        t.flingAsked = true;
    }
    Vec3 v = advance(c, t.position, t.velocity, dt, gravity);
    t.grounded = guard(t, v, dt, world);
    steer(t, true, v);
    t.steering = true;
    t.falling = false;
}
// The web lets go of an enemy: `thrown` at `velocity`, or let go of.
void release(Tracked& t, bool thrown, Vec3 velocity, bool flung) {
    if (thrown && t.launching) {
        t.launch = velocity; // thrown before his flight began: it starts with the throw
        return;
    }
    if (flung && (t.flying || thrown)) {
        // The game flies him on from here and lands him (BotStateGroundFlop,
        // then BotStateStunned): thrown, at the throw's velocity; let go of,
        // as the web left him.
        if (thrown)
            fly(t, velocity, true);
        t.flying = false;
        t.free = 0;
        return;
    }
    t.flying = false;
    if (thrown && length(velocity) >= throwKnockSpeed) {
        // Thrown from his feet, or from where the web laid him down: knocked
        // into the flight the throw gives.
        launchInto(t, velocity);
        return;
    }
    if (t.steering) {
        // On his mover: standing, he stands; off the ground, he falls.
        if (t.grounded) {
            steer(t, false, {});
            t.steering = false;
        } else {
            handOff(t, velocity);
        }
    }
}
// What a flying bot struck: the world it lost its speed to, another enemy in
// its way, the ground it landed on at speed.
void strike(Tracked& t, float dt, bool flung, const Vec3& gravity) {
    const bool flight = t.flying || t.free >= 0;
    if (!flight)
        return;
    uint64_t impacts{}, struck{};
    const float speed = length(t.paced);
    const bool settled = t.settle >= settleSeconds;
    if (flung) {
        // Seen flying fast, it stopped short, and not because the web asked.
        if (const float loss = impactLoss(t.cruise, speed, strikes);
            settled && loss > 0 && length(t.expected) >= strikes.kept * t.cruise) {
            impacts += blow(t.who.record, hero, t.position, length(t.expected) > .1f ? t.expected : t.paced,
                            impactBlow(loss, strikes));
            // What stopped him stops the flight the web carries on.
            t.expected = t.paced;
        }
        if (speed >= strikes.strikeSpeed)
            for (const auto& c : candidates) {
                Vec3 feet{};
                if (!game_targets::enemy(c) || c.record == t.who.record || !game_targets::position(c, feet) ||
                    !touches(t.position, shape(Kind::bot).radius, feet, strikes))
                    continue;
                // The one struck is knocked away from the one flying into him.
                if (blow(c.record, t.who.record, feet + Vec3{0, shape(Kind::bot).lift, 0}, t.paced,
                         strikeBlow(speed, strikes))) {
                    ++struck;
                    impacts += blow(t.who.record, hero, t.position, t.paced * -1.f, impactBlow(speed * .6f, strikes));
                }
            }
        // Let go of, the flight falls freely from here.
        if (t.free >= 0)
            t.expected = t.paced + gravity * dt;
    } else {
        // The game ended the flight, let go of or on the web: it landed, as
        // hard as it came down.
        const float landed = std::max(t.cruise, length(t.expected));
        if (settled && landed >= strikes.landSpeed)
            impacts += blow(t.who.record, hero, t.position, t.expected, impactBlow(landed, strikes));
        t.free = -1;
    }
    if (impacts || struck) {
        AcquireSRWLockExclusive(&output);
        SpidyGrabData.impacts += impacts;
        SpidyGrabData.struck += struck;
        ReleaseSRWLockExclusive(&output);
    }
}
// A thrown prop striking an enemy: kinetic, as the game's own thrown objects.
void strikeWith(const Tracked& t) {
    const float speed = length(t.velocity);
    if (t.thrown < 0 || speed < strikes.strikeSpeed)
        return;
    uint64_t struck{};
    for (const auto& c : candidates) {
        Vec3 feet{};
        if (!game_targets::enemy(c) || !game_targets::position(c, feet) ||
            !touches(t.position, shape(t.who.kind).radius, feet, strikes))
            continue;
        struck += blow(c.record, hero, feet + Vec3{0, shape(Kind::bot).lift, 0}, t.velocity,
                       strikeBlow(speed, strikes));
    }
    if (struck) {
        AcquireSRWLockExclusive(&output);
        SpidyGrabData.struck += struck;
        ReleaseSRWLockExclusive(&output);
    }
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
        } else if (haveDriven && t.steering) {
            // Its mover, as the web steered it; a flight the game carries is
            // measured from where it went.
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
        else if (t.steering && t.grounded)
            steer(t, false, {}); // on his feet: he stands
        else if (t.steering || t.falling)
            handOff(t, t.velocity); // the game's flight brings it down now
        // A flight the web steered goes on as the game flies it.
    }
    tracked.clear();
    commands.clear();
    AcquireSRWLockExclusive(&output);
    for (auto& hand : SpidyGrabData.hands)
        hand = {};
    ReleaseSRWLockExclusive(&output);
}
bool game_grab::offering() {
    return offered != 0;
}
void game_grab::allow(bool on) {
    allowed = on;
}
Input game_grab::claim(float inputSeconds, const Input& in, const WorldQueries& world, const Body& player) {
    if (!offered)
        return in;
    if (!allowed) {
        // Switched off: what the webs hold or still trail goes back to the game.
        if (holds(0) || holds(1) || !tracked.empty())
            cancel();
        return in;
    }
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
    return offered && allowed ? core.forSwing(in) : in;
}
std::optional<GrabTarget> game_grab::preview(unsigned hand, Pose aim, const WorldQueries& world) {
    if (!offered || !allowed || hand > 1 || holds(hand))
        return {};
    return core.preview(aim, world, glance);
}
bool game_grab::holds(unsigned hand) {
    return hand < 2 && core.grabs()[hand].phase != GrabPhase::None;
}
void game_grab::step(float dt, const WorldQueries& world, uint64_t player) {
    if (!offered || !std::isfinite(dt) || dt <= 0 || dt > .05f)
        return;
    hero = player;
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
                t.thrown = command->thrown ? 0.f : -1.f;
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
                t.thrown = 0;
            }
            // Flying, it hurts the thugs it strikes.
            if (t.thrown >= 0) {
                strikeWith(t);
                t.thrown += dt;
            }
            continue;
        }
        const bool flung = native_bodies::flung(t.who.machine);
        if (t.knocked >= 0)
            t.knocked += dt;
        if (t.free >= 0)
            t.free += dt;
        t.settle += dt;
        if (t.launching && (t.launchAge += dt) > launchSeconds)
            t.launching = false; // the blow brought no flight
        // Its pace over the last two steps, and the fastest it went over the
        // three before this one.
        t.paced = t.seenSteps >= 2 && t.dt1 + dt > 0 ? (t.position - t.back2) / (t.dt1 + dt) : t.velocity;
        if (!finite(t.paced))
            t.paced = {};
        t.cruise = std::max({t.speeds[0], t.speeds[1], t.speeds[2]});
        // What it struck since the step before, under what it was given then.
        strike(t, dt, flung, gravity);
        t.speeds[2] = t.speeds[1];
        t.speeds[1] = t.speeds[0];
        t.speeds[0] = length(t.paced);
        t.back2 = t.back1;
        t.back1 = t.position;
        t.dt1 = dt;
        ++t.seenSteps;
        if (command && !command->thrown) {
            pull(t, *command, dt, world, flung, gravity);
            continue;
        }
        if (command) {
            release(t, true, command->velocity, flung); // thrown: the game's flight from here
            continue;
        }
        if (t.launching) {
            // Knocked into a flight the web no longer holds (a throw, a let
            // go): it starts with the launch, and the game flies him on.
            if (flung) {
                fly(t, t.launch, true);
                t.launching = false;
                t.free = 0;
            }
            continue;
        }
        if (t.flying || t.steering) {
            release(t, false, t.velocity, flung); // a slack web or let go: the game's flight
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
        if (!flung)
            t.flingAsked = false;
    }
    // Forget targets nothing acts on any longer, nor a web trails, nor flies
    // where it may strike something.
    tracked.erase(std::remove_if(tracked.begin(), tracked.end(),
                                 [](const Tracked& t) {
                                     const bool held = std::any_of(
                                         core.grabs().begin(), core.grabs().end(), [&](const Grab& g) {
                                             return g.phase != GrabPhase::None && g.target == t.who.record;
                                         });
                                     const bool flying = t.flying || t.launching ||
                                                         (t.free >= 0 && t.free < freeFlightSeconds) ||
                                                         (t.thrown >= 0 && t.thrown < thrownSeconds &&
                                                          length(t.velocity) > 1);
                                     return t.gone || (!held && !t.steering && !t.falling && !flying &&
                                                       !trailing(t.who.record));
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
    SpidyGrabData.flights =
        bodies.flying + static_cast<uint64_t>(std::count_if(tracked.begin(), tracked.end(), [](const Tracked& t) {
            return t.steering || t.falling || t.flying || t.free >= 0;
        }));
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
