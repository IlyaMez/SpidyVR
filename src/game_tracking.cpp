#include "spidy/game_tracking.hpp"

namespace spidy {
native_rays::Command controllerAimRays(const Input& input, uint64_t serial) {
    native_rays::Command out;
    out.serial = serial;
    out.leaseMs = 100;
    if (input.focused) {
        for (unsigned i = 0; i < 2; ++i) {
            const auto& hand = input.hands[i];
            if (!hand.tracked || !finite(hand.aim.position))
                continue;
            const auto direction = hand.aim.orientation.rotate({0, 0, -1});
            if (!finite(direction) || std::abs(length(direction) - 1) > .001f)
                continue;
            out.rays[out.count++] = {hand.aim.position, 100, direction, i};
        }
    }
    if (!out.count)
        out.leaseMs = 0;
    return out;
}
namespace {
Vec3 horizontal(Vec3 v) {
    v.y = 0;
    return normalized(v);
}
bool lens(EyeFov f) {
    return std::isfinite(f.left) && std::isfinite(f.right) && std::isfinite(f.down) && std::isfinite(f.up) &&
           f.left < 0 && f.right > 0 && f.down < 0 && f.up > 0 && f.left > -1.55f && f.right < 1.55f &&
           f.down > -1.55f && f.up < 1.55f;
}
bool handValid(const TrackedHand& h) {
    return h.valid && validTrackedPose(h.aim) && validTrackedPose(h.grip) && std::isfinite(h.trigger) &&
           std::isfinite(h.squeeze) && std::isfinite(h.stickX) && std::isfinite(h.stickY);
}
Mat4 worldPose(Pose pose) {
    const Vec3 x = pose.orientation.rotate({1, 0, 0}), y = pose.orientation.rotate({0, -1, 0}),
               z = pose.orientation.rotate({0, 0, -1}), p = pose.position;
    return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1};
}
} // namespace
Input trackedSwingInput(const XrFrame& f, const Rig& rig) {
    Input input;
    input.focused = f.focused && f.valid && validTrackedPose(f.head);
    if (!input.focused)
        return input;
    input.jump = f.jump;
    input.trackingYaw = rig.yaw;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& hand = f.hands[i];
        if (handValid(hand))
            input.hands[i] = {rig.toWorld(hand.aim), hand.grip.position - f.head.position, true,
                              std::clamp(hand.trigger, 0.f, 1.f), std::clamp(hand.squeeze, 0.f, 1.f)};
    }
    const auto& move = f.hands[0];
    if (handValid(move) && std::hypot(move.stickX, move.stickY) > .2f) {
        const auto forward = horizontal(rig.toWorld(f.head).orientation.rotate({0, 0, -1}));
        input.move = limited(cross(forward, {0, 1, 0}) * move.stickX + forward * move.stickY, 1);
    }
    return input;
}
void GameTrackingRig::reset() {
    *this = {};
}
GameMotionFrame GameTrackingRig::update(const XrFrame& f, Vec3 feet, Vec3 gameForward, bool gameplay) {
    GameMotionFrame out;
    out.swing.focused = false;
    pendingRecenter_ |= f.recentered;
    gameForward = horizontal(gameForward);
    const bool active = gameplay && f.focused && f.valid && validTrackedPose(f.head) && finite(feet) &&
                        length(gameForward) > .9f && std::isfinite(f.seconds) && f.seconds > 0 &&
                        f.seconds <= .1f && f.predictedDisplayTime > lastTime_ &&
                        validTrackedPose(f.eyes[0].pose) && validTrackedPose(f.eyes[1].pose) &&
                        lens(f.eyes[0].fov) && lens(f.eyes[1].fov);
    if (!active) {
        wasActive_ = false;
        return out;
    }
    const bool resumed = !wasActive_;
    if (!initialized_) {
        const auto headForward = horizontal(f.head.orientation.rotate({0, 0, -1}));
        const float trackedYaw = length(headForward) > .9f ? std::atan2(-headForward.x, -headForward.z) : 0;
        rig_.yaw = std::atan2(-gameForward.x, -gameForward.z) - trackedYaw;
        // Stage Y remains physical standing height above the player's feet.
        rig_.origin = feet - Quat::yaw(rig_.yaw).rotate({f.head.position.x, 0, f.head.position.z});
        initialized_ = true;
    } else {
        const Vec3 travel = feet - lastFeet_;
        rig_.origin += travel;
        lastHead_.position += travel;
        if (pendingRecenter_)
            rig_.preserveHead(lastHead_, f.head);
    }
    const float turn = handValid(f.hands[1]) ? f.hands[1].stickX : 0;
    if (resumed)
        snapHeld_ = std::abs(turn) > .3f;
    if (std::abs(turn) > .7f && !snapHeld_) {
        rig_.turn(turn > 0 ? -.5235988f : .5235988f, f.head.position);
        snapHeld_ = true;
    }
    if (std::abs(turn) < .3f)
        snapHeld_ = false;
    out.active = true;
    out.releaseWebs = resumed || pendingRecenter_;
    pendingRecenter_ = false;
    out.predictedDisplayTime = f.predictedDisplayTime;
    out.anchor = feet;
    out.swing = trackedSwingInput(f, rig_);
    if (out.releaseWebs)
        for (auto& hand : out.swing.hands)
            hand.tracked = false;
    const auto head = rig_.toWorld(f.head);
    out.head = worldPose(head);
    out.headPose = head;
    for (unsigned i = 0; i < 2; ++i) {
        out.eyes[i] = worldPose(rig_.toWorld(f.eyes[i].pose));
        out.fovs[i] = f.eyes[i].fov;
        if (handValid(f.hands[i])) {
            // The mesh's fingers point along -Z. A controller's grip pose is
            // angled along its handle; its aim pose supplies the pointing axis.
            Pose hand{f.hands[i].grip.position, f.hands[i].aim.orientation};
            out.hands[i] = rig_.toWorld(hand);
            out.grips[i] = rig_.toWorld(f.hands[i].grip);
        }
    }
    // Existing native input bridge uses these bits for W/A/S/D/Space, the
    // virtual Xbox controller the stick itself. Rotate head-relative movement
    // into the stock camera's horizontal axes.
    const float forward = dot(out.swing.move, gameForward),
                right = dot(out.swing.move, cross(gameForward, {0, 1, 0}));
    out.walkRight = right;
    out.walkForward = forward;
    if (forward > .3f)
        out.nativeKeys |= 1u << 0;
    if (right < -.3f)
        out.nativeKeys |= 1u << 1;
    if (forward < -.3f)
        out.nativeKeys |= 1u << 2;
    if (right > .3f)
        out.nativeKeys |= 1u << 3;
    if (f.jump)
        out.nativeKeys |= 1u << 4;
    lastHead_ = head;
    lastFeet_ = feet;
    lastTime_ = f.predictedDisplayTime;
    wasActive_ = true;
    return out;
}
} // namespace spidy
