#pragma once
#include "game_targets.hpp"
#include "web_grab.hpp"
#include <cstdint>

// The web grab in the game. The core (web_grab) runs in the swing's world
// query callback (game_swing.cpp), on its own clock, with the same input and
// the game's own rays. Targets come from game_targets, and each kind moves
// the way the game itself moves it:
//
// - Props (ThrowableHelper actors) are freed as the game's own throw frees
//   them, and the web's law moves their Havok bodies from their actual
//   motion, contacts included (native_bodies). Let go, they fly, bounce,
//   slide and come to rest by the game's physics, in real time.
// - Thugs fly the game's own launched reaction, BotStateFlung, flailing.
//   The web's first pull deals one the kinetic blow the game answers with
//   that flight (a direct request for the state is overridden by a fighting
//   thug's AI: until October 8 webbed thugs just slid along on their movers),
//   and from the next step on the web steers the flight it brings, its pull
//   written as the flight's velocity every step. Let go of, the game flies it
//   on and lands it (BotStateGroundFlop, then BotStateStunned). A thug the
//   game would not fling, or a civilian, is steered through his own
//   MoverStandard instead (native movement Drive), and let go of, falls
//   under Spidy's rays until he is down.
// - What flies hurts what it strikes, as the game's thrown bodies do: a thug
//   whose flight is stopped by the world, or who lands hard, takes a kinetic
//   blow by how much speed he lost; a thug or a thrown prop striking another
//   thug knocks him down or flings him (web_grab's StrikeConfig).
//
// Pedestrians are kinematic crowd agents with no physics or flung state;
// they are not offered.
namespace spidy::game_grab {
struct Hand {
    uint32_t phase{}, kind{}; // GrabPhase and game_targets::Kind of the hand's target, 0 for none
    uint64_t target{};        // the target's actor record
    Vec3 end{};               // the target's centre, where the web ends
    float length{};
    uint32_t taut{};
    // How hard the web pulls, as a share of its strength (Grab::tension).
    float tension{};
    // The web this hand just let go of (thrown, released or lost) still
    // hangs from `target`, at `end`, for trailSeconds: it is drawn falling
    // away with it rather than left where the target was. phase is 0.
    uint32_t trailing{}, reserved{};
};
// How long a web let go of is reported trailing its target.
constexpr float trailSeconds = 1.5f;
struct Data {
    uint32_t magic = 0x53475244, version = 3, bytes = sizeof(Data), status{};
    int64_t sequence{};
    uint64_t steps{}, commands{}, grabs{}, yanks{}, catches{}, throws{}, releases{}, lost{}, flights{},
        landed{};
    uint32_t candidates{}, error{}, kinds{}, driveFailures{};
    Hand hands[2];
    Vec3 lastThrow{};  // launch velocity of the latest throw
    float timeScale{}; // physics time per real second (native_bodies)
    // native_bodies: physics steps seen, props freed and rebuilt, body
    // velocities set, instance poses Spidy set, bots flung, flights steered,
    // requests the game refused, and leases that lapsed.
    uint64_t bodySteps{}, frees{}, rebuilds{}, writes{}, follows{}, flings{}, steers{}, refused{}, expired{};
    // The latest commanded prop's velocity through the step in flight, and
    // its actual velocity as that step began (real m/s); the real length of
    // the latest grab step and the physics length of the game's step.
    Vec3 commanded{}, observed{};
    float tickDt{}, stepDt{};
    // Thugs the web knocked into the game's flight (kinetic blows sent),
    // steps it steered such a flight, blows to flying thugs for what they
    // struck or landed on, and thugs struck by a flying thug or a thrown prop.
    uint64_t launches{}, flown{}, impacts{}, struck{};
};
static_assert(sizeof(Hand) == 48 && sizeof(Data) == 368);
using Call = unsigned long(__stdcall*)(void*);
// drive and driven: the movement module's SpidyMotionDrive and
// SpidyMotionDrivenSample. kinds: bits 1 << game_targets::Kind to offer.
// Returns native_bodies' start code: 0 when it started, or nothing is offered.
uint32_t start(uintptr_t base, Call drive, Call driven, uint32_t kinds);
uint32_t stop();
// Whether a start offered anything to catch.
bool offering();
// The VR settings switch catching off and on during play,
// in the swing's callback lock. Off, the webs let go of what they hold at the
// next sample and every press swings; the watch keeps running. On by default.
void allow(bool);
// One input sample, in the swing's callback before the swing sees it.
// Returns the input for the swing, without the grips a grab owns.
Input claim(float inputSeconds, const Input&, const WorldQueries&, const Body& player);
// The swing's view of an input between grab ticks.
Input forSwing(const Input&);
// What a grip press aimed along `aim` would catch now, if anything: the pick
// a press makes, without taking the target up. Nothing while a web holds
// something for `hand`, or nothing is offered.
std::optional<GrabTarget> preview(unsigned hand, Pose aim, const WorldQueries&);
// Whether a web holds something for this hand.
bool holds(unsigned hand);
// Whether the game has stepped its physics since the grab last stepped, and
// for how long. The grab steps with the game's physics, not the player's
// mover, which does not step while the player perches or stands: one command
// per physics step, each from the state that step left. Ticking twice per
// step restarted twice from the same state and gave targets half the pull.
bool due(float& dt);
// One grab step, of the length due() gave. player: the player's actor
// record, who deals the web's blows.
void step(float dt, const WorldQueries&, uint64_t player);
// Lets every web go; a bot on a flight the web steered is handed to the game.
void cancel();
Data data();
// The kinds this build can move.
constexpr uint32_t movableKinds = 1u << static_cast<unsigned>(game_targets::Kind::throwable) |
                                  1u << static_cast<unsigned>(game_targets::Kind::bot);
} // namespace spidy::game_grab
