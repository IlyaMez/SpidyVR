// Swing prediction runs inside a native world-query lease. MoverStandard remains
// responsible for body collision and committing the resulting position.
#include "spidy/game_swing.hpp"
#include "spidy/game_grab.hpp"
#include "spidy/game_punch.hpp"
#include "spidy/game_shooter.hpp"
#include "spidy/native_bodies.hpp"
#include "spidy/native_movement.hpp"
#include "spidy/native_query_context.hpp"
#include <atomic>
#include <cstring>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_swing;
extern "C" {
__declspec(dllexport) Data SpidySwingData;
// The aim markers' previews (SpidyAimSample); odd `sequence` while it changes.
__declspec(dllexport) AimData SpidyAimData;
}
namespace {
using Call = DWORD(WINAPI*)(void*);
// A movement command must outlast the slowest step the solver accepts (50 ms)
// twice over, because visit() tolerates one missed observation, plus the 16 ms
// grain of the tick count its lease is measured with.
constexpr uint32_t motionLeaseMs = 150;
Call startMotion{}, submitMotion{}, sampleMotion{}, stopMotion{}, retargetMotion{}, driveMotion{},
    sampleDriven{};
Config config;
Command command;
Swing solver;
InputSampleClock inputClock, grabInputClock;
InputHold inputHold;
InFlightStep inFlight;
SRWLOCK lifecycle = SRWLOCK_INIT, control = SRWLOCK_INIT, output = SRWLOCK_INIT, simulation = SRWLOCK_INIT;
std::atomic<bool> enabled{};
// coasting: owned flight going on without input (coast()).
bool started{}, owned{}, initialized{}, coasting{};
uint64_t deadline{}, inputDeadline{}, motionSerial{}, lastStep{}, worldIdentity{};
// Step length the last prediction assumed, and the native step it was made in.
float predictedDt{};
uint64_t predictedStep{};
SwingTakeoff takeoffTransition;
// The same jump for a player who goes up onto a wall from the ground or out
// of the game's crawl (Swing::mounting): the wall takes the body as it leaves.
SwingTakeoff mountTakeoff;
LARGE_INTEGER frequency{};
// Aim previews are made while sampled before this tick, once per input
// command; the latest was made at aimMadeMs.
std::atomic<uint64_t> aimDeadline{};
uint64_t aimSerial{}, aimMadeMs{};
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
    relinquishMotion();
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
    takeoffTransition.reset();
    mountTakeoff.reset();
    solver.releaseAll();
    inputClock.reset();
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
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidyAimData.sequence);
    SpidyAimData.status = 2;
    SpidyAimData.serial = c.serial;
    for (unsigned i = 0; i < 2; ++i)
        SpidyAimData.hands[i] = hands[i];
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
    const auto in = focused ? input(c) : Input{};
    // The player for picks, throws and aim previews: where it stands, and its
    // speed when moving.
    uintptr_t transform{};
    float feet[16]{};
    Vec3 at = motion.position;
    if (copy(&transform, reinterpret_cast<const void*>(config.record), sizeof(transform)) &&
        copy(feet, reinterpret_cast<const void*>(transform), sizeof(feet)) && finite({feet[12], feet[13], feet[14]}))
        at = {feet[12], feet[13], feet[14]};
    const Body player{at + Vec3{0, 1, 0}, fresh ? motion.achievedVelocity : Vec3{}, motion.grounded != 0};
    // The grab takes every input sample as it comes, so the swing never sees
    // a grip press before the grab has had it, and steps with the game's
    // physics: a perched or standing player's mover does not step, and
    // webbing a thug from a perch must work all the same.
    const float sampleSeconds = focused ? grabInputClock.consume(c) : 0.f;
    if (focused) {
        game_grab::claim(sampleSeconds, in, world, player);
        float grabDt{};
        if (game_grab::due(grabDt))
            game_grab::step(grabDt, world, config.record);
        // Fists take the same samples. A hand whose web holds a target or
        // swings the player does not punch.
        if (game_punch::running()) {
            const auto grab = game_grab::data();
            uint32_t busy{};
            for (unsigned i = 0; i < 2; ++i)
                if (grab.hands[i].phase || solver.webs()[i].attached)
                    busy |= 1u << i;
            game_punch::update(sampleSeconds, in, config.record, busy, world);
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
                    if (grab.hands[i].phase || solver.webs()[i].attached)
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
    const Vec3 actorUp = normalized({feet[4], feet[5], feet[6]});
    if (!owned && finite(actorUp) && length(actorUp) > .9995f && actorUp.y < .866f) {
        const auto hit = world.raycast(at + actorUp, -actorUp, 2.5f);
        if (hit && hit->fixed && finite(hit->normal) && dot(normalized(hit->normal), actorUp) > .7f) {
            crawl = normalized(hit->normal);
            if (focused && motion.grounded)
                solver.offerWall({hit->point, crawl, hit->surface, true});
        }
    }
    // A press aimed at something a web can catch belongs to the grab: the
    // swing gets the input without that hand's grip.
    const auto predicted = solver.predictNativeStep(motion.dt, focused ? game_grab::forSwing(in) : in, world,
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
    bool pointLaunched{}, newAttachment{}, zipped{};
    for (const auto& event : solver.events()) {
        pointLaunched |= event.kind == EventKind::PointLaunch;
        newAttachment |= event.kind == EventKind::Attach;
        zipped |= event.kind == EventKind::Zip;
    }
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
    const float cap = std::min(config.maxSpeed * (1.f - 1e-6f), 1.75f / motion.dt);
    const Vec3 average = delta / motion.dt;
    Vec3 requested = limited(average, cap);
    bool overhead{};
    for (const auto& web : solver.webs())
        overhead |= web.attached && web.anchor.y > body.position.y + .5f;
    const bool wantsLift = requested.y > .25f || (newAttachment && overhead) || in.jump || pointLaunched;
    // Takeoff follows the native jump's measured progress.
    const auto transition =
        takeoffTransition.update(GetTickCount64(), attached, body.grounded, collidable, wantsLift,
                                 zipped || pointLaunched || newAttachment, harness, motion.achievedVelocity,
                                 (zipped || pointLaunched) ? predicted.velocity : Vec3{}, pointLaunched);
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
    const bool drive = collidable && !body.grounded && !transition.waiting &&
                       (attached || owned || transition.resumed || wall.on);
    if (drive) {
        native_movement::Command request;
        request.enabled = 1;
        request.serial = ++motionSerial;
        // The command is applied by the next native step, one frame from now,
        // and repeated if an observation is missed. With a 50 ms lease, a frame
        // longer than that ran the step without it: the game then moved the
        // body at its own fall speed for the time spent airborne (28-42 m/s
        // down in the October 5 reports), a drop of a metre followed by a
        // snap back at the speed limit.
        request.leaseMs = motionLeaseMs;
        request.velocity = requested;
        if (const auto error = submitMotion(&request)) {
            fault(error);
            return;
        }
        // The solver's velocity at the end of this command, with the same
        // speed cap and takeoff launch applied to it as to the request.
        inFlight.issued(request.serial, requested,
                        limited(predicted.velocity + requested - average, config.maxSpeed));
        owned = true;
    } else if (owned) {
        relinquishMotion();
    }
    AcquireSRWLockExclusive(&output);
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
    d.wall = static_cast<uint32_t>(wall.on && drive ? Surface::wall : length(crawl) > .5f ? Surface::game : Surface::none);
    d.wallNormal = wall.on && drive ? wall.normal : crawl;
    d.wallDistance = wall.on && drive ? wall.distance : 0.f;
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
        d.webs[i] = {w.attached, static_cast<uint32_t>(w.surface), w.anchor, w.length, w.tension};
    }
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&output);
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        if (started) {
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
        started = true;
        solver = Swing(physicsConfig(config));
        QueryPerformanceFrequency(&frequency);
        deadline = config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
        game_grab::start(config.base, reinterpret_cast<game_grab::Call>(driveMotion),
                         reinterpret_cast<game_grab::Call>(sampleDriven),
                         driveMotion && sampleDriven ? config.grabKinds : 0);
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
        }
        ReleaseSRWLockExclusive(&simulation);
    }
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
// The VR settings during play: the speed limit, whether webs catch props and
// thugs, whether they hold in open air, the gravity (weight), and whether
// walls are the swing's own. All apply from the next step; the visit holds
// `simulation`.
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingSettings(void* input) {
    // Version 3 settings (the probes') are shorter: read as many bytes as they say.
    Settings s;
    uint32_t header[3]{};
    if (!copy(header, input, sizeof(header)) || (header[2] != settingsBytesV3 && header[2] != sizeof(s)) ||
        !copy(&s, input, header[2]) || s.magic != 0x53575354 ||
        !((s.version == 3 && s.bytes == settingsBytesV3) || (s.version == 4 && s.bytes == sizeof(s))) ||
        s.grab > 1 || !std::isfinite(s.maxSpeed) || s.maxSpeed <= 0 || s.maxSpeed > motionSpeedLimit ||
        s.airWebs > 1 || !std::isfinite(s.gravity) || s.gravity < 0 || s.gravity > maxGravity || s.walls > 1)
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
        if (s.version >= 4)
            solver.allowWalls(s.walls != 0);
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
extern "C" __declspec(dllexport) DWORD WINAPI SpidySwingSubmit(void* input) {
    // A version 2 command (the probes') is shorter: read as many bytes as it says.
    Command c;
    uint32_t header[3]{};
    if (!copy(header, input, sizeof(header)) || (header[2] != commandBytesV2 && header[2] != sizeof(c)) ||
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
    native_bodies::stop();
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
