#include "spidy/web_grab.hpp"
#include <limits>
#include <stdexcept>

namespace spidy {
namespace {
// Hand velocities come from 72-120 Hz tracking samples whose spacing jitters
// by a millisecond or two. Smooth over under one sample: every millisecond of
// lag carries a held target past a hand that stops.
constexpr float velocitySmoothing = .008f;
Vec3 smooth(Vec3 current, Vec3 sample, float seconds) {
    const float keep = std::exp(-seconds / velocitySmoothing);
    return sample + (current - sample) * keep;
}
// The step's command for this target so far, so a second hand's web starts
// from what the first one did to it.
TargetCommand* commandFor(std::vector<TargetCommand>& out, std::size_t first, std::uint64_t id) {
    for (auto i = first; i < out.size(); ++i)
        if (out[i].id == id)
            return &out[i];
    return nullptr;
}
} // namespace

float rayMiss(Vec3 origin, Vec3 direction, float distance, Vec3 centre, float radius) {
    const Vec3 delta = centre - origin;
    const float far = length(delta);
    if (!finite(delta) || !std::isfinite(radius))
        return std::numeric_limits<float>::infinity();
    if (far <= radius)
        return 0;
    const float along = dot(delta, direction);
    if (along <= 0 || far - radius > distance)
        return std::numeric_limits<float>::infinity();
    const float angle = std::acos(std::clamp(along / far, -1.f, 1.f));
    return std::max(0.f, angle - std::asin(radius / far));
}
std::optional<Vec3> aimThrow(Vec3 from, Vec3 velocity, Vec3 target, float cone, float gravity) {
    const float speed = length(velocity);
    const Vec3 delta = target - from;
    const float distance = length(delta);
    if (!finite(velocity) || !finite(delta) || speed < 1e-3f || distance < 1e-3f ||
        dot(velocity / speed, delta / distance) < std::cos(cone))
        return {};
    const Vec3 flat{delta.x, 0, delta.z};
    const float x = length(flat), y = delta.y;
    if (x < 1e-3f || gravity <= 0)
        return normalized(delta) * speed;
    // tan(angle) = (s^2 - sqrt(s^4 - g(g x^2 + 2 y s^2))) / (g x), the lower arc.
    const float s2 = speed * speed;
    const float root = s2 * s2 - gravity * (gravity * x * x + 2 * y * s2);
    if (root < 0)
        return normalized(delta) * speed;
    const float angle = std::atan((s2 - std::sqrt(root)) / (gravity * x));
    const Vec3 across = flat / x;
    return (across * std::cos(angle) + Vec3{0, std::sin(angle), 0}) * speed;
}

WebGrab::WebGrab(GrabConfig c) : config_(c) {
    const float positive[] = {
        c.maxRange,        c.maxMass,       c.yankSpeed,           c.yankDistance, c.yankWindow,
        c.webAcceleration, c.holdResponse,  c.maxHoldAcceleration, c.liftMass,     c.arrivalDeceleration,
        c.yankTimeout,     c.maxThrowSpeed, c.tetherStiffness,     c.fixedStep,    c.maxYankFlight,
        c.throwMultiplier};
    for (float v : positive)
        if (!std::isfinite(v) || v <= 0)
            throw std::invalid_argument("Invalid grab configuration");
    const float nonnegative[] = {c.aimCone,       c.holdDistance,   c.reelSpeed,       c.yankMultiplier,
                                 c.minYankFlight, c.catchDistance,  c.aimAssistCone,   c.aimAssistRange,
                                 c.slack,         c.minAssistSpeed, c.obstructionTime, c.breakStretch,
                                 c.gravity};
    for (float v : nonnegative)
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid grab configuration");
    if (c.tetherStiffness > 1 || c.minYankFlight > c.maxYankFlight || c.aimCone > 1 || c.fixedStep > .02f)
        throw std::invalid_argument("Invalid grab configuration");
}
std::optional<GrabTarget> WebGrab::preview(Pose aim, const WorldQueries& world,
                                           const TargetQueries& targets) const {
    const Vec3 direction = normalized(aim.orientation.rotate({0, 0, -1}));
    if (!finite(aim.position) || length(direction) < .5f)
        return {};
    // A target counts only in front of the first wall: the world hit, plus
    // the target's own depth, limits the pick. A hit on a target is that target.
    const auto wall = world.raycast(aim.position, direction, config_.maxRange);
    std::optional<GrabTarget> target;
    if (const auto own = wall ? targets.owner(wall->surface) : std::nullopt)
        target = targets.find(*own);
    const float reach = wall ? length(wall->point - aim.position) + .75f : config_.maxRange;
    if (!target)
        target = targets.pick(aim.position, direction, std::min(reach, config_.maxRange), config_.aimCone);
    if (!target || !finite(target->position) || !finite(target->velocity) || target->mass > config_.maxMass ||
        !std::isfinite(target->radius) || target->radius < 0)
        return {};
    return target;
}
float WebGrab::holdLength(int i) const {
    return config_.holdDistance + grabs_[i].radius;
}
void WebGrab::end(int i, GrabEventKind reason, Vec3 velocity) {
    auto& g = grabs_[i];
    if (g.phase != GrabPhase::None)
        events_.push_back({reason, i, g.target, reason == GrabEventKind::Lost ? .2f : .45f, velocity});
    g = {};
    auto& h = hands_[i];
    h.reeling = h.yankUsed = false;
    h.pullDistance = h.pullTime = h.obstructed = h.flight = h.stalled = 0;
    h.closest = std::numeric_limits<float>::infinity();
}
void WebGrab::letGo(int i, const TargetQueries& targets, const Body& player) {
    const auto& g = grabs_[i];
    const auto target = targets.find(g.target);
    if (!target || !finite(target->velocity)) {
        end(i, GrabEventKind::Lost);
        return;
    }
    if (g.phase == GrabPhase::Tethered) {
        // Only the web goes; the target keeps whatever the web gave it.
        end(i, GrabEventKind::Release);
        return;
    }
    // The arm's work is the target's motion relative to the player. Carried
    // along at swing speed, that alone must not be multiplied.
    const Vec3 base = finite(player.velocity) ? player.velocity : Vec3{};
    const Vec3 relative = limited((target->velocity - base) * config_.throwMultiplier, config_.maxThrowSpeed);
    Vec3 launch = base + relative;
    if (length(relative) >= config_.minAssistSpeed) {
        std::vector<GrabTarget> candidates;
        targets.characters(candidates);
        float best = config_.aimAssistCone;
        std::optional<Vec3> aimed;
        for (const auto& c : candidates) {
            const Vec3 delta = c.position - target->position;
            const float distance = length(delta);
            if (c.id == g.target || !finite(c.position) || distance > config_.aimAssistRange ||
                distance < .5f)
                continue;
            const float angle = std::acos(std::clamp(dot(normalized(launch), delta / distance), -1.f, 1.f));
            if (angle >= best)
                continue;
            if (auto v =
                    aimThrow(target->position, launch, c.position, config_.aimAssistCone, config_.gravity)) {
                best = angle;
                aimed = v;
            }
        }
        if (aimed)
            launch = *aimed;
    }
    // Both hands letting go together throw it once.
    if (std::none_of(pending_.begin(), pending_.end(),
                     [&](const TargetCommand& c) { return c.id == g.target; }))
        pending_.push_back({g.target, g.kind, launch, true});
    end(i, GrabEventKind::Throw, launch);
}
void WebGrab::releaseAll() {
    for (int i = 0; i < 2; ++i) {
        end(i, GrabEventKind::Lost);
        auto& h = hands_[i];
        h.sample = false;
        h.held = h.owned = true; // a grip already held is not a new shot
    }
}
Input WebGrab::claim(float seconds, const Input& in, const WorldQueries& world, const TargetQueries& targets,
                     const Body& player) {
    Input swing = in;
    if (!std::isfinite(seconds) || seconds < 0 || seconds > .1f || !in.focused) {
        // A paused or unfocused game: nothing may stay attached across it.
        events_.clear();
        releaseAll();
        for (auto& hand : swing.hands)
            hand.grip = hand.trigger = 0;
        return swing;
    }
    events_.clear();
    for (int i = 0; i < 2; ++i) {
        const auto& hand = in.hands[i];
        auto& h = hands_[i];
        auto& g = grabs_[i];
        if (seconds > 0) {
            const Vec3 forward = hand.aim.orientation.rotate({0, 0, -1});
            const bool valid = hand.tracked && finite(hand.aim.position) && finite(hand.gripRelativeToHead) &&
                               std::isfinite(in.trackingYaw) && std::isfinite(hand.trigger) &&
                               std::isfinite(hand.grip) && finite(forward) &&
                               std::abs(length(forward) - 1) < .1f;
            if (!valid) {
                end(i, GrabEventKind::Lost);
                h.sample = false;
                h.held = h.owned = true; // require a full release before the next shot
            } else {
                const Vec3 aim = normalized(forward);
                if (h.sample) {
                    h.wristVelocity =
                        smooth(h.wristVelocity, (hand.aim.position - h.wrist) / seconds, seconds);
                    h.forwardRate = smooth(h.forwardRate, (aim - h.forward) / seconds, seconds);
                } else {
                    h.wristVelocity = h.forwardRate = {};
                }
                h.wrist = hand.aim.position;
                h.forward = aim;
                const bool held = h.held ? hand.grip > .35f : hand.grip > .65f;
                const bool shoot = held && !h.held;
                h.held = held;
                if (!held)
                    h.owned = false;
                if (g.phase != GrabPhase::None && !held)
                    letGo(i, targets, player);
                if (g.phase == GrabPhase::None && shoot) {
                    if (const auto target = preview(hand.aim, world, targets)) {
                        g.phase = GrabPhase::Tethered;
                        g.target = target->id;
                        g.kind = target->kind;
                        g.radius = target->radius;
                        g.wrist = h.wrist;
                        g.end = target->position;
                        g.length =
                            std::max(length(target->position - h.wrist) + config_.slack, holdLength(i));
                        h.owned = true;
                        h.triggerReleased = h.reeling = h.yankUsed = false;
                        h.pullDistance = h.pullTime = h.obstructed = 0;
                        events_.push_back({GrabEventKind::Grab, i, target->id, .55f, {}});
                    }
                }
                if (g.phase != GrabPhase::None) {
                    if (hand.trigger < .35f)
                        h.triggerReleased = true;
                    h.reeling = g.phase == GrabPhase::Tethered && h.triggerReleased && hand.trigger > .65f;
                    // The zip gesture, aimed at a target instead of an anchor:
                    // fast and deliberate within a short window, measured from
                    // head-relative samples so travel and room-scale steps
                    // never count as a pull.
                    if (g.phase == GrabPhase::Tethered && h.sample && !h.yankUsed) {
                        const Vec3 delta =
                            Quat::yaw(in.trackingYaw).rotate(hand.gripRelativeToHead - h.previous);
                        const float pull = -dot(delta, normalized(g.end - h.wrist));
                        if (pull > 0) {
                            h.pullDistance += pull;
                            h.pullTime += seconds;
                            if (h.pullTime > config_.yankWindow) {
                                h.pullDistance = pull;
                                h.pullTime = seconds;
                            }
                            const float speed = pull / seconds;
                            if (h.pullDistance >= config_.yankDistance && speed >= config_.yankSpeed) {
                                const auto target = targets.find(g.target);
                                const float mass = target ? std::max(target->mass, 1.f) : config_.liftMass;
                                const float strength = std::clamp(config_.liftMass / mass, .3f, 1.f);
                                h.flight = std::clamp(speed * config_.yankMultiplier, config_.minYankFlight,
                                                      config_.maxYankFlight) *
                                           strength;
                                h.closest = std::numeric_limits<float>::infinity();
                                h.stalled = 0;
                                h.yankUsed = true;
                                g.phase = GrabPhase::Yanked;
                                events_.push_back({GrabEventKind::Yank, i, g.target, 1, {}});
                            }
                        } else {
                            h.pullDistance = h.pullTime = 0;
                        }
                    }
                }
                h.previous = hand.gripRelativeToHead;
                h.sample = true;
            }
        }
    }
    return forSwing(swing);
}
Input WebGrab::forSwing(const Input& input) const {
    Input swing = input;
    for (int i = 0; i < 2; ++i)
        if (hands_[i].owned || grabs_[i].phase != GrabPhase::None)
            swing.hands[i].grip = swing.hands[i].trigger = 0;
    return swing;
}
void WebGrab::step(float dt, const WorldQueries& world, const TargetQueries& targets,
                   std::vector<TargetCommand>& out) {
    if (!std::isfinite(dt) || dt <= 0 || dt > .05f)
        return;
    const auto first = out.size();
    for (const auto& thrown : pending_)
        out.push_back(thrown);
    pending_.clear();
    const Vec3 gravity{0, -config_.gravity, 0};
    for (int i = 0; i < 2; ++i) {
        auto& g = grabs_[i];
        auto& h = hands_[i];
        if (g.phase == GrabPhase::None)
            continue;
        const auto target = targets.find(g.target);
        if (!target || !finite(target->position) || !finite(target->velocity)) {
            end(i, GrabEventKind::Lost);
            continue;
        }
        auto* existing = commandFor(out, first, g.target);
        if (existing && existing->thrown)
            continue; // the other hand threw it in this step
        const Vec3 start = existing ? existing->velocity : target->velocity;
        // Hand samples arrive slower than physics steps. A held target follows
        // the latest one; carrying the hand on at its measured velocity
        // overshot wherever the hand stopped.
        const Vec3 wrist = h.wrist;
        const float lever = holdLength(i);
        Vec3 hold = wrist + h.forward * lever;
        Vec3 holdVelocity = h.wristVelocity + h.forwardRate * lever;
        // Both hands bringing in or holding one target: it goes between them.
        const auto& og = grabs_[1 - i];
        const auto& oh = hands_[1 - i];
        if (g.phase != GrabPhase::Tethered && og.target == g.target &&
            (og.phase == GrabPhase::Yanked || og.phase == GrabPhase::Held)) {
            const float otherLever = holdLength(1 - i);
            hold = (hold + oh.wrist + oh.forward * otherLever) * .5f;
            holdVelocity = (holdVelocity + oh.wristVelocity + oh.forwardRate * otherLever) * .5f;
        }
        const Vec3 p = target->position;
        g.wrist = wrist;
        g.end = p;
        g.taut = false;
        const Vec3 toTarget = p - wrist;
        const float distance = length(toTarget);
        if (distance > g.length + config_.breakStretch) {
            end(i, GrabEventKind::Lost);
            continue;
        }
        // A wall that stays between hand and target cuts the web. Other
        // targets in the way (a crowd, a stack of crates) do not.
        if (g.phase != GrabPhase::Held && distance > g.radius + .6f) {
            const auto hit = world.raycast(wrist, toTarget / distance, distance - g.radius - .3f);
            const auto own = hit ? targets.owner(hit->surface) : std::nullopt;
            const bool wall = hit && hit->fixed && !(own && *own == g.target);
            h.obstructed = wall ? h.obstructed + dt : 0;
            if (h.obstructed > 0 && h.obstructed >= config_.obstructionTime) {
                end(i, GrabEventKind::Lost);
                continue;
            }
        } else {
            h.obstructed = 0;
        }
        const float mass = std::max(target->mass, 1.f);
        const float strength = std::clamp(config_.liftMass / mass, .15f, 1.f);
        const float pickup = config_.webAcceleration * strength * dt;
        Vec3 v = start;
        bool acts = false;
        switch (g.phase) {
        case GrabPhase::Tethered: {
            if (h.reeling)
                g.length = std::max(lever, g.length - config_.reelSpeed * dt);
            v += gravity * dt;
            // A tension-only web from the moving hand: where this step would
            // leave the target beyond the web's length, take that stretch out.
            const Vec3 ahead = p + v * dt - (wrist + h.wristVelocity * dt);
            const float reach = length(ahead);
            if (reach > g.length && reach > 1e-4f) {
                const Vec3 n = ahead / reach;
                const float inward = (reach - g.length) / dt * config_.tetherStiffness;
                v -= n * std::min(inward, pickup);
                acts = g.taut = true;
            }
            if (h.reeling && g.length <= lever + .01f && distance <= lever + config_.catchDistance) {
                g.phase = GrabPhase::Held;
                events_.push_back({GrabEventKind::Catch, i, g.target, .4f, {}});
            }
            break;
        }
        case GrabPhase::Yanked: {
            // Straight to the hand, slowing in time to arrive with it.
            const Vec3 toHold = hold - p;
            const float left = length(toHold);
            if (left < h.closest - .25f) {
                h.closest = left;
                h.stalled = 0;
            } else {
                h.stalled += dt;
            }
            if (left <= config_.catchDistance + g.radius * .5f) {
                g.phase = GrabPhase::Held;
                g.length = lever;
                events_.push_back({GrabEventKind::Catch, i, g.target, .4f, {}});
            } else if (h.stalled > config_.yankTimeout) {
                // Snagged on something: keep it on the web where it is.
                g.phase = GrabPhase::Tethered;
                g.length = std::max(distance, lever);
            }
            const float speed = std::min(h.flight, std::sqrt(2 * config_.arrivalDeceleration * left));
            const Vec3 want = (left > 1e-4f ? toHold / left * speed : Vec3{}) + holdVelocity;
            v += limited(want - v, pickup);
            acts = g.taut = true;
            break;
        }
        case GrabPhase::Held: {
            // Moving with the hold point and closing the gap to it: first
            // order, so a hand that stops does not fling the target past it.
            // A critically damped spring carried a target 12-18 cm beyond a
            // hand that moved a metre in 0.2 s; this, 4-6 cm. Holding it up
            // cancels its weight.
            const Vec3 want = holdVelocity + (hold - p) * config_.holdResponse;
            v += limited(want - v, config_.maxHoldAcceleration * strength * dt);
            acts = g.taut = true;
            break;
        }
        case GrabPhase::None:
            break;
        }
        if (!acts)
            continue;
        if (existing)
            existing->velocity = v;
        else
            out.push_back({g.target, g.kind, v, false});
    }
}
} // namespace spidy
