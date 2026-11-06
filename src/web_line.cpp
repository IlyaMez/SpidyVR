#include "spidy/web_line.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace spidy {
namespace {
void validate(const LineConfig& c) {
    const float positive[] = {c.handsNear, c.minLength, c.maxLength, c.seconds, c.reach, c.radius};
    for (float v : positive)
        if (!std::isfinite(v) || v <= 0)
            throw std::invalid_argument("Invalid web line configuration");
    if (!std::isfinite(c.grace) || c.grace < 0 || c.minLength >= c.maxLength || !c.maxLines)
        throw std::invalid_argument("Invalid web line configuration");
}
} // namespace

Vec3 nearestOnSegment(Vec3 a, Vec3 b, Vec3 p) {
    const Vec3 ab = b - a;
    const float span = dot(ab, ab);
    if (span < 1e-8f)
        return a;
    return a + ab * std::clamp(dot(p - a, ab) / span, 0.f, 1.f);
}
float rayToSegment(Vec3 origin, Vec3 direction, float distance, Vec3 a, Vec3 b, Vec3& onSegment) {
    // The closest points of two segments: the ray's (origin, origin +
    // direction * distance) and a-b.
    const Vec3 d1 = direction * distance, d2 = b - a, r = origin - a;
    const float aa = dot(d1, d1), ee = dot(d2, d2), f = dot(d2, r);
    float s = 0, t = 0;
    if (aa < 1e-8f && ee < 1e-8f) {
        onSegment = a;
        return 0;
    }
    if (aa < 1e-8f) {
        t = std::clamp(f / ee, 0.f, 1.f);
    } else {
        const float c = dot(d1, r);
        if (ee < 1e-8f) {
            s = std::clamp(-c / aa, 0.f, 1.f);
        } else {
            const float bb = dot(d1, d2), denom = aa * ee - bb * bb;
            s = denom > 1e-8f ? std::clamp((bb * f - c * ee) / denom, 0.f, 1.f) : 0.f;
            t = (bb * s + f) / ee;
            if (t < 0) {
                t = 0;
                s = std::clamp(-c / aa, 0.f, 1.f);
            } else if (t > 1) {
                t = 1;
                s = std::clamp((bb - c) / aa, 0.f, 1.f);
            }
        }
    }
    onSegment = a + d2 * t;
    return s * distance;
}

LineGesture::LineGesture(LineConfig config) : config_(config) {
    validate(config_);
}
void LineGesture::reset() {
    last_.reset();
    since_ = 1e9f;
    armed_ = false;
}
std::optional<LineRequest> LineGesture::update(float seconds, bool click,
                                               const std::array<LineHand, 2>& hands) {
    const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
    const auto& a = hands[0];
    const auto& b = hands[1];
    const float span = finite(a.anchor) && finite(b.anchor) ? length(b.anchor - a.anchor) : 0.f;
    armed_ = a.tracked && b.tracked && a.wallWeb && b.wallWeb && !a.grabbing && !b.grabbing &&
             finite(a.grip) && finite(b.grip) && length(a.grip - b.grip) <= config_.handsNear &&
             span >= config_.minLength && span <= config_.maxLength;
    if (armed_) {
        last_ = LineRequest{a.anchor, b.anchor};
        since_ = 0;
    } else {
        since_ = std::min(since_ + dt, 1e9f);
    }
    const bool pressed = click && !clicked_;
    clicked_ = click;
    if (!pressed || !last_ || since_ > config_.grace)
        return {};
    const auto out = *last_;
    last_.reset();
    since_ = 1e9f;
    return out;
}

WebLines::WebLines(LineConfig config) : config_(config) {
    validate(config_);
}
bool WebLines::kept(std::uint32_t id, std::uint32_t keep) const {
    // Up from `keep` through what its ends are on.
    std::vector<std::uint32_t> todo{keep}, seen;
    while (!todo.empty()) {
        const auto at = todo.back();
        todo.pop_back();
        if (!at || std::find(seen.begin(), seen.end(), at) != seen.end())
            continue;
        if (at == id)
            return true;
        seen.push_back(at);
        for (const auto& l : lines_)
            if (l.id == at) {
                todo.push_back(l.on[0]);
                todo.push_back(l.on[1]);
            }
    }
    return false;
}
void WebLines::prune() {
    for (bool again = true; again;) {
        again = false;
        for (auto it = lines_.begin(); it != lines_.end(); ++it) {
            bool held = true;
            for (const auto on : it->on)
                if (on && std::none_of(lines_.begin(), lines_.end(),
                                       [&](const WebLineSpan& l) { return l.id == on; }))
                    held = false;
            if (!held) {
                lines_.erase(it);
                ++expired_;
                again = true;
                break;
            }
        }
    }
}
bool WebLines::add(Vec3 start, Vec3 end, std::uint32_t startOn, std::uint32_t endOn, std::uint32_t keep) {
    if (!finite(start) || !finite(end))
        return false;
    const float span = length(end - start);
    if (span < config_.minLength || span > config_.maxLength)
        return false;
    // Only on lines there are.
    for (auto* on : {&startOn, &endOn})
        if (*on &&
            std::none_of(lines_.begin(), lines_.end(), [&](const WebLineSpan& l) { return l.id == *on; }))
            *on = 0;
    while (lines_.size() >= config_.maxLines) {
        // The oldest that may go: not the kept one, nor a line it is on, nor
        // one the new line is on.
        auto oldest = std::find_if(lines_.begin(), lines_.end(), [&](const WebLineSpan& l) {
            return !kept(l.id, keep) && !kept(l.id, startOn) && !kept(l.id, endOn);
        });
        if (oldest == lines_.end())
            return false;
        lines_.erase(oldest);
        ++expired_;
        prune();
    }
    WebLineSpan line{next_++, start, end, 0};
    line.on[0] = startOn;
    line.on[1] = endOn;
    lines_.push_back(line);
    ++made_;
    return true;
}
void WebLines::step(float dt, Vec3 player, std::uint32_t keep) {
    if (!std::isfinite(dt) || dt < 0)
        return;
    const auto before = lines_.size();
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(),
                                [&](WebLineSpan& l) {
                                    l.age += dt;
                                    if (kept(l.id, keep))
                                        return false;
                                    const bool far =
                                        finite(player) && length(nearestOnSegment(l.start, l.end, player) -
                                                                 player) > config_.reach;
                                    return l.age > config_.seconds || far;
                                }),
                 lines_.end());
    expired_ += before - lines_.size();
    prune();
}
std::optional<LineHit> WebLines::raycast(Vec3 origin, Vec3 direction, float distance) const {
    if (!finite(origin) || !finite(direction) || !std::isfinite(distance) || distance <= 0 ||
        length(direction) < .5f)
        return {};
    const Vec3 dir = normalized(direction);
    std::optional<LineHit> best;
    for (const auto& l : lines_) {
        Vec3 on{};
        const float along = rayToSegment(origin, dir, distance, l.start, l.end, on);
        const Vec3 at = origin + dir * along;
        if (length(at - on) > config_.radius)
            continue;
        if (!best || along < best->distance)
            best = LineHit{l.id, on, along};
    }
    return best;
}
std::optional<LineTarget> lineTarget(const std::vector<WebLineSpan>& lines, Vec3 eye, Vec3 look,
                                     const LineZipConfig& c) {
    if (!finite(eye) || !finite(look) || length(look) < .5f)
        return {};
    const Vec3 dir = normalized(look);
    std::optional<LineTarget> best;
    float bestAngle = c.cone;
    for (const auto& l : lines) {
        // The point of the line nearest the look's ray, and a few round it:
        // the angle is smallest near there.
        Vec3 on{};
        rayToSegment(eye, dir, c.farthest, l.start, l.end, on);
        const Vec3 ab = l.end - l.start;
        const float span = length(ab);
        if (span < 1e-3f)
            continue;
        const float at = std::clamp(dot(on - l.start, ab) / (span * span), 0.f, 1.f);
        for (int k = -4; k <= 4; ++k) {
            const float t = std::clamp(at + k * (.5f / span), 0.f, 1.f);
            const Vec3 p = l.start + ab * t;
            const Vec3 to = p - eye;
            const float d = length(to);
            if (d < c.nearest || d > c.farthest)
                continue;
            const float angle = std::acos(std::clamp(dot(to / d, dir), -1.f, 1.f));
            if (angle < bestAngle) {
                bestAngle = angle;
                best = LineTarget{l.id, p};
            }
        }
    }
    return best;
}
LineZip::LineZip(LineZipConfig config) : config_(config) {
    const auto& c = config_;
    const float positive[] = {
        c.cone,        c.nearest,     c.farthest,   c.speed,       c.accel,     c.ease,      c.arrive,
        c.hold,        c.holdSpeed,   c.maxSeconds, c.waitSeconds, c.walkSpeed, c.jumpSpeed, c.gravity,
        c.catchRadius, c.catchHeight, c.dropBelow,  c.catchMargin, c.walkSlack, c.speedBase, c.pullAccel};
    for (float v : positive)
        if (!std::isfinite(v) || v <= 0)
            throw std::invalid_argument("Invalid line zip configuration");
    if (!std::isfinite(c.perchHeight) || !std::isfinite(c.endMargin) || c.endMargin < 0 ||
        !std::isfinite(c.jumpCarry) || c.jumpCarry < 0 || c.nearest >= c.farthest)
        throw std::invalid_argument("Invalid line zip configuration");
}
void LineZip::start(LineTarget target) {
    if (!target.id || !finite(target.point))
        return;
    target_ = target;
    perch_ = target.point + Vec3{0, config_.perchHeight, 0};
    phase_ = Phase::Flying;
    flown_ = waited_ = total_ = 0;
    jumped_ = true; // a jump held into the zip is not one off the line
    ++zips_;
}
void LineZip::stop() {
    phase_ = Phase::Idle;
    target_ = {};
    dashLeft_ = 0;
}
void LineZip::dash(Vec3 way) {
    if (phase_ != Phase::Perched || !finite(way) || length(way) < 1e-3f)
        return;
    dashWay_ = way;
    dash_ = 0; // its sign along the line is taken at the next step
    dashLeft_ = config_.dashSeconds;
    ++dashes_;
}
void LineZip::setGravity(float gravity) {
    if (std::isfinite(gravity) && gravity > 0)
        config_.gravity = gravity;
}
namespace {
// Where on line `l` (0..1) the point nearest p lies.
float alongLine(const WebLineSpan& l, Vec3 p) {
    const Vec3 ab = l.end - l.start;
    const float span = dot(ab, ab);
    return span > 1e-8f ? std::clamp(dot(p - l.start, ab) / span, 0.f, 1.f) : 0.f;
}
} // namespace
void LineZip::follow(Vec3 position, const WebLineSpan& line) {
    if (phase_ != Phase::Perched || !finite(position))
        return;
    const Vec3 ab = line.end - line.start;
    const float span = length(ab);
    if (span < 1e-3f)
        return;
    const float margin = std::min(config_.endMargin / span, .5f);
    along_ = std::clamp(alongLine(line, position - Vec3{0, config_.perchHeight, 0}), margin, 1 - margin);
    perch_ = line.start + ab * along_ + Vec3{0, config_.perchHeight, 0};
}
bool LineZip::gameWalk(Vec3 position, Vec3 move, const WebLineSpan& line) const {
    if (phase_ != Phase::Perched || dashLeft_ > 0 || !finite(position) || !finite(move))
        return false;
    const Vec3 ab = line.end - line.start;
    const float span = length(ab);
    if (span < 1e-3f || length(move) < .25f)
        return false;
    const Vec3 dir = ab / span;
    const float way = dot(move, dir);
    if (std::abs(way) < .15f)
        return false; // the stick across the line walks nowhere
    const Vec3 feet = position - Vec3{0, config_.perchHeight, 0};
    const float t = dot(feet - line.start, dir) / span;
    const float margin = std::min(config_.endMargin / span, .5f);
    if ((t <= margin && way < 0) || (t >= 1 - margin && way > 0))
        return false; // at an end, pushed past it
    const Vec3 on = line.start + ab * std::clamp(t, 0.f, 1.f);
    return length(feet - on) <= config_.walkSlack;
}
LineZip::Step LineZip::step(float dt, Vec3 position, Vec3 velocity, bool waiting, const WebLineSpan* line,
                            Vec3 move, bool jump) {
    Step out;
    const bool pressed = jump && !jumped_;
    jumped_ = jump;
    if (phase_ == Phase::Idle || !std::isfinite(dt) || dt < 0 || !finite(position) || !finite(velocity))
        return out;
    if (!line) {
        stop();
        return out;
    }
    const auto& c = config_;
    const Vec3 up{0, 1, 0};
    const Vec3 ab = line->end - line->start;
    const float span = length(ab);
    if (span < 1e-3f) {
        stop();
        return out;
    }
    const Vec3 dir = ab / span;
    if (!finite(move))
        move = {};
    if (phase_ == Phase::Flying) {
        total_ += dt;
        if (total_ > c.maxSeconds + c.waitSeconds) {
            stop();
            return out;
        }
        if (waiting) {
            if ((waited_ += dt) > c.waitSeconds)
                stop();
            return out;
        }
        if (flown_ == 0)
            zipSpan_ = length(perch_ - position);
        flown_ += dt;
        if (flown_ > c.maxSeconds) {
            stop();
            return out;
        }
        const Vec3 to = perch_ - position;
        const float left = length(to);
        if (left > c.arrive) {
            // The web shot out: the body goes on as it went, the game's gravity
            // left to the swing (not driven).
            if (flown_ < c.shotSeconds)
                return out;
            // Its path bows up over a far zip, the lift fading as he nears.
            const float lift = std::min(c.farLift * zipSpan_, c.liftMost) *
                               std::sqrt(std::clamp(left / std::max(zipSpan_, 1e-3f), 0.f, 1.f));
            const Vec3 aim =
                perch_ + Vec3{0, lift * std::min(1.f, (left - c.ease) / std::max(c.ease, 1e-3f)), 0};
            const Vec3 way = aim - position;
            const float reach = std::max(length(way), 1e-3f);
            const float top = std::min(c.speed, c.speedBase + c.speedPerMetre * zipSpan_);
            // Slowing in time to stop on the perch at the pull it has (not past it
            // and back, as a far zip at full speed went).
            const float pull = zipSpan_ > 8 ? c.pullAccel : c.accel;
            const float speed = std::min({top, top * left / c.ease, std::sqrt(2 * pull * .8f * left)});
            const Vec3 wanted = way * (std::max(speed, 2.f) / reach);
            const Vec3 change = wanted - velocity;
            const float most = pull * dt;
            out.velocity = length(change) > most ? velocity + change * (most / length(change)) : wanted;
            // Never past the perch in one step.
            if (length(out.velocity) * dt > left)
                out.velocity = to / std::max(dt, 1e-4f);
            out.drive = true;
            return out;
        }
        phase_ = Phase::Perched;
        along_ = alongLine(*line, target_.point);
        ++perches_;
        out.arrived = true;
    }
    if (phase_ == Phase::Jumping) {
        jumping_ = jumping_ + Vec3{0, -c.gravity * dt, 0};
        const Vec3 feet = position - up * c.perchHeight;
        const float t = alongLine(*line, feet);
        const Vec3 under = line->start + ab * t;
        Vec3 across = feet - under;
        across.y = 0;
        if (jumping_.y <= 0 && length(across) <= c.catchRadius &&
            std::abs(feet.y - under.y) <= c.catchHeight) {
            // Down on the line again.
            phase_ = Phase::Perched;
            along_ = t;
            ++landings_;
            out.arrived = true;
        } else if (feet.y < under.y - c.dropBelow ||
                   (jumping_.y <= 0 && length(across) > c.catchRadius + c.catchMargin)) {
            stop(); // the jump took him off the line: he falls, as the swing falls
            return out;
        } else {
            out.drive = true;
            out.velocity = jumping_;
            return out;
        }
    }
    // On the line: walked along it by the stick, never past its ends, or
    // dashed along it by a dodge.
    float walk = dot(move, dir) * c.walkSpeed;
    if (dashLeft_ > 0) {
        if (dash_ == 0)
            dash_ = dot(dashWay_, dir) >= 0 ? c.dashSpeed : -c.dashSpeed;
        walk = dash_;
        dashLeft_ -= dt;
    }
    const float margin = std::min(c.endMargin / span, .5f);
    along_ = std::clamp(along_ + walk * dt / span, margin, 1 - margin);
    perch_ = line->start + ab * along_ + up * c.perchHeight;
    if (pressed) {
        phase_ = Phase::Jumping;
        Vec3 carry = move;
        carry.y = 0;
        jumping_ = dir * walk + carry * c.jumpCarry + up * c.jumpSpeed;
        ++jumps_;
        out.drive = true;
        out.velocity = jumping_;
        return out;
    }
    const Vec3 to = perch_ - position;
    const float left = length(to);
    const float speed = std::min(left * c.hold, c.holdSpeed);
    const bool atEnd = along_ <= margin + 1e-4f || along_ >= 1 - margin - 1e-4f;
    out.drive = true;
    out.velocity = (left > 1e-4f ? to * (speed / left) : Vec3{}) + (atEnd ? Vec3{} : dir * walk);
    return out;
}
LineAim::State LineAim::update(float seconds, bool click, bool gestureArmed, bool gestureTook) {
    const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
    State out;
    if (click && !before_) {
        held_ = 0;
        used_ = gestureArmed;
        aimed_ = false;
    }
    if (click) {
        held_ += dt;
        used_ = used_ || gestureTook || gestureArmed;
        out.aiming = !used_ && held_ >= hold;
        aimed_ = aimed_ || out.aiming;
    }
    out.zip = !click && before_ && !used_ && aimed_;
    if (!click)
        aimed_ = false;
    before_ = click;
    return out;
}
std::optional<RayHit> LineWorld::raycast(Vec3 origin, Vec3 direction, float distance) const {
    auto hit = world_.raycast(origin, direction, distance);
    if (!finite(origin) || !finite(direction) || length(direction) < .5f || !(distance > ignoreNear))
        return hit;
    const Vec3 dir = normalized(direction);
    // Past ignoreNear: from there on.
    const Vec3 from = origin + dir * ignoreNear;
    const auto line = lines_.raycast(from, dir, distance - ignoreNear);
    if (!line)
        return hit;
    const float along = ignoreNear + line->distance;
    if (hit && length(hit->point - origin) <= along)
        return hit;
    return RayHit{line->point, dir * -1.f, lineSurface | line->id, true};
}
bool LineWorld::exists(std::uint64_t surface) const {
    if ((surface & lineSurfaceMask) != lineSurface)
        return world_.exists(surface);
    const auto id = static_cast<std::uint32_t>(surface & ~lineSurfaceMask);
    for (const auto& l : lines_.lines())
        if (l.id == id)
            return true;
    return false;
}
void WebLines::clear() {
    lines_.clear();
}
} // namespace spidy
