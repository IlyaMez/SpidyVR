#pragma once
#include <cstdint>

// The game's time, slowed for Spidy's slow motion (slow_motion.hpp).
//
// The game runs its whole world on one time scale, kept by its
// TimeScaleSystem (update 19bb430). Its 23 channels (a dodge, the gadget
// wheel, a finisher, ...) each ask for a scale, and the update blends the
// system's scale (+0x18) toward the one that wins, linearly at that
// channel's rate per real second (1c477e0). It copies the scale into the
// double the game's clock multiplies each frame's real time by (7a7fb90: a
// frame's game time 7a7fbd8 is its real time 7a7fbf0 times it). With its
// flag +0x7d0 bit 0 it then blends a physics scale (+0x1c) after it and sets
// Havok's step from that (1822670: 609a560 = its base at 609a564 times it).
//
// Spidy's slow motion goes in after each update: the system's scale, the
// clock's double and Havok's step become the smaller of the game's own and
// Spidy's. Before the next update the game's own values go back, so its
// blends, channels and events never see Spidy's.
namespace spidy::game_time {
// The clock's double, which the ray bridge reads for the world's time too
// (native_bodies), and Havok's step: float step, float base, owner.
constexpr uintptr_t clockScaleRva = 0x7a7fb90, gameFrameRva = 0x7a7fbd8, realFrameRva = 0x7a7fbf0;
constexpr uintptr_t physicsStepRva = 0x609a560;
// Hooks the update. 0 when hooked; 9701-9707 when the code is not this
// game's, 9800-9899 when hooking failed.
uint32_t install(uintptr_t base);
// Spidy's time scale from the next update on: 0.05-1 of real time, 1 the
// game's own.
void slow(float scale);
// Puts the game's own time back and unhooks. An update puts it back within
// `waitMs`; without one (a paused game runs none) this does it itself.
void uninstall(unsigned waitMs = 250);
struct Telemetry {
    uint32_t installed{}, physicsScaled{};
    // The scale Spidy asks for; the game's own scale and physics scale in the
    // last update, and what its world ran at (the smaller); Havok's step then
    // and its base (seconds).
    float wanted = 1, own = 1, ownPhysics = 1, world = 1, physicsStep{}, physicsBase{};
    // Updates seen, those Spidy slowed, and changes of the system updated
    // (a level change makes a new one).
    uint64_t updates{}, slowed{}, systems{};
    // The game's own values are in place (nothing of Spidy's to put back).
    bool restored = true;
};
Telemetry telemetry();
} // namespace spidy::game_time
