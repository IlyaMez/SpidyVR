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
// - Bots are flung (BotStateFlung, the game's launched reaction) on the first
//   pull, and steered through their own MoverStandard while the web holds
//   them (native movement Drive). When the web stops steering, the flight
//   takes the bot's velocity and the game flies and lands it. A bot the game
//   would not fling falls under Spidy's rays instead, until it is down.
//
// Pedestrians are kinematic crowd agents with no physics or flung state;
// they are not offered.
namespace spidy::game_grab {
struct Hand {
    uint32_t phase{}, kind{}; // GrabPhase and game_targets::Kind of the hand's target, 0 for none
    uint64_t target{};        // the target's actor record
    Vec3 end{};               // the target's centre, where the web ends
    float length{};
    uint32_t taut{}, reserved{};
};
struct Data {
    uint32_t magic = 0x53475244, version = 1, bytes = sizeof(Data), status{};
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
};
static_assert(sizeof(Hand) == 40 && sizeof(Data) == 320);
using Call = unsigned long(__stdcall*)(void*);
// drive and driven: the movement module's SpidyMotionDrive and
// SpidyMotionDrivenSample. kinds: bits 1 << game_targets::Kind to offer.
// Returns native_bodies' start code: 0 when it started, or nothing is offered.
uint32_t start(uintptr_t base, Call drive, Call driven, uint32_t kinds);
uint32_t stop();
// One input sample, in the swing's callback before the swing sees it.
// Returns the input for the swing, without the grips a grab owns.
Input claim(float inputSeconds, const Input&, const WorldQueries&, const Body& player);
// The swing's view of an input between grab ticks.
Input forSwing(const Input&);
// Whether the game has stepped its physics since the grab last stepped, and
// for how long. The grab steps with the game's physics, not the player's
// mover, which does not step while the player perches or stands: one command
// per physics step, each from the state that step left. Ticking twice per
// step restarted twice from the same state and gave targets half the pull.
bool due(float& dt);
// One grab step, of the length due() gave.
void step(float dt, const WorldQueries&);
// Lets every web go; a bot on a flight the web steered is handed to the game.
void cancel();
Data data();
// The kinds this build can move.
constexpr uint32_t movableKinds = 1u << static_cast<unsigned>(game_targets::Kind::throwable) |
                                  1u << static_cast<unsigned>(game_targets::Kind::bot);
} // namespace spidy::game_grab
