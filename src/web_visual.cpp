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
// A flat ring around c in the plane of unit axes a and b; with dashes, that
// many dashes with gaps between them.
void ring(std::vector<Vertex>& out, Vec3 c, Vec3 a, Vec3 b, float inner, float outer, Vec3 color, int dashes = 0) {
    constexpr int segments = 32;
    for (int i = 0; i < segments; ++i) {
        if (dashes && (i * dashes * 2 / segments) % 2)
            continue;
        const float t0 = 2 * pi * i / segments, t1 = 2 * pi * (i + 1) / segments;
        const Vec3 d0 = a * std::cos(t0) + b * std::sin(t0), d1 = a * std::cos(t1) + b * std::sin(t1);
        quad(out, {c + d0 * inner, color}, {c + d0 * outer, color}, {c + d1 * outer, color},
             {c + d1 * inner, color});
    }
}
void disc(std::vector<Vertex>& out, Vec3 c, Vec3 a, Vec3 b, float radius, Vec3 color) {
    constexpr int sides = 12;
    for (int i = 0; i < sides; ++i) {
        const float t0 = 2 * pi * i / sides, t1 = 2 * pi * (i + 1) / sides;
        out.insert(out.end(), {{c, color},
                               {c + (a * std::cos(t0) + b * std::sin(t0)) * radius, color},
                               {c + (a * std::cos(t1) + b * std::sin(t1)) * radius, color}});
    }
}
// A flat bar from p to q across the line of sight `view`, its ends squared
// off half its width beyond them so two bars close a corner.
void bar(std::vector<Vertex>& out, Vec3 p, Vec3 q, Vec3 view, float halfWidth, Vec3 color) {
    const Vec3 along = normalized(q - p) * halfWidth, side = normalized(cross(q - p, view)) * halfWidth;
    quad(out, {p - along - side, color}, {q + along - side, color}, {q + along + side, color},
         {p - along + side, color});
}
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
    if (!finite(marker.point) || !finite(viewer) || !std::isfinite(pixelAngle) || pixelAngle <= 0 ||
        !(distance >= .3f && distance <= 1000))
        return;
    // Level and upright across the line of sight, so the marker never rolls.
    const Vec3 view = (viewer - marker.point) / distance;
    Vec3 right = cross(Vec3{0, 1, 0}, view);
    right = length(right) > .1f ? normalized(right) : perpendicular(view);
    const Vec3 up = cross(view, right);
    // Metres per pixel at the marker, and how far the grip has closed toward a shot.
    const float px = pixelAngle * distance;
    const float squeeze = std::isfinite(marker.squeeze) ? std::clamp(marker.squeeze / .65f, 0.f, 1.f) : 0.f;
    // Each shape twice: a dark outline behind, wider by `grow`, keeps it
    // readable on the sky and on a lit wall; the colour in front.
    const Vec3 front = marker.point + view * (distance * .002f), back = marker.point - view * (distance * .002f);
    const auto layers = [&](const auto& shape, Vec3 color) {
        shape(back, 1.5f * px, Vec3{.01f, .012f, .018f});
        shape(front, 0.f, color);
    };
    switch (marker.kind) {
    case AimMark::anchor: {
        const float r = 13 * px * (1 - .35f * squeeze);
        layers(
            [&](Vec3 c, float grow, Vec3 color) {
                ring(out, c, right, up, r - 3 * px - grow, r + grow, color);
                disc(out, c, right, up, 2 * px + grow, color);
            },
            {.9f, .93f, 1});
        break;
    }
    case AimMark::air: {
        const float r = 9 * px * (1 - .35f * squeeze);
        layers([&](Vec3 c, float grow, Vec3 color) { ring(out, c, right, up, r - 2.2f * px - grow, r + grow, color, 8); },
               {.42f, .46f, .52f});
        break;
    }
    case AimMark::blocked: {
        const Vec3 rising = (right + up) * (7 * px * .7071f), falling = (right - up) * (7 * px * .7071f);
        layers(
            [&](Vec3 c, float grow, Vec3 color) {
                bar(out, c - rising, c + rising, view, 1.6f * px + grow, color);
                bar(out, c - falling, c + falling, view, 1.6f * px + grow, color);
            },
            {.85f, .06f, .04f});
        break;
    }
    case AimMark::target: {
        const float size = std::isfinite(marker.radius) ? marker.radius * 1.25f : 0.f;
        const float half = std::max(size, 12 * px) * (1 - .3f * squeeze), arm = half * .45f;
        layers(
            [&](Vec3 c, float grow, Vec3 color) {
                for (const float sx : {-1.f, 1.f})
                    for (const float sy : {-1.f, 1.f}) {
                        const Vec3 corner = c + right * (sx * half) + up * (sy * half);
                        bar(out, corner, corner - right * (sx * arm), view, 1.6f * px + grow, color);
                        bar(out, corner, corner - up * (sy * arm), view, 1.6f * px + grow, color);
                    }
                disc(out, c, right, up, 2 * px + grow, color);
            },
            {1, .62f, .08f});
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
} // namespace spidy
