#pragma once
#include "swing.hpp"
#include <vector>

namespace spidy {
// Webs that catch things instead of the world: props and characters a hand's
// web can grab, yank, carry, swing and throw. Engine independent. An adapter
// reports targets and simulates them under the webs' commands: the lab moves
// its own props, the game its physics bodies.
enum class TargetKind : std::uint8_t { Object = 1, Character = 2 };
struct GrabTarget {
    std::uint64_t id{};
    TargetKind kind = TargetKind::Object;
    Vec3 position{}, velocity{}; // centre, world metres and metres per second
    float mass = 20, radius = .4f;
};
class TargetQueries {
  public:
    virtual ~TargetQueries() = default;
    // The target a web shot along the ray would take: of those whose sphere
    // lies within `cone` radians of the ray and starts nearer than `distance`,
    // the one nearest the ray.
    virtual std::optional<GrabTarget> pick(Vec3 origin, Vec3 direction, float distance, float cone) const = 0;
    // A target as it is now; nothing once it is gone.
    virtual std::optional<GrabTarget> find(std::uint64_t id) const = 0;
    // Characters a throw may be aimed at.
    virtual void characters(std::vector<GrabTarget>&) const {}
    // The target a surface of the world belongs to, if any. A prop's own
    // bodies are where a web strikes it, never a wall in front of it.
    virtual std::optional<std::uint64_t> owner(std::uint64_t /*surface*/) const {
        return {};
    }
};
struct GrabConfig {
    float maxRange = 60, aimCone = .07f, maxMass = 400;
    // Every web is a rope: it only pulls, and the target keeps its weight. A
    // held target hangs from the wrist on a web this much longer than its
    // radius, below the hand, and swings as the hand moves. Until October 6
    // it was held at a point ahead of the hand, like on a stick: a trash can
    // stayed up in the air wherever the hand pointed.
    float holdDistance = .25f;
    // A new web leaves this much slack: taking hold of a target does not yet
    // pull it, so a webbed thug stays on its feet until the hand pulls.
    float slack = .3f;
    // Trigger winds a web in; a sharp pull of the hand away from the target
    // (the zip gesture) yanks it over to the hand at its pull speed times this.
    float reelSpeed = 12;
    float yankSpeed = 1.3f, yankDistance = .14f, yankWindow = .25f;
    float yankMultiplier = 5, minYankFlight = 7, maxYankFlight = 15;
    // A yank jerks the target onto an arc to the hand, taking as long as a
    // straight flight at the yank's speed would, or longer (a higher arc, up
    // to maxYankTime) where the lower arc meets a wall, and launched no faster
    // than yankLaunch times the yank's speed: an arc that needs more falls
    // short. In flight the web reels in at yankReel times the yank's speed and
    // pulls only a target that falls behind that, so what it strikes on the
    // way stops it as it would stop anything thrown. It flies tumbling end
    // over end at yankTumble radians per second, its near side first. One
    // that comes no closer for yankTimeout has stopped short and stays on its
    // web. In the 10:16 session of October 6 yanks launched props at 26-45
    // m/s, mostly upward, aimed where the pulling hand would have been after
    // the whole flight; let go of in flight, they kept soaring.
    float maxYankTime = 2.5f, yankLaunch = 1.5f, yankReel = .8f, yankTumble = 2.5f;
    // While a web brings its target in (a reel, a yank's flight) and pulls,
    // the target's swing across it slows at reelSteer per second, so it comes
    // in along the web: reeled from a perch without it, a can swung under the
    // hand and up past it like a pendulum whose rope shortens. A held
    // target's swing slows at swingDamping.
    float reelSteer = 4, swingDamping = .8f;
    // A target the web brings in (a yank or a reel) is braked at no more than
    // arrivalDeceleration, as a hand catches it, so it comes within
    // catchDistance of its hold length at half catchSpeed instead of flying
    // past the hand, and is caught there once no faster than catchSpeed.
    float arrivalDeceleration = 90, catchDistance = .5f, catchSpeed = 1.5f, yankTimeout = .5f;
    // The hardest one web pulls, in newtons: it changes its target's velocity
    // by at most this over the target's mass per second. A heavy target lags
    // the hand and swings wide, and one heavier than webForce over gravity
    // cannot be lifted by one web, only dragged. Two webs pull twice as hard.
    float webForce = 2400;
    // A taut web takes up its stretch at tetherResponse per second, giving a
    // little like elastic. A reel winds it in no more than reelLead ahead of
    // a target too heavy to follow. A taut web steadies a target's spin by
    // webSpin radians per second squared.
    float tetherResponse = 30, reelLead = .5f, webSpin = 20;
    // Targets heavier than liftMass fly slower when yanked and are thrown
    // with less of the multiplier.
    float liftMass = 60;
    // Letting go of a held target throws it with its own velocity relative to
    // the player, as the arm swung it on its web, times throwMultiplier for a
    // light target and less for a heavier one, up to maxThrowSpeed. A throw
    // near a character is aimed into it. In the 10:16 session every throw
    // reached the former 40 m/s cap (a flick, times 2.2).
    float throwMultiplier = 1.6f, maxThrowSpeed = 25;
    float aimAssistCone = .21f, aimAssistRange = 45, minAssistSpeed = 6;
    // A wall between hand and target this long, or a web stretched this far
    // past its length, lets go.
    float obstructionTime = .3f, breakStretch = 6;
    float gravity = 9.81f, fixedStep = 1.0f / 120.0f;
};
enum class GrabPhase : std::uint8_t { None, Tethered, Yanked, Held };
struct Grab {
    GrabPhase phase{};
    std::uint64_t target{};
    TargetKind kind{};
    float length{}; // web length while tethered
    Vec3 wrist{};   // where the web leaves the hand, at the latest step
    Vec3 end{};     // the target's centre, at the latest step
    float radius{};
    bool taut{}; // the web pulled on the target in the latest step
    // How hard the web pulled in the latest step, as a share of its strength
    // (webForce): a hanging trash can about 0.12, one swung hard or a target
    // too heavy to lift 1. A yank's jerk is 1.
    float tension{};
};
enum class GrabEventKind { Grab, Yank, Catch, Throw, Release, Lost };
struct GrabEvent {
    GrabEventKind kind;
    int hand;
    std::uint64_t target;
    float strength;
    Vec3 velocity{}; // a throw's launch velocity
};
// One hand's web on a target, tension only, from where it leaves the hand.
struct WebRope {
    Vec3 anchor{}, anchorVelocity{};
    float length{};
};
// What the webs do to one target over one physics step. It is a law, not a
// velocity: whoever simulates the target evaluates it with advance() against
// the target's actual state, so what the step before did to it (the ground,
// a wall, another prop) stays in its motion. The game evaluates it on its
// physics thread, the lab in its own step. Targets without a command move
// freely. Gravity always acts: no law holds a target up but a web pulling
// from above it.
struct TargetCommand {
    enum class Mode : std::uint8_t {
        Rope,   // one or two tension-only webs from the hands
        Follow, // the web brakes it toward a velocity, as a hand catches it
        Launch  // it leaves with `velocity` (and `spin`), once
    };
    std::uint64_t id{};
    TargetKind kind{};
    Mode mode{};
    bool thrown{}; // the web let go: the last command for this target
    // Rope: a taut web takes up its stretch at `response` per second, and
    // the target's swing across it, relative to the hand, slows at `damping`
    // per second.
    std::array<WebRope, 2> ropes{};
    std::uint8_t ropeCount{};
    float response{}, damping{};
    // Rope: the most each web changes its velocity, per second (the web's
    // strength over its mass). Follow: the most the brake does.
    float maxAcceleration{};
    // Follow: the velocity to approach. Launch: the velocity it leaves with.
    Vec3 velocity{};
    // When `spins`: the angular velocity (world axes, radians per second) to
    // take up at spinAcceleration (Rope, Follow) or to leave with (Launch).
    bool spins{};
    Vec3 spin{};
    float spinAcceleration{};
};
// The target's velocity after a step of `dt` under the command, from its
// actual position and velocity: gravity included, contacts not.
Vec3 advance(const TargetCommand&, Vec3 position, Vec3 velocity, float dt, Vec3 gravity);
// Its angular velocity after that step.
Vec3 advanceSpin(const TargetCommand&, Vec3 spin, float dt);
class WebGrab {
  public:
    explicit WebGrab(GrabConfig config = {});
    // One input sample (seconds since the previous one; zero repeats the last
    // one without treating it as a new hand sample). A new grip press shoots
    // at a target when one is nearer than the world. Returns the input for the
    // swing, without the grips a grab owns, so the same press never also
    // shoots a swing web.
    Input claim(float seconds, const Input&, const WorldQueries&, const TargetQueries&, const Body& player);
    // One physics step. Appends one command per target the webs act on.
    void step(float dt, const WorldQueries&, const TargetQueries&, std::vector<TargetCommand>& out);
    // The swing's view of `input`: without the grips (and triggers) a grab
    // owns. claim() returns this too; use it for samples claim() did not see.
    Input forSwing(const Input& input) const;
    // What a grip press with this aim would take now, if anything: for a
    // highlight before the shot.
    std::optional<GrabTarget> preview(Pose aim, const WorldQueries&, const TargetQueries&) const;
    // Drops every web; a grip still held must be released before the next shot.
    void releaseAll();
    const std::array<Grab, 2>& grabs() const {
        return grabs_;
    }
    // Since the last claim().
    const std::vector<GrabEvent>& events() const {
        return events_;
    }
    const GrabConfig& config() const {
        return config_;
    }

  private:
    struct HandState {
        // held: grip squeezed, with release hysteresis. owned: the grip
        // belongs to a grab until it is let go, even after the grab ended.
        bool held{}, owned{}, sample{}, triggerReleased{}, reeling{}, yankUsed{};
        Vec3 previous{}; // grip relative to head, tracking metres
        Vec3 wrist{}, wristVelocity{}, forward{};
        float pullDistance{}, pullTime{}, obstructed{}, flight{};
        // A yanked target's nearest approach to its hold length, and time since.
        float closest{}, stalled{};
        bool launched{}; // the yank's jerk is sent
    };
    void end(int hand, GrabEventKind reason, Vec3 velocity = {});
    void letGo(int hand, const TargetQueries&, const Body& player, bool otherReleasing);
    float holdLength(int hand) const;
    // How long a yanked target's arc to `hold` takes: as long as a straight
    // flight at `speed`, or longer and higher where the world is in the way.
    float planYank(Vec3 from, Vec3 hold, Vec3 holdVelocity, float speed, float radius, std::uint64_t target,
                   const WorldQueries&, const TargetQueries&) const;
    GrabConfig config_;
    std::array<Grab, 2> grabs_{};
    std::array<HandState, 2> hands_{};
    std::vector<GrabEvent> events_;
    std::vector<TargetCommand> pending_; // throws, sent with the next step
    // The player's velocity at the latest sample: a yank's arc is aimed where
    // the hand is carried, not where the pulling gesture itself would take it.
    Vec3 playerVelocity_{};
};
// How far a sphere lies off a ray, in radians: 0 when the ray passes through
// it; infinity behind the origin or starting beyond `distance`.
float rayMiss(Vec3 origin, Vec3 direction, float distance, Vec3 centre, float radius);
// The launch velocity, at the speed of `velocity`, whose arc from `from`
// passes through `target` under `gravity`: the flatter of the two arcs, or a
// straight line where the speed cannot reach. Nothing when `target` lies more
// than `cone` radians from the direction of `velocity`.
std::optional<Vec3> aimThrow(Vec3 from, Vec3 velocity, Vec3 target, float cone, float gravity);
// The launch velocity whose arc from `from` reaches `to` after `time`.
Vec3 arcVelocity(Vec3 from, Vec3 to, float time, Vec3 gravity);
} // namespace spidy
