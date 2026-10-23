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
// Sets every joint under hand `side` back to where the rest pose holds it
// from the hand (Rig::handRest).
void restHand(Pose& pose, const Rig& rig, int side) {
    const int hand = rig.arms[side].hand;
    const Vec3 at = pose.position(hand);
    for (const auto& j : rig.handRest[side]) {
        float* m = pose.data() + j.joint * 16;
        for (int row = 0; row < 3; ++row) {
            const Vec3 axis = pose.direction(hand, j.axes[row]);
            m[row * 4] = axis.x;
            m[row * 4 + 1] = axis.y;
            m[row * 4 + 2] = axis.z;
        }
        const Vec3 p = at + pose.direction(hand, j.at);
        m[12] = p.x;
        m[13] = p.y;
        m[14] = p.z;
    }
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
    rig.hipRoot = rig.pelvis;
    for (int j = rig.parent[rig.legs[0].upper]; valid(j, n);) {
        const auto& under = rig.below[j];
        const auto& body = rig.below[rig.pelvis];
        if (std::find(under.begin(), under.end(), rig.legs[1].upper) != under.end() &&
            std::find(body.begin(), body.end(), j) != body.end()) {
            rig.hipRoot = j;
            break;
        }
        const int parent = rig.parent[j];
        if (parent == j) break;
        j = parent;
    }
    rig.hipForward = normalized(rest.local(rig.hipRoot, forward));
    rig.hipUp = normalized(rest.local(rig.hipRoot, up));
    rig.headForward = normalized(rest.local(rig.head, forward));
    rig.upperChestForward = normalized(rest.local(rig.spine[rig.spine.size() / 2], forward));
    rig.spineForward.clear();
    for (const auto joint : rig.spine)
        rig.spineForward.push_back(normalized(rest.local(joint, forward)));
    rig.neckForward.clear();
    for (const auto joint : rig.neck)
        rig.neckForward.push_back(normalized(rest.local(joint, forward)));
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
        // The hand as it rests, from the hand: where each frame starts it.
        auto& own = rig.handRest[i];
        own.clear();
        if (length(rest.local(a.hand, rest.direction(a.hand, {1, 0, 0})) - Vec3{1, 0, 0}) < 1e-3f)
            for (const auto j : rig.below[a.hand]) {
                if (j == a.hand)
                    continue;
                Rig::HandJoint h{j};
                h.axes[0] = rest.local(a.hand, rest.direction(j, {1, 0, 0}));
                h.axes[1] = rest.local(a.hand, rest.direction(j, {0, 1, 0}));
                h.axes[2] = rest.local(a.hand, rest.direction(j, {0, 0, 1}));
                h.at = rest.local(a.hand, rest.position(j) - hand);
                own.push_back(h);
            }
        // The thumb's side of the palm: against the line of the knuckles, or
        // where the thumb starts from the hand.
        Vec3 thumbSide = length(knuckles) > 1e-4f ? knuckles * -1.f
                         : a.thumb >= 0           ? rest.position(a.thumb) - hand
                                                  : Vec3{};
        thumbSide = across(across(thumbSide, fingers), palm);
        rig.handThumb[i] = length(thumbSide) > 1e-4f ? normalized(rest.local(a.hand, normalized(thumbSide))) : Vec3{};
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
        rig.footForward[i] = normalized(rest.local(l.foot, forward));
        rig.footUp[i] = normalized(rest.local(l.foot, up));
        rig.ankleHeight[i] = std::max(0.f, rest.position(l.foot).y);
        float toe = 0;
        for (const int j : rig.below[l.foot])
            toe = std::max(toe, dot(rest.position(j) - rest.position(l.foot), forward));
        const float legLength = rig.thigh[i] + rig.shin[i];
        rig.forefootLength[i] = std::clamp(toe > .01f ? toe : legLength * .16f,
                                         legLength * .08f, legLength * .25f);
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
    for (const Vec3 v : {rig.pelvisForward, rig.pelvisUp, rig.hipForward, rig.hipUp,
                         rig.headForward, rig.headUp})
        if (length(v) < .5f)
            return false;
    rig.ready = true;
    return true;
}

Result solve(Pose& pose, const Rig& rig, const Targets& targets, const Config& c, State& state, bool wanted,
             float dt, float scale, float armScale) {
    Result r;
    dt = std::isfinite(dt) ? std::clamp(dt, 0.f, .1f) : 0.f;
    state.weight = approach(state.weight, wanted ? 1.f : 0.f, c.blendSpeed * dt);
    r.weight = state.weight;
    const float w = state.weight;
    if (!wanted && w <= 0) state.crawlTwist = 0;
    if (!targets.crawl || !targets.crawlPlanted || !targets.head || !wanted)
        state.crawlFeet.reset();
    if (!rig.ready || !targets.head || w <= 0 || pose.count() < static_cast<int>(rig.parent.size()) ||
        !finite(targets.eyes)) {
        state.crawlTwist = 0;
        state.crawlTurn.reset();
        state.outwardFeet.reset();
        state.crawlOutwardMotion = 0;
        state.outwardAnchor = {};
        state.crawlReach = {};
        return r;
    }
    // In a flip the body is solved for the player turned back level about the
    // eyes, and turned with them at the end (7.).
    Targets t = targets;
    t.surfaceHands = t.surfaceHands && finite(t.surfaceNormal) &&
                     std::abs(length(t.surfaceNormal) - 1) < .01f && finite(t.surfacePoint);
    t.crawl = t.crawl && finite(t.crawlNormal) && finite(t.crawlAhead) && finite(t.crawlStep) &&
        std::abs(length(t.crawlNormal) - 1) < .01f && std::abs(length(t.crawlAhead) - 1) < .01f &&
        std::abs(length(t.crawlStep) - 1) < .01f && finite(t.floor) && std::isfinite(t.crawlPhase) &&
        std::abs(dot(t.crawlNormal, t.crawlAhead)) < .01f &&
        std::abs(dot(t.crawlNormal, t.crawlStep)) < .01f;
    if (!t.crawl || !t.crawlHandSync || !t.crawlTurnAway || !wanted || w < .999f) {
        state.crawlTurn.reset();
        state.outwardFeet.reset();
        state.crawlOutwardMotion = 0;
        state.outwardAnchor = {};
        state.crawlReach = {};
    }
    state.crawlWeight = approach(state.crawlWeight, wanted && t.crawl ? 1.f : 0.f, 6.f * dt);
    const Quat tilt = unit(targets.tilt);
    const bool tilted = angle(tilt) > 1e-4f;
    if (tilted) {
        const Quat back = tilt.conjugate();
        t.facing = back * t.facing;
        if (t.surfaceHands) {
            t.surfaceNormal = back.rotate(t.surfaceNormal);
            t.surfacePoint = t.eyes + back.rotate(t.surfacePoint - t.eyes);
        }
        for (auto& hand : t.hands) {
            hand.grip = t.eyes + back.rotate(hand.grip - t.eyes);
            hand.orientation = back * hand.orientation;
        }
        if (t.crawl) {
            t.floor = t.eyes + back.rotate(t.floor - t.eyes);
            t.crawlNormal = back.rotate(t.crawlNormal);
            t.crawlAhead = back.rotate(t.crawlAhead);
            t.crawlStep = back.rotate(t.crawlStep);
        }
        // Over a few degrees, the feet are nowhere near the ground.
        t.airborne = t.airborne || angle(tilt) > .09f;
    }
    const Vec3 F = normalized(t.facing.rotate({0, 0, -1})), U = normalized(t.facing.rotate({0, 1, 0}));
    if (length(F) < .5f || length(U) < .5f)
        return r;
    const float s = 1 + (std::clamp(std::isfinite(scale) ? scale : 1.f, .5f, 2.f) - 1) * w;
    const float arms = 1 + (std::clamp(std::isfinite(armScale) ? armScale : 1.f, .5f, 2.f) - 1) * w;
    // The world's up in the model: the hero may crawl on a wall, the body
    // stands as the player does. Yaw turns about it, from the model's forward
    // (or, facing straight up or down a wall, its left).
    Vec3 up = normalized(t.crawl ? t.crawlAhead : t.up);
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
    // Feet stand on the ground only while the hero itself stands upright -
    // or on the player's own floor, planted, whichever way the hero is turned.
    const float lowest = std::min(dot(feet[0], up), dot(feet[1], up));
    r.grounded = t.crawl ||
        (!t.airborne && (t.plant || (up.y > .9f && lowest > -.2f && lowest < c.groundedFoot * s)));
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
    if (t.crawl) {
        state.yaw = yawOf(-t.crawlNormal);
        state.yawSet = true;
    } else if (!state.yawSet) {
        state.yaw = headYaw;
        state.yawSet = true;
    }
    float behind = wrap(headYaw - state.yaw);
    if (const float excess = std::abs(behind) - c.yawDeadZone; !t.crawl && excess > 0)
        state.yaw = wrap(state.yaw + std::copysign(std::min(excess, c.yawSpeed * dt), behind));
    behind = wrap(headYaw - state.yaw);
    if (!t.crawl)
        state.yaw = wrap(state.yaw + behind * std::min(1.f, c.yawDrift * dt));
    r.yaw = state.yaw;
    const Vec3 forward = reference * std::cos(state.yaw) + quarter * std::sin(state.yaw);
    const Vec3 left = cross(up, forward);
    const bool turnCrawl = t.crawl && t.crawlHandSync && t.crawlPlanted && t.crawlTurnAway && wanted;
    const float bodyYaw = turnCrawl ? (state.crawlTurn.ready() ? state.crawlTurn.angle() :
        std::abs(behind) > pi * .5f ? std::copysign(pi, behind) : 0.f) : 0.f;
    const float outwardAnchor = turnCrawl ? state.crawlTurn.outwardWeight(bodyYaw) : 0.f;
    // 1. The hips upright, facing the body's way; everything turns with them.
    pose.turn(bodyJoints,
              partial(between(pose.direction(rig.pelvis, rig.pelvisForward), pose.direction(rig.pelvis, rig.pelvisUp),
                              forward, up),
                      w),
              pose.position(rig.pelvis));
    // Remove the game's changing lumbar arch before placing an outward body.
    // Align each real spine segment, not just the root-to-head line: opposite
    // bends can cancel along that line while still pushing the chest out.
    if (t.crawlReachTurn && outwardAnchor > .001f && rig.spineForward.size() == rig.spine.size()) {
        for (size_t i = 0; i < rig.spine.size(); ++i) {
            const int joint = rig.spine[i];
            const int next = i + 1 < rig.spine.size() ? rig.spine[i + 1] :
                             !rig.neck.empty() ? rig.neck.front() : rig.head;
            const Vec3 pivot = pose.position(joint);
            const Quat neutral = between(pose.position(next) - pivot,
                pose.direction(joint, rig.spineForward[i]), up, forward);
            pose.turn(rig.below[joint], partial(neutral, outwardAnchor * w), pivot);
        }
        for (size_t i = 0; i < rig.neck.size(); ++i) {
            const int joint = rig.neck[i], next = i + 1 < rig.neck.size() ? rig.neck[i + 1] : rig.head;
            const Vec3 pivot = pose.position(joint);
            const Quat neutral = between(pose.position(next) - pivot,
                pose.direction(joint, rig.neckForward[i]), up, forward);
            pose.turn(rig.below[joint], partial(neutral, outwardAnchor * w), pivot);
        }
    }
    // 2. The spine bends toward the torso's lean: some of the head's pitch.
    const float lean = t.crawl ? 0.f :
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
    // An outward source neck follows the planted body, not casual headset
    // yaw. Stay just short of the antipodal quaternion: choosing opposite
    // shortest arcs at the backward seam moved hips and both knee bends.
    const float neckYaw = turnCrawl ? wrap(behind + wrap(
        std::clamp(bodyYaw, -pi + .001f, pi - .001f) - behind) * outwardAnchor) : behind;
    const Quat neckUnyaw = turnCrawl ? axisAngle(up, -behind) : Quat{};
    const Quat neckYawTurn = turnCrawl ? axisAngle(up, neckYaw) : Quat{};
    if (!rig.neck.empty()) {
        Quat total = between(pose.direction(rig.head, rig.headForward),
            pose.direction(rig.head, rig.headUp), neckYawTurn.rotate(neckUnyaw.rotate(F)),
            neckYawTurn.rotate(neckUnyaw.rotate(U)));
        if (t.crawlReachTurn && turnCrawl)
            total = slerp(total, Quat{}, outwardAnchor);
        const float share = c.neckShare * w / static_cast<float>(rig.neck.size());
        for (const auto j : rig.neck)
            pose.turn(rig.below[j], partial(total, share), pose.position(j));
    }
    pose.turn(rig.below[rig.head], partial(headTurn(), w), pose.position(rig.head));
    // 4. The whole body under the headset: the head joint behind the eyes.
    // In outdoor hand climbing, place the chest slightly lower relative to
    // the eyes. Moving tracking space would carry both together and leave
    // the chest just as high in view. Use crawl-up, never the neck's pitch;
    // controller targets, the camera and the wall-normal distance stay put.
    const float eyeLift = t.crawl && t.crawlHandSync && std::isfinite(c.crawlEyeLift)
        ? std::clamp(c.crawlEyeLift, 0.f, .2f) * state.crawlWeight : 0.f;
    Vec3 eyeOffset = F * rig.eyesFromHead.x + U * rig.eyesFromHead.y +
                     cross(U, F) * rig.eyesFromHead.z;
    if (turnCrawl && !t.crawlReachTurn) {
        // Outward hips are anchored to the wall-facing frame, not the small
        // eye-to-head offset swinging around as the player glances sideways.
        // Keep physical eye translation and head pitch/roll fully tracked.
        const Vec3 steady = axisAngle(up, bodyYaw - behind).rotate(eyeOffset);
        eyeOffset = eyeOffset * (1 - outwardAnchor) + steady * outwardAnchor;
    }
    const Vec3 headTarget = t.eyes - eyeOffset * s - up * eyeLift;
    pose.move(bodyJoints, (headTarget - pose.position(rig.head)) * w);
    r.headError = length(pose.position(rig.head) - headTarget);
    if (t.crawl) {
        // Anchor a compact pelvis near the wall before solving the legs.
        // A distant camera or a downturned neck used to carry the hips out
        // with the eyes, leaving the feet reachable only with straight legs.
        // Turn about the placed head: the camera and tracked hands stay put.
        const Vec3 n = normalized(t.crawlNormal);
        const int crawlHip = t.crawlHandSync ? rig.hipRoot : rig.pelvis;
        const Vec3 head = pose.position(rig.head), hipsAt = pose.position(crawlHip);
        const Vec3 fromHead = hipsAt - head;
        const float along = dot(fromHead, up), depth = dot(fromHead, n);
        const float span = std::hypot(along, depth);
        if (span > .1f * s) {
            const float clearance = t.crawlHandSync
                ? (std::isfinite(t.crawlDistance) && t.crawlDistance > 0
                    ? std::clamp(t.crawlDistance, .1f, 1.f) + .05f : .51f * s)
                : .32f * s;
            const float minimum = t.crawlHandSync ? -.8f : -.5f;
            const float across = std::clamp((dot(head - t.floor, n) - clearance) / span, minimum, .92f);
            const float tuck = wrap(-std::asin(across) - std::atan2(depth, -along));
            // Tilt only in the crawl-up/normal plane. Neck yaw must not add
            // a sideways body turn while the hips are brought toward the wall.
            pose.turn(bodyJoints, partial(axisAngle(left, tuck), w * state.crawlWeight), head);
            pose.turn(rig.below[rig.head], partial(headTurn(), w), head);
        }
    }
    if (t.crawl && t.crawlHandSync) {
        // Correct the actual shared leg parent, not just the body root.
        // The native pelvis can keep an animated squat beneath a_body even
        // after a_body is upright. Keep the spine outside this correction.
        const Vec3 hipsAt = pose.position(rig.hipRoot);
        const Quat slight = axisAngle(left, .12f);
        const Quat settle = partial(between(pose.direction(rig.hipRoot, rig.hipForward),
            pose.direction(rig.hipRoot, rig.hipUp), slight.rotate(forward), slight.rotate(up)),
            w * state.crawlWeight);
        pose.turn(rig.below[rig.hipRoot], settle, hipsAt);
        if (rig.hipRoot == rig.pelvis)
            pose.turn(rig.below[rig.spine.front()], settle.conjugate(), hipsAt);
    }
    // 5. Feet the game stands on stay on the ground, turned with the body,
    // and follow the hips once they wander off; the knees bend forward.
    if (t.crawl) {
        const float share = w * state.crawlWeight;
        const Vec3 n = normalized(t.crawlNormal);
        // Pitch the toes into the facade while the heel lifts away from it.
        // The ankle clearance below keeps the forefoot sole on the wall.
        const float toePitch = t.crawlHandSync ? .61086524f : 0.f; // 35 degrees
        const float toeCos = std::cos(toePitch), toeSin = std::sin(toePitch);
        const float toeOut = t.crawlHandSync ? .61086524f : 0.f; // 35 degrees outward
        const float outCos = std::cos(toeOut), outSin = std::sin(toeOut);
        const auto sideClearance = [&](int foot) {
            // Include the heel's inward reach and a proportional boot width,
            // plus a small gap. Long boots need more than an ankle-only lane.
            const float heel = (rig.forefootLength[foot] * .5f * toeCos +
                rig.ankleHeight[foot] * toeSin) * outSin;
            const float halfWidth = rig.forefootLength[foot] * .35f * outCos;
            return std::max(.1f, heel + halfWidth + .015f) * s;
        };
        std::array<Vec3, 2> footAhead{}, footNormal{};
        for (int i = 0; i < 2; ++i) {
            const Vec3 tangent = up * outCos + left * (i ? -outSin : outSin);
            footAhead[i] = tangent * toeCos - n * toeSin;
            footNormal[i] = n * toeCos + tangent * toeSin;
        }
        const Vec3 step = normalized(t.crawlStep - n * dot(t.crawlStep, n));
        const float moving = std::isfinite(t.crawlMotion) ? std::clamp(t.crawlMotion, 0.f, 1.f) : 0.f;
        const bool planted = t.crawlPlanted && wanted && finite(t.crawlOrigin);
        std::array<Vec3, 2> contacts{};
        if (planted) {
            // The solve is temporarily unrolled. Store actual world contacts,
            // then undo this mapping for IK; actor yaw and neck movement must
            // not move the planted foot on the facade.
            const Quat appliedTilt = tilted ? partial(tilt, w) : Quat{};
            const Quat modelWorld = unit(t.crawlModelToWorld);
            const auto toWorld = [&](Vec3 p) {
                return t.crawlOrigin + modelWorld.rotate(t.eyes + appliedTilt.rotate(p - t.eyes));
            };
            const auto toSolve = [&](Vec3 p) {
                return t.eyes + appliedTilt.conjugate().rotate(
                    modelWorld.conjugate().rotate(p - t.crawlOrigin) - t.eyes);
            };
            CrawlFeet::Frame f;
            f.origin = t.crawlOrigin;
            f.floor = toWorld(t.floor);
            f.normal = modelWorld.rotate(appliedTilt.rotate(n));
            f.ahead = modelWorld.rotate(appliedTilt.rotate(up));
            f.seconds = dt;
            f.scale = s;
            f.handSync = t.crawlHandSync;
            const Vec3 turnLook = length(level(F)) > .25f ? level(F) : look;
            f.finishStep = t.crawlReachTurn ? state.crawlReach.pending :
                t.crawlTurnAway && t.crawlHandSync && length(turnLook) > .1f &&
                dot(normalized(turnLook), n) > .17364818f;
            f.strokes = t.crawlStrokes;
            for (int i = 0; i < 2; ++i) {
                const Vec3 hip = pose.position(legs[i].upper);
                const float legLength = (rig.thigh[i] + rig.shin[i]) * s;
                const float spread = t.crawlHandSync ? .20f + .10f * moving : .18f + .12f * moving;
                const float down = t.crawlHandSync ? .48f + .06f * moving : .36f + .12f * moving;
                const float ankle = (rig.ankleHeight[i] * toeCos + rig.forefootLength[i] * toeSin) * s;
                contacts[i] = hip - n * dot(hip - t.floor, n) + n * ankle +
                    left * (i ? -1.f : 1.f) * spread * s - up * (down * legLength);
                f.rest[i] = toWorld(contacts[i]);
                f.hips[i] = toWorld(hip);
                f.reach[i] = legLength;
                f.clearance[i] = ankle;
                // Reserve the raised toe/heel outline, not just the ankle
                // joint. This ceiling follows the pelvis in the body frame.
                const float footRise = ((rig.forefootLength[i] * toeCos +
                    rig.ankleHeight[i] * toeSin) * outCos + .025f) * s;
                f.maxUp[i] = dot(pose.position(rig.hipRoot) - hip, up) - footRise;
                f.sideClearance[i] = sideClearance(i);
            }
            if (w < .999f || state.crawlWeight < .999f)
                state.crawlFeet.reset();
            else if (!state.crawlTurn.engaged() && state.crawlFeet.update(f))
                for (int i = 0; i < 2; ++i) contacts[i] = toSolve(state.crawlFeet.feet()[i]);
        }
        for (int i = 0; i < 2; ++i) {
            const auto& leg = legs[i];
            const Vec3 hip = pose.position(leg.upper);
            const float length = (rig.thigh[i] + rig.shin[i]) * s;
            const float phase = t.crawlPhase + (i ? 3.14159265f : 0.f);
            // Stance moves the foot back as the body advances; the opposite
            // half of the cycle lifts it to reach forward for its next step.
            const float lift = std::max(0.f, -std::sin(phase)) * .09f * s;
            // Keep the approved compact resting pose. Travel opens the feet
            // and reaches down the selected body frame, even upside down.
            const Vec3 side = left * (i ? -1.f : 1.f) * (.18f + .16f * moving) * s;
            Vec3 foot = planted ? contacts[i] : hip - n * dot(hip - t.floor, n) +
                n * (rig.ankleHeight[i] * s + lift) + side - up * ((.12f + .3f * moving) * length) +
                step * ((.1f + .06f * moving) * length * std::cos(phase));
            const Vec3 middle = (pose.position(legs[0].upper) + pose.position(legs[1].upper)) * .5f;
            const Vec3 ownSide = left * (i ? -1.f : 1.f);
            const float sideMinimum = sideClearance(i) - dot(hip - middle, ownSide);
            if (planted && t.crawlHandSync && (spidy::length(foot - hip) > length * .9999f ||
                dot(foot - middle, ownSide) < sideClearance(i))) {
                // At a camera distance beyond leg reach, keep the requested
                // hips and tracked head. Let the feet fall short of the wall
                // rather than stretching bones or raising toes past pelvis.
                const float rise = ((rig.forefootLength[i] * toeCos +
                    rig.ankleHeight[i] * toeSin) * outCos + .025f) * s;
                const float ceiling = dot(pose.position(rig.hipRoot) - hip, up) - rise;
                const float limit = length * .999f;
                const Vec3 delta = foot - hip;
                const float reserve = std::max(0.f, sideMinimum);
                const float heightRadius = std::sqrt(std::max(0.f, limit * limit - reserve * reserve));
                const float height = std::clamp(dot(delta, up), -heightRadius,
                    std::clamp(ceiling, -heightRadius, heightRadius));
                const float width = std::sqrt(std::max(0.f, limit * limit - height * height));
                const float lateral = std::max(sideMinimum, std::clamp(dot(delta, ownSide), -width, width));
                const float depthRadius = std::sqrt(std::max(0.f, width * width - lateral * lateral));
                const float depth = std::clamp(dot(delta, n), -depthRadius, depthRadius);
                foot = hip + up * height + ownSide * lateral + n * depth;
            }
            // Knee poles stay below the hips and toward their own side.
            // Planted feet keep the shins folded toward the wall contacts.
            Vec3 pole = planted ? normalized(left * (i ? -.45f : .45f) - up * .55f + n * .75f) :
                normalized(left * (i ? -1.1f : 1.1f) + up * (.55f - .35f * moving) + n * .22f);
            if (planted && t.crawlHandSync) {
                // Keep the same anatomical bend plane throughout a step.
                // Forcing a fixed shin height on a shrinking knee circle
                // used to turn the knee inward as the leg straightened.
                // The actual two-bone lengths determine its height instead.
                const float extension = std::max(
                    std::clamp((spidy::length(foot - hip) / length - .7f) / .25f, 0.f, 1.f),
                    std::clamp((dot(hip - foot, up) / length - .45f) / .17f, 0.f, 1.f));
                const Vec3 kneeSide = left * (i ? -1.f : 1.f);
                const Vec3 axis = normalized(foot - hip);
                const Vec3 outward = normalized(across(kneeSide, axis));
                pole = across(kneeSide - up * (.6f + .9f * extension) - n * .35f, axis);
                const float lateral = dot(pole, outward);
                const Vec3 other = pole - outward * lateral;
                // A sideways contact must not send the bend across the body
                // either. Keep an outward component in the knee circle.
                pole = other + outward * std::max(lateral, spidy::length(other) * .5f);
            }
            if (!planted && moving > 0) {
                // Lower the knee's height above its ankle by 35% while moving.
                // Choose another point on the two-bone reach circle, keeping
                // the same hip/ankle targets, bone lengths and outward bend.
                const Vec3 reach = foot - hip;
                const float distance = spidy::length(reach);
                const float thigh = rig.thigh[i] * s, shin = rig.shin[i] * s;
                if (distance > .001f && distance < thigh + shin) {
                    const Vec3 axis = reach / distance;
                    const float along = (thigh * thigh + distance * distance - shin * shin) / (2 * distance);
                    const float radius = std::sqrt(std::max(0.f, thigh * thigh - along * along));
                    const Vec3 kneeBend = normalized(across(pole, axis));
                    const Vec3 rise = across(up, axis);
                    const float vertical = spidy::length(rise);
                    if (radius * vertical > .001f) {
                        const Vec3 u = rise / vertical;
                        const Vec3 centre = hip + axis * along;
                        const float height = dot(centre + kneeBend * radius - foot, up);
                        const float riseTarget = (height * (1 - .35f * moving) - dot(centre - foot, up)) /
                            (radius * vertical);
                        const float y = std::clamp(riseTarget, -.999f, .999f);
                        const Vec3 outward = normalized(across(kneeBend, u));
                        if (spidy::length(outward) > .5f)
                            pole = u * y + outward * std::sqrt(1 - y * y);
                    }
                }
            }
            limb(pose, rig, leg.upper, leg.lower, leg.foot, foot, pole, share);
            const Quat flat = between(pose.direction(leg.foot, rig.footForward[i]),
                                      pose.direction(leg.foot, rig.footUp[i]), footAhead[i], footNormal[i]);
            pose.turn(rig.below[leg.foot], partial(flat, share), pose.position(leg.foot));
        }
    } else if (r.grounded && t.plant) {
        // Planted: under each hip on the floor, the ankle at its rest height,
        // the foot flat and facing the body's way. The game's pose has nothing
        // to say about these legs.
        for (int i = 0; i < 2; ++i) {
            const auto& leg = legs[i];
            const Vec3 hip = pose.position(leg.upper);
            const float height = dot(hip - t.floor, up), ankle = rig.ankleHeight[i] * s;
            float back = 0;
            if (t.kneel) {
                // Kneeling: the shin back along the floor from a knee under
                // the hip - or, with the hip lower than the thigh is long,
                // from a knee down on the floor behind it. Tall enough to
                // stand, the shin hangs straight under the knee: standing.
                const float thigh = rig.thigh[i] * s, shin = rig.shin[i] * s, knee = c.kneeHeight * s;
                const auto run = [](float side, float rise) {
                    return std::sqrt(std::max(0.f, side * side - rise * rise));
                };
                back = height - knee > thigh ? run(shin, height - thigh - ankle)
                                             : run(thigh, height - knee) + run(shin, knee - ankle);
            }
            const Vec3 foot = hip - up * (height - ankle) - forward * back;
            limb(pose, rig, leg.upper, leg.lower, leg.foot, foot, forward, w);
            // A kneeling foot goes where the shin takes it, behind him.
            if (back > .05f * s)
                continue;
            const Quat flat = between(pose.direction(leg.foot, rig.footForward[i]),
                                      pose.direction(leg.foot, rig.footUp[i]), forward, up);
            pose.turn(rig.below[leg.foot], partial(flat, w), pose.position(leg.foot));
        }
    } else if (r.grounded) {
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
    Quat fullTurn;
    bool rememberShoulders = false;
    Vec3 turnHead{}, turnNormal{}, turnUp{};
    Quat turnRotation;
    if (t.crawl && t.crawlHandSync && t.crawlPlanted && t.crawlTurnAway && wanted &&
        w >= .999f && state.crawlWeight >= .999f && state.crawlFeet.ready()) {
        const Quat appliedTilt = tilted ? partial(tilt, w) : Quat{};
        const Quat modelWorld = unit(t.crawlModelToWorld);
        const auto toWorld = [&](Vec3 p) {
            return t.crawlOrigin + modelWorld.rotate(t.eyes + appliedTilt.rotate(p - t.eyes));
        };
        const auto toSolve = [&](Vec3 p) {
            return t.eyes + appliedTilt.conjugate().rotate(
                modelWorld.conjugate().rotate(p - t.crawlOrigin) - t.eyes);
        };
        CrawlTurn::Frame f;
        f.head = toWorld(pose.position(rig.head)); f.pelvis = toWorld(pose.position(rig.hipRoot));
        f.floor = toWorld(t.floor); f.origin = t.crawlOrigin;
        f.normal = modelWorld.rotate(appliedTilt.rotate(t.crawlNormal));
        f.up = modelWorld.rotate(appliedTilt.rotate(up));
        f.seconds = dt; f.scale = s; f.supports = (t.crawlHands | t.crawlTouches) & 3u;
        // Collision confirms the gesture; only a held grip pins an arm
        // throughout the turn. A resting free palm can pivot or slide.
        if (t.crawlReachTurn) f.supports = t.crawlHands & 3u;
        // Make room behind the pelvis during outward pulls. Keep the
        // resting stance and freeze this blend during a planted half-turn.
        if (!state.crawlTurn.turning()) {
            const float moving = state.crawlTurn.outward() && std::isfinite(t.crawlMotion) &&
                                 t.crawlMotion > .01f ? 1.f : 0.f;
            state.crawlOutwardMotion = approach(state.crawlOutwardMotion, moving, 3.f * dt);
        }
        f.outwardClearance = .12f + .10f * state.crawlOutwardMotion;
        const float remainingLift = std::isfinite(c.crawlOutwardEyeLift) ?
            std::clamp(c.crawlOutwardEyeLift, 0.f, .2f) * state.crawlWeight : eyeLift;
        f.bodyRaise = std::max(0.f, eyeLift - remainingLift);
        // Centre the avatar's turning path between wall clearance and
        // supporting-arm reach. Palm pressure can put the camera farther
        // out, but that must not drag the turning shoulder beyond its hand.
        // Only the avatar adjusts; endpoints and the viewpoint stay put.
        if (t.crawlReachTurn)
            f.turnRoom = std::clamp(.33f * s - dot(f.head - f.floor, f.normal), -.15f, .12f);
        const float down = std::clamp((-dot(F, up) - .2f) / .6f, 0.f, 1.f);
        f.eyeClearance = std::isfinite(c.crawlOutwardEyeClearance) ?
            std::min(.2f, std::clamp(c.crawlOutwardEyeClearance, 0.f, .2f) * std::max(1.f, s)) *
                down * down * (3 - 2 * down) : 0.f;
        // Looking down rotates the avatar's head anchor upward around its
        // eyes. Do not lift attached hips and overstretch the long leg just
        // because the neck pitched. Tracking space itself stays untouched.
        if (std::isfinite(c.crawlOutwardEyeClearance) && c.crawlOutwardEyeClearance > 0) {
            const Vec3 pitchedOffset = F * rig.eyesFromHead.x + U * rig.eyesFromHead.y +
                                       cross(U, F) * rig.eyesFromHead.z;
            f.eyeDrop = std::clamp((rig.eyesFromHead.y - dot(pitchedOffset, up)) * s, 0.f, .2f * s) *
                        std::clamp(c.crawlOutwardEyeClearance / .12f, 0.f, 1.f);
        }
        if (t.crawlReachTurn) {
            // The upper torso now fits the physical neck separately. Do not
            // also slide its old visual anchor when the player nods.
            f.eyeClearance = f.eyeDrop = 0;
        }
        const Vec3 gaze = level(F);
        const Vec3 headingLook = length(gaze) > .25f ? gaze : look;
        const float heading = std::atan2(dot(headingLook, left), dot(headingLook, forward));
        const float facing = std::abs(heading);
        f.entryOutward = !state.crawlTurn.ready() && facing > 1.5707963f;
        f.wanted = state.crawlTurn.targetOutward();
        if (length(headingLook) > .1f)
            f.wanted = facing > (f.wanted ? 1.3962634f : 1.7453293f);
        if (f.entryOutward) f.wanted = true;
        const float remaining = wrap(heading - state.crawlTurn.angle());
        f.direction = std::abs(remaining) > 3.05f && std::abs(state.crawlTwist) > .05f ?
                      state.crawlTwist : remaining;
        constexpr float toeCos = .81915204f, toeSin = .57357644f;
        for (int i = 0; i < 2; ++i) {
            f.hips[i] = toWorld(pose.position(legs[i].upper));
            f.shoulders[i] = toWorld(pose.position(rig.arms[i].upper));
            const Vec3 offset{i ? c.wristFromGrip.x : -c.wristFromGrip.x,
                              c.wristFromGrip.y, c.wristFromGrip.z};
            f.wrists[i] = toWorld(t.hands[i].grip + t.hands[i].orientation.rotate(offset));
            if (!t.hands[i].tracked) f.supports &= ~(1u << i);
            f.reach[i] = (rig.thigh[i] + rig.shin[i]) * s;
            f.armReach[i] = (rig.upperArm[i] + rig.forearm[i]) * s * arms;
            // The final arm solve can also move the shoulder about its
            // clavicle. A rigid-chest prediction omitted that existing
            // travel and rejected otherwise reachable native palm turns.
            if (t.crawlReachTurn && rig.arms[i].clavicle >= 0)
                f.armReach[i] += std::min(.06f * s, .35f * length(
                    pose.position(rig.arms[i].upper) - pose.position(rig.arms[i].clavicle)));
            f.ankle[i] = (rig.ankleHeight[i] * toeCos + rig.forefootLength[i] * toeSin) * s;
            f.lead[i] = (rig.forefootLength[i] * toeCos - rig.ankleHeight[i] * toeSin) * s;
            f.span[i] = std::max(1.5f * rig.forefootLength[i] * toeCos +
                rig.ankleHeight[i] * toeSin + .025f, 1.5f * rig.forefootLength[i] + .025f) * s;
            f.contacts[i] = state.crawlFeet.feet()[i] + state.crawlTurn.toeOffset(f, i, 0);
        }
        if (t.crawlReachTurn) {
            auto& reach = state.crawlReach;
            unsigned tracked = 0;
            for (int i = 0; i < 2; ++i)
                if (t.hands[i].tracked && finite(f.wrists[i])) tracked |= 1u << i;
            const unsigned touches = t.crawlTouches & tracked & 3u;
            const bool otherSide = state.crawlTurn.outward() ? facing < 1.3962634f :
                                                                           facing > 1.7453293f;
            if (reach.pending) {
                reach.age += dt;
                if (!state.crawlTurn.turning() && (state.crawlTurn.outward() == reach.outward ||
                    !otherSide || reach.age > .75f || !(tracked & reach.cue)))
                    reach.pending = false;
            }
            if (state.crawlTurn.turning()) {
                reach.armed = 0;
            } else if (!reach.pending) {
                const unsigned returned = touches & ~reach.previousTouches &
                                          reach.previousTracked & reach.armed;
                if (otherSide && returned) {
                    const int hand = returned & 1u ? 0 : 1;
                    reach.pending = true;
                    reach.outward = !state.crawlTurn.outward();
                    reach.direction = std::abs(reach.approach[hand]) > .1f ?
                                      reach.approach[hand] : f.direction;
                    reach.cue = 1u << hand;
                    reach.age = 0;
                    reach.armed = 0;
                }
                // A fresh contact consumes the reach even if the head has
                // not turned yet; looking later cannot trigger that old tap.
                reach.armed &= ~touches & tracked;
                for (int i = 0; i < 2; ++i) {
                    const unsigned bit = 1u << i, other = 1u << (1 - i);
                    const float depth = dot(f.wrists[i] - f.floor, f.normal);
                    const bool withdrawn = depth > std::max(.08f, reach.contactDepth[i] + .045f);
                    const bool away = (tracked & reach.previousTracked & bit) &&
                                      !(touches & bit) && withdrawn;
                    const bool supported = ((touches | reach.previousTouches | t.crawlHands) & other) != 0;
                    if (away && supported && !(reach.armed & bit) && !reach.pending) {
                        reach.armed |= bit;
                        reach.approach[i] = 0;
                    }
                    // Remember the approach side before the backward-yaw
                    // seam, so the completed turn follows the same shoulder.
                    if ((reach.armed & bit) && std::abs(f.direction) > .2f &&
                        (std::abs(f.direction) < 2.6f || std::abs(reach.approach[i]) < .1f))
                        reach.approach[i] = f.direction;
                    if (touches & bit) reach.contactDepth[i] = depth;
                }
            }
            reach.previousTouches = touches;
            reach.previousTracked = tracked;
            f.wanted = reach.pending ? reach.outward : state.crawlTurn.targetOutward();
            if (reach.pending) f.direction = reach.direction;
            if (f.entryOutward) f.wanted = true;
        }
        // Tracked eyes travel sideways around the neck during a head turn.
        // Keep the outward pelvis on its wall lane instead of following that
        // translation. Real climb travel carries the anchor; roll/corners
        // transport it around the placed head. Camera/controllers stay live.
        auto& anchor = state.outwardAnchor;
        Quat waistSourceCorrection;
        bool waistAdjusted = false;
        if (anchor.set && state.crawlTurn.engaged()) {
            Vec3 carried = anchor.pelvis + f.origin - anchor.origin;
            if (dot(anchor.normal, f.normal) < .99999f || dot(anchor.up, f.up) < .99999f) {
                const Quat corner = arc(anchor.normal, f.normal);
                const Vec3 previousUp = corner.rotate(anchor.up);
                const float roll = std::atan2(dot(cross(previousUp, f.up), f.normal),
                                             dot(previousUp, f.up));
                const Quat transport = Quat::around(f.normal, roll) * corner;
                anchor.waistForward = transport.rotate(anchor.waistForward);
                anchor.waistUp = transport.rotate(anchor.waistUp);
                carried = state.crawlTurn.head(f, state.crawlTurn.angle()) +
                          transport.rotate(anchor.pelvis - anchor.head);
            }
            const Vec3 side = cross(f.normal, f.up);
            const Vec3 current = state.crawlTurn.pelvis(f, state.crawlTurn.angle());
            if (t.crawlReachTurn && anchor.settle > 0 && !state.crawlTurn.turning()) {
                // The eyes are still settling to the back-facing stand-off
                // when the final foot lands. Refresh only the body's height,
                // as a fresh outward entry does after pause/resume. Do this
                // once per handoff, not for later head-only look-around.
                const float error = dot(current - carried, f.up);
                const float take = error * (1 - std::exp(-12.f * dt));
                carried += f.up * std::clamp(take, -.6f * dt, .6f * dt);
                anchor.settle = std::max(0.f, anchor.settle - dt);
            }
            const Vec3 error = carried - current;
            const Vec3 tangent = t.crawlReachTurn ? error - f.normal * dot(error, f.normal) :
                                                  side * dot(error, side);
            const Vec3 shift = tangent * state.crawlTurn.outwardWeight(state.crawlTurn.angle());
            f.head += shift; f.pelvis += shift;
            for (int i = 0; i < 2; ++i) {
                f.hips[i] += shift; f.shoulders[i] += shift;
            }
            if (t.crawlReachTurn && anchor.waistSet) {
                waistAdjusted = true;
                const Quat mapping = modelWorld * appliedTilt;
                const Quat rotation = state.crawlTurn.rotation(f, state.crawlTurn.angle());
                const Vec3 hipForward = rotation.rotate(mapping.rotate(
                    pose.direction(rig.hipRoot, rig.hipForward)));
                const Vec3 hipUp = rotation.rotate(mapping.rotate(pose.direction(rig.hipRoot, rig.hipUp)));
                const Quat correction = partial(between(hipForward, hipUp,
                    anchor.waistForward, anchor.waistUp),
                    state.crawlTurn.outwardWeight(state.crawlTurn.angle()));
                waistSourceCorrection = unit(rotation.conjugate() * correction * rotation);
                for (auto& hip : f.hips) hip = f.pelvis + waistSourceCorrection.rotate(hip - f.pelvis);
            }
        }
        if (t.crawlReachTurn && anchor.shouldersSet && state.crawlTurn.outward() &&
            !state.crawlTurn.turning()) {
            // Predict a return from the shoulders actually fitted beneath
            // the tracked neck, rather than the earlier rigid source chest.
            const Vec3 side = cross(f.normal, f.up);
            for (int i = 0; i < 2; ++i) {
                const Vec3 offset = anchor.shoulderOffsets[i];
                f.shoulders[i] = f.head + f.normal * offset.x + f.up * offset.y + side * offset.z;
            }
        }
        // The outward gait keeps the usual spatial left/right lanes. Swap
        // anatomical indices, not world contacts, after the stepped half-turn.
        const auto gaitFrame = [&](bool outward) {
            CrawlFeet::Frame gait;
            gait.origin = f.origin; gait.floor = f.floor; gait.normal = f.normal; gait.ahead = f.up;
            gait.seconds = dt; gait.scale = s; gait.handSync = true; gait.longAlternation = outward;
            gait.finishStep = outward && !f.wanted;
            const float angle = outward ? std::copysign(3.14159265f, state.crawlTurn.angle()) : 0.f;
            const Vec3 pelvis = state.crawlTurn.pelvis(f, angle);
            for (int lane = 0; lane < 2; ++lane) {
                const int i = outward ? 1 - lane : lane;
                const Vec3 hip = state.crawlTurn.place(f, f.hips[i], angle);
                const Vec3 side = cross(f.normal, f.up) * (lane ? -1.f : 1.f);
                const float clearance = state.crawlTurn.ankleClearance(f, i, angle);
                gait.hips[lane] = hip; gait.reach[lane] = f.reach[i]; gait.clearance[lane] = clearance;
                gait.rest[lane] = hip - f.normal * dot(hip - f.floor, f.normal) +
                    f.normal * clearance + side * (.24f * s) - f.up * (.48f * f.reach[i]);
                const Vec3 footForward = state.crawlTurn.footForward(f, i, angle);
                const Vec3 footNormal = state.crawlTurn.footNormal(f, i, angle);
                const Vec3 toe = state.crawlTurn.toeOffset(f, i, angle);
                const Vec3 heel = footForward * (-.5f * rig.forefootLength[i] * s) -
                                  footNormal * (rig.ankleHeight[i] * s);
                const Vec3 width = cross(footForward, footNormal) * (rig.forefootLength[i] * .35f * s);
                const float rise = std::max(dot(toe, f.up), dot(heel, f.up)) +
                                   std::abs(dot(width, f.up)) + .025f * s;
                gait.maxUp[lane] = dot(pelvis - hip, f.up) - rise;
                gait.sideClearance[lane] = std::max(.1f * s,
                    std::max(std::abs(dot(toe, side)), std::abs(dot(heel, side))) +
                    std::abs(dot(width, side)) + .015f * s);
                if (outward) {
                    gait.sideLimit[lane] = std::max(.23f * s, gait.sideClearance[lane] + .06f * s);
                    gait.rest[lane] = pelvis - f.normal * dot(pelvis - f.floor, f.normal) +
                        f.normal * clearance +
                        side * std::max(.14f * s, gait.sideClearance[lane] + .02f * s) -
                        f.up * (.45f * f.reach[i]);
                }
                gait.strokes[lane] = t.crawlStrokes[i];
            }
            return gait;
        };
        const bool wasEngaged = state.crawlTurn.engaged(), wasOutward = state.crawlTurn.outward();
        const bool wasTurning = state.crawlTurn.turning();
        if (wasOutward && !state.crawlTurn.turning() && state.outwardFeet.ready()) {
            state.outwardFeet.update(gaitFrame(true));
            for (int i = 0; i < 2; ++i)
                f.contacts[i] = state.outwardFeet.feet()[1 - i] +
                    state.crawlTurn.toeOffset(f, i, state.crawlTurn.angle());
        }
        if (!state.crawlTurn.turning()) state.crawlTurn.sync(f, f.contacts);
        f.mayStart = wasOutward ? state.outwardFeet.swinging() < 0 : state.crawlFeet.swinging() < 0;
        state.crawlTurn.update(f);
        if (t.crawlReachTurn && wasTurning && !wasOutward && state.crawlTurn.outward() &&
            !state.crawlTurn.turning())
            anchor.settle = .6f;
        if (state.crawlTurn.outward() || (anchor.set && state.crawlTurn.turning())) {
            anchor.set = true;
            anchor.pelvis = state.crawlTurn.pelvis(f, state.crawlTurn.angle());
            anchor.head = state.crawlTurn.head(f, state.crawlTurn.angle());
            anchor.origin = f.origin; anchor.normal = f.normal; anchor.up = f.up;
            rememberShoulders = t.crawlReachTurn && !state.crawlTurn.turning();
            turnHead = anchor.head; turnNormal = f.normal; turnUp = f.up;
            turnRotation = state.crawlTurn.rotation(f, state.crawlTurn.angle());
        } else {
            anchor = {};
        }
        if (wasEngaged || state.crawlTurn.engaged()) {
            const float angle = state.crawlTurn.angle();
            const Vec3 head = toSolve(state.crawlTurn.head(f, angle));
            pose.move(bodyJoints, head - pose.position(rig.head));
            // Use the same rigid hips/head transform as reach prediction.
            const Quat mapping = modelWorld * appliedTilt;
            fullTurn = unit(mapping.conjugate() * state.crawlTurn.rotation(f, angle) * mapping);
            pose.turn(bodyJoints, fullTurn, head);
            Quat legTurn = fullTurn;
            if (waistAdjusted) {
                const Quat rotation = state.crawlTurn.rotation(f, angle);
                const Quat correction = unit(mapping.conjugate() * rotation * waistSourceCorrection *
                                             rotation.conjugate() * mapping);
                legTurn = unit(correction * fullTurn);
                const Vec3 pivot = pose.position(rig.hipRoot);
                pose.turn(rig.below[rig.hipRoot], correction, pivot);
                if (rig.hipRoot == rig.pelvis)
                    pose.turn(rig.below[rig.spine.front()], correction.conjugate(), pivot);
            }
            if (t.crawlReachTurn && state.crawlTurn.outward() && !state.crawlTurn.turning()) {
                anchor.waistSet = true;
                anchor.waistForward = mapping.rotate(pose.direction(rig.hipRoot, rig.hipForward));
                anchor.waistUp = mapping.rotate(pose.direction(rig.hipRoot, rig.hipUp));
            }
            pose.turn(rig.below[rig.head], partial(headTurn(), w), head);
            std::array<Vec3, 2> ankles;
            for (int i = 0; i < 2; ++i)
                ankles[i] = state.crawlTurn.feet()[i] - state.crawlTurn.toeOffset(f, i, angle);
            if (!state.crawlTurn.turning()) {
                if (state.crawlTurn.outward() && (!wasOutward || !state.outwardFeet.ready()))
                    state.outwardFeet.replant(gaitFrame(true), {ankles[1], ankles[0]});
                if (!state.crawlTurn.outward() && wasEngaged)
                    state.crawlFeet.replant(gaitFrame(false), ankles);
            }
            for (int i = 0; i < 2; ++i) {
                const auto& leg = legs[i];
                const Vec3 foot = toSolve(ankles[i]);
                const Vec3 side = legTurn.rotate(left * (i ? -1.f : 1.f));
                const float outward = state.crawlTurn.outwardWeight(angle);
                const Vec3 pole = normalized(side * (.65f - .15f * outward) -
                    up * (.9f - .45f * outward) + legTurn.rotate(forward) * (.8f + .6f * outward));
                limb(pose, rig, leg.upper, leg.lower, leg.foot, foot, pole, w);
                // Pivot the whole shoe around its sole anchor. Facing away,
                // toes point down/out and the shin solves to that ankle.
                const Vec3 footForward = mapping.conjugate().rotate(state.crawlTurn.footForward(f, i, angle));
                const Vec3 footNormal = mapping.conjugate().rotate(state.crawlTurn.footNormal(f, i, angle));
                pose.turn(rig.below[leg.foot], between(pose.direction(leg.foot, rig.footForward[i]),
                    pose.direction(leg.foot, rig.footUp[i]), footForward, footNormal),
                    pose.position(leg.foot));
            }
        }
    }
    const float nativeOutward = turnCrawl && t.crawlReachTurn ?
        state.crawlTurn.outwardWeight(state.crawlTurn.angle()) : 0.f;
    if (turnCrawl && !rig.neck.empty()) {
        const Vec3 head = pose.position(rig.head);
        const float relativeYaw = wrap(behind - state.crawlTurn.angle()) - neckYaw;
        for (const auto joint : rig.neck) {
            const Vec3 pivot = pose.position(joint);
            pose.turn(rig.below[joint], axisAngle(head - pivot,
                relativeYaw * c.neckShare * w * (1 - nativeOutward) /
                static_cast<float>(rig.neck.size())), pivot);
        }
        pose.turn(rig.below[rig.head], partial(headTurn(), w), head);
    }
    Quat upperTurn = fullTurn;
    // A single sticky palm leaves the upper body free to look/reach around.
    // Twist only spine subtrees, each about the line from its own joint to
    // the placed head. These rigid turns leave both the head position and
    // every bone length intact; the hips/feet are outside these subtrees.
    if (((t.crawl && t.crawlHandSync) || t.surfaceHands) && std::isfinite(c.crawlTorsoTwist)) {
        const Vec3 contactNormal = t.surfaceHands ? t.surfaceNormal : t.crawlNormal;
        const Vec3 contactPoint = t.surfaceHands ? t.surfacePoint : t.floor;
        const unsigned supportMask = (t.crawlHands | t.crawlTouches) & 3u;
        const bool single = supportMask == 1 || supportMask == 2;
        const int support = supportMask == 1 ? 0 : 1, free = 1 - support;
        float target = 0;
        const float maximum = std::clamp(c.crawlTorsoTwist, 0.f, 1.2217305f);
        if (wanted && single && t.hands[support].tracked && finite(t.hands[support].grip)) {
            const Vec3 torsoForward = fullTurn.rotate(forward), torsoLeft = fullTurn.rotate(left);
            const Vec3 reach = across(t.hands[free].grip - pose.position(rig.arms[free].upper),
                                     fullTurn.rotate(up));
            float handYaw = t.hands[free].tracked && finite(reach) && length(reach) > .1f * s
                ? std::atan2(dot(reach, torsoLeft), dot(reach, torsoForward)) : 0.f;
            const Vec3 gaze = across(F, fullTurn.rotate(up));
            float viewYaw = length(gaze) > .25f ?
                std::atan2(dot(gaze, torsoLeft), dot(gaze, torsoForward)) : 0.f;
            // Looking across the back must not flip the shoulders from one
            // limit to the other at the +/-pi seam. Keep the entered side.
            if (std::abs(viewYaw) > 2.6f && std::abs(state.crawlTwist) > .05f)
                viewYaw = std::copysign(std::abs(viewYaw), state.crawlTwist);
            else if (std::abs(viewYaw) > 3.05f && std::abs(handYaw) > .35f)
                viewYaw = std::copysign(std::abs(viewYaw), handYaw);
            if (std::abs(handYaw) > 2.6f)
                handYaw = std::copysign(std::abs(handYaw), std::abs(viewYaw) > .35f ? viewYaw :
                                       std::abs(state.crawlTwist) > .05f ? state.crawlTwist :
                                       free == 0 ? 1.f : -1.f);
            const auto outside = [](float angle, float deadZone) {
                return std::copysign(std::max(0.f, std::abs(angle) - deadZone), angle);
            };
            target = std::clamp(outside(viewYaw, .35f) * .65f + outside(handYaw, .52f) * .35f,
                                -maximum, maximum);
            // When backed against the wall, the neck handles a glance.
            // Keep the chest under it instead of orbiting the shoulders
            // about the old, wall-anchored visual head.
            target *= 1 - nativeOutward;
        }
        state.crawlTwist = approach(state.crawlTwist, target, 2.4f * dt);
        const float share = w * (t.surfaceHands ? 1.f : state.crawlWeight);
        float twist = state.crawlTwist * share;
        const Vec3 head = pose.position(rig.head);
        // Keep the outward belt/lower spine still. Share the look/reach
        // through the upper chest instead; all axes still pass through head.
        const size_t lowerCount = rig.spine.size() / 2;
        const float outward = state.crawlTurn.outwardWeight(state.crawlTurn.angle());
        const float lowerShare = (1 - outward) / static_cast<float>(rig.spine.size());
        const float upperShare = (1 - lowerShare * static_cast<float>(lowerCount)) /
                                 static_cast<float>(rig.spine.size() - lowerCount);
        const auto twistShare = [&](size_t index) { return index < lowerCount ? lowerShare : upperShare; };
        // The composed turn predicts shoulder positions without copying or
        // rewriting the pose. Every constituent axis passes through head.
        const auto turnFor = [&](float angle) {
            Quat turn;
            for (size_t index = 0; index < rig.spine.size(); ++index) {
                const auto joint = rig.spine[index];
                turn = unit(turn * axisAngle(head - pose.position(joint),
                    angle * twistShare(index)));
            }
            return turn;
        };
        const auto reachable = [&](float angle) {
            const Quat turn = turnFor(angle);
            for (int i = 0; i < 2; ++i) {
                const Vec3 shoulder = pose.position(rig.arms[i].upper);
                const Vec3 placed = head + turn.rotate(shoulder - head);
                const float before = dot(shoulder - contactPoint, contactNormal);
                if (dot(placed - contactPoint, contactNormal) < std::min(before, .06f * s) - .001f)
                    return false;
                if (!(supportMask & (1u << i)) || !t.hands[i].tracked) continue;
                const Vec3 offset{i ? c.wristFromGrip.x : -c.wristFromGrip.x,
                                  c.wristFromGrip.y, c.wristFromGrip.z};
                const Vec3 wrist = t.hands[i].grip + t.hands[i].orientation.rotate(offset);
                const float reach = (rig.upperArm[i] + rig.forearm[i]) * s * arms * .995f;
                if (length(wrist - placed) > std::max(reach, length(wrist - shoulder)) + .001f)
                    return false;
            }
            return true;
        };
        if (!reachable(twist)) {
            float low = 0, high = 1;
            for (int step = 0; step < 9; ++step) {
                const float middle = (low + high) * .5f;
                if (reachable(twist * middle)) low = middle; else high = middle;
            }
            twist *= low;
            state.crawlTwist = share > .001f ? twist / share : 0.f;
        }
        if (std::abs(twist) > 1e-5f) {
            upperTurn = turnFor(twist) * fullTurn;
            for (size_t index = 0; index < rig.spine.size(); ++index) {
                const auto joint = rig.spine[index];
                const Vec3 pivot = pose.position(joint);
                pose.turn(rig.below[joint], axisAngle(head - pivot,
                          twist * twistShare(index)), pivot);
            }
            // The neck already shared the original headset turn in (3.).
            // Share the remaining turn now that the chest has followed it,
            // rather than carrying that full neck yaw past the headset.
            if (!rig.neck.empty())
                for (const auto joint : rig.neck) {
                    const Vec3 pivot = pose.position(joint);
                    pose.turn(rig.below[joint], axisAngle(head - pivot,
                        -twist * c.neckShare / static_cast<float>(rig.neck.size())), pivot);
                }
            pose.turn(rig.below[rig.head], partial(headTurn(), w), head);
        }
    } else {
        state.crawlTwist = 0;
    }
    if (nativeOutward > .001f && !rig.neck.empty()) {
        // The lower body is wall-anchored, but the real neck still sits
        // behind the tracked eyes. Fit only the upper spine/neck to that
        // anatomical point, rather than carrying the old low visual head
        // with the frozen pelvis. Rigid subtree turns preserve every bone;
        // the waist, legs, contacts and physical camera are untouched.
        const Vec3 offset = F * rig.eyesFromHead.x + U * rig.eyesFromHead.y +
                            cross(U, F) * rig.eyesFromHead.z;
        // While turning with the head down, the rigid turn path can carry
        // the neck away from the eyes before the final outward fit takes
        // over. Fit that neck throughout the handoff. Keep the established
        // relative eye lift's gradual removal, and leave tracking space live.
        const float down = state.crawlTurn.turning() ?
            std::clamp((-dot(F, up) - .2f) / .4f, 0.f, 1.f) : 0.f;
        const float downTurn = down * down * (3 - 2 * down);
        const float neckFit = nativeOutward + (1 - nativeOutward) * downTurn;
        const Vec3 target = t.eyes - offset * s - up * (eyeLift * (1 - nativeOutward) * downTurn);
        const int upper = rig.spine[rig.spine.size() / 2], neck = rig.neck.front();
        // A full neck fit needs the chest's current turning direction;
        // using its final outward direction would twist the shoulders early.
        const float chestYaw = state.crawlTurn.angle() - std::copysign(pi, state.crawlTurn.angle());
        const Vec3 chestForward = axisAngle(up, chestYaw * downTurn).rotate(normalized(t.crawlNormal));
        const Vec3 root = pose.position(upper), middle = pose.position(neck);
        const float a = length(middle - root), b = length(pose.position(rig.head) - middle);
        const Vec3 delta = target - root;
        if (a > .001f && b > .001f && length(delta) > .001f) {
            const Vec3 dir = normalized(delta);
            const float distance = std::clamp(length(delta), std::abs(a - b) + .001f, (a + b) * .9999f);
            const Vec3 bendDirection = normalized(across(chestForward, dir));
            const float cosine = std::clamp((a * a + distance * distance - b * b) /
                                           (2 * a * distance), -1.f, 1.f);
            const Vec3 next = root + (dir * cosine + bendDirection * std::sqrt(1 - cosine * cosine)) * a;
            // A fixed forward reference also fixes axial chest roll when
            // the neck is almost straight; a bare two-bone solve can spin
            // the shoulders at that singularity.
            const Quat turn = between(middle - root, pose.direction(upper, rig.upperChestForward),
                                      next - root, chestForward);
            pose.turn(rig.below[upper], partial(turn, neckFit * w), root);
            const Vec3 pivot = pose.position(neck), end = root + dir * distance;
            pose.turn(rig.below[neck], partial(between(pose.position(rig.head) - pivot, end - pivot),
                                              neckFit * w), pivot);
        }
        float neckLength = 0;
        for (size_t i = 0; i < rig.neck.size(); ++i)
            neckLength += length(pose.position(i + 1 < rig.neck.size() ? rig.neck[i + 1] : rig.head) -
                                 pose.position(rig.neck[i]));
        const float neckUpAmount = dot(pose.position(rig.head) - pose.position(neck), up);
        float posture = neckLength > .001f ?
            std::clamp((.9f - neckUpAmount / neckLength) / .25f, 0.f, 1.f) : 0.f;
        const float neckError = length(pose.position(rig.head) - target);
        if (neckError > .015f)
            posture = std::max(posture, std::clamp((neckError - .005f) / .02f, 0.f, 1.f));
        if (nativeOutward > .999f || (downTurn > .001f && posture > .001f)) {
            // A lower viewpoint needs a bending upper back, not a neck
            // folding down into a high chest. Fit the individual spine
            // segments beneath an upright neck; the belt joint stays put.
            std::vector<int16_t> joints = rig.spine;
            joints.push_back(static_cast<int16_t>(neck));
            std::vector<Vec3> points;
            std::vector<float> lengths;
            float reach = 0;
            for (const auto joint : joints) points.push_back(pose.position(joint));
            for (size_t i = 1; i < points.size(); ++i) {
                lengths.push_back(length(points[i] - points[i - 1]));
                reach += lengths.back();
            }
            const Vec3 base = points.front();
            Vec3 neckUp = up, goal = target - up * neckLength;
            if (length(goal - base) > reach) {
                neckUp = normalized(target - base);
                goal = target - neckUp * neckLength;
            }
            if (reach > .001f) {
                const Vec3 direction = normalized(goal - base);
                // At full extension, round-off must not switch between a
                // straight chain and a curved FABRIK seed on alternate poses.
                if (length(goal - base) >= reach - .0002f * s) {
                    for (size_t i = 1; i < points.size(); ++i)
                        points[i] = points[i - 1] + direction * lengths[i - 1];
                } else {
                    for (size_t i = 1; i + 1 < points.size(); ++i) {
                        const float fraction = static_cast<float>(i) / (points.size() - 1);
                        points[i] = base + (goal - base) * fraction +
                            chestForward * (-.025f * s * std::sin(pi * fraction));
                    }
                    for (int iteration = 0; iteration < 32; ++iteration) {
                        points.back() = goal;
                        for (size_t i = points.size() - 1; i > 0; --i)
                            points[i - 1] = points[i] +
                                normalized(points[i - 1] - points[i]) * lengths[i - 1];
                        points.front() = base;
                        for (size_t i = 1; i < points.size(); ++i)
                            points[i] = points[i - 1] +
                                normalized(points[i] - points[i - 1]) * lengths[i - 1];
                        if (length(points.back() - goal) < .0002f) break;
                    }
                }
                const float fit = w * (nativeOutward > .999f ? 1.f : downTurn);
                for (size_t i = 0; i + 1 < joints.size(); ++i) {
                    const Vec3 pivot = pose.position(joints[i]);
                    pose.turn(rig.below[joints[i]], partial(between(pose.position(joints[i + 1]) - pivot,
                              points[i + 1] - pivot), fit), pivot);
                }
                for (size_t i = 0; i < rig.neck.size(); ++i) {
                    const int joint = rig.neck[i];
                    const int next = i + 1 < rig.neck.size() ? rig.neck[i + 1] : rig.head;
                    const Vec3 pivot = pose.position(joint);
                    pose.turn(rig.below[joint], partial(between(pose.position(next) - pivot, neckUp),
                                                       fit), pivot);
                }
            }
        }
        r.headError = length(pose.position(rig.head) - target);
        pose.turn(rig.below[rig.head], partial(headTurn(), w), pose.position(rig.head));
    }
    // 6. Arms to the controllers: the shoulder lifts toward a far reach, the
    // elbow hangs down, out and a little back, the hand takes the
    // controller's orientation and the forearm shares its roll. A controller
    // the headset lost leaves its hand where it was last tracked from the
    // body: the arm never goes back to the game's animation.
    const Quat bodyFrame = between({0, 0, 1}, {0, 1, 0}, upperTurn.rotate(forward), upperTurn.rotate(up));
    for (int i = 0; i < 2; ++i) {
        auto hand = t.hands[i];
        const auto& arm = rig.arms[i];
        auto& held = state.held[i];
        if (hand.tracked && finite(hand.grip)) {
            const Quat back = bodyFrame.conjugate();
            held = {true, back.rotate(hand.grip - t.eyes), back * hand.orientation};
        } else if (held.set) {
            hand.grip = t.eyes + bodyFrame.rotate(held.grip);
            hand.orientation = bodyFrame * held.orientation;
        }
        if (held.set) {
            const Vec3 knuckles = normalized(hand.orientation.rotate({0, -1, 0}));
            const Vec3 right = normalized(hand.orientation.rotate({1, 0, 0}));
            const Vec3 offset{i == 0 ? -c.wristFromGrip.x : c.wristFromGrip.x, c.wristFromGrip.y,
                              c.wristFromGrip.z};
            const Vec3 wrist = hand.grip + hand.orientation.rotate(offset);
            const float length2 = (rig.upperArm[i] + rig.forearm[i]) * s * arms;
            if (arm.clavicle >= 0) {
                const Vec3 base = pose.position(arm.clavicle), shoulder = pose.position(arm.upper);
                const float stretch =
                    std::clamp((length(wrist - shoulder) - .8f * length2) / (.4f * length2), 0.f, 1.f);
                if (stretch > 0)
                    pose.turn(rig.below[arm.clavicle],
                              partial(between(shoulder - base, wrist - base), .35f * stretch * w), base);
            }
            const Vec3 side = i == 0 ? left : left * -1.f;
            const Vec3 pole = upperTurn.rotate(normalized(up * -1.f + side * .6f - forward * .3f));
            limb(pose, rig, arm.upper, arm.lower, arm.hand, wrist, pole, w);
            r.handError[i] = length(pose.position(arm.hand) - wrist);
            if (c.handOrientation) {
                // The palm faces +x on a left hand, -x on a right one.
                const Quat pitch = axisAngle(right, c.fingerPitch);
                const Vec3 fingers = pitch.rotate(knuckles);
                const Vec3 palm = pitch.rotate(i == 0 ? right : right * -1.f);
                const auto handTurn = [&] {
                    return between(pose.direction(arm.hand, rig.handFingers[i]),
                                   pose.direction(arm.hand, rig.handPalm[i]), fingers, palm);
                };
                const Vec3 elbow = pose.position(arm.lower);
                const Vec3 forearm = normalized(pose.position(arm.hand) - elbow);
                pose.turn(rig.below[arm.lower], partial(twistAbout(handTurn(), forearm), c.forearmRoll * w),
                          elbow);
                pose.turn(rig.below[arm.hand], partial(handTurn(), w), pose.position(arm.hand));
            }
        }
        // The hand is this body's own: everything under the wrist goes back
        // to where the rest pose holds it from the wrist, and the fingers
        // bend from there - open, into a fist, or shooting a web, each at its
        // own angles. Only bending the joints past the palm left the game's
        // animation in the palm's bones, in each finger's spread and twist,
        // in the thumb, and in the whole hand whenever nothing bent it ("Hand
        // poses for web shooting (and in general) seem to be affected by some
        // animations", October 15). While the body blends out to the game
        // (no longer wanted), the bends fade over the game's own hand.
        const float own = wanted ? 1.f : w;
        if (wanted)
            restHand(pose, rig, i);
        // Every finger joint past the palm turns about its own hinge to a set
        // bend from the bone before it. (Bending each bone toward the palm's
        // normal instead turned the last ones backward once a finger curled
        // past it: a claw.)
        const float fist = std::clamp(std::isfinite(hand.fist) ? hand.fist : 0.f, 0.f, 1.f) * own;
        const float shoot = std::clamp(std::isfinite(hand.shoot) ? hand.shoot : 0.f, 0.f, 1.f) * own;
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
        // A closing finger gathers in: its first bone turns `share` of the
        // way into the plane its knuckle bends in, about the knuckle. The rest
        // pose spreads the fingers (the index 9 degrees one way, the ring and
        // the little finger 12 and 16 the other), and a bend about each one's
        // own hinge kept that spread: closed, their tips stood 4 cm apart where
        // the knuckles are 2.3, a fanned claw ("when your hand closes your
        // hand looks off", October 16). Gathered, the tips are as far apart
        // as the knuckles.
        const auto gather = [&](const std::vector<int16_t>& chain, Vec3 local, float share) {
            if (share <= 0 || chain.size() < 3)
                return;
            const Vec3 hinge = normalized(pose.direction(chain[0], local));
            const Vec3 bone = normalized(pose.position(chain[2]) - pose.position(chain[1]));
            const Vec3 flat = bone - hinge * dot(bone, hinge);
            if (length(hinge) < .5f || length(bone) < .5f || length(flat) < .17f)
                return;
            pose.turn(rig.below[static_cast<size_t>(chain[1])],
                      partial(between(bone, normalized(flat)), share), pose.position(chain[1]));
        };
        const auto& hinges = rig.fingerHinges[i];
        const size_t fingers = std::min(arm.fingers.size(), hinges.size());
        // A finger's joints all bend about its knuckle's hinge, the one axis
        // the rest pose gives all three (Rig::fingerHinges, across its bone in
        // the palm and the palm's normal), so it curls in one plane: the
        // knuckle's, once gathered too. About the hinges the joints past the
        // knuckle carry, a gathered finger curled back out of it.
        const auto bendFinger = [&](size_t f, size_t k, float angle, float share) {
            const auto& chain = arm.fingers[f];
            const Vec3 hinge = normalized(pose.direction(chain[0], hinges[f][0]));
            const float now = length(hinge) > .5f
                                  ? bendAbout(pose, chain[k - 1], chain[k], chain[k + 1], hinge)
                                  : std::numeric_limits<float>::quiet_NaN();
            if (std::isfinite(now))
                pose.turn(rig.below[static_cast<size_t>(chain[k])], axisAngle(hinge, (angle - now) * share),
                          pose.position(chain[k]));
        };
        // Open, holding nothing; a fist or the shooting pose closes over it.
        for (size_t f = 0; f < fingers; ++f)
            for (size_t k = 1; k <= 3 && k + 1 < arm.fingers[f].size(); ++k)
                bendFinger(f, k, c.openBend[k - 1], own);
        if (arm.thumbChain.size() >= 4)
            for (size_t k = 1; k <= 2; ++k)
                bendTo(arm.thumbChain, k, rig.thumbHinges[i][k - 1], c.openThumb, own);
        if (fist > 0)
            for (size_t f = 0; f < fingers; ++f) {
                gather(arm.fingers[f], hinges[f][0], fist);
                for (size_t k = 1; k <= 3 && k + 1 < arm.fingers[f].size(); ++k)
                    bendFinger(f, k, c.fistBend[k - 1], fist);
            }
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
        if (fist > 0 && thumb.size() >= 4 && middles) {
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
        if (shoot <= 0)
            continue;
        // Shooting a web, over whatever the fist left: the fingers between
        // the first and the last press the web shooter's trigger in the palm,
        // those two stay out, and the thumb straightens and points out to its
        // side. All of the way at 1, however the hand was closed.
        for (size_t f = 0; f < fingers; ++f) {
            const bool out = fingers >= 3 && (f == 0 || f + 1 == fingers);
            if (!out)
                gather(arm.fingers[f], hinges[f][0], shoot);
            for (size_t k = 1; k <= 3 && k + 1 < arm.fingers[f].size(); ++k)
                bendFinger(f, k, (out ? c.shootOut : c.shootPress)[k - 1], shoot);
        }
        if (thumb.size() >= 3 && length(rig.handThumb[i]) > .5f) {
            for (size_t k = 1; k <= 2 && k + 1 < thumb.size(); ++k)
                bendTo(thumb, k, rig.thumbHinges[i][k - 1], c.shootThumbBend, shoot);
            const Vec3 base = pose.position(thumb[0]);
            const Vec3 along = normalized(pose.direction(arm.hand, rig.handFingers[i]));
            const Vec3 aside = normalized(pose.direction(arm.hand, rig.handThumb[i]));
            pose.turn(rig.below[static_cast<size_t>(thumb[0])],
                      partial(between(pose.position(thumb.back()) - base,
                                      along * std::cos(c.shootThumb) + aside * std::sin(c.shootThumb)),
                              shoot),
                      base);
        }
    }
    // 7. A flip turns the whole body with the player, about the eyes.
    if (tilted)
        pose.turn(bodyJoints, partial(tilt, w), t.eyes);
    if (rememberShoulders) {
        const Vec3 side = cross(turnNormal, turnUp);
        const Quat modelWorld = unit(t.crawlModelToWorld);
        for (int i = 0; i < 2; ++i) {
            const Vec3 actual = t.crawlOrigin + modelWorld.rotate(pose.position(rig.arms[i].upper));
            const Vec3 offset = turnRotation.conjugate().rotate(actual - turnHead);
            state.outwardAnchor.shoulderOffsets[i] =
                {dot(offset, turnNormal), dot(offset, turnUp), dot(offset, side)};
        }
        state.outwardAnchor.shouldersSet = true;
    }
    // 8. The head shrinks to a point at its joint, behind and below the eyes:
    // at once while the body is wanted, so the eyes never see it from inside
    // while the body blends in; gradually as it blends out.
    if (c.hideHead)
        pose.scale(rig.below[rig.head], wanted ? .001f : 1 - .999f * w, pose.position(rig.head));
    r.solved = true;
    return r;
}
} // namespace spidy::body
