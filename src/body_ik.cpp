#include "spidy/body_ik.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace spidy::body {
namespace {
constexpr float pi = 3.14159265358979f;
constexpr Vec3 worldUp{0, 1, 0};
float dot4(Quat a, Quat b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
// The part of v across the unit `axis`.
Vec3 across(Vec3 v, Vec3 axis) {
    return v - axis * dot(v, axis);
}
float wrap(float a) {
    return std::remainder(a, 2 * pi);
}
float approach(float value, float target, float step) {
    return value < target ? std::min(target, value + step) : std::max(target, value - step);
}
// Axes right-handed: x = y cross z.
Quat frame(Vec3 forward, Vec3 upward) {
    const Vec3 f = normalized(forward);
    Vec3 u = across(upward, f);
    if (length(u) < 1e-4f)
        u = across(std::abs(f.y) < .9f ? worldUp : Vec3{1, 0, 0}, f);
    u = normalized(u);
    return fromAxes(cross(u, f), u, f);
}
// The part of `q` that turns about the unit `axis` (swing-twist).
Quat twistAbout(Quat q, Vec3 axis) {
    const float along = q.x * axis.x + q.y * axis.y + q.z * axis.z;
    const Quat twist{axis.x * along, axis.y * along, axis.z * along, q.w};
    return dot4(twist, twist) > 1e-12f ? unit(twist) : Quat{};
}
bool valid(int joint, int count) {
    return joint >= 0 && joint < count;
}
// How far the bone from `joint` to `next` bends from the bone from `before`
// to `joint`, about the unit `hinge` (radians, signed); NaN when either bone
// lies within ten degrees of the hinge.
float bendAbout(const Pose& pose, int before, int joint, int next, Vec3 hinge) {
    const Vec3 a = across(normalized(pose.position(joint) - pose.position(before)), hinge);
    const Vec3 b = across(normalized(pose.position(next) - pose.position(joint)), hinge);
    if (length(a) < .17f || length(b) < .17f)
        return std::numeric_limits<float>::quiet_NaN();
    return std::atan2(dot(cross(a, b), hinge), dot(a, b));
}
// The turns about the unit `hinge` through `pivot` (radians, within half a
// turn) that take `point` to `distance` from `from`: the two that do, or the
// nearest one twice when none does.
std::array<float, 2> turnsToDistance(Vec3 pivot, Vec3 hinge, Vec3 point, Vec3 from, float distance) {
    const Vec3 u = point - pivot, along = hinge * dot(u, hinge), out = u - along, side = cross(hinge, out);
    const Vec3 r = pivot - from;
    // |r + along + out cos t + side sin t| = distance, with |side| = |out|:
    // a cos t + b sin t = k.
    const float a = 2 * dot(r, out), b = 2 * dot(r, side);
    const float k = distance * distance - dot(r + along, r + along) - dot(out, out);
    const float m = std::sqrt(a * a + b * b);
    if (!(m > 1e-12f))
        return {0, 0};
    const float phase = std::atan2(b, a), spread = std::acos(std::clamp(k / m, -1.f, 1.f));
    return {wrap(phase + spread), wrap(phase - spread)};
}
// Two bones from `upper` through `lower` to `end`: `end` onto `target` (or
// toward it, out of reach), bending toward `pole`. The upper bone first
// rolls about itself so the lower one lies in the bend's plane: the middle
// joint then bends about its hinge as the game animates it.
void limb(Pose& pose, const Rig& rig, int upper, int lower, int end, Vec3 target, Vec3 pole, float w) {
    const Vec3 s = pose.position(upper), e = pose.position(lower), h = pose.position(end);
    const float a = length(e - s), b = length(h - e);
    float d = length(target - s);
    if (a < 1e-4f || b < 1e-4f || d < 1e-4f || !finite(target))
        return;
    const Vec3 dir = (target - s) / d;
    d = std::clamp(d, std::abs(a - b) + 1e-3f, (a + b) * .9999f);
    Vec3 bend = across(pole, dir);
    if (length(bend) < 1e-3f)
        bend = across(e - s, dir);
    if (length(bend) < 1e-3f)
        return;
    bend = normalized(bend);
    const float cosine = std::clamp((a * a + d * d - b * b) / (2 * a * d), -1.f, 1.f);
    const Vec3 middle = s + (dir * cosine + bend * std::sqrt(1 - cosine * cosine)) * a;
    const Vec3 reach = s + dir * d;
    pose.turn(rig.below[upper], partial(between(e - s, middle - s), w), s);
    const Vec3 axis = normalized(pose.position(lower) - s);
    const Vec3 have = across(pose.position(end) - pose.position(lower), axis);
    const Vec3 want = across(reach - pose.position(lower), axis);
    if (length(have) > 1e-3f && length(want) > 1e-3f) {
        const Vec3 x = normalized(have), y = normalized(want);
        pose.turn(rig.below[upper], axisAngle(axis, std::atan2(dot(cross(x, y), axis), dot(x, y)) * w), s);
    }
    const Vec3 joint = pose.position(lower);
    pose.turn(rig.below[lower], partial(between(pose.position(end) - joint, reach - joint), w), joint);
}
} // namespace

Quat unit(Quat q) {
    const float n = std::sqrt(dot4(q, q));
    return n > 1e-12f && std::isfinite(n) ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}
Quat axisAngle(Vec3 axis, float radians) {
    const Vec3 a = normalized(axis);
    if (length(a) < .5f || !std::isfinite(radians))
        return {};
    const float s = std::sin(radians / 2);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians / 2)};
}
Quat between(Vec3 a, Vec3 b) {
    a = normalized(a);
    b = normalized(b);
    if (length(a) < .5f || length(b) < .5f)
        return {};
    const float d = dot(a, b);
    if (d < -.999999f) {
        Vec3 axis = cross({1, 0, 0}, a);
        if (length(axis) < .1f)
            axis = cross(worldUp, a);
        axis = normalized(axis);
        return {axis.x, axis.y, axis.z, 0};
    }
    const Vec3 c = cross(a, b);
    return unit({c.x, c.y, c.z, 1 + d});
}
Quat fromAxes(Vec3 x, Vec3 y, Vec3 z) {
    // Columns x, y, z of the rotation matrix.
    const float m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
    const float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0) {
        const float s = std::sqrt(trace + 1) * 2;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, s / 4};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1 + m00 - m11 - m22) * 2;
        q = {s / 4, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1 + m11 - m00 - m22) * 2;
        q = {(m01 + m10) / s, s / 4, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1 + m22 - m00 - m11) * 2;
        q = {(m02 + m20) / s, (m12 + m21) / s, s / 4, (m10 - m01) / s};
    }
    return unit(q);
}
Quat between(Vec3 forward, Vec3 upward, Vec3 toForward, Vec3 toUp) {
    if (length(forward) < 1e-6f || length(toForward) < 1e-6f)
        return {};
    return unit(frame(toForward, toUp) * frame(forward, upward).conjugate());
}
Quat slerp(Quat a, Quat b, float t) {
    float d = dot4(a, b);
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > .9995f)
        return unit({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    const float theta = std::acos(std::min(d, 1.f)), s = std::sin(theta);
    const float wa = std::sin((1 - t) * theta) / s, wb = std::sin(t * theta) / s;
    return unit({a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}
void turnState(State& state, float radians) {
    if (std::isfinite(radians))
        state.yaw = wrap(state.yaw + radians);
}
float angle(Quat q) {
    return 2 * std::acos(std::clamp(std::abs(unit(q).w), 0.f, 1.f));
}

Vec3 Pose::position(int joint) const {
    const float* p = m_ + joint * 16 + 12;
    return {p[0], p[1], p[2]};
}
Vec3 Pose::direction(int joint, Vec3 l) const {
    const float* r = m_ + joint * 16;
    return {l.x * r[0] + l.y * r[4] + l.z * r[8], l.x * r[1] + l.y * r[5] + l.z * r[9],
            l.x * r[2] + l.y * r[6] + l.z * r[10]};
}
Vec3 Pose::local(int joint, Vec3 d) const {
    // Solves l.x * row0 + l.y * row1 + l.z * row2 = d (Cramer's rule).
    const float* r = m_ + joint * 16;
    const Vec3 a{r[0], r[1], r[2]}, b{r[4], r[5], r[6]}, c{r[8], r[9], r[10]};
    const float det = dot(a, cross(b, c));
    if (!(std::abs(det) > 1e-12f))
        return {};
    return {dot(d, cross(b, c)) / det, dot(a, cross(d, c)) / det, dot(a, cross(b, d)) / det};
}
void Pose::turn(std::span<const int16_t> joints, Quat q, Vec3 pivot) {
    for (const auto joint : joints) {
        float* r = m_ + joint * 16;
        for (int row = 0; row < 3; ++row) {
            const Vec3 v = q.rotate({r[row * 4], r[row * 4 + 1], r[row * 4 + 2]});
            r[row * 4] = v.x;
            r[row * 4 + 1] = v.y;
            r[row * 4 + 2] = v.z;
        }
        const Vec3 p = pivot + q.rotate(Vec3{r[12], r[13], r[14]} - pivot);
        r[12] = p.x;
        r[13] = p.y;
        r[14] = p.z;
    }
}
void Pose::move(std::span<const int16_t> joints, Vec3 by) {
    for (const auto joint : joints) {
        float* r = m_ + joint * 16;
        r[12] += by.x;
        r[13] += by.y;
        r[14] += by.z;
    }
}
void Pose::scale(std::span<const int16_t> joints, float s, Vec3 pivot) {
    for (const auto joint : joints) {
        float* r = m_ + joint * 16;
        for (int row = 0; row < 3; ++row)
            for (int i = 0; i < 3; ++i)
                r[row * 4 + i] *= s;
        const Vec3 p = pivot + (Vec3{r[12], r[13], r[14]} - pivot) * s;
        r[12] = p.x;
        r[13] = p.y;
        r[14] = p.z;
    }
}

bool prepare(Rig& rig, std::span<const float> restPose) {
    rig.ready = false;
    const int n = static_cast<int>(rig.parent.size());
    if (n <= 0 || n > 4096 || restPose.size() < static_cast<size_t>(n) * 16)
        return false;
    if (!valid(rig.pelvis, n) || !valid(rig.head, n) || rig.spine.empty())
        return false;
    for (const auto j : rig.spine)
        if (!valid(j, n))
            return false;
    for (const auto j : rig.neck)
        if (!valid(j, n))
            return false;
    for (const auto& a : rig.arms) {
        if (!valid(a.upper, n) || !valid(a.lower, n) || !valid(a.hand, n) || !valid(a.finger, n) ||
            (a.clavicle != -1 && !valid(a.clavicle, n)) || (a.thumb != -1 && !valid(a.thumb, n)))
            return false;
        for (const auto& finger : a.fingers)
            for (const auto j : finger)
                if (!valid(j, n))
                    return false;
        for (const auto j : a.thumbChain)
            if (!valid(j, n))
                return false;
    }
    for (const auto& l : rig.legs)
        if (!valid(l.upper, n) || !valid(l.lower, n) || !valid(l.foot, n))
            return false;
    std::vector<std::vector<int16_t>> children(n);
    for (int j = 0; j < n; ++j)
        if (const int p = rig.parent[j]; valid(p, n) && p != j)
            children[p].push_back(static_cast<int16_t>(j));
    rig.below.assign(n, {});
    for (int j = 0; j < n; ++j) {
        auto& out = rig.below[j];
        out.push_back(static_cast<int16_t>(j));
        for (size_t i = 0; i < out.size(); ++i) {
            for (const auto c : children[out[i]])
                out.push_back(c);
            if (out.size() > static_cast<size_t>(n))
                return false; // the parents make a cycle
        }
    }
    rig.all.resize(n);
    std::iota(rig.all.begin(), rig.all.end(), int16_t{0});
    std::vector<float> copy(restPose.begin(), restPose.begin() + n * 16);
    const Pose rest(copy.data(), n);
    const Vec3 pelvis = rest.position(rig.pelvis), head = rest.position(rig.head);
    // The rest pose stands upright: its left is from the right hip to the left.
    if (normalized(head - pelvis).y < .9f)
        return false;
    const Vec3 up = worldUp;
    const Vec3 left = normalized(across(rest.position(rig.legs[0].upper) - rest.position(rig.legs[1].upper), up));
    const Vec3 forward = cross(left, up);
    if (length(left) < .5f)
        return false;
    rig.pelvisForward = normalized(rest.local(rig.pelvis, forward));
    rig.pelvisUp = normalized(rest.local(rig.pelvis, up));
    rig.headForward = normalized(rest.local(rig.head, forward));
    rig.headUp = normalized(rest.local(rig.head, up));
    for (int i = 0; i < 2; ++i) {
        const auto& a = rig.arms[i];
        const Vec3 hand = rest.position(a.hand);
        const Vec3 fingers = normalized(rest.position(a.finger) - hand);
        // A rest pose holds its palms down; the thumb says otherwise only
        // when it is clearly on another side.
        Vec3 palm = normalized(across(Vec3{0, -1, 0}, fingers));
        if (a.thumb >= 0) {
            const Vec3 thumb = across(rest.position(a.thumb) - hand, fingers);
            // A left hand's palm is fingers cross thumb, a right hand's the reverse.
            const Vec3 byThumb = normalized(i == 0 ? cross(fingers, thumb) : cross(thumb, fingers));
            if (length(palm) < .5f || (length(byThumb) > .5f && dot(byThumb, palm) < .5f))
                palm = byThumb;
        }
        // Two fingers or more say exactly: the palm faces across the line of
        // their knuckles and along their bones in the palm (the hero's rest
        // pose holds its palms 30 degrees from down, toward the thumb).
        Vec3 knuckles{};
        if (a.fingers.size() >= 2 && a.fingers.front().size() >= 2 && a.fingers.back().size() >= 2) {
            Vec3 bones{};
            for (const auto& f : a.fingers)
                if (f.size() >= 2)
                    bones += normalized(rest.position(f[1]) - rest.position(f[0]));
            knuckles = rest.position(a.fingers.back()[1]) - rest.position(a.fingers.front()[1]);
            Vec3 byKnuckles = normalized(across(cross(normalized(knuckles), normalized(bones)), fingers));
            if (dot(byKnuckles, palm) < 0)
                byKnuckles = byKnuckles * -1.f;
            if (dot(byKnuckles, palm) > .5f)
                palm = byKnuckles;
        }
        if (length(fingers) < .5f || length(palm) < .5f)
            return false;
        rig.handFingers[i] = normalized(rest.local(a.hand, fingers));
        rig.handPalm[i] = normalized(rest.local(a.hand, palm));
        // A finger bends across its bone in the palm and the palm's normal.
        auto& hinges = rig.fingerHinges[i];
        hinges.assign(a.fingers.size(), {});
        for (size_t f = 0; f < a.fingers.size(); ++f) {
            const auto& chain = a.fingers[f];
            if (chain.size() < 3)
                continue;
            const Vec3 axis = cross(normalized(rest.position(chain[1]) - rest.position(chain[0])), palm);
            if (length(axis) < .3f)
                continue;
            for (size_t k = 1; k <= 3 && k + 1 < chain.size(); ++k)
                hinges[f][k - 1] = normalized(rest.local(chain[k - 1], normalized(axis)));
        }
        // The thumb across its first bone and the way to the little finger,
        // across the palm (away from the thumb's side when the knuckles do
        // not say).
        rig.thumbHinges[i] = {};
        if (const auto& thumb = a.thumbChain; thumb.size() >= 3) {
            const Vec3 away = length(knuckles) > 1e-4f ? knuckles : hand - rest.position(thumb[0]);
            const Vec3 toward = normalized(across(across(away, fingers), palm));
            const Vec3 axis = cross(normalized(rest.position(thumb[1]) - rest.position(thumb[0])), toward);
            if (length(axis) > .3f)
                for (size_t k = 1; k <= 2 && k + 1 < thumb.size(); ++k)
                    rig.thumbHinges[i][k - 1] = normalized(rest.local(thumb[k - 1], normalized(axis)));
        }
        rig.upperArm[i] = length(rest.position(a.lower) - rest.position(a.upper));
        rig.forearm[i] = length(hand - rest.position(a.lower));
        const auto& l = rig.legs[i];
        rig.thigh[i] = length(rest.position(l.lower) - rest.position(l.upper));
        rig.shin[i] = length(rest.position(l.foot) - rest.position(l.lower));
        if (std::min({rig.upperArm[i], rig.forearm[i], rig.thigh[i], rig.shin[i]}) < .05f)
            return false;
    }
    // Between the eyes: from eye joints when the rig has them, else a typical
    // head (the joint at the top of the neck, the eyes ahead of and above it).
    const Vec3 eyes = valid(rig.eyes[0], n) && valid(rig.eyes[1], n)
                          ? (rest.position(rig.eyes[0]) + rest.position(rig.eyes[1])) / 2
                          : head + forward * .09f + up * .07f;
    const Vec3 offset = eyes - head;
    rig.eyesFromHead = {dot(offset, forward), dot(offset, up), dot(offset, left)};
    rig.eyeHeight = eyes.y;
    for (int i = 0; i < 2; ++i) {
        const Vec3 shoulder = rest.position(rig.arms[i].upper) - eyes;
        rig.shoulders[i] = {dot(shoulder, forward), dot(shoulder, up), dot(shoulder, left)};
    }
    for (const Vec3 v : {rig.pelvisForward, rig.pelvisUp, rig.headForward, rig.headUp})
        if (length(v) < .5f)
            return false;
    rig.ready = true;
    return true;
}

Result solve(Pose& pose, const Rig& rig, const Targets& t, const Config& c, State& state, bool wanted, float dt,
             float scale, float armScale) {
    Result r;
    dt = std::isfinite(dt) ? std::clamp(dt, 0.f, .1f) : 0.f;
    state.weight = approach(state.weight, wanted ? 1.f : 0.f, c.blendSpeed * dt);
    r.weight = state.weight;
    const float w = state.weight;
    if (!rig.ready || !t.head || w <= 0 || pose.count() < static_cast<int>(rig.parent.size()) || !finite(t.eyes))
        return r;
    const Vec3 F = normalized(t.facing.rotate({0, 0, -1})), U = normalized(t.facing.rotate({0, 1, 0}));
    if (length(F) < .5f || length(U) < .5f)
        return r;
    const float s = 1 + (std::clamp(std::isfinite(scale) ? scale : 1.f, .5f, 2.f) - 1) * w;
    const float arms = 1 + (std::clamp(std::isfinite(armScale) ? armScale : 1.f, .5f, 2.f) - 1) * w;
    // The world's up in the model: the hero may crawl on a wall, the body
    // stands as the player does. Yaw turns about it, from the model's forward
    // (or, facing straight up or down a wall, its left).
    Vec3 up = normalized(t.up);
    if (length(up) < .5f)
        up = worldUp;
    const auto level = [&](Vec3 v) { return v - up * dot(v, up); };
    Vec3 reference = level({0, 0, 1});
    if (length(reference) < .3f)
        reference = level({1, 0, 0});
    reference = normalized(reference);
    const Vec3 quarter = cross(up, reference);
    const auto yawOf = [&](Vec3 v) { return std::atan2(dot(v, quarter), dot(v, reference)); };
    // The game's own pose: where its feet stand and which way its hips face.
    const auto& legs = rig.legs;
    const Vec3 feet[2] = {pose.position(legs[0].foot) * s, pose.position(legs[1].foot) * s};
    const Vec3 footAxes[2][2] = {{pose.direction(legs[0].foot, {1, 0, 0}), pose.direction(legs[0].foot, {0, 1, 0})},
                                 {pose.direction(legs[1].foot, {1, 0, 0}), pose.direction(legs[1].foot, {0, 1, 0})}};
    const Vec3 hips = pose.position(rig.pelvis) * s;
    const Vec3 hipsForward = level(pose.direction(rig.pelvis, rig.pelvisForward));
    // Feet stand on the ground only while the hero itself stands upright.
    const float lowest = std::min(dot(feet[0], up), dot(feet[1], up));
    r.grounded = !t.airborne && up.y > .9f && lowest > -.2f && lowest < c.groundedFoot * s;
    // The body is everything under its root; the rig's other roots (effects,
    // IK targets, camera targets) keep the game's pose.
    const auto& bodyJoints = rig.below[static_cast<size_t>(rig.pelvis)];
    if (s != 1)
        pose.scale(bodyJoints, s, {});
    // The player's arms: each arm, hand and all, about its shoulder.
    if (arms != 1)
        for (const auto& arm : rig.arms)
            pose.scale(rig.below[static_cast<size_t>(arm.upper)], arms, pose.position(arm.upper));
    // Which way the body faces: the headset's, once it turns far enough.
    Vec3 look = level(F);
    if (length(look) < .2f)
        look = level(dot(F, up) < 0 ? U : U * -1.f); // straight down, the crown points ahead
    const float headYaw = yawOf(normalized(look));
    if (!state.yawSet) {
        state.yaw = headYaw;
        state.yawSet = true;
    }
    float behind = wrap(headYaw - state.yaw);
    if (const float excess = std::abs(behind) - c.yawDeadZone; excess > 0)
        state.yaw = wrap(state.yaw + std::copysign(std::min(excess, c.yawSpeed * dt), behind));
    behind = wrap(headYaw - state.yaw);
    state.yaw = wrap(state.yaw + behind * std::min(1.f, c.yawDrift * dt));
    r.yaw = state.yaw;
    const Vec3 forward = reference * std::cos(state.yaw) + quarter * std::sin(state.yaw);
    const Vec3 left = cross(up, forward);
    // 1. The hips upright, facing the body's way; everything turns with them.
    pose.turn(bodyJoints,
              partial(between(pose.direction(rig.pelvis, rig.pelvisForward), pose.direction(rig.pelvis, rig.pelvisUp),
                              forward, up),
                      w),
              pose.position(rig.pelvis));
    // 2. The spine bends toward the torso's lean: some of the head's pitch.
    const float lean =
        std::clamp(-std::asin(std::clamp(dot(F, up), -1.f, 1.f)) * c.lean, -c.maxLean, c.maxLean);
    const Vec3 torso = up * std::cos(lean) + forward * std::sin(lean);
    const Quat bend = between(pose.position(rig.head) - pose.position(rig.pelvis), torso);
    const float spineShare = w / static_cast<float>(rig.spine.size());
    for (const auto j : rig.spine)
        pose.turn(rig.below[j], partial(bend, spineShare), pose.position(j));
    // 3. The head takes the headset's orientation, the neck some of the way.
    const auto headTurn = [&] {
        return between(pose.direction(rig.head, rig.headForward), pose.direction(rig.head, rig.headUp), F, U);
    };
    if (!rig.neck.empty()) {
        const Quat total = headTurn();
        const float share = c.neckShare * w / static_cast<float>(rig.neck.size());
        for (const auto j : rig.neck)
            pose.turn(rig.below[j], partial(total, share), pose.position(j));
    }
    pose.turn(rig.below[rig.head], partial(headTurn(), w), pose.position(rig.head));
    // 4. The whole body under the headset: the head joint behind the eyes.
    const Vec3 headTarget =
        t.eyes - (F * rig.eyesFromHead.x + U * rig.eyesFromHead.y + cross(U, F) * rig.eyesFromHead.z) * s;
    pose.move(bodyJoints, (headTarget - pose.position(rig.head)) * w);
    r.headError = length(pose.position(rig.head) - headTarget);
    // 5. Feet the game stands on stay on the ground, turned with the body,
    // and follow the hips once they wander off; the knees bend forward.
    if (r.grounded) {
        float yawTurn = 0;
        if (length(hipsForward) > .2f)
            yawTurn = wrap(state.yaw - yawOf(hipsForward)) * w;
        const Quat turn = axisAngle(up, yawTurn);
        const Vec3 wander = level(pose.position(rig.pelvis) - hips);
        const float far = length(wander);
        const Vec3 carried = far > c.footReach * s ? wander * ((far - c.footReach * s) / far) : Vec3{};
        for (int i = 0; i < 2; ++i) {
            const auto& leg = legs[i];
            // Turned about the up through the hips and carried across it, a
            // foot keeps its height.
            const Vec3 foot = hips + turn.rotate(feet[i] - hips) + carried;
            limb(pose, rig, leg.upper, leg.lower, leg.foot, foot, forward, w);
            const Quat back = between(pose.direction(leg.foot, {1, 0, 0}), pose.direction(leg.foot, {0, 1, 0}),
                                      turn.rotate(footAxes[i][0]), turn.rotate(footAxes[i][1]));
            pose.turn(rig.below[leg.foot], partial(back, w), pose.position(leg.foot));
        }
    }
    // 6. Arms to the controllers: the shoulder lifts toward a far reach, the
    // elbow hangs down, out and a little back, the hand takes the
    // controller's orientation and the forearm shares its roll.
    for (int i = 0; i < 2; ++i) {
        const auto& hand = t.hands[i];
        const auto& arm = rig.arms[i];
        if (!hand.tracked || !finite(hand.grip))
            continue;
        const Vec3 knuckles = normalized(hand.orientation.rotate({0, -1, 0}));
        const Vec3 right = normalized(hand.orientation.rotate({1, 0, 0}));
        const Vec3 offset{i == 0 ? -c.wristFromGrip.x : c.wristFromGrip.x, c.wristFromGrip.y, c.wristFromGrip.z};
        const Vec3 wrist = hand.grip + hand.orientation.rotate(offset);
        const float length2 = (rig.upperArm[i] + rig.forearm[i]) * s * arms;
        if (arm.clavicle >= 0) {
            const Vec3 base = pose.position(arm.clavicle), shoulder = pose.position(arm.upper);
            const float stretch = std::clamp((length(wrist - shoulder) - .8f * length2) / (.4f * length2), 0.f, 1.f);
            if (stretch > 0)
                pose.turn(rig.below[arm.clavicle], partial(between(shoulder - base, wrist - base), .35f * stretch * w),
                          base);
        }
        const Vec3 side = i == 0 ? left : left * -1.f;
        limb(pose, rig, arm.upper, arm.lower, arm.hand, wrist, normalized(up * -1.f + side * .6f - forward * .3f), w);
        r.handError[i] = length(pose.position(arm.hand) - wrist);
        if (c.handOrientation) {
            // The palm faces +x on a left hand, -x on a right one.
            const Quat pitch = axisAngle(right, c.fingerPitch);
            const Vec3 fingers = pitch.rotate(knuckles), palm = pitch.rotate(i == 0 ? right : right * -1.f);
            const auto handTurn = [&] {
                return between(pose.direction(arm.hand, rig.handFingers[i]),
                               pose.direction(arm.hand, rig.handPalm[i]), fingers, palm);
            };
            const Vec3 elbow = pose.position(arm.lower);
            const Vec3 forearm = normalized(pose.position(arm.hand) - elbow);
            pose.turn(rig.below[arm.lower], partial(twistAbout(handTurn(), forearm), c.forearmRoll * w), elbow);
            pose.turn(rig.below[arm.hand], partial(handTurn(), w), pose.position(arm.hand));
        }
        // A fist: every finger joint past the palm turns about its own hinge
        // to a set bend from the bone before it, however the game had bent
        // it. (Bending each bone toward the palm's normal instead turned the
        // last ones backward once a finger curled past it: a claw.)
        const float fist = std::clamp(std::isfinite(hand.fist) ? hand.fist : 0.f, 0.f, 1.f) * w;
        if (fist <= 0)
            continue;
        // Where the joint before `chain[k]` sends `local`: its hinge, and the
        // joint's bend about it now (NaN when there is none).
        const auto hingeAt = [&](const std::vector<int16_t>& chain, size_t k, Vec3 local, Vec3& hinge) {
            hinge = normalized(pose.direction(chain[k - 1], local));
            return length(hinge) > .5f ? bendAbout(pose, chain[k - 1], chain[k], chain[k + 1], hinge)
                                       : std::numeric_limits<float>::quiet_NaN();
        };
        // Turns `chain[k]` and what hangs from it `share` of the way to
        // `angle` from the bone before it.
        const auto bendTo = [&](const std::vector<int16_t>& chain, size_t k, Vec3 local, float angle, float share) {
            Vec3 hinge;
            if (const float now = hingeAt(chain, k, local, hinge); std::isfinite(now))
                pose.turn(rig.below[static_cast<size_t>(chain[k])], axisAngle(hinge, (angle - now) * share),
                          pose.position(chain[k]));
        };
        const auto& hinges = rig.fingerHinges[i];
        for (size_t f = 0; f < arm.fingers.size() && f < hinges.size(); ++f)
            for (size_t k = 1; k <= 3 && k + 1 < arm.fingers[f].size(); ++k)
                bendTo(arm.fingers[f], k, hinges[f][k - 1], c.fistBend[k - 1], fist);
        // The thumb lies across the curled fingers, its tip on the middle
        // bones of the first two, out of the fist: its last joint bends to a
        // set bend, the one before it as far as puts the tip as far from the
        // base as that place, and the base swings the tip there.
        const auto& thumb = arm.thumbChain;
        Vec3 onto{};
        int middles = 0;
        for (size_t f = 0; f < arm.fingers.size() && f < 2; ++f)
            if (const auto& finger = arm.fingers[f]; finger.size() >= 4) {
                onto += (pose.position(finger[2]) + pose.position(finger[3])) / 2;
                ++middles;
            }
        if (thumb.size() < 4 || !middles)
            continue;
        const Vec3 palm = normalized(pose.direction(arm.hand, rig.handPalm[i]));
        onto = onto / static_cast<float>(middles) + palm * (c.thumbRest * s * arms);
        bendTo(thumb, 2, rig.thumbHinges[i][1], c.thumbBend, fist);
        const Vec3 base = pose.position(thumb[0]), knuckle = pose.position(thumb[1]);
        Vec3 hinge;
        if (const float now = hingeAt(thumb, 1, rig.thumbHinges[i][0], hinge); std::isfinite(now)) {
            const Vec3 tip = pose.position(thumb.back());
            const float want = length(onto - base);
            float best = std::clamp(now, 0.f, c.thumbBendMax), miss = std::numeric_limits<float>::infinity();
            for (const float turn : turnsToDistance(knuckle, hinge, tip, base, want)) {
                const float b = std::clamp(wrap(now + turn), 0.f, c.thumbBendMax);
                const float off =
                    std::abs(length(knuckle + axisAngle(hinge, b - now).rotate(tip - knuckle) - base) - want);
                if (off < miss) {
                    miss = off;
                    best = b;
                }
            }
            pose.turn(rig.below[static_cast<size_t>(thumb[1])], axisAngle(hinge, (best - now) * fist), knuckle);
        }
        pose.turn(rig.below[static_cast<size_t>(thumb[0])],
                  partial(between(pose.position(thumb.back()) - base, onto - base), fist), base);
    }
    // 7. The head shrinks to a point at its joint, behind and below the eyes:
    // at once while the body is wanted, so the eyes never see it from inside
    // while the body blends in; gradually as it blends out.
    if (c.hideHead)
        pose.scale(rig.below[rig.head], wanted ? .001f : 1 - .999f * w, pose.position(rig.head));
    r.solved = true;
    return r;
}
} // namespace spidy::body
