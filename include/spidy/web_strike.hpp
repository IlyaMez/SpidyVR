#pragma once
#include "math.hpp"
#include "punch.hpp"
#include "swing.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>

// Fighting across the room: the combat of Batman: Arkham Shadow, with
// Spider-Man's own means. There a punch thrown at an enemy anywhere in the
// room slides the player to him while time slows, each blow answers with a
// flash and a kick in the hand, and blows that follow one another build a
// combo toward a finisher (its makers' talk on it: "Punching Across the
// Room"). Here:
//
// - The web strike. A fist thrown at an enemy beyond its reach takes the
//   player to him, along the ground or through the air, and its blow lands
//   as he arrives (WebStrike). Out of a swing, a jump or a fall it is a
//   dive; at an enemy in the air it goes up to him, and the player hangs
//   there for a moment after every blow landed off the ground, to land the
//   next.
// - Blows that carry. The game's melee answers the hardest blow with a
//   flinch (VALIDATION.md, October 8: kMelee with kFlyBack, kSuperFlyBack or
//   kAirborne played BotStateHitReactGame only), where its kinetic blow
//   sends a thug flying. A hard blow, an uppercut, a blow at a thug in the
//   air and a finisher go out as kinetic blows, and his flight gets a
//   velocity (deliver()): an uppercut lifts him off his feet to be followed
//   up, a blow down at a thug in the air slams him into the ground.
// - Flow. Blows within a few seconds of one another count up; each hits a
//   little harder than the one before, and every fifth is a finisher (Flow).
// - What a landed blow feels like. The game's time stops for a moment
//   (HitStop); the burst at the fist (web_visual.hpp), the controller's kick
//   and the sound (impact_sound.hpp) are the headset's (game_xr.cpp).
//
// Engine independent, like punch.hpp, which it builds on: game_punch.cpp
// runs it in the game.
namespace spidy {
// One enemy the player can fight.
struct FightTarget {
    std::uint64_t id{};
    Vec3 feet{}; // his base (a flyer's: its underside)
    float height = 1.8f, radius = .3f;
    // Off his feet in a flight (launched, flung, thrown): a blow juggles him.
    bool airborne{};
    Vec3 middle() const {
        return feet + Vec3{0, height * .5f, 0};
    }
};
// One hand in one input sample.
struct FightFist {
    bool tracked{};
    // The fist in the world, and the arm's own motion (Punches::velocity:
    // relative to the player, in the world's axes).
    Vec3 position{}, velocity{};
    // free: no web of this hand's holds anything or carries the player.
    // holds: the enemy this hand's web holds (0: none; such a hand is not
    // free, and strikes him alone).
    bool free{};
    std::uint64_t holds{};
    // The hand's web button, 0..1 (HandInput::grip).
    float web{};
};
// What the player has in mind as a fist is thrown.
struct StrikeWish {
    // The enemy his next punch goes to (Lock; 0: nobody is marked, and the
    // fist picks as it points). With one marked a strike goes to him and to
    // no other: a punch that falls short of the thug in front of the player
    // does not fly him to another the fist happens to point at.
    std::uint64_t target{};
    // The way the flow's call asks a fist to travel (Cues::direction: world,
    // unit; zero: nothing is called). A fist swung that way at the marked
    // enemy opens the strike too: a hook, an uppercut or a blow down, which
    // go across and not out at him.
    Vec3 called{};
    // With combat visuals off, an upward/downward swing needs no cue.
    bool freeDirections{};
};
struct WebStrikeConfig {
    // A strike opens with a fist at least this fast relative to the player
    // (a punch lands from 2.2 m/s, PunchConfig), going out from the player:
    // the cosine between its motion and the line to the fist from the base
    // of the neck, `neck` under the head. A hand drawn back and a hook
    // across the body open none; a fist thrown up from the guard at a thug
    // overhead does.
    float minSpeed = 2.6f, outward = .3f, neck = .25f;
    // For this long without a break: a twitch of the tracking is no punch.
    float sustain = .04f;
    // A free hand whose web button went down this recently is shooting a
    // web, not punching: an arm thrown out to swing away from a fight opens
    // no strike back into it. (A web that caught a thug makes the hand his:
    // the same throw then strikes him.)
    float pressSeconds = .3f;
    // A strike whose player takes no step for this long ends (a perch, a
    // ledge the game holds him on): no flight out of nowhere, seconds later.
    float stall = .3f;
    // A flight that goes up more than `liftOff` from where the player
    // stands leaves the ground by the game's own jump first (the game keeps
    // a standing body on its ground, whatever velocity it is given: the
    // swing's takeoff, October 4). The flight's clock waits for that jump
    // (wait()), for maxWait at most.
    float liftOff = .6f, maxWait = .7f;
    // Thrown at an enemy: the cosine between its motion and the line from
    // the fist to his middle (37 degrees), wider for the one enemy this
    // hand's web holds. Of several, the one it points at best.
    float aim = .8f, heldAim = .4f;
    // At the enemy the player has marked (StrikeWish): thrown his way within
    // 60 degrees. And a fist that travels the way the flow calls (within
    // calledAim of it, a cosine) need only not go away from him.
    float markedAim = .5f, calledAim = .5f, calledToward = .15f;
    // He is this far from the player's chest at least (nearer, the fist
    // reaches him by itself) and at most, with nothing fixed in between.
    float minDistance = 1.3f, maxDistance = 10, chest = 1.2f;
    // A hand whose web let go of something (a throw's follow-through, the
    // end of a swing) opens none for this long; one that opened a strike or
    // landed a punch opens the next once it slowed below rearmSpeed.
    float freeSeconds = .35f, rearmSpeed = 1.2f;
    // The player stops this far from his axis (wider for a wider enemy than
    // a street thug), feet `middle` below his middle: on the ground beside
    // a thug on his feet, at chest height beside one in the air.
    float standOff = .95f, middle = .9f;
    // The flight: at `speed` (real time, however slow the world runs), but
    // no shorter than minSeconds: a step to a thug two metres off is no
    // jolt. The player arrives within `arrive` of where he stops.
    float speed = 26, minSeconds = .12f, arrive = .08f;
    // A flight that has not arrived `grace` after it should have (a railing,
    // a wall's corner) ends; its blow still lands on a target within `reach`
    // of the chest.
    float grace = .25f, reach = 1.8f;
    // After a blow landed off the ground the player hangs for `hang`; each
    // further blow there starts it again, for maxHang in all between two
    // landings.
    float hang = .45f, maxHang = 3;
    // The blow a strike lands is the opening fist's, no weaker than this
    // (PunchEvent::strength): the flight is behind it.
    float minStrength = .5f;
    // From a flight's end to the next one opening.
    float pause = .08f;
    // The dive (dive()): the player's own flight - a swing, a zip, a jump, a
    // fall - takes him at the marked enemy, off the ground, at diveSpeed (real
    // m/s) or faster. It opens once his line passes within diveMiss of the
    // enemy's middle (more for a wider enemy) and gets there within diveTime,
    // diveReach at most. The strike flies him the rest of the way feet first,
    // as fast as he came (diveFlight at the least), and its blow is his
    // speed's: a heavy one from diveSpeed, the hardest from diveFull.
    float diveSpeed = 9, diveMiss = 1.1f, diveTime = .35f, diveReach = 7, diveFlight = 12, diveFull = 20;
};
class WebStrike {
  public:
    enum class Phase : std::uint8_t { Idle, Flying, Hanging };
    struct Opening {
        int hand{};
        std::uint64_t target{};
        float distance{}; // from the chest to his middle, metres
        bool held{};      // the hand's own web held him
        bool called{};    // the fist travelled the way the flow calls
    };
    // One step of the player's body.
    struct Step {
        bool drive{};            // the body is the strike's for this step
        Vec3 velocity{};         // at this velocity, real m/s
        bool landed{}, missed{}; // the flight ended: its blow lands, or it does not
    };
    explicit WebStrike(WebStrikeConfig config = {});
    // One input sample, `seconds` (real) after the previous one. head and
    // feet: the player's, in the world. Opens a strike when a fist asks for
    // one: the flight starts with the next step().
    std::optional<Opening> open(float seconds, const std::array<FightFist, 2>& fists, Vec3 head, Vec3 feet,
                                std::span<const FightTarget> targets, const WorldQueries& world,
                                const StrikeWish& wish = {});
    // Swings that would have opened a strike at another enemy than the
    // marked one, and opened none.
    std::uint64_t passedOver() const {
        return passedOver_;
    }
    // The player's own flight into the marked enemy: a dive (WebStrikeConfig
    // above) opens a strike whose hand is diveHand, its flight starting with
    // the next step(). velocity: the player's, real m/s, in the world; marked:
    // the enemy his next punch goes to (0: nobody, no dive). Call after open()
    // in the same sample. Its blow is as strong as `punch`'s fist speed.
    std::optional<Opening> dive(Vec3 velocity, bool grounded, Vec3 head, Vec3 feet,
                                std::span<const FightTarget> targets, const WorldQueries& world,
                                std::uint64_t marked, const PunchConfig& punch);
    static constexpr int diveHand = 2;
    // The flight in progress is a dive.
    bool diving() const {
        return phase_ == Phase::Flying && hand_ == diveHand;
    }
    // The strength of a dive at this speed (real m/s): 0 below diveSpeed.
    float diveStrength(float speed) const;
    // One step of the player's body, `seconds` (real) long, from `feet`;
    // target: the strike's, where he is now (nullptr: gone).
    Step step(float seconds, Vec3 feet, bool grounded, const FightTarget* target);
    // The flight in progress goes up from `feet` by more than liftOff: a
    // player on the ground must jump first.
    bool lifting(Vec3 feet) const {
        return phase_ == Phase::Flying && destination_.y - feet.y > config_.liftOff;
    }
    // Instead of step(): the body cannot take the flight's step yet (the
    // game's jump is taking it off the ground). `seconds` real.
    void wait(float seconds);
    // A blow landed while the player or the one it hit was off the ground:
    // the player hangs (WebStrikeConfig::hang, or `seconds` if longer: a thug
    // pulled to him in the air has just arrived).
    void hangOn(bool grounded, float seconds = 0);
    // This hand landed a punch: its swing opens no strike.
    void spent(int hand);
    // Idle at once, with no blow.
    void cancel();
    Phase phase() const {
        return phase_;
    }
    int hand() const {
        return hand_;
    }
    std::uint64_t target() const {
        return target_;
    }
    // Where the flight in progress stops (the player's feet), as of its latest step.
    Vec3 destination() const {
        return destination_;
    }
    // Seconds the flight took, or has taken so far.
    float elapsed() const {
        return elapsed_;
    }
    // The opening fist's motion (unit) and speed: the blow the flight lands.
    Vec3 direction() const {
        return direction_;
    }
    float fistSpeed() const {
        return fistSpeed_;
    }
    const WebStrikeConfig& config() const {
        return config_;
    }

  private:
    Vec3 stop(Vec3 feet, const FightTarget& target);
    WebStrikeConfig config_;
    Phase phase_{};
    int hand_{};
    std::uint64_t target_{};
    Vec3 destination_{}, direction_{}, away_{0, 0, 1};
    float fistSpeed_{}, elapsed_{}, duration_{}, hangLeft_{}, hung_{}, sinceEnd_ = 100, sinceStep_{}, waited_{};
    std::array<float, 2> freeFor_{}, thrownFor_{}, sincePress_{100, 100};
    std::array<bool, 2> armed_{true, true}, pressed_{}, passed_{};
    std::uint64_t passedOver_{};
};
// The fist speed whose blow has this strength (PunchEvent::strength).
float speedOf(const PunchConfig& config, float strength);

// One enemy at a time. A look marks the enemy the next punch goes to; a blow
// makes that mark a lock, and the lock stays on him wherever the player
// looks, until he is down. The user, October 12, after a session of 96 web
// strikes at 64 thugs: "i had a looot of accidental targeting and chaining of
// enemies where i just wanted to finish one". Until then the mark went to
// whoever was nearest the line of sight that instant, and a strike to
// whoever the fist's motion pointed at best: 24 of the 51 strikes thrown
// within a second and a half of a blow flew to another thug, 5.6 m off in
// the middle.
struct LockConfig {
    // A look takes the enemy nearest the line of sight, within `cone` of it
    // (a cosine: 40 degrees) and within `reach`; another takes the look's
    // mark from him only when this much nearer to that line.
    float cone = .766f, reach = 10, nearer = .04f;
    // A lock holds for this long (real seconds) after the latest blow at him.
    float idle = 4;
    // The player turns to another: one within `square` of the line of sight
    // (20 degrees) while the locked one is farther off it than `aside` (30
    // degrees), for `dwell` seconds.
    float square = .94f, aside = .866f, dwell = .3f;
};
class Lock {
  public:
    explicit Lock(LockConfig config = {});
    // One input sample, `seconds` (real) after the one before: the player's
    // head, where it looks (unit; zero: unknown) and the enemies in the
    // fight. Returns the enemy the next punch goes to, 0 for none.
    std::uint64_t update(float seconds, Vec3 head, Vec3 look, std::span<const FightTarget> enemies);
    // A blow landed on him, a strike opened at him, or a web holds him: he
    // is the one.
    void take(std::uint64_t id);
    void clear();
    std::uint64_t target() const {
        return target_;
    }
    // Held by a blow, where a look alone only marks.
    bool locked() const {
        return locked_;
    }
    const LockConfig& config() const {
        return config_;
    }

  private:
    LockConfig config_;
    std::uint64_t target_{};
    bool locked_{};
    float since_{}, away_{};
};

struct FlowConfig {
    // Real seconds from one blow to the next for the flow to go on.
    float window = 3.5f;
    // Every blow whose count this divides is a finisher.
    int finisher = 5;
    // Each blow of a flow hits this much harder than its first, up to `most`
    // times as hard.
    float gain = .1f, most = 1.5f;
};
class Flow {
  public:
    explicit Flow(FlowConfig config = {});
    void update(float seconds); // real
    // A blow landed: the count it brings the flow to.
    int hit();
    // A blow landed that does not count (a finisher that was not called,
    // Cues): the flow goes on from it as it stands.
    void keep() {
        since_ = 0;
    }
    // 1..0 of the time the flow has left before it starts over; 0 with no flow.
    float left() const {
        return count_ ? std::clamp(1 - since_ / config_.window, 0.f, 1.f) : 0.f;
    }
    void reset();
    int count() const {
        return count_;
    }
    bool finisher(int count) const {
        return count > 0 && count % config_.finisher == 0;
    }
    // How much harder than a first blow the blow with this count hits.
    float damage(int count) const;

  private:
    FlowConfig config_;
    int count_{};
    float since_{};
};

// Called blows. In Arkham Shadow a mark on the enemy asks for one blow in
// particular (a hook from one side, an uppercut, a blow down), and a combo
// runs through the blows it calls. Here the flow calls its next blow, from
// its `from`th on: an arrow (web_visual.hpp's appendCue) over the enemy the
// player's next punch would go to shows which way the fist must travel, as
// the player sees him, drawn from the side it comes from. A fist that lands
// going that way, or opens a web strike swung that way, is a called blow: it
// hits harder and throws him the way it went (up lifts him, down slams or
// floors him, a hook sends him flying to that side), and the flow calls
// another way. Any other blow is an ordinary one and the call stands, the
// same way, until a blow answers it or the flow ends. The flow's finisher
// must be called: at the count before it the call is the finisher's, and
// until a blow answers it the count stays where it is.
//
// The first version (October 12, fourth build) tied each call to the thug a
// blow landed on, showed it 0.2 s later for 3.5 s, and let any other blow at
// him replace it with a new way. In that day's 14:06 session 17 calls were
// made in 24 blows and none answered: a flurry replaced each before it
// showed, 13 of the blows were web strikes, which go straight at him and
// answer nothing, and the count stood at 4 for a minute with no finisher.
//
// The second (fifth build) kept one call for the flow, and web strikes still
// answered nothing. The 15:42 session: 27 calls, 2 answered. Of the 58 blows
// that landed while a call stood, 42 were web strikes; 32 blows landed with
// the finisher called and the count at 4. Since then a strike answers with
// the fist that opened it (StrikeWish::called), a swing the called way at
// the marked enemy opens one, and the arrow comes in from its side.
enum class CueWay : std::uint8_t { None, Left, Right, Up, Down };
struct CueConfig {
    // The flow's count from which it calls the next blow.
    int from = 2;
    // The cosine between a fist's motion and the called way, at least: 60
    // degrees to either side.
    float aim = .5f;
    // A called blow's damage, times.
    float damage = 1.5f;
};
struct Cue {
    CueWay way{};    // None: nothing is called
    bool finisher{}; // the blow it calls is the flow's finisher
};
class Cues {
  public:
    explicit Cues(CueConfig config = {});
    // Whether a fist's blow along `direction` (world, unit) goes the called
    // way, for a player who sees the enemy along `toTarget` (world, from the
    // head to him). False while nothing is called.
    bool answers(Vec3 direction, Vec3 toTarget) const;
    // A blow landed and brought the flow to `count`; the next one is its
    // finisher if `finisherNext`. A call that stands stays, the finisher's
    // from then on; one that was `answered` gives way to another. True when
    // a new way is called. `chance`: any number that differs from blow to
    // blow; the way called is never the one before.
    bool call(int count, bool finisherNext, bool answered, std::uint32_t chance);
    void clear();
    const Cue& cue() const {
        return cue_;
    }
    const CueConfig& config() const {
        return config_;
    }
    // The way `way` points in the world for a player who sees the target
    // along `toTarget`: level across his view to his left or right, or up or down.
    static Vec3 direction(CueWay way, Vec3 toTarget);

  private:
    CueConfig config_;
    Cue cue_{};
    CueWay last_{};
};

// What a landed blow is, for how it is delivered and how it feels.
enum class BlowKind : std::uint8_t {
    Hit,      // the game's melee, as hard as the fist was
    Heavy,    // a hard one: he flies off along it
    Launch,   // an uppercut: off his feet and up
    Juggle,   // at a thug in the air: he stays up
    Slam,     // down at a thug in the air: into the ground
    Finisher, // the flow's: he flies far
};
struct DeliveryConfig {
    // The strength from which a blow sends him flying (punch.hpp's FlyBack),
    // how fast along it at that strength and at full strength, and how fast up.
    float heavy = .85f, heavySpeed = 9, heavyFull = 14, heavyLift = 3;
    // An uppercut's launch: up, and a little along the blow. He peaks about
    // 3.5 m up, three quarters of a second later.
    float launchLift = 8.5f, launchCarry = 1.5f;
    // A blow at a thug in the air keeps him there.
    float juggleLift = 4.5f, juggleCarry = 3;
    // One that goes down this steeply (the sine of its pitch) slams him.
    float slamPitch = .5f, slamSpeed = 24, slamCarry = 2, slamDamage = 1.6f;
    float finisherDamage = 2.5f, finisherSpeed = 18, finisherLift = 5;
};
struct Delivery {
    BlowKind kind{};
    float damage{};
    // The game's kinetic blow (DamageType 7 with kFlyBack and an impulse,
    // which it answers with BotStateFlung) instead of its melee.
    bool kinetic{};
    Knockback knockback{};
    // His flight gets this velocity (real m/s), once he is on it.
    bool fly{};
    Vec3 velocity{};
};
// The freely chosen vertical move, using the same strength and pitch gates
// as ordinary uppercuts and downward punches.
CueWay verticalBlow(const PunchEvent& blow, bool targetAirborne, const DeliveryConfig& config = {});

// How the blow `blow` goes to the game: the `count`th of its flow, at a
// target on his feet or in the air. called: the way it was called and went
// (Cues::answers), None for an ordinary blow; calledDamage: such a blow's
// damage, times. freeDirections ignores calls and lets vertical moves take
// priority over an automatic finisher.
Delivery deliver(const PunchEvent& blow, bool targetAirborne, int count, const Flow& flow,
                 const DeliveryConfig& config = {}, CueWay called = CueWay::None, float calledDamage = 1.5f,
                 bool freeDirections = false);

// The game's time around a landed blow: it all but stops for a moment, then
// eases back, as fighting games freeze on a hit. The head and the hands keep
// real time (game_time.hpp slows the game).
struct HitStopTuning {
    // The game's time while stopped, per real second.
    float scale = .1f;
    // Real seconds stopped: for the lightest blow, and more for a full one;
    // and for a finisher, at its own scale.
    float hold = .03f, holdGain = .07f, finisherHold = .4f, finisherScale = .2f;
    // Real seconds easing back.
    float ease = .14f;
};
class HitStop {
  public:
    explicit HitStop(const HitStopTuning& tuning = {});
    // A blow of this strength (0..1) landed.
    void hit(float strength, bool finisher);
    void update(float seconds); // real
    // The game's time per real second now: 1, down to the tuning's scale.
    float timeScale() const;
    bool active() const {
        return since_ < hold_ + tuning_.ease;
    }
    void reset();

  private:
    HitStopTuning tuning_;
    float since_ = 1e9f, hold_{}, scale_ = 1;
};
} // namespace spidy
