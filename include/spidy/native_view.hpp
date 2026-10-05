#pragma once
#include "math.hpp"
#include <cstring>

namespace spidy::native_view {
// The renderer's pose uses right/down/forward rows and translation in row 3.
// OpenXR uses right/up/back. This conversion does not change world units.
inline Mat4 relativePose(const Mat4& native, Pose relative) {
    const Vec3 right{native[0], native[1], native[2]};
    const Vec3 up{-native[4], -native[5], -native[6]};
    const Vec3 back{-native[8], -native[9], -native[10]};
    auto world = [&](Vec3 p) { return right * p.x + up * p.y + back * p.z; };
    const Vec3 x = world(relative.orientation.rotate({1, 0, 0}));
    const Vec3 y = -world(relative.orientation.rotate({0, 1, 0}));
    const Vec3 z = -world(relative.orientation.rotate({0, 0, 1}));
    const Vec3 p = Vec3{native[12], native[13], native[14]} + world(relative.position);
    return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1};
}
inline bool validPose(const Mat4& m) {
    for (float f : m)
        if (!std::isfinite(f) || std::abs(f) > 1e6f)
            return false;
    const Vec3 x{m[0], m[1], m[2]}, y{m[4], m[5], m[6]}, z{m[8], m[9], m[10]};
    return std::abs(length(x) - 1) < .01f && std::abs(length(y) - 1) < .01f &&
           std::abs(length(z) - 1) < .01f && std::abs(dot(x, y)) < .01f && std::abs(dot(y, z)) < .01f &&
           std::abs(dot(x, z)) < .01f && dot(cross(x, y), z) > .99f &&
           std::abs(m[3]) + std::abs(m[7]) + std::abs(m[11]) < .01f && std::abs(m[15] - 1) < .01f;
}
// Row-major OpenXR-style view matrix for geometry over the native eye image.
inline Mat4 view(const Mat4& m) {
    const Vec3 right{m[0], m[1], m[2]}, up{-m[4], -m[5], -m[6]}, back{-m[8], -m[9], -m[10]};
    const Vec3 p{m[12], m[13], m[14]};
    return {right.x, right.y, right.z, -dot(right, p), up.x, up.y, up.z, -dot(up, p),
            back.x,  back.y,  back.z,  -dot(back, p),  0,    0,    0,    1};
}
struct Descriptor {
    float values[272]{};
    Mat4 pose() const {
        Mat4 result;
        std::memcpy(result.data(), values, 64);
        return result;
    }
    float nearZ() const {
        return values[0x3f8 / 4];
    }
    float farZ() const {
        return values[0x3fc / 4];
    }
};
static_assert(sizeof(Descriptor) == 0x440);
inline bool valid(const Descriptor& d) {
    if (!validPose(d.pose()) || !std::isfinite(d.nearZ()) || !std::isfinite(d.farZ()) || d.nearZ() <= 0 ||
        d.farZ() <= d.nearZ() || d.farZ() > 1e6f)
        return false;
    for (int i = 0x400 / 4; i <= 0x40c / 4; ++i)
        if (!std::isfinite(d.values[i]) || std::abs(d.values[i]) > 100)
            return false;
    return d.values[0x400 / 4] < d.values[0x404 / 4] && d.values[0x408 / 4] < d.values[0x40c / 4];
}
} // namespace spidy::native_view
