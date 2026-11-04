#pragma once
#include "web_grab.hpp"
#include <array>
#include <optional>
#include <vector>

// The web tie: an enemy webbed up (the web shooter's balls, three hits for a
// street thug) is stuck by a web to a wall, the floor or a thing a web holds
// on (a lamp post, a crane, a crate), and stays there until the fight is over.
//
// The gesture: one hand's web holds on a surface (a swing web shot at it), the
// other hand's web holds the webbed-up enemy (a grab). The player brings both
// hands together and clicks the right thumbstick. Both webs leave the hands
// and become one: from the point the first web stuck to, it draws the enemy
// over to the surface and holds him there, a little off it (his radius plus
// `gap`), whichever side of him faces it - his back or his belly.
//
// Engine independent. TieGesture decides when the gesture is made, WebTies
// moves the tied targets: each physics step one command per tie (a
// TargetCommand, evaluated as the grab's are). The adapter says whether the
// fight still goes on: a tie lets go once no enemy but the tied ones has been
// in the fight for `fightOverSeconds`, when its target is gone or down, or
// when a hand's web takes it again.
namespace spidy {
struct TieConfig {
    // The two grips this close (tracking metres, HandInput::gripRelativeToHead)
    // are together; the click counts up to `grace` seconds after they were.
    float handsNear = .3f, grace = .4f;
    // The tie draws its target toward its place at up to approachSpeed,
    // slower near it (approachGain per second of the distance left), so it
    // comes onto the surface instead of striking it, and holds it there.
    // Faster since the first session (October 31: 10 m/s and 3 /s on the
    // thug's mover: "it pulls very slowly"); a gain of 8 since November 2
    // ("there is a slight delay" before he sticks): he slows only in the
    // last two metres.
    float approachSpeed = 16, approachGain = 8;
    // He comes in an arc, not along the ground: far off, the tie aims this
    // share of the distance left above his place (at most arcLift metres),
    // the aim coming down onto it over the last arcEnd metres. Not up into a
    // ceiling: the lift fades out as the surface faces down.
    float arcShare = .15f, arcLift = 1.5f, arcEnd = 2;
    // The tie's first pull (WebTies::launch) starts him at this share of
    // approachSpeed; the tie speeds him up from there, a yank, not a jump.
    float launchShare = .55f;
    // His centre stays this far beyond his radius off the surface.
    float gap = .05f;
    // Under a surface facing down (a ceiling, a crane's arm, a lamp), his
    // head touches it: his centre is this far below it, not his radius. In
    // November 2's sessions (08:36, 08:58) thugs tied under ceilings and
    // cranes stopped 1.0 m under them, half a metre short of a place their
    // radius put off it, and hung there pulled at a place they never reached.
    float overheadReach = 1;
    // The tie's strength, newtons: it changes its target's velocity by at
    // most this over the target's mass per second (a thug's 80 kg: 75 m/s²,
    // well above gravity, so he does not sag).
    float webForce = 6000;
    // No enemy but the tied ones in the fight this long: the fight is over.
    float fightOverSeconds = 5;
    float gravity = 9.81f;
    std::size_t maxTies = 8;
};
// What each hand has now, for the gesture.
struct TieHand {
    bool tracked{};
    Vec3 grip{}; // tracking metres, relative to the head
    // Its swing web holds on a surface (ready, not an open-air anchor), at `anchor`.
    bool wallWeb{};
    Vec3 anchor{};
    // Its grab holds a target (any phase): which, whether it is a character,
    // and whether the game has him webbed up.
    bool grabbing{};
    std::uint64_t target{};
    bool character{}, cocooned{};
};
struct TieRequest {
    int anchorHand{}, targetHand{};
    Vec3 anchor{};
    std::uint64_t target{};
};
class TieGesture {
  public:
    explicit TieGesture(TieConfig config = {});
    // One input sample (seconds since the previous one); `click`: the right
    // thumbstick's click is down now. A request on the press that comes while
    // the hands are together on the right webs, or within `grace` after.
    std::optional<TieRequest> update(float seconds, bool click, const std::array<TieHand, 2>& hands);
    // The webs are right and the hands are together now.
    bool armed() const {
        return armed_;
    }
    void reset();

  private:
    TieConfig config_;
    std::optional<TieRequest> last_; // the latest pairing the hands made
    float since_ = 1e9f;             // since it was made
    bool armed_{}, clicked_{};
};
// Where a target of `radius` is held off a surface whose web stuck at
// `anchor`, the surface's normal there `normal` (out of it, unit): his radius
// off a wall or the floor, overheadReach under a ceiling, between the two
// as the surface tilts.
Vec3 tiePlace(Vec3 anchor, Vec3 normal, float radius, const TieConfig& = {});
struct Tie {
    std::uint64_t target{};
    Vec3 anchor{}; // where the web sticks to the surface
    Vec3 place{};  // where the target's centre is held
    float age{};
    bool arrived{}; // the target came to its place
};
class WebTies {
  public:
    explicit WebTies(TieConfig config = {});
    // A new tie of `target` (as it is now) to the surface at `anchor`, its
    // normal `normal`. A target already tied is tied anew. False when there
    // is no room or the input is unusable.
    bool add(const GrabTarget& target, Vec3 anchor, Vec3 normal);
    // One physics step. fightOn: an enemy who is not tied is in the fight.
    // `taken`: targets a hand's web holds now (their ties end). Appends one
    // command per tie that goes on.
    void step(float dt, const TargetQueries&, bool fightOn, const std::vector<std::uint64_t>& taken,
              std::vector<TargetCommand>& out);
    bool tied(std::uint64_t target) const;
    // He came against his surface short of his place (something of it
    // nearer than its anchor, his size): his place is where he is now.
    void settle(std::uint64_t target, Vec3 place);
    void drop(std::uint64_t target);
    void clear();
    const std::vector<Tie>& ties() const {
        return ties_;
    }
    // Ties made, and ties the end of a fight let go of.
    std::uint64_t made() const {
        return made_;
    }
    std::uint64_t ended() const {
        return ended_;
    }
    // Ties a hand's web took over, and ties whose target was gone.
    std::uint64_t taken() const {
        return taken_;
    }
    std::uint64_t gone() const {
        return gone_;
    }
    const TieConfig& config() const {
        return config_;
    }

  private:
    TieConfig config_;
    std::vector<Tie> ties_;
    float quiet_{};
    std::uint64_t made_{}, ended_{}, taken_{}, gone_{};
};
// The command that holds a target at his tie's place: it is braked toward a velocity
// that takes it there (up to approachSpeed), gravity taken out, no harder
// than the tie's strength.
TargetCommand tieCommand(const GrabTarget& target, const Tie& tie, float dt, const TieConfig& = {});
// Where the tie aims a target at `position` on his way to `place` on a
// surface facing `normal`: above it while far (the arc; none under a
// ceiling), onto it near.
Vec3 tieAim(Vec3 position, Vec3 place, Vec3 normal, const TieConfig& = {});
// The velocity the tie's first pull gives him (a share of what it wants).
Vec3 tieLaunch(const GrabTarget& target, const Tie& tie, const TieConfig& = {});
} // namespace spidy
