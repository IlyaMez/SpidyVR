#include "spidy/swing.hpp"
#include <stdexcept>

namespace spidy {
namespace {
Vec3 projectBall(Vec3 p, Vec3 center, float radius) {
    return center + limited(p - center, radius);
}

Vec3 projectTwoWebs(Vec3 p, const Web& a, const Web& b) {
    // Exact projection onto the intersection of two rope-length spheres.
    // Alternating single-rope projections converge poorly near opposing anchors.
    const auto onA = projectBall(p, a.anchor, a.length);
    if (length(onA - b.anchor) <= b.length)
        return onA;
    const auto onB = projectBall(p, b.anchor, b.length);
    if (length(onB - a.anchor) <= a.length)
        return onB;
    const Vec3 span = b.anchor - a.anchor;
    const float distance = length(span);
    if (distance < 1e-5f)
        return projectBall(p, a.anchor, std::min(a.length, b.length));
    const Vec3 axis = span / distance;
    const float along = (a.length * a.length - b.length * b.length + distance * distance) / (2 * distance);
    const Vec3 center = a.anchor + axis * along;
    const float radius = std::sqrt(std::max(0.f, a.length * a.length - along * along));
    Vec3 radial = p - center - axis * dot(p - center, axis);
    if (length(radial) < 1e-5f)
        radial = cross(axis, std::abs(axis.y) > .95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
    return center + normalized(radial) * radius;
}
} // namespace

Swing::Swing(SwingConfig c) : config_(c) {
    const float positive[] = {c.radius,    c.maxRange,     c.minRope,    c.maxSpeed,
                              c.yankSpeed, c.yankDistance, c.yankWindow, c.fixedStep};
    for (float v : positive)
        if (!std::isfinite(v) || v <= 0)
            throw std::invalid_argument("Invalid swing configuration");
    const float nonnegative[] = {c.gravity,         c.reelSpeed,     c.airAcceleration, c.groundAcceleration,
                                 c.zipMultiplier,   c.maxZipImpulse, c.jumpSpeed,       c.pointLaunchWindow,
                                 c.zipLandingWindow};
    for (float v : nonnegative)
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid swing configuration");
    if (c.minRope > c.maxRange || c.fixedStep > .02f)
        throw std::invalid_argument("Invalid rope range or fixed timestep");
}
void Swing::reset(Body b) {
    if (!finite(b.position) || !finite(b.velocity))
        throw std::invalid_argument("Invalid body");
    body_ = b;
    webs_ = {};
    hands_ = {};
    events_.clear();
    accumulator_ = 0;
    sinceZip_ = sinceLanding_ = 100;
    zipSpeed_ = 0;
    zipDirection_ = {};
    jumpHeld_ = jumpQueued_ = false;
}
void Swing::release(int i, EventKind reason) {
    if (webs_[i].attached)
        events_.push_back({reason, i, .2f});
    webs_[i] = {};
    hands_[i].zipUsed = false;
    hands_[i].reeling = false;
    hands_[i].pullDistance = hands_[i].pullTime = 0;
}
void Swing::releaseAll() {
    for (int i = 0; i < 2; ++i) {
        release(i);
        hands_[i].sample = false;
        hands_[i].blocked = true;
    }
    jumpQueued_ = false;
    accumulator_ = 0;
    sinceZip_ = sinceLanding_ = 100;
    zipSpeed_ = 0;
    zipDirection_ = {};
}
void Swing::inputs(float dt, const Input& in, const WorldQueries& world) {
    jumpQueued_ |= in.jump && !jumpHeld_;
    jumpHeld_ = in.jump;
    for (int i = 0; i < 2; ++i) {
        const auto& h = in.hands[i];
        auto& s = hands_[i];
        auto& w = webs_[i];
        s.retryAfter = std::max(0.f, s.retryAfter - dt);
        const bool valid = h.tracked && finite(h.aim.position) && finite(h.gripRelativeToHead) &&
                           std::isfinite(in.trackingYaw) && std::isfinite(h.trigger) && std::isfinite(h.grip);
        const Vec3 forward = h.aim.orientation.rotate({0, 0, -1});
        if (!valid || !finite(forward) || length(forward) < .9f || length(forward) > 1.1f) {
            release(i, EventKind::TrackingLost);
            s.sample = false;
            s.blocked = true;
            continue;
        }
        if (h.grip < .35f)
            s.blocked = false;
        const bool chord = h.trigger > .65f && h.grip > .65f;
        if (w.attached && (h.grip < .35f || (!w.airAnchor && !world.exists(w.surface))))
            release(i);
        if (!w.attached && chord && (!s.chord || s.retryAfter <= 0) && !s.blocked) {
            s.retryAfter = .15f;
            auto hit = world.raycast(h.aim.position, normalized(forward), config_.maxRange);
            const bool airAnchor = !hit && config_.airAnchors;
            if (airAnchor)
                hit = RayHit{h.aim.position + normalized(forward) * config_.maxRange, {}, 0, true};
            // Confirm the body has a clear line to the same anchor. Hands cannot
            // shoot through a wall while the body remains on its other side.
            if (hit && hit->fixed && (airAnchor || world.exists(hit->surface)) && finite(hit->point)) {
                const Vec3 delta = hit->point - body_.position;
                const float dist = length(delta);
                auto obstacle = world.raycast(body_.position, normalized(delta), std::max(0.0f, dist - .08f));
                if (length(hit->point - h.aim.position) <= config_.maxRange + .01f &&
                    dist >= config_.minRope && !obstacle) {
                    w = {true, hit->point, hit->surface, dist, 0, airAnchor};
                    s.triggerReleased = false;
                    s.reeling = false;
                    s.zipUsed = false;
                    s.pullDistance = s.pullTime = 0;
                    events_.push_back({EventKind::Attach, i, .65f});
                }
            }
            if (!w.attached)
                events_.push_back({EventKind::Miss, i, .15f});
        }
        if (w.attached) {
            if (h.trigger < .35f)
                s.triggerReleased = true;
            const bool reel = s.triggerReleased && h.trigger > .65f;
            if (reel && !s.reeling) {
                // A yank/forward swing leaves slack. Starting a winch should
                // engage at the current distance, not spend seconds winding in
                // invisible slack before the player feels any response.
                w.length = std::min(w.length, std::max(config_.minRope, length(w.anchor - body_.position)));
            }
            s.reeling = reel;
            // Motion must be both fast and deliberate within a short window.
            // Head-relative samples remove virtual movement and room-scale translation.
            if (s.sample && !s.zipUsed) {
                const Vec3 delta = Quat::yaw(in.trackingYaw).rotate(h.gripRelativeToHead - s.previous);
                const float pull = -dot(delta, normalized(w.anchor - body_.position));
                if (pull > 0) {
                    s.pullDistance += pull;
                    s.pullTime += dt;
                    if (s.pullTime > config_.yankWindow) {
                        s.pullDistance = pull;
                        s.pullTime = dt;
                    }
                    const float speed = pull / dt;
                    if (s.pullDistance >= config_.yankDistance && speed >= config_.yankSpeed) {
                        const float impulse = std::min(speed * config_.zipMultiplier, config_.maxZipImpulse);
                        body_.velocity =
                            limited(body_.velocity + normalized(w.anchor - body_.position) * impulse,
                                    config_.maxSpeed);
                        sinceZip_ = 0;
                        zipSpeed_ = length(body_.velocity);
                        zipDirection_ = normalized(Vec3{body_.velocity.x, 0, body_.velocity.z});
                        s.zipUsed = true;
                        events_.push_back({EventKind::Zip, i, 1});
                    }
                } else {
                    s.pullDistance = s.pullTime = 0;
                }
            }
        }
        s.chord = chord;
        s.previous = h.gripRelativeToHead;
        s.sample = true;
    }
}
void Swing::move(Vec3 target, const World* collision) {
    if (!collision) {
        // Internal prediction only. No collision has been performed here.
        body_.position = target;
        return;
    }
    const auto result = collision->sweep(body_.position, target, config_.radius);
    if (!finite(result.position))
        return;
    body_.position = result.position;
    if (result.hit) {
        const Vec3 n = normalized(result.normal);
        const float vn = dot(body_.velocity, n);
        if (vn < 0)
            body_.velocity -= n * vn;
        if (n.y > .65f)
            body_.grounded = true;
    }
}
void Swing::step(float dt, const Input& in, const WorldQueries& world, const World* collision) {
    sinceZip_ += dt;
    sinceLanding_ += dt;
    const bool wasGrounded = body_.grounded;
    if (jumpQueued_) {
        if (body_.grounded) {
            body_.velocity.y = config_.jumpSpeed;
            if (pointLaunchReady()) {
                Vec3 horizontal = normalized(Vec3{body_.velocity.x, 0, body_.velocity.z});
                if (length(horizontal) < .1f)
                    horizontal = normalized(Vec3{in.move.x, 0, in.move.z});
                if (length(horizontal) < .1f)
                    horizontal = zipDirection_;
                const float speed = std::min(zipSpeed_, config_.maxSpeed);
                body_.velocity.x = horizontal.x * speed;
                body_.velocity.z = horizontal.z * speed;
                body_.velocity.y = config_.jumpSpeed * 1.5f;
                sinceZip_ = 100;
                events_.push_back({EventKind::PointLaunch, -1, 1});
            }
            body_.grounded = false;
        }
        jumpQueued_ = false;
    }
    Vec3 steering = finite(in.move) ? limited(Vec3{in.move.x, 0, in.move.z}, 1) : Vec3{};
    body_.velocity +=
        steering * ((body_.grounded ? config_.groundAcceleration : config_.airAcceleration) * dt);
    if (body_.grounded) {
        const float damping = std::exp(-5 * dt);
        body_.velocity.x *= damping;
        body_.velocity.z *= damping;
    }
    // A native prediction has no sweep to cancel gravity at the floor. Keep
    // the measured support until a jump or rope pull actually lifts the body.
    const bool supported = !collision && body_.grounded && body_.velocity.y <= 0;
    if (supported)
        body_.velocity.y = 0;
    else
        body_.velocity.y -= config_.gravity * dt;
    body_.velocity = limited(body_.velocity, config_.maxSpeed);
    body_.grounded = supported;
    move(body_.position + body_.velocity * dt, collision);
    for (int i = 0; i < 2; ++i) {
        auto& w = webs_[i];
        if (!w.attached)
            continue;
        const Vec3 d = w.anchor - body_.position;
        const float dist = length(d);
        if ((!w.airAnchor && !world.exists(w.surface)) ||
            world.raycast(body_.position, normalized(d), std::max(0.0f, dist - .1f))) {
            release(i, EventKind::Obstructed);
            continue;
        }
        w.tension = 0;
        if (hands_[i].reeling) {
            float requested = std::max(config_.minRope, w.length - config_.reelSpeed * dt);
            const auto& other = webs_[1 - i];
            if (other.attached) {
                // Opposing winches cannot shorten their combined length below
                // the span between anchors. Leave clearance for the body too.
                const float span = length(w.anchor - other.anchor) + config_.radius * 2;
                requested = std::max(requested, std::min(w.length, span - other.length));
            }
            w.length = requested;
        }
    }
    if (webs_[0].attached && webs_[1].attached)
        move(projectTwoWebs(body_.position, webs_[0], webs_[1]), collision);

    // Unilateral, tension-only constraints. Slack rope never pushes the player.
    // Alternate order to reduce left/right bias with two simultaneous ropes.
    for (int pass = 0; pass < 8; ++pass)
        for (int j = 0; j < 2; ++j) {
            const int i = (pass % 2) ? 1 - j : j;
            auto& w = webs_[i];
            if (!w.attached)
                continue;
            const Vec3 delta = body_.position - w.anchor;
            const float dist = length(delta);
            if (dist < w.length || dist < 1e-5f)
                continue;
            const Vec3 radial = delta / dist;
            move(w.anchor + radial * w.length, collision);
            const float outward = dot(body_.velocity, radial);
            if (outward > 0) {
                body_.velocity -= radial * outward;
                w.tension += outward / dt;
            }
        }
    if (body_.grounded && !wasGrounded)
        sinceLanding_ = 0;
}
void Swing::update(float seconds, const Input& input, const World& world) {
    events_.clear();
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    if (!input.focused || seconds > .1f) {
        releaseAll();
        jumpHeld_ = input.jump;
        return;
    }
    inputs(seconds, input, world);
    accumulator_ += seconds;
    int steps = 0;
    while (accumulator_ + 1e-9 >= config_.fixedStep && steps++ < 13) {
        step(config_.fixedStep, input, world, &world);
        accumulator_ -= config_.fixedStep;
    }
}
MotionIntent Swing::predictNativeStep(float seconds, const Input& input, const WorldQueries& world,
                                      Body actual) {
    return predictNativeStep(seconds, input, world, actual, seconds);
}
MotionIntent Swing::predictNativeStep(float seconds, const Input& input, const WorldQueries& world,
                                      Body actual, float inputSeconds) {
    events_.clear();
    accumulator_ = 0;
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > .05f || !input.focused ||
        !std::isfinite(inputSeconds) || inputSeconds < 0 || inputSeconds > .1f || !finite(actual.position) ||
        !finite(actual.velocity) || length(actual.velocity) > config_.maxSpeed * 2) {
        releaseAll();
        return {};
    }
    if (actual.grounded && !body_.grounded)
        sinceLanding_ = 0;
    body_ = actual;
    body_.velocity = limited(body_.velocity, config_.maxSpeed);
    if (inputSeconds > 0)
        inputs(inputSeconds, input, world);
    const auto count = static_cast<unsigned>(std::ceil(seconds / config_.fixedStep));
    const float dt = seconds / count;
    for (unsigned i = 0; i < count; ++i)
        step(dt, input, world, nullptr);
    MotionIntent result{body_.position, body_.velocity, true};
    // Native collision owns position and ground contact. Do not publish the
    // prediction as if the game had already accepted it.
    body_.position = actual.position;
    body_.grounded = actual.grounded;
    return result;
}
} // namespace spidy
