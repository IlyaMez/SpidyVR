#pragma once
#include "native_rays.hpp"
#include "tracking.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace spidy {
// Local, ray-confirmed building contact. This changes placement, never the
// physical hand motion used for grip climbing or the native player collider.
class HandCollision {
  public:
    static constexpr float keepSeconds = .1f, patchReach = .2f, probeExtra = .15f;
    static constexpr float maxPush = .45f, pushSpeed = 1.2f, returnSpeed = .6f;
    static constexpr unsigned firstTag = 8;

    // A conservative open-hand outline: palm, heel/wrist and fingertips.
    // Grip axes and wrist offsets match the body solver; left mirrors x.
    static float clearance(Pose grip, unsigned hand, Vec3 normal) {
        const Vec3 wrist{hand ? .02f : -.02f, .09f, 0};
        return std::max({.025f, .035f - dot(grip.orientation.rotate(wrist), normal),
                         .018f - dot(grip.orientation.rotate({0, -.1f, 0}), normal)});
    }
    void feedback(const native_rays::Data& data, int64_t now, int64_t frequency) {
        if (data.magic != 0x53525944 || data.version != 2 || data.bytes != sizeof(data) ||
            data.status != 2 || data.count > native_rays::capacity || !data.serial ||
            data.serial <= serial_ || frequency <= 0 || now <= 0 || !data.qpc ||
            data.qpc > static_cast<uint64_t>(now) ||
            now - static_cast<int64_t>(data.qpc) > frequency / 10)
            return;
        serial_ = data.serial;
        for (unsigned i = 0; i < data.count; ++i) {
            const auto& hit = data.hits[i];
            if (hit.ray.tag < firstTag || hit.ray.tag >= firstTag + 2) continue;
            auto& plane = planes_[hit.ray.tag - firstTag];
            plane = {};
            if (native_rays::fixedSurface(hit) && std::isfinite(hit.fraction) &&
                hit.fraction >= 0 && hit.fraction < 1 && finite(hit.position) && finite(hit.normal) &&
                finite(hit.ray.origin) && finite(hit.ray.direction) &&
                std::abs(length(hit.ray.direction) - 1) < .001f &&
                std::isfinite(hit.ray.distance) && hit.ray.distance > probeExtra &&
                hit.ray.distance < 2.f + probeExtra &&
                std::abs(length(hit.normal) - 1) < .01f && dot(hit.normal, hit.ray.direction) < -.05f) {
                const Vec3 sampled = hit.ray.origin + hit.ray.direction * (hit.ray.distance - probeExtra);
                plane = {hit.position, hit.normal, sampled, 0};
            }
        }
    }
    Vec3 update(float seconds, bool enabled, Pose head, const std::array<Pose, 2>& grips,
                std::array<bool, 2> tracked) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        active_ = enabled && dt > 0 && validTrackedPose(head);
        if (!active_) {
            planes_ = {}; push_ = {}; tracked_ = {};
            return {};
        }
        raw_ = grips;
        for (unsigned i = 0; i < 2; ++i) {
            tracked_[i] = tracked[i] && validTrackedPose(grips[i]);
            if (!tracked_[i]) planes_[i] = {};
        }
        head_ = head.position;
        Vec3 wanted{};
        // Two palms on different faces share one displacement, not two
        // independent camera impulses. Revisit angled faces after each push.
        for (unsigned pass = 0; pass < 24; ++pass)
            for (unsigned i = 0; i < 2; ++i)
                if (usable(i)) {
                    const auto& p = planes_[i];
                    const float into = clearance(raw_[i], i, p.normal) -
                                       dot(raw_[i].position + wanted - p.point, p.normal);
                    if (into > 0) wanted += p.normal * into;
                }
        wanted = limited(wanted, maxPush);
        const float speed = length(wanted) > length(push_) ? pushSpeed : returnSpeed;
        push_ += limited(wanted - push_, speed * dt);
        for (auto& p : planes_) p.age += dt;
        return push_;
    }
    // Stops the visible hand immediately while the camera displacement
    // catches up. Tangential hand movement is retained along the real face.
    Vec3 correction(unsigned hand, Pose placed) const {
        if (hand >= 2 || !active_ || !usable(hand) || !validTrackedPose(placed)) return {};
        const auto& p = planes_[hand];
        return p.normal * std::max(0.f, clearance(placed, hand, p.normal) -
                                       dot(placed.position - p.point, p.normal));
    }
    unsigned contacts(const std::array<Pose, 2>& placed) const {
        unsigned mask = 0;
        if (!active_) return mask;
        for (unsigned i = 0; i < 2; ++i)
            if (usable(i) && validTrackedPose(placed[i])) {
                const float gap = dot(placed[i].position - planes_[i].point, planes_[i].normal) -
                                  clearance(placed[i], i, planes_[i].normal);
                const float pressure = dot(raw_[i].position - planes_[i].point, planes_[i].normal) -
                                       clearance(raw_[i], i, planes_[i].normal);
                // One deeply pressing palm moves both hands with the camera.
                // Keep the other ray-confirmed pressing palm in contact even
                // when that shared displacement lifts its visible mesh off.
                if ((gap >= -.002f && gap <= .012f) || pressure <= .012f) mask |= 1u << i;
            }
        return mask;
    }
    std::array<native_rays::Ray, 2> next(Vec3 safeHead) const {
        std::array<native_rays::Ray, 2> rays{};
        for (auto& ray : rays) ray.distance = 0;
        if (!active_ || !finite(safeHead)) return rays;
        for (unsigned i = 0; i < 2; ++i) {
            const Vec3 way = raw_[i].position - safeHead;
            const float distance = length(way);
            if (tracked_[i] && distance > .05f && distance < 2.f)
                rays[i] = {safeHead, distance + probeExtra, way / distance, firstTag + i};
        }
        return rays;
    }
    bool active() const { return active_; }
    Vec3 push() const { return push_; }
    void reset() { *this = {}; }

  private:
    struct Plane { Vec3 point{}, normal{}, sampled{}; float age = 1e9f; };
    bool usable(unsigned hand) const {
        const auto& p = planes_[hand];
        if (!tracked_[hand] || p.age > keepSeconds || dot(head_ - p.point, p.normal) <= .01f)
            return false;
        // The ray can meet a face above a deeply pushed palm. Locality is
        // measured from the sampled controller, not that oblique hit point.
        const Vec3 delta = raw_[hand].position - p.sampled;
        return length(delta - p.normal * dot(delta, p.normal)) <= patchReach;
    }
    std::array<Plane, 2> planes_{};
    std::array<Pose, 2> raw_{};
    std::array<bool, 2> tracked_{};
    Vec3 head_{}, push_{};
    uint64_t serial_{};
    bool active_{};
};
} // namespace spidy
