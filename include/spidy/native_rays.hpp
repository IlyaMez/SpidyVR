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
// bodyRays: a bit for each ray (1 << its index) that stands in for the
// player's own body - the head's clearance, the hands' contact, the roof over
// the feet - and so takes the nearest hit his body meets (meets()). The rest
// (the hands' aim for the webs) take the nearest of all.
struct Command {
    uint32_t magic = 0x5352594d, version = 1, bytes = sizeof(Command), count{};
    uint64_t serial{};
    uint32_t leaseMs = 100, bodyRays{};
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
// The player's own body as the game's collision filter sees it (October 24).
// A query's filter word carries its categories (the low 11 bits) and the
// types it meets (bits 12-19), a body's its categories and its type (bits
// 12-15). They collide when they share a category (or either has none) and
// the body's type is among the query's (18060a0, InsomniacCollisionFilter's
// test for a query of a group, as the game's own queries are; world bodies
// have no group). The player's mover queries by its collision profile (its
// MoverManager's +0xd9c, an index into the game's profiles, 32 bytes each
// from [7813120]: the types at +8, the categories at +0xc): profile 3,
// categories 0x004 and types 0xe7 in free roam, measured. Spidy's own rays
// carry the categories 0x410, which the webs' anchors want: a broken glass
// pane (0x10) in a burning office met them as a wall, and the game let the
// player walk through it. So the rays that stand in for his body pass what
// his body passes: what he walks at, stands on, climbs, and where his head is.
struct BodyFilter {
    uint32_t categories{}, types{};
};
inline bool meets(BodyFilter body, uint32_t filter) {
    // The player's profile not read: every hit, as before.
    if (!body.types)
        return true;
    const uint32_t mine = body.categories & 0x7ff, theirs = filter & 0x7ff;
    if (mine && theirs && !(mine & theirs))
        return false;
    return (body.types & (1u << ((filter >> 12) & 0xf)) & 0xff) != 0;
}
// The player whose body the rays stand in for (his actor record, 0 none).
// His MoverManager is looked up once for each record, and his profile read
// from it again every quarter of a second. Any thread.
void setPlayer(uint64_t record);
BodyFilter body();
inline bool valid(const Command& c) {
    if (c.magic != 0x5352594d || c.version != 1 || c.bytes != sizeof(Command) || !c.serial ||
        c.count > capacity || c.leaseMs > 250 || (c.count && !c.leaseMs) || (c.bodyRays >> c.count))
        return false;
    for (unsigned i = 0; i < c.count; ++i) {
        const auto& ray = c.rays[i];
        const float magnitude = length(ray.direction);
        if (!finite(ray.origin) || length(ray.origin) > 100000 || !finite(ray.direction) ||
            magnitude < .999f || magnitude > 1.001f || !std::isfinite(ray.distance) || ray.distance < .01f ||
            ray.distance > 8192)
            return false;
    }
    return true;
}
} // namespace spidy::native_rays
