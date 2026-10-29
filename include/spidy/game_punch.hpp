#pragma once
#include "punch.hpp"
#include "swing.hpp"
#include "web_strike.hpp"
#include <cstdint>

// Punching in the game. The fists come from the swing's input samples (the
// aim pose sits at the knuckles; the grip relative to the head, in tracking
// space, is the arm's own motion), the characters from a watch of the
// game's bots (game_targets), and a punch becomes the game's own melee
// damage against the bot it hit (native_bodies::damage): DamageType kMelee,
// as much damage as the fist's speed gave it, the hit reaction the punch's
// strength calls for (punch.hpp's Knockback), the hero as the damager. The
// bot reacts, loses health and is knocked out as the game has it. The blow
// also earns the Focus the game's own melee blow earns (game_focus.hpp): the
// damage alone earns none.
//
// Fighting across the room (web_strike.hpp) runs here too. A fist thrown at
// an enemy out of reach opens a web strike: the swing moves the player to
// him (fly(), each step of his mover) and the blow lands as he arrives.
// Every blow, a fist's or a strike's, counts into the flow and goes out as
// deliver() has it: the hard ones, uppercuts, blows at a thug in the air and
// finishers as the game's kinetic blow, which it answers with its flight
// (BotStateFlung); that flight then gets the blow's velocity
// (native_bodies::fling), as the web grab steers the thugs it pulls.
namespace spidy::game_punch {
struct Hand {
    uint64_t punches{};    // landed by this hand
    uint64_t lastTarget{}; // the actor record it hit last
    float lastStrength{};  // 0-1
    float speed{};         // the hand's speed relative to the player now, m/s
    uint32_t lastKnockback{}, busy{};
};
struct Data {
    uint32_t magic = 0x53505544, version = 1, bytes = sizeof(Data), status{};
    int64_t sequence{};
    // Input samples seen, punches landed, damage requests the game issued and
    // those it dropped, the bots in reach of a fist, and the latest punch.
    uint64_t samples{}, punches{}, issued{}, dropped{};
    uint32_t bots{}, error{};
    Hand hands[2];
    Vec3 lastPoint{}, lastDirection{};
    float lastDamage{}, lastSpeed{};
    // Of the issued (the web's and the web shooter's blows among them), those
    // that went out as nobody's: native_bodies::Counters::damageUnnamed.
    uint64_t unnamed{};
};
static_assert(sizeof(Hand) == 32 && sizeof(Data) == 168);
// Fighting across the room (SpidyStrikeData, SpidyStrikeSample): the web
// strikes and every blow landed, for the headset's feedback and the session
// report.
struct Strikes {
    uint32_t magic = 0x53535452, version = 3, bytes = sizeof(Strikes), status{};
    int64_t sequence{};
    // Strikes opened; those whose blow landed, and those that ended without
    // it (a wall in the way, the target gone); of the opened, those at the
    // enemy the hand's own web held.
    uint64_t opened{}, landed{}, missed{}, held{};
    // Blows landed, fists' and strikes' alike, and of them by BlowKind.
    uint64_t blows{}, heavies{}, launches{}, juggles{}, slams{}, finishers{};
    // Flights given a blow's velocity, and those the game never flew (a
    // heavy, a scripted scene).
    uint64_t flown{}, unflown{};
    // WebStrike::Phase, the striking hand, the flow's count, and the enemies
    // alive within a strike's reach.
    uint32_t phase{}, hand{}, flow{}, enemies{};
    uint64_t target{}; // the strike's (an actor record)
    Vec3 destination{};
    // The latest strike: how far off its target was, and how long it took.
    float distance{}, seconds{};
    // The latest blow: where, along what, how hard (0-1), the damage it
    // asked for, its BlowKind, its count in the flow, its hand, whether a
    // strike landed it, and (version 2) whether it was a called blow.
    Vec3 lastPoint{}, lastDirection{};
    float lastStrength{}, lastDamage{};
    uint32_t lastKind{}, lastCount{}, lastHand{}, lastStrike{}, lastCalled{};
    // Version 2, called blows (web_strike.hpp's Cues): the ways called, the
    // fists' blows that answered one, and those that landed while a call
    // stood and went another way. Then the enemy the player's next punch
    // would go to (an actor record; 0: none in view), where his mark goes
    // (his chest), and what the flow calls now: which way (CueWay; 0:
    // nothing, the mark is a plain ring), whether it calls the finisher, and
    // how much of the flow's time is left (1 to 0).
    uint64_t calls{}, called{}, missedCalls{};
    uint64_t cueTarget{};
    Vec3 cuePoint{};
    uint32_t cueWay{}, cueFinisher{};
    float cueLeft{};
    // Version 3, one enemy at a time (web_strike.hpp's Lock). The enemy a
    // blow locked the player onto (0: none, the mark follows his look);
    // enemies the player's blows knocked out, and where the latest stood
    // (his chest); whether the mark shown is a lock's. Then strikes opened
    // by a fist swung the called way, strikes' blows that answered a call,
    // and swings that would have opened a strike at another enemy than the
    // marked one and opened none.
    uint64_t locked{}, knockouts{};
    Vec3 knockoutPoint{};
    uint32_t cueLocked{};
    uint64_t swung{}, strikeCalled{}, passedOver{};
};
static_assert(sizeof(Strikes) == 328);
// Starts the bot watch and the main-thread damage requests.
uint32_t start(uintptr_t base);
uint32_t stop();
bool running();
// The player as the swing has him at an input sample.
struct Player {
    uint64_t record{}; // his actor record, who deals the blows
    Vec3 feet{};
    // On the ground; and on a web that carries him (then a blow landed in
    // the air does not stop him there).
    bool grounded{}, swinging{};
    // Off: no hidden directional calls may override a launcher or ground smash.
    bool combatVisuals = true;
    bool halfPunchDamage{};
    // A bit per hand whose web holds something or carries the player: it
    // neither punches nor opens a strike. And the bot each hand's web holds
    // (an actor record, 0 for none): a punch at him with that hand strikes.
    uint32_t busy{};
    uint64_t holds[2]{};
    // His own motion, real m/s in the world (zero while the mover gives
    // none): a flight of his into the marked enemy dives at him
    // (WebStrike::dive).
    Vec3 velocity{};
};
// What a sample opened: a web strike, at the enemy the hand's own web held.
// The swing lets its webs go, and the grab his.
struct Opened {
    bool strike{}, held{};
};
// One input sample in the swing's callback (seconds since the previous one,
// 0: the same sample again).
Opened update(float seconds, const Input&, const Player&, const WorldQueries&);
// The web strike's say over the step of the player's mover about to be
// asked for: `seconds` of real time after the one before, from `feet`.
struct Flight {
    bool drive{};    // the player moves as the strike has it this step
    Vec3 velocity{}; // metres per real second, however slow the game's time runs
};
// waiting: the player cannot take the step (the game's jump is taking him off
// the ground for a strike that goes up): the flight's clock stands.
Flight fly(float seconds, Vec3 feet, bool grounded, bool waiting = false);
// The strike in flight goes up from `feet`: a player on the ground leaves it
// by the game's jump first (WebStrike::lifting).
bool lifting(Vec3 feet);
// The strike in progress ends with no blow (play stopped, the player changed).
void cancelStrike();
// A thug the web pulled to a player in the air has arrived beside him: the
// player hangs there for `seconds`, to land a blow (not on a web that
// carries him).
void hangInAir(float seconds);
// The game's time per real second (game_time.hpp's clock): the mover's steps
// are that much shorter than the real time they take.
float worldScale();
Data data();
Strikes strikes();
// Wire formats: SpidyPunchStart's configuration, and SpidyPunchTest's blow,
// which a probe sends straight to the game's DamageSystem.
struct Config {
    uint32_t magic = 0x53505543, version = 1, bytes = sizeof(Config), pid{};
    uint64_t base{};
};
struct Test {
    uint32_t magic = 0x53505554, version = 1, bytes = sizeof(Test), type = 1;
    uint64_t victim{}, damager{};
    Vec3 point{}, direction{};
    float amount{}, knockbackAmount = -1, impulse = -1;
    int32_t knockback = -1;
    uint32_t hash{}, reserved{};
};
static_assert(sizeof(Config) == 24 && sizeof(Test) == 80);
} // namespace spidy::game_punch
