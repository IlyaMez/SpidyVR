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
    const float positive[] = {c.radius,    c.maxRange,     c.minRope,    c.maxSpeed,      c.yankSpeed,
                              c.yankDistance, c.yankWindow, c.fixedStep,  c.wallClearance, c.wallSlope};
    for (float v : positive)
        if (!std::isfinite(v) || v <= 0)
            throw std::invalid_argument("Invalid swing configuration");
    const float nonnegative[] = {
        c.gravity,          c.reelSpeed,       c.airAcceleration, c.groundAcceleration, c.zipMultiplier,
        c.maxZipImpulse,    c.jumpSpeed,       c.pointLaunchWindow, c.zipLandingWindow, c.obstructionTime,
        c.anchorClearance,  c.wallStep,        c.wallCarry,       c.wallWalkSpeed,   c.wallAcceleration,   c.wallBrake,
        c.wallJumpOut,      c.wallJumpUp,      c.wallJumpPause,   c.wallGrace,          c.crestSeconds,
        c.crestPush,        c.crestHop};
    for (float v : nonnegative)
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid swing configuration");
    if (c.minRope > c.maxRange || c.fixedStep > .02f)
        throw std::invalid_argument("Invalid rope range or fixed timestep");
    if (!std::isfinite(c.wallReach) || c.wallReach < c.wallClearance || c.wallReach > 10 || c.wallSlope >= 1)
        throw std::invalid_argument("Invalid wall reach or slope");
    if (c.wallStep > 1)
        throw std::invalid_argument("Invalid wall step");
    if (!std::isfinite(c.mountReach) || c.mountReach < 0 || c.mountReach > 10 || !(c.mountSeconds >= 0) ||
        !(c.mountHold >= 0) || !std::isfinite(c.mountHeight))
        throw std::invalid_argument("Invalid wall mount");
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
void Swing::allowWalls(bool allowed) {
    config_.walls = allowed;
    if (!allowed)
        leaveWall();
}
void Swing::leaveWall() {
    if (wall_.on)
        events_.push_back({EventKind::WallOff, -1, .2f});
    wall_ = {};
    wallLost_ = 0;
    wrapWay_ = {};
}
void Swing::joinWall(const RayHit& hit) {
    const Vec3 n = hit.normal;
    const float into = -dot(body_.velocity, n);
    Vec3 along = body_.velocity + n * into;
    // Part of the speed into the wall goes on along it, the more the body
    // already went that way: grazing the wall it keeps its speed, head on
    // it stops.
    if (const float speed = length(along), total = length(body_.velocity); into > 0 && speed > 1e-3f)
        along = along * std::min(total / speed, 1 + config_.wallCarry * into / total);
    // What still closes on the wall ends at its clearance (holdWall).
    body_.velocity = along - n * into;
    if (!wall_.on) {
        wall_.seconds = 0;
        events_.push_back({EventKind::WallOn, -1, std::clamp(into / 20, .2f, 1.f)});
    }
    wall_.on = true;
    wall_.normal = n;
    wall_.point = hit.point;
    wall_.distance = dot(body_.position - hit.point, n);
    wallLost_ = crest_ = 0;
    wrapWay_ = {};
}
void Swing::offerWall(const RayHit& wall) {
    const Vec3 n = normalized(wall.normal);
    if (!config_.walls || !wall.fixed || !finite(wall.point) || length(n) < .5f ||
        std::abs(n.y) > config_.wallSlope)
        return;
    mountWall_ = {wall.point, n, wall.surface, true};
    offered_ = true;
}
void Swing::senseWall(float dt, const Input& in, const WorldQueries& world) {
    sinceWallJump_ += dt;
    const bool offered = offered_;
    offered_ = mounting_ = false;
    if (!config_.walls) {
        leaveWall();
        crest_ = mount_ = mountLeft_ = 0;
        return;
    }
    const Vec3 at = body_.position;
    // The wall a ray meets from outside: fixed, steep, facing the ray. The
    // game's world takes unit directions only.
    const auto wallAlong = [&](Vec3 from, Vec3 direction, float reach) -> std::optional<RayHit> {
        direction = normalized(direction);
        if (!finite(from) || length(direction) < .9995f || !(reach >= .02f))
            return {};
        const auto hit = world.raycast(from, direction, std::min(reach, 100.f));
        if (!hit || !hit->fixed || !finite(hit->point) || !finite(hit->normal))
            return {};
        const Vec3 n = normalized(hit->normal);
        if (length(n) < .5f || std::abs(n.y) > config_.wallSlope || dot(n, direction) > -.2f)
            return {};
        return RayHit{hit->point, n, hit->surface, true};
    };
    // The wall the stick walks the body at, level: within the mount's reach,
    // and faced within `facing` (a cosine) of head on.
    const auto walkedAt = [&](float facing) -> std::optional<RayHit> {
        const Vec3 stick = finite(in.move) ? Vec3{in.move.x, 0, in.move.z} : Vec3{};
        // From the tilt at which the game walks the player.
        if (length(stick) < .35f)
            return {};
        const Vec3 way = normalized(stick);
        const auto hit = wallAlong(at, way, config_.mountReach);
        if (!hit || dot(hit->normal, way) > -facing)
            return {};
        return hit;
    };
    if (offered) {
        mountLeft_ = config_.mountHold;
        mountOffered_ = true;
    }
    if (body_.grounded) {
        leaveWall();
        crest_ = 0;
        if (offered) {
            mount_ = 0;
            mounting_ = true;
            return;
        }
        auto wall = walkedAt(.7f);
        if (wall) {
            const auto above =
                wallAlong(at + Vec3{0, config_.mountHeight, 0}, -wall->normal, config_.mountReach + .5f);
            if (!above || dot(above->normal, wall->normal) < .9f)
                wall.reset();
        }
        mount_ = wall ? mount_ + dt : 0;
        if (wall && mount_ >= config_.mountSeconds) {
            mountWall_ = *wall;
            mountLeft_ = config_.mountHold;
            mountOffered_ = false;
            mounting_ = true;
        } else {
            // A jump already asked for still ends on its wall.
            mountLeft_ = std::max(0.f, mountLeft_ - dt);
        }
        return;
    }
    mount_ = 0;
    if (mountLeft_ > 0) {
        // Off the ground by the mount's jump: onto its wall, up it at a walk
        // at most. Once more if the game stood the body again meanwhile.
        mountLeft_ -= dt;
        const Vec3 n = mountWall_.normal;
        if (const float distance = dot(at - mountWall_.point, n);
            !wall_.on && distance > -.2f && distance <= config_.mountReach + .5f) {
            body_.velocity = mountOffered_ ? Vec3{}
                                           : limited(body_.velocity - n * dot(body_.velocity, n),
                                                     config_.wallWalkSpeed);
            joinWall(mountWall_);
            return;
        }
    }
    if (wall_.on) {
        wall_.seconds += dt;
        const Vec3 n = wall_.normal;
        const Vec3 along = body_.velocity - n * dot(body_.velocity, n);
        const float speed = length(along);
        const bool rounding = length(wrapWay_) > .5f;
        // A wall ahead along this one (an inside corner): onto it.
        if (speed > 1 && !rounding)
            if (const auto next = wallAlong(at, along, speed * dt + config_.wallClearance + .2f);
                next && dot(next->normal, n) < .9f) {
                joinWall(*next);
                return;
            }
        const float closing = std::max(0.f, -dot(body_.velocity, n));
        // Start outside a small projecting sill, so the ray still finds its
        // front when the body's centre has already passed its edge. Look
        // ahead before the capsule hits it and below the harness until the
        // feet have cleared it. All samples must stay on this facade.
        const float step = config_.wallStep;
        const auto facade = [&](Vec3 from) -> std::optional<RayHit> {
            auto hit = wallAlong(from + n * step, -n,
                                 config_.wallReach + .3f + 2 * step + closing * dt);
            if (!hit)
                return {};
            const float distance = dot(at - hit->point, n);
            if (distance < -step || distance > config_.wallReach + step)
                return {};
            return hit;
        };
        auto hit = facade(at);
        if (!rounding && step > 0) {
            const Vec3 ahead = speed > .3f ? along / speed : Vec3{};
            const float look = config_.wallClearance + std::min(speed * .2f, 2.f);
            for (const Vec3 offset : {ahead * look, Vec3{0, -1.1f, 0}}) {
                const float span = length(offset);
                if (span < .01f)
                    continue;
                // An endpoint alone skips a thin sill. Trace along the
                // facade as well, then sample just past its lip to find its
                // outward face (the lip's own normal points up or down).
                const float depth = std::max(dot(at - wall_.point, n),
                                             hit ? dot(at - hit->point, n) : 0.f);
                const Vec3 start = at - n * (depth - .05f), way = offset / span;
                const auto edge = world.raycast(start, way, span);
                std::optional<RayHit> lip;
                if (edge && edge->fixed && finite(edge->point)) {
                    const float travel = dot(edge->point - start, way);
                    if (travel >= 0 && travel <= span)
                        lip = facade(at + way * (travel + .02f));
                }
                for (const auto& next : {facade(at + offset), lip}) {
                    if (!next || dot(next->normal, n) < .9f)
                        continue;
                    // A nearby parallel face may extend our contact footprint;
                    // a new building or a deep overhang must not pull us onto it.
                    const float rise = dot(next->point - wall_.point, n);
                    if (std::abs(rise) > step || (hit && dot(next->point - hit->point, n) <= 0))
                        continue;
                    hit = next;
                }
            }
        }
        if (hit) {
            // A facade's sills and pilasters turn the normal for a moment.
            wall_.normal = normalized(n + (hit->normal - n) * (1 - std::exp(-dt / .08f)));
            wall_.point = hit->point;
            wall_.distance = dot(at - hit->point, wall_.normal);
            wallLost_ = 0;
            wrapWay_ = {};
            // Carried off it (a web's pull): the wall lets go.
            const float reach = dot(body_.velocity, n) < .2f
                                    ? std::max(config_.wallReach, config_.wallClearance + step)
                                    : config_.wallReach;
            if (wall_.distance > reach && closing <= 0)
                leaveWall();
            return;
        }
        // Rounding a corner, the body is not in front of the next face yet.
        if (rounding && (wrapLeft_ -= dt) > 0)
            return;
        // At a walk, round the wall's end onto its next face. The body is
        // beside that face's edge, a clearance out along the old wall: it
        // goes on round the corner by itself (wallMotion) until the next
        // face is beside it.
        if (!rounding && speed > .3f && speed <= config_.wallWalkSpeed + 1) {
            const Vec3 ahead = along / speed;
            const Vec3 past = at + ahead * (speed * dt + .3f) - n * (wall_.distance + .4f);
            if (const auto next = wallAlong(past, -ahead, speed * dt + 1.5f)) {
                wall_.normal = next->normal;
                wall_.point = next->point;
                wall_.distance = dot(at - next->point, next->normal);
                wallLost_ = 0;
                wrapWay_ = normalized(-n - next->normal * dot(-n, next->normal));
                wrapLeft_ = .8f;
                body_.velocity = {};
                return;
            }
        }
        wrapWay_ = {};
        wallLost_ += dt;
        if (wallLost_ > config_.wallGrace) {
            // Over the wall's top edge: on over it, onto the roof.
            if (along.y > 1) {
                crest_ = config_.crestSeconds;
                crestNormal_ = n;
                body_.velocity.y = std::max(body_.velocity.y, config_.crestHop);
            }
            leaveWall();
        }
        return;
    }
    if (sinceWallJump_ < config_.wallJumpPause)
        return;
    // Ahead of the body and to both its sides: a wall it meets head on, and
    // one it grazes.
    const Vec3 level{body_.velocity.x, 0, body_.velocity.z};
    std::optional<RayHit> wall;
    if (length(level) >= .5f) {
        const Vec3 heading = normalized(level), side{-heading.z, 0, heading.x};
        const float reach = 1.5f * config_.wallClearance + length(body_.velocity) * dt + .2f;
        float nearest{};
        for (const Vec3 direction : {heading, side, -side}) {
            const auto hit = wallAlong(at, direction, reach);
            if (!hit)
                continue;
            // Coming into it, and within its clearance by the end of this step.
            const float distance = dot(at - hit->point, hit->normal),
                        closing = -dot(body_.velocity, hit->normal);
            if (closing > .5f && distance - closing * dt <= config_.wallClearance + .05f &&
                (!wall || distance < nearest)) {
                wall = hit;
                nearest = distance;
            }
        }
    }
    // Pushed at a wall beside the body, the stick takes it with no speed
    // into it, and none off it: a fall down a facade, a jump beside one.
    if (!wall && (wall = walkedAt(.5f)))
        body_.velocity -= wall->normal * std::max(0.f, dot(body_.velocity, wall->normal));
    if (wall)
        joinWall(*wall);
}
void Swing::wallMotion(float dt, const Input& in) {
    const Vec3 n = wall_.normal;
    const float out = dot(body_.velocity, n);
    Vec3 along = body_.velocity - n * out;
    if (length(wrapWay_) > .5f) {
        // Round a corner, slowly enough to clear its edge while the next
        // face pushes the body out to its clearance.
        body_.velocity = wrapWay_ * 3 + n * out;
        return;
    }
    // What of the stick points into the wall goes up it: pushed at a wall,
    // the body climbs.
    const Vec3 up = normalized(Vec3{0, 1, 0} - n * n.y);
    Vec3 move = finite(in.move) ? limited(in.move, 1) : Vec3{};
    const float into = -dot(move, n);
    move = limited(move + n * into + up * into, 1);
    if (const float tilt = length(move); tilt > .05f) {
        // The stick walks the wall; a faster run keeps what it has that way.
        const Vec3 way = move / tilt;
        const Vec3 wanted = way * std::max(dot(along, way), config_.wallWalkSpeed * tilt);
        along += limited(wanted - along, config_.wallAcceleration * dt);
    } else if (!webs_[0].attached && !webs_[1].attached) {
        if (const float speed = length(along); speed > 1e-4f) {
            const float slower = speed > config_.wallWalkSpeed ? speed - config_.wallBrake * dt
                                                               : speed * std::exp(-6 * dt) - dt;
            along = along * (std::max(0.f, slower) / speed);
        }
    }
    body_.velocity = limited(along + n * out, config_.maxSpeed);
}
void Swing::holdWall(float dt, float before, const World* collision) {
    const Vec3 n = wall_.normal;
    const float distance = dot(body_.position - wall_.point, n), out = dot(body_.velocity, n);
    // No nearer than the clearance; a body already inside it (the facade
    // stepped out, a corner) comes out at a walk. One the wall lets drift
    // off comes back slowly, unless something carries it away.
    const float nearest = std::min(config_.wallClearance, before + 4 * dt);
    float shift{};
    if (distance < nearest)
        shift = nearest - distance;
    else if (distance > config_.wallClearance + .1f && out < .2f)
        shift = -std::min(distance - config_.wallClearance, 2 * dt);
    if (shift != 0)
        move(body_.position + n * shift, collision);
    if (out < 0 && distance + shift <= config_.wallClearance + 1e-3f)
        body_.velocity -= n * out;
    wall_.distance = distance + shift;
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
    wall_ = {};
    wallLost_ = crest_ = 0;
    wrapWay_ = {};
    sinceWallJump_ = 100;
    mount_ = mountLeft_ = 0;
    mounting_ = offered_ = mountOffered_ = false;
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
                const Vec3 delta = trackingTurn(in).rotate(h.gripRelativeToHead - s.previous);
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
        if (wall_.on) {
            // Off the wall: out from it and up, with what the body had along it.
            const Vec3 n = wall_.normal;
            body_.velocity = limited(body_.velocity - n * dot(body_.velocity, n) + n * config_.wallJumpOut +
                                         Vec3{0, config_.wallJumpUp, 0},
                                     config_.maxSpeed);
            leaveWall();
            sinceWallJump_ = 0;
            mountLeft_ = 0;
            events_.push_back({EventKind::WallJump, -1, 1});
        } else if (body_.grounded) {
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
    const float wallBefore = wall_.on ? dot(body_.position - wall_.point, wall_.normal) : 0.f;
    if (wall_.on) {
        // On a wall the stick walks it, and gravity does not pull along it.
        wallMotion(dt, in);
        body_.grounded = false;
    } else {
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
        if (crest_ > 0) {
            // Over a wall's top edge: on over the roof behind it.
            crest_ -= dt;
            body_.velocity -= crestNormal_ * (config_.crestPush * dt);
        }
        body_.velocity = limited(body_.velocity, config_.maxSpeed);
        body_.grounded = supported;
    }
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
    // A web may pull along a wall or off it, never through it.
    if (wall_.on)
        holdWall(dt, wallBefore, collision);
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
    senseWall(seconds, input, world);
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
    senseWall(seconds, input, world);
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
