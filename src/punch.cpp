#include "spidy/punch.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace spidy {
namespace {
// The first share t of the step d at which a point p + t d comes within r of
// the origin (0 when it starts there).
bool enters(Vec3 p, Vec3 d, float r, float& t) {
    const float c = dot(p, p) - r * r;
    if (c <= 0) {
        t = 0;
        return true;
    }
    const float a = dot(d, d), b = 2 * dot(p, d);
    if (a < 1e-12f)
        return false;
    const float discriminant = b * b - 4 * a * c;
    if (discriminant < 0)
        return false;
    const float root = (-b - std::sqrt(discriminant)) / (2 * a);
    if (root < 0 || root > 1)
        return false;
    t = root;
    return true;
}
// The capsule's axis: from the centre of its bottom cap to that of its top.
float bottomOf(const PunchTarget& t) {
    return t.feet.y + t.radius;
}
float topOf(const PunchTarget& t) {
    return t.feet.y + std::max(t.radius, t.height - t.radius);
}
} // namespace

bool sweepCapsule(Vec3 from, Vec3 to, float sphereRadius, const PunchTarget& target, float& share, Vec3& point) {
    const float r = target.radius + sphereRadius, bottom = bottomOf(target), top = topOf(target);
    const Vec3 d = to - from;
    float best = 2;
    float t{};
    // The side, an upright cylinder between the caps' centres.
    if (enters({from.x - target.feet.x, 0, from.z - target.feet.z}, {d.x, 0, d.z}, r, t)) {
        const float y = from.y + d.y * t;
        if (y >= bottom && y <= top)
            best = t;
    }
    // The caps; between them the side meets the fist first.
    for (const float y : {bottom, top})
        if (enters(from - Vec3{target.feet.x, y, target.feet.z}, d, r, t) && t < best) {
            const float at = from.y + d.y * t;
            if ((y == bottom && at <= bottom + 1e-4f) || (y == top && at >= top - 1e-4f))
                best = t;
        }
    if (best > 1)
        return false;
    share = best;
    const Vec3 centre = from + d * best;
    const Vec3 axis{target.feet.x, std::clamp(centre.y, bottom, top), target.feet.z};
    const Vec3 out = normalized(centre - axis);
    // The fist's surface where it meets the character.
    point = length(out) > .5f ? centre - out * sphereRadius : centre;
    return true;
}

Punches::Punches(PunchConfig c) : config_(c) {
    const auto bad = [](float v) { return !std::isfinite(v) || v < 0; };
    if (bad(c.fistRadius) || bad(c.minSpeed) || c.minSpeed == 0 || bad(c.fullSpeed) || c.fullSpeed <= c.minSpeed ||
        bad(c.rearmSpeed) || bad(c.rearmTime) || bad(c.smoothing) || bad(c.minDamage) || bad(c.maxDamage) ||
        c.maxDamage < c.minDamage || !std::isfinite(c.uppercutRise) || bad(c.uppercutStrength) ||
        !std::isfinite(c.minInward) || c.minInward > 1)
        throw std::invalid_argument("Invalid punch configuration");
}
void Punches::reset() {
    hands_ = {};
}
PunchEvent Punches::blow(Vec3 direction, float speed) const {
    PunchEvent e;
    e.direction = normalized(direction);
    e.speed = speed;
    e.strength = std::clamp((speed - config_.minSpeed) / (config_.fullSpeed - config_.minSpeed), 0.f, 1.f);
    e.damage = config_.minDamage + (config_.maxDamage - config_.minDamage) * e.strength;
    // A light jab makes him flinch, a hard one knocks him down or sends him
    // flying; a hard blow from below lifts him off his feet.
    if (e.direction.y > config_.uppercutRise && e.strength >= config_.uppercutStrength)
        e.knockback = Knockback::PopUp;
    else if (speed >= config_.fullSpeed * 1.4f)
        e.knockback = Knockback::SuperFlyBack;
    else if (e.strength >= .85f)
        e.knockback = Knockback::FlyBack;
    else if (e.strength >= .6f)
        e.knockback = Knockback::Knockdown;
    else if (e.strength >= .35f)
        e.knockback = Knockback::Knockback;
    else if (e.strength >= .1f)
        e.knockback = Knockback::Stagger;
    else
        e.knockback = Knockback::Twitch;
    return e;
}
void Punches::turn(float radians) {
    if (!std::isfinite(radians) || radians == 0)
        return;
    turn(Quat::yaw(radians));
}
void Punches::turn(Quat q) {
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(n) || std::abs(n - 1) > .01f)
        return;
    for (auto& h : hands_) {
        h.relative = q.rotate(h.relative);
        h.velocity = q.rotate(h.velocity);
    }
}
void Punches::update(float seconds, const std::array<PunchHand, 2>& in, std::span<const PunchTarget> targets,
                     std::vector<PunchEvent>& out) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return; // the same sample again
    seconds = std::min(seconds, .1f);
    for (int i = 0; i < 2; ++i) {
        auto& h = hands_[static_cast<size_t>(i)];
        const auto& s = in[static_cast<size_t>(i)];
        if (!s.tracked || !finite(s.fist) || !finite(s.relative)) {
            h = {};
            continue;
        }
        if (!h.seen) {
            h = {};
            h.seen = true;
            h.fist = s.fist;
            h.relative = s.relative;
            continue;
        }
        // The arm's own motion: the hand relative to the player.
        const Vec3 measured = (s.relative - h.relative) / seconds;
        const float keep = config_.smoothing > 0 ? std::exp(-seconds / config_.smoothing) : 0.f;
        h.velocity = measured * (1 - keep) + h.velocity * keep;
        h.sinceHit += seconds;
        const float speed = length(h.velocity);
        if (!h.armed && (speed < config_.rearmSpeed || h.sinceHit > config_.rearmTime))
            h.armed = true;
        const Vec3 from = h.fist, to = s.fist;
        h.fist = s.fist;
        h.relative = s.relative;
        if (!h.armed || s.busy || speed < config_.minSpeed)
            continue;
        // The first character the fist meets on its way.
        const PunchTarget* hit{};
        float first = 2;
        Vec3 at{};
        for (const auto& t : targets) {
            float share{};
            Vec3 point{};
            if (sweepCapsule(from, to, config_.fistRadius, t, share, point) && share < first) {
                first = share;
                hit = &t;
                at = point;
            }
        }
        if (!hit)
            continue;
        // A blow into his middle or his head; a graze across his front or a
        // fist pulled back out of him is no punch. An uppercut drives up
        // into the chin.
        float into = -1;
        for (const float share : {.6f, 1.f}) {
            const Vec3 aim = hit->feet + Vec3{0, std::max(hit->height * .6f, hit->height * share - .2f), 0};
            const Vec3 inward = normalized(aim - at);
            into = std::max(into, length(inward) > .5f ? dot(h.velocity / speed, inward) : 1.f);
        }
        if (into < config_.minInward)
            continue;
        auto e = blow(h.velocity, speed);
        e.hand = i;
        e.target = hit->id;
        e.point = at;
        out.push_back(e);
        h.armed = false;
        h.sinceHit = 0;
        h.lastTarget = hit->id;
    }
}
} // namespace spidy
