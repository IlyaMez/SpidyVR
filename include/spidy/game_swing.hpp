#pragma once
#include "swing.hpp"
#include "swing_takeoff.hpp"
#include <cstdint>
#include <limits>

namespace spidy::game_swing {
struct Config {
    uint32_t magic = 0x53574346, version = 1, bytes = sizeof(Config), pid{};
    uint64_t base{}, record{}, mover{}, motionModule{};
    uint32_t durationMs = 20000;
    float maxSpeed = 32, gravity = 18;
    uint32_t reserved{};
};
inline SwingConfig physicsConfig(const Config& c) {
    SwingConfig physics;
    physics.maxSpeed = c.maxSpeed;
    physics.gravity = c.gravity;
    physics.reelSpeed = 16;
    physics.airAcceleration = 12;
    physics.maxZipImpulse = 18;
    physics.zipMultiplier = 4.5f;
    return physics;
}
struct Hand {
    Pose aim{};
    Vec3 relative{};
    uint32_t tracked{};
    float trigger{}, grip{};
};
struct Command {
    uint32_t magic = 0x5357434d, version = 2, bytes = sizeof(Command), focused{};
    uint64_t serial{};
    uint32_t leaseMs = 100, jump{};
    Hand hands[2];
    Vec3 move{};
    float trackingYaw{};
    float sampleSeconds = 1.f / 90.f;
    uint32_t reserved{};
    uint64_t sampleTimeNs = 1;
};
struct WebState {
    uint32_t attached{}, bodyId{};
    Vec3 anchor{};
    float length{}, tension{};
};
struct Data {
    uint32_t magic = 0x53574441, version = 3, bytes = sizeof(Data), status{};
    int64_t sequence{};
    uint64_t qpc{}, steps{}, controlled{}, serial{}, attaches{}, releases{}, zips{}, world{}, sourceStep{};
    Vec3 position{}, velocity{}, requested{};
    float dt{};
    uint32_t owned{}, error{};
    WebState webs[2];
    uint32_t grounded{}, collisionFlags{}, takeoff{}, misses{}, obstructed{}, trackingLost{};
    uint32_t takeoffPhase{}, takeoffAttempts{}, takeoffTimeouts{}, nativeContact{};
};
static_assert(sizeof(Config) == 64 && sizeof(Hand) == 52 && sizeof(Command) == 168);
static_assert(sizeof(WebState) == 28 && sizeof(Data) == 240);
inline bool valid(const Command& c) {
    if (c.magic != 0x5357434d || c.version != 2 || c.bytes != sizeof(c) || !c.serial || c.focused > 1 ||
        c.jump > 1 || c.leaseMs > 250 || (c.focused && !c.leaseMs) || !finite(c.move) ||
        length(c.move) > 1.001f || !std::isfinite(c.trackingYaw) || c.reserved ||
        !std::isfinite(c.sampleSeconds) || c.sampleSeconds <= 0 || c.sampleSeconds > .1f ||
        (c.focused && !c.sampleTimeNs))
        return false;
    for (const auto& h : c.hands) {
        const auto q = h.aim.orientation;
        const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (h.tracked > 1 || !finite(h.aim.position) || !finite(h.relative) || !std::isfinite(norm) ||
            norm < .99f || norm > 1.01f || !std::isfinite(h.trigger) || !std::isfinite(h.grip) ||
            h.trigger < 0 || h.trigger > 1 || h.grip < 0 || h.grip > 1)
            return false;
    }
    return true;
}
class InputSampleClock {
  public:
    float consume(const Command& c) {
        if (c.serial == serial_)
            return 0;
        const float seconds =
            time_ ? (c.sampleTimeNs > time_ ? static_cast<float>((c.sampleTimeNs - time_) * 1e-9)
                                            : std::numeric_limits<float>::quiet_NaN())
                  : c.sampleSeconds;
        serial_ = c.serial;
        time_ = c.sampleTimeNs;
        return seconds;
    }
    void reset() {
        serial_ = time_ = 0;
    }

  private:
    uint64_t serial_{}, time_{};
};
inline Input input(const Command& c) {
    Input out;
    out.focused = c.focused != 0;
    out.jump = c.jump != 0;
    out.move = c.move;
    out.trackingYaw = c.trackingYaw;
    for (unsigned i = 0; i < 2; ++i)
        out.hands[i] = {c.hands[i].aim, c.hands[i].relative, c.hands[i].tracked != 0, c.hands[i].trigger,
                        c.hands[i].grip};
    return out;
}
} // namespace spidy::game_swing
