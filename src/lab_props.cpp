#include "spidy/lab_props.hpp"
#include <iterator>
#include <limits>

namespace spidy {
namespace {
constexpr float knockSpeed = 4;     // an impact this fast knocks a standing thug over
constexpr float restSeconds = 2.5f; // lying still this long before getting up
constexpr float riseSeconds = .8f;
LabProp object(std::uint64_t id, Vec3 base, Vec3 half, float mass, float radius, Vec3 color) {
    LabProp p;
    p.id = id;
    p.kind = TargetKind::Object;
    p.position = base + Vec3{0, radius, 0};
    p.half = half;
    p.color = color;
    p.mass = mass;
    p.radius = radius;
    return p;
}
// A thug standing on `feet`, facing along yaw (radians from -Z, like the rig).
LabProp thug(std::uint64_t id, Vec3 feet, float yaw, Vec3 jacket) {
    LabProp p;
    p.id = id;
    p.kind = TargetKind::Character;
    p.position = feet + Vec3{0, LabProps::standingHeight, 0};
    p.color = jacket;
    p.mass = 80;
    p.radius = .4f;
    p.orientation = Quat::yaw(yaw);
    return p;
}
Quat normalizedQuat(Quat q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-6f ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}
Quat turned(Quat q, Vec3 spin, float dt) {
    const float angle = length(spin) * dt;
    if (angle < 1e-6f)
        return q;
    const Vec3 axis = normalized(spin) * std::sin(angle / 2);
    return normalizedQuat(Quat{axis.x, axis.y, axis.z, std::cos(angle / 2)} * q);
}
// Upright, keeping the way the body faces.
Quat upright(Quat q) {
    const Vec3 forward = q.rotate({0, 0, -1});
    const Vec3 flat{forward.x, 0, forward.z};
    return length(flat) > .1f ? Quat::yaw(std::atan2(-flat.x, -flat.z)) : Quat{};
}
Quat blend(Quat a, Quat b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0)
        b = {-b.x, -b.y, -b.z, -b.w};
    return normalizedQuat(
        {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
}
// A standing or rising thug is moved by its own legs: walls and impacts
// below the knock speed do not push it.
bool kinematic(const LabProp& p) {
    return p.kind == TargetKind::Character && p.stance != LabProp::Stance::Tumbling;
}
} // namespace

LabProps::LabProps() {
    const Vec3 wood{.55f, .36f, .18f}, paint{.72f, .22f, .08f}, steel{.12f, .32f, .17f};
    const Vec3 crate{.3f, .3f, .3f}, barrel{.3f, .4f, .3f};
    std::uint64_t objects = 1001, people = 2001;
    // The launch rooftop (top at 18 m): within reach of the start.
    initial_.push_back(object(objects++, {-2.2f, 18, 15.5f}, crate, 25, .32f, wood));
    initial_.push_back(object(objects++, {2, 18, 14.5f}, crate, 25, .32f, wood));
    initial_.push_back(object(objects++, {3.8f, 18, 17.5f}, barrel, 40, .4f, paint));
    initial_.push_back(thug(people++, {-3.2f, 18, 13.2f}, 3.1416f, {.13f, .13f, .16f}));
    initial_.push_back(thug(people++, {3.2f, 18, 12.8f}, 3.1416f, {.3f, .1f, .1f}));
    // The street below and ahead.
    initial_.push_back(object(objects++, {-3, 0, 4}, crate, 25, .32f, wood));
    initial_.push_back(object(objects++, {6, 0, -1}, crate, 25, .32f, wood));
    initial_.push_back(object(objects++, {2, 0, -12}, crate, 25, .32f, wood));
    initial_.push_back(object(objects++, {-8, 0, -6}, barrel, 40, .4f, paint));
    initial_.push_back(object(objects++, {8, 0, -10}, barrel, 40, .4f, paint));
    initial_.push_back(object(objects++, {-12, 0, -4}, {1, .6f, .55f}, 300, .6f, steel));
    const Vec3 feet[] = {{-5, 0, 2}, {4, 0, -3}, {0, 0, -9}, {-7, 0, -15}, {6, 0, -18}};
    for (unsigned i = 0; i < std::size(feet); ++i)
        initial_.push_back(thug(people++, feet[i], 3.1416f + .3f * static_cast<float>(i % 3) - .3f,
                                i % 2 ? Vec3{.13f, .13f, .16f} : Vec3{.3f, .1f, .1f}));
    props_ = initial_;
}
LabProps::LabProps(std::vector<LabProp> props) : props_(props), initial_(std::move(props)) {}
void LabProps::reset() {
    props_ = initial_;
    accumulator_ = 0;
    knockdowns_ = 0;
}
std::optional<GrabTarget> LabProps::pick(Vec3 origin, Vec3 direction, float distance, float cone) const {
    const LabProp* best{};
    float bestMiss = cone, bestDistance = std::numeric_limits<float>::infinity();
    for (const auto& p : props_) {
        // A standing thug is a tall target: its sphere covers head to knees.
        const float radius = p.kind == TargetKind::Character && p.stance != LabProp::Stance::Tumbling
                                 ? p.radius * 1.5f
                                 : p.radius;
        const float miss = rayMiss(origin, direction, distance, p.position, radius);
        const float far = length(p.position - origin);
        if (miss > bestMiss + 1e-4f || (miss > bestMiss - 1e-4f && far >= bestDistance))
            continue;
        best = &p;
        bestMiss = miss;
        bestDistance = far;
    }
    if (!best)
        return {};
    return find(best->id);
}
std::optional<GrabTarget> LabProps::find(std::uint64_t id) const {
    for (const auto& p : props_)
        if (p.id == id)
            return GrabTarget{p.id, p.kind, p.position, p.velocity, p.mass, p.radius};
    return {};
}
void LabProps::characters(std::vector<GrabTarget>& out) const {
    for (const auto& p : props_)
        if (p.kind == TargetKind::Character)
            out.push_back({p.id, p.kind, p.position, p.velocity, p.mass, p.radius});
}
void LabProps::knock(LabProp& p) {
    if (p.kind != TargetKind::Character || p.stance == LabProp::Stance::Tumbling)
        return;
    p.stance = LabProp::Stance::Tumbling;
    p.still = p.rise = 0;
    ++knockdowns_;
}
void LabProps::step(float dt, std::span<const TargetCommand> commands, const World& world) {
    if (!std::isfinite(dt) || dt <= 0 || dt > .05f)
        return;
    const float gravity = 9.81f;
    for (std::size_t index = 0; index < props_.size(); ++index) {
        auto& p = props_[index];
        const TargetCommand* command{};
        for (const auto& c : commands)
            if (c.id == p.id)
                command = &c;
        const Vec3 next =
            command ? spidy::advance(*command, p.position, p.velocity, dt, {0, -gravity, 0}) : Vec3{};
        if (command && !finite(next))
            command = nullptr;
        if (command) {
            knock(p);
            p.velocity = next;
            p.still = 0;
            // A throw sets it tumbling end over end; a held one settles.
            if (command->thrown)
                p.spin = cross({0, 1, 0}, p.velocity) * (.6f / std::max(p.radius, .1f));
            else
                p.spin = p.spin * std::exp(-2 * dt);
        } else if (p.kind == TargetKind::Character && p.stance == LabProp::Stance::Standing) {
            continue;
        } else if (p.kind == TargetKind::Character && p.stance == LabProp::Stance::Rising) {
            p.rise = std::min(1.f, p.rise + dt / riseSeconds);
            const float t = p.rise * p.rise * (3 - 2 * p.rise);
            const Vec3 standing = p.lying + Vec3{0, standingHeight - p.radius, 0};
            p.position = p.lying + (standing - p.lying) * t;
            p.orientation = blend(p.orientation, upright(p.orientation), std::min(1.f, 6 * dt));
            if (p.rise >= 1) {
                p.stance = LabProp::Stance::Standing;
                p.orientation = upright(p.orientation);
            }
            continue;
        } else {
            p.velocity.y -= gravity * dt;
        }
        const auto moved = world.sweep(p.position, p.position + p.velocity * dt, p.radius);
        if (finite(moved.position))
            p.position = moved.position;
        p.grounded = false;
        if (moved.hit) {
            const Vec3 n = normalized(moved.normal);
            const float into = dot(p.velocity, n);
            if (into < 0) {
                // Slow contacts stop dead, so resting props do not buzz.
                const float bounce = into < -2 ? (p.kind == TargetKind::Character ? .1f : .3f) : 0.f;
                p.velocity -= n * (into * (1 + bounce));
                if (into < -3)
                    p.spin = p.spin + cross(n, p.velocity) * (.5f / std::max(p.radius, .1f));
            }
            p.grounded = n.y > .65f;
        }
        if (p.grounded && !command) {
            const float keep = std::exp(-(p.kind == TargetKind::Character ? 5.f : 3.f) * dt);
            p.velocity.x *= keep;
            p.velocity.z *= keep;
            p.spin = p.spin * std::exp(-4 * dt);
        }
        p.orientation = turned(p.orientation, p.spin, dt);
        if (p.kind == TargetKind::Character && p.stance == LabProp::Stance::Tumbling) {
            p.still = p.grounded && !command && length(p.velocity) < .5f ? p.still + dt : 0;
            if (p.still >= restSeconds) {
                p.stance = LabProp::Stance::Rising;
                p.rise = 0;
                p.lying = p.position;
                p.velocity = p.spin = {};
            }
        }
        // Lost off the edge of the world: back where it started.
        if (p.position.y < -30 || std::abs(p.position.x) > 600 || std::abs(p.position.z) > 900)
            p = initial_[index];
    }
    // Sphere contacts between props. Standing thugs give way only to a hard hit.
    for (std::size_t i = 0; i < props_.size(); ++i)
        for (std::size_t j = i + 1; j < props_.size(); ++j) {
            auto& a = props_[i];
            auto& b = props_[j];
            const Vec3 delta = b.position - a.position;
            const float distance = length(delta), reach = a.radius + b.radius;
            if (distance >= reach || distance < 1e-5f)
                continue;
            const Vec3 n = delta / distance;
            const float closing = -dot(b.velocity - a.velocity, n);
            if (closing > knockSpeed) {
                if (kinematic(a))
                    knock(a);
                if (kinematic(b))
                    knock(b);
            }
            const float inverseA = kinematic(a) ? 0 : 1 / a.mass, inverseB = kinematic(b) ? 0 : 1 / b.mass;
            const float inverse = inverseA + inverseB;
            if (inverse <= 0)
                continue;
            const float overlap = reach - distance;
            a.position -= n * (overlap * inverseA / inverse);
            b.position += n * (overlap * inverseB / inverse);
            if (closing > 0) {
                const float impulse = (1 + .2f) * closing / inverse;
                a.velocity -= n * (impulse * inverseA);
                b.velocity += n * (impulse * inverseB);
                if (closing > knockSpeed) {
                    a.spin = a.spin + cross(n, a.velocity) * (.4f / std::max(a.radius, .1f));
                    b.spin = b.spin + cross(n, b.velocity) * (.4f / std::max(b.radius, .1f));
                }
            }
            a.still = b.still = 0;
        }
    // A contact can push a prop into the floor, where the world's sweep no
    // longer sees it: lift it back out from above.
    for (auto& p : props_) {
        if (kinematic(p))
            continue;
        const auto out = world.sweep(p.position + Vec3{0, p.radius, 0}, p.position, p.radius);
        if (out.hit && normalized(out.normal).y > .65f && finite(out.position))
            p.position = out.position;
    }
}
void LabProps::advance(float seconds, WebGrab& grab, const World& world) {
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > .1f)
        return;
    const float dt = grab.config().fixedStep;
    accumulator_ += seconds;
    int steps = 0;
    while (accumulator_ + 1e-9 >= dt && steps++ < 13) {
        commands_.clear();
        grab.step(dt, world, *this, commands_);
        step(dt, commands_, world);
        accumulator_ -= dt;
    }
}
} // namespace spidy
