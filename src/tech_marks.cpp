#include "spidy/tech_marks.hpp"
#include "spidy/overlay_text.hpp"
#include "spidy/soft_shapes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace spidy::tech {
namespace {
using soft::perpendicular, soft::softArc, soft::softBar, soft::softTriangle;
constexpr float pi = 3.14159265f, degree = pi / 180;
Vec3 mix(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}
float unit(float v) {
    return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f;
}
// From 0 to 1 as t goes from 0 to 1: easing in and out, or fast and then
// settling, as the game's own HUD moves.
float smooth(float t) {
    t = unit(t);
    return t * t * (3 - 2 * t);
}
float easeOut(float t) {
    const float u = 1 - unit(t);
    return 1 - u * u * u;
}
// A display coming on `t` seconds ago: on, off, on, dim and on again within
// its first tenth of a second; nothing before it.
float flicker(float t) {
    if (!(t >= 0))
        return 0;
    return t < .03f ? 1.f : t < .05f ? .2f : t < .075f ? 1.f : t < .09f ? .55f : 1.f;
}
// A light running round, 0..1 with its brightest at `phase` = 0 (period 1).
float wave(float phase) {
    return .5f + .5f * std::cos(2 * pi * phase);
}
struct Flat {
    float x{}, y{};
    Flat operator+(Flat b) const {
        return {x + b.x, y + b.y};
    }
    Flat operator-(Flat b) const {
        return {x - b.x, y - b.y};
    }
    Flat operator*(float s) const {
        return {x * s, y * s};
    }
};
// The way at `angle` radians anticlockwise from the mark's right, `r` long.
Flat polar(float angle, float r = 1) {
    return {std::cos(angle) * r, std::sin(angle) * r};
}
Flat between(Flat a, Flat b, float t) {
    return a + (b - a) * t;
}
// Level and upright across the line of sight, as the printed marks stand.
struct Facing {
    Vec3 view, right, up;
};
Facing facing(Vec3 at, Vec3 viewer) {
    Facing f;
    f.view = normalized(viewer - at);
    const Vec3 right = cross(Vec3{0, 1, 0}, f.view);
    f.right = length(right) > .1f ? normalized(right) : perpendicular(f.view);
    f.up = cross(f.view, f.right);
    return f;
}
// One mark, flat across the line of sight: its shapes in its own plane (x
// to the viewer's right, y up, metres from its centre), sized, turned and
// moved as a whole by `place`, and laid down by `lit` as the style draws
// them: a soft dark halo under every shape, a faint glow of its colour, then
// the shapes, smooth to their last pixel.
class Hud {
  public:
    Hud(std::vector<Vertex>& out, Vec3 centre, Vec3 right, Vec3 up, Vec3 view, float px)
        : out_(out), centre_(centre), right_(right), up_(up), view_(view), px_(px) {
        place(1);
    }
    // From here on the shapes are `scale` their size and turned by `turn`
    // (radians, anticlockwise) about a centre `moved` from the mark's.
    void place(float scale, float turn = 0, Flat moved = {}) {
        scale_ = scale;
        across_ = right_ * std::cos(turn) + up_ * std::sin(turn);
        along_ = up_ * std::cos(turn) - right_ * std::sin(turn);
        moved_ = moved;
    }
    // How opaque what is drawn from here on is, 0..1.
    void opacity(float o) {
        opacity_ = unit(o);
    }
    // Draws what `draw` draws: its halo, its glow at `glow` (0: none) and the
    // shapes.
    template <class Draw> void lit(float glow, Draw&& draw) {
        if (opacity_ <= 0)
            return;
        pass_ = Pass::halo;
        draw();
        if (glow > 0) {
            pass_ = Pass::glow;
            glow_ = glow;
            draw();
        }
        pass_ = Pass::shape;
        draw();
    }
    // Their glow (0: none) and the shapes, no halo: for what lies on a
    // plate.
    template <class Draw> void glowing(float glow, Draw&& draw) {
        if (opacity_ <= 0)
            return;
        if (glow > 0) {
            pass_ = Pass::glow;
            glow_ = glow;
            draw();
        }
        pass_ = Pass::shape;
        draw();
    }
    // The shapes alone.
    template <class Draw> void plain(Draw&& draw) {
        if (opacity_ <= 0)
            return;
        pass_ = Pass::shape;
        draw();
    }
    // Metres a pixel at the mark.
    float px() const {
        return px_;
    }
    // A ring `half` to either side of `radius`, or its arc from angle `from`
    // to `to` (anticlockwise from the mark's right); `sides` 4 makes it a
    // diamond, 6 a hexagon.
    void arc(Flat at, float radius, float half, Vec3 color, float alpha = 1, float from = 0,
             float to = 2 * pi, int sides = 0) {
        const float r = radius * scale_, h = half * scale_, grow = growth();
        const float inner = std::max(r - h - grow * (pass_ == Pass::halo ? .4f : 1.f), 0.f);
        const int segments =
            sides ? sides : std::clamp(static_cast<int>(3.6f * std::sqrt((r + h) / px_)), 12, 72);
        softArc(out_, world(at), across_, along_, inner, r + h + grow, from, to, tint(color), shade(alpha),
                edge(), segments, pass_ == Pass::halo ? edge() * .6f : edge());
    }
    void disc(Flat at, float radius, Vec3 color, float alpha = 1, int sides = 0) {
        const float r = radius * scale_ + growth();
        const int segments = sides ? sides : std::clamp(static_cast<int>(3.6f * std::sqrt(r / px_)), 12, 48);
        softArc(out_, world(at), across_, along_, 0, r, 0, 2 * pi, tint(color), shade(alpha), edge(),
                segments);
    }
    // A rhombus standing on a corner, `radius` from its middle to each.
    void diamond(Flat at, float radius, Vec3 color, float alpha = 1) {
        disc(at, radius + growth() * .41f / std::max(scale_, 1e-6f), color, alpha, 4);
    }
    // A straight bar from p to q, its ends squared off half its width beyond.
    void bar(Flat p, Flat q, float half, Vec3 color, float alpha = 1) {
        const Vec3 a = world(p), b = world(q);
        if (length(b - a) < 1e-7f)
            return;
        softBar(out_, a, b, view_, half * scale_ + growth(), tint(color), shade(alpha), edge());
    }
    // A thin rule from p to q: its sides soft, its ends square at p and q.
    void rule(Flat p, Flat q, float half, Vec3 color, float alpha = 1) {
        const Vec3 a = world(p), b = world(q), c = tint(color);
        if (length(b - a) < 1e-7f)
            return;
        const float h = half * scale_ + growth(), s = edge(), o = shade(alpha);
        const Vec3 side = normalized(cross(b - a, view_));
        const float across[4] = {-h - s, -h, h, h + s}, alphas[4] = {0, o, o, 0};
        for (int i = 0; i < 3; ++i)
            soft::quad(out_, {a + side * across[i], c, alphas[i]}, {b + side * across[i], c, alphas[i]},
                       {b + side * across[i + 1], c, alphas[i + 1]},
                       {a + side * across[i + 1], c, alphas[i + 1]});
    }
    // A line through `count` points: rules squared off past each joint so
    // they close it, round only at its two ends.
    void trail(const Flat* points, int count, float half, Vec3 color, float alpha = 1) {
        for (int i = 0; i + 1 < count; ++i) {
            const Flat d = points[i + 1] - points[i];
            const float n = std::sqrt(d.x * d.x + d.y * d.y);
            const Flat past = n > 0 ? d * (half / n) : Flat{};
            const Flat from = i ? points[i] - past : points[i];
            const Flat to = i + 2 < count ? points[i + 1] + past : points[i + 1];
            rule(from, to, half, color, alpha);
        }
        if (count > 1) {
            disc(points[0], half, color, alpha);
            disc(points[count - 1], half, color, alpha);
        }
    }
    // A stroke with round ends, as a pen draws it: strokes join smoothly.
    void stroke(Flat p, Flat q, float half, Vec3 color, float alpha = 1) {
        const Vec3 a = world(p), b = world(q), c = tint(color);
        const float h = half * scale_ + growth(), s = edge(), o = shade(alpha);
        if (length(b - a) > 1e-7f) {
            const Vec3 side = normalized(cross(b - a, view_));
            const float across[4] = {-h - s, -h, h, h + s}, alphas[4] = {0, o, o, 0};
            for (int i = 0; i < 3; ++i)
                soft::quad(out_, {a + side * across[i], c, alphas[i]}, {b + side * across[i], c, alphas[i]},
                           {b + side * across[i + 1], c, alphas[i + 1]},
                           {a + side * across[i + 1], c, alphas[i + 1]});
        }
        // The round ends: a half disc each, so nothing is laid twice.
        const Vec3 way = length(b - a) > 1e-7f ? normalized(b - a) : across_;
        const Vec3 side = normalized(cross(way, view_));
        for (const auto& [end, out] : {std::pair{a, -way}, std::pair{b, way}}) {
            constexpr int pieces = 3;
            for (int i = 0; i < pieces; ++i) {
                const float t0 = pi * static_cast<float>(i) / pieces;
                const float t1 = pi * static_cast<float>(i + 1) / pieces;
                const Vec3 d0 = side * std::cos(t0) + out * std::sin(t0);
                const Vec3 d1 = side * std::cos(t1) + out * std::sin(t1);
                const Vertex p0{end + d0 * h, c, o}, p1{end + d1 * h, c, o};
                out_.insert(out_.end(), {{end, c, o}, p0, p1});
                soft::quad(out_, p0, p1, {end + d1 * (h + s), c, 0}, {end + d0 * (h + s), c, 0});
            }
        }
    }
    void triangle(Flat a, Flat b, Flat c, Vec3 color, float alpha = 1) {
        softTriangle(out_, {world(a), world(b), world(c)}, view_, tint(color), shade(alpha), growth(),
                     edge());
    }
    // A plate of colour: hard edged, under what is drawn on it, no halo.
    void fill(Flat a, Flat b, Flat c, Flat d, Vec3 color, float alpha) {
        if (pass_ != Pass::shape)
            return;
        const Vertex p{world(a), color, alpha * opacity_}, q{world(b), color, alpha * opacity_},
            r{world(c), color, alpha * opacity_}, s{world(d), color, alpha * opacity_};
        soft::quad(out_, p, q, r, s);
    }
    // A star of `points` spikes from `inner` out to `outer`, the first up.
    void star(Flat at, float inner, float outer, int points, Vec3 color, float alpha = 1) {
        disc(at, inner, color, alpha);
        const float step = 2 * pi / static_cast<float>(points);
        for (int i = 0; i < points; ++i) {
            const float middle = pi / 2 + step * static_cast<float>(i);
            triangle(at + polar(middle - step * .5f, inner * .97f), at + polar(middle, outer),
                     at + polar(middle + step * .5f, inner * .97f), color, alpha);
        }
    }
    // Lettering upright, in the overlay's stroke font, from the left end of
    // its baseline: square strokes, the font's corners chamfered, their
    // sides soft. No glow: it would blur the strokes.
    void text(std::string_view words, Flat origin, float height, Vec3 color, float alpha = 1,
              float weight = .1f) {
        if (pass_ == Pass::glow)
            return;
        const size_t from = out_.size();
        TextStyle style;
        style.height = height * scale_;
        style.weight = weight + 2 * (pass_ == Pass::halo ? 1.1f * px_ : 0.f) / std::max(style.height, 1e-6f);
        style.color = tint(color);
        style.soft = pass_ == Pass::halo ? 1.6f * px_ : .85f * px_;
        appendText(out_, words, world(origin), across_, along_, style);
        const float o = shade(alpha);
        for (size_t i = from; i < out_.size(); ++i)
            out_[i].alpha *= o;
    }

  private:
    enum class Pass : std::uint8_t { halo, glow, shape };
    Vec3 world(Flat at) const {
        return centre_ + across_ * (at.x * scale_) + along_ * (at.y * scale_) + right_ * moved_.x +
               up_ * moved_.y;
    }
    Vec3 tint(Vec3 color) const {
        return pass_ == Pass::halo ? navy : color;
    }
    float shade(float alpha) const {
        return unit(alpha) * opacity_ * (pass_ == Pass::halo ? .55f : pass_ == Pass::glow ? glow_ : 1.f);
    }
    // How far a pass lays past the shape, and the width of its fading edge.
    float growth() const {
        return pass_ == Pass::halo ? 1.3f * px_ : pass_ == Pass::glow ? 2.6f * px_ : 0.f;
    }
    float edge() const {
        return pass_ == Pass::halo ? 1.9f * px_ : pass_ == Pass::glow ? 3.2f * px_ : .85f * px_;
    }
    std::vector<Vertex>& out_;
    Vec3 centre_, right_, up_, view_, across_{}, along_{};
    float px_, scale_ = 1, opacity_ = 1, glow_{};
    Flat moved_{};
    Pass pass_ = Pass::shape;
};
// The menus' corner brackets round a box `half` wide and high about `at`,
// each arm `arm` long.
void brackets(Hud& hud, Flat at, float halfWidth, float halfHeight, float arm, float half, Vec3 color,
              float alpha = 1) {
    for (const float sx : {-1.f, 1.f})
        for (const float sy : {-1.f, 1.f}) {
            const Flat corner = at + Flat{sx * halfWidth, sy * halfHeight};
            hud.bar(corner, corner - Flat{sx * arm, 0}, half, color, alpha);
            hud.bar(corner, corner - Flat{0, sy * arm}, half, color, alpha);
        }
}
} // namespace

void appendAimMarker(std::vector<Vertex>& out, const AimMarker& marker, Vec3 viewer, float pixelAngle) {
    const float distance = length(viewer - marker.point);
    const float opacity = std::isfinite(marker.opacity) ? std::clamp(marker.opacity, 0.f, 1.f) : 0.f;
    if (!finite(marker.point) || !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0 ||
        !(distance >= .3f && distance <= 1000) || opacity <= 0)
        return;
    const Facing f = facing(marker.point, viewer);
    // Metres per pixel at the marker, and how far the grip has closed toward a shot.
    const float px = pixelAngle * distance;
    const float squeeze = std::isfinite(marker.squeeze) ? std::clamp(marker.squeeze / .65f, 0.f, 1.f) : 0.f;
    const Vec3 color = hands[marker.hand ? 1 : 0], light = mix(color, {1, 1, 1}, .55f);
    Hud hud(out, marker.point, f.right, f.up, f.view, px);
    hud.opacity(opacity);
    switch (marker.kind) {
    case AimMark::anchor: {
        // Four arcs round a diamond, the gaps on the four sides, a tick out
        // from each: a scope's reticle, tightening as the grip closes.
        const float r = 10.5f * px * (1 - .4f * squeeze);
        hud.lit(.22f, [&] {
            for (int i = 0; i < 4; ++i) {
                const float middle = pi / 4 + pi / 2 * static_cast<float>(i);
                hud.arc({}, r, px, color, 1, middle - 31 * degree, middle + 31 * degree);
                const Flat way = polar(pi / 2 * static_cast<float>(i));
                hud.bar(way * (r * 1.22f), way * (r * 1.5f), .8f * px, color);
            }
            hud.diamond({}, 2.4f * px, light);
        });
        break;
    }
    case AimMark::air: {
        // Faint: short dashes round a dot, nothing glowing.
        const float r = 8.5f * px * (1 - .4f * squeeze);
        constexpr int dashes = 8;
        hud.lit(0, [&] {
            for (int i = 0; i < dashes; ++i) {
                const float from = 2 * pi * static_cast<float>(i) / dashes + pi / dashes * .5f;
                hud.arc({}, r, .8f * px, color, .75f, from, from + 2 * pi / dashes * .42f);
            }
            hud.disc({}, 1.1f * px, color, .6f);
        });
        break;
    }
    case AimMark::blocked:
        // An open red cross, either hand.
        hud.lit(.2f, [&] {
            for (int i = 0; i < 4; ++i) {
                const Flat way = polar(pi / 4 + pi / 2 * static_cast<float>(i));
                hud.bar(way * (2.2f * px), way * (6.f * px), .95f * px, red, .95f);
            }
        });
        break;
    case AimMark::target: {
        // Three arcs round the target, turning, each with a claw in at it
        // and its ends ticked; inside them a ring of dashes turning the
        // other way. While the ring locks on it closes in from wider; the
        // right hand's is a little wider and turned between the left's.
        const float lock = std::isfinite(marker.lock) ? std::clamp(marker.lock, 0.f, 1.f) : 1.f;
        const float closing = smooth(lock);
        const float size = std::max(std::isfinite(marker.radius) ? marker.radius * 1.15f : 0.f, 13 * px);
        const float r = size * (marker.hand ? 1.15f : 1.f) * (1 - .25f * squeeze) * (1 + .8f * (1 - closing));
        const float spin = (std::isfinite(marker.spin) ? marker.spin : 0.f) + (marker.hand ? pi / 3 : 0.f);
        const float half = 1.1f * px, claw = std::clamp(r * .18f, 4 * px, 9 * px);
        constexpr float span = 72 * degree;
        const float inner = r * .64f;
        const int dashes = std::clamp(static_cast<int>(2 * pi * inner / (7 * px)), 6, 24);
        hud.lit(.25f, [&] {
            for (int i = 0; i < 3; ++i) {
                const float middle = pi / 2 + spin + 2 * pi * static_cast<float>(i) / 3;
                hud.arc({}, r, half, color, 1, middle - span / 2, middle + span / 2);
                for (const float end : {middle - span / 2, middle + span / 2})
                    hud.bar(polar(end, r - 3.6f * px), polar(end, r - half), .7f * px, color);
                const Flat way = polar(middle), across{-way.y, way.x};
                const Flat base = way * (r - half);
                hud.triangle(base + across * (claw * .5f), way * (r - half - claw),
                             base - across * (claw * .5f), color);
            }
            hud.diamond({}, 2 * px, light);
        });
        // The inner ring is faint: no halo or glow of its own.
        hud.plain([&] {
            for (int i = 0; i < dashes; ++i) {
                const float from = -1.6f * spin + 2 * pi * static_cast<float>(i) / static_cast<float>(dashes);
                hud.arc({}, inner, .55f * px, color, .55f, from,
                        from + 2 * pi / static_cast<float>(dashes) * .45f);
            }
        });
        break;
    }
    }
}

void appendSlowMotionMeter(std::vector<Vertex>& out, const SlowMotionView& view, Pose hand, Vec3 viewer,
                           float pixelAngle, float seconds) {
    const float opacity = unit(view.meter);
    if (opacity <= 0 || !finite(hand.position) || !finite(viewer) || !std::isfinite(pixelAngle) ||
        pixelAngle <= 0)
        return;
    // Over the back of the wrist, toward the forearm, like a watch's face.
    const Vec3 c = hand.position + hand.orientation.rotate({0, .045f, .065f});
    const float distance = length(viewer - c);
    if (!finite(c) || !(distance > .08f && distance < 3))
        return;
    const Facing f = facing(c, viewer);
    const float px = pixelAngle * distance;
    const float focus = unit(view.focus), blend = unit(view.blend), warning = unit(view.warning);
    const float t = std::isfinite(seconds) ? seconds : 0.f;
    // In slow motion it brightens and breathes; a warning flashes it red.
    const float pulse = view.active ? .5f + .5f * std::sin(t * 6.f) : 0.f;
    const float flash = warning * (.55f + .45f * std::cos(t * 28.f));
    const Vec3 lit = mix(mix(cyan, line, blend * (.25f + .35f * pulse)), red, flash);
    const Vec3 rim = mix(line, red, flash);
    constexpr float face = .026f, track = .0185f, width = .0021f;
    constexpr int segments = 20;
    Hud hud(out, c, f.right, f.up, f.view, px);
    hud.opacity(opacity);
    // The face: a translucent blue disc under a thin bezel, ticked every
    // fifteen degrees and longer at the quarters.
    hud.plain([&] { hud.disc({}, face, panel, .5f); });
    hud.lit(0, [&] { hud.arc({}, face, .0005f, rim, .85f); });
    hud.plain([&] {
        for (int i = 0; i < 24; ++i) {
            const Flat way = polar(2 * pi * static_cast<float>(i) / 24);
            hud.rule(way * (i % 6 ? .0233f : .0222f), way * .0246f, .0003f, rim, i % 6 ? .45f : .9f);
        }
    });
    // Focus: segments round from the top, clockwise; spent ones dim. The one
    // being spent shows its share.
    const float step = 2 * pi / segments, gap = 4 * degree;
    hud.plain([&] {
        for (int i = 0; i < segments; ++i) {
            const float start = pi / 2 - step * static_cast<float>(i) - gap / 2;
            hud.arc({}, track, width, dim, .55f, start - step + gap, start);
        }
    });
    hud.glowing(view.active ? .4f : .18f, [&] {
        for (int i = 0; i < segments; ++i) {
            const float share = std::clamp(focus * segments - static_cast<float>(i), 0.f, 1.f);
            if (share <= .02f)
                continue;
            const float start = pi / 2 - step * static_cast<float>(i) - gap / 2;
            hud.arc({}, track, width, lit, 1, start - (step - gap) * share, start);
        }
    });
    // An hourglass in the middle, its sand what focus is left: in the top
    // bulb, and run down into the bottom one.
    constexpr float w = .0058f, h = .0085f, neck = .0007f;
    hud.plain([&] {
        const Flat corners[] = {{-w, h}, {w, h}, {neck, 0}, {w, -h}, {-w, -h}, {-neck, 0}, {-w, h}};
        hud.trail(corners, 7, .00045f, rim, .9f);
    });
    hud.plain([&] {
        const float top = std::sqrt(focus), bottom = std::sqrt(1 - focus);
        const float level = neck + (h * .86f - neck) * top, pile = -h * .86f + (h * .86f - neck) * bottom;
        if (top > .05f)
            hud.triangle({-w * .78f * top, level}, {w * .78f * top, level}, {0, neck * 1.5f}, lit, .9f);
        if (bottom > .05f)
            hud.triangle({-w * .78f, -h * .86f}, {w * .78f, -h * .86f}, {0, pile}, lit, .9f);
    });
    // In slow motion a sweep goes round outside the face.
    if (view.active) {
        const float at = -t * 2.6f;
        hud.lit(.45f, [&] {
            hud.arc({}, .0296f, .0006f, lit, .3f);
            hud.arc({}, .0296f, .0011f, lit, .9f, at, at + 50 * degree);
        });
    }
}

void appendPadLayerSign(std::vector<Vertex>& out, Pose hand, Vec3 viewer, float pixelAngle) {
    if (!finite(hand.position) || !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0)
        return;
    const Vec3 c = hand.position + hand.orientation.rotate({0, .07f, .065f});
    const float distance = length(viewer - c);
    if (!finite(c) || !(distance > .08f && distance < 3))
        return;
    const Facing f = facing(c, viewer);
    const float px = pixelAngle * distance;
    // A tag as the knockout's: a plate ruled above and below, an amber bar
    // at its left, the title cyan over the hint.
    constexpr float title = .014f, hint = .0072f, gap = .005f, margin = .006f;
    const float wide = std::max(textWidth(padLayerTitle, title), textWidth(padLayerHint, hint));
    const float x0 = wide * -.5f - margin, x1 = -x0;
    const float y0 = (title + gap + hint) * -.5f - margin, y1 = -y0;
    Hud hud(out, c, f.right, f.up, f.view, px);
    hud.plain([&] { hud.fill({x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, panel, .6f); });
    hud.lit(0, [&] {
        const float half = std::max(title * .025f, .7f * px);
        hud.bar({x0, y1}, {x1, y1}, half, line, .9f);
        hud.bar({x0, y0}, {x1, y0}, half, line, .9f);
        hud.bar({x0, y0}, {x0, y1}, title * .07f, amber);
    });
    hud.plain([&] {
        hud.text(padLayerTitle, {textWidth(padLayerTitle, title) * -.5f, y1 - margin - title}, title, cyan, 1,
                 .13f);
        hud.text(padLayerHint, {textWidth(padLayerHint, hint) * -.5f, y0 + margin}, hint, line, 1, .12f);
    });
}
void appendImpact(std::vector<Vertex>& out, const ImpactView& impact, Vec3 viewer, float pixelAngle) {
    const float distance = length(viewer - impact.point);
    if (!finite(impact.point) || !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0 ||
        !std::isfinite(impact.age) || impact.age < 0 || impact.age > ImpactView::seconds ||
        !(distance >= .12f && distance <= 60))
        return;
    const Facing f = facing(impact.point, viewer);
    const float px = pixelAngle * distance, age = impact.age;
    const float strength = unit(impact.strength);
    const bool finisher = impact.kind == BlowKind::Finisher, heavy = impact.kind != BlowKind::Hit;
    // How far it reaches, as the printed burst: a jab's the size of the fist,
    // a finisher's of his chest.
    const float reach = finisher ? .26f : heavy ? .16f : .07f + .08f * strength;
    const Vec3 color = finisher ? red : heavy ? amber : impact.called ? cyan : line;
    Hud hud(out, impact.point, f.right, f.up, f.view, px);
    // The first instant: a bright diamond where the fist met him.
    if (age < .08f) {
        hud.opacity(1 - age / .08f);
        hud.lit(.45f, [&] { hud.diamond({}, reach * .13f, mix(color, {1, 1, 1}, .6f)); });
    }
    // The hit marker: four strokes on the diagonals springing out from the
    // blow, then fading.
    if (const float fading = 1 - smooth((age - .16f) / .2f); fading > 0) {
        const float spring = easeOut(age / .14f);
        const float from = reach * (.18f + .3f * spring), to = reach * (.42f + .4f * spring);
        const float half = std::max(reach * (finisher ? .028f : .034f), 1.1f * px);
        hud.opacity(fading);
        hud.lit(.3f, [&] {
            for (int i = 0; i < 4; ++i) {
                const Flat way = polar(pi / 4 + pi / 2 * static_cast<float>(i));
                hud.bar(way * from, way * to, half, color);
            }
        });
    }
    // A ring going out from it; a finisher's two, and a hexagon turning. A
    // finisher's reaches no farther than a jab's does for its size: an arm's
    // length away it must not fill the view.
    for (int k = 0; k < (finisher ? 2 : 1); ++k) {
        const float t = (age - .07f * static_cast<float>(k)) / (finisher ? .36f : .28f);
        if (t <= 0 || t >= 1)
            continue;
        hud.opacity((1 - t) * (1 - t));
        const float r = reach * (.3f + (finisher ? .55f + .2f * static_cast<float>(k) : .8f) * easeOut(t));
        hud.lit(.25f, [&] { hud.arc({}, r, std::max(reach * .014f * (1 - t), .7f * px), color); });
    }
    if (finisher && age < .4f) {
        const float t = age / .4f;
        hud.opacity(1 - smooth(t));
        hud.place(.8f + .3f * easeOut(t), t * pi / 6);
        const float half = std::max(reach * .008f, .7f * px);
        hud.lit(.3f, [&] { hud.arc({}, reach * .42f, half, amber, 1, 0, 2 * pi, 6); });
        hud.place(1);
    }
    // A called blow: a ring of ticks turning out round it, the call's cyan.
    if (impact.called && age < .4f) {
        const float t = age / .4f;
        hud.opacity(1 - smooth(t));
        hud.place(1, t * pi / 4);
        hud.lit(.3f, [&] {
            for (int i = 0; i < 12; ++i) {
                const Flat way = polar(2 * pi * static_cast<float>(i) / 12);
                const float r = reach * (.62f + .3f * easeOut(t / .5f));
                hud.bar(way * r, way * (r + reach * .1f), std::max(reach * .012f, .8f * px), cyan);
            }
        });
        hud.place(1);
    }
    // The count, from the second blow on: on a callout, up and to the right
    // of the blow, a leader drawn out to a rule, "X3" over it and the
    // game's COMBO over that; it flickers on, rises a little and fades.
    if (impact.count < 2)
        return;
    const float height = (finisher ? .05f : .036f) * std::clamp(distance / .7f, .8f, 2.5f);
    const std::string count = "X" + std::to_string(std::min(impact.count, 999));
    const float width = textWidth(count, height);
    const Flat from = polar(pi / 4, reach * .9f), elbow = from + Flat{1, 1} * (height * .8f);
    const Flat end = elbow + Flat{width + height * .5f, 0};
    const float drawn = easeOut(age / .09f), ruled = easeOut((age - .07f) / .09f);
    const float fading = 1 - smooth((age - .58f) / .15f);
    hud.place(1, 0, {0, .04f * age});
    hud.opacity(fading);
    const float half = std::max(height * .028f, .8f * px);
    hud.lit(0, [&] {
        hud.bar(from, between(from, elbow, drawn), half, line, .9f);
        if (ruled > 0)
            hud.bar(elbow, between(elbow, end, ruled), half, line, .9f);
    });
    hud.opacity(fading * flicker(age - .12f));
    hud.lit(0, [&] {
        hud.text(count, elbow + Flat{height * .25f, height * .3f}, height, line, 1, .12f);
        hud.text("COMBO", elbow + Flat{height * .25f, height * 1.55f}, height * .36f, cyan, 1, .14f);
    });
}

void appendCue(std::vector<Vertex>& out, const CueView& cue, Vec3 viewer, float pixelAngle) {
    const float distance = length(viewer - cue.point);
    if (!finite(cue.point) || !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0 ||
        !std::isfinite(cue.left) || cue.left <= 0 || !std::isfinite(cue.age) || cue.age < 0 ||
        !(distance >= .15f && distance <= 60))
        return;
    const Facing f = facing(cue.point, viewer);
    const bool called = cue.way != CueWay::None, finisher = called && cue.finisher;
    // The ring's radius, as the printed mark's: an arm's length away a hand
    // wide; farther off it keeps most of its size on screen.
    const float size = (finisher ? .13f : .1f) * std::clamp(distance / .9f, 1.f, 5.f);
    const float age = cue.age, px = pixelAngle * distance;
    Hud hud(out, cue.point, f.right, f.up, f.view, px);
    // It snaps on from a little wider, flickering.
    const float in = easeOut(age / .12f);
    hud.place(1.25f - .25f * in);
    hud.opacity(smooth(age / .06f) * flicker(age));
    if (!called) {
        // Nothing called: four arcs on the diagonals, a scope's. A lock's
        // are bolder and cyan, with a pointer in at each side and a ring of
        // dashes turning round them.
        const Vec3 color = cue.locked ? mix(line, cyan, .6f) : line;
        const float half = size * (cue.locked ? .016f : .011f);
        hud.lit(cue.locked ? .22f : 0, [&] {
            for (int i = 0; i < 4; ++i) {
                const float middle = pi / 4 + pi / 2 * static_cast<float>(i);
                hud.arc({}, size, half, color, cue.locked ? 1.f : .85f, middle - 20 * degree,
                        middle + 20 * degree);
            }
            for (int i = 0; cue.locked && i < 4; ++i) {
                const Flat way = polar(pi / 2 * static_cast<float>(i)), across{-way.y, way.x};
                hud.triangle(way * (size * 1.04f) + across * (size * .075f), way * (size * .88f),
                             way * (size * 1.04f) - across * (size * .075f), color);
            }
        });
        if (cue.locked) {
            constexpr int dashes = 24;
            hud.lit(0, [&] {
                for (int i = 0; i < dashes; ++i) {
                    const float from = age * .6f + 2 * pi * static_cast<float>(i) / dashes;
                    hud.arc({}, size * 1.2f, size * .008f, color, .45f, from, from + 2 * pi / dashes * .5f);
                }
            });
        }
        return;
    }
    const Vec3 tone = finisher ? amber : cyan, ring = finisher ? amber : line;
    // The flow's time left: segments round the ring, running down clockwise
    // from the top a segment at a time, over the dim ones spent.
    constexpr int segments = 24;
    const float step = 2 * pi / segments, gap = 3.5f * degree;
    const int left = static_cast<int>(std::ceil(std::clamp(cue.left, 0.f, 1.f) * segments));
    hud.lit(.2f, [&] {
        for (int i = 0; i < segments; ++i) {
            const float start = pi / 2 - step * static_cast<float>(i) - gap / 2;
            const bool on = i < left;
            hud.arc({}, size, size * (on ? .028f : .012f), on ? ring : dim, on ? 1.f : .6f,
                    start - step + gap, start);
        }
    });
    // A finisher's: a red ring outside it with six diamonds on it.
    if (finisher)
        hud.lit(.3f, [&] {
            hud.arc({}, size * 1.16f, size * .008f, red, .9f);
            for (int i = 0; i < 6; ++i)
                hud.diamond(polar(pi / 2 + pi / 3 * static_cast<float>(i), size * 1.16f), size * .045f, red);
        });
    // Three chevrons pointing the way the fist must go, from outside the ring
    // on the side it comes from to his middle, a light running along them
    // the way they point.
    Flat way{0, -1};
    switch (cue.way) {
    case CueWay::Left:
        way = {-1, 0};
        break;
    case CueWay::Right:
        way = {1, 0};
        break;
    case CueWay::Up:
        way = {0, 1};
        break;
    default:
        break;
    }
    const Flat across{-way.y, way.x};
    const float running = std::fmod(age * 1.8f, 1.f);
    const float half = size * (finisher ? .05f : .045f), arm = size * .32f;
    hud.lit(.35f, [&] {
        for (int k = 0; k < 3; ++k) {
            const Flat tip = way * (size * (-.85f + .5f * static_cast<float>(k)));
            const float light = .4f + .6f * wave(running - static_cast<float>(k) / 3);
            hud.stroke(tip - way * arm + across * arm, tip, half, tone, light);
            hud.stroke(tip - way * arm - across * arm, tip, half, tone, light);
        }
    });
}

void appendKnockout(std::vector<Vertex>& out, const KnockoutView& knockout, Vec3 viewer, float pixelAngle) {
    const float distance = length(viewer - knockout.point);
    if (!finite(knockout.point) || !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0 ||
        !std::isfinite(knockout.age) || knockout.age < 0 || knockout.age >= KnockoutView::seconds ||
        !(distance >= .15f && distance <= 60))
        return;
    const Facing f = facing(knockout.point, viewer);
    const float age = knockout.age, px = pixelAngle * distance;
    const float height = .036f * std::clamp(distance / .7f, .8f, 2.5f), r = height * 1.2f;
    // Gone a little before its time is up.
    const float fading = 1 - smooth((age - .48f) / .16f);
    Hud hud(out, knockout.point, f.right, f.up, f.view, px);
    // A reticle closing on where he stood, and a cross in it once it has.
    const float close = easeOut(age / .16f);
    hud.opacity(fading);
    hud.lit(.3f, [&] {
        for (int i = 0; i < 4; ++i) {
            const float middle = pi / 4 + pi / 2 * static_cast<float>(i);
            hud.arc({}, r * (1.7f - .7f * close), r * .045f, red, 1, middle - 24 * degree,
                    middle + 24 * degree);
        }
    });
    hud.opacity(fading * flicker(age - .14f));
    hud.lit(.3f, [&] {
        for (int i = 0; i < 4; ++i) {
            const Flat way = polar(pi / 4 + pi / 2 * static_cast<float>(i));
            hud.bar(way * (r * .18f), way * (r * .58f), r * .045f, red);
        }
    });
    // A tag to the right: a plate drawn out from the reticle, ruled above and
    // below, an amber bar at its left, "KO" on it.
    const float drawn = easeOut((age - .1f) / .12f);
    if (drawn <= 0)
        return;
    const float width = textWidth("KO", height) + height * 1.1f, x0 = r * 1.12f, x1 = x0 + width * drawn;
    const float y0 = -height * .85f, y1 = height * .85f;
    hud.opacity(fading);
    hud.plain([&] { hud.fill({x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, panel, .6f); });
    hud.lit(0, [&] {
        const float half = std::max(height * .025f, .7f * px);
        hud.bar({x0, y1}, {x1, y1}, half, line, .9f);
        hud.bar({x0, y0}, {x1, y0}, half, line, .9f);
        hud.bar({x0, y0}, {x0, y1}, height * .07f, amber);
    });
    hud.opacity(fading * flicker(age - .2f));
    hud.plain([&] { hud.text("KO", {x0 + height * .55f, -height * .5f}, height, line, 1, .13f); });
}

void appendBlock(std::vector<Vertex>& out, const BlockView& block, Vec3 viewer, float pixelAngle) {
    const float distance = length(viewer - block.point);
    if (!finite(block.point) || !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0 ||
        !std::isfinite(block.age) || block.age < 0 || block.age >= BlockView::seconds ||
        !(distance >= .12f && distance <= 60))
        return;
    const Facing f = facing(block.point, viewer);
    const float age = block.age, px = pixelAngle * distance;
    const float strength = unit(block.strength);
    // The tag keeps a readable size at a distance, as the count's does (`height` is its unit); a weak blow's
    // shield is the smaller.
    const float reading = std::clamp(distance / .7f, .8f, 2.5f);
    const float height = .03f * reading;
    const float half = .05f * reading * (.85f + .3f * strength);
    // He swings back at this blow: the chevron, the tag's bar and the diamonds are red, not amber.
    const bool counter = block.counterAt >= 2 && block.streak >= block.counterAt;
    const Vec3 accent = counter ? red : amber;
    // Gone a little before its time is up.
    const float fading = 1 - smooth((age - .5f) / .17f);
    Hud hud(out, block.point, f.right, f.up, f.view, px);
    // The first instant: a bright diamond where the fist met him.
    if (age < .08f) {
        hud.opacity(1 - age / .08f);
        hud.lit(.45f, [&] { hud.diamond({}, half * .26f, mix(line, {1, 1, 1}, .6f)); });
    }
    // A hexagon going out from it, as a field flares where a blow strikes it.
    if (const float t = age / .3f; t > 0 && t < 1) {
        hud.opacity((1 - t) * (1 - t));
        const float r = half * (.45f + 1.15f * easeOut(t));
        hud.lit(.25f,
                [&] { hud.arc({}, r, std::max(half * .035f * (1 - t), .7f * px), line, 1, 0, 2 * pi, 6); });
    }
    // The shield: thin lines on a translucent blue plate, drawn in as the display comes on, a chevron in it.
    const float w = half * .82f;
    const Flat shape[8] = {
        {-w, half}, {w, half}, {w, 0}, {w * .7f, -half * .6f}, {0, -half * 1.12f}, {-w * .7f, -half * .6f},
        {-w, 0},    {-w, half}};
    hud.opacity(fading * easeOut(age / .1f));
    hud.plain([&] {
        hud.fill(shape[6], shape[2], shape[1], shape[0], panel, .55f);
        hud.fill(shape[5], shape[3], shape[2], shape[6], panel, .55f);
        hud.fill(shape[5], shape[4], shape[3], shape[3], panel, .55f);
    });
    hud.opacity(fading * easeOut(age / .1f) * flicker(age - .02f));
    hud.lit(.3f, [&] {
        hud.trail(shape, 8, std::max(half * .035f, .8f * px), line, .95f);
        const Flat chevron[3] = {{-w * .5f, half * .5f}, {0, half * -.1f}, {w * .5f, half * .5f}};
        hud.trail(chevron, 3, std::max(half * .06f, .9f * px), accent);
    });
    // The callout, as the combo count's is: a leader out from the shield's shoulder to a tag - a panel ruled
    // above and below, a bar at its left - with a diamond in it for each blocked blow he takes before he
    // swings back: lit for those he has taken, a thin outline for those to come. No lettering (the comic
    // style has the word): the shield says it, and with no count to show there is no tag at all.
    const int pips = block.counterAt >= 2 && block.counterAt <= BlockView::maxPips && block.streak >= 1
                         ? block.counterAt
                         : 0;
    if (pips == 0)
        return;
    const float pipRadius = height * .32f, pipStep = pipRadius * 3.1f, pad = height * .55f;
    const Flat from{w * .92f, half * .98f}, elbow = from + Flat{1, 1} * (height * .95f);
    const float tagHigh = height * 1.5f, x0 = elbow.x;
    const float wide =
        pad + pipRadius * 1.1f + pipStep * static_cast<float>(pips - 1) + pipRadius * 1.1f + pad * .7f;
    const float lead = easeOut(age / .08f), grown = easeOut((age - .06f) / .12f);
    hud.opacity(fading);
    const float half1 = std::max(height * .025f, .8f * px);
    hud.lit(0, [&] { hud.bar(from, between(from, elbow, lead), half1, line, .9f); });
    if (grown <= 0)
        return;
    const float x1 = x0 + wide * grown, y0 = elbow.y - tagHigh * .5f, y1 = elbow.y + tagHigh * .5f;
    hud.plain([&] { hud.fill({x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, panel, .6f); });
    hud.lit(0, [&] {
        hud.bar({x0, y1}, {x1, y1}, half1, line, .9f);
        hud.bar({x0, y0}, {x1, y0}, half1, line, .9f);
        hud.bar({x0, y0}, {x0, y1}, height * .07f, accent);
    });
    // The diamonds flicker on once the plate is out.
    hud.opacity(fading * flicker(age - .16f));
    const float firstPip = x0 + pad + pipRadius * 1.1f;
    hud.lit(0, [&] {
        for (int i = 0; i < pips; ++i) {
            const Flat at{firstPip + pipStep * static_cast<float>(i), elbow.y};
            if (i < block.streak || counter)
                hud.diamond(at, pipRadius * 1.1f, accent);
            else
                hud.arc(at, pipRadius, std::max(pipRadius * .26f, .8f * px), line, .9f, 0, 2 * pi, 4);
        }
    });
}

namespace {
Vec3 senseColor(SenseAnswer answer) {
    return answer == SenseAnswer::Dodge ? red : answer == SenseAnswer::Jump ? amber : cyan;
}
// Spider-Sense's lines about the mark's centre, `s` its size, as the game
// draws its own round the hero's head: five wavy strokes fanned up and out
// from under them, the middle one the longest, the waves running out along
// them; `reach` how far out they go.
void senseLines(Hud& hud, float s, Vec3 color, float age, float reach = 1, float alpha = 1) {
    static constexpr float angles[5] = {-56, -28, 0, 28, 56};
    const Flat from{0, -.62f * s};
    constexpr int pieces = 8;
    for (int i = 0; i < 5; ++i) {
        const float a = angles[i] * degree;
        const Flat way{std::sin(a), std::cos(a)}, across{way.y, -way.x};
        const float r0 = .42f * s * reach, r1 = (i == 2 ? 1.18f : .98f) * s * reach;
        Flat points[pieces + 1];
        for (int k = 0; k <= pieces; ++k) {
            const float t = static_cast<float>(k) / pieces;
            const float swing = std::sin(2 * pi * (1.25f * t - age * 1.6f) + static_cast<float>(i));
            points[k] = from + way * (r0 + (r1 - r0) * t) + across * (.07f * s * swing);
        }
        hud.trail(points, pieces + 1, .038f * s, color, alpha);
    }
}
bool senseShows(SenseAnswer answer, float age, float left, float broken, float pixelAngle) {
    return answer != SenseAnswer::None && std::isfinite(age) && age >= 0 && std::isfinite(left) &&
           std::isfinite(broken) && broken < SenseView::brokenSeconds && std::isfinite(pixelAngle) &&
           pixelAngle > 0;
}
// One Spider-Sense mark: a ring of dashes closing on the blow, amber with
// pointers in at its last notch; its lines (a jump's under two chevrons);
// or, broken by a counter, the lines thrown out and a ring going out, fading.
void senseMark(Hud& hud, SenseAnswer answer, float s, float left, bool timed, float age, float broken) {
    const Vec3 color = senseColor(answer);
    if (broken >= 0) {
        const float t = broken / SenseView::brokenSeconds;
        hud.opacity(1 - smooth(t));
        hud.lit(.4f, [&] {
            senseLines(hud, s, color, age, 1 + .9f * easeOut(t));
            hud.arc({}, s * (1.05f + 1.1f * easeOut(t)), s * .03f * (1 - t) + .001f, line);
        });
        return;
    }
    const float in = easeOut(age / .14f);
    hud.place(1.3f - .3f * in);
    hud.opacity(smooth(age / .06f) * flicker(age));
    if (timed) {
        const float l = unit(left);
        const bool now = l <= 1.f / 8 + 1e-4f;
        const float r = s * (1.02f + .55f * l);
        constexpr int dashes = 24;
        hud.lit(now ? .4f : 0, [&] {
            for (int i = 0; i < dashes; ++i) {
                const float from = age * .5f + 2 * pi * static_cast<float>(i) / dashes;
                hud.arc({}, r, s * (now ? .035f : .022f), now ? amber : line, now ? 1.f : .8f, from,
                        from + 2 * pi / dashes * .55f);
            }
            for (int i = 0; now && i < 4; ++i) {
                const Flat way = polar(pi / 4 + pi / 2 * static_cast<float>(i)), side{-way.y, way.x};
                hud.triangle(way * (r * 1.2f) + side * (s * .09f), way * (r * 1.06f),
                             way * (r * 1.2f) - side * (s * .09f), amber);
            }
        });
    }
    hud.lit(.35f, [&] {
        senseLines(hud, s, color, age);
        for (int k = 0; answer == SenseAnswer::Jump && k < 2; ++k) {
            const float y = (.7f + .17f * static_cast<float>(k)) * s;
            hud.stroke({-.2f * s, y}, {0, y + .17f * s}, .04f * s, line);
            hud.stroke({0, y + .17f * s}, {.2f * s, y}, .04f * s, line);
        }
    });
}
} // namespace

void appendSense(std::vector<Vertex>& out, const SenseView& sense, Vec3 viewer, float pixelAngle) {
    const float away = length(viewer - sense.point);
    if (!finite(sense.point) || !finite(viewer) || !(away >= .3f && away <= 60) ||
        !senseShows(sense.answer, sense.age, sense.left, sense.broken, pixelAngle))
        return;
    // Its size and place as the printed mark's: a hand wide an arm's length
    // away, a few degrees across farther off, clear over his head.
    const float s = .055f * std::clamp(away / .9f, 1.4f, 9.f);
    const Vec3 centre = sense.point + Vec3{0, .14f + .5f * s, 0};
    const float distance = length(viewer - centre);
    if (!(distance >= .15f))
        return;
    const Facing f = facing(centre, viewer);
    Hud hud(out, centre, f.right, f.up, f.view, pixelAngle * distance);
    senseMark(hud, sense.answer, s, sense.left, sense.timed, sense.age, sense.broken);
}

void appendSenseSight(std::vector<Vertex>& out, const SenseSightView& view, Pose head, float pixelAngle,
                      const SenseSightConfig& config) {
    const float across = std::sqrt(view.sight.x * view.sight.x + view.sight.y * view.sight.y);
    if (!view.sight.shown || !finite(head.position) || !(across > .5f && across < 2) ||
        !senseShows(view.answer, view.age, view.left, view.broken, pixelAngle))
        return;
    const Quat q = head.orientation;
    const Vec3 forward = q.rotate({0, 0, -1}), right = q.rotate({1, 0, 0}), up = q.rotate({0, 1, 0});
    if (!finite(forward) || !finite(right) || !finite(up))
        return;
    const Vec3 way = (right * view.sight.x + up * view.sight.y) / across;
    // A metre off, upright as the view is, where the printed one is.
    constexpr float distance = 1, s = .032f;
    const auto at = [&](float angle) {
        return head.position + (forward * std::cos(angle) + way * std::sin(angle)) * distance;
    };
    const Vec3 color = senseColor(view.answer);
    {
        const Vec3 centre = at(config.angle), facingHead = normalized(head.position - centre);
        const Vec3 r = normalized(cross(up, facingHead)), u = cross(facingHead, r);
        Hud hud(out, centre, r, u, facingHead, pixelAngle * distance);
        senseMark(hud, view.answer, s, view.left, view.timed, view.age, view.broken);
        if (view.broken < 0) {
            // A chevron beyond the ring pointing to him, as the game's own
            // point to what is off the screen.
            const float fx = dot(way, r), fy = dot(way, u), n = std::sqrt(fx * fx + fy * fy);
            const Flat ahead{fx / std::max(n, 1e-4f), fy / std::max(n, 1e-4f)}, side{-ahead.y, ahead.x};
            const float pulse = .6f + .4f * wave(view.age * 1.5f);
            hud.place(1);
            hud.opacity(smooth(view.age / .1f));
            hud.lit(.35f, [&] {
                for (int k = 0; k < 2; ++k) {
                    const Flat tip = ahead * ((2.35f + .42f * static_cast<float>(k)) * s);
                    hud.stroke(tip - ahead * (.32f * s) + side * (.4f * s), tip, .07f * s, color, pulse);
                    hud.stroke(tip - ahead * (.32f * s) - side * (.4f * s), tip, .07f * s, color, pulse);
                }
            });
        }
    }
    if (view.broken >= 0)
        return;
    // At the rim of the view on his side: three short arcs about the middle
    // of the view, a ping running out along them toward him.
    const Vec3 centre = at(config.rim), facingHead = normalized(head.position - centre);
    const Vec3 radial = normalized(way - facingHead * dot(way, facingHead));
    const Vec3 tangent = normalized(cross(radial, facingHead));
    if (!finite(radial) || !finite(tangent))
        return;
    Hud hud(out, centre, tangent, radial, facingHead, pixelAngle * distance);
    hud.opacity(smooth(view.age / .1f));
    // The view's middle, seen from the rim in its plane, is this far below.
    const float middle = std::tan(config.rim) * distance;
    hud.lit(.4f, [&] {
        for (int k = 0; k < 3; ++k) {
            const float r = middle + (static_cast<float>(k) - 1) * .022f;
            const float span = .085f / middle;
            const float light = .3f + .7f * wave(view.age * 2 - static_cast<float>(k) / 3);
            hud.arc({0, -middle}, r, .0028f, color, light, pi / 2 - span, pi / 2 + span);
        }
    });
}

namespace {
// A gadget's picture as a line drawing, `s` its radius: what the gadget
// does, in the thin strokes of the game's own icons.
void gadgetPicture(Hud& hud, Gadget g, Flat at, float s, Vec3 color, float alpha) {
    const auto p = [&](float x, float y) { return Flat{at.x + x * s, at.y + y * s}; };
    const float h = .06f * s; // a stroke's half width
    switch (g) {
    case Gadget::webShooter:
        // A web: six spokes and two rounds strung between them.
        for (int k = 0; k < 6; ++k) {
            const float a = pi / 2 + pi / 3 * static_cast<float>(k), b = a + pi / 3;
            hud.stroke(at, at + polar(a, .7f * s), h, color, alpha);
            hud.stroke(at + polar(a, .36f * s), at + polar(b, .36f * s), h * .8f, color, alpha);
            hud.stroke(at + polar(a, .64f * s), at + polar(b, .64f * s), h * .8f, color, alpha);
        }
        break;
    case Gadget::impactWeb:
        // A web ball landing hard: a star in a ring.
        hud.star(at, .2f * s, .5f * s, 8, color, alpha);
        hud.arc(at, .66f * s, h * .8f, color, alpha);
        break;
    case Gadget::webBomb:
        // A round bomb, its fuse alight.
        hud.arc(p(-.08f, -.12f), .38f * s, h, color, alpha);
        hud.disc(p(-.08f, -.12f), .16f * s, color, alpha);
        hud.stroke(p(.2f, .17f), p(.34f, .38f), h, color, alpha);
        hud.star(p(.42f, .5f), .05f * s, .17f * s, 6, color, alpha);
        break;
    case Gadget::electricWeb:
        // A bolt.
        hud.stroke(p(.24f, .66f), p(-.18f, .06f), h * 1.3f, color, alpha);
        hud.stroke(p(-.18f, .06f), p(.18f, .06f), h * 1.3f, color, alpha);
        hud.stroke(p(.18f, .06f), p(-.24f, -.66f), h * 1.3f, color, alpha);
        break;
    case Gadget::tripMine:
        // A mine on a wall, its line across to the next.
        hud.arc(p(-.36f, 0), .2f * s, h, color, alpha);
        hud.disc(p(-.36f, 0), .07f * s, color, alpha);
        hud.stroke(p(-.16f, 0), p(.56f, 0), h * .8f, color, alpha);
        hud.stroke(p(.56f, -.28f), p(.56f, .28f), h, color, alpha);
        break;
    case Gadget::concussionBlast:
        // A blast of air, its rings going out from where it went off.
        hud.disc(p(-.44f, 0), .11f * s, color, alpha);
        for (int k = 0; k < 3; ++k)
            hud.arc(p(-.44f, 0), (.34f + .22f * static_cast<float>(k)) * s, h * .8f, color, alpha, -.9f, .9f);
        break;
    case Gadget::spiderDrone: {
        // A spider: its body, its head and eight legs.
        hud.arc(p(0, -.1f), .19f * s, h, color, alpha);
        hud.disc(p(0, .17f), .1f * s, color, alpha);
        static constexpr Flat knees[4] = {{.3f, .24f}, {.36f, .1f}, {.36f, -.1f}, {.28f, -.26f}};
        static constexpr Flat feet[4] = {{.44f, .52f}, {.62f, .16f}, {.6f, -.24f}, {.44f, -.56f}};
        for (const float side : {-1.f, 1.f})
            for (int k = 0; k < 4; ++k) {
                const Flat knee = p(side * knees[k].x, knees[k].y), foot = p(side * feet[k].x, feet[k].y);
                hud.stroke(p(side * .16f, -.04f), knee, h * .7f, color, alpha);
                hud.stroke(knee, foot, h * .7f, color, alpha);
            }
        break;
    }
    case Gadget::suspensionMatrix:
        // The field that lifts what it catches: an orb, two orbits crossing
        // round it.
        hud.disc(at, .17f * s, color, alpha);
        for (const float tilt : {.45f, -.45f}) {
            constexpr int pieces = 18;
            Flat from{};
            for (int k = 0; k <= pieces; ++k) {
                const float t = 2 * pi * static_cast<float>(k) / pieces;
                const float x = .64f * std::cos(t), y = .24f * std::sin(t);
                const Flat to =
                    p(x * std::cos(tilt) - y * std::sin(tilt), x * std::sin(tilt) + y * std::cos(tilt));
                if (k)
                    hud.stroke(from, to, h * .7f, color, alpha);
                from = to;
            }
        }
        break;
    default:
        break;
    }
}
} // namespace

void appendGadgetWheel(std::vector<Vertex>& out, const GadgetWheelView& view, Vec3 viewer, float pixelAngle) {
    const WheelFrame& f = view.frame;
    const float distance = length(viewer - f.centre);
    if (!view.open || !finite(f.centre) || !finite(f.right) || !finite(f.up) || !finite(f.out) ||
        !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0 || !std::isfinite(view.age) ||
        view.age < 0 || !(distance >= .05f && distance <= 5))
        return;
    // Sizes in metres in the wheel's plane: the places' ring, the plate's
    // edges, a picture and the lit one's.
    constexpr float radius = .13f, inner = .086f, outer = .176f, picture = .025f, litPicture = .03f;
    constexpr float wedge = pi / gadgetPlaces; // half a place's share
    const float px = pixelAngle * distance;
    const int lit = view.lit >= 0 && view.lit < gadgetPlaces &&
                            view.places[static_cast<size_t>(view.lit)] != Gadget::none
                        ? view.lit
                        : -1;
    // A place's way from the middle as the mark's angles run (anticlockwise
    // from its right): the wheel's run clockwise from the top.
    const auto angle = [](int p) { return pi / 2 - gadgetPlaceAngle(p); };
    const auto place = [&](int p, float r) { return polar(angle(p), r); };
    const auto spent = [&](int p) {
        return view.most[static_cast<size_t>(p)] && !view.ammo[static_cast<size_t>(p)];
    };
    // It opens from a little smaller, flickering on, its outer edge drawn
    // round from the top.
    const float opening = easeOut(view.age / .18f);
    Hud wheel(out, f.centre, f.right, f.up, f.out, px);
    wheel.place(.88f + .12f * opening);
    wheel.opacity(smooth(view.age / .08f) * flicker(view.age));
    // The plate: a translucent blue ring, as the game's menus are blue, and
    // a darker disc in the middle for the name. What lies on them needs no
    // halo.
    wheel.plain([&] {
        wheel.arc({}, (inner + outer) / 2, (outer - inner) / 2, panel, .55f);
        wheel.disc({}, inner - .004f, navy, .45f);
    });
    // Its edges: the outer drawn round from the top as it opens, haloed
    // against the world; ticks round the outside and a line between places.
    if (opening > .01f)
        wheel.lit(0, [&] { wheel.arc({}, outer, .0007f, line, .9f, pi / 2 - 2 * pi * opening, pi / 2); });
    wheel.plain([&] {
        wheel.arc({}, inner, .0005f, line, .5f);
        for (int k = 0; k < 64; ++k) {
            const bool major = k % 8 == 0;
            const Flat way = polar(pi / 2 - 2 * pi * static_cast<float>(k) / 64);
            wheel.rule(way * (outer + .0016f), way * (outer + (major ? .0068f : .0042f)), .00035f, line,
                       major ? .8f : .4f);
        }
        for (int p = 0; p < gadgetPlaces; ++p) {
            const float a = angle(p) + wedge;
            wheel.rule(polar(a, inner + .004f), polar(a, outer - .004f), .0004f, line, .3f);
        }
    });
    // The lit place: its wedge cyan, a light flashing over it as it lights,
    // inside the menus' corner brackets, which close on it.
    const float litFor = std::isfinite(view.litAge) ? std::max(view.litAge, 0.f) : 1.f;
    if (lit >= 0) {
        const float pop = easeOut(litFor / .1f);
        wheel.plain([&] {
            wheel.arc({}, (inner + outer) / 2, (outer - inner) / 2 - .0012f, cyan, .2f + .3f * (1 - pop),
                      angle(lit) - wedge, angle(lit) + wedge);
        });
        wheel.glowing(.3f, [&] {
            wheel.arc({}, outer, .0011f, cyan, 1, angle(lit) - wedge, angle(lit) + wedge);
            const float box = .037f * (1.35f - .35f * pop);
            brackets(wheel, place(lit, radius), box, box, .011f, .0009f, line);
        });
    }
    // The pictures in white lines, one after another as it opens; the
    // equipped one's amber, with an amber diamond inside it; one out of
    // charges dim; the lit one cyan, larger and glowing. An empty place has
    // a dim diamond.
    const auto shown = [&](int p) { return smooth((view.age - .015f * static_cast<float>(p)) / .12f); };
    wheel.plain([&] {
        for (int p = 0; p < gadgetPlaces; ++p) {
            const Gadget g = view.places[static_cast<size_t>(p)];
            if (g == Gadget::none) {
                wheel.diamond(place(p, radius), .0045f, dim, shown(p));
                continue;
            }
            if (p == view.equipped)
                wheel.diamond(place(p, inner + .009f), .004f, amber, shown(p));
            if (p != lit)
                gadgetPicture(wheel, g, place(p, radius), picture,
                              p == view.equipped ? amber : spent(p) ? dim : line, shown(p));
        }
    });
    if (lit >= 0)
        wheel.glowing(.3f, [&] {
            gadgetPicture(wheel, view.places[static_cast<size_t>(lit)], place(lit, radius), litPicture,
                          mix(cyan, line, .25f), shown(lit));
        });
    // Each place's charges: a short dash each across its outer side, cyan
    // left, dim spent; eight at most.
    wheel.plain([&] {
        for (int p = 0; p < gadgetPlaces; ++p) {
            const int most = std::min<int>(view.most[static_cast<size_t>(p)], 8);
            if (view.places[static_cast<size_t>(p)] == Gadget::none || !most)
                continue;
            constexpr float spacing = 4.6f * degree, r = outer - .0085f, half = .0045f;
            for (int k = 0; k < most; ++k) {
                const float middle =
                    angle(p) - spacing * (static_cast<float>(k) - .5f * static_cast<float>(most - 1));
                const bool left = k < view.ammo[static_cast<size_t>(p)];
                const Flat at = polar(middle, r), along = polar(middle + pi / 2, half);
                wheel.rule(at - along, at + along, .0017f, left ? cyan : dim, left ? 1.f : .7f);
            }
        }
    });
    // The middle: the lit gadget's name between two rules with ticked ends,
    // its charges under it; nothing lit, a diamond in a ring, or what a bare
    // wheel says.
    if (lit < 0) {
        wheel.plain([&] {
            if (view.bare) {
                const float height = fittedHeight(bareWheelText, .0105f, .136f);
                wheel.text(bareWheelText, {textWidth(bareWheelText, height) * -.5f, -height * .5f}, height, line,
                           .8f, .11f);
                return;
            }
            wheel.arc({}, .012f, .0005f, line, .5f);
            wheel.diamond({}, .004f, line, .8f);
        });
        return;
    }
    const char* name = gadgetName(view.places[static_cast<size_t>(lit)]);
    const float height = fittedHeight(name, .0125f, .136f);
    const float width = textWidth(name, height);
    const float ruleHalf = std::max(width * .5f + .008f, .03f) * easeOut(litFor / .1f);
    wheel.plain([&] {
        for (const float y : {.021f, -.013f}) {
            wheel.rule({-ruleHalf, y}, {ruleHalf, y}, .00035f, line, .7f);
            for (const float x : {-ruleHalf, ruleHalf})
                wheel.rule({x, y - .0022f}, {x, y + .0022f}, .00035f, line, .7f);
        }
    });
    wheel.opacity(flicker(litFor));
    wheel.plain([&] {
        wheel.text(name, {width * -.5f, -height * .5f + .004f}, height, line, 1, .11f);
        if (const int most = view.most[static_cast<size_t>(lit)]) {
            char charges[16];
            std::snprintf(charges, sizeof charges, "%d/%d", view.ammo[static_cast<size_t>(lit)], most);
            const float h = .0085f;
            wheel.text(charges, {textWidth(charges, h) * -.5f, -.03f}, h,
                       view.ammo[static_cast<size_t>(lit)] ? cyan : red, 1, .12f);
        }
    });
}

void appendCalibration(std::vector<Vertex>& out, const body_calibration::View& view, Vec3 viewer) {
    using namespace body_calibration;
    if (view.phase == Phase::idle || !finite(view.panel.position) || !finite(viewer))
        return;
    // A headset's pixel, about, in radians: the panel's edges and halos.
    constexpr float pixel = .0007f;
    const auto& q = view.panel.orientation;
    const Vec3 right = q.rotate({1, 0, 0}), up = q.rotate({0, 1, 0}), front = q.rotate({0, 0, 1});
    const float px = pixel * std::max(length(viewer - view.panel.position), .3f);
    const float w = panelWidth / 2, h = panelHeight / 2;
    Hud hud(out, view.panel.position, right, up, front, px);
    // The plate: the menus' blue, ruled across the top and the bottom with
    // ticks at the right-hand ends, a rail down its left with a gap and a
    // dash at either end, corner brackets outside it.
    hud.plain([&] { hud.fill({-w, -h}, {w, -h}, {w, h}, {-w, h}, panel, .8f); });
    hud.lit(0, [&] {
        constexpr float thin = .0013f;
        for (const float y : {h, -h}) {
            hud.bar({-w, y}, {w, y}, thin, line, .85f);
            const float tick = y + (y > 0 ? .006f : -.006f);
            hud.bar({w - .1f, tick}, {w, tick}, thin * 1.6f, line);
        }
        const float rail = -w + .03f;
        hud.bar({rail, h - .09f}, {rail, -h + .09f}, thin, line, .6f);
        for (const float s : {1.f, -1.f})
            hud.bar({rail, s * (h - .05f)}, {rail, s * (h - .07f)}, thin, line, .6f);
        brackets(hud, {}, w + .012f, h + .012f, .035f, thin * 1.4f, line, .8f);
    });
    // The title on a tab over the plate's left, as the game's menus name
    // theirs.
    const char* title = view.phase == Phase::done ? "CALIBRATED" : "BODY CALIBRATION";
    const float titleHeight = .03f, tab = textWidth(title, titleHeight) + .06f;
    hud.plain([&] {
        hud.fill({-w, h + .012f}, {-w + tab, h + .012f}, {-w + tab, h + .07f}, {-w, h + .07f}, navy, .7f);
    });
    hud.plain([&] {
        hud.bar({-w, h + .07f}, {-w + tab * .55f, h + .07f}, .0013f, line, .9f);
        hud.text(title, {-w + .03f, h + .026f}, titleHeight, view.phase == Phase::done ? cyan : line, 1,
                 .11f);
    });
    // Lines of text on the plate, `align` 0 from x, .5 centred on it; shrunk
    // to `width`.
    const auto say = [&](const char* s, float x, float y, float height, Vec3 color, float align = .5f,
                         float width = panelWidth - .16f) {
        const float fitted = fittedHeight(s, height, width);
        hud.text(s, {x - textWidth(s, fitted) * align, y}, fitted, color, 1, .1f);
    };
    // The picked row's pointer on the rail: a dash and a diamond.
    const auto pointer = [&](float y) {
        hud.bar({-w + .03f, y}, {-w + .065f, y}, .0013f, line);
        hud.arc({-w + .08f, y}, .011f, .0016f, line, 1, 0, 2 * pi, 4);
    };
    if (view.phase == Phase::done) {
        char line1[64], line2[64];
        std::snprintf(line1, sizeof line1, "HEIGHT %.2f M", body_calibration::height(view.result.eyeHeight));
        std::snprintf(line2, sizeof line2, "ARM LENGTH %.2f M", view.result.armLength);
        hud.plain([&] {
            say("CALIBRATED", 0, .15f, .055f, cyan);
            say(line1, 0, .05f, .04f, line);
            say(line2, 0, -.03f, .04f, line);
            say("SPIDER-MAN NOW HAS YOUR SIZE", 0, -.12f, .032f, text);
            say("REDO IT IN SETTINGS, SPIDY VR", 0, -.22f, .026f, text);
            pointer(.15f + .0275f);
        });
    } else {
        // A figure in a T-pose in thin lines; its arms cyan while both are
        // in place.
        const float fx = -.4f, fy = -.01f, stroke = .0035f;
        const bool both = view.ready[0] && view.ready[1];
        const bool holding = view.phase == Phase::holding;
        hud.glowing(both ? .3f : 0, [&] {
            hud.arc({fx, fy + .105f}, .028f, stroke, line);
            hud.stroke({fx, fy + .075f}, {fx, fy - .04f}, stroke, line);
            hud.stroke({fx - .115f, fy + .055f}, {fx + .115f, fy + .055f}, stroke, both ? cyan : line);
            hud.stroke({fx, fy - .04f}, {fx - .045f, fy - .15f}, stroke, line);
            hud.stroke({fx, fy - .04f}, {fx + .045f, fy - .15f}, stroke, line);
            brackets(hud, {fx, fy}, .15f, .17f, .025f, .0012f, line, .5f);
        });
        const float textLeft = -.2f, column = w - .05f - textLeft;
        hud.plain([&] {
            say("STAND TALL, LOOK AHEAD", textLeft, .1f, .032f, text, 0, column);
            say("ARMS STRAIGHT OUT TO THE SIDES", textLeft, .04f, .032f, text, 0, column);
            say("HOLD BOTH TRIGGERS", textLeft, -.02f, .032f, text, 0, column);
            say(holding ? "HOLD STILL" : hintText(view.hint), textLeft, -.11f, .036f, holding ? cyan : amber,
                0, column);
            pointer(-.11f + .018f);
        });
        // The hold's bar: segments filling left to right.
        constexpr int segments = 24;
        const float barLeft = textLeft, barRight = w - .06f, low = -.175f, high = -.155f;
        const float each = (barRight - barLeft) / segments, progress = unit(view.progress);
        hud.glowing(.25f, [&] {
            for (int i = 0; i < segments; ++i) {
                const float x0 = barLeft + each * static_cast<float>(i), x1 = x0 + each * .8f;
                const float share = std::clamp(progress * segments - static_cast<float>(i), 0.f, 1.f);
                const float y = (low + high) / 2, half = (high - low) / 2 - .003f;
                hud.bar({x0, y}, {x1, y}, half, dim, .8f);
                if (share > 0)
                    hud.bar({x0, y}, {x0 + (x1 - x0) * share, y}, half, cyan);
            }
        });
        // The way out, as the game's own button prompts: B in a ring, SKIP.
        hud.lit(0, [&] {
            const float y = -h - .06f, size = .026f;
            hud.arc({-w + .03f + size * .75f, y + size * .5f}, size * .75f, .0016f, line);
            const float button = -w + .03f + size * .75f;
            hud.text("B", {button - textWidth("B", size * .8f) * .5f, y + size * .1f}, size * .8f, line, 1,
                     .12f);
            hud.text("SKIP", {-w + .03f + size * 1.9f, y + size * .05f}, size * .9f, line, 1, .11f);
        });
    }
    // Each controller: a ring of segments facing the eyes, cyan while its
    // arm is in place, a second ring outside filling with the hold.
    for (int i = 0; i < 2; ++i) {
        if (!view.handTracked[static_cast<size_t>(i)] || !finite(view.grips[static_cast<size_t>(i)].position))
            continue;
        const Vec3 centre = view.grips[static_cast<size_t>(i)].position;
        if (length(viewer - centre) < .05f)
            continue;
        const bool done = view.phase == Phase::done, ready = view.ready[static_cast<size_t>(i)] || done;
        const Facing g = facing(centre, viewer);
        Hud ring(out, centre, g.right, g.up, g.view, pixel * length(viewer - centre));
        constexpr int segments = 16;
        const float step = 2 * pi / segments, progress = done ? 1.f : unit(view.progress);
        ring.lit(ready ? .3f : 0, [&] {
            for (int k = 0; k < segments; ++k) {
                const float start = pi / 2 - step * static_cast<float>(k) - 2.5f * degree;
                ring.arc({}, .074f, .0025f, ready ? cyan : text, 1, start - step + 5 * degree, start);
            }
            if (progress > 0)
                ring.arc({}, .087f, .0035f, cyan, 1, pi / 2 - 2 * pi * progress, pi / 2);
        });
    }
}
} // namespace spidy::tech
