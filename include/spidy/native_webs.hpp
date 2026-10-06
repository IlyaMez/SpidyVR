#pragma once
#include "math.hpp"
#include <cstdint>

// The game's own web lines. Hero::HeroRopeManager (vtable 38b3df8) owns 16
// RopeManager slots; swing states create type-2 ropes with EnsureRope 677d20
// and aim them with SetRopeTargetPosition 67d7c0. RopeManager::Update 676dd0
// starts each rope at per-hand joint positions; Spidy substitutes the tracked
// wrists there while immersive VR owns the webs.
namespace spidy::native_webs {
struct Hand {
    uint32_t attached{}, tracked{};
    int64_t attachedAt{}; // one attachment; a new value shoots a new web
    Vec3 anchor{}, wrist{};
};
// Wrists are world positions placed from `feet`. The game thread moves them
// with the player to the simulation frame it is updating.
struct Request {
    Vec3 feet{};
    Hand hands[2]{};
};
enum State : uint32_t { off = 0, waiting = 1, active = 2, failed = 3 };
struct Status {
    uint32_t state{}, live{}; // live: bit per hand with a game rope
    uint64_t creates{}, releases{}, failures{}, updates{};
    // Distance from each owned rope's first point (rope +1c) to the tracked
    // start Spidy wrote, last update and maximum, in metres; -1 before any.
    // The game's hand-held tail kept this near 0.45 m until October 5.
    float startError = -1, startErrorMax = -1;
};
uint32_t start(uintptr_t base, uintptr_t record);
// The local player became another actor (a loaded save, a respawn, a
// character switch); zero until there is one again.
void retarget(uintptr_t record);
void submit(const Request&, uint32_t leaseMs);
Status status();
// True while the game draws this hand's web, so the overlay must not.
bool drawing(unsigned hand);
// Hero position the newest hero rope update read, and how many updates have
// read one. Ropes start from it; native eyes place the same frame from it.
bool heroSample(uint64_t& samples, Vec3& position);
// First point of each hand's game rope as built by the update that took hero
// sample `sample`. Returns a bit per hand with a rope; 0 for any other sample.
uint32_t ropeStarts(uint64_t sample, Vec3 starts[2]);
// Tube and end-cone instances of the hero's ropes. They are never avatar.
bool webInstance(uintptr_t instance);
uint32_t stop();
// Wire formats of the SpidyWebsStart and SpidyWebsSubmit exports, which let a
// headless probe drive the game's web lines without the OpenXR worker.
struct ProbeConfig {
    uint32_t magic = 0x53574243, version = 1, bytes = sizeof(ProbeConfig), pid{};
    uint64_t base{}, record{};
};
struct ProbeCommand {
    uint32_t magic = 0x5357424d, version = 1, bytes = sizeof(ProbeCommand), leaseMs{};
    Request request{};
};
static_assert(sizeof(Hand) == 40 && sizeof(Request) == 96 && sizeof(ProbeConfig) == 32 &&
              sizeof(ProbeCommand) == 112);
} // namespace spidy::native_webs
