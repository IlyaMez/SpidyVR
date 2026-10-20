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

// A web with its anchor at `hold`.
struct Rope {
    Vec3 anchor{};
    float length{};
};
Vec3 projectTwoWebs(Vec3 p, const Rope& a, const Rope& b) {
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
                              c.yankDistance, c.yankWindow, c.fixedStep,  c.wallClearance, c.wallSlope,
                              c.bendOut,   c.bendClearance, c.bendRope,  c.maxHandSpeed,  c.thinDepth};
    for (float v : positive)
        if (!std::isfinite(v) || v <= 0)
            throw std::invalid_argument("Invalid swing configuration");
    const float nonnegative[] = {
        c.gravity,          c.reelSpeed,       c.airAcceleration, c.groundAcceleration, c.zipMultiplier,
        c.maxZipImpulse,    c.jumpSpeed,       c.pointLaunchWindow, c.zipLandingWindow, c.obstructionTime,
        c.anchorClearance,  c.wallStep,        c.wallCarry,       c.wallWalkSpeed,   c.wallAcceleration,   c.wallBrake,
        c.wallJumpOut,      c.wallJumpUp,      c.wallJumpPause,   c.wallGrace,          c.crestSeconds,
        c.crestPush,        c.crestHop,        c.strandReach,     c.handSlack,          c.groundClearance,
        c.groundTurn,       c.arcadeClearance};
    for (float v : nonnegative)
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid swing configuration");
    if (c.minRope > c.maxRange || c.fixedStep > .02f)
        throw std::invalid_argument("Invalid rope range or fixed timestep");
    if (!std::isfinite(c.wallReach) || c.wallReach < c.wallClearance || c.wallReach > 10 || c.wallSlope >= 1)
        throw std::invalid_argument("Invalid wall reach or slope");
    if (!std::isfinite(c.hugClearance) || c.hugClearance < 0 || c.hugClearance > c.wallClearance)
        throw std::invalid_argument("Invalid hug clearance");
    if (c.wallStep > 1)
        throw std::invalid_argument("Invalid wall step");
    if (!std::isfinite(c.mountReach) || c.mountReach < 0 || c.mountReach > 10 || !(c.mountSeconds >= 0) ||
        !(c.mountHold >= 0) || !std::isfinite(c.mountHeight))
        throw std::invalid_argument("Invalid wall mount");
    if (!std::isfinite(c.handHeight) || c.bendRope > c.minRope || c.strandReach > 1)
        throw std::invalid_argument("Invalid physical web");
    if ((c.groundClearance > 0 || c.arcadeClearance > 0) && !(c.groundTurn > 0))
        throw std::invalid_argument("Invalid ground clearance");
}
void Swing::allowPhysical(bool allowed) {
    config_.physical = allowed;
}
void Swing::allowArcade(bool allowed) {
    config_.arcade = allowed;
}
bool Swing::thin(const RayHit& hit, const WorldQueries& world) const {
    const Vec3 face = finite(hit.normal) ? normalized(hit.normal) : Vec3{};
    if (length(face) < .5f || !finite(hit.point))
        return false;
    // From thinDepth behind the face, back out to it: past a thin thing the
    // ray starts in the open and meets its far face, turned away from this
    // one. Inside a building it meets this face from within (the game's rays
    // meet faces from either side), turned the same way, or nothing.
    const auto back = world.raycast(hit.point - face * config_.thinDepth, face, config_.thinDepth - .02f);
    return back && finite(back->normal) && dot(normalized(back->normal), face) < -.5f;
}
std::optional<RayHit> Swing::solid(Vec3 from, Vec3 way, float reach, const WorldQueries& world,
                                   int* passed) const {
    auto hit = world.raycast(from, way, reach);
    if (!config_.arcade)
        return hit;
    // A few thin things at most; past them, what comes next holds as anything does.
    Vec3 at = from;
    float left = reach;
    for (int through = 0; hit && finite(hit->point) && through < 6; ++through) {
        // Inside a thin thing the ray goes out through its far face, which
        // the game's rays meet too: turned along the ray.
        const bool leaving = through > 0 && finite(hit->normal) && dot(hit->normal, way) > 0;
        if (!leaving && hit->fixed && !thin(*hit, world))
            return hit;
        if (!leaving && passed)
            ++*passed;
        const float past = dot(hit->point - at, way) + .02f;
        at += way * past;
        left -= past;
        if (left < .02f)
            return {};
        hit = world.raycast(at, way, left);
    }
    return hit;
}
Vec3 Swing::hold(int i) const {
    return config_.physical ? webs_[i].anchor - hands_[i].offset : webs_[i].anchor;
}
bool Swing::onStrand(int i) const {
    if (i < 0 || i > 1 || !(config_.strandReach > 0))
        return false;
    const int o = 1 - i;
    const auto& held = webs_[o];
    if (webs_[i].attached || !held.attached || !hands_[i].sample || !hands_[o].sample)
        return false;
    // The web from the hand that holds it to what it hangs from, and this
    // hand from that one.
    const Vec3 line = held.anchor - body_.position - hands_[o].reach, to = hands_[i].reach - hands_[o].reach;
    const float span = dot(line, line);
    const float along = span > 1e-6f ? std::clamp(dot(to, line) / span, 0.f, 1.f) : 0.f;
    return length(to - line * along) <= config_.strandReach;
}
bool Swing::bears(int i) const {
    if (i < 0 || i > 1 || !webs_[i].attached)
        return false;
    return !shared() || webs_[i].length <= webs_[1 - i].length + 1e-4f;
}
void Swing::payOut(Vec3 position, float slack) {
    if (!config_.physical || !finite(position) || !(slack >= 0))
        return;
    for (int i = 0; i < 2; ++i)
        if (auto& w = webs_[i]; w.attached)
            w.length = std::max(w.length, length(hold(i) - position) + slack);
}
bool Swing::wrap(int i, Vec3 before, const RayHit& first, const WorldQueries& world) {
    auto& w = webs_[i];
    const Vec3 pivot = w.anchor, now = body_.position;
    if (w.bent >= maxBends || !finite(before) || length(now - before) < 1e-4f)
        return false;
    const bool bend = w.bent > 0;
    const auto blocker = [&](Vec3 from) -> std::optional<RayHit> {
        const Vec3 d = pivot - from;
        const float dist = length(d);
        if (dist < .2f)
            return {};
        const auto hit = solid(from, d / dist, std::max(0.f, dist - .1f), world);
        if (hit && finite(hit->point) && blocks(*hit, from, pivot, bend))
            return hit;
        return {};
    };
    // The line was clear a step ago: the edge it met lies in between. Halving
    // the step finds the line that just meets it, and where.
    if (blocker(before))
        return false;
    Vec3 clear = before;
    RayHit edge = first;
    float low = 0, high = 1;
    for (int n = 0; n < 5; ++n) {
        const float middle = (low + high) / 2;
        const Vec3 at = before + (now - before) * middle;
        if (const auto hit = blocker(at)) {
            high = middle;
            edge = *hit;
        } else {
            low = middle;
            clear = at;
        }
    }
    // Out of the edge: off the face the line met, and toward the line that
    // still passed it.
    const Vec3 face = finite(edge.normal) ? normalized(edge.normal) : Vec3{};
    const Vec3 line = clear - pivot;
    const float span2 = dot(line, line);
    const float along = span2 > 1e-6f ? std::clamp(dot(edge.point - pivot, line) / span2, 0.f, 1.f) : 0.f;
    Vec3 aside = pivot + line * along - edge.point;
    aside = length(aside) > 1e-4f ? normalized(aside) : Vec3{};
    Vec3 out = face + aside;
    if (length(out) < .1f)
        out = length(face) > .5f ? face : aside;
    if (length(out) < .1f)
        return false;
    out = normalized(out);
    const Vec3 point = edge.point + out * config_.bendOut;
    const float span = length(point - pivot);
    if (!finite(point) || span < config_.bendOut)
        return false;
    w.bends[w.bent++] = {pivot, out, span};
    w.anchor = point;
    // The rope beyond the bend: what is left of it, and no shorter than the
    // way to the hand is now (the bend lies a little off the straight line).
    w.length = std::max({config_.bendRope, w.length - span, length(hold(i) - now)});
    events_.push_back({EventKind::Wrap, i, .3f});
    return true;
}
void Swing::unwrap(int i, const WorldQueries& world) {
    auto& w = webs_[i];
    if (!w.bent)
        return;
    const Bend& last = w.bends[w.bent - 1];
    const Vec3 line = body_.position - last.from;
    const float span = length(line);
    if (span < .2f)
        return;
    // The straight rope from before the bend would pass the edge on its
    // outside: it lies free of it.
    const float along = std::clamp(dot(w.anchor - last.from, line) / (span * span), 0.f, 1.f);
    if (dot(last.from + line * along - w.anchor, last.out) < config_.bendOut * .5f)
        return;
    // And of everything else.
    const auto hit = solid(body_.position, line * (-1 / span), std::max(0.f, span - .1f), world);
    if (hit && blocks(*hit, body_.position, last.from, w.bent > 1))
        return;
    w.anchor = last.from;
    w.length += last.span;
    --w.bent;
    events_.push_back({EventKind::Unwrap, i, .2f});
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
    climbing_ = {};
    wallLost_ = 0;
    wrapWay_ = {};
}
void Swing::joinWall(const RayHit& hit) {
    const Vec3 n = hit.normal;
    // A wall the body is behind is the inside of one. Off the game's own
    // crawl the body is on the wall's surface, inside its collision, and a ray
    // from there meets a face turned back toward the body: taken, it held the
    // player half a metre "behind" his own wall and stood his view 1.7 m out
    // along that backward normal - through the wall, into the void (October
    // 14, the first mission's shaft: normal (+0.71, 0, +0.71) on a wall facing
    // (-0.71, 0, -0.71), distance -0.35 to -0.67, "got oob completely").
    if (!finite(n) || dot(body_.position - hit.point, n) < -.05f)
        return;
    if (wall_.on && dot(n, wall_.normal) < .8f && !handClimb_)
        climbing_ = {};
    // Nor the face of the wall the game's crawl had the body on, met from
    // behind it (supportSurface).
    if (backLeft_ > 0 && dot(n, back_.normal) < -.5f && dot(hit.point - back_.point, back_.normal) < .05f)
        return;
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
    if (!config_.walls || !newWalls_ || !wall.fixed || !finite(wall.point) || length(n) < .5f ||
        std::abs(n.y) > config_.wallSlope)
        return;
    mountWall_ = {wall.point, n, wall.surface, true};
    offered_ = true;
}
void Swing::senseWall(float dt, const Input& in, const WorldQueries& world) {
    sinceWallJump_ += dt;
    backLeft_ = std::max(0.f, backLeft_ - dt);
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
        if (!body_.grounded)
            mountLeft_ = config_.mountHold;
        mountOffered_ = true;
    }
    if (body_.grounded) {
        leaveWall();
        crest_ = 0;
        // No mounts (mountFromGround, allowMounts): the game's crawl offered
        // to the swing is not taken from it, nor is a wall the stick walks
        // the body at.
        if (!config_.mountFromGround || !mounts_) {
            mount_ = 0;
            mountLeft_ = 0;
            return;
        }
        // On the game's own crawl the player stays until he walks on: at rest
        // there the game's interactions are its own, and one the player asked
        // for (game_offers) put him there to run it. Nor is a move of the
        // game's own off the wall taken from it - a vent's way in - only the
        // jump the walk asks for, which ends on the wall as the walked mount's
        // does, stick let go of meanwhile or not.
        if (offered) {
            mount_ = 0;
            mounting_ = finite(in.move) && length(in.move) >= .35f;
            mountLeft_ = mounting_ ? config_.mountHold : std::max(0.f, mountLeft_ - dt);
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
    if (mountLeft_ > 0 && mounts_) {
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
    // The game holds the body on a wall of its own: nothing is taken from
    // there but by the mount's jump, above.
    if (gameHolds_) {
        leaveWall();
        return;
    }
    if (wall_.on) {
        wall_.seconds += dt;
        const Vec3 n = wall_.normal;
        const Vec3 along = body_.velocity - n * dot(body_.velocity, n);
        const float speed = length(along);
        const bool rounding = length(wrapWay_) > .5f;
        // A wall ahead along this one (an inside corner): onto it.
        if (speed > (handClimb_ ? .1f : 1.f) && !rounding)
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
            if (rounding && handClimb_) {
                // Keep going a little past the edge after the new facade is
                // beside us. Stopping at its first ray leaves the camera and
                // shoulders hanging out beyond the corner during the turn.
                if (!wrapSeated_) {
                    wrapEnd_ = dot(at, wrapWay_) + .85f;
                    wrapSeated_ = true;
                }
                if (dot(at, wrapWay_) >= wrapEnd_ || (wrapLeft_ -= dt) <= 0)
                    wrapWay_ = {};
            } else {
                wrapWay_ = {};
            }
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
        if (!rounding && speed > (handClimb_ ? .05f : .3f) && speed <= config_.wallWalkSpeed + 1) {
            const Vec3 ahead = along / speed;
            const Vec3 past = at + ahead * (speed * dt + .3f) - n * (wall_.distance + .4f);
            if (const auto next = wallAlong(past, -ahead, speed * dt + 1.5f)) {
                wall_.normal = next->normal;
                wall_.point = next->point;
                wall_.distance = dot(at - next->point, next->normal);
                wallLost_ = 0;
                wrapWay_ = normalized(-n - next->normal * dot(-n, next->normal));
                wrapLeft_ = handClimb_ ? 1.3f : .8f;
                wrapSeated_ = false;
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
    // This path is how a wall carries on: the next face of a shaft, the wall
    // past a gap, the one a climbing body is pushed at. Refusing it here
    // dropped the player at the vent he was climbing to - the game's own crawl
    // cannot catch a body held 0.9 m clear of the wall, so a refused wall is a
    // fall (the player, October 14: "i went forward, wall walked to the vent
    // and fell"). A climb is never interrupted; only STARTING one is refused
    // when new walls are off, which offerWall does.
    if (wall)
        joinWall(*wall);
}
std::optional<RayHit> Swing::wallContact(const HandInput& hand, const WorldQueries& world) const {
    const Vec3 n = wall_.normal;
    const float distance = dot(hand.wallPosition - wall_.point, n);
    if (!finite(hand.wallPosition) || distance > .18f)
        return {};
    // Start outside the current wall even when the tracked palm passed into
    // it. Project the palm onto the facade, then confirm solid wall there.
    const Vec3 outside = hand.wallPosition + n * (.35f - distance);
    const auto hit = world.raycast(outside, -n, .6f);
    if (hit && hit->fixed && world.exists(hit->surface) && finite(hit->point) && finite(hit->normal) &&
        dot(hit->normal, n) > .8f)
        return hit;
    return {};
}
bool Swing::wallHand(int i, const Input& in, const WorldQueries& world) const {
    if (i < 0 || i > 1 || !handClimb_ || !wall_.on || webs_[i].attached ||
        (finite(in.move) && length(in.move) > .2f))
        return false;
    const auto& h = in.hands[i];
    if (!in.focused || !h.tracked || !finite(h.wallPosition) || !finite(h.gripRelativeToHead) ||
        !std::isfinite(h.wallGrip) || h.wallGrip <= .35f)
        return false;
    if (climbing_[i].attached)
        return true;
    return wallContact(h, world).has_value();
}
void Swing::climbInputs(const Input& in, const WorldQueries& world) {
    for (int i = 0; i < 2; ++i) {
        auto& c = climbing_[i];
        const auto& h = in.hands[i];
        const bool valid = handClimb_ && wall_.on && in.focused && h.tracked &&
                           !(finite(in.move) && length(in.move) > .2f) && finite(h.gripRelativeToHead) &&
                           finite(h.wallPosition) && std::isfinite(h.wallGrip);
        const bool held = valid && h.wallGrip > (c.held ? .35f : .65f);
        const bool press = held && !c.held;
        c.held = valid ? held : true;
        if (!held || (c.attached && !world.exists(c.surface)))
            c.attached = false;
        if (press && !webs_[i].attached && wallHand(i, in, world)) {
            const auto hit = wallContact(h, world);
            if (hit) {
                c.attached = true;
                c.surface = hit->surface;
                c.previous = h.gripRelativeToHead;
                events_.push_back({EventKind::WallGrip, i, .45f});
            }
        }
        c.rate = {};
        if (c.attached) {
            c.rate = limited(trackingTurn(in).rotate(c.previous - h.gripRelativeToHead) /
                                 std::max(climbSeconds_, 1e-3f), 3.f);
            c.rate -= wall_.normal * dot(c.rate, wall_.normal);
            c.previous = h.gripRelativeToHead;
            hands_[i].held = true;
        }
    }
}
void Swing::wallMotion(float dt, const Input& in) {
    const Vec3 n = wall_.normal;
    if (length(wrapWay_) > .5f) {
        // The short clearance move around an outside edge must finish even
        // with a sticky palm. Otherwise its zero normal pull pins the body
        // beside the new face until the hold times out.
        const float speed = handClimb_ && wrapSeated_ ? 1.5f : 3.f;
        body_.velocity = wrapWay_ * speed + n * dot(body_.velocity, n);
        return;
    }
    if (handClimb_ && climbingHands()) {
        Vec3 pull{};
        unsigned count{};
        for (const auto& hand : climbing_)
            if (hand.attached) {
                pull += hand.rate;
                ++count;
            }
        // Two hands share the body: equal pulls move it once, never twice.
        body_.velocity = limited(pull / static_cast<float>(count), config_.maxSpeed);
        return;
    }
    const float out = dot(body_.velocity, n);
    Vec3 along = body_.velocity - n * out;
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
    body_.velocity = limited(along + n * (press_ ? -config_.pressSpeed : out), config_.maxSpeed);
}
void Swing::holdWall(float dt, float before, const World* collision) {
    const Vec3 n = wall_.normal;
    const float distance = dot(body_.position - wall_.point, n), out = dot(body_.velocity, n);
    // Pressed: no clearance kept, nothing taken off the speed into the wall;
    // the game's collision is what stops the body there.
    if (press_) {
        wall_.distance = distance;
        return;
    }
    // No nearer than the clearance; a body already inside it (the facade
    // stepped out, a corner) comes out at a walk. One the wall lets drift
    // off comes back slowly, unless something carries it away.
    const float held = clearance();
    const float nearest = std::min(held, before + 4 * dt);
    float shift{};
    if (distance < nearest)
        shift = nearest - distance;
    else if (distance > held + .1f && out < .2f)
        shift = -std::min(distance - held, 2 * dt);
    if (shift != 0)
        move(body_.position + n * shift, collision);
    if (out < 0 && distance + shift <= held + 1e-3f)
        body_.velocity -= n * out;
    wall_.distance = distance + shift;
}
void Swing::reset(Body b) {
    if (!finite(b.position) || !finite(b.velocity))
        throw std::invalid_argument("Invalid body");
    body_ = b;
    webs_ = {};
    hands_ = {};
    climbing_ = {};
    events_.clear();
    accumulator_ = 0;
    sinceZip_ = sinceLanding_ = 100;
    zipSpeed_ = 0;
    zipDirection_ = {};
    jumpHeld_ = jumpQueued_ = false;
    leapQueued_ = 0;
    leapHeld_ = false;
    wall_ = {};
    wallLost_ = crest_ = 0;
    wrapWay_ = {};
    sinceWallJump_ = 100;
    mount_ = mountLeft_ = 0;
    mounting_ = offered_ = mountOffered_ = false;
    strands_ = 0;
}
void Swing::release(int i, EventKind reason) {
    if (webs_[i].attached)
        events_.push_back({reason, i, .2f});
    // One web in both hands, and the hand that bore the body lets go: the
    // other has the web where it now is. Its own hold was the slack below,
    // as long or as short as that hand had moved since.
    if (const int o = 1 - i; shared() && !bears(o))
        webs_[o].length =
            std::max(std::min(config_.minRope, webs_[o].length), length(hold(o) - body_.position));
    webs_[i] = {};
    hands_[i].zipUsed = false;
    hands_[i].reeling = false;
    hands_[i].pullDistance = hands_[i].pullTime = hands_[i].obstructed = hands_[i].winch = 0;
    hands_[i].slack = 0;
    hands_[i].passing = false;
}
bool Swing::blocks(const RayHit& hit, Vec3 from, Vec3 anchor, bool bend) const {
    const float clearance =
        bend ? config_.bendClearance : std::max(config_.anchorClearance, .1f * length(anchor - from));
    return length(hit.point - anchor) > clearance && length(hit.point - from) > config_.radius;
}
std::array<float, 2> Swing::reeled(std::array<float, 2> rates, float seconds) const {
    std::array<float, 2> lengths{webs_[0].length, webs_[1].length}, taken{};
    for (int i = 0; i < 2; ++i)
        if (webs_[i].attached) {
            // A rope that bends round an edge may be shorter beyond it than a
            // web can be shot: no winch lengthens it.
            const float wanted =
                std::max(std::min(config_.minRope, lengths[i]), lengths[i] - rates[i] * seconds);
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
    if (!bears(1 - i))
        return 1;
    const Vec3 delta = body_.position - hold(1 - i);
    const float dist = length(delta);
    if (dist < other.length - nearlyTaut || dist < 1e-5f)
        return 1; // the other rope is slack
    // Two taut ropes at an angle carry the body faster than either shortens,
    // without bound as they come to oppose each other. Follow that exactly up
    // to a right angle between them; past it, hold the carried speed there.
    const float cosHalfAngle = std::sqrt(std::max(0.f, (1 + dot(radial, delta / dist)) / 2));
    return std::min(1.f, cosHalfAngle * 1.4142135f);
}
void Swing::skimGround(float dt, const WorldQueries& world) {
    const float falling = -body_.velocity.y;
    const float clearance = config_.arcade ? config_.arcadeClearance : config_.groundClearance;
    if (!(clearance > 0) || !(dt > 0) || !(falling > 0) || !finite(body_.position))
        return;
    // Only a body a web holds from above: one on a web to the ground below
    // it goes down to it.
    bool held = false;
    for (int i = 0; i < 2; ++i)
        if (bears(i)) {
            const Vec3 up = hold(i) - body_.position;
            held |= up.y > .3f * length(up);
        }
    if (!held)
        return;
    const float turn = config_.groundTurn;
    // As far as the ground matters: the clearance, and what turning this
    // fall level takes.
    const float reach = clearance + falling * falling / (2 * turn) + falling * dt + .5f;
    const auto ground = world.raycast(body_.position, {0, -1, 0}, std::min(reach, config_.maxRange));
    if (!ground || !finite(ground->point) || !finite(ground->normal))
        return;
    const Vec3 face = normalized(ground->normal);
    if (length(face) < .5f || face.y < .5f)
        return;
    // The fastest fall that still turns level at the clearance; and no more
    // turn in a step than three times the usual, behind as the body may be.
    const float height = body_.position.y - ground->point.y;
    const float allowed = std::sqrt(2 * turn * std::max(0.f, height - clearance));
    const float kept = std::max(allowed, falling - 3 * turn * dt);
    if (kept >= falling)
        return;
    // What the fall loses goes on along the ground, the way the body goes.
    const float speed = length(body_.velocity);
    const float across = length(Vec3{body_.velocity.x, 0, body_.velocity.z});
    body_.velocity.y = -kept;
    if (across > .5f) {
        const float along = std::sqrt(std::max(0.f, speed * speed - kept * kept)) / across;
        body_.velocity.x *= along;
        body_.velocity.z *= along;
    }
}
WebShot Swing::shot(Pose aim, Vec3 from, const WorldQueries& world) const {
    WebShot out;
    const Vec3 forward = aim.orientation.rotate({0, 0, -1});
    if (!finite(aim.position) || !finite(from) || !finite(forward) || length(forward) < .9f || length(forward) > 1.1f)
        return out;
    const Vec3 direction = normalized(forward);
    out.hit = solid(aim.position, direction, config_.maxRange, world, &out.passed);
    const bool airAnchor = !out.hit && config_.airAnchors;
    if (!out.hit && !airAnchor)
        return out;
    const RayHit anchor = airAnchor ? RayHit{aim.position + direction * config_.maxRange, {}, 0, true} : *out.hit;
    // Confirm the body has a clear line to the same anchor. Hands cannot
    // shoot through a wall while the body remains on its other side.
    if (anchor.fixed && (airAnchor || world.exists(anchor.surface)) && finite(anchor.point)) {
        const Vec3 delta = anchor.point - from;
        const float dist = length(delta);
        const auto obstacle = solid(from, normalized(delta), std::max(0.0f, dist - .08f), world);
        if (length(anchor.point - aim.position) <= config_.maxRange + .01f && dist >= config_.minRope &&
            !(obstacle && blocks(*obstacle, from, anchor.point)))
            out.web = Web{true, anchor.point, anchor.surface, dist, 0, airAnchor};
    }
    return out;
}
void Swing::releaseAll() {
    climbing_ = {};
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
    climbSeconds_ = dt;
    climbInputs(in, world);
    jumpQueued_ |= in.jump && !jumpHeld_;
    const bool leap = std::isfinite(in.leap) && in.leap > 0;
    if (leap && !leapHeld_)
        leapQueued_ = std::min(in.leap, 1.f);
    leapHeld_ = leap;
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
            s.rate = {};
            continue;
        }
        // The hand that holds the rope: where the grip is from the head,
        // turned into the world. It follows a jump of that turn (a snap turn)
        // no faster than a hand moves, and its rate is the arm's own motion.
        const Quat turn = trackingTurn(in);
        const Vec3 offset = turn.rotate(h.gripRelativeToHead) + Vec3{0, config_.handHeight, 0};
        // On the other hand's web, as the samples before this one had both
        // hands: a press takes hold of that web.
        const bool onOther = onStrand(i);
        s.reach = offset;
        if (config_.physical) {
            const float over = std::max({dt, stepSeconds_, 1e-3f});
            if (s.sample) {
                const Vec3 was = s.offset;
                s.offset += limited(offset - s.offset, config_.maxHandSpeed * dt);
                s.rate = limited(turn.rotate(h.gripRelativeToHead - s.previous) / over, config_.maxHandSpeed);
                // Toward the web's end the hand makes slack; back from it, it
                // takes that up first (SwingConfig::taut).
                if (w.attached)
                    s.slack = std::clamp(s.slack + length(w.anchor - was - body_.position) -
                                             length(w.anchor - s.offset - body_.position),
                                         0.f, config_.handSlack);
            } else {
                s.offset = offset;
                s.rate = {};
            }
        } else {
            s.offset = s.rate = {};
        }
        // Squeezing the grip shoots this hand's web; holding it keeps the web.
        const bool held = s.held ? h.grip > .35f : h.grip > .65f;
        if (climbing_[i].attached) {
            s.held = true;
            s.sample = false;
            continue;
        }
        const bool shoot = held && !s.held;
        s.held = held;
        if (w.attached && (!held || (!w.airAnchor && !world.exists(w.surface))))
            release(i);
        if (!w.attached && shoot && onOther && webs_[1 - i].attached) {
            // One web in both hands. This hand holds it where it is, as slack
            // there as the web is at the hand that had it; and a web zips
            // once, whichever hand pulls.
            const int o = 1 - i;
            const float slack = std::max(0.f, webs_[o].length - length(hold(o) - body_.position));
            w = webs_[o];
            w.taken = true;
            w.tension = 0;
            w.length = std::max(std::min(config_.minRope, w.length), length(hold(i) - body_.position) + slack);
            s.triggerReleased = false;
            s.reeling = false;
            s.zipUsed = hands_[o].zipUsed;
            s.pullDistance = s.pullTime = s.obstructed = s.slack = 0;
            events_.push_back({EventKind::Join, i, .5f});
        } else if (!w.attached && shoot) {
            // From the body, stood off a surface the game holds it on.
            const Vec3 from = length(support_) > .5f ? body_.position + support_ * config_.wallClearance
                                                     : body_.position;
            if (const auto fired = shot(h.aim, from, world); fired.web) {
                w = *fired.web;
                w.strand = ++strands_;
                // To the hand that holds it.
                w.length = std::max(config_.minRope, length(hold(i) - body_.position));
                s.triggerReleased = false;
                s.reeling = false;
                s.zipUsed = false;
                s.pullDistance = s.pullTime = s.obstructed = s.slack = 0;
                events_.push_back({EventKind::Attach, i, .65f});
                if (fired.passed)
                    events_.push_back({EventKind::Pass, i, 0});
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
                w.length = std::min(w.length, std::max(std::min(config_.minRope, w.length),
                                                       length(hold(i) - body_.position)));
            }
            s.reeling = reel;
            // Motion must be both fast and deliberate within a short window.
            // Head-relative samples remove virtual movement and room-scale translation.
            if (s.sample && !s.zipUsed && bears(i)) {
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
                        if (shared())
                            hands_[1 - i].zipUsed = true;
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
    if (leapQueued_ > 0) {
        // A charged leap: up, and along the stick. From the ground only.
        if (body_.grounded && !wall_.on) {
            const Vec3 stick = finite(in.move) ? Vec3{in.move.x, 0, in.move.z} : Vec3{};
            const float tilt = std::min(length(stick), 1.f);
            const Vec3 along = tilt > .1f ? stick * (config_.leapForward * (.5f + .5f * leapQueued_) / length(stick) * tilt)
                                          : Vec3{};
            body_.velocity = limited(
                along + Vec3{0, config_.leapUp + (config_.leapUpFull - config_.leapUp) * leapQueued_, 0},
                config_.maxSpeed);
            body_.grounded = false;
            events_.push_back({EventKind::Leap, -1, leapQueued_});
            jumpQueued_ = false;
        }
        leapQueued_ = 0;
    }
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
    const Vec3 before = body_.position;
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
        if (!supported && !wasGrounded)
            skimGround(dt, world);
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
        int passed{};
        const auto hit = solid(body_.position, normalized(d), std::max(0.0f, dist - .1f), world, &passed);
        // Swung past a thin or loose thing (arcade webs): once as the line comes onto it.
        if (passed && !hands_[i].passing)
            events_.push_back({EventKind::Pass, i, 0});
        hands_[i].passing = passed > 0;
        // Swinging along a facade, the line grazes ledges and sills near the
        // anchor every few frames. Only a wall that stays in the way releases:
        // one a physical web cannot bend round (no edge found, too many).
        auto& obstructed = hands_[i].obstructed;
        bool blocked = hit && blocks(*hit, body_.position, w.anchor, w.bent > 0);
        if (config_.physical) {
            if (blocked)
                blocked = !wrap(i, before, *hit, world);
            else
                unwrap(i, world);
        }
        obstructed = blocked ? obstructed + dt : 0;
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
    const bool bearing[2] = {bears(0), bears(1)};
    // Taut, in the air: no web slacker than its hand made it. After the
    // winch, and with the hand's own pull in this step, or the rope's own
    // pulls are taken in twice and the body climbs at twice their speed. On
    // the ground the game walks the body, its webs paying out (payOut), and
    // on a wall the stick walks it.
    if (config_.taut && !body_.grounded && !wall_.on)
        for (int i = 0; i < 2; ++i) {
            auto& w = webs_[i];
            const Vec3 out = body_.position - hold(i);
            const float span = length(out);
            if (!bearing[i] || span < 1e-5f)
                continue;
            const float pulled = std::max(0.f, dot(hands_[i].rate, out / span)) * dt;
            const float slackest = span + hands_[i].slack + pulled;
            w.length = std::min(w.length, std::max(std::min(config_.minRope, w.length), slackest));
        }
    // What each rope holds the body to: its anchor, or with physical webs
    // the point that keeps the hand on it.
    const Rope ropes[2] = {{hold(0), webs_[0].length}, {hold(1), webs_[1].length}};
    if (bearing[0] && bearing[1])
        move(projectTwoWebs(body_.position, ropes[0], ropes[1]), collision);

    // Unilateral, tension-only constraints. Slack rope never pushes the player.
    // Alternate order to reduce left/right bias with two simultaneous ropes.
    float pull[2]{}; // speed each rope has taken out of the body in this step
    for (int pass = 0; pass < 8; ++pass)
        for (int j = 0; j < 2; ++j) {
            const int i = (pass % 2) ? 1 - j : j;
            auto& w = webs_[i];
            if (!bearing[i])
                continue;
            const Vec3 delta = body_.position - ropes[i].anchor;
            const float dist = length(delta);
            if (dist < w.length - nearlyTaut || dist < 1e-5f)
                continue;
            const Vec3 radial = delta / dist;
            if (dist > w.length)
                move(ropes[i].anchor + radial * w.length, collision);
            // A taut rope that is being reeled carries the body inward at the
            // winch rate. With the pull applied to position only, the velocity
            // never held it, and letting go mid-reel stopped the body dead.
            // A hand that pulls on its rope does the same (physical webs).
            const float outward = dot(body_.velocity, radial) + hands_[i].winch * follow(i, radial) +
                                  dot(hands_[i].rate, radial);
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
    float extra = actualSeconds - predictedSeconds;
    if (!std::isfinite(extra) || std::abs(extra) > .1f)
        return;
    // A step longer than the longest was predicted as that one: the body
    // went that share of the way, and the rope is wound for as much.
    if (predictedSeconds > longestNativeStep)
        extra *= longestNativeStep / predictedSeconds;
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
    if (!std::isfinite(seconds) || seconds <= 0 || !input.focused ||
        !std::isfinite(inputSeconds) || inputSeconds < 0 || !finite(actual.position) ||
        !finite(actual.velocity) || length(actual.velocity) > config_.maxSpeed * 2) {
        releaseAll();
        return {};
    }
    // A long step is a slow game, not stale input. Over 50 ms used to be
    // refused here: the caller let go of a swinging player, whom the game
    // then dropped at its own fall speed (October 10 headset report).
    seconds = std::min(seconds, longestNativeStep);
    if (actual.grounded && !body_.grounded)
        sinceLanding_ = 0;
    body_ = actual;
    body_.velocity = limited(body_.velocity, config_.maxSpeed);
    stepSeconds_ = seconds;
    // A hand moves its rope once per sample of it.
    if (!(inputSeconds > 0))
        for (auto& hand : hands_)
            hand.rate = {};
    if (!(inputSeconds > 0))
        for (auto& hand : climbing_)
            hand.rate = {};
    if (inputSeconds > 0) {
        // The first sample after a gap in input (a long frame, a moment
        // without focus) starts each hand's motion afresh: travel across the
        // gap is no yank. Releasing the webs there dropped the player.
        if (inputSeconds > .1f)
            for (auto& hand : hands_)
                hand.sample = false;
        if (inputSeconds > .1f)
            climbing_ = {};
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
