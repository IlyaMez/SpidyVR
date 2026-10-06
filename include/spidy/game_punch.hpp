#pragma once
#include "punch.hpp"
#include "swing.hpp"
#include <cstdint>

// Punching in the game. The fists come from the swing's input samples (the
// aim pose sits at the knuckles; the grip relative to the head, in tracking
// space, is the arm's own motion), the characters from a watch of the
// game's bots (game_targets), and a punch becomes the game's own melee
// damage against the bot it hit (native_bodies::damage): DamageType kMelee,
// as much damage as the fist's speed gave it, the hit reaction the punch's
// strength calls for (punch.hpp's Knockback), the hero as the damager. The
// bot reacts, loses health and is knocked out as the game has it.
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
};
static_assert(sizeof(Hand) == 32 && sizeof(Data) == 160);
// Starts the bot watch and the main-thread damage requests.
uint32_t start(uintptr_t base);
uint32_t stop();
bool running();
// One input sample in the swing's callback (seconds since the previous one,
// 0: the same sample again). busy: a bit per hand whose web holds something.
void update(float seconds, const Input&, uint64_t heroRecord, uint32_t busy, const WorldQueries&);
Data data();
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
