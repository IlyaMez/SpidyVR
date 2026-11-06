#pragma once
#include "math.hpp"
#include "swing.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Web lines (docs/WEB-LINE.md): a straight, taut web between two surfaces,
// as Spider-Man 2's. The player shoots one web at where the line is to
// start and the other at where it is to end (a wall beside him, a post, a
// crane), brings both hands together and clicks the right thumbstick: both
// webs leave the hands and become the line. The web tie's gesture with a
// surface in the other hand instead of a webbed-up enemy (web_tie.hpp).
//
// Engine independent. LineGesture decides when the gesture is made, WebLines
// keeps the lines (a few at once, the oldest giving way, each for a while,
// none far from the player) and answers where a ray meets one.
namespace spidy {
struct LineConfig {
    // The two grips this close (tracking metres) are together; the click
    // counts up to `grace` seconds after they were.
    float handsNear = .3f, grace = .4f;
    // A line is between minLength and maxLength long.
    float minLength = 1.5f, maxLength = 40;
    // How many at once (a new one past it takes the oldest's place), how
    // long each lasts, and how far from the player one may be before it goes.
    // Six since November 3 (three: "the fourth comes out a white line").
    std::size_t maxLines = 6;
    // No line goes by age, and only 500 m from the player (November 3, 19:00:
    // lines went after 3 minutes and 150 m away, the thugs on them left in
    // the air - "a web must never just go").
    float seconds = 1e9f, reach = 500;
    // A ray meets a line within this of its axis (WebLines::raycast).
    float radius = .12f;
};
struct LineHand {
    bool tracked{};
    Vec3 grip{}; // tracking metres, relative to the head
    // Its web holds on a surface (ready, not open air), at `anchor`.
    bool wallWeb{};
    Vec3 anchor{};
    // Its web holds a target (a grab): no line then, it is a tie's.
    bool grabbing{};
};
struct LineRequest {
    Vec3 start{}, end{}; // hand 0's anchor, hand 1's
};
class LineGesture {
  public:
    explicit LineGesture(LineConfig config = {});
    std::optional<LineRequest> update(float seconds, bool click, const std::array<LineHand, 2>& hands);
    bool armed() const {
        return armed_;
    }
    void reset();

  private:
    LineConfig config_;
    std::optional<LineRequest> last_;
    float since_ = 1e9f;
    bool armed_{}, clicked_{};
};
struct WebLineSpan {
    std::uint32_t id{};
    Vec3 start{}, end{};
    float age{};
    // The lines its start and its end are on (0: a surface). A line on a
    // line that goes goes with it: one end in the air holds nothing
    // (November 3, 18:30: a line whose line the limit took hung from the air).
    std::uint32_t on[2]{};
};
struct LineHit {
    std::uint32_t id{};
    Vec3 point{};     // on the line's axis, nearest the ray
    float distance{}; // along the ray
};
class WebLines {
  public:
    explicit WebLines(LineConfig config = {});
    // A new line, its start on line startOn and its end on endOn (0: on a
    // surface); false when it is too short, too long or not finite. Past
    // maxLines the oldest gives way - never `keep` (the line the player is on)
    // nor a line it is on - and the lines on it with it.
    bool add(Vec3 start, Vec3 end, std::uint32_t startOn = 0, std::uint32_t endOn = 0,
             std::uint32_t keep = 0);
    // Ages the lines and lets go of those too old or too far from `player`
    // (never `keep`, nor a line it is on), and of the lines on them.
    void step(float dt, Vec3 player, std::uint32_t keep = 0);
    // The nearest line a ray from `origin` along unit `direction` meets
    // within `distance` (within the line's radius of its axis).
    std::optional<LineHit> raycast(Vec3 origin, Vec3 direction, float distance) const;
    void clear();
    const std::vector<WebLineSpan>& lines() const {
        return lines_;
    }
    std::uint64_t made() const {
        return made_;
    }
    std::uint64_t expired() const {
        return expired_;
    }
    const LineConfig& config() const {
        return config_;
    }

  private:
    // Whether line `id` is `keep` or a line it is on (through any number).
    bool kept(std::uint32_t id, std::uint32_t keep) const;
    // Lets go of the lines on lines that are gone, and of those on them.
    void prune();
    LineConfig config_;
    std::vector<WebLineSpan> lines_;
    std::uint32_t next_ = 1;
    std::uint64_t made_{}, expired_{};
};
// The zip to a line (the right thumbstick held down to aim, let go with a
// line's marker in view), and the player on it: perched, walking along it,
// jumping off it and coming down on it again.
struct LineZipConfig {
    // A line is in view when a point of it lies within `cone` (radians) of
    // where the head looks, between nearest and farthest metres away.
    float cone = .18f, nearest = 2, farthest = 35;
    // The flight: up to `speed` m/s, faster by `accel` m/s^2, slowing over
    // the last `ease` metres; arrived within `arrive` m of the perch.
    float speed = 24, accel = 70, ease = 2.5f, arrive = .3f;
    // A zip as the game's web zip is (November 4: a far one "pulled me in
    // place to the other line, too arcade"): its web shot out first
    // (shotSeconds, the body left as it goes), then pulled in, the top speed
    // growing with the distance (speedBase + speedPerMetre a metre, up to
    // speed), the pull building at pullAccel; over farLift metres a distance
    // (farLift up to liftMost metres) its path bows up and comes down onto the
    // line, the lift fading as he nears it.
    float shotSeconds = .12f, speedBase = 10, speedPerMetre = .6f, pullAccel = 45, farLift = .15f,
          liftMost = 4;
    // On the line: the body (its centre) this high above it, held there by
    // `hold` per second of the distance off it, up to holdSpeed m/s; walked
    // along it at up to walkSpeed m/s by the stick, never past endMargin of
    // its ends.
    float perchHeight = 1, hold = 10, holdSpeed = 6, walkSpeed = 4, endMargin = .4f;
    // A jump off it: up at jumpSpeed m/s, along the stick at jumpCarry, under
    // `gravity` (the swing's: the VR settings' weight, setGravity); coming
    // down, the feet within catchRadius of the line (across it) and
    // catchHeight of its height are on it again. Coming down with no line
    // under them (past catchRadius + catchMargin across it), or dropBelow
    // under it, the line lets the body go to fall as the swing falls. Until
    // November 3 (16:34) the jump fell at 14 m/s^2 to 0.8 m under the line
    // whatever the weight: slow, then fast ("as if gravity were wrong").
    float jumpSpeed = 6.5f, jumpCarry = 4, gravity = 9.81f, catchRadius = .55f, catchHeight = .4f,
          catchMargin = .25f, dropBelow = .8f;
    // The game walks a player on a line (gameWalk) while he stays within this
    // of where the line holds him; farther, the line takes him back.
    float walkSlack = .5f;
    // The dodge on a line (the dodge's button, B): a dash along it at
    // dashSpeed for dashSeconds, the way the stick or the look goes along it.
    float dashSpeed = 11, dashSeconds = .3f;
    // A flight not there within this long lets go; nor one whose jump off
    // the ground has not come within waitSeconds; nor any zip at all past
    // maxSeconds + waitSeconds from its start.
    float maxSeconds = 3, waitSeconds = 1;
};
struct LineTarget {
    std::uint32_t id{};
    Vec3 point{}; // on the line
};
// The line point nearest where the head looks (from `eye` along unit
// `look`), if one is in view.
std::optional<LineTarget> lineTarget(const std::vector<WebLineSpan>& lines, Vec3 eye, Vec3 look,
                                     const LineZipConfig& = {});
class LineZip {
  public:
    enum class Phase { Idle, Flying, Perched, Jumping };
    explicit LineZip(LineZipConfig config = {});
    // Off to the point on line `id`.
    void start(LineTarget target);
    void stop();
    // The swing's gravity from now on (the VR settings' weight), m/s^2.
    void setGravity(float gravity);
    // The game walks him (its own walk, its own legs: GameWalk): his place on
    // the line follows where he is, ready to hold him there again.
    void follow(Vec3 position, const WebLineSpan& line);
    // Whether the game may walk a body at `position` on its line with the
    // stick `move` (world, horizontal): standing on it (perched), within
    // `walkSlack` of where the line holds him, and not pushed past an end.
    bool gameWalk(Vec3 position, Vec3 move, const WebLineSpan& line) const;
    // A dodge on the line: a dash along it, the way `way` goes along it
    // (world; its part along the line decides). Only standing on it.
    void dash(Vec3 way);
    std::uint64_t dashes() const {
        return dashes_;
    }
    struct Step {
        bool drive{}; // the zip has the body: velocity is its velocity now
        Vec3 velocity{};
        bool arrived{}; // came onto the line in this step (a zip's end, a jump's)
    };
    // One step of the body at `position` (its centre) moving at `velocity`.
    // waiting: the body is still on the ground, the game's jump to come -
    // nothing moves it yet. line: its line (null: gone). move: the stick,
    // world horizontal, magnitude up to 1. jump: the jump held.
    Step step(float dt, Vec3 position, Vec3 velocity, bool waiting, const WebLineSpan* line, Vec3 move = {},
              bool jump = false);
    Phase phase() const {
        return phase_;
    }
    std::uint32_t line() const {
        return target_.id;
    }
    // Where the body is held now (on the line), or flies to.
    Vec3 perch() const {
        return perch_;
    }
    // The point of the line a zip goes to (its web's end).
    Vec3 zipTo() const {
        return target_.point;
    }
    std::uint64_t zips() const {
        return zips_;
    }
    std::uint64_t perches() const {
        return perches_;
    }
    std::uint64_t jumps() const {
        return jumps_;
    }
    std::uint64_t landings() const {
        return landings_;
    }
    const LineZipConfig& config() const {
        return config_;
    }

  private:
    LineZipConfig config_;
    LineTarget target_{};
    Phase phase_ = Phase::Idle;
    Vec3 perch_{}, jumping_{};
    float zipSpan_{}; // how far the zip had to go when it began
    float flown_{}, waited_{}, total_{}, along_{}, dash_{}, dashLeft_{};
    bool jumped_{};
    std::uint64_t zips_{}, perches_{}, jumps_{}, landings_{}, dashes_{};
    Vec3 dashWay_{};
};
// The right thumbstick's click for the zip's aim: held down `hold` seconds
// it aims (the marker shows); let go after aiming, the zip. A click a gesture
// takes (the tie's or a line's: armed as it goes down, or made with it) is
// the gesture's, from its press to its release.
class LineAim {
  public:
    static constexpr float hold = .25f;
    struct State {
        bool aiming{}; // the marker shows now
        bool zip{};    // let go after aiming: zip to the marker it last showed
    };
    // click: held now. gestureArmed: a gesture would take a press now.
    // gestureTook: a gesture took this sample's click.
    State update(float seconds, bool click, bool gestureArmed, bool gestureTook);
    void reset() {
        *this = {};
    }

  private:
    float held_{};
    bool used_{}, before_{}, aimed_{};
};
// The world as the swing's webs see it, the web lines in it: a ray meets a
// line (WebLines::raycast) if it is nearer than what the world has there,
// and not within ignoreNear of where it starts - the line under the feet of
// a player on it is not where a web aimed down goes. A hit on a line is fixed,
// faces the ray, and its surface is lineSurface | the line's id, which exists
// while the line does. The body's own rays (bodyRaycast) meet no line. Until
// November 3 (17:30) webs went through lines, so no web held on one, and a
// web aimed under the line a player stood on tied his thug to that line.
constexpr std::uint64_t lineSurface = 0x5350494400000000ull, lineSurfaceMask = 0xffffffff00000000ull;
class LineWorld final : public WorldQueries {
  public:
    static constexpr float ignoreNear = 2;
    LineWorld(const WorldQueries& world, const WebLines& lines) : world_(world), lines_(lines) {}
    std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float distance) const override;
    std::optional<RayHit> bodyRaycast(Vec3 origin, Vec3 direction, float distance) const override {
        return world_.bodyRaycast(origin, direction, distance);
    }
    bool exists(std::uint64_t surface) const override;

  private:
    const WorldQueries& world_;
    const WebLines& lines_;
};
// The point of segment a-b nearest p.
Vec3 nearestOnSegment(Vec3 a, Vec3 b, Vec3 p);
// The nearest points of a ray (origin, unit direction, up to `distance`)
// and segment a-b: the ray's parameter and the segment's point.
float rayToSegment(Vec3 origin, Vec3 direction, float distance, Vec3 a, Vec3 b, Vec3& onSegment);
} // namespace spidy
