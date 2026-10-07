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
};
struct HandInput {
    Pose aim{};                // world space, -Z forward
    Vec3 gripRelativeToHead{}; // tracking meters, excludes artificial travel
    bool tracked{};
    float trigger{}, grip{};
};
struct Input {
    std::array<HandInput, 2> hands{};
    Vec3 move{}; // world horizontal direction, magnitude <= 1
    float trackingYaw{};
    bool focused = true, jump{};
};
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
enum class EventKind { Attach, Miss, Release, Zip, PointLaunch, TrackingLost, Obstructed };
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
    // Whether a clear shot attaches in the air at maximum reach from now on
    // (the VR settings); off, it misses. Webs already attached
    // keep their anchors.
    void allowAirAnchors(bool allowed);
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
};
} // namespace spidy
