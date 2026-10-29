// Swing prediction runs inside a native world-query lease. MoverStandard remains
// responsible for body collision and committing the resulting position.
#include "spidy/game_swing.hpp"
#include "spidy/game_dodge.hpp"
#include "spidy/game_focus.hpp"
#include "spidy/game_sound.hpp"
#include "spidy/crawl_hands.hpp"
#include "spidy/game_grab.hpp"
#include "spidy/game_offers.hpp"
#include "spidy/game_punch.hpp"
#include "spidy/game_shooter.hpp"
#include "spidy/game_throw.hpp"
#include "spidy/game_warp.hpp"
#include "spidy/native_bodies.hpp"
#include "spidy/native_movement.hpp"
#include "spidy/native_query_context.hpp"
#include "spidy/native_rays.hpp"
#include <atomic>
#include <cstring>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_swing;
extern "C" {
__declspec(dllexport) Data SpidySwingData;
// The aim markers' previews (SpidyAimSample); odd `sequence` while it changes.
__declspec(dllexport) AimData SpidyAimData;
// Physical webs' telemetry (Ropes); odd `sequence` while it changes.
__declspec(dllexport) Ropes SpidyRopeData;
__declspec(dllexport) ClimbData SpidyClimbData;
}
namespace {
// Each hand's web's edges (Bends, SpidySwingBends), written with Data.
Bends bends;
}
namespace {
using Call = DWORD(WINAPI*)(void*);
// A movement command must outlast the longest step the solver predicts
// (longestNativeStep) twice over, because visit() tolerates one missed
// observation, plus the 16 ms grain of the tick count its lease is measured
// with.
constexpr uint32_t motionLeaseMs = static_cast<uint32_t>(2000 * longestNativeStep) + 16;
static_assert(motionLeaseMs > 200 && motionLeaseMs <= 250, "the movement module takes leases up to 250 ms");
Call startMotion{}, submitMotion{}, sampleMotion{}, stopMotion{}, retargetMotion{}, driveMotion{},
    sampleDriven{};
Config config;
Command command;
Swing solver;
CrawlHands crawlHands;
VentCrawlMode ventCrawlMode;
InputSampleClock inputClock, grabInputClock;
InputHold inputHold;
// The game's own move out of the swing's flight, asked for (Command::asks).
AirYield airYield;
InFlightStep inFlight;
SRWLOCK lifecycle = SRWLOCK_INIT, control = SRWLOCK_INIT, output = SRWLOCK_INIT, simulation = SRWLOCK_INIT;
std::atomic<bool> enabled{};
// coasting: owned flight going on without input (coast()).
bool started{}, owned{}, initialized{}, coasting{};
// The game's grasp of a pressed body, latched (see `grasped` in visit): since
// when, and the body's height then. Zero: none.
uint64_t graspedAt{};
float graspHeight{};
// The game's own crawl asked for where the player came to rest on the swing's
// wall (see `handing` in visit): how long he has rested, the request and when
// the game began on it, the body's height then; a rest the game would not take
// (not asked again until he walks).
float restSeconds{}, crawlHeight{};
uint64_t crawlAskedAt{};
bool crawlBegun{}, crawlDeclined{};
// The wall the game's crawl last held the player on, where its own wall run
// goes on (see `gameRun` in visit), and the hand-over as the last step saw it
// (game_warp::Data::swing).
RayHit crawlWall{};
// The game crawls the player through a vent or a duct (a floor crawl seen,
// not since stood or flown): no mount.
bool ventMode{};
// No mount before this tick either: a use of one of the game's interactions
// (game_offers) puts its crawl under the player, and the stick he still holds
// would walk him off it onto the swing's wall before the game's interact
// came.
uint64_t usePauseUntil{};
uint32_t steppedView{};
// The game's own wall run, begun (see `gameRun` in visit): it has the player
// until it lets go of the wall.
bool gameRunning{};
uint64_t deadline{}, inputDeadline{}, motionSerial{}, lastStep{}, worldIdentity{};
// Step length the last prediction assumed, and the native step it was made in.
float predictedDt{};
uint64_t predictedStep{};
// The rope a web has to spare while the game walks the player (Swing::payOut), m.
constexpr float groundSlack = .1f;
SwingTakeoff takeoffTransition;
// The same jump for a player who goes up onto a wall from the ground or out
// of the game's crawl (Swing::mounting): the wall takes the body as it leaves.
SwingTakeoff mountTakeoff;
LARGE_INTEGER frequency{};
// When the web strike (game_punch::fly) last had its say over a step: the
// real time between two is the step it flies the player through. And whether
// it flew him then, or the game's own move out of the swing's flight had him
// (its zip to a point, AirYield): a web that carries him, as a swing's does
// (game_dodge).
int64_t strikeQpc{};
std::atomic<bool> strikeFlying{}, gameMoveFlying{};
// Aim previews are made while sampled before this tick, once per input
// command; the latest was made at aimMadeMs.
std::atomic<uint64_t> aimDeadline{};
uint64_t aimSerial{}, aimMadeMs{};
// The hands whose motion lands no blow (Settings::noBlows).
std::atomic<uint32_t> noBlows{};
std::atomic<bool> combatVisuals{true};
std::atomic<bool> halfPunchDamage{};
bool copy(void* destination, const void* source, size_t bytes) {
    __try {
        if (reinterpret_cast<uintptr_t>(source) < 0x10000 ||
            reinterpret_cast<uintptr_t>(destination) < 0x10000)
            return false;
        std::memcpy(destination, source, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void relinquishMotion() {
    if (owned) {
        native_movement::Command c;
        c.serial = ++motionSerial;
        submitMotion(&c);
    }
    owned = false;
}
// The swing alone: a perched player's webs are let go, a grab keeps its target.
// keepWall: the player stays on the wall it is on (a paused game goes on from
// there); otherwise the body may be anywhere by the next step.
void cancelSwing(bool keepWall = false) {
    if (!keepWall) {
        crawlHands.reset();
        ventCrawlMode.reset();
    }
    relinquishMotion();
    game_punch::cancelStrike();
    strikeQpc = 0;
    coasting = false;
    takeoffTransition.reset();
    mountTakeoff.reset();
    solver.releaseAll();
    if (!keepWall)
        solver.leaveWall();
    inputClock.reset();
    inFlight.reset();
    predictedDt = 0;
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidySwingData.sequence);
    SpidySwingData.owned = SpidySwingData.takeoff = SpidySwingData.mount = 0;
    SpidySwingData.takeoffPhase = SpidySwingData.takeoffAttempts = 0;
    for (auto& web : SpidySwingData.webs)
        web = {};
    if (!keepWall) {
        InterlockedIncrement64(&SpidyClimbData.sequence);
        SpidyClimbData.status = SpidyClimbData.hands = SpidyClimbData.crawl = 0;
        SpidyClimbData.crawlMove = {};
        InterlockedIncrement64(&SpidyClimbData.sequence);
        SpidySwingData.wall = 0;
        SpidySwingData.wallNormal = {};
        SpidySwingData.wallDistance = 0;
    }
    InterlockedIncrement64(&SpidySwingData.sequence);
    ReleaseSRWLockExclusive(&output);
}
void cancel() {
    cancelSwing();
    game_grab::cancel();
    game_shooter::cancel();
    grabInputClock.reset();
    inputHold.reset();
}
// Flight the swing owns goes on without input: its webs, grabs and shots end,
// and the body flies on under the swing's gravity until it lands, when the
// game has it back. Handed to the game in midair, it fell at the game's own
// fall speed for the time spent airborne instead (InputHold). Once per stretch
// without input.
void coast() {
    if (coasting)
        return;
    coasting = true;
    crawlHands.reset();
    ventCrawlMode.reset();
    takeoffTransition.reset();
    mountTakeoff.reset();
    solver.releaseAll();
    inputClock.reset();
    game_punch::cancelStrike();
    game_grab::cancel();
    game_shooter::cancel();
    grabInputClock.reset();
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidySwingData.sequence);
    SpidySwingData.takeoff = SpidySwingData.takeoffPhase = SpidySwingData.takeoffAttempts = 0;
    SpidySwingData.mount = 0;
    for (auto& web : SpidySwingData.webs)
        web = {};
    InterlockedIncrement64(&SpidySwingData.sequence);
    ReleaseSRWLockExclusive(&output);
}
void fault(uint32_t error) {
    cancel();
    enabled = false;
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidySwingData.sequence);
    SpidySwingData.status = 4;
    SpidySwingData.owned = 0;
    SpidySwingData.error = error;
    InterlockedIncrement64(&SpidySwingData.sequence);
    ReleaseSRWLockExclusive(&output);
}
// What a grip press would do now with each free hand (AimData): the grab's
// pick first, as a press makes it, then the swing's shot from where the
// player stands. Once per input command, while sampled.
void previewAims(const Command& c, const Input& in, const Body& player, const WorldQueries& world) {
    if (GetTickCount64() >= aimDeadline || c.serial == aimSerial)
        return;
    aimSerial = c.serial;
    Aim hands[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        const auto& h = in.hands[i];
        if (!h.tracked || solver.webs()[i].attached || game_grab::holds(i))
            continue;
        auto& aim = hands[i];
        if (solver.onStrand(static_cast<int>(i))) {
            aim = {static_cast<uint32_t>(AimKind::strand), 0, solver.webs()[1 - i].end(), {}};
            continue;
        }
        if (const auto target = game_grab::preview(i, h.aim, world)) {
            const auto kind = target->kind == TargetKind::Character ? AimKind::character : AimKind::prop;
            aim = {static_cast<uint32_t>(kind), target->radius, target->position, {}};
            continue;
        }
        const auto shot = solver.shot(h.aim, player.position, world);
        const auto& physics = solver.config();
        if (shot.web)
            aim = {static_cast<uint32_t>(shot.web->airAnchor ? AimKind::air : AimKind::anchor), 0, shot.web->anchor,
                   shot.hit ? shot.hit->normal : Vec3{}};
        else if (shot.hit)
            aim = {static_cast<uint32_t>(AimKind::blocked), 0, shot.hit->point, shot.hit->normal};
        else if (physics.airAnchors) // the body has no clear line to the air anchor
            aim = {static_cast<uint32_t>(AimKind::blocked), 0,
                   h.aim.position + normalized(h.aim.orientation.rotate({0, 0, -1})) * physics.maxRange, {}};
    }
    // What the eyes look at (EYE AIM): the game's camera is turned onto it for
    // a zip, from where that camera stands (game_xr).
    Aim gaze{};
    if (length(in.gaze) > .5f) {
        if (const auto hit = world.raycast(in.gazeOrigin, in.gaze, gazeRange))
            gaze = {static_cast<uint32_t>(AimKind::anchor), 0, hit->point, hit->normal};
        else
            gaze = {static_cast<uint32_t>(AimKind::air), 0, in.gazeOrigin + in.gaze * gazeRange, {}};
    }
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidyAimData.sequence);
    SpidyAimData.status = 2;
    SpidyAimData.serial = c.serial;
    for (unsigned i = 0; i < 2; ++i)
        SpidyAimData.hands[i] = hands[i];
    SpidyAimData.gaze = gaze;
    aimMadeMs = GetTickCount64();
    InterlockedIncrement64(&SpidyAimData.sequence);
    ReleaseSRWLockExclusive(&output);
}
void visit(const native_rays::QueryContext& world) {
    // Stop waits for this lock before stopping the movement module.
    AcquireSRWLockExclusive(&simulation);
    struct Unlock {
        ~Unlock() {
            ReleaseSRWLockExclusive(&simulation);
        }
    } unlock;
    if (!enabled)
        return;
    if (GetTickCount64() >= deadline) {
        cancel();
        enabled = false;
        return;
    }
    // The rays that stand in for the player's body pass what his own body
    // passes (native_rays::meets): his mover's collision profile.
    native_rays::setPlayer(config.record);
    Command c;
    AcquireSRWLockShared(&control);
    c = command;
    const bool live = c.focused && GetTickCount64() < inputDeadline;
    ReleaseSRWLockShared(&control);
    const bool focused = inputHold.update(c, live, GetTickCount64());
    native_movement::Data motion;
    if (sampleMotion(&motion)) {
        fault(3001);
        return;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    // Seconds since the player's mover last stepped.
    const double idle = motion.qpc && now.QuadPart >= static_cast<int64_t>(motion.qpc)
                            ? static_cast<double>(now.QuadPart - motion.qpc) / frequency.QuadPart
                            : std::numeric_limits<double>::infinity();
    const bool fresh = idle < .05;
    if (worldIdentity && worldIdentity != world.identity()) {
        cancel();
        worldIdentity = world.identity();
        return;
    }
    if (!focused && !owned) {
        cancel();
        worldIdentity = world.identity();
        return;
    }
    if (focused)
        coasting = false;
    else
        coast();
    // The player for picks, throws and aim previews: where it stands, and its
    // speed when moving.
    uintptr_t transform{};
    float feet[16]{};
    Vec3 at = motion.position;
    if (copy(&transform, reinterpret_cast<const void*>(config.record), sizeof(transform)) &&
        copy(feet, reinterpret_cast<const void*>(transform), sizeof(feet)) && finite({feet[12], feet[13], feet[14]}))
        at = {feet[12], feet[13], feet[14]};
    auto in = focused ? input(c) : Input{};
    const float sampleSeconds = focused ? grabInputClock.consume(c) : 0.f;
    const Vec3 currentUp = normalized({feet[4], feet[5], feet[6]});
    const bool supported = focused && !owned && motion.grounded &&
        (motion.status == 1 || motion.status == 2) && !(motion.moverFlags & 0x80000000u) &&
        finite(currentUp) && length(currentUp) > .9995f;
    const bool nativeFloor = supported && currentUp.y > .866f &&
        (motion.moverFlags & 1u) && !(motion.moverFlags & 0x80u);
    Vec3 crawlNormal = nativeFloor ? Vec3{0, 1, 0} : Vec3{};
    Vec3 crawlPoint = at;
    if (supported && currentUp.y < .866f) {
        const auto hit = world.bodyRaycast(at + currentUp * .4f, -currentUp, 1.2f);
        if (hit && hit->fixed && finite(hit->point) && finite(hit->normal) &&
            dot(normalized(hit->normal), currentUp) > .7f) {
            crawlNormal = normalized(hit->normal);
            crawlPoint = hit->point;
        } else if (ventMode && (motion.moverFlags & 0x81u)) {
            // Scripted shaft walls, like some duct floors, need not answer
            // world rays. Native support still gives the floor under the feet.
            crawlNormal = currentUp;
        }
    }
    // Native edge turns can temporarily be unsupported while their wall-state
    // flag stays set. Keep the confirmed vent's frame through that scripted
    // handover, including the first frames before the actor tilts off level.
    if (length(crawlNormal) < .5f)
        crawlNormal = ventCrawlMode.turningSurface(currentUp, motion.moverFlags,
            focused && !owned && (motion.status == 1 || motion.status == 2));
    if (nativeFloor) ventMode = true;
    else if (owned || (supported && currentUp.y > .866f && !(motion.moverFlags & 0x81u)))
        ventMode = false;
    // A vent entered on its wall can be recognised by an opposing surface;
    // ordinary exposed building walls retain all outdoor run transitions.
    const bool nativeCrawl = length(crawlNormal) > .5f;
    bool handCrawl{};
    if constexpr (ventHandCrawl) {
        bool enclosed{};
        if (solver.handClimbEnabled() && !ventCrawlMode.active() && nativeCrawl)
            enclosed = crawlEnclosed(world, crawlPoint, crawlNormal);
        if (!solver.handClimbEnabled() || !focused) ventCrawlMode.reset();
        handCrawl = solver.handClimbEnabled() &&
            ventCrawlMode.update(nativeFloor, nativeCrawl && !nativeFloor, enclosed,
                                supported && !nativeCrawl, sampleSeconds);
    }
    if (handCrawl) ventMode = true;
    const unsigned webHands = (solver.webs()[0].attached ? 1u : 0u) | (solver.webs()[1].attached ? 2u : 0u);
    crawlHands.update(handCrawl, sampleSeconds, in, crawlNormal, crawlPoint + crawlNormal * .4f,
                      world, webHands, crawlPoint);
    if (handCrawl) solver.leaveWall(); // native crawl retains collision and scripted turns
    AcquireSRWLockExclusive(&output);
    if (handCrawl) {
        // Publish even at rest, before the native-physics early returns.
        InterlockedIncrement64(&SpidySwingData.sequence);
        SpidySwingData.status = 1;
        SpidySwingData.qpc = now.QuadPart;
        SpidySwingData.position = at;
        SpidySwingData.velocity = fresh ? motion.achievedVelocity : Vec3{};
        SpidySwingData.wall = static_cast<uint32_t>(nativeFloor ? Surface::floor : Surface::game);
        SpidySwingData.wallNormal = crawlNormal;
        SpidySwingData.wallDistance = std::max(0.f, dot(at - crawlPoint, crawlNormal));
        InterlockedIncrement64(&SpidySwingData.sequence);
    }
    InterlockedIncrement64(&SpidyClimbData.sequence);
    SpidyClimbData.status = 1;
    SpidyClimbData.hands = solver.climbingHands() | crawlHands.hands();
    SpidyClimbData.crawl = handCrawl ? 1u : 0u;
    SpidyClimbData.crawlMove = crawlHands.move();
    SpidyClimbData.sampleTimeNs = c.sampleTimeNs;
    for (unsigned i = 0; i < 2; ++i)
        if (crawlHands.contacts() & (1u << i)) ++SpidyClimbData.contacts[i];
    InterlockedIncrement64(&SpidyClimbData.sequence);
    ReleaseSRWLockExclusive(&output);
    unsigned wallHands{};
    for (unsigned i = 0; i < 2; ++i)
        if (focused && (in.wallRotate ||
                        (handCrawl ? (crawlHands.hands() & (1u << i)) : solver.wallHand(i, in, world)))) {
            wallHands |= 1u << i;
            in.hands[i].grip = in.hands[i].trigger = 0;
        }
    game_warp::observe(steppedView | (focused ? 2u : 0u), false);
    const Body player{at + Vec3{0, 1, 0}, fresh ? motion.achievedVelocity : Vec3{}, motion.grounded != 0};
    // The grab takes every input sample as it comes, so the swing never sees
    // a grip press before the grab has had it, and steps with the game's
    // physics: a perched or standing player's mover does not step, and
    // webbing a thug from a perch must work all the same.
    if (focused) {
        // A press with the hand on the other hand's web takes hold of that
        // web, whatever the hand points at.
        const unsigned onStrand = (solver.onStrand(0) ? 1u : 0u) | (solver.onStrand(1) ? 2u : 0u);
        game_grab::claim(sampleSeconds, in, world, player, onStrand);
        float grabDt{};
        if (game_grab::due(grabDt))
            game_grab::step(grabDt, world, config.record);
        // A web that carries the player - a swing's, a zip's, a pull's, a web
        // strike's flight, the game's zip to a point out of the swing - dodges
        // the blows the game warns of (game_dodge), as the flat game's web
        // moves do. The mover's velocity is in the game's time: in real m/s,
        // as the dodge's speed is.
        {
            bool webbed = strikeFlying.load(std::memory_order_relaxed) ||
                          gameMoveFlying.load(std::memory_order_relaxed);
            for (unsigned i = 0; i < 2; ++i)
                webbed |= solver.webs()[i].attached && (owned || !motion.grounded);
            game_dodge::carry(config.record, webbed, motion.grounded == 0,
                              player.velocity * game_punch::worldScale());
        }
        // Fists take the same samples. A hand whose web holds a target or
        // carries the player does not punch; one whose web only hangs from a
        // wall while the player stands (a clenched fist shoots one) does.
        // Until October 11 any attached web kept its hand from punching:
        // in that day's 14:49 session both hands moved at 4.4 m/s with their
        // webs out and landed nothing.
        if (game_punch::running()) {
            const auto grab = game_grab::data();
            game_punch::Player fighter;
            fighter.record = config.record;
            fighter.feet = at;
            fighter.grounded = motion.grounded != 0;
            // The mover's velocity is in the game's time: in real m/s, as
            // the strikes' speeds are.
            fighter.velocity = player.velocity * game_punch::worldScale();
            fighter.busy = noBlows.load(std::memory_order_relaxed);
            fighter.combatVisuals = combatVisuals.load(std::memory_order_relaxed);
            fighter.halfPunchDamage = halfPunchDamage.load(std::memory_order_relaxed);
            for (unsigned i = 0; i < 2; ++i) {
                const bool carried = solver.webs()[i].attached && (owned || !motion.grounded);
                fighter.swinging |= carried;
                if (grab.hands[i].phase || carried || (wallHands & (1u << i)))
                    fighter.busy |= 1u << i;
                if (grab.hands[i].phase && grab.hands[i].kind == static_cast<uint32_t>(game_targets::Kind::bot))
                    fighter.holds[i] = grab.hands[i].target;
            }
            // A web strike is the player's whole motion: the webs let go, and
            // the web that held the one struck with them.
            const auto opened = game_punch::update(sampleSeconds, in, fighter, world);
            if (opened.strike) {
                solver.releaseAll();
                takeoffTransition.reset();
                mountTakeoff.reset();
                if (opened.held)
                    game_grab::cancel();
            }
        }
        if (world.error()) {
            fault(world.error());
            return;
        }
    }
    // The web shooter and the aim previews come last, however the swing's
    // step below ends, so a ray they make never faults the swing: no one
    // reads the world's error after this visit. A trigger shoots from a hand
    // whose web neither holds a target nor swings the player after this step;
    // on one that does, it reels.
    struct Previews {
        const Command& c;
        const Input& in;
        const Body& player;
        const native_rays::QueryContext& world;
        float seconds;
        Vec3 feet;
        bool focused;
        ~Previews() {
            if (!focused)
                return;
            if (enabled && !world.error() && game_shooter::running()) {
                const auto grab = game_grab::data();
                uint32_t busy{};
                for (unsigned i = 0; i < 2; ++i)
                    if (grab.hands[i].phase || solver.webs()[i].attached ||
                        ((solver.climbingHands() | crawlHands.hands()) & (1u << i)))
                        busy |= 1u << i;
                game_shooter::update(seconds, in, busy, config.record, feet, world);
            }
            if (enabled && !world.error())
                previewAims(c, in, player, world);
        }
    } previews{c, in, player, world, sampleSeconds, at, focused};
    if ((motion.status != 1 && motion.status != 2) || motion.moverFlags & 0x80000000u) {
        cancelSwing();
        worldIdentity = world.identity();
        return;
    }
    // No step for 50 ms: a long frame, or a paused game. The next step goes on
    // from here; letting go dropped the player into the game's fall (InputHold).
    // With live input and no step for longer than a stutter, the game moves
    // the player some other way (a perch), or is paused: the swing lets go. A
    // player on a wall is on it still when the steps come back.
    if (!fresh) {
        if (live && idle * 1000 > controlHoldMs)
            cancelSwing(true);
        return;
    }
    if (motion.steps == lastStep)
        return;
    // Steps that ran between two visits repeated the command before them,
    // within its lease; the prediction goes on from where they left the body.
    lastStep = motion.steps;
    worldIdentity = world.identity();
    // The actor transform is at the feet. Rope constraints/visibility need a
    // harness above the supporting surface, while native motion stays in feet coordinates.
    const Vec3 harness = motion.position + Vec3{0, 1, 0};
    // The next command starts where the step in flight ends.
    const Body body = inFlight.predict(
        {motion.steps, motion.serial, harness, motion.achievedVelocity, motion.dt, motion.status == 2},
        motion.grounded != 0);
    if (!initialized) {
        solver.reset(body);
        initialized = true;
    }
    // The last prediction assumed the step now in flight would last as long
    // as the one before it. An unobserved step in between ran the same command.
    if (predictedDt > 0 && motion.steps > predictedStep)
        solver.settleStep(predictedDt, motion.dt * static_cast<float>(motion.steps - predictedStep));
    predictedDt = motion.dt;
    predictedStep = motion.steps;
    const float inputSeconds = focused ? inputClock.consume(c) : 0.f;
    // The game's wall crawl turns its actor onto the surface and rocks it
    // there (up to 14 degrees, several times a second): the surface's own
    // normal is what a ray down the actor's up finds. In its crawl the game
    // reports the body as standing (October 10 headset report); a wall it
    // stands on so is the swing's as soon as the body is off it.
    Vec3 crawl{};
    // How far the feet are off that surface (zero on the crawl itself).
    float crawlDistance{};
    // Until the crawl below says otherwise, the game holds the body on nothing.
    solver.supportSurface({});
    const Vec3 actorUp = normalized({feet[4], feet[5], feet[6]});
    if (!owned && finite(actorUp) && length(actorUp) > .9995f && actorUp.y < .866f) {
        const auto hit = world.bodyRaycast(at + actorUp, -actorUp, 2.5f);
        if (hit && hit->fixed && finite(hit->normal) && dot(normalized(hit->normal), actorUp) > .7f) {
            crawl = normalized(hit->normal);
            solver.supportSurface(crawl, hit->point);
            crawlWall = {hit->point, crawl, hit->surface, true};
            crawlDistance = std::max(0.f, dot(at - hit->point, crawl));
            if (focused && motion.grounded && !ventMode && !handCrawl)
                solver.offerWall(crawlWall);
        }
    }
    // The game's own wall run, off its crawl (its RT with the stick up the
    // wall) or up a wall from the ground: its wall flag 0x80 in its
    // air (0x80a1, contact 2), the actor upright on the wall, 8 m/s up it and
    // back onto the crawl at its top when let go of (in the game, October
    // 14). The game holds the player there, the swing does not: the crawl's
    // wall, found again beside the body, holds his view and his body on the
    // wall for the run. Only while the player asks for the run - the stick up
    // the wall, which until October 15 was when the headset held the game's
    // RT; it no longer does, so this is a run the game began by itself - and
    // the body keeps to the wall: the same flag shows as the
    // game's jump (RT + A) throws him off the crawl, 0.8 m out in 0.07 s, and
    // that body the swing catches at once, as before (taken for a run, it flew
    // out of reach and fell 43 m, October 14); the run drifted off the wall at
    // 0.3 m/s.
    //
    // Begun, the run is the game's until it lets go of the wall: let go of,
    // it goes back onto its crawl (0x80a6, then 0x8001 standing), and the
    // swing taking the body the moment the stick came back stopped that
    // halfway and dropped him 11 m (October 14).
    constexpr float runReach = .8f, runLeaving = 1.5f;
    const Vec3 runUp = normalized(Vec3{0, 1, 0} - crawlWall.normal * crawlWall.normal.y);
    const bool climbing = focused && finite(in.move) && length(in.move) > .7f && finite(runUp) &&
                          length(runUp) > .5f && dot(in.move, runUp) > .85f * length(in.move);
    if (!owned && !motion.grounded && (motion.moverFlags & 0x80u) && length(crawl) < .5f &&
        length(crawlWall.normal) > .5f && dot(body.velocity, crawlWall.normal) < runLeaving) {
        const Vec3 n = crawlWall.normal;
        const auto hit = world.bodyRaycast(at + Vec3{0, 1, 0}, -n, 2.5f);
        if (hit && hit->fixed && finite(hit->normal) && dot(normalized(hit->normal), n) > .9f &&
            dot(at - hit->point, normalized(hit->normal)) < runReach) {
            crawl = normalized(hit->normal);
            solver.supportSurface(crawl, hit->point);
            crawlWall = {hit->point, crawl, hit->surface, true};
            crawlDistance = std::max(0.f, dot(at - hit->point, crawl));
        }
    }
    // The game's crawl on a floor, its actor upright: the hold it keeps a
    // player in through a vent or a duct, where he cannot stand (in the
    // first mission's vent, October 14: 0x8001, standing, 2.6 m/s). Its own
    // crawl flag without its wall states (a wall run's 0x80a1 carries the
    // same bit). Nothing for the swing to hold: this only tells the view and
    // the body that the player crawls there.
    if (handCrawl && !nativeFloor && length(crawl) < .5f) {
        crawl = crawlNormal;
        crawlDistance = std::max(0.f, dot(at - crawlPoint, crawl));
        solver.supportSurface(crawl, crawlPoint);
    }
    const bool floorCrawl = !owned && length(crawl) < .5f && motion.grounded && (motion.moverFlags & 0x1u) &&
                            !(motion.moverFlags & 0x80u) && finite(actorUp) && actorUp.y > .866f;
    // Crawled along a floor, the player is in a vent or a duct, and the game
    // keeps him through it - its shafts' walls included - until he stands on
    // his feet or the swing has him in the air: no mount there.
    if (floorCrawl)
        ventMode = true;
    else if (owned || (motion.grounded && !(motion.moverFlags & 0x81u)))
        ventMode = false;
    solver.allowMounts(!ventMode && GetTickCount64() >= usePauseUntil);
    // The game's own wall states: the swing takes no wall from inside them.
    solver.gameHoldsWall(!owned && (motion.moverFlags & 0x80u));
    // The game in one of its own wall states with the wall found beside him:
    // its run, or its turn from the run back onto the crawl.
    const bool gameWall = !owned && !motion.grounded && (motion.moverFlags & 0x80u) && length(crawl) > .5f;
    gameRunning = gameWall && (gameRunning || climbing);
    const bool gameRun = gameRunning;
    // A player the game walks (or jumps off the ground for a takeoff) takes
    // his webs along: they are ropes that pay out, and hold him at their new
    // length once he is in the air (Swing::payOut).
    uint32_t paid{};
    if (!owned) {
        const float before[2]{solver.webs()[0].length, solver.webs()[1].length};
        solver.payOut(body.position, groundSlack);
        for (unsigned i = 0; i < 2; ++i)
            paid |= solver.webs()[i].attached && solver.webs()[i].length > before[i] + 1e-3f;
    }
    // A press aimed at something a web can catch belongs to the grab: the
    // swing gets the input without that hand's grip.
    auto swingInput = focused ? game_grab::forSwing(in) : in;
    if (handCrawl) {
        // Do not let the outdoor solver attach a second wall or jump out of
        // the crawl while native input is moving through a duct.
        swingInput.wallRotate = false;
        for (auto& hand : swingInput.hands) hand.wallGrip = 0;
    }
    const auto predicted = solver.predictNativeStep(motion.dt, swingInput, world,
                                                    body, inputSeconds);
    if (world.error()) {
        fault(world.error());
        return;
    }
    if (!predicted.valid) {
        cancel();
        return;
    }
    const bool attached = solver.webs()[0].attached || solver.webs()[1].attached;
    bool pointLaunched{}, newAttachment{}, zipped{}, slingshotLaunched{};
    for (const auto& event : solver.events()) {
        // A charged leap leaves the ground as a point launch does: by the
        // game's own jump, then at the leap's velocity.
        pointLaunched |= event.kind == EventKind::PointLaunch || event.kind == EventKind::Leap;
        slingshotLaunched |= event.kind == EventKind::Slingshot;
        newAttachment |= event.kind == EventKind::Attach;
        zipped |= event.kind == EventKind::Zip;
    }
    pointLaunched |= slingshotLaunched;
    // Keep released flight and the short post-zip landing window. This lets
    // the solver issue a point launch before handing ordinary walking back.
    // Native gravity history is not a reliable midair handoff source.
    const bool collidable = native_movement::collisionEnabled(motion.collisionFlags);
    const Vec3 delta = predicted.target - body.position;
    if (!finite(delta)) {
        fault(3002);
        return;
    }
    // Constraint correction and world-coordinate float rounding can make the
    // requested displacement slightly exceed the solver's velocity limit.
    // Cap the actual native request instead of relinquishing control midair.
    // The movement module moves the body farthestStep a step at most; the
    // command's own step may be an eighth longer than this one before that
    // shortens it.
    const float cap =
        std::min(config.maxSpeed * (1.f - 1e-6f), .875f * native_movement::farthestStep / motion.dt);
    const Vec3 average = delta / motion.dt;
    Vec3 requested = limited(average, cap);
    bool overhead{};
    for (const auto& web : solver.webs())
        overhead |= web.attached && web.anchor.y > body.position.y + .5f;
    // With the slingshot on, a web shot on the ground lifts nothing by
    // itself; the reel still does.
    const bool wantsLift = in.jump || pointLaunched ||
        ((!solver.slingshotEnabled() || solver.reeling()) && requested.y > .25f) ||
        (!solver.slingshotEnabled() && newAttachment && overhead);
    // A web strike that goes up from the ground (at a thug an uppercut
    // lifted, at one on a ledge) leaves it as a web does: by the game's jump.
    const bool strikeLift = body.grounded && game_punch::lifting(body.position - Vec3{0, 1, 0});
    // Takeoff follows the native jump's measured progress.
    const auto transition =
        takeoffTransition.update(GetTickCount64(), attached || strikeLift || slingshotLaunched,
                                 body.grounded, collidable, wantsLift || strikeLift,
                                 zipped || pointLaunched || newAttachment, harness,
                                 motion.achievedVelocity, (zipped || pointLaunched) ? predicted.velocity : Vec3{},
                                 pointLaunched, length(crawl) > .5f ? crawl : Vec3{0, 1, 0});
    if (transition.resumed)
        requested = limited(requested + transition.launchVelocity - body.velocity, cap);
    // A player who walks at a wall, or whom the game has on one in its crawl,
    // comes onto the swing's wall by the game's own jump.
    const auto mount = mountTakeoff.update(GetTickCount64(), solver.mounting(), body.grounded, collidable, true,
                                           false, harness, motion.achievedVelocity, {});
    // Walking/landing and takeoff belong to the native state machine, even if
    // we owned flight on the preceding frame. Preserve held webs across this handoff.
    // A player the game's own jump or fall brought into a wall is the swing's
    // from there: on the wall, then in flight off it.
    const auto wall = solver.wall();
    // A web strike has the player for its flight, and holds him in the air
    // for a moment after its blow: on the ground too, where the swing
    // otherwise leaves him to the game. It flies in real time, and the
    // mover's step is the game's, shorter by its time scale while slow motion
    // or a blow's stop slows the world: the request is that much faster.
    const float timeScale = game_punch::worldScale();
    const float sinceStrike =
        strikeQpc && now.QuadPart > strikeQpc
            ? std::min(static_cast<float>(static_cast<double>(now.QuadPart - strikeQpc) / frequency.QuadPart), .1f)
            : motion.dt / timeScale;
    strikeQpc = now.QuadPart;
    const auto flight = game_punch::fly(sinceStrike, body.position - Vec3{0, 1, 0}, body.grounded,
                                        strikeLift && transition.waiting);
    strikeFlying.store(flight.drive, std::memory_order_relaxed);
    const bool striking = flight.drive && collidable;
    if (striking)
        requested = limited(flight.velocity / timeScale,
                            std::min(motionSpeedLimit * (1.f - 1e-6f),
                                     .875f * native_movement::farthestStep / motion.dt));
    // The game's own wall state - its grasp of a body that meets a wall moving
    // into it - shows as mover flag 0x80 (0x80a0 grasped, 0x80a6 turning onto
    // the wall; the swing's air is 0x10008020, the crawl's idle 0x8001). While
    // the swing presses its wall it lets go the moment the game grasps: its
    // own commands went on pushing the body into the wall's collision and the
    // game let go again after 0.48 s (October 14, the first mission's vent),
    // where it needs about a second before it turns the hero onto the wall in
    // its crawl. In that state the game's air handler does not run ("air
    // events stop", October 9), so there is no fall of its own to snap to.
    //
    // Latched, once it grasps: between its own wall states the flag can clear
    // for a step, and the swing taking the body back there broke the game's
    // turn onto the wall - every grasp ended after 0.5 s with the swing
    // holding him again (October 14). It stays the game's for graspSeconds,
    // until its crawl has him, or until the body drops graspDrop below where
    // it was grasped - the game letting go into its own fall, which the swing
    // catches at once.
    constexpr uint64_t graspMs = 2500;
    constexpr float graspDrop = .5f;
    const uint64_t tick = GetTickCount64();
    if (!solver.pressingWalls())
        graspedAt = 0;
    else if (!graspedAt && (motion.moverFlags & 0x80u)) {
        graspedAt = tick;
        graspHeight = body.position.y;
    }
    if (graspedAt && (tick - graspedAt > graspMs || body.position.y < graspHeight - graspDrop))
        graspedAt = 0;
    const bool grasped = graspedAt != 0;
    // The game's own crawl under the player where he asked for one of the
    // game's interactions on the swing's wall (game_offers: pointing at it,
    // and the interact): the game runs it from its own crawl, the one hold
    // it registers him in. It goes onto the wall its own way (game_warp), at
    // the floor his view stands on, and the swing holds the body until the
    // game has begun (its wall flag 0x80, or standing): a body let go of
    // before that falls at the speed of the whole flight behind it. Then
    // hands off, until the crawl has him; if the game has not begun within
    // crawlBeginMs, has not finished within crawlHoldMs, or the body drops
    // crawlDrop, the swing has him again. Never at rest any more (October 15,
    // the player: "use our locomotion system only").
    constexpr float restSpeed = .3f, restStick = .2f, crawlDrop = .5f;
    constexpr uint64_t crawlBeginMs = 400, crawlHoldMs = 1500;
    // Once the crawl has him, the headset's own press of the game's interact
    // follows within a second (offer_pointing::Use): no mount meanwhile.
    constexpr uint64_t usePauseMs = 1500;
    const float stick = finite(in.move) ? length(in.move) : 0.f;
    const bool resting = wall.on && owned && !attached && stick < restStick && length(body.velocity) < restSpeed;
    restSeconds = resting ? restSeconds + motion.dt : 0.f;
    if (Vec3 point; wall.on && owned && !crawlAskedAt && game_offers::takeUse(point)) {
        if (game_warp::crawl(point, wall.normal)) {
            crawlAskedAt = tick;
            crawlBegun = false;
            // A spot of the offer's below him is no drop.
            crawlHeight = std::min(body.position.y, point.y + .5f);
            usePauseUntil = tick + crawlHoldMs + usePauseMs;
        } else {
            game_offers::usedOutcome(false);
        }
    }
    if (crawlAskedAt) {
        crawlBegun |= (motion.moverFlags & 0x80u) != 0 || body.grounded;
        if (body.grounded && length(crawl) > .5f) {
            crawlAskedAt = 0; // the game's crawl has him: a standing body is the game's anyway
            game_warp::outcome(true);
            game_offers::usedOutcome(true);
            usePauseUntil = tick + usePauseMs;
        } else if ((!crawlBegun && tick - crawlAskedAt > crawlBeginMs) || tick - crawlAskedAt > crawlHoldMs ||
                   body.position.y < crawlHeight - crawlDrop) {
            crawlAskedAt = 0;
            crawlDeclined = true;
            game_warp::outcome(false);
            game_offers::usedOutcome(false);
            usePauseUntil = 0;
        }
        if (!crawlAskedAt)
            crawlBegun = false;
    }
    const bool handing = crawlAskedAt && crawlBegun;
    const bool crawlHeld = !owned && body.grounded && length(crawl) > .5f;
    steppedView = (crawlHeld ? 1u : 0u) | (body.grounded ? 4u : 0u) | (length(crawl) > .5f ? 8u : 0u) |
                  (owned ? 16u : 0u) | (gameRun ? 32u : 0u) | (resting ? 64u : 0u) | (handing ? 128u : 0u);
    game_warp::observe(steppedView | (focused ? 2u : 0u), true);
    // The game's own move the player asked for (its zip to a point, its web
    // strike, a prompt's) once the game has begun it out of the swing's
    // flight (AirYield): hands off, its webs and its wall let go - the move
    // has its own - until it is done.
    const bool yielding = airYield.update(tick, live && (c.asks & 1u), owned, !body.grounded,
                                          (motion.moverFlags & 0x80u) != 0, motion.steps, motion.airEvents);
    gameMoveFlying.store(yielding && !body.grounded, std::memory_order_relaxed);
    if (airYield.began()) {
        solver.releaseAll();
        solver.leaveWall();
        game_punch::cancelStrike();
        takeoffTransition.reset();
        mountTakeoff.reset();
    }
    // Nor does the swing take the body the game runs up its wall (gameRun).
    const bool drive = !handCrawl && !grasped && !handing && !gameRun && !yielding &&
                       (striking || (collidable && !body.grounded && !transition.waiting &&
                                     (attached || owned || transition.resumed || wall.on)));
    if (drive) {
        native_movement::Command request;
        request.enabled = 1;
        request.serial = ++motionSerial;
        // The command is applied by the next native step, one frame from now,
        // and repeated if an observation is missed. With a 50 ms lease, a frame
        // longer than that ran the step without it: the game then moved the
        // body at its own fall speed for the time spent airborne (28-42 m/s
        // down in the October 5 reports), a drop of a metre followed by a
        // snap back at the speed limit. The step itself may be any length:
        // the module moves the body for all of it (native_movement::stepDelta).
        request.leaseMs = motionLeaseMs;
        request.velocity = requested;
        if (const auto error = submitMotion(&request)) {
            fault(error);
            return;
        }
        // The solver's velocity at the end of this command, with the same
        // speed cap and takeoff launch applied to it as to the request; a
        // strike's own in real time, at rest once it has arrived.
        inFlight.issued(request.serial, requested,
                        striking ? limited(flight.velocity, config.maxSpeed)
                                 : limited(predicted.velocity + requested - average, config.maxSpeed));
        owned = true;
    } else if (owned) {
        relinquishMotion();
    }
    AcquireSRWLockExclusive(&output);
    {
        auto& r = SpidyRopeData;
        InterlockedIncrement64(&r.sequence);
        r.physical = solver.config().physical;
        InterlockedIncrement64(&SpidyClimbData.sequence);
        SpidyClimbData.status = 1;
        SpidyClimbData.hands = solver.climbingHands() | crawlHands.hands();
        SpidyClimbData.crawl = handCrawl ? 1u : 0u;
        SpidyClimbData.crawlMove = crawlHands.move();
        SpidyClimbData.sampleTimeNs = c.sampleTimeNs;
        for (const auto& event : solver.events())
            if (event.kind == EventKind::WallGrip && event.hand >= 0 && event.hand < 2)
                ++SpidyClimbData.contacts[event.hand];
        InterlockedIncrement64(&SpidyClimbData.sequence);
        r.paidOut += paid;
        for (auto event : solver.events()) {
            r.wraps += event.kind == EventKind::Wrap;
            r.unwraps += event.kind == EventKind::Unwrap;
            r.cut += event.kind == EventKind::Obstructed;
            r.joins += event.kind == EventKind::Join;
            r.passed += event.kind == EventKind::Pass;
        }
        r.shared = solver.shared();
        for (unsigned i = 0; i < 2; ++i) {
            const auto& w = solver.webs()[i];
            r.bends[i] = w.attached ? static_cast<uint32_t>(w.bent) : 0u;
            r.free[i] = w.attached ? w.length : 0.f;
            r.whole[i] = r.free[i];
            for (int b = 0; w.attached && b < w.bent; ++b)
                r.whole[i] += w.bends[static_cast<size_t>(b)].span;
        }
        InterlockedIncrement64(&r.sequence);
        InterlockedIncrement64(&bends.sequence);
        for (unsigned i = 0; i < 2; ++i)
            bends.hands[i] = edgesOf(solver.webs()[i]);
        bends.status = 2;
        InterlockedIncrement64(&bends.sequence);
    }
    auto& d = SpidySwingData;
    InterlockedIncrement64(&d.sequence);
    d.status = drive ? 2 : 1;
    d.qpc = now.QuadPart;
    ++d.steps;
    d.controlled += drive;
    d.serial = c.serial;
    d.world = world.identity();
    d.sourceStep = motion.steps;
    d.position = motion.position;
    d.velocity = body.velocity;
    d.requested = requested;
    d.dt = motion.dt;
    d.owned = owned;
    d.grounded = motion.grounded;
    d.collisionFlags = motion.collisionFlags;
    const bool lifting = takeoffTransition.phase() != SwingTakeoff::Idle;
    d.takeoff = transition.jump || mount.jump;
    d.takeoffPhase = lifting ? takeoffTransition.phase() : mountTakeoff.phase();
    d.takeoffAttempts = lifting ? takeoffTransition.attempts() : mountTakeoff.attempts();
    d.takeoffTimeouts += transition.timedOut + mount.timedOut;
    // After a jump the game did not make, the stick is the game's again.
    const bool mounts =
        mountTakeoff.phase() != SwingTakeoff::TimedOut && (mount.waiting || solver.mountBegun());
    d.mount = !mounts ? 0u : length(crawl) > .5f ? 2u : 1u;
    d.nativeContact = motion.contact;
    d.wall = static_cast<uint32_t>(wall.on && drive       ? Surface::wall
                                   : length(crawl) > .5f ? Surface::game
                                   : floorCrawl          ? Surface::floor
                                                         : Surface::none);
    d.wallNormal = wall.on && drive ? wall.normal : floorCrawl ? actorUp : crawl;
    // The game offers its interactions there as on its own crawl, the hero
    // crawling where the view stands him: on the wall's face under his feet.
    game_offers::hold(config.record, d.wall == static_cast<uint32_t>(Surface::wall),
                      body.position - Vec3{0, 1, 0} - wall.normal * (wall.distance - wall.normal.y), wall.normal);
    d.wallDistance = wall.on && drive ? wall.distance : crawlDistance;
    for (auto event : solver.events()) {
        d.walls += event.kind == EventKind::WallOn;
        d.wallJumps += event.kind == EventKind::WallJump;
        d.attaches += event.kind == EventKind::Attach;
        d.releases += event.kind == EventKind::Release || event.kind == EventKind::TrackingLost ||
                      event.kind == EventKind::Obstructed;
        d.zips += event.kind == EventKind::Zip;
        d.misses += event.kind == EventKind::Miss;
        d.obstructed += event.kind == EventKind::Obstructed;
        d.trackingLost += event.kind == EventKind::TrackingLost;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const auto& w = solver.webs()[i];
        d.webs[i] = {!w.attached ? 0u : 1u | (solver.shared() ? 2u : 0u) | (w.taken ? 4u : 0u),
                     static_cast<uint32_t>(w.surface), w.anchor, w.length, w.tension};
    }
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&output);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        // Running already: a second start is refused. Stopped, a start goes
        // again from the beginning (the movement module takes its hooks up
        // again).
        if (started && enabled) {
            result = 1000;
            break;
        }
        if (!copy(&config, input, sizeof(config)) || config.magic != 0x53574346 || config.version != 1 ||
            config.bytes != sizeof(config) || config.pid != GetCurrentProcessId() ||
            config.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) || config.grabKinds > 0xe ||
            (config.durationMs && config.durationMs < 1000) || config.durationMs > 30000 ||
            !std::isfinite(config.maxSpeed) || config.maxSpeed <= 0 || config.maxSpeed > 65 ||
            !std::isfinite(config.gravity) || config.gravity < 0 || config.gravity > maxGravity) {
            result = 1001;
            break;
        }
        const auto module = reinterpret_cast<HMODULE>(config.motionModule);
        startMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionStart"));
        submitMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionSubmit"));
        sampleMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionSample"));
        stopMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionStop"));
        retargetMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionRetarget"));
        if (!startMotion || !submitMotion || !sampleMotion || !stopMotion || !retargetMotion) {
            result = 1002;
            break;
        }
        // Optional: a movement module without them cannot move bots on a web.
        driveMotion = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionDrive"));
        sampleDriven = reinterpret_cast<Call>(GetProcAddress(module, "SpidyMotionDrivenSample"));
        native_movement::Config motion;
        motion.pid = config.pid;
        motion.base = config.base;
        motion.record = config.record;
        motion.mover = config.mover;
        motion.durationMs = config.durationMs;
        // The swing caps its own requests at config.maxSpeed, which can rise
        // during play (SpidySwingSettings); the module only rejects faster ones.
        motion.maxSpeed = motionSpeedLimit;
        motion.syncAirVelocity = 1;
        if ((result = startMotion(&motion)))
            break;
        combatVisuals = true;
        halfPunchDamage = false;
        started = true;
        solver = Swing(physicsConfig(config));
        QueryPerformanceFrequency(&frequency);
        deadline = config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
        game_grab::start(config.base, reinterpret_cast<game_grab::Call>(driveMotion),
                         reinterpret_cast<game_grab::Call>(sampleDriven),
                         driveMotion && sampleDriven ? config.grabKinds : 0);
        // The game's own crawl at rest on a wall (game_warp): its functions
        // checked, and the player's transition manager found here, off the
        // game's thread. Without them the swing keeps the wall itself.
        if (!game_warp::start(config.base))
            game_warp::target(config.record);
        // The game's Focus for the punches' and the web's blows (game_focus):
        // without it they earn none, as before. And the game's own impact
        // sounds for the punches' blows (game_sound): without them the
        // headset plays its own.
        game_focus::start(config.base);
        game_sound::start(config.base);
        // The game's own interactions where the swing's wall holds the
        // player (game_offers): without them that wall offers nothing.
        game_offers::start(config.base);
        // The game's throw of a prop aimed where the free hand points
        // (game_throw): without it the throw goes the camera's way.
        game_throw::start(config.base);
        // The game's own dodge for a player a web carries (game_dodge):
        // without it a warned blow finds him on the swing undodged.
        strikeFlying = gameMoveFlying = false;
        game_dodge::start(config.base);
        // A start after a stop takes input from the first sample on: the
        // last one's serial would have every new sample refused as old.
        AcquireSRWLockExclusive(&control);
        command = {};
        inputDeadline = 0;
        ReleaseSRWLockExclusive(&control);
        inputHold.reset();
        restSeconds = 0;
        crawlAskedAt = 0;
        crawlBegun = crawlDeclined = gameRunning = false;
        crawlWall = {};
        ventMode = false;
        usePauseUntil = 0;
        enabled = true;
        native_rays::setVisitor(visit);
    } while (false);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
// The local player became another actor (a loaded save, a respawn, a
// character switch). Webs and flight belonged to the previous body; the
// solver starts again from the new one. Only record and mover are used.
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingRetarget(void* input) {
    Config next;
    if (!copy(&next, input, sizeof(next)) || next.magic != 0x53574346 || next.version != 1 ||
        next.bytes != sizeof(next) || next.pid != GetCurrentProcessId() ||
        next.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)))
        return 1001;
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    if (!started) {
        result = 1004;
    } else {
        AcquireSRWLockExclusive(&simulation);
        cancel();
        initialized = false;
        lastStep = worldIdentity = predictedStep = 0;
        native_movement::Config motion;
        motion.pid = config.pid;
        motion.base = config.base;
        motion.record = next.record;
        motion.mover = next.mover;
        motion.durationMs = config.durationMs;
        motion.maxSpeed = motionSpeedLimit;
        motion.syncAirVelocity = 1;
        if (!(result = retargetMotion(&motion))) {
            config.record = next.record;
            config.mover = next.mover;
            game_warp::target(next.record);
            crawlAskedAt = 0;
            crawlBegun = crawlDeclined = gameRunning = false;
            crawlWall = {};
            ventMode = false;
            usePauseUntil = 0;
        }
        ReleaseSRWLockExclusive(&simulation);
    }
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
// The VR settings during play: the speed limit, whether webs catch props and
// thugs, whether they hold in open air, the gravity (weight), whether walls
// are the swing's own, and the webs' style. All apply from the next step; the
// visit holds `simulation`.
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingSettings(void* input) {
    // Version 3 to 6 settings (the probes') are shorter: read as many bytes as they say.
    Settings s;
    uint32_t header[3]{};
    if (!copy(header, input, sizeof(header)) ||
        (header[2] != settingsBytesV3 && header[2] != settingsBytesV4 &&
         header[2] != settingsBytesV5 && header[2] != settingsBytesV6 &&
         header[2] != settingsBytesV7 && header[2] != settingsBytesV8 && header[2] != settingsBytesV9 && header[2] != settingsBytesV10 && header[2] != settingsBytesV11 &&
         header[2] != sizeof(s)) ||
        !copy(&s, input, header[2]) || !validSettings(s))
        return 2001;
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    if (!started) {
        result = 1004;
    } else {
        AcquireSRWLockExclusive(&simulation);
        config.maxSpeed = s.maxSpeed;
        config.gravity = s.gravity;
        solver.limitSpeed(s.maxSpeed);
        solver.setGravity(s.gravity);
        solver.allowAirAnchors(s.airWebs != 0);
        if (s.version >= 4) {
            // 0 off (the wall it is on goes with it), 1 on, 2 keep this one and
            // take no other, 3 on and held against the wall (hugWalls), 4 on and
            // pressed into it (pressWalls). The swing holds the body in 3 and 4
            // as in 1. 2 and 3 are for probes (tools/probe_vent_hug.py).
            //
            // A 4 that yielded the body to the game's own steps against its wall
            // was tried and removed the same day: the game's first step of its
            // own fell at 42 m/s - it counts its fall from the time airborne,
            // right through the swing's flight. Nothing hands a player over in
            // midair; pressing keeps hold of him.
            solver.allowWalls(s.walls != 0);
            solver.takeNewWalls(s.walls != 2);
            solver.hugWalls(s.walls == 3);
            solver.pressWalls(s.walls == 4);
        }
        if (s.version >= 5)
            solver.allowPhysical(s.physical != 0);
        noBlows = s.version >= 6 ? s.noBlows : 0u;
        combatVisuals = s.version < 11 || s.combatVisuals != 0;
        halfPunchDamage = s.version >= 12 && s.halfPunchDamage != 0;
        if (s.version >= 7)
            solver.allowArcade(s.arcade != 0);
        if (s.version >= 8)
            solver.allowHandClimb(s.handClimb != 0);
        if (s.version >= 9) {
            if (solver.slingshotEnabled() != (s.webSlingshot != 0))
                takeoffTransition.reset();
            solver.allowSlingshot(s.webSlingshot != 0);
        }
        if (s.version >= 10)
            solver.allowBetterClimbing(s.betterClimbing != 0);
        // A swing started without the grab (-NoWebGrab) offers it from now on.
        if (s.grab && !game_grab::offering() && driveMotion && sampleDriven) {
            config.grabKinds = game_grab::movableKinds;
            result = game_grab::start(config.base, reinterpret_cast<game_grab::Call>(driveMotion),
                                      reinterpret_cast<game_grab::Call>(sampleDriven), config.grabKinds);
        }
        game_grab::allow(s.grab != 0);
        ReleaseSRWLockExclusive(&simulation);
    }
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySlingshotSample(void* destination) {
    SlingshotData sample;
    AcquireSRWLockShared(&simulation);
    sample.charge = solver.slingshotCharge();
    ReleaseSRWLockShared(&simulation);
    return copy(destination, &sample, sizeof(sample)) ? 0 : 2001;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingSubmit(void* input) {
    // A version 2 or 3 command (the probes') is shorter: read as many bytes as it says.
    Command c;
    uint32_t header[3]{};
    if (!copy(header, input, sizeof(header)) ||
        (header[2] != commandBytesV2 && header[2] != commandBytesV3 && header[2] != commandBytesV4 &&
         header[2] != commandBytesV5 && header[2] != sizeof(c)) ||
        !copy(&c, input, header[2]) || !valid(c))
        return 2001;
    AcquireSRWLockExclusive(&control);
    DWORD result{};
    if (!enabled || GetTickCount64() >= deadline)
        result = 2002;
    else if (c.serial <= command.serial)
        result = 2003;
    else {
        command = c;
        inputDeadline = GetTickCount64() + c.leaseMs;
    }
    ReleaseSRWLockExclusive(&control);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingSample(void* out) {
    AcquireSRWLockShared(&control);
    AcquireSRWLockShared(&output);
    auto sample = SpidySwingData;
    ReleaseSRWLockShared(&output);
    if (!enabled || GetTickCount64() >= deadline || GetTickCount64() >= inputDeadline || !command.focused) {
        sample.status = 0;
        sample.owned = 0;
        sample.takeoff = 0;
        sample.takeoffPhase = sample.takeoffAttempts = 0;
        for (auto& web : sample.webs)
            web = {};
    }
    ReleaseSRWLockShared(&control);
    return copy(out, &sample, sizeof(sample)) ? 0 : 2201;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyClimbSample(void* out) {
    AcquireSRWLockShared(&control);
    AcquireSRWLockShared(&output);
    auto sample = SpidyClimbData;
    ReleaseSRWLockShared(&output);
    if (!enabled || GetTickCount64() >= deadline || GetTickCount64() >= inputDeadline || !command.focused) {
        sample.status = 0;
        sample.hands = 0;
        sample.crawl = 0;
        sample.crawlMove = {};
    }
    ReleaseSRWLockShared(&control);
    return copy(out, &sample, sizeof(sample)) ? 0 : 2201;
}
// The edges each hand's web bends round (Bends), for drawing it round them.
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingBends(void* out) {
    AcquireSRWLockShared(&control);
    AcquireSRWLockShared(&output);
    auto sample = bends;
    ReleaseSRWLockShared(&output);
    if (!enabled || GetTickCount64() >= deadline || GetTickCount64() >= inputDeadline || !command.focused) {
        sample.status = 0;
        for (auto& hand : sample.hands)
            hand = {};
    }
    ReleaseSRWLockShared(&control);
    return copy(out, &sample, sizeof(sample)) ? 0 : 2201;
}
// What each hand's grip press would do now (AimData), for the aim markers.
// Sampling keeps the previews coming for aimLeaseMs; none older than 150 ms.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyAimSample(void* out) {
    aimDeadline = GetTickCount64() + aimLeaseMs;
    AcquireSRWLockShared(&control);
    AcquireSRWLockShared(&output);
    auto sample = SpidyAimData;
    const auto made = aimMadeMs;
    ReleaseSRWLockShared(&output);
    const auto now = GetTickCount64();
    if (!enabled || now >= deadline || now >= inputDeadline || !command.focused || now - made > 150) {
        sample.status = 0;
        for (auto& hand : sample.hands)
            hand = {};
        sample.gaze = {};
    }
    ReleaseSRWLockShared(&control);
    return copy(out, &sample, sizeof(sample)) ? 0 : 2201;
}
// The web grab's telemetry (game_grab::Data): what each hand's web holds.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyGrabSample(void* out) {
    auto sample = game_grab::data();
    AcquireSRWLockShared(&control);
    if (!enabled || GetTickCount64() >= deadline || GetTickCount64() >= inputDeadline || !command.focused)
        for (auto& hand : sample.hands)
            hand = {};
    ReleaseSRWLockShared(&control);
    return copy(out, &sample, sizeof(sample)) ? 0 : 2201;
}
// Headless checks of what the web does to a bot, at any distance: fling it
// (BotStateFlung at a velocity, or a new velocity for the flight it is on).
// Output: whether its state machine is in BotStateFlung now.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyGrabTest(void* input) {
    struct Test {
        uint32_t magic, version, bytes, flung;
        uint64_t machine, record;
        Vec3 velocity;
        uint32_t reserved;
    } t{};
    if (!copy(&t, input, sizeof(t)) || t.magic != 0x53475454 || t.version != 1 || t.bytes != sizeof(t) ||
        !finite(t.velocity) || length(t.velocity) > 45)
        return 2001;
    if (!enabled)
        return 2002;
    const bool queued = !t.machine || native_bodies::fling(t.machine, t.record, t.velocity);
    t.flung = native_bodies::flung(t.machine);
    copy(input, &t, sizeof(t));
    return queued ? 0 : 2004;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    native_rays::setVisitor(nullptr);
    AcquireSRWLockExclusive(&simulation);
    enabled = false;
    cancel();
    game_grab::stop();
    // Punches started the main-thread damage hook when the grab did not.
    game_punch::stop();
    game_shooter::stop();
    game_focus::stop();
    game_sound::stop();
    native_bodies::stop();
    game_offers::stop();
    game_throw::stop();
    game_dodge::stop();
    ReleaseSRWLockExclusive(&simulation);
    const DWORD result = started ? stopMotion(nullptr) : 0;
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidySwingData.sequence);
    if (result)
        SpidySwingData.error = result;
    SpidySwingData.status = SpidySwingData.error ? 4 : 3;
    SpidySwingData.owned = 0;
    InterlockedIncrement64(&SpidySwingData.sequence);
    ReleaseSRWLockExclusive(&output);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
