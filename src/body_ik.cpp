#include "spidy/body_ik.hpp"
#include <algorithm>
#include <cmath>
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
        if (length(fingers) < .5f || length(palm) < .5f)
            return false;
        rig.handFingers[i] = normalized(rest.local(a.hand, fingers));
        rig.handPalm[i] = normalized(rest.local(a.hand, palm));
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
    for (const Vec3 v : {rig.pelvisForward, rig.pelvisUp, rig.headForward, rig.headUp})
        if (length(v) < .5f)
            return false;
    rig.ready = true;
    return true;
}

Result solve(Pose& pose, const Rig& rig, const Targets& t, const Config& c, State& state, bool wanted, float dt,
             float scale) {
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
        const float length2 = (rig.upperArm[i] + rig.forearm[i]) * s;
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
        // A fist: every finger joint past the palm bends toward the palm to a
        // set angle from the joint before it, however the game had curled
        // it; the thumb folds in.
        const float fist = std::clamp(std::isfinite(hand.fist) ? hand.fist : 0.f, 0.f, 1.f) * w;
        if (fist <= 0)
            continue;
        const Vec3 palm = normalized(pose.direction(arm.hand, rig.handPalm[i]));
        const auto curl = [&](const std::vector<int16_t>& chain, size_t count, auto angleOf) {
            for (size_t k = 1; k <= count && k + 1 < chain.size(); ++k) {
                const int joint = chain[k];
                const Vec3 before = normalized(pose.position(joint) - pose.position(chain[k - 1]));
                const Vec3 axis = normalized(cross(before, palm));
                if (length(axis) < .5f || length(before) < .5f)
                    continue;
                const Vec3 bent = axisAngle(axis, angleOf(k - 1)).rotate(before);
                pose.turn(rig.below[static_cast<size_t>(joint)],
                          partial(between(pose.position(chain[k + 1]) - pose.position(joint), bent), fist),
                          pose.position(joint));
            }
        };
        for (const auto& finger : arm.fingers)
            curl(finger, 3, [&](size_t k) { return c.fistBend[k]; });
        curl(arm.thumbChain, 2, [&](size_t) { return c.thumbFold; });
        // The thumb wraps across the curled fingers: its tip onto the index
        // finger's middle joint, from its base and its next joint.
        const auto& thumb = arm.thumbChain;
        if (thumb.size() >= 3 && !arm.fingers.empty() && arm.fingers.front().size() >= 3) {
            const Vec3 onto = pose.position(arm.fingers.front()[2]) + palm * .012f;
            for (int pass = 0; pass < 2; ++pass)
                for (size_t k = 0; k + 2 < thumb.size(); ++k) {
                    const Vec3 at = pose.position(thumb[k]);
                    const Vec3 tip = pose.position(thumb.back());
                    pose.turn(rig.below[static_cast<size_t>(thumb[k])], partial(between(tip - at, onto - at), fist),
                              at);
                }
        }
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
