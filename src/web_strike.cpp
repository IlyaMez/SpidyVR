#include "spidy/web_strike.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace spidy {
namespace {
float real(float seconds) {
    return std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
}
// Nothing fixed stands between the two points. What a ray meets within a
// metre of its end is the target's own cover: the ground under a thug who
// lies there, the wall he stands against.
bool inSight(const WorldQueries& world, Vec3 from, Vec3 to) {
    const Vec3 line = to - from;
    const float distance = length(line);
    if (distance < 1)
        return true;
    const auto hit = world.bodyRaycast(from, line / distance, distance);
    return !hit || !hit->fixed || length(hit->point - from) > distance - 1;
}
} // namespace

WebStrike::WebStrike(WebStrikeConfig c) : config_(c) {
    const auto bad = [](float v) { return !std::isfinite(v) || v < 0; };
    if (bad(c.minSpeed) || c.minSpeed == 0 || !std::isfinite(c.outward) || !std::isfinite(c.aim) || c.aim > 1 ||
        !std::isfinite(c.heldAim) || c.heldAim > c.aim || bad(c.neck) || bad(c.minDistance) || bad(c.maxDistance) ||
        c.maxDistance <= c.minDistance || bad(c.chest) || bad(c.freeSeconds) || bad(c.rearmSpeed) ||
        c.rearmSpeed >= c.minSpeed || bad(c.standOff) || bad(c.middle) || bad(c.speed) || c.speed == 0 ||
        bad(c.minSeconds) || bad(c.arrive) || c.arrive == 0 || bad(c.grace) || bad(c.reach) || bad(c.hang) ||
        bad(c.maxHang) || bad(c.minStrength) || c.minStrength > 1 || bad(c.pause) || bad(c.sustain) ||
        bad(c.pressSeconds) || bad(c.stall) || c.stall == 0 || bad(c.liftOff) || bad(c.maxWait) ||
        bad(c.diveSpeed) || c.diveSpeed == 0 || bad(c.diveMiss) || bad(c.diveTime) || bad(c.diveReach) ||
        bad(c.diveFlight) || c.diveFlight == 0 || bad(c.diveFull) || c.diveFull <= c.diveSpeed)
        throw std::invalid_argument("Invalid web strike configuration");
}
float WebStrike::diveStrength(float speed) const {
    if (!std::isfinite(speed) || speed < config_.diveSpeed)
        return 0;
    // From a heavy blow's strength (DeliveryConfig::heavy) to the hardest.
    return .85f + .15f * std::clamp((speed - config_.diveSpeed) / (config_.diveFull - config_.diveSpeed), 0.f, 1.f);
}
std::optional<WebStrike::Opening> WebStrike::dive(Vec3 velocity, bool grounded, Vec3 head, Vec3 feet,
                                                  std::span<const FightTarget> targets, const WorldQueries& world,
                                                  std::uint64_t marked, const PunchConfig& punch) {
    const float speed = finite(velocity) ? length(velocity) : 0.f;
    if (phase_ != Phase::Idle || sinceEnd_ < config_.pause || grounded || !marked || speed < config_.diveSpeed ||
        !finite(head) || !finite(feet))
        return std::nullopt;
    const Vec3 way = velocity / speed, chest = feet + Vec3{0, config_.chest, 0};
    for (const auto& t : targets) {
        if (t.id != marked || !finite(t.feet))
            continue;
        // On course for him: his middle ahead, the line passing close to it,
        // and reached soon.
        const Vec3 to = t.middle() - chest;
        const float along = dot(to, way), distance = length(to);
        const float miss = length(to - way * along);
        if (along <= 0 || distance > config_.diveReach || along / speed > config_.diveTime ||
            miss > config_.diveMiss + std::max(0.f, t.radius - .3f) || !inSight(world, head, t.middle()))
            return std::nullopt;
        phase_ = Phase::Flying;
        hand_ = diveHand;
        target_ = t.id;
        direction_ = way;
        fistSpeed_ = speedOf(punch, diveStrength(speed));
        elapsed_ = sinceStep_ = waited_ = 0;
        destination_ = stop(feet, t);
        const float flight = std::clamp(speed, config_.diveFlight, config_.speed * 1.25f);
        duration_ = std::max(config_.minSeconds, length(destination_ - feet) / flight);
        return Opening{hand_, target_, distance, false, false};
    }
    return std::nullopt;
}
void WebStrike::cancel() {
    if (phase_ == Phase::Flying)
        sinceEnd_ = 0;
    phase_ = Phase::Idle;
    hangLeft_ = 0;
}
void WebStrike::wait(float seconds) {
    sinceStep_ = 0;
    if (phase_ == Phase::Flying && (waited_ += real(seconds)) > config_.maxWait)
        cancel();
}
void WebStrike::spent(int hand) {
    if (hand >= 0 && hand < 2)
        armed_[static_cast<size_t>(hand)] = false;
}
void WebStrike::hangOn(bool grounded, float seconds) {
    if (grounded || phase_ == Phase::Flying || hung_ >= config_.maxHang)
        return;
    if (phase_ == Phase::Idle)
        sinceStep_ = 0;
    phase_ = Phase::Hanging;
    hangLeft_ = std::max(config_.hang, std::isfinite(seconds) ? seconds : 0.f);
}
Vec3 WebStrike::stop(Vec3 feet, const FightTarget& t) {
    // On the player's side of him; from straight above or below, the side
    // the player last came from.
    const Vec3 away{feet.x - t.feet.x, 0, feet.z - t.feet.z};
    if (length(away) > .05f)
        away_ = normalized(away);
    const float off = config_.standOff + std::max(0.f, t.radius - .3f);
    return {t.feet.x + away_.x * off, t.feet.y + t.height * .5f - config_.middle, t.feet.z + away_.z * off};
}
std::optional<WebStrike::Opening> WebStrike::open(float seconds, const std::array<FightFist, 2>& fists, Vec3 head,
                                                  Vec3 feet, std::span<const FightTarget> targets,
                                                  const WorldQueries& world, const StrikeWish& wish) {
    const float dt = real(seconds);
    sinceEnd_ += dt;
    // The body takes its steps while a strike has it: without them it ends.
    if (phase_ != Phase::Idle && (sinceStep_ += dt) > config_.stall)
        cancel();
    struct Pick {
        int hand{};
        const FightTarget* target{};
        float score{}, distance{}, speed{};
        bool held{}, called{};
        Vec3 way{};
    };
    std::optional<Pick> best;
    const Vec3 chest = feet + Vec3{0, config_.chest, 0}, neck = head - Vec3{0, config_.neck, 0};
    const bool finiteBody = finite(head) && finite(feet);
    // The enemy the player has marked, and the way the flow's call asks a
    // fist to travel.
    const bool wished = wish.target != 0;
    const Vec3 calledWay = finite(wish.called) && length(wish.called) > .5f ? normalized(wish.called) : Vec3{};
    for (int i = 0; i < 2; ++i) {
        const auto& f = fists[static_cast<size_t>(i)];
        auto& freeFor = freeFor_[static_cast<size_t>(i)];
        auto& armed = armed_[static_cast<size_t>(i)];
        auto& thrownFor = thrownFor_[static_cast<size_t>(i)];
        auto& sincePress = sincePress_[static_cast<size_t>(i)];
        auto& pressed = pressed_[static_cast<size_t>(i)];
        auto& passed = passed_[static_cast<size_t>(i)];
        if (!f.tracked || !finite(f.position) || !finite(f.velocity)) {
            freeFor = thrownFor = 0;
            armed = true;
            passed = false;
            continue;
        }
        freeFor = f.free ? freeFor + dt : 0.f;
        // The web button going down, with the hysteresis a held grip has.
        const bool down = std::isfinite(f.web) && (pressed ? f.web > .3f : f.web > .6f);
        sincePress = down && !pressed ? 0.f : sincePress + dt;
        pressed = down;
        const float speed = length(f.velocity);
        if (speed < config_.rearmSpeed) {
            armed = true;
            passed = false;
        }
        const Vec3 way = speed > 0 ? f.velocity / speed : Vec3{};
        // Thrown out from the player; or, with an enemy marked, swung the way
        // the flow calls: a hook, an uppercut and a blow down go across.
        const bool outward = dot(way, normalized(f.position - neck)) >= config_.outward;
        const bool vertical =
            wish.freeDirections && verticalBlow(Punches{}.blow(way, speed), false) != CueWay::None;
        const bool swung =
            wished && (vertical || (length(calledWay) > .5f && dot(way, calledWay) >= config_.calledAim));
        const bool thrown = finiteBody && speed >= config_.minSpeed && (outward || swung);
        thrownFor = thrown ? thrownFor + dt : 0.f;
        if (!thrown || thrownFor < config_.sustain || phase_ == Phase::Flying || sinceEnd_ < config_.pause || !armed)
            continue;
        const bool held = f.holds != 0;
        if (!held && (freeFor < config_.freeSeconds || sincePress < config_.pressSeconds))
            continue;
        bool found{}, other{};
        for (const auto& t : targets) {
            if (held && t.id != f.holds)
                continue;
            const Vec3 middle = t.middle();
            const float distance = length(middle - chest);
            if (!(distance >= config_.minDistance && distance <= config_.maxDistance))
                continue;
            const float pointing = dot(way, normalized(middle - f.position));
            // The marked enemy takes a fist thrown his way, or one swung the
            // called way that does not go away from him; anyone else, like
            // the enemy the hand's own web holds, a fist thrown at him.
            const bool his = !held && wished && t.id == wish.target;
            const bool called = his && swung && pointing >= config_.calledToward;
            const bool aimed = held  ? pointing >= config_.heldAim
                               : his ? called || (outward && pointing >= config_.markedAim)
                                     : outward && pointing >= config_.aim;
            if (!aimed || !inSight(world, head, middle))
                continue;
            // With an enemy marked the strike is his alone.
            if (!held && wished && !his) {
                other = true;
                continue;
            }
            found = true;
            // Of several, the one the fist points at best; of two it points
            // at alike, the nearer.
            const float score = pointing - distance * .01f;
            if (best && score <= best->score)
                continue;
            best = Pick{i, &t, score, distance, speed, held, called, way};
        }
        if (other && !found && !passed) {
            passed = true;
            ++passedOver_;
        }
    }
    if (!best)
        return std::nullopt;
    phase_ = Phase::Flying;
    hand_ = best->hand;
    target_ = best->target->id;
    armed_[static_cast<size_t>(hand_)] = false;
    direction_ = best->way;
    fistSpeed_ = best->speed;
    elapsed_ = sinceStep_ = waited_ = 0;
    destination_ = stop(feet, *best->target);
    duration_ = std::max(config_.minSeconds, length(destination_ - feet) / config_.speed);
    return Opening{hand_, target_, best->distance, best->held, best->called};
}
WebStrike::Step WebStrike::step(float seconds, Vec3 feet, bool grounded, const FightTarget* target) {
    const float dt = real(seconds);
    sinceStep_ = 0;
    if (grounded)
        hung_ = 0;
    if (phase_ == Phase::Idle)
        return {};
    if (phase_ == Phase::Hanging) {
        hangLeft_ -= dt;
        hung_ += dt;
        if (grounded || hangLeft_ <= 0 || hung_ >= config_.maxHang) {
            phase_ = Phase::Idle;
            return {};
        }
        Step hold;
        hold.drive = true;
        return hold;
    }
    const auto end = [&](bool landed) {
        phase_ = Phase::Idle;
        sinceEnd_ = 0;
        Step s;
        s.landed = landed;
        s.missed = !landed;
        // Off the ground the player stays where the blow landed for a moment.
        if (landed && !grounded && hung_ < config_.maxHang) {
            phase_ = Phase::Hanging;
            hangLeft_ = config_.hang;
            s.drive = true;
        }
        return s;
    };
    if (!target || !finite(feet) || !finite(target->feet))
        return end(false);
    destination_ = stop(feet, *target);
    const Vec3 to = destination_ - feet;
    const float left = length(to);
    if (left <= config_.arrive)
        return end(true);
    if (elapsed_ >= duration_ + config_.grace)
        return end(length(target->middle() - (feet + Vec3{0, config_.chest, 0})) <= config_.reach);
    // There in what is left of the flight's time, a quarter faster than its
    // speed at most (he moved); the last step ends where the player stops.
    const float time = std::max({duration_ - elapsed_, dt, 1e-3f});
    elapsed_ += dt;
    Step s;
    s.drive = true;
    s.velocity = limited(to / time, config_.speed * 1.25f);
    return s;
}
float speedOf(const PunchConfig& c, float strength) {
    return c.minSpeed + std::clamp(strength, 0.f, 1.f) * (c.fullSpeed - c.minSpeed);
}

Flow::Flow(FlowConfig c) : config_(c) {
    if (!std::isfinite(c.window) || c.window <= 0 || c.finisher < 2 || !std::isfinite(c.gain) || c.gain < 0 ||
        !std::isfinite(c.most) || c.most < 1)
        throw std::invalid_argument("Invalid flow configuration");
}
void Flow::update(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    since_ += seconds;
    if (since_ > config_.window)
        count_ = 0;
}
int Flow::hit() {
    since_ = 0;
    return ++count_;
}
void Flow::reset() {
    count_ = 0;
    since_ = 0;
}
float Flow::damage(int count) const {
    return std::min(config_.most, 1 + config_.gain * static_cast<float>(std::max(count, 1) - 1));
}

Cues::Cues(CueConfig c) : config_(c) {
    if (c.from < 1 || !std::isfinite(c.aim) || c.aim <= 0 || c.aim >= 1 || !std::isfinite(c.damage) || c.damage < 1)
        throw std::invalid_argument("Invalid cue configuration");
}
Vec3 Cues::direction(CueWay way, Vec3 toTarget) {
    if (way == CueWay::Up)
        return {0, 1, 0};
    if (way == CueWay::Down)
        return {0, -1, 0};
    Vec3 level{toTarget.x, 0, toTarget.z};
    if (way == CueWay::None || !finite(level) || length(level) < 1e-3f)
        return {};
    // The player's right as he faces the target: cross(forward, up).
    const Vec3 right = normalized(cross(level, {0, 1, 0}));
    return way == CueWay::Right ? right : right * -1.f;
}
bool Cues::answers(Vec3 direction, Vec3 toTarget) const {
    const Vec3 wanted = Cues::direction(cue_.way, toTarget);
    return finite(direction) && length(direction) > .5f && length(wanted) > .5f &&
           dot(normalized(direction), wanted) >= config_.aim;
}
bool Cues::call(int count, bool finisherNext, bool answered, std::uint32_t chance) {
    if (count < config_.from && !finisherNext) {
        cue_ = {};
        return false;
    }
    if (cue_.way != CueWay::None && !answered) {
        cue_.finisher = finisherNext;
        return false;
    }
    // One of the three ways other than the last.
    constexpr CueWay ways[] = {CueWay::Left, CueWay::Right, CueWay::Up, CueWay::Down};
    CueWay way = ways[chance % 4];
    if (way == last_)
        way = ways[(chance % 4 + 1 + (chance / 4) % 3) % 4];
    last_ = way;
    cue_ = {way, finisherNext};
    return true;
}
void Cues::clear() {
    cue_ = {};
    last_ = CueWay::None;
}

Lock::Lock(LockConfig config) : config_(config) {}
std::uint64_t Lock::update(float seconds, Vec3 head, Vec3 look, std::span<const FightTarget> enemies) {
    const float dt = real(seconds);
    since_ += dt;
    const bool sees = finite(head) && finite(look) && length(look) > .5f;
    const Vec3 line = sees ? normalized(look) : Vec3{};
    // How near the line of sight each is (a cosine): the nearest in reach,
    // and the one the mark is on.
    std::uint64_t best{};
    float bestScore = -2, bestAlong = -2, keptScore = -2, keptAlong = -2;
    bool present{};
    for (const auto& e : enemies) {
        const bool mine = target_ && e.id == target_;
        present |= mine;
        const Vec3 to = e.middle() - head;
        const float distance = length(to);
        if (!sees || !(distance >= .05f))
            continue;
        const float along = dot(to / distance, line);
        if (mine)
            keptAlong = along;
        if (distance > config_.reach || along < config_.cone)
            continue;
        const float score = along - distance * .004f;
        if (mine)
            keptScore = score;
        if (score > bestScore) {
            bestScore = score;
            bestAlong = along;
            best = e.id;
        }
    }
    // He is down or gone, or left alone: a look marks again.
    if (locked_ && (!present || since_ > config_.idle))
        locked_ = false;
    if (locked_) {
        const bool turned = best && best != target_ && bestAlong >= config_.square && keptAlong < config_.aside;
        away_ = turned ? away_ + dt : 0.f;
        if (away_ < config_.dwell)
            return target_;
        locked_ = false;
    }
    away_ = 0;
    target_ = keptScore > -2 && keptScore + config_.nearer >= bestScore ? target_ : best;
    return target_;
}
void Lock::take(std::uint64_t id) {
    if (!id)
        return;
    target_ = id;
    locked_ = true;
    since_ = away_ = 0;
}
void Lock::clear() {
    target_ = 0;
    locked_ = false;
    since_ = away_ = 0;
}

CueWay verticalBlow(const PunchEvent& blow, bool targetAirborne, const DeliveryConfig& c) {
    if (blow.knockback == Knockback::PopUp)
        return CueWay::Up;
    if (blow.direction.y < -c.slamPitch && (targetAirborne || blow.strength >= .35f))
        return CueWay::Down;
    return CueWay::None;
}
Delivery deliver(const PunchEvent& blow, bool targetAirborne, int count, const Flow& flow,
                 const DeliveryConfig& c, CueWay called, float calledDamage, bool freeDirections) {
    if (freeDirections)
        called = CueWay::None;
    Delivery d;
    d.damage = blow.damage * flow.damage(count) * (called == CueWay::None ? 1.f : calledDamage);
    d.knockback = blow.knockback;
    Vec3 level{blow.direction.x, 0, blow.direction.z};
    level = length(level) > .05f ? normalized(level) : Vec3{};
    const auto fly = [&](BlowKind kind, float carry, float lift) {
        d.kind = kind;
        d.kinetic = d.fly = true;
        d.knockback = Knockback::FlyBack;
        d.velocity = level * carry + Vec3{0, lift, 0};
    };
    const CueWay vertical = freeDirections ? verticalBlow(blow, targetAirborne, c) : CueWay::None;
    if (vertical == CueWay::Up) {
        fly(targetAirborne ? BlowKind::Juggle : BlowKind::Launch, c.launchCarry, c.launchLift);
    } else if (vertical == CueWay::Down) {
        if (targetAirborne) {
            fly(BlowKind::Slam, c.slamCarry, -c.slamSpeed);
            d.damage *= c.slamDamage;
        } else {
            d.knockback = Knockback::Knockdown;
        }
    } else if (flow.finisher(count)) {
        fly(BlowKind::Finisher, c.finisherSpeed, c.finisherLift);
        d.damage *= c.finisherDamage;
    } else if (called == CueWay::Down && targetAirborne) {
        fly(BlowKind::Slam, c.slamCarry, -c.slamSpeed);
        d.damage *= c.slamDamage;
    } else if (called == CueWay::Down) {
        // Down onto a thug on his feet: floored, as below, however hard it was.
        d.knockback = Knockback::Knockdown;
    } else if (called == CueWay::Up) {
        // Up: off his feet, or higher still.
        fly(targetAirborne ? BlowKind::Juggle : BlowKind::Launch, c.launchCarry, c.launchLift);
    } else if (called != CueWay::None) {
        // A hook: he flies off to that side, as from a full blow.
        fly(BlowKind::Heavy, c.heavyFull, c.heavyLift);
    } else if (targetAirborne && blow.direction.y < -c.slamPitch) {
        fly(BlowKind::Slam, c.slamCarry, -c.slamSpeed);
        d.damage *= c.slamDamage;
    } else if (targetAirborne) {
        fly(BlowKind::Juggle, c.juggleCarry, c.juggleLift);
    } else if (blow.knockback == Knockback::PopUp) {
        fly(BlowKind::Launch, c.launchCarry, c.launchLift);
    } else if (blow.strength >= c.heavy) {
        const float t = c.heavy < 1 ? std::clamp((blow.strength - c.heavy) / (1 - c.heavy), 0.f, 1.f) : 1.f;
        fly(BlowKind::Heavy, c.heavySpeed + (c.heavyFull - c.heavySpeed) * t, c.heavyLift);
    } else if (blow.direction.y < -c.slamPitch && blow.strength >= .35f) {
        // A firm blow down onto a thug on his feet: the game stuns one it
        // knocks down (kMelee kKnockdown: a hit reaction, then BotStateStunned).
        d.knockback = Knockback::Knockdown;
    }
    return d;
}

HitStop::HitStop(const HitStopTuning& t) : tuning_(t) {
    const auto bad = [](float v) { return !std::isfinite(v) || v < 0; };
    const auto scale = [](float v) { return std::isfinite(v) && v > 0 && v <= 1; };
    if (!scale(t.scale) || !scale(t.finisherScale) || bad(t.hold) || bad(t.holdGain) || bad(t.finisherHold) ||
        bad(t.ease) || t.ease == 0)
        throw std::invalid_argument("Invalid hit stop tuning");
}
void HitStop::reset() {
    since_ = 1e9f;
    hold_ = 0;
    scale_ = 1;
}
void HitStop::hit(float strength, bool finisher) {
    const float s = std::isfinite(strength) ? std::clamp(strength, 0.f, 1.f) : 0.f;
    const float hold = finisher ? tuning_.finisherHold : tuning_.hold + tuning_.holdGain * s;
    // During another blow's stop the longer of the two goes on.
    if (since_ < hold_ && hold_ - since_ >= hold)
        return;
    since_ = 0;
    hold_ = hold;
    scale_ = finisher ? tuning_.finisherScale : tuning_.scale;
}
void HitStop::update(float seconds) {
    // In real time, a long frame's too: a stutter must not hold the stop.
    if (std::isfinite(seconds) && seconds > 0)
        since_ = std::min(since_ + seconds, 1e9f);
}
float HitStop::timeScale() const {
    if (since_ < hold_)
        return scale_;
    const float t = (since_ - hold_) / tuning_.ease;
    if (!(t < 1))
        return 1;
    // Back along an S-curve in log time, as slow motion eases.
    const float s = t * t * (3 - 2 * t);
    return std::exp(std::log(scale_) * (1 - s));
}
} // namespace spidy
