#pragma once
#include "math.hpp"
#include <cstdint>

namespace spidy::native_movement {
struct Config {
    uint32_t magic = 0x534d5643, version = 2, bytes = sizeof(Config), pid{};
    uint64_t base{}, record{}, mover{};
    uint32_t durationMs = 5000;
    float maxSpeed = 2;
    uint32_t syncAirVelocity{}, reserved{};
};
struct Command {
    uint32_t magic = 0x534d564d, version = 1, bytes = sizeof(Command), enabled{};
    uint64_t serial{};
    uint32_t leaseMs{}, reserved{};
    Vec3 velocity{};
    uint32_t reserved2{};
};
struct Data {
    uint32_t magic = 0x534d5644, version = 4, bytes = sizeof(Data), status{};
    int64_t sequence{};
    uint64_t qpc{}, steps{}, controlled{}, serial{}, gravityCorrections{};
    Vec3 position{}, requested{}, achievedVelocity{};
    float dt{};
    uint32_t moverFlags{}, collisionFlags{}, thread{}, error{};
    uint64_t airEvents{}, airOverrides{}, airState{};
    Vec3 airVelocity{};
    float airVertical{}, airGravity{}, airDt{};
    uint32_t grounded{}, contact{};
};
// A movement command governs the first step of the player's mover after it
// arrives, however late that step comes (a paused game, a long frame), and
// every step within its lease. A step without one runs on the game's own
// airborne state, which kept counting the time airborne through Spidy's
// flight: in midair it started at 36-48 m/s down.
inline bool commandApplies(uint64_t nowMs, uint64_t leaseEndMs, uint64_t steps, uint64_t stepsAtCommand) {
    return nowMs < leaseEndMs || steps == stepsAtCommand;
}
// A driven step moves the body by its command's velocity for the step's own
// length, however long the game makes it, and by farthestStep at most: a step
// is shortened, never left to the game. One longer than 50 ms, or one that
// would have moved the body over 2 m, used to run without its command, on
// that same fall; the game's step is its smoothed frame time, so below 20
// frames a second every step was such a step (October 10 headset report:
// three dives out of a swing, each from the first step of 50.4-51.7 ms).
// A command of version 2 with reserved2 1 is a SLINGSHOT SPEED launch over
// 65 m/s (pull request #45): its steps may go as far as boostedStep, and
// the module takes speeds up to maxPlayerSpeed.
constexpr float farthestStep = 2, boostedStep = 8, maxPlayerSpeed = 300;
inline float commandDistance(const Command& c) {
    return c.version == 2 && c.reserved2 == 1 ? boostedStep : farthestStep;
}
inline bool validCommandVersion(const Command& c) {
    return (c.version == 1 && !c.reserved2) || (c.version == 2 && c.reserved2 <= 1);
}
inline bool drivable(float dt) {
    return std::isfinite(dt) && dt > 0;
}
inline Vec3 stepDelta(Vec3 velocity, float dt, float distance=farthestStep) {
    return limited(velocity * dt, distance);
}
// MoverStandard::prequery selects its non-sweeping request at 1fbe575
// when any of these bits are set. They do not describe ground contact.
inline bool collisionEnabled(uint32_t flags) {
    return !(flags & 0x800003u);
}
// Native contact classification: 0 = supported, 1 = sliding, 2 = airborne.
// 1fbd460 returns 0 for valid support and 2 outside its support bounds;
// 1fbeea3 stores that result, accumulating positive support time only for 0.
inline bool groundedContact(uint8_t contact) {
    return contact == 0;
}
// Another actor's MoverStandard (a bot on a web) driven like the player's,
// through the same hooks: a leased velocity, collision by the game. A mover
// without a live command moves as the game moves it.
constexpr unsigned driveSlots = 4;
// options bit 0: drive the mover also while it does not sweep (collision
// flags 0x800003; bots standing about use that mode), so it moves without
// the game's collision and the caller must keep it out of walls itself.
struct Drive {
    uint32_t magic = 0x534d5652, version = 1, bytes = sizeof(Drive), enabled{};
    uint64_t mover{}, record{}, serial{};
    uint32_t leaseMs{}, options{};
    Vec3 velocity{};
    uint32_t reserved2{};
};
// What each driven mover did in its latest step, as Data does for the player.
struct Driven {
    uint64_t mover{}, record{}, steps{}, controlled{}, serial{};
    Vec3 position{}, achievedVelocity{};
    float dt{};
    uint32_t grounded{}, contact{}, moverFlags{}, collisionFlags{}, reserved{};
};
struct DrivenData {
    uint32_t magic = 0x534d5653, version = 1, bytes = sizeof(DrivenData), reserved{};
    int64_t sequence{};
    Driven movers[driveSlots];
};
static_assert(sizeof(Config) == 56 && sizeof(Command) == 48 && sizeof(Data) == 176);
static_assert(sizeof(Drive) == 64 && sizeof(Driven) == 88 && sizeof(DrivenData) == 376);
} // namespace spidy::native_movement
