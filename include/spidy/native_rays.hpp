#pragma once
#include "math.hpp"
#include <cstdint>

namespace spidy::native_rays {
constexpr unsigned capacity = 8;
struct Config {
    uint32_t magic = 0x53525943, version = 1, bytes = sizeof(Config), pid{};
    uint64_t base{};
    uint32_t durationMs = 10000, mask = 0x410;
};
struct Ray {
    Vec3 origin{};
    float distance = 100;
    Vec3 direction{};
    uint32_t tag{};
};
struct Command {
    uint32_t magic = 0x5352594d, version = 1, bytes = sizeof(Command), count{};
    uint64_t serial{};
    uint32_t leaseMs = 100, reserved{};
    Ray rays[capacity];
};
struct Hit {
    Ray ray;
    Vec3 position{};
    float fraction = 1;
    Vec3 normal{};
    uint32_t count{};
    uint32_t bodyId = 0xffffffff, filter{}, bodyFlags{}, bodyMatches{};
    uint32_t motionId = 0xffffffff, broadPhaseId = 0xffffffff;
};
struct Data {
    uint32_t magic = 0x53525944, version = 2, bytes = sizeof(Data), status{};
    int64_t sequence{};
    uint64_t serial{}, qpc{}, calls{}, batches{}, world{};
    uint32_t count{}, error{}, thread{}, reserved{};
    Hit hits[capacity];
};
static_assert(sizeof(Config) == 32 && sizeof(Ray) == 32 && sizeof(Command) == 288);
static_assert(sizeof(Hit) == 88 && sizeof(Data) == 784);
inline bool fixedSurface(const Hit& hit) {
    // Reflected hknpBody layout: motionId +0x40, flags +0x44, ID +0x70.
    // Native motion-property updates skip flags bit 1; the captured static
    // buildings use that bit and shared motion 0. Require both plus live identity.
    return hit.count && hit.bodyMatches && (hit.bodyFlags & 3) == 1 && hit.motionId == 0 &&
           hit.broadPhaseId != 0xffffffff;
}
inline bool valid(const Command& c) {
    if (c.magic != 0x5352594d || c.version != 1 || c.bytes != sizeof(Command) || !c.serial ||
        c.count > capacity || c.leaseMs > 250 || (c.count && !c.leaseMs) || c.reserved)
        return false;
    for (unsigned i = 0; i < c.count; ++i) {
        const auto& ray = c.rays[i];
        const float magnitude = length(ray.direction);
        if (!finite(ray.origin) || length(ray.origin) > 100000 || !finite(ray.direction) ||
            magnitude < .999f || magnitude > 1.001f || !std::isfinite(ray.distance) || ray.distance < .01f ||
            ray.distance > 150)
            return false;
    }
    return true;
}
} // namespace spidy::native_rays
