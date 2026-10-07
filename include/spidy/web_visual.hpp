#pragma once
#include "vertex.hpp"
#include <cstdint>
#include <vector>

namespace spidy {
// One web as drawn over the eye images. Positions are world metres.
struct WebLine {
    Vec3 start{}, end{}; // wrist and anchor
    float slack{};       // rope length beyond the straight distance; 0 when taut
    float extended = 1;  // 0..1 of the way from the wrist while the web is shot
    bool splat = true;   // impact web at the anchor once the shot arrives
    float seed{};        // varies splat strands between webs
};
struct WebLook {
    float radius = .005f;   // half width of the strand
    float minPixels = 1.6f; // on-screen width floor, so distant webs stay continuous
    Vec3 core{.86f, .87f, .9f}, edge{.32f, .36f, .44f};
};
// Camera-facing braided strand with a twisting sheen, parabolic sag when
// slack, a travelling tip during the shot, and a splat at the anchor.
// pixelAngle is the eye's angular size of one pixel, in radians.
void appendWeb(std::vector<Vertex>& out, const WebLine& web, Vec3 viewer, float pixelAngle,
               const WebLook& look = {});
// Shot and release timing shared by the headset overlay and the lab.
struct WebAnimation {
    static constexpr float shotSeconds = .09f, releaseSeconds = .22f;
    // Portion of the web extended this long after attachment.
    static float extended(float sinceAttach) {
        return std::isfinite(sinceAttach) ? std::clamp(sinceAttach / shotSeconds, 0.f, 1.f) : 1.f;
    }
    // After release the free end whips back toward the anchor and sags.
    static bool released(float sinceRelease, Vec3 hand, Vec3 anchor, WebLine& out) {
        if (!std::isfinite(sinceRelease) || sinceRelease < 0 || sinceRelease >= releaseSeconds)
            return false;
        const float t = sinceRelease / releaseSeconds, pull = t * t * (3 - 2 * t);
        out.start = hand + (anchor - hand) * pull;
        out.end = anchor;
        out.slack = length(anchor - hand) * .35f * (1 - pull);
        out.extended = 1;
        return length(out.end - out.start) > .05f;
    }
};
// When each hand's web was shot or let go, in predicted display time (ns).
class WebTimeline {
  public:
    struct Hand {
        int64_t attachedAt{}, releasedAt{};
        Vec3 anchor{}, wrist{};
        bool attached{};
    };
    void update(unsigned hand, bool attached, Vec3 anchor, Vec3 wrist, int64_t now) {
        auto& h = hands_.at(hand);
        if (attached && !h.attached)
            h.attachedAt = now;
        if (!attached && h.attached)
            h.releasedAt = now;
        if (attached) {
            h.anchor = anchor;
            h.wrist = wrist;
        }
        h.attached = attached;
    }
    const Hand& operator[](unsigned hand) const {
        return hands_.at(hand);
    }
    void clear() {
        hands_ = {};
    }

  private:
    std::array<Hand, 2> hands_{};
};
// Web shooters sit on the palm side of the wrist, behind the grip centre.
inline Vec3 webWrist(Pose hand) {
    return hand.position + hand.orientation.rotate({0, -.025f, .03f});
}
// Aim markers: where a grip press would send a hand's web now, drawn over the
// eye images facing the viewer, the same size on screen at any distance.
enum class AimMark : std::uint8_t {
    anchor,  // the web attaches here: a white ring around a dot
    air,     // nothing within reach: the web attaches in the air, a faint dashed ring
    blocked, // the web misses here: a red cross
    target,  // the web catches what is here: amber corners around it
};
struct AimMarker {
    AimMark kind = AimMark::anchor;
    Vec3 point{};    // where the web goes; a target's centre
    float radius{};  // a target's radius, metres
    float squeeze{}; // the hand's grip, 0..1: the marker tightens as it closes
};
// pixelAngle as for appendWeb.
void appendAimMarker(std::vector<Vertex>& out, const AimMarker& marker, Vec3 viewer, float pixelAngle);
// Where the marker for a surface the aim ray met at `point` (facing `normal`)
// goes on an aim ray from `origin` along unit `direction`: where that ray
// crosses the surface's plane, so the marker stays on the line the hand
// points along until the next preview arrives. `point` itself when the ray
// runs nearly along the plane or crosses it far from `point`.
Vec3 onAimLine(Vec3 point, Vec3 normal, Vec3 origin, Vec3 direction);
} // namespace spidy
