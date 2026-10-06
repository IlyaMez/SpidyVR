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
