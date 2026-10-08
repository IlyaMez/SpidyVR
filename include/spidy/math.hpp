#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace spidy {
// Meters, seconds, right handed, +Y up, -Z forward, matching OpenXR.
struct Vec3 {
    float x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec3 operator-(Vec3 b) const {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec3 operator-() const {
        return {-x, -y, -z};
    }
    Vec3 operator*(float s) const {
        return {x * s, y * s, z * s};
    }
    Vec3 operator/(float s) const {
        return *this * (1.0f / s);
    }
    Vec3& operator+=(Vec3 b) {
        return *this = *this + b;
    }
    Vec3& operator-=(Vec3 b) {
        return *this = *this - b;
    }
};
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 v) {
    return std::sqrt(dot(v, v));
}
inline Vec3 normalized(Vec3 v) {
    const float n = length(v);
    return n > 1e-6f ? v / n : Vec3{};
}
inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 limited(Vec3 v, float n) {
    const float m = length(v);
    return m > n ? v * (n / m) : v;
}
struct Quat {
    float x{}, y{}, z{}, w{1};
    Quat conjugate() const {
        return {-x, -y, -z, w};
    }
    Quat operator*(Quat b) const {
        return {w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w, w * b.w - x * b.x - y * b.y - z * b.z};
    }
    Vec3 rotate(Vec3 v) const {
        const Vec3 q{x, y, z};
        return v + cross(q, cross(q, v) + v * w) * 2;
    }
    static Quat yaw(float r) {
        return {0, std::sin(r / 2), 0, std::cos(r / 2)};
    }
    // A turn of `r` radians about `axis` (any length but zero; zero: none).
    static Quat around(Vec3 axis, float r) {
        const float n = length(axis);
        if (!(n > 1e-6f))
            return {};
        const float s = std::sin(r / 2) / n;
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(r / 2)};
    }
};
struct Pose {
    Vec3 position{};
    Quat orientation{};
};
inline Pose compose(Pose a, Pose b) {
    return {a.position + a.orientation.rotate(b.position), a.orientation * b.orientation};
}
// The pose that undoes `p` (a unit orientation): compose(p, inverse(p)) is the identity.
inline Pose inverse(Pose p) {
    const Quat back = p.orientation.conjugate();
    return {back.rotate(-p.position), back};
}
// Row-major matrices, multiplied by column vectors. D3D depth range [0,1].
using Mat4 = std::array<float, 16>;
inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 o{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k)
                o[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
    return o;
}
inline Mat4 viewMatrix(Pose p) {
    const auto q = p.orientation.conjugate();
    const Vec3 x = q.rotate({1, 0, 0}), y = q.rotate({0, 1, 0}), z = q.rotate({0, 0, 1}),
               t = q.rotate(-p.position);
    return {x.x, y.x, z.x, t.x, x.y, y.y, z.y, t.y, x.z, y.z, z.z, t.z, 0, 0, 0, 1};
}
inline Mat4 projection(float left, float right, float down, float up, float nearZ = .05f, float farZ = 1500) {
    const float l = std::tan(left), r = std::tan(right), d = std::tan(down), u = std::tan(up);
    return {2 / (r - l),
            0,
            (r + l) / (r - l),
            0,
            0,
            2 / (u - d),
            (u + d) / (u - d),
            0,
            0,
            0,
            -farZ / (farZ - nearZ),
            -farZ * nearZ / (farZ - nearZ),
            0,
            0,
            -1,
            0};
}
// Standing tracking space is translated and yawed with locomotion. Never inherit
// animated body pitch/roll. Physical head translation remains 1:1. Only a flip
// (game_tracking's FlipMotion) tilts it, about `pivot`, a point of the tracking
// space (the head where the flip began); level, `tilt` is the identity.
struct Rig {
    Vec3 origin{};
    float yaw{};
    Quat tilt{};
    Vec3 pivot{};
    // The tracking space's orientation in the world.
    Quat orientation() const {
        return Quat::yaw(yaw) * tilt;
    }
    Pose toWorld(Pose p) const {
        if (tilt.x != 0 || tilt.y != 0 || tilt.z != 0)
            p = {pivot + tilt.rotate(p.position - pivot), tilt * p.orientation};
        return compose({origin, Quat::yaw(yaw)}, p);
    }
    void turn(float radians, Vec3 trackedHead) {
        const Vec3 before = toWorld({trackedHead, {}}).position;
        yaw += radians;
        origin += before - toWorld({trackedHead, {}}).position;
    }
    void preserveHead(Pose previousWorldHead, Pose newTrackedHead) {
        const Vec3 oldForward = previousWorldHead.orientation.rotate({0, 0, -1});
        const Vec3 newForward = newTrackedHead.orientation.rotate({0, 0, -1});
        if (std::hypot(oldForward.x, oldForward.z) > .05f && std::hypot(newForward.x, newForward.z) > .05f)
            yaw = std::atan2(-oldForward.x, -oldForward.z) - std::atan2(-newForward.x, -newForward.z);
        origin = previousWorldHead.position - Quat::yaw(yaw).rotate(newTrackedHead.position);
    }
};
} // namespace spidy
