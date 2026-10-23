#pragma once
#include "math.hpp"

namespace spidy {
// A visual, feet-supported half-turn. The first foot rises on its own side;
// the other drops before crossing; then the raised foot crosses above it.
// Forefoot anchors, rather than ankles, let the supporting shoe pivot.
class CrawlTurn {
  public:
    struct Frame {
        Vec3 head{}, pelvis{}, floor{}, normal{}, up{}, origin{};
        std::array<Vec3, 2> hips{}, shoulders{}, contacts{}, wrists{};
        std::array<float, 2> reach{}, armReach{}, ankle{}, lead{}, span{};
        float seconds{}, scale = 1, direction = 1, eyeClearance{}, eyeDrop{}, outwardClearance = .12f;
        float bodyRaise{}, turnRoom{};
        unsigned supports{};
        bool wanted{}, mayStart{}, entryOutward{};
    };
    float outwardWeight(float angle) const { return (1 - std::cos(angle)) * .5f; }
    Vec3 head(const Frame& f, float angle) const {
        // Give a downturned outward view space ahead of the chest. This
        // shifts only the avatar's anchor, never tracked eyes or controllers.
        const float blend = std::clamp(outwardWeight(angle) * 2 - 1, 0.f, 1.f);
        const float spare = std::max(0.f, dot(f.head - f.floor, f.normal) - (.12f + .04f * f.scale));
        const float share = blend * blend * (3 - 2 * blend);
        return f.head - (f.normal * std::min(f.eyeClearance, spare) + f.up * f.eyeDrop) * share +
               f.up * (f.bodyRaise * outwardWeight(angle)) +
               f.normal * (f.turnRoom * std::sin(angle) * std::sin(angle));
    }
    Vec3 tangent(const Frame& f, unsigned i, float angle) const {
        const float yaw = (i ? -1.f : 1.f) * (.61086524f - .34906585f * outwardWeight(angle));
        return Quat::around(f.normal, angle).rotate(f.up * std::cos(yaw) +
            cross(f.normal, f.up) * std::sin(yaw));
    }
    float ankleClearance(const Frame& f, unsigned i, float angle) const {
        const float pitch = -.43633231f * outwardWeight(angle);
        return f.ankle[i] * std::cos(pitch) + f.lead[i] * std::sin(pitch);
    }
    Vec3 footForward(const Frame& f, unsigned i, float angle) const {
        const float pitch = .61086524f - .43633231f * outwardWeight(angle);
        return tangent(f, i, angle) * std::cos(pitch) - f.normal * std::sin(pitch);
    }
    Vec3 footNormal(const Frame& f, unsigned i, float angle) const {
        const float pitch = .61086524f - .43633231f * outwardWeight(angle);
        return f.normal * std::cos(pitch) + tangent(f, i, angle) * std::sin(pitch);
    }
    Vec3 toeOffset(const Frame& f, unsigned i, float angle) const {
        const float pitch = -.43633231f * outwardWeight(angle);
        const float lead = f.lead[i] * std::cos(pitch) - f.ankle[i] * std::sin(pitch);
        return tangent(f, i, angle) * lead - f.normal * ankleClearance(f, i, angle);
    }
    Quat rotation(const Frame& f, float angle) const {
        // Settle hips about the avatar's adjusted head anchor, with
        // rigid bone lengths. Normal crawling keeps its saved clearance.
        const Vec3 delta = f.pelvis - f.head;
        const float along = dot(delta, f.up), depth = dot(delta, f.normal);
        const float span = std::hypot(along, depth);
        Quat settle;
        if (span > .1f * f.scale) {
            const float before = dot(f.pelvis - f.floor, f.normal);
            const float clearance = before +
                (std::min(before, f.outwardClearance) - before) * outwardWeight(angle);
            const float across = std::clamp((dot(head(f, angle) - f.floor, f.normal) - clearance) / span,
                                           -.995f, .995f);
            const float pitch = -std::asin(across) - std::atan2(depth, -along);
            settle = Quat::around(cross(f.normal, f.up), pitch);
        }
        return Quat::around(settle.rotate(-delta), angle) * settle;
    }
    Vec3 pelvis(const Frame& f, float angle) const {
        return place(f, f.pelvis, angle);
    }
    Vec3 place(const Frame& f, Vec3 point, float angle) const {
        return head(f, angle) + rotation(f, angle).rotate(point - f.head);
    }
    void sync(const Frame& f, const std::array<Vec3, 2>& contacts) {
        if (turning_) return;
        feet_ = contacts;
        remember(f);
    }
    bool update(const Frame& f) {
        if (!std::isfinite(f.seconds) || f.seconds < 0 || f.seconds > .1f ||
            !finite(f.head) || !finite(f.pelvis) || !finite(f.floor) || !finite(f.origin) ||
            !finite(f.normal) || !finite(f.up) || std::abs(length(f.normal) - 1) > .01f ||
            std::abs(length(f.up) - 1) > .01f || std::abs(dot(f.normal, f.up)) > .01f ||
            !std::isfinite(f.scale) || f.scale < .5f || f.scale > 2 ||
            !std::isfinite(f.eyeClearance) || f.eyeClearance < 0 || f.eyeClearance > .2f ||
            !std::isfinite(f.eyeDrop) || f.eyeDrop < 0 || f.eyeDrop > .4f ||
            !std::isfinite(f.outwardClearance) || f.outwardClearance < .03f || f.outwardClearance > .4f ||
            !std::isfinite(f.bodyRaise) || f.bodyRaise < 0 || f.bodyRaise > .2f ||
            !std::isfinite(f.turnRoom) || f.turnRoom < -.15f || f.turnRoom > .2f)
            return reset();
        for (unsigned i = 0; i < 2; ++i)
            if (!finite(f.hips[i]) || !finite(f.shoulders[i]) || !finite(f.contacts[i]) ||
                !std::isfinite(f.reach[i]) || f.reach[i] <= 0 || !std::isfinite(f.armReach[i]) ||
                !std::isfinite(f.ankle[i]) || !std::isfinite(f.lead[i]) || !std::isfinite(f.span[i]) ||
                f.span[i] < 0 ||
                ((f.supports & (1u << i)) && (!finite(f.wrists[i]) || f.armReach[i] <= 0)))
                return reset();
        if (f.seconds == 0) return engaged();
        if (!ready_) {
            feet_ = f.contacts; remember(f); ready_ = true;
            // An outward entry has no preceding wall-facing stance to turn
            // away from. Seed its own contacts directly, without a half-turn.
            const float entryAngle = (f.direction < 0 ? -1.f : 1.f) * 3.14159265f;
            if (f.entryOutward && canPose(f, entryAngle, true)) {
                outward_ = toOut_ = true;
                angle_ = entryAngle;
                for (unsigned i = 0; i < 2; ++i) feet_[i] = goalFor(f, i, i == 1, true);
            }
        }
        if (dot(normal_, f.normal) < .99999f || dot(up_, f.up) < .99999f) {
            const Quat corner = arc(normal_, f.normal);
            const Vec3 previousUp = corner.rotate(up_);
            const float roll = std::atan2(dot(cross(previousUp, f.up), f.normal), dot(previousUp, f.up));
            const Quat turn = Quat::around(f.normal, roll) * corner;
            for (auto& foot : feet_) foot = centre(f) + turn.rotate(foot - centre_);
            start_ = centre(f) + turn.rotate(start_ - centre_);
        }
        if (!turning_) {
            wait_ = f.wanted == outward_ ? 0 : wait_ + f.seconds;
            if (wait_ >= .15f && f.mayStart && canTurn(f)) {
                toOut_ = !outward_;
                sign_ = f.direction < 0 ? -1.f : 1.f;
                fromAngle_ = angle_;
                high_ = dot(feet_[0] - feet_[1], f.up) < 0 ? 0 : 1;
                stage_ = 0; progress_ = 0; turning_ = true;
                start_ = feet_[high_];
                wait_ = 0;
            }
        }
        if (turning_) {
            const unsigned moving = stage_ == 1 ? 1 - high_ : high_;
            const unsigned support = 1 - moving;
            const Vec3 hip = place(f, f.hips[support], angle_);
            const float distance = length(feet_[support] - toeOffset(f, support, angle_) - hip);
            const float speed = length(f.origin - origin_) / f.seconds;
            float increment = f.seconds / .24f;
            const float room = f.reach[support] * .97f - distance;
            if (speed > .01f && room < speed * f.seconds * 6)
                increment = std::max(increment, (1 - progress_) * .5f);
            progress_ = std::min(1.f, progress_ + increment);
            const float ease = progress_ * progress_ * (3 - 2 * progress_);
            const float fractions[4]{0, 1.f / 6, 2.f / 3, 1};
            const float from = fractions[stage_], to = fractions[stage_ + 1];
            angle_ = fromAngle_ + sign_ * 3.14159265f * (from + (to - from) * ease);
            const Vec3 goal = goalFor(f, moving, stage_ == 1, stage_ == 0 ? outward_ : toOut_);
            if (stage_ == 1) {
                // Drop on the old side before crossing the planted high foot.
                const Vec3 bridge = start_ + f.up * dot(goal - start_, f.up);
                const float first = std::min(1.f, ease / .4f);
                const float second = std::max(0.f, (ease - .4f) / .6f);
                feet_[moving] = ease < .4f ? start_ + (bridge - start_) * first :
                                          bridge + (goal - bridge) * second;
            } else {
                feet_[moving] = start_ + (goal - start_) * ease;
            }
            feet_[moving] += f.normal * (.065f * f.scale * std::sin(3.14159265f * progress_));
            if (progress_ >= 1) {
                feet_[moving] = goal;
                if (++stage_ == 3) {
                    turning_ = false; outward_ = toOut_;
                    angle_ = outward_ ? sign_ * 3.14159265f : 0;
                } else {
                    progress_ = 0;
                    start_ = feet_[stage_ == 1 ? 1 - high_ : high_];
                }
            }
            // A rotating hip and shoe trace arcs between the reachable end
            // poses. Bound the flying ankle on its current wall-parallel
            // plane as well; otherwise IK shortens the reach and the visible
            // toe misses its planned contact. The supporting toe stays fixed.
            const Vec3 movingHip = place(f, f.hips[moving], angle_);
            const Vec3 offset = toeOffset(f, moving, angle_);
            const Vec3 delta = feet_[moving] - offset - movingHip;
            const float depth = dot(delta, f.normal), limit = f.reach[moving] * .995f;
            const Vec3 tangent = delta - f.normal * depth;
            const float radius = std::sqrt(std::max(0.f, limit * limit - depth * depth));
            if (length(tangent) > radius)
                feet_[moving] = movingHip + f.normal * depth + limited(tangent, radius) + offset;
        }
        remember(f);
        return engaged();
    }
    bool engaged() const { return turning_ || outward_; }
    bool ready() const { return ready_; }
    bool turning() const { return turning_; }
    bool outward() const { return outward_; }
    bool targetOutward() const { return turning_ ? toOut_ : outward_; }
    int swinging() const { return turning_ ? static_cast<int>(stage_ == 1 ? 1 - high_ : high_) : -1; }
    float angle() const { return angle_; }
    const std::array<Vec3, 2>& feet() const { return feet_; }
    bool reset() { *this = {}; return false; }

  private:
    Vec3 centre(const Frame& f) const {
        return f.pelvis - f.normal * dot(f.pelvis - f.floor, f.normal) - f.up * (.34f * f.scale);
    }
    Vec3 goalFor(const Frame& f, unsigned foot, bool low, bool outward) const {
        const Vec3 left = cross(f.normal, f.up);
        if (outward && low) {
            const float angle = 3.14159265f;
            const Vec3 hip = place(f, f.hips[foot], angle);
            const Vec3 offset = toeOffset(f, foot, angle);
            const Vec3 base = f.pelvis - f.normal * dot(f.pelvis - f.floor, f.normal) +
                              left * ((foot ? 1.f : -1.f) * .19f * f.scale) - offset;
            const Vec3 delta = base - hip;
            const float depth = dot(delta, f.normal), side = dot(delta, left);
            const float reach = f.reach[foot] * .94f;
            const float down = std::sqrt(std::max(0.f, reach * reach - depth * depth - side * side));
            return hip + f.normal * depth + left * side - f.up * down + offset;
        }
        const float gap = std::max(.36f * f.scale, std::max(f.span[0], f.span[1]) + .015f * f.scale);
        // Share the height between both reachable pelvis positions. Closing
        // the hips toward the wall raises them about the fixed head; retaining
        // the old low lane can otherwise put the return step beyond reach.
        const float rise = std::clamp(dot(pelvis(f, 3.14159265f) - f.pelvis, f.up) * .5f,
                                      0.f, .25f * f.scale);
        return f.pelvis - f.normal * dot(f.pelvis - f.floor, f.normal) +
               left * ((foot ? -1.f : 1.f) * (outward ? -1.f : 1.f) *
                       (outward ? .19f : .24f) * f.scale) -
               f.up * (.33f * f.scale + (low ? gap : 0.f) - rise);
    }
    bool canPose(const Frame& f, float angle, bool outward) const {
        for (unsigned i = 0; i < 2; ++i) {
            const Vec3 shoulder = place(f, f.shoulders[i], angle);
            if (dot(shoulder - f.floor, f.normal) < .02f * f.scale) return false;
            if ((f.supports & (1u << i)) &&
                length(shoulder - f.wrists[i]) > f.armReach[i] + .035f * f.scale) return false;
            const Vec3 hip = place(f, f.hips[i], angle);
            for (bool low : {false, true})
                if (length(goalFor(f, i, low, outward) - toeOffset(f, i, angle) - hip) >
                    f.reach[i] * .97f) return false;
        }
        return true;
    }
    bool canTurn(const Frame& f) const {
        const float direction = f.direction < 0 ? -1.f : 1.f;
        for (unsigned i = 0; i < 2; ++i)
            if (std::abs(dot(feet_[i] - f.floor, f.normal)) > .025f)
                return false;
        for (int part = 0; part <= 12; ++part) {
            const float angle = angle_ + direction * 3.14159265f * part / 12;
            for (unsigned i = 0; i < 2; ++i) {
                const Vec3 shoulder = place(f, f.shoulders[i], angle);
                if (dot(shoulder - f.floor, f.normal) < .02f * f.scale) return false;
                if ((f.supports & (1u << i)) &&
                    length(shoulder - f.wrists[i]) > f.armReach[i] + .035f * f.scale)
                    return false;
            }
        }
        return canPose(f, angle_, outward_) &&
               canPose(f, angle_ + direction * 3.14159265f, !outward_);
    }
    void remember(const Frame& f) {
        origin_ = f.origin; normal_ = f.normal; up_ = f.up; centre_ = centre(f);
    }
    std::array<Vec3, 2> feet_{};
    Vec3 start_{}, origin_{}, normal_{}, up_{}, centre_{};
    unsigned high_{}, stage_{};
    float progress_{}, angle_{}, fromAngle_{}, sign_ = 1, wait_{};
    bool ready_{}, turning_{}, outward_{}, toOut_{};
};
} // namespace spidy
