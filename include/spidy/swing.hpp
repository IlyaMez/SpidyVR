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
    // A charged leap from the ground (Input::leap): up at leapUp for the
    // least charge and leapUpFull for a full one (4 and 11.5 m high at real
    // gravity), and along the stick at up to leapForward.
    float leapUp = 9, leapUpFull = 15, leapForward = 12;
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
    // Held against the wall instead (Swing::hugWalls): the body's centre this
    // far from it, inside the game's own capsule, so its collision does meet
    // the wall - which is the point. That is what the game answers by sticking
    // the body to the wall in its own crawl, the one hold it calls standing,
    // and so the one whose vents, doors and prompts work on a wall. The swing
    // keeps holding the body the whole time; nothing lets go of it.
    // It rests within a tenth of a metre above this, as the usual hold does
    // above its own clearance.
    float hugClearance = .2f;
    // Pressed into the wall (Swing::pressWalls): the body is driven into it at
    // this speed and the wall's clearance is not kept, so the game's own
    // collision meets it moving. That is what the game answers with its wall
    // state - held there, its crawl a second later - on a swing that meets a
    // wall (October 9 headset reports); a body only held against it at rest
    // got nothing in five seconds (October 14).
    float pressSpeed = 3;
    // Follow shallow facade steps and clear a sill before the native capsule
    // reaches it. Deeper setbacks still end the wall.
    float wallStep = .75f;
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
    // A standing body the stick walks at a wall mounts it: the game's jump,
    // then the swing's wall (mountWall). On by default since October 15, the
    // player: "use our locomotion system only and override the games state to
    // what we need". From October 14 a wall walked onto was the game's crawl,
    // so that its vents and doors worked; the game's interactions now work on
    // the swing's wall (game_offers), and the crawl, its wall run and the
    // hand-overs between them and the swing - which felt bad and twice put a
    // player out of the world - are gone from walls. allowMounts switches it
    // off where the game crawls the player through a vent.
    bool mountFromGround = true;
    // Physical webs: a web is a rope (the VR settings' PHYSICAL WEBS).
    // - It bends round the edge that comes between the body and its end
    //   instead of letting go there, round maxBends edges at most, and lies
    //   straight again when the body swings back. Until October 12 a wall in
    //   the way for obstructionTime cut the web off. A bend sits bendOut out
    //   of its edge, and a hit within bendClearance of it is that edge again.
    //   The rope beyond a bend is at least bendRope long.
    // - Its length is measured to the hand that holds it, not to the body: a
    //   taut web keeps its length whatever the arm does, and a hand pulled
    //   back pulls the body after it, as fast as it moved (maxHandSpeed at
    //   most). The hand is where the grip is from the head, handHeight above
    //   the body's centre.
    // - payOut(): it grows while the game walks the body away from it.
    //   Until October 12 it kept the length it was shot with, and snapped a
    //   body that had walked on back by all of that once it left the ground.
    bool physical = false;
    float bendOut = .06f, bendClearance = .35f, bendRope = .5f, handHeight = .6f, maxHandSpeed = 8;
    // One web in both hands: a grip press with the free hand within
    // strandReach of the other hand's web (from that hand on, toward what it
    // hangs from) takes hold of that web instead of shooting one. Each hand
    // holds it where it took it, as slack there as the web was. The hand
    // nearer the web's end along it (the shorter rope) bears the body, and
    // pulls and zips it; the other holds the slack below and moves nothing,
    // as on a real rope. When the bearing hand lets go, the other has the
    // web where it now is: no drop, no snap. Let go with the lower hand and
    // take the web again above the other: hand over hand. 0: every press
    // shoots.
    float strandReach = .16f;
    // In the air a web is slack only as far as its hand makes it: a body that
    // comes nearer the web's end - thrown toward it by a zip, carried on up
    // by a jump, swinging past close to it, turned along the ground - has the
    // web taken in by as much, and hangs from where it got to. Until October
    // 15 a web kept the length it was shot with, and the body fell free for
    // all the slack it had come in by before the web caught it again: up to
    // 56 m at a time in that day's session ("webs are too stretchy when
    // you're in the air with them"). The slack a hand makes reaching toward
    // the web's end (handSlack at most) it keeps, and takes up first as it
    // draws back, so the arms pump nothing: the first build took that in too,
    // and every move of an arm after it pulled the body up the web ("too
    // springy"); a second kept up to 0.6 m of any slack, and caught a fast
    // swing past close to a web's end 0.6 m late, at up to 9 m/s. On the
    // ground and on a wall a web keeps its length, as before (payOut).
    bool taut = false;
    float handSlack = .8f;
    // Near misses: a body a web holds from above in the air comes down to the
    // ground no nearer than groundClearance (its centre over the ground), its
    // fall turned along the ground at groundTurn (m/s^2) from as high as that
    // takes, its speed kept. The web is not wound in for it: October 15's
    // first build wound it in wherever the swing's bottom was under the
    // street, from the moment it was shot - half of the next session's
    // swinging, the player pulled toward the web's end and held at the speed
    // limit ("throw me too much", "the wind is too strong", "i want the near
    // misses"). 0: none.
    float groundClearance = 0, groundTurn = 15;
    // Arcade webs (the VR settings' WEB STYLE: ARCADE): a web goes through
    // thin things - lamp posts, signs, railings, a roof's parapet - and
    // through whatever is not fixed (cars, props), both as it is shot and as
    // it swings: it neither sticks to one, nor bends round it, nor is cut off
    // by it, and what is beyond takes the shot. Thin: a ray from thinDepth
    // behind the face a web met, back out to it, meets the thing's far face
    // (Swing::thin). Buildings hold webs and bend them as before. And a swing
    // comes down to the ground no nearer than arcadeClearance instead of
    // groundClearance. The player, October 19: "webs get tangled over
    // lightpoles and other small geometry, also they pull you down too much
    // to the street" - that day's sessions bent 12 to 21 webs each round
    // edges, some 3-7 m over the street, and took them off again 0 to 3
    // times.
    bool arcade = false;
    float thinDepth = 1.5f, arcadeClearance = 6;
};
// The edges one web bends round at most (SwingConfig::physical).
constexpr int maxBends = 6;
struct HandInput {
    Pose aim{};                // world space, -Z forward
    Vec3 gripRelativeToHead{}; // tracking meters, excludes artificial travel
    bool tracked{};
    // grip shoots and holds this hand's web; trigger reels it in (a free
    // hand's shoots a web ball). The controller's own grip and trigger unless
    // the player swapped them (trackedSwingInput).
    float trigger{}, grip{};
    // Physical controller grip and position, independent of the web-button mapping.
    float wallGrip{};
    Vec3 wallPosition{};
};
struct Input {
    std::array<HandInput, 2> hands{};
    Vec3 move{}; // world horizontal direction, magnitude <= 1
    float trackingYaw{};
    bool focused = true, jump{};
    // A flip's tilt of the tracking space (Rig::tilt), the identity while level.
    Quat tilt{};
    // A charged leap let go of: its charge, above 0 up to 1, for as long as
    // the caller holds it out (it counts once, as it appears). The swing's
    // own Charge Jump: the game's is a skill a save may lack.
    float leap{};
    // Where the head looks (world, unit; zero: unknown): the enemy a punch
    // would go to is the one looked at (game_punch).
    Vec3 look{};
    // Both grips are held for the wall roll (HAND WALL CLIMBING): they shoot
    // no web and grab nothing until let go.
    bool wallRotate{};
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
// The longest step predictNativeStep predicts. A longer one counts as this
// long: the body goes less far in it, and keeps its webs and its flight.
constexpr float longestNativeStep = .1f;
// An edge a web bends round (SwingConfig::physical).
struct Bend {
    Vec3 from{}; // what the rope hung from before: the web's end, or the bend before
    Vec3 out{};  // out of the edge, unit: the rope lies on this side of it
    float span{}; // the rope between `from` and this bend
};
struct Web {
    bool attached{};
    // What the body hangs from: the web's end, or the nearest of its bends.
    Vec3 anchor{};
    std::uint64_t surface{};
    // length: the rope from `anchor` on (to the body, or with physical webs
    // to the hand).
    float length{}, tension{};
    bool airAnchor{};
    // The edges it bends round, from its end on; `anchor` is the last one.
    std::array<Bend, maxBends> bends{};
    int bent{};
    // Which web this is: each shot makes a new one. A hand that takes hold of
    // the other hand's web (SwingConfig::strandReach) holds the same one,
    // `taken` and not shot by it.
    std::uint32_t strand{};
    bool taken{};
    // Where the web sticks.
    Vec3 end() const {
        return bent ? bends[0].from : anchor;
    }
};
// What a web shot along an aim would do (Swing::shot): the web it would
// attach, or nothing when it would miss; and the first surface its ray met,
// held by a web or not (nothing: the ray was clear to maximum reach).
struct WebShot {
    std::optional<Web> web;
    std::optional<RayHit> hit;
    // Thin or loose things the ray went through before `hit` (SwingConfig::arcade).
    int passed{};
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
    Wrap,     // a web bent round an edge
    Unwrap,   // it came off its last one
    Leap,     // a charged leap left the ground; strength: its charge
    Join,     // a hand took hold of the other hand's web
    Pass,     // an arcade web went through a thin or loose thing: shot past one, or swung past one
    WallGrip, // a palm stuck to the wall; one contact pulse in that hand
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
    // Whether it takes NEW walls from now on. Off it keeps the one it has and
    // takes no other, so the next wall after this one is the game's - its own
    // crawl, the only hold the game itself calls standing, and so the only one
    // whose vents, doors and prompts work on a wall.
    //
    // This is what indoors asks for. Switching the walls off outright leaves
    // the wall at once (allowWalls -> leaveWall), and the body is held 0.9 m
    // clear of it, so that is a fall straight past it - measured twice in the
    // headset, October 13 and 14, both times as a drop the player did not ask
    // for. Taking no new ones drops nobody: he keeps what he is on until he
    // leaves it himself, and the game has the next.
    void takeNewWalls(bool allowed) {
        newWalls_ = allowed;
    }
    bool takingNewWalls() const {
        return newWalls_;
    }
    // Whether the wall the body is on holds it against itself (hugClearance)
    // rather than wallClearance out. It still holds it: the body is drawn in
    // at a walk, never let go of.
    void hugWalls(bool on) {
        hug_ = on;
    }
    bool huggingWalls() const {
        return hug_;
    }
    // Whether the wall the body is on is pressed into (pressSpeed) rather than
    // held off. The swing still holds the body: it does not leave the wall.
    void pressWalls(bool on) {
        press_ = on;
    }
    bool pressingWalls() const {
        return press_;
    }
    // The surface the GAME holds the body on now - its own wall crawl - as its
    // normal out of it and a point of its face; zero while it holds the body
    // on none. A web's shot is checked for a clear line from the body to its
    // anchor ("hands cannot shoot through a wall"), and on the game's crawl
    // the body is on the wall itself, so that line began at its surface: a
    // player who had walked up a wall could not swing off it (October 14, the
    // first mission's shaft: no web held in 160 tries from the crawl). It
    // begins wallClearance out, as it does from a wall of the swing's own.
    //
    // And for backSeconds after, no wall is taken whose face is turned
    // against it and lies behind it: that is its own face met from inside it.
    // Off the game's crawl the body is on the wall's surface, inside its
    // collision, and a ray from there meets that face from behind, turned
    // toward the body; taken, it held a player on the wrong side of the first
    // mission's shaft until he fell out of the world (October 15,
    // game-vr-20261015-094756 at 55 s: 0.6 m behind it). An outside corner's
    // next face is never turned against the last one.
    static constexpr float backSeconds = 1.5f;
    void supportSurface(Vec3 normal, Vec3 point = {}) {
        support_ = finite(normal) && length(normal) > .5f ? normalized(normal) : Vec3{};
        if (length(support_) > .5f && finite(point) && length(point) > 0) {
            back_ = {point, support_};
            backLeft_ = backSeconds;
        }
    }
    // Whether walking at a wall - or on along the game's own crawl - mounts
    // the swing's wall from now on (with SwingConfig::mountFromGround). Off
    // where the game crawls the player through a vent or a duct: a mount
    // there is the game's jump inside a duct.
    void allowMounts(bool allowed) {
        mounts_ = allowed;
    }
    // Whether the game holds the body in one of its own wall states now (its
    // crawl, its grasp, its run and the turns between them). No wall is taken
    // from there but by the mount's jump: the body is on the wall's surface,
    // inside its collision, and a ray from there meets the back of the wall,
    // turned toward the body. Taken, it held a player behind the first
    // mission's shaft until he fell out of the world (October 15).
    void gameHoldsWall(bool holds) {
        gameHolds_ = holds;
    }
    // Whether webs are ropes from now on (SwingConfig::physical). Off, a web
    // that bends round an edge hangs from that edge as if it had stuck there.
    void allowPhysical(bool allowed);
    // Whether webs are arcade webs from now on (SwingConfig::arcade). A web
    // already bent round something keeps its bends until it comes off them.
    void allowArcade(bool allowed);
    // The body, at `position`, stands or moves some other way than this
    // swing moves it (the game walks it): each physical web is long enough
    // to reach it with `slack` to spare, so an arm that moves does not lift a
    // standing body off the ground; a winch, a zip or a step off an edge
    // does, as before. Before the prediction of such a step.
    void payOut(Vec3 position, float slack);
    // Off the wall at once, with no jump (the caller lost track of the body).
    // releaseAll() keeps the wall: the body stays on it without input.
    void leaveWall();
    void allowHandClimb(bool on) {
        handClimb_ = on;
        if (!on)
            climbing_ = {};
    }
    // A near-wall squeeze belongs to climbing before other actions.
    bool wallHand(int hand, const Input&, const WorldQueries&) const;
    unsigned climbingHands() const {
        return (climbing_[0].attached ? 1u : 0u) | (climbing_[1].attached ? 2u : 0u);
    }
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
    // Both hands hold one web (SwingConfig::strandReach).
    bool shared() const {
        return webs_[0].attached && webs_[1].attached && webs_[0].strand && webs_[0].strand == webs_[1].strand;
    }
    // Hand `hand` is free and on the other hand's web, as of the latest
    // input: its grip press would take hold of that web, and shoot none.
    bool onStrand(int hand) const;
    // Hand `hand`'s web holds the body when taut: any web but the longer
    // rope of two hands on one web.
    bool bears(int hand) const;
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
        // Physical webs: the hand from the body's centre, in the world, and
        // how fast it moved there in the latest input sample.
        Vec3 offset{}, rate{};
        // The same place whatever the webs are, as the latest sample had it:
        // where the hand takes hold of the other hand's web.
        Vec3 reach{};
        // The slack this hand made reaching toward its web's end, which a
        // taut web keeps (SwingConfig::taut).
        float slack{};
        // Its web's line goes through a thin or loose thing now (arcade webs).
        bool passing{};
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
    // Whether a hit on the line from `from` to `anchor` is a real wall between
    // them; `bend`: the anchor is an edge the web bends round.
    bool blocks(const RayHit& hit, Vec3 from, Vec3 anchor, bool bend = false) const;
    // Arcade webs: whether what a ray met at `hit` is a thin thing.
    bool thin(const RayHit& hit, const WorldQueries&) const;
    // The first hit from `from` along unit `way` within `reach`; with arcade
    // webs the first that is fixed and not thin, the others it went through
    // counted into `passed`.
    std::optional<RayHit> solid(Vec3 from, Vec3 way, float reach, const WorldQueries&,
                                int* passed = nullptr) const;
    // What hand `i`'s rope holds the body's centre to: its web's anchor, less
    // the hand's offset with physical webs.
    Vec3 hold(int i) const;
    // The line from the body to web `i`'s anchor met `first`: bends the web
    // round the edge between there and `before`, where the body was a step
    // ago with the line clear. False when no such edge is found.
    bool wrap(int i, Vec3 before, const RayHit& first, const WorldQueries&);
    // Takes web `i` off its last bend once the rope lies free of that edge.
    void unwrap(int i, const WorldQueries&);
    // Rope lengths after winding each hand's rope in at its rate for a time,
    // which may be negative, within what the ropes and their anchors allow.
    std::array<float, 2> reeled(std::array<float, 2> rates, float seconds) const;
    // The share of this hand's winch rate the body's velocity takes up, given
    // the unit direction from its anchor to the body.
    float follow(int hand, Vec3 radial) const;
    // Turns the fall of a body a web holds along the ground near it, for a
    // step of `dt` (SwingConfig::groundClearance).
    void skimGround(float dt, const WorldQueries&);
    SwingConfig config_;
    Body body_{};
    std::array<Web, 2> webs_{};
    std::array<HandState, 2> hands_{};
    std::vector<Event> events_;
    double accumulator_{};
    float sinceZip_ = 100, sinceLanding_ = 100, zipSpeed_{};
    Vec3 zipDirection_{};
    bool jumpHeld_{}, jumpQueued_{};
    // A leap's charge waiting for the next step, and whether one is held out.
    float leapQueued_{};
    bool leapHeld_{};
    WallHold wall_{};
    struct ClimbingHand {
        bool attached{}, held = true;
        Vec3 previous{}, rate{};
        uint64_t surface{};
    };
    std::array<ClimbingHand, 2> climbing_{};
    bool handClimb_{};
    float climbSeconds_{};
    void climbInputs(const Input&, const WorldQueries&);
    std::optional<RayHit> wallContact(const HandInput&, const WorldQueries&) const;
    // Time the wall's ray has missed, time since a wall jump, and what is
    // left of the push over a wall's top edge, against crestNormal_.
    float wallLost_{}, sinceWallJump_ = 100, crest_{};
    // Whether a wall the body comes into is taken (takeNewWalls). The wall it
    // is already on is kept either way, corners of that wall included.
    bool newWalls_ = true;
    bool hug_{}, press_{};
    // Mounts allowed (allowMounts); the game holds the body on a wall of its
    // own (gameHoldsWall).
    bool mounts_ = true, gameHolds_{};
    Vec3 support_{};
    // The game's crawl's surface as it last held the body there (a point of
    // its face, its normal), and how long its back is still refused.
    struct Face {
        Vec3 point{}, normal{};
    } back_{};
    float backLeft_{};
    // The clearance the wall holds the body at now.
    float clearance() const {
        return hug_ ? config_.hugClearance : config_.wallClearance;
    }
    Vec3 crestNormal_{};
    // Rounding a corner onto the wall's next face: the way along that face,
    // and the time left to come in front of it.
    Vec3 wrapWay_{};
    float wrapLeft_{};
    float wrapEnd_{};
    bool wrapSeated_{};
    // Mounting from the ground: how long the stick has walked at a wall, the
    // wall that takes the body once it is in the air and for how long yet,
    // and whether the caller offered that wall (now, and the one kept).
    RayHit mountWall_{};
    float mount_{}, mountLeft_{};
    bool mounting_{}, offered_{}, mountOffered_{};
    // The step the input sample in hand is for (a hand's rate is no faster
    // than its travel over that step).
    float stepSeconds_{};
    // Webs shot so far (Web::strand).
    std::uint32_t strands_{};
};
} // namespace spidy
