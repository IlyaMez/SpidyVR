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
// The step's command for this target so far, from the other hand's web.
TargetCommand* commandFor(std::vector<TargetCommand>& out, std::size_t first, std::uint64_t id) {
    for (auto i = first; i < out.size(); ++i)
        if (out[i].id == id)
            return &out[i];
    return nullptr;
}
// Which of two webs on one target decides its step: a yank's jerk or a
// catch over a web that only pulls. Two pulling webs both pull.
int rank(const TargetCommand& c) {
    return c.mode == TargetCommand::Mode::Rope ? 1 : 2;
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
Vec3 arcVelocity(Vec3 from, Vec3 to, float time, Vec3 gravity) {
    return (to - from) / time - gravity * (time * .5f);
}
Vec3 advance(const TargetCommand& c, Vec3 p, Vec3 v, float dt, Vec3 gravity) {
    switch (c.mode) {
    case TargetCommand::Mode::Launch:
        return c.velocity;
    case TargetCommand::Mode::Rope: {
        v += gravity * dt;
        // Each web from its moving hand: where this step would leave the
        // target beyond the web's length, it takes up that stretch at its
        // response, no harder than its strength allows. The share taken in
        // one step follows from the rate, so the web is as stiff at 30
        // physics steps a second as at 360. A web only pulls.
        const float take = 1 - std::exp(-std::max(c.response, 0.f) * dt);
        const float settle = 1 - std::exp(-std::max(c.damping, 0.f) * dt);
        for (unsigned i = 0; i < std::min<unsigned>(c.ropeCount, 2); ++i) {
            const auto& rope = c.ropes[i];
            const Vec3 ahead = p + v * dt - (rope.anchor + rope.anchorVelocity * dt);
            const float reach = length(ahead);
            if (reach > rope.length && reach > 1e-4f) {
                const Vec3 along = ahead / reach;
                const float inward = (reach - rope.length) / dt * take;
                v -= along * std::min(inward, c.maxAcceleration * dt);
                const Vec3 relative = v - rope.anchorVelocity;
                v -= limited((relative - along * dot(relative, along)) * settle, c.maxAcceleration * dt);
            }
        }
        return v;
    }
    case TargetCommand::Mode::Follow:
        return v + limited(c.velocity - v, c.maxAcceleration * dt) + gravity * dt;
    }
    return v;
}
Vec3 advanceSpin(const TargetCommand& c, Vec3 spin, float dt) {
    if (!c.spins)
        return spin;
    if (c.mode == TargetCommand::Mode::Launch)
        return c.spin;
    return spin + limited(c.spin - spin, c.spinAcceleration * dt);
}

WebGrab::WebGrab(GrabConfig c) : config_(c) {
    const float positive[] = {c.maxRange,        c.maxMass,       c.yankSpeed,    c.yankDistance,
                              c.yankWindow,      c.webForce,      c.liftMass,     c.arrivalDeceleration,
                              c.yankTimeout,     c.maxThrowSpeed, c.fixedStep,    c.maxYankFlight,
                              c.throwMultiplier, c.maxYankTime,   c.yankLaunch,   c.tetherResponse,
                              c.catchSpeed};
    for (float v : positive)
        if (!std::isfinite(v) || v <= 0)
            throw std::invalid_argument("Invalid grab configuration");
    const float nonnegative[] = {c.aimCone,       c.holdDistance,   c.reelSpeed,       c.yankMultiplier,
                                 c.minYankFlight, c.catchDistance,  c.aimAssistCone,   c.aimAssistRange,
                                 c.slack,         c.minAssistSpeed, c.obstructionTime, c.breakStretch,
                                 c.gravity,       c.yankTumble,     c.yankReel,        c.reelLead,
                                 c.webSpin,       c.reelSteer,      c.swingDamping};
    for (float v : nonnegative)
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid grab configuration");
    if (c.minYankFlight > c.maxYankFlight || c.aimCone > 1 || c.fixedStep > .02f || c.maxYankTime > 5)
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
    h.reeling = h.yankUsed = h.launched = false;
    h.pullDistance = h.pullTime = h.obstructed = h.flight = h.stalled = 0;
    h.closest = std::numeric_limits<float>::infinity();
}
void WebGrab::letGo(int i, const TargetQueries& targets, const Body& player, bool otherReleasing) {
    const auto& g = grabs_[i];
    const auto target = targets.find(g.target);
    if (!target || !finite(target->velocity)) {
        end(i, GrabEventKind::Lost);
        return;
    }
    const auto& other = grabs_[1 - i];
    const bool stillHeld = other.phase == GrabPhase::Held && other.target == g.target && !otherReleasing;
    if (g.phase != GrabPhase::Held || stillHeld) {
        // Only the web goes. A towed target keeps what the web gave it; a
        // yanked one flies on where it was going, as anything let go of
        // does: the arm's throw belongs to what the hand holds. Nor does
        // one hand letting go throw what the other still holds.
        end(i, GrabEventKind::Release);
        return;
    }
    // The arm's work is the target's motion relative to the player, as the
    // arm swung it on its web: a flick of the wrist does not swing what hangs
    // from it. Carried along at swing speed, that alone must not be
    // multiplied. A heavy target gets less of the multiplier.
    const Vec3 base = finite(player.velocity) ? player.velocity : Vec3{};
    const float mass = std::max(target->mass, 1.f);
    const float boost = 1 + (config_.throwMultiplier - 1) * std::clamp(config_.liftMass / mass, 0.f, 1.f);
    const Vec3 relative = limited((target->velocity - base) * boost, config_.maxThrowSpeed);
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
    // Both hands letting go together throw it once. It keeps the spin it has.
    if (std::none_of(pending_.begin(), pending_.end(),
                     [&](const TargetCommand& c) { return c.id == g.target; })) {
        TargetCommand thrown{g.target, g.kind, TargetCommand::Mode::Launch, true};
        thrown.velocity = launch;
        pending_.push_back(thrown);
    }
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
    if (seconds > 0)
        playerVelocity_ = finite(player.velocity) ? player.velocity : Vec3{};
    // Whether each hand lets its grab go in this sample: two hands letting
    // go of one target together throw it.
    std::array<bool, 2> releasing{};
    for (int i = 0; i < 2; ++i)
        releasing[i] = seconds > 0 && grabs_[i].phase != GrabPhase::None && in.hands[i].tracked &&
                       !(in.hands[i].grip > .35f);
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
                if (h.sample)
                    h.wristVelocity =
                        smooth(h.wristVelocity, (hand.aim.position - h.wrist) / seconds, seconds);
                else
                    h.wristVelocity = {};
                h.wrist = hand.aim.position;
                h.forward = normalized(forward);
                const bool held = h.held ? hand.grip > .35f : hand.grip > .65f;
                const bool shoot = held && !h.held;
                h.held = held;
                if (!held)
                    h.owned = false;
                if (g.phase != GrabPhase::None && !held)
                    letGo(i, targets, player, releasing[1 - i]);
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
                                // A heavier target flies slower for the same pull.
                                const auto target = targets.find(g.target);
                                const float mass = target ? std::max(target->mass, 1.f) : config_.liftMass;
                                const float strength = std::clamp(config_.liftMass / mass, .3f, 1.f);
                                h.flight = std::clamp(speed * config_.yankMultiplier, config_.minYankFlight,
                                                      config_.maxYankFlight) *
                                           strength;
                                h.closest = std::numeric_limits<float>::infinity();
                                h.stalled = 0;
                                h.launched = false;
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
float WebGrab::planYank(Vec3 from, Vec3 hold, Vec3 holdVelocity, float speed, float radius,
                        std::uint64_t target, const WorldQueries& world, const TargetQueries& targets) const {
    const Vec3 gravity{0, -config_.gravity, 0};
    const float direct =
        std::clamp(length(hold - from) / std::max(speed, 1.f), .2f, config_.maxYankTime);
    // What a flying target strikes is mostly below it (a railing, a car, a
    // parapet): the arc is traced along its underside, from its centre.
    const Vec3 under{0, -.9f * std::max(radius, 0.f), 0};
    for (const float stretch : {1.f, 1.35f, 1.8f, 2.4f}) {
        const float time = std::min(direct * stretch, config_.maxYankTime);
        const Vec3 launch = arcVelocity(from, hold + holdVelocity * time, time, gravity);
        // The arc in six chords: only the fixed world blocks it, never the
        // target's own bodies; other props it would knock aside.
        bool clear = true;
        Vec3 a = from;
        constexpr int chords = 6;
        for (int s = 1; s <= chords && clear; ++s) {
            const float t = time * static_cast<float>(s) / chords;
            const Vec3 b = from + launch * t + gravity * (.5f * t * t) + under;
            const float span = length(b - a);
            if (span > .05f && span < 140) {
                const auto hit = world.raycast(a, (b - a) / span, span);
                const auto own = hit ? targets.owner(hit->surface) : std::nullopt;
                clear = !(hit && hit->fixed && !(own && *own == target));
            }
            a = b;
        }
        if (clear)
            return time;
        if (time >= config_.maxYankTime)
            break;
    }
    return direct;
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
        g.tension = 0;
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
        // Hand samples arrive slower than physics steps: the web leaves the
        // latest one, carried on at the hand's smoothed velocity through the
        // step.
        const Vec3 wrist = h.wrist;
        const float lever = holdLength(i);
        const Vec3 p = target->position, v = target->velocity;
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
        // The most this web changes the target's velocity, per second.
        const float pull = config_.webForce / mass;
        const float brakeLimit = std::min(config_.arrivalDeceleration, pull);
        // How fast the target moves relative to the hand.
        const Vec3 relative = v - h.wristVelocity;
        const float speed = length(relative);
        TargetCommand c{g.target, g.kind};
        // The web as it is: a rope from the moving wrist that only pulls, no
        // harder than its strength over the target's mass, against gravity.
        // A held target hangs from it; a hand that moves swings it. Taut, it
        // steadies the target's spin and slows its swing across the web at
        // `damping`. Until October 6 a held target followed a point ahead of
        // the hand, held up, like on a stick.
        const auto rope = [&](float damping) {
            c.mode = TargetCommand::Mode::Rope;
            c.ropes[0] = {wrist, h.wristVelocity, g.length};
            c.ropeCount = 1;
            c.response = config_.tetherResponse;
            c.damping = damping;
            c.maxAcceleration = pull;
            const Vec3 ahead = p + (v + gravity * dt) * dt - (wrist + h.wristVelocity * dt);
            g.taut = length(ahead) > g.length;
            c.spins = g.taut;
            c.spin = {};
            c.spinAcceleration = config_.webSpin;
            return g.taut;
        };
        // Bringing the target in (a reel, a yank's flight): the web winds in
        // from `from` at `rate`, slower near the hand so the target can stop
        // there, and no more than reelLead ahead of a target too heavy to
        // follow (it slips rather than snapping the web). A target moving
        // faster than it can stop at the edge of reach (catchDistance beyond
        // its hold length) is braked, as a hand catches it, with the web paid
        // out so it never pulls against the catch: only a hand can push. The
        // brake looks a physics step ahead (20-35 ms in VR). Wound on to the
        // end, a reeled can slid under the hand at 12 m/s and swung up behind
        // it; braked toward the hold length itself, which a can on the ground
        // cannot come as close to as a hand above it, it slid under at 5 m/s.
        const float zone = lever + config_.catchDistance;
        const float closing = distance > 1e-4f ? -dot(relative, toTarget) / distance : 0.f;
        const float room = std::max(distance - std::max(closing, 0.f) * dt - zone, 0.f);
        const float brake = std::sqrt(config_.catchSpeed * config_.catchSpeed * .25f + 2 * brakeLimit * room);
        const auto bringIn = [&](float from, float rate, float damping) {
            if (speed > brake) {
                g.length = std::max(g.length, distance);
                c.mode = TargetCommand::Mode::Follow;
                c.velocity = h.wristVelocity + relative * (brake / speed);
                c.maxAcceleration = brakeLimit;
                g.taut = true;
                return true;
            }
            g.length = std::max(lever, std::max(from - std::min(rate, brake) * dt, distance - config_.reelLead));
            return rope(damping);
        };
        // Brought in: within reach of its hold length and slow, it hangs there.
        const auto caught = [&] {
            if (distance > zone || speed > config_.catchSpeed)
                return false;
            g.phase = GrabPhase::Held;
            g.length = std::max(distance, lever);
            events_.push_back({GrabEventKind::Catch, i, g.target, .4f, {}});
            rope(config_.swingDamping);
            return true;
        };
        bool acts = false;
        switch (g.phase) {
        case GrabPhase::Tethered:
            if (h.reeling && caught()) {
                acts = true;
                break;
            }
            // Slack, the web does nothing.
            acts = h.reeling ? bringIn(g.length, config_.reelSpeed, config_.reelSteer) : rope(0);
            break;
        case GrabPhase::Yanked: {
            const float reach = distance - lever;
            if (reach < h.closest - .25f) {
                h.closest = reach;
                h.stalled = 0;
            } else {
                h.stalled += dt;
            }
            if (caught()) {
                acts = true;
                break;
            }
            if (h.stalled > config_.yankTimeout) {
                // It struck something and stopped short: it stays on a web
                // where it lies, and physics has it.
                g.phase = GrabPhase::Tethered;
                g.length = std::max(distance, lever);
                break;
            }
            if (!h.launched) {
                // The jerk: onto the arc to the hand at no more than the
                // yank's speed allows, tumbling gently as the web tips its
                // near side over. Spin it had from being freed (30 rad/s
                // measured, physics running 8 times real time) does not
                // survive the pull. The arc aims where the hand is, carried
                // with the player: the pulling gesture's own speed, carried
                // through the flight, put the hold point metres behind and
                // above the player and launched props at 26-45 m/s.
                Vec3 hold = wrist + h.forward * lever;
                const auto& og = grabs_[1 - i];
                const auto& oh = hands_[1 - i];
                if (og.target == g.target && (og.phase == GrabPhase::Yanked || og.phase == GrabPhase::Held))
                    hold = (hold + oh.wrist + oh.forward * holdLength(1 - i)) * .5f; // between both hands
                const float time =
                    planYank(p, hold, playerVelocity_, h.flight, g.radius, g.target, world, targets);
                h.launched = true;
                g.length = distance + config_.slack;
                c.mode = TargetCommand::Mode::Launch;
                c.velocity = limited(arcVelocity(p, hold + playerVelocity_ * time, time, gravity),
                                     h.flight * config_.yankLaunch);
                c.spins = true;
                c.spin = normalized(cross({0, 1, 0}, Vec3{c.velocity.x, 0, c.velocity.z})) * config_.yankTumble;
                acts = g.taut = true;
                break;
            }
            // In flight the web takes in what the flight gives it, and reels
            // on at yankReel times the yank's speed: it pulls only a target
            // that falls behind, as one whose arc falls short does, up to a
            // rooftop, and then along the web. Slack, it still owns the
            // flight, which falls as anything thrown does.
            bringIn(std::min(g.length, distance + config_.slack), h.flight * config_.yankReel, config_.reelSteer);
            acts = true;
            break;
        }
        case GrabPhase::Held:
            // It hangs on its web, slack or taut. Caught short of its hold
            // length, the web takes in what slack it gives and winds on
            // gently: wound at the catch speed, it flung the target past the
            // wrist.
            g.length = std::max(lever, std::min(g.length, distance + .05f) - config_.catchSpeed * .3f * dt);
            rope(config_.swingDamping);
            acts = true;
            break;
        case GrabPhase::None:
            break;
        }
        if (!acts)
            continue;
        // How hard this web pulls: what its law does beyond gravity, against
        // the web's strength.
        g.tension = std::clamp(length(advance(c, p, v, dt, gravity) - (v + gravity * dt)) / (dt * pull), 0.f, 1.f);
        if (!existing)
            out.push_back(c);
        else if (c.mode == TargetCommand::Mode::Rope && existing->mode == TargetCommand::Mode::Rope &&
                 existing->ropeCount == 1)
            existing->ropes[existing->ropeCount++] = c.ropes[0]; // two webs both pull
        else if (rank(c) > rank(*existing))
            *existing = c;
    }
}
} // namespace spidy
