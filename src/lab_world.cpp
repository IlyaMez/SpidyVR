#include "spidy/lab_world.hpp"
#include <limits>
namespace spidy {
namespace {
struct Intersection {
    float t;
    Vec3 normal;
};
std::optional<Intersection> intersect(Vec3 p, Vec3 d, Vec3 lo, Vec3 hi, float maxT) {
    float enter = -std::numeric_limits<float>::infinity(), exit = maxT;
    Vec3 normal{};
    const float ps[] = {p.x, p.y, p.z}, ds[] = {d.x, d.y, d.z}, ls[] = {lo.x, lo.y, lo.z},
                hs[] = {hi.x, hi.y, hi.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(ds[axis]) < 1e-7f) {
            if (ps[axis] < ls[axis] || ps[axis] > hs[axis])
                return {};
            continue;
        }
        float a = (ls[axis] - ps[axis]) / ds[axis], b = (hs[axis] - ps[axis]) / ds[axis];
        float sign = -1;
        if (a > b) {
            std::swap(a, b);
            sign = 1;
        }
        if (a > enter) {
            enter = a;
            normal = {};
            if (axis == 0)
                normal.x = sign;
            else if (axis == 1)
                normal.y = sign;
            else
                normal.z = sign;
        }
        exit = std::min(exit, b);
        if (enter > exit)
            return {};
    }
    if (enter < 0 || enter > maxT || exit < 0)
        return {};
    return Intersection{enter, normal};
}
} // namespace
LabWorld::LabWorld() {
    boxes_.push_back({{-250, -3, -450}, {250, 0, 150}, {.08f, .11f, .16f}, 1});
    std::uint64_t id = 2;
    for (int row = 0; row < 7; ++row)
        for (int side : {-1, 1}) {
            const float x = static_cast<float>(side) * 27, z = -static_cast<float>(row) * 48;
            const float h = 25 + static_cast<float>((row * 13 + (side + 1) * 7) % 40);
            boxes_.push_back(
                {{x - 10, 0, z - 17}, {x + 10, h, z + 17}, {.15f + row * .025f, .22f, .32f}, id++});
        }
    // Launch rooftop centered under the initial player.
    boxes_.push_back({{-6, 0, 12}, {6, 18, 26}, {.28f, .16f, .19f}, id});
}
std::optional<RayHit> LabWorld::raycast(Vec3 o, Vec3 d, float range) const {
    std::optional<RayHit> out;
    for (const auto& b : boxes_)
        if (auto hit = intersect(o, d, b.min, b.max, range)) {
            range = hit->t;
            out = RayHit{o + d * range, hit->normal, b.id, true};
        }
    return out;
}
MoveResult LabWorld::sweep(Vec3 from, Vec3 to, float radius) const {
    Vec3 p = from, remaining = to - from, lastNormal{};
    bool collided = false;
    const Vec3 r{radius, radius, radius};
    for (int pass = 0; pass < 4 && length(remaining) > 1e-7f; ++pass) {
        std::optional<Intersection> nearest;
        float fraction = 1;
        for (const auto& b : boxes_)
            if (auto hit = intersect(p, remaining, b.min - r, b.max + r, fraction)) {
                fraction = hit->t;
                nearest = hit;
            }
        if (!nearest) {
            p += remaining;
            break;
        }
        collided = true;
        lastNormal = nearest->normal;
        p += remaining * fraction + lastNormal * .0001f;
        remaining = remaining * (1 - fraction);
        const float inward = dot(remaining, lastNormal);
        if (inward < 0)
            remaining -= lastNormal * inward;
    }
    return {p, lastNormal, collided};
}
bool LabWorld::exists(std::uint64_t id) const {
    return std::any_of(boxes_.begin(), boxes_.end(), [id](const Box& b) { return b.id == id; });
}
} // namespace spidy
