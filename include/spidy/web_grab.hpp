#pragma once
#include "swing.hpp"
#include <vector>

namespace spidy {
// Webs that catch things instead of the world: props and characters a hand's
// web can grab, yank, carry, swing and throw. Engine independent. An adapter
// reports targets and applies the velocities computed here: the lab simulates
// its own props, the game drives its physics bodies.
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
    // A held target hangs this far beyond the hand along its aim, plus its radius.
    float holdDistance = .9f;
    // A new web leaves this much slack: taking hold of a target does not yet
    // pull it, so a webbed thug stays on its feet until the hand pulls.
    float slack = .3f;
    // Trigger winds a web in; a sharp pull of the hand away from the target
    // (the zip gesture) yanks it over to the hand at its pull speed times this.
    float reelSpeed = 12;
    float yankSpeed = 1.3f, yankDistance = .14f, yankWindow = .25f;
    float yankMultiplier = 6, minYankFlight = 10, maxYankFlight = 24;
    // A yanked target that has come no closer for yankTimeout is snagged on
    // something and stays on the web where it is. A fixed timeout ran out
    // 8.5 m short of the hand on a 43 m yank in the game.
    float arrivalDeceleration = 90, catchDistance = .5f, yankTimeout = .5f;
    // The hardest a web speeds up a target it tows or yanks, at liftMass or
    // below. A web taking up slack picks a target up over a few steps.
    float webAcceleration = 240;
    // A held target moves with its hold point and closes the gap to it at
    // holdResponse per second. Mass above liftMass scales its strongest
    // acceleration down, to a floor.
    float holdResponse = 20, maxHoldAcceleration = 420, liftMass = 120;
    // Letting go multiplies the target's speed relative to the player, so a
    // flick of the arm throws hard; a throw near a character is aimed into it.
    float throwMultiplier = 2.2f, maxThrowSpeed = 40;
    float aimAssistCone = .21f, aimAssistRange = 45, minAssistSpeed = 6;
    // Fraction of a taut web's stretch taken out in one step: below 1 the web
    // gives a little, like elastic.
    float tetherStiffness = .6f;
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
};
enum class GrabEventKind { Grab, Yank, Catch, Throw, Release, Lost };
struct GrabEvent {
    GrabEventKind kind;
    int hand;
    std::uint64_t target;
    float strength;
    Vec3 velocity{}; // a throw's launch velocity
};
// What the target does in this step. Targets without a command move freely.
struct TargetCommand {
    std::uint64_t id{};
    TargetKind kind{};
    Vec3 velocity{}; // velocity over this step, gravity already included
    bool thrown{};   // the web let go: the last command for this target
};
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
        Vec3 wrist{}, wristVelocity{}, forward{}, forwardRate{};
        float pullDistance{}, pullTime{}, obstructed{}, flight{};
        float closest{}, stalled{}; // a yanked target's nearest approach to the hand, and time since
    };
    void end(int hand, GrabEventKind reason, Vec3 velocity = {});
    void letGo(int hand, const TargetQueries&, const Body& player);
    float holdLength(int hand) const;
    GrabConfig config_;
    std::array<Grab, 2> grabs_{};
    std::array<HandState, 2> hands_{};
    std::vector<GrabEvent> events_;
    std::vector<TargetCommand> pending_; // throws, sent with the next step
};
// How far a sphere lies off a ray, in radians: 0 when the ray passes through
// it; infinity behind the origin or starting beyond `distance`.
float rayMiss(Vec3 origin, Vec3 direction, float distance, Vec3 centre, float radius);
// The launch velocity, at the speed of `velocity`, whose arc from `from`
// passes through `target` under `gravity`: the flatter of the two arcs, or a
// straight line where the speed cannot reach. Nothing when `target` lies more
// than `cone` radians from the direction of `velocity`.
std::optional<Vec3> aimThrow(Vec3 from, Vec3 velocity, Vec3 target, float cone, float gravity);
} // namespace spidy
