// Runs the web grab against the game's actors, inside the swing's world-query
// callback: picks come from the candidate watch, props move by their Havok
// bodies (native_bodies), an enemy a web pulls in is knocked into the game's
// own flung reaction, launched toward the hand and steered there, and what
// flies hurts what it strikes. An enemy the web only holds, or one the game
// would not fling, is steered through his mover, the game's rays keeping him
// out of walls and the ground.
#include "spidy/game_grab.hpp"
#include "spidy/game_focus.hpp"
#include "spidy/game_punch.hpp"
#include "spidy/game_shooter.hpp"
#include "spidy/native_bodies.hpp"
#include "spidy/native_movement.hpp"
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_grab;
// Read by probes like SpidySwingData: odd `sequence` while it changes.
extern "C" {
__declspec(dllexport) Data SpidyGrabData;
__declspec(dllexport) TieData SpidyTieData;
}
namespace {
using game_targets::Candidate;
using game_targets::Kind;
// A target's centre above its actor transform (feet, or the base of a prop),
// its radius and its mass for the web. A bot's centre and radius are a
// street thug's carried over to his size (game_targets::Size).
struct Shape {
    float lift, radius, mass;
    TargetKind core;
};
Shape shape(const Candidate& c) {
    switch (c.kind) {
    case Kind::throwable:
        return {.45f, .45f, 30, TargetKind::Object};
    case Kind::bot:
        return {c.size.height(.95f), c.size.width(.45f), 80, TargetKind::Character};
    case Kind::pedestrian:
        return {.95f, .4f, 70, TargetKind::Character};
    }
    return {.5f, .4f, 50, TargetKind::Object};
}
// Whether a sphere at `centre` touches the standing bot `c` whose actor is at
// `feet`: the strikes' capsule, a street thug's, carried over to his size.
bool touchesBot(Vec3 centre, float radius, const Candidate& c, Vec3 feet, const StrikeConfig& strikes) {
    StrikeConfig sized = strikes;
    const float bottom = c.size.height(0);
    sized.characterHeight = c.size.height(strikes.characterHeight) - bottom;
    sized.characterRadius = c.size.width(strikes.characterRadius);
    return touches(centre, radius, feet + Vec3{0, bottom, 0}, sized);
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
    // Snatched: yanked by a player in the air, it comes to him and floats
    // before him (snatch()). snatchAge: since the yank; floated: beside him.
    bool snatched{};
    float snatchAge{}, floated{};
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
    // The web tie (hold()): since he arrived at his place (-1: not yet),
    // since the tie's last webbing hit him, and how many did.
    float tieContact = -1, tieWebbed = -1;
    uint32_t tieWebs{};
    // Once arrived: the nearest he came to his place, and how long he has
    // come no nearer (he is against his surface: settled there).
    float tieNearest = 1e9f, tieStill{};
    bool tieMarked{};
    // The game's stick asked of him (native_bodies::stick): how many times,
    // and since the last (-1: never).
    uint32_t tieSticks{};
    float tieAsked = -1;
    // Webbed on his way, out of his cocoon's state (hold).
    bool tiePrewebbed{};
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
WebTies ties{TieConfig{}};
bool tieReady{};
// WEB TIE (allowTies); and whether the state log watches tied bots (step).
bool tiesAllowed{}, statesWatched{};
// Bots seen webbed up, and when (GetTickCount64), for game_grab::cocooned.
std::unordered_map<uint64_t, uint64_t> cocoonSeen;
// The bot each hand's web held lately (game_grab::heldLately).
struct Lately {
    uint64_t target{};
    float since = 1e9f;
};
Lately lately[2];
uint64_t tiesDown{}, tiesStuck{};
const StrikeConfig strikes{};
uint64_t driveSerial{}, stepped{}, botsLanded{};
// The player's actor record at the latest step: the one who deals the web's
// blows.
uint64_t hero{};
// The player as the latest input sample has him (claim()): his centre, his
// velocity in the game's time, whether he stands.
Body heroBody{{}, {}, true};
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
// What the webs catch: props, and the enemies among the bots who are still
// in the fight. On October 8, 15:36, pulls took civilians: every bot was
// offered; until October 12 they took the knocked out and the thugs of waves
// to come.
bool takes(const Candidate& c) {
    return offers(c.kind) && (c.kind != Kind::bot || game_targets::fighting(base, c));
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
        const auto s = shape(c);
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
    const auto s = shape(c);
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
                    t = follow(c, at + Vec3{0, shape(c).lift, 0});
                    break;
                }
            }
        }
        if (!t || t->gone || !t->seen)
            return {};
        const auto s = shape(t->who);
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
            const auto s = shape(c);
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
                return described(c, at + Vec3{0, shape(c).lift, 0}, {});
        }
        return {};
    }
    std::optional<std::uint64_t> owner(std::uint64_t surface) const override {
        return targets.owner(surface);
    }
} glance;

// A ray the game takes (native_rays::valid: a finite origin, a unit way, 1 cm
// to 1 km): any other one the swing's world refuses with error 2105, and the
// swing stops - VR with it (November 4, 10:59: a gone thug's position read
// as the world's origin, a ray from there over a kilometre long).
std::optional<RayHit> safeRay(const WorldQueries& world, Vec3 from, Vec3 way, float distance) {
    if (!finite(from) || !finite(way) || !std::isfinite(distance) || length(way) < .5f || distance < .02f ||
        distance > 200 || length(from) < 1e-3f)
        return {};
    return world.raycast(from, normalized(way), distance);
}
// Rays stand in for the collision a non-sweeping mover skips: the velocity
// loses what goes into a wall ahead, and the centre stays its lift above the
// ground. Returns whether the target stands on the ground.
bool guard(const Tracked& t, Vec3& v, float dt, const WorldQueries& world) {
    const auto s = shape(t.who);
    const float travel = length(v) * dt;
    if (travel > 1e-4f) {
        const Vec3 direction = v / length(v);
        if (const auto hit = safeRay(world, t.position, direction, travel + s.radius); hit && hit->fixed) {
            const Vec3 n = normalized(hit->normal);
            const float into = dot(v, n);
            if (into < 0)
                v -= n * into;
        }
    }
    const float reach = s.lift + std::max(0.f, -v.y * dt) + .1f;
    const auto ground = safeRay(world, t.position, {0, -1, 0}, reach);
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
void knock(Tracked& t, Vec3 pull, float amount = knockDamage) {
    native_bodies::Damage d;
    d.victim = t.who.record;
    d.damager = hero;
    d.point = t.position;
    d.direction = length(pull) > .1f ? normalized(pull) : Vec3{0, 0, 1};
    d.normal = d.direction * -1.f;
    d.amount = amount;
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
// A tied enemy (web_tie): carried to his place and stuck there in his
// cocoon. Carried in the game's own flight, as the web pulls a thug: a blow
// of tieKnockDamage knocks him into it at tieLaunch (a yank), and the tie's
// law steers it in an arc to his place. Until October 31 he was held on his
// mover (09:44: "it pulls very slowly", "it does not stick to walls").
//
// Stuck by the game itself, at once, where he arrives: in November 2's
// session (08:13) 24 of 29 ties ended as their thug came to the wall and the
// game took him down; he stuck only once the player's web balls finished
// him. The user wants him stuck every time he arrives - on a wall, a ceiling,
// anything - with no delay; not webbed on the way (a first build that day
// webbed him on any touch: a bump, a lamp, a ledge on the way). So as he
// comes within tieStickReach of his place (or tieStickLead seconds of it at
// his speed: the webbing lands as he touches, not after), or the game takes
// him down there, the tie webs him as a web ball would, with all the
// webbing his cocoon needs in one hit (tieWebbing: a street thug's threshold
// is 30), and again every tieWebEvery while the game has not stuck him
// (BotStateWallStuck; tieWebsMost at most). Then no more blows near his
// place: knocks again and again at a place he could not reach made him
// twitch (one tie that day: 14 of them); his mover holds him there.
constexpr float tieKnockDamage = .25f, tieReknockSeconds = .8f;
constexpr float tieStickReach = 1.2f, tieStickLead = .12f, tieWebDamage = .1f, tieWebbing = 40,
                tieWebEvery = .25f;
constexpr uint32_t tieWebsMost = 6;
// Down without being stuck this long after he arrived: the game has him
// (knocked out); the tie lets go.
constexpr float tieDownSeconds = 3;
// Off his place this far once arrived and on his feet: carried back.
constexpr float tieCarryBack = 1.5f;
// Under a ceiling or a crane's arm: in November 2's sessions (08:36, 08:58)
// walls stuck every time, while thugs tied up there hung 1 m under the
// surface and stuck only now and then - when a web hit came as he was
// pressed close to it. The game sticks a webbed thug hit by a web near a
// surface (it has BotWallStuckEvent and BotGroundStuckEvent); from his
// centre a ceiling is a metre away, a wall half that. So up there the tie's
// webbing hits him where his head meets the surface, along the line to
// where the web stuck (encase), every tieOverheadEvery up to tieOverheadWebs
// times while the game has not stuck him; the first build that day hit him
// once from his centre, and the user: "the ceiling still practically does
// not work". Walls keep their hits as they were.
constexpr uint32_t tieOverheadWebs = 2;
constexpr float tieOverheadEvery = .5f, tieOverheadHead = .85f;
// The game's own stick, asked by Spidy (native_bodies::stick), as the state
// log of November 2 (09:44) showed the game asking it: the moment he is at
// his surface (within tieAskReach of his place, or settled against it), or
// tieAskLate after he arrived at the latest, on any surface but a floor;
// again every tieAskEvery, tieAsksMost times. Until November 2 (13:30) a
// surface not facing down waited a second for the game to stick him itself:
// on a crane's beams it did not, and he hung there that second ("it sticks,
// but late", with a video).
constexpr float tieAskReach = .4f, tieAskLate = .3f, tieAskEvery = .2f, tieAskGiveUp = 3;
constexpr uint32_t tieAsksMost = 8;
// A thug tied in the air (yanked first) is out of his cocoon's state, in the
// game's flight, his webbing worn off; at a wall he waited for the tie's
// first webbing hit at his touch and stuck 0.5-0.9 s after arriving, while
// one tied off the floor stuck within 0.1 s (November 2, 13:46, with a
// video: "only those pulled in the air stick late"). So one tied out of his
// cocoon's state is webbed at once, on his way (prewebbed), and the stick is
// asked again every 0.2 s (it waited 0.6 s, and a ray that met nothing cost
// that wait too).
constexpr float tieSettleSeconds = .2f, tieSettleGain = .03f, tieSettleReach = 1.5f;
uint64_t tieKnocks{}, tieWebHits{};
// Overhead: a surface facing down (a ceiling, a crane's arm), where his head,
// not his side, meets it.
bool overhead(const Tie& tie) {
    const Vec3 out = tie.place - tie.anchor;
    return length(out) > .01f && normalized(out).y < -.5f;
}
void encase(Tracked& t, const Tie& tie) {
    const Vec3 out = tie.place - tie.anchor;
    const Vec3 normal = length(out) > .01f ? normalized(out) : Vec3{0, 1, 0};
    native_bodies::Damage d;
    d.victim = t.who.record;
    d.damager = hero;
    d.point = t.position;
    d.direction = normal * -1.f; // onto the surface
    d.normal = normal;
    if (overhead(tie)) {
        // Where his head meets it, toward where the web stuck.
        const Vec3 up = tie.anchor - t.position;
        const float reach = length(up);
        if (reach > .05f) {
            d.direction = up / reach;
            d.point = t.position + d.direction * std::min(tieOverheadHead, std::max(reach - .05f, 0.f));
        }
    }
    d.amount = tieWebDamage;
    d.type = 20;     // kWebImpact
    d.knockback = 1; // kTwitch, as the web shooter's ball
    d.knockbackAmount = 1;
    d.statusType = 19; // kWebEncase
    d.statusAmount = tieWebbing;
    t.tieWebbed = 0;
    if (native_bodies::damage(d)) {
        ++t.tieWebs;
        ++tieWebHits;
    }
}
// A thug the game (or Spidy) stuck, to be finished where he hangs: stuck
// is only a state of his body, his hit points stay full, so the game and
// its scanner still count him in the fight (November 2, 12:35: "stuck, they
// are not defeated: the sensor still lights them red"). finishAfter after he
// was stuck a blow of all his hit points (kWebImpact, no knockback, no
// status) defeats him the game's own way, its events and counts with it;
// finishCheck after it, if the blow took him off his surface, the stick is
// asked again (finishResticks times), and the state log is told how it went.
// Ties to web lines, by their bot's record: the body at one of the line's
// ends, the line's direction, its id, the point of it he hangs from, and where
// his centre hangs (game_grab::lineHang under it). Hung from it once stuck
// (Hang), on a strand of web, until the line goes. Until November 3 (17:30)
// he was stuck under the line itself, as under a beam: in the way of a
// player walking on it, and not hanging as a cocoon does from a web.
struct LineTie {
    uint32_t surface{}, line{};
    Vec3 along{}, point{}, hang{};
};
std::unordered_map<uint64_t, LineTie> lineTies;
struct Hang {
    uint64_t record{}, machine{};
    uint32_t line{};
    Vec3 top{};
    // Since the game stopped holding him there (GetTickCount64; 0: it holds).
    uint64_t loose{};
};
// A hung thug the game no longer holds stuck this long has fallen off his
// line (pushed off by one hung too near, November 4, 11:47): his strand goes,
// and he lies where he falls, defeated. His strand stayed on his falling body
// until then, and with several fallen the strands tangled (with a video).
constexpr uint64_t hangLooseMs = 300;
std::vector<Hang> hangs;
// Asks the game's stick of a thug at his hang under a web line: as on a wall
// (pose 6) facing the player, his back to it, on the body at the line's end.
bool stickOnLine(uint64_t machine, uint64_t record, const LineTie& line) {
    if (!line.surface)
        return false;
    Vec3 along = line.along;
    along.y = 0;
    Vec3 facing = finite(heroBody.position) ? heroBody.position - line.hang : Vec3{};
    facing.y = 0;
    if (length(along) > .3f) {
        const Vec3 a = normalized(along);
        facing = facing - a * dot(facing, a);
        if (length(facing) < .3f)
            facing = cross(Vec3{0, 1, 0}, a);
    }
    if (length(facing) < .3f)
        facing = {0, 0, 1};
    const Vec3 n = normalized(facing);
    return native_bodies::stick(machine, record, line.surface, line.hang - n * .45f, n, Vec3{0, 1, 0}, 6);
}
struct Finish {
    uint64_t record{}, machine{};
    Vec3 anchor{}, normal{};
    float age{};
    bool struck{}, checked{};
    uint32_t resticks{};
};
std::vector<Finish> finishing;
constexpr float finishAfter = .35f, finishCheck = 1, finishEnd = 2.5f, finishExtra = 5;
constexpr uint32_t finishResticks = 2;
uint64_t finishes{};
// The stick asked of a bot at `position` toward a surface whose web stuck at
// `anchor` (normal out of it): the body and point a ray meets, its normal
// facing him, a direction along it (up a wall; under a ceiling away from the
// player). False when no surface was met, or it is a floor.
bool askStickAt(uint64_t machine, uint64_t record, Vec3 position, Vec3 anchor, Vec3 normal,
                const WorldQueries& world, uint32_t& surface) {
    std::optional<RayHit> hit;
    const Vec3 to = anchor - position;
    if (const float reach = length(to); reach > .05f)
        hit = safeRay(world, position, to / reach, reach + .6f);
    if (!hit || !hit->fixed)
        hit = safeRay(world, position, normal * -1.f, 2.f);
    if (!hit || !hit->fixed || !finite(hit->point) || !finite(hit->normal) || length(hit->normal) < .5f)
        return false;
    Vec3 n = normalized(hit->normal);
    if (dot(n, position - hit->point) < 0)
        n = n * -1.f;
    if (n.y > .5f)
        return false; // a floor
    const Vec3 up{0, 1, 0};
    Vec3 along = up - n * dot(up, n);
    if (length(along) < .3f) {
        const Vec3 away = position - heroBody.position;
        along = finite(away) ? away - n * dot(away, n) : Vec3{};
    }
    if (length(along) < .3f)
        along = cross(n, Vec3{1, 0, 0});
    const uint32_t pose = n.y < -.5f ? 7u : std::abs(n.y) < .2f ? 6u : 5u;
    surface = static_cast<uint32_t>(hit->surface);
    return native_bodies::stick(machine, record, surface, hit->point, n, normalized(along), pose);
}
// Asks the game to stick him to his surface: the point and body a ray from
// his centre meets toward where the web stuck (or against the surface's
// normal), its normal facing him, and a direction along it - up a wall;
// under a ceiling away from the player, his head away from him.
// Asks the stick; counted, and timed for the next ask, only once it is
// made (a ray that met nothing is tried again at the next step, not 0.6 s on).
bool askStick(Tracked& t, const Tie& tie, const WorldQueries& world) {
    const Vec3 out = tie.place - tie.anchor;
    const Vec3 normal = length(out) > .01f ? normalized(out) : Vec3{0, 1, 0};
    uint32_t surface{};
    if (const auto line = lineTies.find(t.who.record); line != lineTies.end()) {
        if (!stickOnLine(t.who.machine, t.who.record, line->second))
            return false;
        surface = line->second.surface;
    } else if (!askStickAt(t.who.machine, t.who.record, t.position, tie.anchor, normal, world, surface)) {
        return false;
    }
    t.tieAsked = 0;
    ++t.tieSticks;
    native_bodies::markState(t.who.machine, tie.anchor, normal, t.position, 2, surface);
    return true;
}
void hold(Tracked& t, const TargetCommand& c, const Tie& tie, float dt, const WorldQueries& world, bool flung,
          bool down, Vec3 gravity) {
    t.free = -1;
    t.snatched = false;
    t.falling = false;
    const float left = length(tie.place - t.position);
    if (t.tieWebbed >= 0)
        t.tieWebbed += dt;
    if (t.tieContact >= 0) {
        t.tieContact += dt;
    } else {
        // Arriving: within reach of his place, or about to touch it at the
        // speed he comes; or taken down there by the game.
        const float speed = length(t.flying ? t.expected : t.velocity);
        if (left < std::max(tieStickReach, speed * tieStickLead) || (down && left < tieCarryBack))
            t.tieContact = 0;
    }
    // Arrived and against his surface short of his place: held where he is.
    if (t.tieContact >= 0 && !tie.arrived && left < tieSettleReach) {
        if (left < t.tieNearest - tieSettleGain) {
            t.tieNearest = left;
            t.tieStill = 0;
        } else if ((t.tieStill += dt) >= tieSettleSeconds) {
            ties.settle(t.who.record, t.position);
        }
    }
    // Arrived: webbed as a web ball webs him, his whole cocoon in the first
    // hit, until the game sticks him; under a ceiling at his head, slower.
    const bool up = overhead(tie);
    // The state log: an overhead tie that arrived and was webbed, not stuck.
    if (up && t.tieContact >= 2 && !t.tieMarked) {
        const Vec3 out = tie.place - tie.anchor;
        native_bodies::markState(t.who.machine, tie.anchor, length(out) > .01f ? normalized(out) : Vec3{},
                                 t.position, 0);
        t.tieMarked = true;
    }
    // Out of his cocoon's state (yanked into the air first): webbed at once,
    // so his cocoon is whole when he comes to the surface.
    if (!t.tiePrewebbed && t.tieContact < 0) {
        t.tiePrewebbed = true;
        if (!game_targets::cocooned(base, t.who)) {
            encase(t, tie);
            t.tieWebs = 0; // the hits at his touch still go out
        }
    }
    const uint32_t most = up ? tieOverheadWebs : tieWebsMost;
    const float every = up ? tieOverheadEvery : tieWebEvery;
    if (t.tieContact >= 0 && t.tieWebs < most && (t.tieWebbed < 0 || t.tieWebbed >= every))
        encase(t, tie);
    if (t.tieAsked >= 0)
        t.tieAsked += dt;
    const bool atSurface = tie.arrived || left < tieAskReach || t.tieContact >= tieAskLate;
    if (t.tieContact >= 0 && t.tieContact < tieAskGiveUp && atSurface && left < tieSettleReach &&
        t.tieSticks < tieAsksMost && (t.tieAsked < 0 || t.tieAsked >= tieAskEvery))
        askStick(t, tie, world);
    if (down) {
        // The game has him (a flop, a wall stick to come): nothing steers him.
        if (t.steering)
            steer(t, false, {});
        t.steering = t.flying = t.launching = false;
        return;
    }
    if (flung) {
        if (t.steering) {
            steer(t, false, {});
            t.steering = false;
        }
        // The flight's first step takes the launch; then the tie's law from
        // the velocity it gave him, which the flight keeps.
        const Vec3 from = t.launching ? t.launch : t.flying ? t.expected : t.paced;
        fly(t, advance(c, t.position, from, dt, gravity), t.launching);
        t.launching = false;
        t.flying = t.flew = true;
        return;
    }
    t.flying = false;
    // Touched and near his place, the flight over: no more blows (they made
    // him twitch); his mover holds him there while the webbing sticks him.
    const bool carry = t.tieContact < 0 || left > tieCarryBack;
    if (carry && (t.knocked < 0 || t.knocked > tieReknockSeconds)) {
        if (t.steering) {
            steer(t, false, {});
            t.steering = false;
        }
        t.launch =
            limited(tieLaunch(described(t.who, t.position, t.velocity), tie, ties.config()), maxDriveSpeed);
        knock(t, t.launch, tieKnockDamage);
        t.launching = true;
        t.launchAge = 0;
        t.flingAsked = false;
        ++tieKnocks;
        return;
    }
    // The blow lands within a step or two.
    if (carry && t.knocked < knockWait)
        return;
    // Not flung (a heavy, a scripted scene), or held at the surface: his mover.
    if (carry && !t.flingAsked) {
        native_bodies::fling(t.who.machine, t.who.record, limited(c.velocity, maxDriveSpeed));
        t.flingAsked = true;
    }
    Vec3 v = advance(c, t.position, t.velocity, dt, gravity);
    t.grounded = guard(t, v, dt, world);
    steer(t, true, v);
    t.steering = true;
}
void release(Tracked& t, bool thrown, Vec3 velocity, bool flung);
// An enemy yanked by a player in the air comes to the player, wherever the
// player goes meanwhile and whether or not the grip still holds, stops an
// arm's length before him at chest height and floats there, to be hit:
// "like dmc air combat". Until October 12 an air yank was the ground's: an
// arc to where the hand was. In that day's 14:06 session all 16 yanks were
// let go of a quarter of a second in (a flick of the wrist), and none of
// their thugs came within reach.
// He flies at snatchSpeed at most, slower near his place (snatchGain per
// second of what is left), on top of the player's own velocity. Within
// snatchNear of it he has arrived: the player hangs (game_punch) and the
// headset slows time (the catch counts). It is over floatSeconds after that,
// snatchSeconds after the yank, when the player lands, or when a fist takes
// him (struck()); then the game's flight has him as the web left him.
constexpr float snatchSpeed = 28, snatchGain = 7, snatchReach = 1.05f, snatchNear = .7f, snatchSeconds = 3,
                floatSeconds = 1.6f;
void snatch(Tracked& t, float dt, bool flung) {
    t.snatchAge += dt;
    // The player's velocity is the game's; the web's are real.
    const float scale = std::clamp(native_bodies::timeScale(), .05f, 1.f);
    const Vec3 carried = finite(heroBody.velocity) ? heroBody.velocity * scale : Vec3{};
    if (heroBody.grounded || t.snatchAge > snatchSeconds || t.floated > floatSeconds || !finite(heroBody.position)) {
        t.snatched = false;
        release(t, false, t.velocity, flung);
        return;
    }
    // His place: before the player on the side he comes from, at chest height.
    const Vec3 chest = heroBody.position + Vec3{0, .2f, 0};
    Vec3 side{t.position.x - chest.x, 0, t.position.z - chest.z};
    side = length(side) > .05f ? normalized(side) : Vec3{0, 0, 1};
    const Vec3 to = chest + side * (snatchReach + shape(t.who).radius) - t.position;
    const float left = length(to);
    Vec3 v = carried;
    if (left > 1e-3f)
        v += to * (std::min(snatchSpeed, left * snatchGain) / left);
    v = limited(v, maxDriveSpeed);
    if (flung) {
        fly(t, v, t.launching || !t.flying);
        t.launching = false;
        t.flying = t.flew = true;
    } else if (!t.launching) {
        launchInto(t, v);
    } else {
        t.launch = v;
    }
    if (left < snatchNear || t.floated > 0) {
        if (t.floated == 0) {
            game_punch::hangInAir(floatSeconds);
            AcquireSRWLockExclusive(&output);
            InterlockedIncrement64(&SpidyGrabData.sequence);
            ++SpidyGrabData.catches;
            InterlockedIncrement64(&SpidyGrabData.sequence);
            ReleaseSRWLockExclusive(&output);
        }
        t.floated += dt;
    }
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
                if (!game_targets::fighting(base, c) || c.record == t.who.record ||
                    !game_targets::position(c, feet) ||
                    !touchesBot(t.position, shape(t.who).radius, c, feet, strikes))
                    continue;
                // The one struck is knocked away from the one flying into him.
                if (blow(c.record, t.who.record, feet + Vec3{0, shape(c).lift, 0}, t.paced,
                         strikeBlow(speed, strikes))) {
                    ++struck;
                    game_focus::earn(hero, game_focus::Kind::botFlungIntoBot);
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
        if (!game_targets::fighting(base, c) || !game_targets::position(c, feet) ||
            !touchesBot(t.position, shape(t.who).radius, c, feet, strikes))
            continue;
        if (blow(c.record, hero, feet + Vec3{0, shape(c).lift, 0}, t.velocity, strikeBlow(speed, strikes))) {
            ++struck;
            game_focus::earn(hero, game_focus::Kind::throwObjectIntoBot);
        }
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
    // The game's Focus for a yank or a throw of an enemy in the fight, as for
    // its own (game_focus.hpp), and its bonus for a yank into the air.
    for (auto i = from; i < events.size(); ++i) {
        const auto& e = events[i];
        if (e.kind != GrabEventKind::Yank && e.kind != GrabEventKind::Throw)
            continue;
        if (std::none_of(candidates.begin(), candidates.end(), [&](const Candidate& c) {
                return c.record == e.target && game_targets::fighting(base, c);
            }))
            continue;
        if (e.kind == GrabEventKind::Throw) {
            game_focus::earn(hero, game_focus::Kind::throwBot);
        } else {
            game_focus::earn(hero, game_focus::Kind::yankBot);
            if (!heroBody.grounded)
                game_focus::earn(hero, game_focus::Kind::yankBotIntoAirBonus);
        }
    }
    // A thug yanked by a player in the air comes to him (snatch()).
    for (auto i = from; i < events.size(); ++i)
        if (events[i].kind == GrabEventKind::Yank && !heroBody.grounded)
            for (auto& t : tracked)
                if (t.who.record == events[i].target && t.who.kind == Kind::bot && !t.gone && !t.snatched) {
                    t.snatched = true;
                    t.snatchAge = t.floated = 0;
                }
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
        at += Vec3{0, shape(t.who).lift, 0};
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
    ties = WebTies(TieConfig{});
    tieReady = tiesAllowed = statesWatched = false;
    tiesDown = tiesStuck = tieKnocks = tieWebHits = 0;
    finishing.clear();
    finishes = 0;
    cocoonSeen.clear();
    lately[0] = lately[1] = {};
    lineTies.clear();
    hangs.clear();
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
    // The ties go with the grab: their targets are handed to the game below.
    ties.clear();
    finishing.clear();
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
        // A tied enemy stays tied (a pause, a menu): he is held again once
        // play steps on.
        if (ties.tied(t.who.record))
            continue;
        if (t.who.kind == Kind::throwable)
            native_bodies::release(t.actor);
        else if (t.steering && t.grounded)
            steer(t, false, {}); // on his feet: he stands
        else if (t.steering || t.falling)
            handOff(t, t.velocity); // the game's flight brings it down now
        // A flight the web steered goes on as the game flies it.
    }
    tracked.erase(std::remove_if(tracked.begin(), tracked.end(),
                                 [](const Tracked& t) { return !ties.tied(t.who.record); }),
                  tracked.end());
    commands.clear();
    AcquireSRWLockExclusive(&output);
    for (auto& hand : SpidyGrabData.hands)
        hand = {};
    ReleaseSRWLockExclusive(&output);
}
void game_grab::struck(uint64_t record) {
    for (auto& t : tracked)
        if (t.snatched && t.who.record == record) {
            t.snatched = false;
            t.flying = false;
            t.launching = false;
            t.free = 0;
        }
}
bool game_grab::offering() {
    return offered != 0;
}
void game_grab::allow(bool on) {
    allowed = on;
}
void game_grab::allowTies(bool on) {
    tiesAllowed = on;
    core.yankOnlyCharacters(on);
    if (!on) {
        ties.clear();
        finishing.clear();
        tieReady = false;
    }
}
Input game_grab::claim(float inputSeconds, const Input& in, const WorldQueries& world, const Body& player,
                       unsigned kept) {
    if (!offered)
        return in;
    if (!allowed) {
        // Switched off: what the webs hold or still trail goes back to the game.
        ties.clear();
        if (holds(0) || holds(1) || !tracked.empty())
            cancel();
        return in;
    }
    // Picks and owners look the targets up in this copy of the watch's list.
    if (inputSeconds > 0)
        watch.current(candidates);
    heroBody = player;
    auto out = core.claim(inputSeconds, in, world, targets, player, kept);
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
    for (unsigned i = 0; i < 2; ++i) {
        const auto& g = core.grabs()[i];
        if (g.phase != GrabPhase::None && g.kind == TargetKind::Character)
            lately[i] = {g.target, 0};
        else
            lately[i].since += dt;
    }
    count(counted);
    counted = 0;
    const Vec3 gravity{0, -core.config().gravity, 0};
    // The ties: a target knocked out is the game's again; the fight goes on
    // while an enemy who is not tied is in it near the player. Only while
    // there are ties or finishing blows to see to (and once after, to stop
    // watching their states): without them nothing here runs.
    if (!ties.ties().empty() || !finishing.empty() || statesWatched) {
        std::vector<uint64_t> taken, down, stuck;
        for (const auto& g : core.grabs())
            if (g.phase != GrabPhase::None)
                taken.push_back(g.target);
        for (const auto& tie : ties.ties()) {
            auto* t = track(tie.target);
            if (!t)
                continue;
            // Stuck by the game in his cocoon: done. Down otherwise: the tie
            // webs him on (hold) until the game sticks him, or lets go once
            // he stays down (knocked out).
            if (game_targets::wallStuck(base, t->who))
                stuck.push_back(tie.target);
            else if (!game_targets::up(base, t->who) && t->tieContact >= tieDownSeconds)
                down.push_back(tie.target);
        }
        // The state log (native_bodies.hpp): what the game asks for tied
        // bots, and what the tie knew as the game stuck one.
        for (const auto record : stuck)
            if (const auto* t = track(record))
                for (const auto& tie : ties.ties())
                    if (tie.target == record) {
                        const Vec3 out = tie.place - tie.anchor;
                        const Vec3 normal = length(out) > .01f ? normalized(out) : Vec3{};
                        native_bodies::markState(t->who.machine, tie.anchor, normal, t->position, 1);
                        if (finishing.size() < 32)
                            finishing.push_back({record, t->who.machine, tie.anchor, normal});
                        if (const auto line = lineTies.find(record);
                            line != lineTies.end() && hangs.size() < 64) {
                            hangs.erase(std::remove_if(hangs.begin(), hangs.end(),
                                                       [&](const Hang& h) { return h.record == record; }),
                                        hangs.end());
                            hangs.push_back({record, t->who.machine, line->second.line, line->second.point});
                        }
                    }
        for (int list = 0; list < 2; ++list)
            for (const auto record : list ? down : stuck) {
                // The game has him where he is. Let go without handing him to
                // a flight, which would pull him off the wall.
                if (auto* t = track(record)) {
                    if (t->steering)
                        steer(*t, false, {});
                    t->steering = t->flying = t->launching = t->falling = false;
                }
                ties.drop(record);
                ++(list ? tiesDown : tiesStuck);
            }
        bool fightOn{};
        for (const auto& c : candidates) {
            Vec3 at{};
            if (!fightOn && !ties.tied(c.record) && game_targets::fighting(base, c) &&
                game_targets::position(c, at))
                fightOn = !finite(heroBody.position) || length(at - heroBody.position) <= fightReach;
        }
        ties.step(dt, targets, fightOn, taken, commands);
        // The stuck, finished where they hang (Finish).
        finishing.erase(
            std::remove_if(
                finishing.begin(), finishing.end(),
                [&](Finish& f) {
                    const game_targets::Candidate* c{};
                    for (const auto& each : candidates)
                        if (each.record == f.record)
                            c = &each;
                    if (!c || f.age > finishEnd + finishCheck)
                        return true;
                    f.age += dt;
                    Vec3 at{};
                    if (!game_targets::position(*c, at) || !finite(at))
                        return true; // gone: nothing to finish, no ray from nowhere
                    const bool stuckNow = game_targets::wallStuck(base, *c);
                    if (!f.struck) {
                        if (f.age < finishAfter)
                            return false;
                        if (!stuckNow)
                            return f.age > finishEnd; // off his surface: not ours to finish
                        float points{};
                        game_targets::health(*c, points);
                        native_bodies::Damage d;
                        d.victim = f.record;
                        d.damager = hero;
                        d.point = at;
                        d.direction = f.normal * -1.f;
                        d.normal = f.normal;
                        d.amount = std::max(points, 1.f) + finishExtra;
                        d.type = 20;     // kWebImpact: the web finished him
                        d.knockback = 0; // kNone: he stays where he hangs
                        d.knockbackAmount = 0;
                        d.impulse = 0;
                        f.struck = native_bodies::damage(d) != 0;
                        if (f.struck) {
                            ++finishes;
                            f.age = 0;
                            native_bodies::markState(f.machine, f.anchor, Vec3{points, 0, 0}, at, 3);
                        }
                        return false;
                    }
                    if (!f.checked && f.age >= finishCheck) {
                        f.checked = true;
                        native_bodies::markState(f.machine, f.anchor, f.normal, at, stuckNow ? 4 : 5);
                    }
                    if (f.checked && !stuckNow && f.resticks < finishResticks &&
                        f.age >= finishCheck + .4f * f.resticks) {
                        ++f.resticks;
                        uint32_t surface{};
                        if (const auto line = lineTies.find(f.record); line != lineTies.end())
                            stickOnLine(f.machine, f.record, line->second);
                        else
                            askStickAt(f.machine, f.record, at, f.anchor, f.normal, world, surface);
                    }
                    if (f.age >= finishEnd) {
                        if (stuckNow && !game_targets::up(base, *c))
                            native_bodies::markState(f.machine, f.anchor, f.normal, at, 6);
                        return true;
                    }
                    return false;
                }),
            finishing.end());
        {
            uint64_t machines[8]{};
            unsigned watching{};
            for (const auto& tie : ties.ties())
                if (const auto* t = track(tie.target); t && t->who.machine && watching < 8)
                    machines[watching++] = t->who.machine;
            native_bodies::watchStates(machines, watching);
            statesWatched = watching != 0;
        }
    }
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
        const bool tiedNow = ties.tied(t.who.record);
        // What it struck since the step before, under what it was given then
        // (a tied enemy is brought onto his surface, not struck against it).
        if (!tiedNow)
            strike(t, dt, flung, gravity);
        t.speeds[2] = t.speeds[1];
        t.speeds[1] = t.speeds[0];
        t.speeds[0] = length(t.paced);
        t.back2 = t.back1;
        t.back1 = t.position;
        t.dt1 = dt;
        ++t.seenSteps;
        if (tiedNow && command) {
            const Tie* tie{};
            for (const auto& each : ties.ties())
                if (each.target == t.who.record)
                    tie = &each;
            if (tie) {
                hold(t, *command, *tie, dt, world, flung, !game_targets::up(base, t.who), gravity);
                continue;
            }
        }
        if (t.snatched) {
            snatch(t, dt, flung);
            continue;
        }
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
                                                       !trailing(t.who.record) && !ties.tied(t.who.record));
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
    InterlockedIncrement64(&SpidyTieData.sequence);
    SpidyTieData.status = 2;
    SpidyTieData.made = ties.made();
    SpidyTieData.ended = ties.ended();
    SpidyTieData.taken = ties.taken();
    SpidyTieData.gone = ties.gone();
    SpidyTieData.down = tiesDown;
    SpidyTieData.knocks = tieKnocks;
    SpidyTieData.stuck = tiesStuck;
    SpidyTieData.webs = tieWebHits;
    SpidyTieData.armed = tieReady ? 1u : 0u;
    SpidyTieData.count = 0;
    for (const auto& tie : ties.ties()) {
        const auto* t = track(tie.target);
        if (!t || t->gone || SpidyTieData.count >= std::size(SpidyTieData.lines))
            continue;
        const auto line = lineTies.find(tie.target);
        SpidyTieData.lines[SpidyTieData.count++] = {
            line != lineTies.end() ? line->second.point : tie.anchor, t->position,
            length(tie.place - t->position),
            (t->flying ? 1u : 0u) | (t->steering ? 2u : 0u) | (tie.arrived ? 4u : 0u) |
                (t->tieContact >= 0 ? 8u : 0u) | (game_targets::up(base, t->who) ? 0u : 16u)};
    }
    // Thugs hung under web lines, each on its strand: gone ones let go of.
    SpidyTieData.hangCount = 0;
    hangs.erase(std::remove_if(hangs.begin(), hangs.end(),
                               [&](Hang& h) {
                                   const game_targets::Candidate* c{};
                                   for (const auto& each : candidates)
                                       if (each.record == h.record)
                                           c = &each;
                                   Vec3 at{};
                                   if (!c || !game_targets::position(*c, at) || !finite(at)) {
                                       lineTies.erase(h.record);
                                       return true;
                                   }
                                   const auto tick = GetTickCount64();
                                   if (game_targets::wallStuck(base, *c))
                                       h.loose = 0;
                                   else if (!h.loose)
                                       h.loose = tick;
                                   if (h.loose && tick - h.loose >= hangLooseMs) {
                                       // Fallen off: still standing, he is finished as the stick
                                       // would have finished him; no knock, he falls as he is.
                                       if (game_targets::up(base, *c)) {
                                           float points{};
                                           game_targets::health(*c, points);
                                           native_bodies::Damage d;
                                           d.victim = h.record;
                                           d.damager = hero;
                                           d.point = at;
                                           d.direction = {0, -1, 0};
                                           d.normal = {0, 1, 0};
                                           d.amount = std::max(points, 1.f) + finishExtra;
                                           d.type = 20; // kWebImpact
                                           d.knockback = 0;
                                           d.knockbackAmount = 0;
                                           d.impulse = 0;
                                           native_bodies::damage(d);
                                       }
                                       lineTies.erase(h.record);
                                       return true;
                                   }
                                   if (h.loose)
                                       return false; // a moment loose: no strand drawn to him meanwhile
                                   if (SpidyTieData.hangCount < std::size(SpidyTieData.hangs)) {
                                       float id{};
                                       std::memcpy(&id, &h.line, sizeof(id));
                                       SpidyTieData.hangs[SpidyTieData.hangCount++] = {
                                           h.top, at + Vec3{0, shape(*c).lift + .55f, 0}, id,
                                           static_cast<uint32_t>(h.record >> 4)};
                                   }
                                   return false;
                               }),
                hangs.end());
    InterlockedIncrement64(&SpidyTieData.sequence);
    ReleaseSRWLockExclusive(&output);
}
void game_grab::keepLines(const uint32_t* ids, uint32_t count) {
    // A line that went: who hung from it falls (out of his stick into the
    // game's flight, at rest).
    hangs.erase(std::remove_if(hangs.begin(), hangs.end(),
                               [&](const Hang& h) {
                                   for (uint32_t i = 0; ids && i < count; ++i)
                                       if (ids[i] == h.line)
                                           return false;
                                   // Stuck and defeated, the game's flight alone does not move
                                   // him (he stayed in the air, November 3, 19:00): a blow
                                   // down that knocks him off too.
                                   native_bodies::fling(h.machine, h.record, {0, -1, 0});
                                   native_bodies::Damage d;
                                   d.victim = h.record;
                                   d.damager = hero;
                                   d.point = h.top - Vec3{0, lineHang, 0};
                                   d.direction = {0, -1, 0};
                                   d.normal = {0, 1, 0};
                                   d.amount = 1;
                                   d.type = 7;      // kKinetic
                                   d.knockback = 5; // kFlyBack
                                   d.knockbackAmount = 1;
                                   native_bodies::damage(d);
                                   lineTies.erase(h.record);
                                   return true;
                               }),
                hangs.end());
}
bool game_grab::cocooned(uint64_t record) {
    if (!offered || !allowed || !record)
        return false;
    for (const auto& c : candidates) {
        if (c.record != record)
            continue;
        if (c.kind != Kind::bot)
            return false;
        const auto now = GetTickCount64();
        if (game_targets::cocooned(base, c) || game_shooter::webbedUp(record)) {
            if (cocoonSeen.size() > 256)
                cocoonSeen.clear();
            cocoonSeen[record] = now;
            return true;
        }
        const auto seen = cocoonSeen.find(record);
        return seen != cocoonSeen.end() && now - seen->second <= static_cast<uint64_t>(cocoonMemory * 1000) &&
               game_targets::up(base, c);
    }
    return false;
}
uint64_t game_grab::heldLately(unsigned hand) {
    if (!offered || !allowed || hand > 1)
        return 0;
    const auto& g = core.grabs()[hand];
    if (g.phase != GrabPhase::None)
        return g.kind == TargetKind::Character ? g.target : 0;
    return lately[hand].since <= heldGrace ? lately[hand].target : 0;
}
bool game_grab::tie(unsigned hand, Vec3 anchor, Vec3 normal, uint32_t lineSurface, Vec3 lineAlong,
                    uint32_t line) {
    const bool onLine = line != 0;
    // Under a web line: carried to his hang, lineHang under it - the tie's
    // place, its normal down, overheadReach + gap under its anchor. Clear of
    // the thugs already hung from it (hangSpacing apart along it): two hung
    // closer pushed each other off it (November 4, 10:59).
    Vec3 linePoint = anchor;
    if (onLine && finite(lineAlong) && length(lineAlong) > .5f) {
        constexpr float hangSpacing = 1.1f;
        const Vec3 a = normalized(lineAlong);
        const auto clear = [&](Vec3 at) {
            for (const auto& h : hangs)
                if (h.line == line && length(h.top - at) < hangSpacing)
                    return false;
            for (const auto& held : lineTies)
                if (held.second.line == line && length(held.second.point - at) < hangSpacing)
                    return false;
            return true;
        };
        for (int k = 0; k <= 8; ++k) {
            const float shift = (k + 1) / 2 * hangSpacing * (k % 2 ? 1.f : -1.f);
            if (clear(anchor + a * shift)) {
                linePoint = anchor + a * shift;
                break;
            }
        }
    }
    if (onLine) {
        const auto& config = ties.config();
        anchor = linePoint - Vec3{0, lineHang - config.overheadReach - config.gap, 0};
        normal = {0, -1, 0};
    }
    if (!offered || !allowed || !tiesAllowed || hand > 1 || !finite(anchor))
        return false;
    // Its web's target now, or the one it held lately (a yanked thug at the hand).
    const auto record = heldLately(hand);
    auto* t = record ? track(record) : nullptr;
    if (!t && record)
        for (const auto& c : candidates) {
            Vec3 at{};
            if (c.record == record && c.kind == Kind::bot && game_targets::live(base, c) &&
                game_targets::position(c, at)) {
                t = follow(c, at + Vec3{0, shape(c).lift, 0});
                break;
            }
        }
    if (!t || t->gone || t->who.kind != Kind::bot)
        return false;
    if (!ties.add(described(t->who, t->position, t->velocity), anchor, normal))
        return false;
    core.handOver(static_cast<int>(hand));
    trails[hand] = {};
    lately[hand] = {};
    // The state log follows tied bots from the first tie on.
    native_bodies::logStates();
    if (onLine)
        lineTies[t->who.record] = {lineSurface, line, lineAlong, linePoint, linePoint - Vec3{0, lineHang, 0}};
    else
        lineTies.erase(t->who.record);
    t->snatched = false;
    t->tieContact = t->tieWebbed = -1;
    t->tieWebs = 0;
    t->tieNearest = 1e9f;
    t->tieStill = 0;
    t->tieMarked = false;
    t->tieSticks = 0;
    t->tieAsked = -1;
    t->tiePrewebbed = false;
    t->knocked = -1;
    return true;
}
void game_grab::tieArmed(bool on) {
    tieReady = on;
    AcquireSRWLockExclusive(&output);
    SpidyTieData.armed = on ? 1u : 0u;
    ReleaseSRWLockExclusive(&output);
}
TieData game_grab::tieData() {
    AcquireSRWLockShared(&output);
    auto out = SpidyTieData;
    ReleaseSRWLockShared(&output);
    return out;
}
Data game_grab::data() {
    AcquireSRWLockShared(&output);
    auto out = SpidyGrabData;
    ReleaseSRWLockShared(&output);
    return out;
}
