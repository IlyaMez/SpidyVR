#include "spidy/web_visual.hpp"

namespace spidy {
namespace {
constexpr float pi = 3.14159265f;
constexpr float braid = .075f; // metres per visible twist of the strand
Vec3 mix(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}
Vec3 brightest(Vec3 c) {
    return {std::min(c.x, 1.f), std::min(c.y, 1.f), std::min(c.z, 1.f)};
}
void quad(std::vector<Vertex>& out, const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
    out.insert(out.end(), {a, b, c, a, c, d});
}
Vec3 perpendicular(Vec3 d) {
    return normalized(cross(d, std::abs(d.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
}
float halfWidth(const WebLook& look, Vec3 viewer, Vec3 p, float pixelAngle, float radius) {
    return std::max(radius, .5f * look.minPixels * pixelAngle * length(viewer - p));
}
// Deterministic 0..1 value per strand, so a splat keeps its shape.
float hash(float seed, int i) {
    const float s = std::sin(seed * 12.9898f + i * 78.233f) * 43758.547f;
    return s - std::floor(s);
}
void glob(std::vector<Vertex>& out, Vec3 center, Vec3 viewer, float radius, Vec3 color) {
    const Vec3 v = normalized(viewer - center);
    const Vec3 a = perpendicular(v), b = cross(v, a);
    constexpr int sides = 8;
    for (int i = 0; i < sides; ++i) {
        const float t0 = 2 * pi * i / sides, t1 = 2 * pi * (i + 1) / sides;
        const Vec3 p0 = center + (a * std::cos(t0) + b * std::sin(t0)) * radius;
        const Vec3 p1 = center + (a * std::cos(t1) + b * std::sin(t1)) * radius;
        out.insert(out.end(), {{center, color}, {p0, color * .8f}, {p1, color * .8f}});
    }
}
// Soft-edged shapes for the aim markers: at `alpha` inside, fading to nothing
// across `soft` metres at every edge, so their outlines stay smooth without
// multisampling while they move by fractions of a pixel.
//
// An arc of a flat ring around c in the plane of unit axes a and b, from
// angle t0 to t1 (a whole ring when they span 2 pi) in `segments` pieces a
// whole turn, between radii inner and outer (0: a disc); its inner edge
// fades across `innerSoft` instead when that is given.
void softArc(std::vector<Vertex>& out, Vec3 c, Vec3 a, Vec3 b, float inner, float outer, float t0, float t1,
             Vec3 color, float alpha, float soft, int segments, float innerSoft = -1) {
    const bool whole = t1 - t0 > 2 * pi - 1e-3f;
    const int pieces = std::max(2, static_cast<int>(std::ceil((t1 - t0) / (2 * pi) * segments)));
    const float radii[4] = {std::max(inner - (innerSoft >= 0 ? innerSoft : soft), 0.f), inner, outer, outer + soft};
    const float alphas[4] = {inner > 0 ? 0.f : alpha, alpha, alpha, 0};
    const auto at = [&](float t, int k, float opacity) {
        return Vertex{c + (a * std::cos(t) + b * std::sin(t)) * radii[k], color, opacity};
    };
    const int first = inner > 0 ? 0 : 1; // a disc has no inner edge
    for (int i = 0; i < pieces; ++i) {
        const float u0 = t0 + (t1 - t0) * i / pieces, u1 = t0 + (t1 - t0) * (i + 1) / pieces;
        for (int k = first; k < 3; ++k)
            quad(out, at(u0, k, alphas[k]), at(u1, k, alphas[k]), at(u1, k + 1, alphas[k + 1]),
                 at(u0, k + 1, alphas[k + 1]));
    }
    if (whole)
        return;
    // Each end fades out along the ring.
    const float cap = soft / std::max((inner + outer) / 2, 1e-6f);
    for (const float end : {t0, t1}) {
        const float beyond = end == t0 ? t0 - cap : t1 + cap;
        for (int k = first; k < 3; ++k)
            quad(out, at(end, k, alphas[k]), at(beyond, k, 0), at(beyond, k + 1, 0), at(end, k + 1, alphas[k + 1]));
    }
}
// A flat bar from p to q across the line of sight `view`, its ends squared
// off half its width beyond them so two bars close a corner.
void softBar(std::vector<Vertex>& out, Vec3 p, Vec3 q, Vec3 view, float halfWidth, Vec3 color, float alpha,
             float soft) {
    const Vec3 along = normalized(q - p), side = normalized(cross(q - p, view));
    const float span = length(q - p);
    const float x[4] = {-halfWidth - soft, -halfWidth, span + halfWidth, span + halfWidth + soft};
    const float y[4] = {-halfWidth - soft, -halfWidth, halfWidth, halfWidth + soft};
    const auto at = [&](int i, int j) {
        return Vertex{p + along * x[i] + side * y[j], color, i % 3 && j % 3 ? alpha : 0.f};
    };
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            quad(out, at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
}
// A flat triangle across the line of sight `view`, grown `grow` past its
// corners' edges and then fading out across `soft`.
void softTriangle(std::vector<Vertex>& out, std::array<Vec3, 3> p, Vec3 view, Vec3 color, float alpha, float grow,
                  float soft) {
    const Vec3 centre = (p[0] + p[1] + p[2]) / 3;
    std::array<Vec3, 3> outward{}, mitre{};
    for (int i = 0; i < 3; ++i) {
        const Vec3 normal = normalized(cross(p[(i + 1) % 3] - p[i], view));
        outward[i] = dot(normal, (p[i] + p[(i + 1) % 3]) * .5f - centre) < 0 ? -normal : normal;
    }
    // A corner moves out along the bisector of its edges' normals, far
    // enough to move both edges out by one unit, but no more than three.
    for (int i = 0; i < 3; ++i) {
        const Vec3 bisector = normalized(outward[(i + 2) % 3] + outward[i]);
        mitre[i] = bisector / std::max(dot(bisector, outward[i]), 1.f / 3);
    }
    std::array<Vec3, 3> inside{}, edge{};
    for (int i = 0; i < 3; ++i) {
        inside[i] = p[i] + mitre[i] * grow;
        edge[i] = p[i] + mitre[i] * (grow + soft);
    }
    out.insert(out.end(), {{inside[0], color, alpha}, {inside[1], color, alpha}, {inside[2], color, alpha}});
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        quad(out, {inside[i], color, alpha}, {inside[j], color, alpha}, {edge[j], color, 0}, {edge[i], color, 0});
    }
}
// Each hand's aim markers in their own colour (linear RGB): sky blue for the
// left hand, orange for the right. A miss is red for either.
constexpr Vec3 handColors[2] = {{.13f, .55f, 1.f}, {1.f, .4f, .05f}};
constexpr Vec3 missColor{.9f, .07f, .05f}, shadowColor{.01f, .012f, .018f};
// Tapered camera-facing strand for the impact splat.
void strand(std::vector<Vertex>& out, Vec3 from, Vec3 to, Vec3 viewer, float pixelAngle, const WebLook& look,
            float rootRadius) {
    constexpr int pieces = 4;
    for (int i = 0; i < pieces; ++i) {
        const float u0 = float(i) / pieces, u1 = float(i + 1) / pieces;
        const Vec3 p0 = mix(from, to, u0), p1 = mix(from, to, u1);
        const Vec3 side0 = normalized(cross(to - from, viewer - p0)), side1 = normalized(cross(to - from, viewer - p1));
        const float w0 = halfWidth(look, viewer, p0, pixelAngle, rootRadius * (1 - .7f * u0));
        const float w1 = halfWidth(look, viewer, p1, pixelAngle, rootRadius * (1 - .7f * u1));
        const Vec3 c0 = mix(look.core, look.edge, .25f + .3f * u0), c1 = mix(look.core, look.edge, .25f + .3f * u1);
        quad(out, {p0 - side0 * w0, c0 * .75f}, {p1 - side1 * w1, c1 * .75f}, {p1 + side1 * w1, c1},
             {p0 + side0 * w0, c0});
    }
}
} // namespace

void appendWeb(std::vector<Vertex>& out, const WebLine& web, Vec3 viewer, float pixelAngle, const WebLook& look) {
    const Vec3 chord = web.end - web.start;
    const float distance = length(chord);
    if (!finite(web.start) || !finite(web.end) || !finite(viewer) || distance < .05f || distance > 1000 ||
        !std::isfinite(pixelAngle) || pixelAngle <= 0 || !std::isfinite(web.extended) || web.extended <= 0)
        return;
    const float extended = std::min(web.extended, 1.f);
    // Parabolic approximation: arc length ~= chord + 8 h^2 / (3 chord).
    const float slack = std::isfinite(web.slack) ? std::max(web.slack, 0.f) : 0.f;
    const float sag = std::min(std::sqrt(3 * distance * slack / 8), distance * .5f);
    const Vec3 down{0, -1, 0};
    auto point = [&](float u) { return web.start + chord * u + down * (sag * 4 * u * (1 - u)); };
    auto tangent = [&](float u) { return normalized(chord + down * (sag * 4 * (1 - 2 * u))); };
    // Short segments near the viewer resolve the braid; distant ones only need
    // to follow the curve.
    float u = 0, travelled = 0;
    bool first = true;
    Vertex previous[5]{};
    constexpr float columns[5] = {-1, -.5f, 0, .5f, 1};
    while (true) {
        const Vec3 p = point(u);
        const float range = length(viewer - p);
        const Vec3 t = tangent(u);
        Vec3 side = cross(t, normalized(viewer - p));
        side = length(side) > 1e-4f ? normalized(side) : perpendicular(t);
        const float w = halfWidth(look, viewer, p, pixelAngle, look.radius);
        // Fade the braid out before it would alias at a distance.
        const float detail = std::clamp(1 - range / 14, 0.f, 1.f);
        const float phase = 2 * pi * travelled / braid;
        const float sheen = std::sin(phase) * .55f;
        Vertex current[5];
        for (int c = 0; c < 5; ++c) {
            const float x = columns[c];
            const float body = 1 - x * x;
            const float highlight = .3f * detail * std::exp(-(x - sheen) * (x - sheen) / .09f);
            const float band = 1 - detail * .1f * (.5f + .5f * std::cos(phase * 2));
            current[c] = {p + side * (w * x), brightest(mix(look.edge, look.core, body) * band +
                                                        Vec3{highlight, highlight, highlight})};
        }
        if (!first)
            for (int c = 0; c < 4; ++c)
                quad(out, previous[c], current[c], current[c + 1], previous[c + 1]);
        std::copy(std::begin(current), std::end(current), std::begin(previous));
        first = false;
        if (u >= extended)
            break;
        const float step = std::clamp(.03f + .025f * range, .03f, 6.f);
        const float du = step / std::max(distance, step);
        u = std::min(u + du, extended);
        travelled += step;
    }
    if (extended < 1) {
        glob(out, point(extended), viewer, halfWidth(look, viewer, point(extended), pixelAngle, .018f), look.core);
        return;
    }
    if (!web.splat)
        return;
    // Impact web: a central glob with strands spread across the surface. The
    // surface normal is unknown here; face the strands back along the web.
    const Vec3 normal = normalized(web.start - web.end);
    const Vec3 a = perpendicular(normal), b = cross(normal, a);
    const Vec3 center = web.end + normal * .004f;
    constexpr int strands = 7;
    for (int i = 0; i < strands; ++i) {
        const float angle = 2 * pi * (i + .35f * hash(web.seed, i)) / strands + web.seed;
        const Vec3 direction = a * std::cos(angle) + b * std::sin(angle);
        const float reach = .09f + .13f * hash(web.seed, i + 17);
        const Vec3 curl = (a * -std::sin(angle) + b * std::cos(angle)) * (reach * .18f * (hash(web.seed, i + 31) - .5f));
        strand(out, center, center + direction * reach + curl, viewer, pixelAngle, look, .0055f);
    }
    glob(out, center + normal * .002f, viewer, halfWidth(look, viewer, center, pixelAngle, .026f), look.core);
}
void appendAimMarker(std::vector<Vertex>& out, const AimMarker& marker, Vec3 viewer, float pixelAngle) {
    const float distance = length(viewer - marker.point);
    const float opacity = std::isfinite(marker.opacity) ? std::clamp(marker.opacity, 0.f, 1.f) : 0.f;
    if (!finite(marker.point) || !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0 ||
        !(distance >= .3f && distance <= 1000) || opacity <= 0)
        return;
    // Level and upright across the line of sight, so the marker never rolls.
    const Vec3 view = (viewer - marker.point) / distance;
    Vec3 right = cross(Vec3{0, 1, 0}, view);
    right = length(right) > .1f ? normalized(right) : perpendicular(view);
    const Vec3 up = cross(view, right);
    // Metres per pixel at the marker, and how far the grip has closed toward a shot.
    const float px = pixelAngle * distance;
    const float squeeze = std::isfinite(marker.squeeze) ? std::clamp(marker.squeeze / .65f, 0.f, 1.f) : 0.f;
    const Vec3 c = marker.point;
    const Vec3 color = handColors[marker.hand ? 1 : 0], light = mix(color, {1, 1, 1}, .6f);
    // Every shape is drawn twice, first all of a marker's shadows, then its
    // colours: a soft dark shadow reaching `shade` past the shape keeps it
    // readable on the sky and on a lit wall.
    const float shade = 1.3f * px, shadowSoft = 1.8f * px, soft = .8f * px, shadowAlpha = .6f;
    const auto segments = [&](float radius) { return std::clamp(static_cast<int>(radius / px) + 12, 16, 72); };
    // An arc's shadow lies mostly outside it, a thin edge within, and
    // reaches past its ends by `lengthen` metres.
    const auto arc = [&](bool shadow, Vec3 tint, float alpha, float r, float halfWidth, float from = 0,
                         float to = 2 * pi, float lengthen = 0) {
        const float grow = shadow ? shade : 0, beyond = shadow ? lengthen / r : 0;
        softArc(out, c, right, up, std::max(r - halfWidth - grow * .4f, 0.f), r + halfWidth + grow, from - beyond,
                to + beyond, shadow ? shadowColor : tint, alpha * opacity * (shadow ? shadowAlpha : 1),
                shadow ? shadowSoft : soft, segments(r), shadow ? shadowSoft * .6f : soft);
    };
    // A dot's shadow is a thin dark edge: a wider one would read as a second ring.
    const auto dot = [&](bool shadow, Vec3 tint, float alpha, float r) {
        softArc(out, c, right, up, 0, r + (shadow ? shade * .5f : 0), 0, 2 * pi, shadow ? shadowColor : tint,
                alpha * opacity * (shadow ? shadowAlpha : 1), shadow ? shadowSoft * .7f : soft, segments(r));
    };
    switch (marker.kind) {
    case AimMark::anchor: {
        const float r = 10.5f * px * (1 - .4f * squeeze);
        for (const bool shadow : {true, false}) {
            arc(shadow, color, 1, r, 1.15f * px);
            dot(shadow, light, 1, 2 * px);
        }
        break;
    }
    case AimMark::air: {
        // Faint: short translucent dashes, their shadows lighter and no
        // longer than they are, so the gaps stay open on a bright sky.
        const float r = 8.5f * px * (1 - .4f * squeeze);
        constexpr int dashes = 8;
        for (const bool shadow : {true, false})
            for (int i = 0; i < dashes; ++i) {
                const float from = 2 * pi * i / dashes;
                arc(shadow, color, shadow ? .7f : .75f, r, .9f * px, from, from + 2 * pi / dashes * .45f);
            }
        break;
    }
    case AimMark::blocked: {
        const Vec3 rising = (right + up) * (5.5f * px * .7071f), falling = (right - up) * (5.5f * px * .7071f);
        for (const bool shadow : {true, false})
            for (const Vec3 arm : {rising, falling})
                softBar(out, c - arm, c + arm, view, .95f * px + (shadow ? shade : 0), shadow ? shadowColor : missColor,
                        opacity * (shadow ? shadowAlpha : .95f), shadow ? shadowSoft : soft);
        break;
    }
    case AimMark::target: {
        // Three arcs around the target, turning, each with a claw pointing in
        // at it; while the ring locks on it closes in from wider. The right
        // hand's ring is a little wider and turned between the left's arcs,
        // so both hands' rings on one target stay apart.
        const float lock = std::isfinite(marker.lock) ? std::clamp(marker.lock, 0.f, 1.f) : 1.f;
        const float closing = lock * lock * (3 - 2 * lock);
        const float size = std::max(std::isfinite(marker.radius) ? marker.radius * 1.15f : 0.f, 13 * px);
        const float r = size * (marker.hand ? 1.15f : 1.f) * (1 - .25f * squeeze) * (1 + .8f * (1 - closing));
        const float spin = (std::isfinite(marker.spin) ? marker.spin : 0.f) + (marker.hand ? pi / 3 : 0.f);
        const float halfWidth = 1.2f * px, claw = std::clamp(r * .2f, 4.5f * px, 10 * px);
        constexpr float span = 75 * pi / 180;
        for (const bool shadow : {true, false}) {
            for (int i = 0; i < 3; ++i) {
                const float middle = pi / 2 + spin + 2 * pi * i / 3;
                arc(shadow, color, 1, r, halfWidth, middle - span / 2, middle + span / 2, shade);
                const Vec3 outward = right * std::cos(middle) + up * std::sin(middle);
                const Vec3 across = right * -std::sin(middle) + up * std::cos(middle);
                const Vec3 base = c + outward * (r - halfWidth);
                softTriangle(out, {base + across * (claw * .55f), base - outward * claw, base - across * (claw * .55f)},
                             view, shadow ? shadowColor : color, opacity * (shadow ? shadowAlpha : 1),
                             shadow ? shade : 0, shadow ? shadowSoft : soft);
            }
            dot(shadow, light, 1, 1.8f * px);
        }
        break;
    }
    }
}
Vec3 onAimLine(Vec3 point, Vec3 normal, Vec3 origin, Vec3 direction) {
    const Vec3 n = normalized(normal);
    const float facing = dot(direction, n);
    if (!finite(point) || !finite(origin) || !finite(direction) || !finite(n) || length(n) < .5f ||
        !(std::abs(facing) >= .2f))
        return point;
    const float along = dot(point - origin, n) / facing;
    const Vec3 crossing = origin + direction * along;
    return along > 0 && finite(crossing) && length(crossing - point) <= .25f * length(point - origin) + .25f
               ? crossing
               : point;
}
void AimMarkerMotion::reset() {
    *this = {};
}
void AimMarkerMotion::begin(int64_t timeNs) {
    seconds_ = 0;
    if (timeNs <= time_)
        return; // the same image again
    if (time_ && static_cast<double>(timeNs - time_) * 1e-9 > gapSeconds)
        reset();
    else if (time_)
        seconds_ = static_cast<float>(static_cast<double>(timeNs - time_) * 1e-9);
    time_ = timeNs;
}
Vec3 AimMarkerMotion::aim(Vec3 direction, float yaw) {
    if (!finite(direction) || length(direction) < .5f || !std::isfinite(yaw))
        return direction;
    const Quat turn = Quat::yaw(yaw);
    const Vec3 raw = turn.conjugate().rotate(normalized(direction));
    if (!filtering_) {
        steady_ = raw;
        speed_ = {};
        filtering_ = true;
    } else if (seconds_ > 0) {
        // The share of the way to the raw direction a low-pass at `hz` takes
        // in this image's time; the cutoff rises with the aim's speed.
        const auto share = [dt = seconds_](float hz) { return dt / (dt + 1 / (2 * pi * hz)); };
        speed_ += ((raw - steady_) / seconds_ - speed_) * share(speedCutoffHz);
        const float cutoff = minCutoffHz + speedCutoff * std::max(length(speed_) - speedDeadband, 0.f);
        steady_ = normalized(steady_ + (raw - steady_) * share(cutoff));
    }
    return turn.rotate(steady_);
}
void AimMarkerMotion::markers(const AimMarker* wanted, Vec3 origin, Vec3 direction, std::vector<AimMarker>& out) {
    const bool ray = finite(origin) && finite(direction) && length(direction) > .5f;
    if (wanted && (wanted->kind > AimMark::target || !finite(wanted->point) || (wanted->kind != AimMark::target && !ray)))
        wanted = nullptr;
    direction = normalized(direction);
    const float dt = seconds_;
    // The share of the way to a new value an ease over `seconds` takes now.
    const auto ease = [dt](float seconds) { return 1 - std::exp(-dt / seconds); };
    for (unsigned k = 0; k < kinds_.size(); ++k) {
        auto& shown = kinds_[k];
        const bool target = k == static_cast<unsigned>(AimMark::target);
        if (wanted && static_cast<unsigned>(wanted->kind) == k) {
            const bool fresh = shown.weight <= 0;
            const AimMarker before = shown.marker;
            shown.marker = *wanted;
            if (target && !fresh) {
                // The ring slides to where the target is now, or to the next one.
                shown.marker.point = mix(before.point, wanted->point, ease(targetSeconds));
                shown.marker.radius = before.radius + (wanted->radius - before.radius) * ease(targetSeconds);
            } else if (!target) {
                // A marker on the ray eases to a new distance (its depth).
                const float distance = std::max(length(wanted->point - origin), .01f);
                shown.distance = fresh || !(shown.distance > 0)
                                     ? distance
                                     : std::exp(std::log(shown.distance) +
                                                (std::log(distance) - std::log(shown.distance)) * ease(distanceSeconds));
                shown.marker.point = origin + direction * shown.distance;
            }
            // A new marker shows at once, faintly, and is whole within fadeInSeconds.
            shown.weight = fresh ? .35f : std::min(1.f, shown.weight + dt / fadeInSeconds);
            shown.lock = fresh ? 0.f : std::min(1.f, shown.lock + dt / lockSeconds);
        } else if (shown.weight > 0) {
            shown.weight = std::max(0.f, shown.weight - dt / fadeOutSeconds);
            shown.lock = std::min(shown.lock, shown.weight); // a target's ring widens as it goes
        }
        if (shown.weight <= 0)
            continue;
        // A marker fading out stays where it was last wanted: where the web
        // just went, or where the aim left it.
        AimMarker marker = shown.marker;
        marker.opacity = shown.weight;
        marker.lock = target ? shown.lock : 1.f;
        marker.spin = spin_;
        out.push_back(marker);
    }
    // The target's ring turns, faster as the grip closes.
    const float squeeze = kinds_[static_cast<unsigned>(AimMark::target)].marker.squeeze;
    spin_ = std::fmod(spin_ + dt * (.8f + 2.4f * std::clamp(std::isfinite(squeeze) ? squeeze : 0.f, 0.f, 1.f)),
                      2 * pi);
}
} // namespace spidy
