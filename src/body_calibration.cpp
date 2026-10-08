#include "spidy/body_calibration.hpp"
#include "spidy/overlay_text.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace spidy::body_calibration {
namespace {
constexpr float pi = 3.14159265f;
constexpr Vec3 worldUp{0, 1, 0};
Vec3 level(Vec3 v) {
    return {v.x, 0, v.z};
}
} // namespace

Proportions proportions(const body::Rig& rig) {
    Proportions p;
    if (!rig.ready || !(rig.eyeHeight > .5f))
        return p;
    p.eyeHeight = rig.eyeHeight;
    for (int i = 0; i < 2; ++i) {
        p.shoulders[i] = rig.shoulders[i];
        p.arms[i] = rig.upperArm[i] + rig.forearm[i];
    }
    return p;
}
float bodyScale(float eyeHeight, const Proportions& p) {
    if (!std::isfinite(eyeHeight) || eyeHeight <= 0 || !(p.eyeHeight > .5f))
        return 1;
    return std::clamp(eyeHeight / p.eyeHeight, minBodyScale, maxBodyScale);
}
float armScale(float armLength, float body, const Proportions& p) {
    const float arm = (p.arms[0] + p.arms[1]) / 2 * (std::isfinite(body) && body > 0 ? body : 1.f);
    if (!std::isfinite(armLength) || armLength <= 0 || !(arm > .05f))
        return 1;
    return std::clamp(armLength / arm, minArmScale, maxArmScale);
}
Vec3 wrist(Pose grip, int side, const body::Config& c) {
    return grip.position +
           grip.orientation.rotate({side == 0 ? -c.wristFromGrip.x : c.wristFromGrip.x, c.wristFromGrip.y,
                                    c.wristFromGrip.z});
}

void Calibration::restart() {
    held_ = lapse_ = progress_ = 0;
    frames_ = 0;
    eyeSum_ = 0;
    reachSum_ = {};
}
void Calibration::start() {
    phase_ = Phase::waiting;
    hint_ = Hint::tracking;
    ready_ = {};
    triggersHeld_ = previous_ = false;
    restart();
}
void Calibration::stop() {
    phase_ = Phase::idle;
    ready_ = {};
    triggersHeld_ = previous_ = false;
    restart();
}
bool Calibration::update(const Sample& s, const Proportions& p, const body::Config& bc, const Config& c) {
    if (phase_ == Phase::idle || phase_ == Phase::done)
        return false;
    const float dt = std::isfinite(s.seconds) ? std::clamp(s.seconds, 0.f, .1f) : 0.f;
    const bool tracked = s.headTracked && s.handTracked[0] && s.handTracked[1] && finite(s.head.position) &&
                         finite(s.grips[0].position) && finite(s.grips[1].position);
    const float eyes = s.head.position.y;
    Hint hint = Hint::none;
    std::array<bool, 2> ready{};
    std::array<float, 2> reach{};
    std::array<Vec3, 2> wrists{};
    const float lowest = std::min(s.triggers[0], s.triggers[1]);
    triggersHeld_ = std::isfinite(lowest) && lowest >= (triggersHeld_ ? c.triggerHeld : c.trigger);
    if (!tracked) {
        hint = Hint::tracking;
    } else if (!(eyes >= c.lowestEyes)) {
        hint = Hint::standUp;
    } else {
        for (int i = 0; i < 2; ++i)
            wrists[i] = wrist(s.grips[i], i, bc);
        const Vec3 look = s.head.orientation.rotate({0, 0, -1});
        Vec3 ahead = normalized(level(look));
        if (length(ahead) < .5f)
            ahead = {0, 0, -1};
        // The body faces square to the line from the right wrist to the
        // left, once the arms are apart; before that, the way the head does.
        const Vec3 across = level(wrists[0] - wrists[1]);
        const bool apart = length(across) >= .25f;
        const Vec3 left = apart ? normalized(across) : cross(worldUp, ahead);
        const Vec3 forward = cross(left, worldUp);
        const float scale = bodyScale(eyes, p);
        std::array<bool, 2> out{}, straight{};
        for (int i = 0; i < 2; ++i) {
            const Vec3 sh = p.shoulders[i];
            const Vec3 shoulder = s.head.position + (forward * sh.x + worldUp * sh.y + left * sh.z) * scale;
            const Vec3 arm = wrists[i] - shoulder;
            reach[i] = length(arm);
            const Vec3 side = i == 0 ? left : left * -1.f;
            out[i] = reach[i] > 1e-3f && dot(arm, side) / reach[i] >= c.sideways;
            straight[i] = reach[i] >= c.bent * scale * p.arms[i];
            ready[i] = out[i] && straight[i];
        }
        const float pitch = std::asin(std::clamp(look.y, -1.f, 1.f));
        // Crossed arms put the left wrist on the right: the body would face back.
        if (!out[0] || !out[1] || dot(forward, ahead) < 0)
            hint = Hint::armsOut;
        else if (!straight[0] || !straight[1] ||
                 std::abs(reach[0] - reach[1]) > c.mismatch * std::max(reach[0], reach[1]))
            hint = Hint::straight;
        else if (std::abs(pitch) > c.headPitch || dot(forward, ahead) < std::cos(c.headYaw))
            hint = Hint::lookAhead;
        else if (!triggersHeld_)
            hint = Hint::triggers;
        else if (previous_ && dt > 0 &&
                 (length(s.head.position - lastHead_) > c.headSpeed * dt ||
                  length(wrists[0] - lastWrists_[0]) > c.handSpeed * dt ||
                  length(wrists[1] - lastWrists_[1]) > c.handSpeed * dt))
            hint = Hint::still;
    }
    previous_ = tracked;
    if (tracked) {
        lastHead_ = s.head.position;
        lastWrists_ = wrists;
    }
    hint_ = hint;
    ready_ = ready;
    if (hint == Hint::none) {
        phase_ = Phase::holding;
        lapse_ = 0;
        held_ += dt;
        ++frames_;
        eyeSum_ += eyes;
        for (int i = 0; i < 2; ++i)
            reachSum_[i] += reach[i];
        progress_ = std::clamp(held_ / std::max(c.holdSeconds, .01f), 0.f, 1.f);
        // (Frame times summed in floats fall a hair short of the whole.)
        if (held_ + 1e-4f >= c.holdSeconds) {
            result_.eyeHeight = static_cast<float>(eyeSum_ / frames_);
            for (int i = 0; i < 2; ++i)
                result_.reach[i] = static_cast<float>(reachSum_[i] / frames_);
            // The straighter arm reaches further.
            result_.armLength = std::max(result_.reach[0], result_.reach[1]);
            phase_ = Phase::done;
            progress_ = 1;
            return true;
        }
    } else if (phase_ == Phase::holding && (lapse_ += dt) > c.graceSeconds) {
        phase_ = Phase::waiting;
        restart();
    }
    return false;
}

Pose panelPose(Pose head) {
    Vec3 ahead = normalized(level(head.orientation.rotate({0, 0, -1})));
    if (length(ahead) < .5f)
        ahead = {0, 0, -1};
    // Quat::yaw(r) turns -z to (-sin r, 0, -cos r).
    return {head.position + ahead * 1.4f + Vec3{0, -.12f, 0}, Quat::yaw(std::atan2(-ahead.x, -ahead.z))};
}

const char* hintText(Hint hint) {
    switch (hint) {
    case Hint::tracking:
        return "KEEP BOTH CONTROLLERS IN VIEW";
    case Hint::standUp:
        return "PLEASE STAND UP";
    case Hint::armsOut:
        return "STRETCH YOUR ARMS OUT TO THE SIDES";
    case Hint::straight:
        return "STRAIGHTEN BOTH ARMS";
    case Hint::lookAhead:
        return "LOOK STRAIGHT AHEAD";
    case Hint::triggers:
        return "HOLD BOTH TRIGGERS";
    default:
        return "HOLD STILL";
    }
}

namespace {
// The panel, in metres: its size and the colours (linear RGB).
constexpr float panelWidth = 1.2f, panelHeight = .6f;
constexpr Vec3 background{.010f, .012f, .020f}, accent{.62f, .02f, .03f}, white{1, 1, 1}, soft{.78f, .8f, .84f},
    amber{1, .52f, .05f}, green{.1f, .78f, .22f}, grey{.32f, .33f, .38f};
// Points on the panel: x to the player's right and y up from its centre, z
// toward the player.
struct Panel {
    Vec3 centre, right, up, out;
    Vec3 at(float x, float y, float z = 0) const {
        return centre + right * x + up * y + out * z;
    }
    void rect(std::vector<Vertex>& v, float x0, float y0, float x1, float y1, float z, Vec3 color) const {
        const Vertex a{at(x0, y0, z), color}, b{at(x1, y0, z), color}, c{at(x1, y1, z), color},
            d{at(x0, y1, z), color};
        v.insert(v.end(), {a, b, c, a, c, d});
    }
    void line(std::vector<Vertex>& v, float x0, float y0, float x1, float y1, float half, Vec3 color) const {
        const Vec3 p = at(x0, y0, .004f), q = at(x1, y1, .004f);
        const Vec3 along = normalized(q - p) * half, side = normalized(cross(out, q - p)) * half;
        const Vertex a{p - along - side, color}, b{q + along - side, color}, c{q + along + side, color},
            d{p - along + side, color};
        v.insert(v.end(), {a, b, c, a, c, d});
    }
    // Text on one line, `align` 0 from x, .5 centred on it; shrunk to `width`.
    void text(std::vector<Vertex>& v, const char* s, float x, float y, float height, Vec3 color, float align = .5f,
              float width = panelWidth - .1f) const {
        TextStyle style;
        style.height = fittedHeight(s, height, width);
        style.color = color;
        appendText(v, s, at(x - textWidth(s, style.height) * align, y, .004f), right, up, style);
    }
};
// A ring of `segments` facing `view` around `centre`, the first `share` of
// it from the top, clockwise as seen.
void ring(std::vector<Vertex>& v, Vec3 centre, Vec3 view, float inner, float outer, float share, Vec3 color) {
    const Vec3 toward = normalized(view);
    Vec3 x = normalized(cross(worldUp, toward));
    if (length(x) < .5f)
        x = {1, 0, 0};
    const Vec3 y = cross(toward, x);
    constexpr int segments = 40;
    const int count = static_cast<int>(std::ceil(std::clamp(share, 0.f, 1.f) * segments));
    for (int i = 0; i < count; ++i) {
        const float t0 = 2 * pi * i / segments, t1 = 2 * pi * std::min(static_cast<float>(i + 1), share * segments) / segments;
        // From the top (+y), clockwise for a viewer looking along -toward.
        const Vec3 d0 = y * std::cos(t0) + x * std::sin(t0), d1 = y * std::cos(t1) + x * std::sin(t1);
        const Vertex a{centre + d0 * inner, color}, b{centre + d0 * outer, color}, c{centre + d1 * outer, color},
            d{centre + d1 * inner, color};
        v.insert(v.end(), {a, b, c, a, c, d});
    }
}
} // namespace

void appendView(std::vector<Vertex>& out, const View& view, Vec3 viewer) {
    if (view.phase == Phase::idle || !finite(view.panel.position) || !finite(viewer))
        return;
    const auto& q = view.panel.orientation;
    const Panel panel{view.panel.position, q.rotate({1, 0, 0}), q.rotate({0, 1, 0}), q.rotate({0, 0, 1})};
    const float w = panelWidth / 2, h = panelHeight / 2, edge = .008f;
    panel.rect(out, -w, -h, w, h, 0, background);
    panel.rect(out, -w, h - edge, w, h, .002f, accent);
    panel.rect(out, -w, -h, w, -h + edge, .002f, accent);
    panel.rect(out, -w, -h, -w + edge, h, .002f, accent);
    panel.rect(out, w - edge, -h, w, h, .002f, accent);
    if (view.phase == Phase::done) {
        char line[64];
        panel.text(out, "CALIBRATED", 0, .17f, .055f, green);
        std::snprintf(line, sizeof line, "EYE HEIGHT %.2f M", view.result.eyeHeight);
        panel.text(out, line, 0, .06f, .04f, white);
        std::snprintf(line, sizeof line, "ARM LENGTH %.2f M", view.result.armLength);
        panel.text(out, line, 0, -.02f, .04f, white);
        panel.text(out, "SPIDER-MAN NOW HAS YOUR SIZE", 0, -.11f, .032f, soft);
        panel.text(out, "REDO IT IN SETTINGS, SPIDY VR", 0, -.22f, .027f, grey);
    } else {
        panel.text(out, "BODY CALIBRATION", 0, .19f, .05f, white);
        // A figure in a T-pose; its arms turn green while both are in place.
        const float fx = -.44f, fy = -.01f, stroke = .005f;
        const bool both = view.ready[0] && view.ready[1];
        for (int i = 0; i < 8; ++i) {
            const float a0 = 2 * pi * (i + .5f) / 8, a1 = 2 * pi * (i + 1.5f) / 8;
            panel.line(out, fx + .028f * std::cos(a0), fy + .105f + .028f * std::sin(a0), fx + .028f * std::cos(a1),
                       fy + .105f + .028f * std::sin(a1), stroke, white);
        }
        panel.line(out, fx, fy + .075f, fx, fy - .04f, stroke, white);
        panel.line(out, fx - .115f, fy + .055f, fx + .115f, fy + .055f, stroke, both ? green : white);
        panel.line(out, fx, fy - .04f, fx - .045f, fy - .15f, stroke, white);
        panel.line(out, fx, fy - .04f, fx + .045f, fy - .15f, stroke, white);
        const float textLeft = -.31f, column = w - .04f - textLeft;
        panel.text(out, "STAND TALL, LOOK AHEAD", textLeft, .09f, .034f, soft, 0, column);
        panel.text(out, "ARMS STRAIGHT OUT TO THE SIDES", textLeft, .03f, .034f, soft, 0, column);
        panel.text(out, "HOLD BOTH TRIGGERS", textLeft, -.03f, .034f, soft, 0, column);
        const bool holding = view.phase == Phase::holding;
        panel.text(out, holding ? "HOLD STILL" : hintText(view.hint), 0, -.13f, .034f, holding ? green : amber);
        const float barLeft = -.4f, barRight = .4f, barLow = -.195f, barHigh = -.17f, rim = .004f;
        panel.rect(out, barLeft - rim, barLow - rim, barRight + rim, barHigh + rim, .002f, grey);
        panel.rect(out, barLeft, barLow, barRight, barHigh, .003f, background);
        if (view.progress > 0)
            panel.rect(out, barLeft, barLow, barLeft + (barRight - barLeft) * std::min(view.progress, 1.f), barHigh,
                       .004f, green);
        panel.text(out, "PRESS B TO SKIP", 0, -.26f, .026f, grey);
    }
    // Each controller: a ring, green while its arm is in place, filling with the hold.
    for (int i = 0; i < 2; ++i) {
        if (!view.handTracked[i] || !finite(view.grips[i].position))
            continue;
        const Vec3 centre = view.grips[i].position, toward = viewer - centre;
        if (length(toward) < .05f)
            continue;
        const bool done = view.phase == Phase::done;
        ring(out, centre, toward, .07f, .078f, 1, view.ready[i] || done ? green : soft);
        if (!done && view.progress > 0)
            ring(out, centre + normalized(toward) * .002f, toward, .078f, .09f, view.progress, green);
    }
}
} // namespace spidy::body_calibration
