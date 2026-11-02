#pragma once
#include "game_tracking.hpp"
#include "math.hpp"
#include <array>
#include <cmath>
#include <cstdint>

// FLIP REPROJECTION (the tab's row under EXPERIMENTAL; off by default): the
// eye pair the headset shows turned by however far the player's own turn has
// gone since it was rendered - a flip, a swing's spin, a smooth turn.
//
// Each pair goes to OpenXR with the tracking pose its eyes were rendered from,
// so the runtime turns it to the newer head, and only the head: the tracking
// space's turn in the world (the rig's yaw, a flip's tilt, the turns onto
// walls and into ducts) never reaches the layer. A pair shown a frame late, or
// shown again while the game has none newer, shows the world where it stood
// when the pair was asked for, and the next pair jumps it on (the user,
// October 29: head turns are smooth, flips and smooth turns judder). With this
// on the layer's poses carry the difference: the world seen from the rendered
// tracking pose, turned into this frame's tracking space about the head.
//
// The overlay is drawn into the same image. Its world-fixed marks (web
// anchors, aim points, bursts) turn with the city, as they should; what the
// player holds - hands, web shooters, the phone, the gadget wheel, the
// calibration panel, the sightline marks - must stay with the head, so it is
// turned the other way in the world before it is drawn (world()).
namespace spidy::flip_reprojection {
// Beyond this the pair is shown as rendered: a snap turn (30 degrees by
// default), a recenter or a placement anew would turn a stale image's edge
// into view rather than steady the world. Flips at 480 degrees a second turn
// about 13 degrees in two 72 Hz frames.
inline constexpr float maxTurn = .5f;
// The tracking space's orientation in the world for one frame: the world's
// head from the tracked head (the orientations alone; the seat's lift and the
// rig's placement are translations).
struct Turn {
    Quat turn{};
    bool valid{};
};
inline Quat unitTurn(Quat q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-6f) || !std::isfinite(n))
        return {};
    q = {q.x / n, q.y / n, q.z / n, q.w / n};
    return q.w < 0 ? Quat{-q.x, -q.y, -q.z, -q.w} : q;
}
inline bool usable(Quat q) {
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return std::isfinite(n) && std::abs(n - 1) < .01f;
}
inline Turn trackingTurn(Quat worldHead, Quat trackedHead) {
    if (!usable(worldHead) || !usable(trackedHead))
        return {};
    return {unitTurn(worldHead * trackedHead.conjugate()), true};
}
// The angle a unit turn turns, radians, 0 to pi.
inline float angle(Quat q) {
    q = unitTurn(q);
    return 2 * std::atan2(std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), q.w);
}
// A pose turned about a point.
inline Pose about(Pose p, Quat turn, Vec3 pivot) {
    return {pivot + turn.rotate(p.position - pivot), unitTurn(turn * p.orientation)};
}
inline Vec3 about(Vec3 p, Quat turn, Vec3 pivot) {
    return pivot + turn.rotate(p - pivot);
}
struct Correction {
    // The layer's turn, in the tracking space: rendered pose to submitted.
    Quat layer{};
    // The overlay's turn in the world for what stays with the head.
    Quat world{};
    float angle{};
    bool applied{};
};
// What a pair rendered with the tracking space at `rendered` needs to be shown
// with it at `now`. Nothing applied when either is unknown, or past maxTurn
// (`angle` still says how far).
inline Correction correction(Turn rendered, Turn now) {
    if (!rendered.valid || !now.valid)
        return {};
    Correction c;
    c.layer = unitTurn(now.turn.conjugate() * rendered.turn);
    c.angle = angle(c.layer);
    if (!std::isfinite(c.angle) || c.angle > maxTurn)
        return {{}, {}, std::isfinite(c.angle) ? c.angle : 0.f, false};
    c.world = unitTurn(now.turn * rendered.turn.conjugate());
    c.applied = true;
    return c;
}
// Both eyes' layer poses turned about the head, the middle of the eyes.
inline std::array<Pose, 2> layerEyes(const std::array<Pose, 2>& eyes, Quat layer) {
    const Vec3 middle = (eyes[0].position + eyes[1].position) * .5f;
    return {about(eyes[0], layer, middle), about(eyes[1], layer, middle)};
}
// Where the rendered frame's eyes stood in the world: their middle, which the
// layer turns about.
inline Vec3 eyesMiddle(const GameMotionFrame& f) {
    return Vec3{f.eyes[0][12] + f.eyes[1][12], f.eyes[0][13] + f.eyes[1][13], f.eyes[0][14] + f.eyes[1][14]} * .5f;
}
// The rendered frame with what the player holds turned by `world` about the
// eyes' middle: the hands, grips, aims, head and gaze, and the tracking space's
// turn the aims are steadied in. The eyes, their lenses and the anchor stay as
// rendered: the overlay's views must match the image.
inline GameMotionFrame held(GameMotionFrame f, Quat world) {
    const Vec3 middle = eyesMiddle(f);
    for (unsigned i = 0; i < 2; ++i) {
        f.hands[i] = about(f.hands[i], world, middle);
        f.grips[i] = about(f.grips[i], world, middle);
        f.swing.hands[i].aim = about(f.swing.hands[i].aim, world, middle);
    }
    f.headPose = about(f.headPose, world, middle);
    if (f.gazeValid)
        f.gazePose = about(f.gazePose, world, middle);
    const Quat yaw = Quat::yaw(f.swing.trackingYaw);
    f.swing.tilt = unitTurn(yaw.conjugate() * world * yaw * f.swing.tilt);
    return f;
}
// SpidyReprojectData: what FLIP REPROJECTION did, for the session report.
struct Data {
    // features: 1 the setting is on.
    uint32_t magic = 0x53525052, version = 1, bytes = sizeof(Data), features{};
    int64_t sequence{};
    // Immersive frames shown: turned (any angle over 0.05 degrees), shown as
    // rendered (no turn to make), passed over (past maxTurn, or a turn not
    // known), and turned while showing a pair again.
    uint64_t turned{}, still{}, passed{}, reusedTurned{};
    // Degrees: the last turn, the largest applied, and the sum of all applied
    // (mean = sum / turned).
    float lastDegrees{}, mostDegrees{};
    double sumDegrees{};
};
static_assert(sizeof(Data) == 72);
} // namespace spidy::flip_reprojection
