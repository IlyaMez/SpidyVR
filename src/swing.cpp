#include "spidy/swing.hpp"
#include <stdexcept>

namespace spidy {
namespace {
// A body this close to the end of its rope is held by it. After an exact
// projection the distance equals the length only to float rounding, about a
// millimetre at game world coordinates; an exact comparison let rounding
// choose which of two ropes acted on the velocity in a step.
constexpr float nearlyTaut = .005f;
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
    const float nonnegative[] = {
        c.gravity,          c.reelSpeed,       c.airAcceleration, c.groundAcceleration,
        c.zipMultiplier,    c.maxZipImpulse,   c.jumpSpeed,       c.pointLaunchWindow,
        c.zipLandingWindow, c.obstructionTime, c.anchorClearance};
    for (float v : nonnegative)
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid swing configuration");
    if (c.minRope > c.maxRange || c.fixedStep > .02f)
        throw std::invalid_argument("Invalid rope range or fixed timestep");
}
void Swing::limitSpeed(float maxSpeed) {
    if (!std::isfinite(maxSpeed) || maxSpeed <= 0)
        throw std::invalid_argument("Invalid swing speed limit");
    config_.maxSpeed = maxSpeed;
}
void Swing::setGravity(float gravity) {
    if (!std::isfinite(gravity) || gravity < 0)
        throw std::invalid_argument("Invalid swing gravity");
    config_.gravity = gravity;
}
void Swing::allowAirAnchors(bool allowed) {
    config_.airAnchors = allowed;
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
    hands_[i].pullDistance = hands_[i].pullTime = hands_[i].obstructed = hands_[i].winch = 0;
}
bool Swing::blocks(const RayHit& hit, Vec3 from, Vec3 anchor) const {
    const float clearance = std::max(config_.anchorClearance, .1f * length(anchor - from));
    return length(hit.point - anchor) > clearance && length(hit.point - from) > config_.radius;
}
std::array<float, 2> Swing::reeled(std::array<float, 2> rates, float seconds) const {
    std::array<float, 2> lengths{webs_[0].length, webs_[1].length}, taken{};
    for (int i = 0; i < 2; ++i)
        if (webs_[i].attached) {
            const float wanted = std::max(config_.minRope, lengths[i] - rates[i] * seconds);
            taken[i] = std::max(0.f, lengths[i] - wanted);
            lengths[i] = wanted;
        }
    if (webs_[0].attached && webs_[1].attached) {
        // Opposing winches cannot shorten their combined length below
        // the span between anchors. Leave clearance for the body too.
        // Each gives back in proportion to what it took: when one hand's
        // winch kept the last of the rope, it kicked the body to its side.
        const float span = length(webs_[0].anchor - webs_[1].anchor) + config_.radius * 2;
        const float took = taken[0] + taken[1];
        const float back = std::min(span - lengths[0] - lengths[1], took);
        if (back > 0)
            for (int i = 0; i < 2; ++i)
                lengths[i] += back * taken[i] / took;
    }
    return lengths;
}
float Swing::follow(int i, Vec3 radial) const {
    const auto& other = webs_[1 - i];
    if (!other.attached)
        return 1;
    const Vec3 delta = body_.position - other.anchor;
    const float dist = length(delta);
    if (dist < other.length - nearlyTaut || dist < 1e-5f)
        return 1; // the other rope is slack
    // Two taut ropes at an angle carry the body faster than either shortens,
    // without bound as they come to oppose each other. Follow that exactly up
    // to a right angle between them; past it, hold the carried speed there.
    const float cosHalfAngle = std::sqrt(std::max(0.f, (1 + dot(radial, delta / dist)) / 2));
    return std::min(1.f, cosHalfAngle * 1.4142135f);
}
WebShot Swing::shot(Pose aim, Vec3 from, const WorldQueries& world) const {
    WebShot out;
    const Vec3 forward = aim.orientation.rotate({0, 0, -1});
    if (!finite(aim.position) || !finite(from) || !finite(forward) || length(forward) < .9f || length(forward) > 1.1f)
        return out;
    const Vec3 direction = normalized(forward);
    out.hit = world.raycast(aim.position, direction, config_.maxRange);
    const bool airAnchor = !out.hit && config_.airAnchors;
    if (!out.hit && !airAnchor)
        return out;
    const RayHit anchor = airAnchor ? RayHit{aim.position + direction * config_.maxRange, {}, 0, true} : *out.hit;
    // Confirm the body has a clear line to the same anchor. Hands cannot
    // shoot through a wall while the body remains on its other side.
    if (anchor.fixed && (airAnchor || world.exists(anchor.surface)) && finite(anchor.point)) {
        const Vec3 delta = anchor.point - from;
        const float dist = length(delta);
        const auto obstacle = world.raycast(from, normalized(delta), std::max(0.0f, dist - .08f));
        if (length(anchor.point - aim.position) <= config_.maxRange + .01f && dist >= config_.minRope &&
            !(obstacle && blocks(*obstacle, from, anchor.point)))
            out.web = Web{true, anchor.point, anchor.surface, dist, 0, airAnchor};
    }
    return out;
}
void Swing::releaseAll() {
    for (int i = 0; i < 2; ++i) {
        release(i);
        hands_[i].sample = false;
        hands_[i].held = true; // a grip already held is not a new shot
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
        const bool valid = h.tracked && finite(h.aim.position) && finite(h.gripRelativeToHead) &&
                           std::isfinite(in.trackingYaw) && std::isfinite(h.trigger) && std::isfinite(h.grip);
        const Vec3 forward = h.aim.orientation.rotate({0, 0, -1});
        if (!valid || !finite(forward) || length(forward) < .9f || length(forward) > 1.1f) {
            release(i, EventKind::TrackingLost);
            s.sample = false;
            s.held = true; // require a full release before the next shot
            continue;
        }
        // Squeezing the grip shoots this hand's web; holding it keeps the web.
        const bool held = s.held ? h.grip > .35f : h.grip > .65f;
        const bool shoot = held && !s.held;
        s.held = held;
        if (w.attached && (!held || (!w.airAnchor && !world.exists(w.surface))))
            release(i);
        if (!w.attached && shoot) {
            if (const auto fired = shot(h.aim, body_.position, world); fired.web) {
                w = *fired.web;
                s.triggerReleased = false;
                s.reeling = false;
                s.zipUsed = false;
                s.pullDistance = s.pullTime = s.obstructed = 0;
                events_.push_back({EventKind::Attach, i, .65f});
            } else {
                events_.push_back({EventKind::Miss, i, .15f});
            }
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
        if (!w.airAnchor && !world.exists(w.surface)) {
            release(i, EventKind::Obstructed);
            continue;
        }
        const Vec3 d = w.anchor - body_.position;
        const float dist = length(d);
        const auto hit = world.raycast(body_.position, normalized(d), std::max(0.0f, dist - .1f));
        // Swinging along a facade, the line grazes ledges and sills near the
        // anchor every few frames. Only a wall that stays in the way releases.
        auto& obstructed = hands_[i].obstructed;
        obstructed = hit && blocks(*hit, body_.position, w.anchor) ? obstructed + dt : 0;
        if (obstructed > 0 && obstructed >= config_.obstructionTime) {
            release(i, EventKind::Obstructed);
            continue;
        }
    }
    std::array<float, 2> rates{};
    for (int i = 0; i < 2; ++i)
        if (webs_[i].attached && hands_[i].reeling)
            rates[i] = config_.reelSpeed;
    const auto lengths = reeled(rates, dt);
    for (int i = 0; i < 2; ++i) {
        hands_[i].winch = (webs_[i].length - lengths[i]) / dt;
        webs_[i].length = lengths[i];
    }
    if (webs_[0].attached && webs_[1].attached)
        move(projectTwoWebs(body_.position, webs_[0], webs_[1]), collision);

    // Unilateral, tension-only constraints. Slack rope never pushes the player.
    // Alternate order to reduce left/right bias with two simultaneous ropes.
    float pull[2]{}; // speed each rope has taken out of the body in this step
    for (int pass = 0; pass < 8; ++pass)
        for (int j = 0; j < 2; ++j) {
            const int i = (pass % 2) ? 1 - j : j;
            auto& w = webs_[i];
            if (!w.attached)
                continue;
            const Vec3 delta = body_.position - w.anchor;
            const float dist = length(delta);
            if (dist < w.length - nearlyTaut || dist < 1e-5f)
                continue;
            const Vec3 radial = delta / dist;
            if (dist > w.length)
                move(w.anchor + radial * w.length, collision);
            // A taut rope that is being reeled carries the body inward at the
            // winch rate. With the pull applied to position only, the velocity
            // never held it, and letting go mid-reel stopped the body dead.
            const float outward = dot(body_.velocity, radial) + hands_[i].winch * follow(i, radial);
            // Once the other rope has pulled too, this one may have taken more
            // than the two need together. It hands that back, never more than
            // it took: kept, the surplus pushed the body sideways every step.
            const float change = std::max(outward, -pull[i]);
            pull[i] += change;
            body_.velocity -= radial * change;
        }
    for (int i = 0; i < 2; ++i)
        webs_[i].tension = webs_[i].attached ? pull[i] / dt : 0;
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
void Swing::settleStep(float predictedSeconds, float actualSeconds) {
    const float extra = actualSeconds - predictedSeconds;
    if (!std::isfinite(extra) || std::abs(extra) > .1f)
        return;
    // Only what the winch really took in: a rope held at its shortest length
    // did not shorten, and must not start breathing with the frame time.
    const auto lengths = reeled({hands_[0].winch, hands_[1].winch}, extra);
    for (int i = 0; i < 2; ++i)
        if (webs_[i].attached)
            webs_[i].length = lengths[i];
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
        !std::isfinite(inputSeconds) || inputSeconds < 0 || !finite(actual.position) ||
        !finite(actual.velocity) || length(actual.velocity) > config_.maxSpeed * 2) {
        releaseAll();
        return {};
    }
    if (actual.grounded && !body_.grounded)
        sinceLanding_ = 0;
    body_ = actual;
    body_.velocity = limited(body_.velocity, config_.maxSpeed);
    if (inputSeconds > 0) {
        // The first sample after a gap in input (a long frame, a moment
        // without focus) starts each hand's motion afresh: travel across the
        // gap is no yank. Releasing the webs there dropped the player.
        if (inputSeconds > .1f)
            for (auto& hand : hands_)
                hand.sample = false;
        inputs(inputSeconds, input, world);
    }
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
