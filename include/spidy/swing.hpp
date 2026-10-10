#pragma once
#include "math.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace spidy {
struct RayHit {
    Vec3 point{}, normal{};
    std::uint64_t surface{};
    bool fixed{true};
};
struct MoveResult {
    Vec3 position{}, normal{};
    bool hit{};
};
class WorldQueries {
  public:
    virtual ~WorldQueries() = default;
    virtual std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float distance) const = 0;
    virtual bool exists(std::uint64_t surface) const = 0;
};
class World : public WorldQueries {
  public:
    // Swept body center, sphere radius. Production adapter must use game collision.
    virtual MoveResult sweep(Vec3 from, Vec3 to, float radius) const = 0;
};
struct SwingConfig {
    float gravity = 9.81f, radius = .35f, maxRange = 100, minRope = 1.5f;
    float reelSpeed = 8, maxSpeed = 65, airAcceleration = 2, groundAcceleration = 24;
    float yankSpeed = 1.3f, yankDistance = .14f, yankWindow = .25f, zipMultiplier = 3.5f, maxZipImpulse = 12;
    float jumpSpeed = 6, pointLaunchWindow = .25f, zipLandingWindow = 1.2f;
    float fixedStep = 1.0f / 120.0f;
    // A held web lets go only when something stays between the body and the
    // anchor this long. Hits this close to the anchor (or a tenth of the rope,
    // if longer) are the anchor's own facade, ledges and sills, not a wall.
    float obstructionTime = .15f, anchorClearance = 1.5f;
    bool airAnchors = true; // A clear ray attaches at maximum reach, including open sky.
    // Walls: a body in the air that comes into a wall stays on it (WallHold).
    // The game held such a body still for a second and then stuck it to the
    // wall in its crawl, which only a jump left (October 9 headset reports).
    // On a wall the body keeps wallClearance between its centre and the wall,
    // so the game's own collision never meets it, and stays on it within
    // wallReach. A wall: fixed, its normal within wallSlope of level.
    bool walls = false;
    float wallClearance = .9f, wallReach = 1.5f, wallSlope = .5f;
    // The speed along the wall goes on (a wall run), raised by up to
    // wallCarry of itself for the speed that went into the wall; gravity does
    // not pull along a wall. The stick walks the
    // wall at wallWalkSpeed (what of it points into the wall goes up it) and
    // steers a faster run; without it or a web a run slows by wallBrake a
    // second, and below the walk's speed the body stops and stays.
    float wallCarry = .35f, wallWalkSpeed = 6, wallAcceleration = 24, wallBrake = 3;
    // A jump leaves the wall: out from it and up, on top of the speed along
    // it; no wall takes the body again within wallJumpPause.
    float wallJumpOut = 6, wallJumpUp = 5, wallJumpPause = .3f;
    // A wall the ray misses for wallGrace is gone (a recess, the wall's end).
    // Lost while the body went up it, the body hops over its top edge and is
    // pushed on over the roof for crestSeconds.
    float wallGrace = .15f, crestSeconds = .4f, crestPush = 12, crestHop = 4.5f;
    // A body on the ground cannot come into a wall: the game walks it, and
    // put one that walked into a wall on it in its own crawl (all five crawls
    // of the October 10 headset report began so). Walked at a wall within
    // mountReach for mountSeconds, such a body wants up onto it (mounting()):
    // the caller has the game jump, and the wall takes the body as it leaves
    // the ground, at a walk at most, and again within mountHold: as its jump
    // begins the game reports the body standing once more for a step (in the
    // game, October 10). A wall, not a kerb or a car: it is there mountHeight
    // above the body too. In the air the stick takes a wall within mountReach
    // the same way, with no speed into it.
    float mountReach = 1.3f, mountSeconds = .1f, mountHold = .5f, mountHeight = 1.2f;
};
struct HandInput {
    Pose aim{};                // world space, -Z forward
    Vec3 gripRelativeToHead{}; // tracking meters, excludes artificial travel
    bool tracked{};
    // grip shoots and holds this hand's web; trigger reels it in (a free
    // hand's shoots a web ball). The controller's own grip and trigger unless
    // the player swapped them (trackedSwingInput).
    float trigger{}, grip{};
};
struct Input {
    std::array<HandInput, 2> hands{};
    Vec3 move{}; // world horizontal direction, magnitude <= 1
    float trackingYaw{};
    bool focused = true, jump{};
    // A flip's tilt of the tracking space (Rig::tilt), the identity while level.
    Quat tilt{};
};
// The tracking space's orientation in the world (Rig::orientation): hand
// motion relative to the head turns into the world with it.
inline Quat trackingTurn(const Input& in) {
    return Quat::yaw(in.trackingYaw) * in.tilt;
}
struct Body {
    Vec3 position{0, 3, 0}, velocity{};
    bool grounded{};
};
struct MotionIntent {
    Vec3 target{}, velocity{};
    bool valid{};
};
struct Web {
    bool attached{};
    Vec3 anchor{};
    std::uint64_t surface{};
    float length{}, tension{};
    bool airAnchor{};
};
// What a web shot along an aim would do (Swing::shot): the web it would
// attach, or nothing when it would miss; and the first surface its ray met,
// held by a web or not (nothing: the ray was clear to maximum reach).
struct WebShot {
    std::optional<Web> web;
    std::optional<RayHit> hit;
};
// The wall the body is on (SwingConfig's walls).
struct WallHold {
    bool on{};
    Vec3 normal{};    // out of the wall, unit
    Vec3 point{};     // on the wall beside the body
    float distance{}; // the body's centre from the wall
    float seconds{};  // on walls since it came onto one
};
enum class EventKind {
    Attach,
    Miss,
    Release,
    Zip,
    PointLaunch,
    TrackingLost,
    Obstructed,
    WallOn,   // the body came onto a wall; strength: how hard it came into it
    WallOff,  // it left the wall, or the wall ended
    WallJump, // it jumped off the wall
};
struct Event {
    EventKind kind;
    int hand;
    float strength;
};
class Swing {
  public:
    explicit Swing(SwingConfig config = {});
    void update(float seconds, const Input& input, const World& world);
    // Predict a request for an external collision engine. The caller must submit
    // it through that engine and supply its actual result on the next call.
    // body().position always remains the supplied authoritative position.
    MotionIntent predictNativeStep(float seconds, const Input&, const WorldQueries&, Body actual);
    // Input and physics clocks may differ. Zero inputSeconds reuses button/
    // steering state without treating a repeated pose as a new hand sample.
    MotionIntent predictNativeStep(float seconds, const Input&, const WorldQueries&, Body actual,
                                   float inputSeconds);
    // A prediction covers a step whose length is not known yet. Once the
    // external engine reports how long that step lasts, call this before the
    // next prediction, so a winch has reeled for the real time. Frame times
    // vary by milliseconds; a rope that shortened for a different time than
    // the body travelled snapped the body by centimetres, a 1 m/s jolt.
    void settleStep(float predictedSeconds, float actualSeconds);
    void reset(Body body = {});
    void releaseAll();
    // A new speed limit during play (the VR settings). A body
    // faster than it slows to it in the next step.
    void limitSpeed(float maxSpeed);
    // A new gravity during play (the VR settings' weight), m/s^2, from the
    // next step.
    void setGravity(float gravity);
    // Whether a clear shot attaches in the air at maximum reach from now on
    // (the VR settings); off, it misses. Webs already attached
    // keep their anchors.
    void allowAirAnchors(bool allowed);
    // Whether a body in the air stays on the walls it comes into from now on
    // (SwingConfig's walls); off, it leaves the one it is on.
    void allowWalls(bool allowed);
    // Off the wall at once, with no jump (the caller lost track of the body).
    // releaseAll() keeps the wall: the body stays on it without input.
    void leaveWall();
    const WallHold& wall() const {
        return wall_;
    }
    // After a prediction: the body stands and wants up onto a wall, because
    // the stick walks it at one or the caller offered one (SwingConfig's
    // mountReach). The caller makes the game's jump while this holds.
    bool mounting() const {
        return mounting_;
    }
    // The same from the first moment the stick walks the standing body at a
    // wall. The caller keeps the stick from the game meanwhile: the game's
    // crawl takes a body 0.1 s after it meets a wall it is walked into (in
    // the game, October 10), as soon as the mount's own wait.
    bool mountBegun() const {
        return mounting_ || mount_ > 0;
    }
    // Before a prediction: a wall the game holds the body on in its own way
    // (its wall crawl), which the swing takes as soon as the body is off it.
    // A body that does not stand is taken at once, and arrives at rest: the
    // jump that took it off was not the player's.
    void offerWall(const RayHit& wall);
    // A web shot now along `aim` (world space, -Z forward) with the body at
    // `from`, without shooting it: the same test a grip press makes.
    WebShot shot(Pose aim, Vec3 from, const WorldQueries&) const;
    bool pointLaunchReady() const {
        return body_.grounded && sinceLanding_ <= config_.pointLaunchWindow &&
               sinceZip_ <= config_.zipLandingWindow;
    }
    const Body& body() const {
        return body_;
    }
    const std::array<Web, 2>& webs() const {
        return webs_;
    }
    const SwingConfig& config() const {
        return config_;
    }
    const std::vector<Event>& events() const {
        return events_;
    }

  private:
    struct HandState {
        // held: grip squeezed, with release hysteresis. A web shoots only on the
        // press that sets it, so a lost web never re-fires while the grip stays held.
        bool held{}, sample{}, zipUsed{}, triggerReleased{}, reeling{};
        Vec3 previous{};
        float pullDistance{}, pullTime{}, obstructed{};
        float winch{}; // how fast this hand's rope shortened in the last step
    };
    void inputs(float dt, const Input&, const WorldQueries&);
    void step(float dt, const Input&, const WorldQueries&, const World* collision);
    void move(Vec3 target, const World* collision);
    // Walls, once per update: follows the wall the body is on (round an
    // inside corner, and at a walk round an outside one), or finds the wall
    // it is coming into.
    void senseWall(float dt, const Input&, const WorldQueries&);
    void joinWall(const RayHit&);
    // The body's velocity on its wall for one step: the stick's, the brake.
    void wallMotion(float dt, const Input&);
    // Keeps the body at the wall's clearance; `before`: its distance from
    // the wall as the step began.
    void holdWall(float dt, float before, const World* collision);
    void release(int hand, EventKind reason = EventKind::Release);
    // Whether a hit on the line from `from` to `anchor` is a real wall between them.
    bool blocks(const RayHit& hit, Vec3 from, Vec3 anchor) const;
    // Rope lengths after winding each hand's rope in at its rate for a time,
    // which may be negative, within what the ropes and their anchors allow.
    std::array<float, 2> reeled(std::array<float, 2> rates, float seconds) const;
    // The share of this hand's winch rate the body's velocity takes up, given
    // the unit direction from its anchor to the body.
    float follow(int hand, Vec3 radial) const;
    SwingConfig config_;
    Body body_{};
    std::array<Web, 2> webs_{};
    std::array<HandState, 2> hands_{};
    std::vector<Event> events_;
    double accumulator_{};
    float sinceZip_ = 100, sinceLanding_ = 100, zipSpeed_{};
    Vec3 zipDirection_{};
    bool jumpHeld_{}, jumpQueued_{};
    WallHold wall_{};
    // Time the wall's ray has missed, time since a wall jump, and what is
    // left of the push over a wall's top edge, against crestNormal_.
    float wallLost_{}, sinceWallJump_ = 100, crest_{};
    Vec3 crestNormal_{};
    // Rounding a corner onto the wall's next face: the way along that face,
    // and the time left to come in front of it.
    Vec3 wrapWay_{};
    float wrapLeft_{};
    // Mounting from the ground: how long the stick has walked at a wall, the
    // wall that takes the body once it is in the air and for how long yet,
    // and whether the caller offered that wall (now, and the one kept).
    RayHit mountWall_{};
    float mount_{}, mountLeft_{};
    bool mounting_{}, offered_{}, mountOffered_{};
};
} // namespace spidy
