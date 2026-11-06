#pragma once
#include "math.hpp"
#include <cstdint>

// The game's own dodge for a player a web carries (spider_sense.hpp's
// WebDodge). Found from disassembly, October 24 (docs/REFERENCE.md has the
// table):
//
// - The hero's HeroDodgeManager (game_sense.hpp) runs his dodges. A state of
//   his that dodges starts one as it starts (92a040: the manager, the kind,
//   an attacker's handle or 0, the dodge's way or none, whether a perfect
//   one slows time) and ends it as it ends (929ef0: the manager, whether his
//   HeroWAMUser hears so at once). Sixteen states do, each with its kind: 4
//   the ground's spline dodge, 5 HeroStateTraversalDodge's (the flat game's
//   dodge on the move), 8 the web rush, 14 the zip to a target. Its kind at
//   +0xcd8 (0: none), when it started at +0xcc0 (the game's clock), its
//   answer at +0xcdc.
// - Started, it answers each warned attack it may (929990, by the kind's
//   flag and the attack's): marks it dodged, makes the hero immune to its
//   damager (1ecabe0 on his damage component; for the dodge config's
//   immunity time, or as long as the dodge lasts), and sends the game's
//   dodge event, its sound and the action bits the Focus meter answers. Its
//   answer: 0 none answered, 1 a plain dodge (no immunity), 2 an accurate
//   one, 3 a perfect one. While its window lasts the manager's update
//   (92d640) answers each warning that goes off too.
// - That update runs once a game frame on the main thread, for the hero's
//   manager among them (926040, the component type's update). Spidy hooks
//   it: its starts and ends go in there, after the game's own.
namespace spidy::game_dodge {
// The kind Spidy's dodges are: the traversal dodge's, the flat game's dodge
// on the move. Its perfect one does not slow time (the start's last
// argument): the swing goes on at its own speed.
constexpr uint32_t webKind = 5;
// SpidyDodgeData, for the headset and the session report.
struct Data {
    // status: 0 off, 1 running, 3 not this build.
    uint32_t magic = 0x53444744, version = 1, bytes = sizeof(Data), status{};
    int64_t sequence{};
    // Frames the hero's manager was updated in; dodges asked for, those the
    // game took (its dodge on after the ask) and its answers to them (by
    // answer: none, plain, accurate, perfect); dodges Spidy ended, and those
    // the game ended first.
    uint64_t frames{}, asked{}, taken{}, answers[4]{}, ended{}, cutShort{};
    // The latest dodge: its answer, the warnings standing as it started, the
    // error a start met, and whether a web carries the player now; the way it
    // went and his speed then (real m/s).
    uint32_t lastAnswer{}, lastWarnings{}, error{}, carried{};
    Vec3 lastWay{};
    float lastSpeed{};
};
static_assert(sizeof(Data) == 128);
// SpidyDodgeLive, for DODGE TUMBLE (dodge_tumble.hpp): the hero's dodge as
// his manager has it each game frame (the hook's, after the game's update):
// its kind now (0 none), the dodges begun so far (the kind from 0 to another;
// written after lastKind, the latest one's kind), whether the one on is
// Spidy's own web dodge, and the frames read. Version 1. A frame of the
// game's in which the swing has not named the hero (carry) is not read.
struct Live {
    uint32_t magic = 0x5344474c, version = 1, bytes = sizeof(Live), kind{};
    uint64_t starts{};
    uint32_t lastKind{}, ours{};
    uint64_t frames{};
};
static_assert(sizeof(Live) == 40);
// A carry sample stands this long without a new one.
constexpr uint32_t leaseMs = 250;
// Checks this game build's dodge manager (9201 another build) and hooks its
// update. The swing starts it with the punches.
uint32_t start(uintptr_t base);
// Ends a dodge of Spidy's at the next update (within a quarter of a second),
// then unhooks.
void stop();
// One input sample of the swing's (any thread): the player's actor record,
// whether a web carries him, whether he is off the ground, his velocity
// (real m/s).
void carry(uint64_t hero, bool webbed, bool airborne, Vec3 velocity);
Data data();
} // namespace spidy::game_dodge
