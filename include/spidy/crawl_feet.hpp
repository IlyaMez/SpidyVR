#pragma once
#include "math.hpp"

namespace spidy {
// Visual foot contacts in world space. Pulling the body past a planted foot
// cannot drag that foot along with the hips. Only one foot reaches at a time.
class CrawlFeet {
  public:
    struct Frame {
        Vec3 origin{}, floor{}, normal{}, ahead{};
        std::array<Vec3, 2> rest{}, hips{};
        std::array<float, 2> reach{}, clearance{}, maxUp{}, sideClearance{}, sideLimit{};
        float seconds{}, scale = 1;
        bool handSync{}, longAlternation{}, finishStep{};
        std::array<uint32_t, 2> strokes{};
    };
    bool update(const Frame& f) {
        if (!finite(f.origin) || !finite(f.floor) || !finite(f.normal) || !finite(f.ahead) ||
            !std::isfinite(f.seconds) || f.seconds < 0 || f.seconds > .1f ||
            !std::isfinite(f.scale) || f.scale < .5f || f.scale > 2 ||
            std::abs(length(f.normal) - 1) > .01f || std::abs(length(f.ahead) - 1) > .01f ||
            std::abs(dot(f.normal, f.ahead)) > .01f)
            return reset();
        for (int i = 0; i < 2; ++i)
            if (!finite(f.rest[i]) || !finite(f.hips[i]) || !std::isfinite(f.reach[i]) ||
                f.reach[i] <= 0 || !std::isfinite(f.clearance[i]) || f.clearance[i] < 0 ||
                !std::isfinite(f.maxUp[i]) || !std::isfinite(f.sideClearance[i]) || f.sideClearance[i] < 0 ||
                !std::isfinite(f.sideLimit[i]) || f.sideLimit[i] < 0 ||
                (f.sideLimit[i] > 0 && f.sideLimit[i] < std::max(.1f * f.scale, f.sideClearance[i])))
                return reset();
        Vec3 travel = f.origin - origin_;
        const Vec3 centre = (f.rest[0] + f.rest[1]) * .5f;
        const bool entering = !ready_;
        if (!ready_ || length(travel) > .5f * f.scale || std::abs(f.scale - scale_) > .03f) {
            feet_ = f.rest;
            swing_ = -1;
            next_ = 0;
            last_ = -1;
            distance_ = 0;
            ready_ = true;
            travel = {};
            // A pull can start while the body is still blending onto the wall.
            // Preserve that first cue; teleports/calibration changes seed anew.
            seenStrokes_ = entering && f.handSync ? std::array<uint32_t, 2>{} : f.strokes;
            pending_ = 0;
            idle_ = 0;
        } else if (dot(normal_, f.normal) < .99999f || dot(ahead_, f.ahead) < .99999f) {
            const Quat corner = arc(normal_, f.normal);
            const Vec3 previous = corner.rotate(ahead_);
            const float roll = std::atan2(dot(cross(previous, f.ahead), f.normal), dot(previous, f.ahead));
            const Quat turn = Quat::around(f.normal, roll) * corner;
            // Camera roll turns about the head, not the actor's feet. Carry
            // contacts with the stance centre only while the frame turns.
            const auto carry = [&](Vec3 p) { return centre + turn.rotate(p - centre_); };
            for (auto& foot : feet_)
                foot = carry(foot);
            start_ = carry(start_);
            goal_ = carry(goal_);
        }
        travel -= f.normal * dot(travel, f.normal);
        const float moved = length(travel);
        distance_ += moved;
        if (f.handSync) {
            for (unsigned hand = 0; hand < 2; ++hand) {
                if (f.strokes[hand] != seenStrokes_[hand])
                    pending_ |= 1u << (1 - hand);
                seenStrokes_[hand] = f.strokes[hand];
            }
            idle_ = moved > .00001f ? 0 : idle_ + f.seconds;
            if (idle_ > .15f) pending_ = 0;
        }
        if (moved > .00001f) direction_ = travel / moved;
        // The body supplies the ankle ceiling after allowing for the whole
        // foot above it, so even the toes stay below the pelvis.
        const auto heightRoom = [&](int foot, Vec3 contact) {
            return f.maxUp[foot] - dot(contact - f.hips[foot], f.ahead);
        };
        const Vec3 middle = (f.hips[0] + f.hips[1]) * .5f;
        const Vec3 left = cross(f.normal, f.ahead);
        const auto separation = [&](int foot) { return std::max(.1f * f.scale, f.sideClearance[foot]); };
        const auto outward = [&](int foot) { return left * (foot ? -1.f : 1.f); };
        const auto sideRoom = [&](int foot, Vec3 contact) {
            return dot(contact - middle, outward(foot)) - separation(foot);
        };
        const auto outerRoom = [&](int foot, Vec3 contact) {
            return f.sideLimit[foot] > 0 ?
                f.sideLimit[foot] - dot(contact - middle, outward(foot)) : 100.f * f.scale;
        };
        const auto reachable = [&](int foot, Vec3 contact, float fraction) {
            const Vec3 fromHip = contact - f.hips[foot];
            const float depth = dot(fromHip, f.normal);
            const float limit = f.reach[foot] * fraction;
            const float radius = std::sqrt(std::max(.0001f, limit * limit - depth * depth));
            const Vec3 tangent = fromHip - f.normal * depth;
            const float along = dot(tangent, f.ahead);
            const Vec3 side = outward(foot);
            const float minimum = separation(foot) - dot(f.hips[foot] - middle, side);
            const float reserved = std::max(0.f, minimum);
            const float heightRadius = std::sqrt(std::max(0.f, radius * radius - reserved * reserved));
            const float height = std::clamp(along, -heightRadius,
                std::clamp(f.maxUp[foot], -heightRadius, heightRadius));
            const float width = std::sqrt(std::max(0.f, radius * radius - height * height));
            const float maximum = f.sideLimit[foot] > 0 ?
                f.sideLimit[foot] - dot(f.hips[foot] - middle, side) : width;
            const float lateral = std::clamp(dot(tangent, side), minimum,
                                            std::max(minimum, std::min(width, maximum)));
            return f.hips[foot] + f.normal * depth + f.ahead * height + side * lateral;
        };
        const float descending = -dot(travel, f.ahead);
        const auto travelRoom = [&](int foot, Vec3 contact) {
            const Vec3 fromHip = contact - f.hips[foot];
            const float along = dot(fromHip, direction_);
            const float limit = f.reach[foot] * .999f;
            float room = along + std::sqrt(std::max(0.f,
                along * along + limit * limit - dot(fromHip, fromHip)));
            if (descending > .00001f)
                room = std::min(room, heightRoom(foot, contact) * moved / descending);
            const float crossing = dot(travel, outward(foot));
            if (crossing > .00001f)
                room = std::min(room, sideRoom(foot, contact) * moved / crossing);
            if (crossing < -.00001f)
                room = std::min(room, outerRoom(foot, contact) * moved / -crossing);
            return room;
        };
        // On a new facade the contacts lie on its plane, including ceilings.
        for (int i = 0; i < 2; ++i) {
            if (i != swing_)
                feet_[i] += f.normal * (f.clearance[i] - dot(feet_[i] - f.floor, f.normal));
        }
        const bool landing = swing_ >= 0;
        if (landing) {
            float increment = f.seconds / duration_;
            if (f.longAlternation && moved > .00001f)
                increment = moved / std::max(moved, strideTravel_);
            if (f.finishStep) increment = std::max(increment, f.seconds / .12f);
            if (f.handSync && moved > .00001f) {
                const int support = 1 - swing_;
                // Recheck the support reserve during flight: the pull can
                // accelerate or change direction after a step has begun.
                const float reserve = std::min(travelRoom(support, feet_[support]),
                    travelRoom(swing_, goal_)) / moved;
                const float frames = std::max(1.f, reserve - 3.f);
                increment = std::max(increment, (1 - progress_) / frames);
            }
            progress_ = std::min(1.f, progress_ + increment);
            const float ease = progress_ * progress_ * (3 - 2 * progress_);
            feet_[swing_] = start_ * (1 - ease) + goal_ * ease +
                f.normal * (.065f * f.scale * std::sin(3.14159265f * progress_));
            if (f.longAlternation && moved > .00001f && !f.finishStep) {
                // Keep the free leg long until the planted leg extends, then
                // fold it into the next contact. Pull distance sets the phase;
                // slow hand pulls must not collapse both knees halfway through.
                const int support = 1 - swing_;
                const float extension = length(feet_[support] - f.hips[support]) / f.reach[support];
                const float fold = std::clamp((extension - .86f) / .08f, 0.f, 1.f);
                const float minimum = (.94f - .49f * fold * fold * (3 - 2 * fold)) * f.reach[swing_];
                const Vec3 delta = feet_[swing_] - f.hips[swing_];
                if (length(delta) < minimum) {
                    const Vec3 side = outward(swing_);
                    const float depth = dot(delta, f.normal), lateral = dot(delta, side);
                    const float down = std::sqrt(std::max(0.f,
                        minimum * minimum - depth * depth - lateral * lateral));
                    feet_[swing_] = f.hips[swing_] + f.normal * depth + side * lateral - f.ahead * down;
                }
            }
            if (f.handSync) {
                feet_[swing_] = reachable(swing_, feet_[swing_], .985f);
            }
            // Finish a begun step even if the pull stops halfway through it.
            if (progress_ >= 1) {
                if (!f.handSync) feet_[swing_] = goal_;
                next_ = 1 - swing_;
                swing_ = -1;
            }
        } else if (moved > .00001f && f.handSync) {
            // A stroke begins an opposite-hand reach; subsequent contacts
            // alternate, including a long pull made with just one hand.
            // Replaying a short cue on the same foot caused the old shuffle.
            int candidate = next_;
            if (last_ < 0 && pending_)
                candidate = pending_ & 1u ? 0 : 1;
            // Use the first constraint that will expire: leg reach, foot
            // height or the body's middle. Always prioritizing the leading
            // foot's centre boundary could exhaust the trailing leg.
            const int other = 1 - candidate;
            if (travelRoom(other, feet_[other]) < moved * 4.f &&
                travelRoom(other, feet_[other]) < travelRoom(candidate, feet_[candidate]))
                candidate = other;
            const float reach = f.reach[candidate];
            const bool cue = (pending_ & (1u << candidate)) != 0;
            const Vec3 fromHip = feet_[candidate] - f.hips[candidate];
            // Keep each contact until that leg has used almost its whole
            // reach. Hand cues can start the entry step; later short pulls
            // accumulate instead of replaying a partly bent, small step.
            const bool stretched = length(fromHip) >= reach * .995f ||
                length(fromHip - travel) > reach * .999f ||
                heightRoom(candidate, feet_[candidate]) < std::max(0.f, descending * 2.f) ||
                sideRoom(candidate, feet_[candidate]) <
                    std::max(0.f, dot(travel, outward(candidate)) * 2.f) ||
                outerRoom(candidate, feet_[candidate]) <
                    std::max(0.f, -dot(travel, outward(candidate)) * 2.f);
            const bool request = stretched || (last_ < 0 && cue);
            Vec3 goal = f.rest[candidate];
            // The resting stance must not shorten a sideways step on the
            // trailing side. Both feet need a target ahead of their own hip.
            const float lead = dot(goal - f.hips[candidate], direction_);
            goal += direction_ * (reach * .45f - lead);
            const float goalRoom = heightRoom(candidate, goal);
            if (goalRoom < 0) goal += f.ahead * goalRoom;
            goal += f.normal * (f.clearance[candidate] - dot(goal - f.floor, f.normal));
            // Keep the target on the wall and inside this character's actual
            // leg reach. This also bounds sideways/downward climbing targets.
            goal = reachable(candidate, goal, .94f);
            const float advance = dot(goal - feet_[candidate], direction_);
            const float minimum = reach * (last_ < 0 ? .14f : .6f);
            if (request && (advance > minimum || stretched)) {
                swing_ = last_ = candidate;
                pending_ &= ~(1u << swing_);
                start_ = feet_[swing_];
                // The lift begins in this update, rather than holding an
                // already extended toe for one extra fast-travel frame.
                start_ = reachable(swing_, start_, .995f);
                feet_[swing_] = start_;
                goal_ = goal;
                // Time a complete reach to the pull speed. Fast travel
                // shortens it only enough to keep the supporting leg within
                // reach until the new contact is available.
                const float speed = moved / std::max(f.seconds, .001f);
                duration_ = std::clamp(reach * .45f / speed, .26f, 1.1f);
                for (int i = 0; i < 2; ++i) {
                    // Land with enough reserve for the next update to lift
                    // the now-extended supporting foot, without toe sliding.
                    const float reserve = travelRoom(i, i == swing_ ? goal_ : feet_[i]);
                    const float safeTravel = std::max(0.f, reserve - moved * 3.f);
                    duration_ = std::min(duration_, std::max(f.seconds, safeTravel / speed));
                }
                strideTravel_ = std::max(moved, .94f * std::min(
                    travelRoom(1 - swing_, feet_[1 - swing_]), travelRoom(swing_, goal_)) - moved * 3.f);
                progress_ = 0;
                distance_ = 0;
            }
        } else if (moved > .00001f) {
            int candidate = next_;
            const Vec3 error = f.rest[candidate] - feet_[candidate];
            const bool stretched = length(feet_[candidate] - f.hips[candidate]) > f.reach[candidate] * .8f;
            if (distance_ > .08f * f.scale &&
                (length(error - f.normal * dot(error, f.normal)) > .07f * f.scale || stretched)) {
                swing_ = candidate;
                pending_ &= ~(1u << swing_);
                start_ = feet_[swing_];
                goal_ = f.rest[swing_] + direction_ * (.12f * f.scale);
                goal_ += f.normal * (f.clearance[swing_] - dot(goal_ - f.floor, f.normal));
                duration_ = std::clamp(.12f * f.scale / std::max(.3f, moved / std::max(f.seconds, .001f)),
                                       .05f * f.scale, .2f);
                progress_ = 0;
                distance_ = 0;
            }
        }
        origin_ = f.origin;
        centre_ = centre;
        normal_ = f.normal;
        ahead_ = f.ahead;
        scale_ = f.scale;
        return true;
    }
    const std::array<Vec3, 2>& feet() const { return feet_; }
    bool ready() const { return ready_; }
    void replant(const Frame& frame, const std::array<Vec3, 2>& contacts) {
        reset();
        if (update(frame)) {
            feet_ = contacts;
            seenStrokes_ = frame.strokes;
            pending_ = 0;
        }
    }
    int swinging() const { return swing_; }
    bool reset() { *this = {}; return false; }
  private:
    bool ready_{};
    int swing_ = -1, last_ = -1, next_{};
    unsigned pending_{};
    std::array<uint32_t, 2> seenStrokes_{};
    Vec3 origin_{}, centre_{}, normal_{}, ahead_{}, direction_{}, start_{}, goal_{};
    std::array<Vec3, 2> feet_{};
    float distance_{}, progress_{}, strideTravel_{}, duration_ = .2f, scale_ = 1, idle_{};
};
} // namespace spidy
