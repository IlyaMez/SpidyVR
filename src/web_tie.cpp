#include "spidy/web_tie.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace spidy {
namespace {
void validate(const TieConfig& c) {
    const float positive[] = {c.handsNear, c.approachSpeed, c.approachGain, c.webForce, c.fightOverSeconds,
                              c.launchShare};
    for (float v : positive)
        if (!std::isfinite(v) || v <= 0)
            throw std::invalid_argument("Invalid tie configuration");
    const float nonnegative[] = {c.grace, c.gap, c.gravity, c.arcShare, c.arcLift, c.arcEnd, c.overheadReach};
    for (float v : nonnegative)
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid tie configuration");
    if (!c.maxTies)
        throw std::invalid_argument("Invalid tie configuration");
}
} // namespace

TieGesture::TieGesture(TieConfig config) : config_(config) {
    validate(config_);
}
void TieGesture::reset() {
    last_.reset();
    since_ = 1e9f;
    armed_ = false;
}
std::optional<TieRequest> TieGesture::update(float seconds, bool click, const std::array<TieHand, 2>& hands) {
    const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
    armed_ = false;
    for (int a = 0; a < 2 && !armed_; ++a) {
        const auto& wall = hands[a];
        const auto& held = hands[1 - a];
        if (!wall.tracked || !held.tracked || !wall.wallWeb || !finite(wall.anchor) || !held.grabbing ||
            !held.target || !held.character || !held.cocooned || !finite(wall.grip) || !finite(held.grip) ||
            length(wall.grip - held.grip) > config_.handsNear)
            continue;
        armed_ = true;
        last_ = TieRequest{a, 1 - a, wall.anchor, held.target};
        since_ = 0;
    }
    if (!armed_)
        since_ = std::min(since_ + dt, 1e9f);
    // Only the press counts: a click held through the hands coming together
    // ties nothing.
    const bool pressed = click && !clicked_;
    clicked_ = click;
    if (!pressed || !last_ || since_ > config_.grace)
        return {};
    const auto out = *last_;
    // The pairing is spent: both webs leave the hands.
    last_.reset();
    since_ = 1e9f;
    return out;
}

Vec3 tiePlace(Vec3 anchor, Vec3 normal, float radius, const TieConfig& config) {
    const Vec3 n = finite(normal) && length(normal) > .5f ? normalized(normal) : Vec3{0, 1, 0};
    const float r = std::max(radius, 0.f);
    const float under = std::clamp(-n.y, 0.f, 1.f);
    const float reach = r + (std::max(config.overheadReach, r) - r) * under;
    return anchor + n * (reach + config.gap);
}
Vec3 tieAim(Vec3 position, Vec3 place, Vec3 normal, const TieConfig& config) {
    const float left = length(place - position);
    // A wall (normal level) or the floor: the whole arc; a ceiling: none.
    const float facing = finite(normal) && length(normal) > .5f ? normalized(normal).y : 0.f;
    const float share = std::clamp((facing + .5f) / .5f, 0.f, 1.f);
    const float lift = std::clamp(config.arcShare * (left - config.arcEnd), 0.f, config.arcLift) * share;
    return place + Vec3{0, lift, 0};
}
namespace {
Vec3 surfaceNormal(const Tie& tie) {
    const Vec3 out = tie.place - tie.anchor;
    return length(out) > 1e-3f ? normalized(out) : Vec3{0, 1, 0};
}
Vec3 wantedVelocity(Vec3 position, const Tie& tie, const TieConfig& config) {
    const Vec3 to = tieAim(position, tie.place, surfaceNormal(tie), config) - position;
    const float left = length(to);
    if (left <= 1e-3f)
        return {};
    return to * (std::min(config.approachSpeed, left * config.approachGain) / left);
}
} // namespace
Vec3 tieLaunch(const GrabTarget& target, const Tie& tie, const TieConfig& config) {
    return wantedVelocity(target.position, tie, config) * config.launchShare;
}
TargetCommand tieCommand(const GrabTarget& target, const Tie& tie, float dt, const TieConfig& config) {
    TargetCommand c;
    c.id = target.id;
    c.kind = target.kind;
    c.mode = TargetCommand::Mode::Follow;
    const Vec3 wanted = wantedVelocity(target.position, tie, config);
    // Follow adds gravity after the brake: asking for it to be taken out
    // first leaves the target at the wanted velocity, not sagging under it.
    c.velocity = wanted + Vec3{0, config.gravity * std::max(dt, 0.f), 0};
    c.maxAcceleration = config.webForce / std::max(target.mass, 1.f);
    return c;
}

WebTies::WebTies(TieConfig config) : config_(config) {
    validate(config_);
}
bool WebTies::add(const GrabTarget& target, Vec3 anchor, Vec3 normal) {
    if (!target.id || !finite(target.position) || !finite(anchor))
        return false;
    drop(target.id);
    if (ties_.size() >= config_.maxTies)
        return false;
    ties_.push_back({target.id, anchor, tiePlace(anchor, normal, target.radius, config_), 0, false});
    quiet_ = 0;
    ++made_;
    return true;
}
void WebTies::step(float dt, const TargetQueries& targets, bool fightOn, const std::vector<std::uint64_t>& taken,
                   std::vector<TargetCommand>& out) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    quiet_ = fightOn ? 0.f : quiet_ + dt;
    if (!ties_.empty() && quiet_ >= config_.fightOverSeconds) {
        // The fight is over: every tie lets go.
        ended_ += ties_.size();
        ties_.clear();
        return;
    }
    ties_.erase(std::remove_if(ties_.begin(), ties_.end(),
                               [&](Tie& tie) {
                                   if (std::find(taken.begin(), taken.end(), tie.target) != taken.end()) {
                                       ++taken_;
                                       return true;
                                   }
                                   const auto target = targets.find(tie.target);
                                   if (!target || !finite(target->position) || !finite(target->velocity)) {
                                       ++gone_;
                                       return true;
                                   }
                                   tie.age += dt;
                                   tie.arrived |= length(tie.place - target->position) < .3f;
                                   out.push_back(tieCommand(*target, tie, dt, config_));
                                   return false;
                               }),
                ties_.end());
}
bool WebTies::tied(std::uint64_t target) const {
    return std::any_of(ties_.begin(), ties_.end(), [&](const Tie& t) { return t.target == target; });
}
void WebTies::settle(std::uint64_t target, Vec3 place) {
    if (!finite(place))
        return;
    for (auto& t : ties_)
        if (t.target == target) {
            t.place = place;
            t.arrived = true;
        }
}
void WebTies::drop(std::uint64_t target) {
    ties_.erase(std::remove_if(ties_.begin(), ties_.end(), [&](const Tie& t) { return t.target == target; }),
                ties_.end());
}
void WebTies::clear() {
    ties_.clear();
    quiet_ = 0;
}
} // namespace spidy
