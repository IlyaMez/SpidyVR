#include "spidy/game_tracking.hpp"
#include "spidy/presentation_gate.hpp"

namespace spidy {
native_rays::Command controllerAimRays(const Input& input, uint64_t serial) {
    native_rays::Command out;
    out.serial = serial;
    out.leaseMs = 100;
    if (input.focused) {
        for (unsigned i = 0; i < 2; ++i) {
            const auto& hand = input.hands[i];
            if (!hand.tracked || !finite(hand.aim.position))
                continue;
            const auto direction = hand.aim.orientation.rotate({0, 0, -1});
            if (!finite(direction) || std::abs(length(direction) - 1) > .001f)
                continue;
            out.rays[out.count++] = {hand.aim.position, 100, direction, i};
        }
    }
    if (!out.count)
        out.leaseMs = 0;
    return out;
}
namespace {
Vec3 horizontal(Vec3 v) {
    v.y = 0;
    return normalized(v);
}
bool lens(EyeFov f) {
    return std::isfinite(f.left) && std::isfinite(f.right) && std::isfinite(f.down) && std::isfinite(f.up) &&
           f.left < 0 && f.right > 0 && f.down < 0 && f.up > 0 && f.left > -1.55f && f.right < 1.55f &&
           f.down > -1.55f && f.up < 1.55f;
}
bool handValid(const TrackedHand& h) {
    return h.valid && validTrackedPose(h.aim) && validTrackedPose(h.grip) && std::isfinite(h.trigger) &&
           std::isfinite(h.squeeze) && std::isfinite(h.stickX) && std::isfinite(h.stickY);
}
Mat4 worldPose(Pose pose) {
    const Vec3 x = pose.orientation.rotate({1, 0, 0}), y = pose.orientation.rotate({0, -1, 0}),
               z = pose.orientation.rotate({0, 0, -1}), p = pose.position;
    return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1};
}
Quat unit(Quat q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-6f && std::isfinite(n) ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}
constexpr float pi = 3.14159265f;
float wallRunHeadYaw(Quat head, float previous) {
    const Vec3 forward = head.rotate({0, 0, -1});
    // At vertical there is no reliable horizontal look direction. Keep the
    // last heading; as pitch passes vertical, choose the continuous yaw
    // rather than mistaking the flipped projection for a half turn.
    if (std::hypot(forward.x, forward.z) < .15f)
        return previous;
    float change = std::remainder(std::atan2(-forward.x, -forward.z) - previous, 2 * pi);
    if (change > pi / 2)
        change -= pi;
    else if (change < -pi / 2)
        change += pi;
    return previous + change;
}
/// The left stick as a flip's turn in the tracking space: the head goes the
// way the stick points (from where the headset looks) and down. Its length is
// how far the stick is tilted: 0 within the dead zone, 1 from stickFull.
Vec3 stickTurn(const FlipMotion::Sample& s) {
    const float x = std::isfinite(s.stickX) ? s.stickX : 0, y = std::isfinite(s.stickY) ? s.stickY : 0;
    const float amount = std::clamp((std::hypot(x, y) - FlipMotion::stickDeadZone) /
                                        (FlipMotion::stickFull - FlipMotion::stickDeadZone),
                                    0.f, 1.f);
    if (amount <= 0)
        return {};
    // Looking straight down the crown points ahead, straight up behind.
    const Vec3 look = s.head.rotate({0, 0, -1});
    Vec3 forward = horizontal(look);
    if (length(forward) < .5f)
        forward = horizontal(s.head.rotate({0, look.y < 0 ? 1.f : -1.f, 0}));
    if (length(forward) < .5f)
        forward = {0, 0, -1};
    const Vec3 right{-forward.z, 0, forward.x};
    return normalized(cross({0, 1, 0}, right * x + forward * y)) * amount;
}
// A front flip's axis: the left of where the headset looks, level.
Vec3 frontFlipAxis(const FlipMotion::Sample& s) {
    FlipMotion::Sample ahead = s;
    ahead.stickX = 0;
    ahead.stickY = 1;
    return normalized(stickTurn(ahead));
}
} // namespace
void FlipMotion::speed(float radiansPerSecond) {
    const float scale = radiansPerSecond * holdTurnSeconds / (2 * pi);
    scale_ = std::isfinite(scale) ? std::clamp(scale, .25f, 4.f) : 1.f;
}
void FlipMotion::reset() {
    const bool held = held_, fromAir = fromAir_;
    const float scale = scale_;
    *this = {};
    held_ = held;
    fromAir_ = fromAir;
    scale_ = scale;
}
void FlipMotion::toLevel(Phase phase, Vec3 way) {
    // The turn that takes the tilt back to level, the short way.
    Quat back = tilt_.conjugate();
    if (back.w < 0)
        back = {-back.x, -back.y, -back.z, -back.w};
    const Vec3 v{back.x, back.y, back.z};
    const float sine = length(v);
    float angle = 2 * std::atan2(sine, back.w);
    Vec3 axis = sine > 1e-7f ? v / sine : way;
    // A flip goes round the way it turns: from level, a whole turn.
    if (phase == Phase::flipping) {
        if (sine <= 1e-7f) {
            axis = way;
            angle = 2 * pi;
        } else if (dot(axis, way) < 0) {
            axis = -axis;
            angle = 2 * pi - angle;
        }
    }
    from_ = tilt_;
    axis_ = axis;
    total_ = left_ = angle;
    speed_ = rate_ = length(spin_);
    spin_ = {};
    phase_ = phase;
    if (left_ <= 1e-5f || length(axis) < .5f) {
        phase_ = Phase::level;
        tilt_ = {};
    }
}
Quat FlipMotion::update(const Sample& s) {
    const float dt = std::isfinite(s.seconds) ? std::clamp(s.seconds, 0.f, .1f) : 0.f;
    const float returnRate = 2 * pi / returnTurnSeconds * scale_;
    // A press is from the air or not as it starts, as AirJumpFilter decides.
    const bool press = s.jump && !held_;
    if (!s.jump)
        fromAir_ = false;
    else if (press)
        fromAir_ = s.airborne;
    held_ = s.jump;
    landed_ = s.airborne && !s.surface ? 0.f : landed_ + dt;
    const bool landed = landed_ >= landingSeconds;
    // The stick flips once it has been at rest in the air: one held into
    // the air from a running jump goes on moving the player.
    const Vec3 want = stickTurn(s);
    const bool pushed = length(want) > 0;
    if (landed)
        stickFree_ = false;
    else if (s.airborne && !s.surface && !pushed)
        stickFree_ = true;
    const bool stick = stickFree_ && pushed, heldA = s.jump && fromAir_;
    if (landed && (phase_ == Phase::holding || phase_ == Phase::flipping)) {
        toLevel(Phase::settling);
        returnRate_ = 2 * returnRate;
    }
    const bool pressA = press && fromAir_ && !landed;
    if (pressA || (stick && phase_ != Phase::holding && !landed)) {
        // From level a new flip; during one A or the stick takes it over
        // where it is, still turning until the stick says otherwise.
        if (phase_ == Phase::level)
            turned_ = 0;
        else if (phase_ != Phase::holding)
            spin_ = axis_ * rate_;
        phase_ = Phase::holding;
        tap_ = pressA;
        heldFor_ = 0;
        tapWay_ = {};
    }
    if (phase_ == Phase::holding) {
        // A let go while the stick turns the player is no tap.
        if (stick && !heldA)
            tap_ = false;
        if (!heldA && !stick) {
            if (tap_ && heldFor_ < tapSeconds) {
                // A tap: a whole flip the way the stick pointed, else the
                // way it was turning, else ahead.
                Vec3 way = tapWay_;
                if (length(way) < .5f)
                    way = length(spin_) > 1e-3f ? normalized(spin_) : frontFlipAxis(s);
                toLevel(Phase::flipping, way);
            } else {
                toLevel(Phase::settling);
                returnRate_ = returnRate;
            }
        } else {
            heldFor_ += dt;
            if (pushed)
                tapWay_ = normalized(want);
            spin_ += (want * (2 * pi / holdTurnSeconds * scale_) - spin_) * (1 - std::exp(-dt / spinUpSeconds));
            const float speed = length(spin_);
            if (speed > 1e-6f) {
                tilt_ = unit(tilt_ * Quat::around(spin_ / speed, speed * dt));
                turned_ += speed * dt;
            }
        }
    }
    if (phase_ == Phase::flipping || phase_ == Phase::settling) {
        float speed = returnRate_;
        if (phase_ == Phase::flipping) {
            speed_ += (2 * pi / tapTurnSeconds * scale_ - speed_) * (1 - std::exp(-dt / spinUpSeconds));
            speed = speed_;
        }
        rate_ = speed * std::clamp(left_ / easeAngle, slowest, 1.f);
        const float step = std::min(left_, rate_ * dt);
        left_ -= step;
        turned_ += step;
        if (left_ <= 1e-5f) {
            phase_ = Phase::level;
            tilt_ = {};
        } else {
            tilt_ = unit(from_ * Quat::around(axis_, total_ - left_));
        }
    }
    return tilt_;
}
Input trackedSwingInput(const XrFrame& f, const Rig& rig, bool triggerWebs) {
    Input input;
    input.focused = f.focused && f.valid && validTrackedPose(f.head);
    if (!input.focused)
        return input;
    input.jump = f.jump;
    input.trackingYaw = rig.yaw;
    input.tilt = rig.tilt;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& hand = f.hands[i];
        // The web button and the reel: the grip and the trigger, or swapped.
        const float web = triggerWebs ? hand.trigger : hand.squeeze;
        const float reel = triggerWebs ? hand.squeeze : hand.trigger;
        // The hand aims from its web shooter where it points (webShooter),
        // not along the controller's aim pose.
        if (handValid(hand))
            input.hands[i] = {rig.toWorld(webShooter(hand.grip, i)), hand.grip.position - f.head.position,
                              true, std::clamp(reel, 0.f, 1.f), std::clamp(web, 0.f, 1.f)};
    }
    const auto& move = f.hands[0];
    if (handValid(move) && std::hypot(move.stickX, move.stickY) > .2f) {
        // Ahead of the player as they stand, a flip's tilt aside.
        const auto forward = horizontal(Quat::yaw(rig.yaw).rotate(f.head.orientation.rotate({0, 0, -1})));
        input.move = limited(cross(forward, {0, 1, 0}) * move.stickX + forward * move.stickY, 1);
    }
    return input;
}
void GameTrackingRig::reset() {
    const float snap = snap_, smooth = smooth_, wallDistance = wallDistance_;
    const bool flips = flips_, triggerWebs = triggerWebs_, standOnWalls = standOnWalls_;
    const bool handClimb = handClimb_;
    const FlipMotion flip = flip_;
    *this = {};
    snap_ = snap;
    smooth_ = smooth;
    flips_ = flips;
    triggerWebs_ = triggerWebs;
    standOnWalls_ = standOnWalls;
    handClimb_ = handClimb;
    wallDistance_ = wallDistance;
    flip_ = flip;
    flip_.reset();
}
GameMotionFrame GameTrackingRig::update(const XrFrame& f, Vec3 feet, Vec3 gameForward, bool gameplay,
                                        Vec3 surfaceUp, bool airborne, const SurfaceHold& surface,
                                        Vec3 gameUp) {
    GameMotionFrame out;
    out.swing.focused = false;
    pendingRecenter_ |= f.recentered;
    // The game camera as it looks, for the crawl's stick (below); level for
    // the rest. Looking straight up or down a shaft it has no level look, and
    // the last one stands: it only aligned the tracking space at the start
    // and lays the stick along the ground.
    const Vec3 cameraForward = gameForward, cameraUp = gameUp;
    gameForward = horizontal(gameForward);
    const bool cameraLevel = length(gameForward) > .9f;
    if (cameraLevel)
        lastGameForward_ = gameForward;
    else
        gameForward = lastGameForward_;
    out.inactive = (gameplay ? 0u : notPlaying) | (f.focused && f.valid ? 0u : unfocused) |
                   (validTrackedPose(f.head) ? 0u : noHead) | (finite(feet) ? 0u : noFeet) |
                   (cameraLevel || initialized_ ? 0u : noCamera) |
                   (std::isfinite(f.seconds) && f.seconds > 0 && f.seconds <= .1f ? 0u : badFrameTime) |
                   (f.predictedDisplayTime > lastTime_ ? 0u : staleDisplay);
    if (!validTrackedPose(f.eyes[0].pose) || !validTrackedPose(f.eyes[1].pose) || !lens(f.eyes[0].fov) ||
        !lens(f.eyes[1].fov))
        out.inactive |= noEyes;
    if (out.inactive) {
        wallRoll_.suspend();
        wasActive_ = false;
        // Menus and scenes find the player level, and so does a break as
        // long as blipSeconds. A frame or a few that play could not place -
        // a stutter, a display time the headset repeated, the game's camera
        // as the crawl turns the hero onto a shaft's wall - keep the view
        // standing where it stood: levelled there and turned back onto the
        // wall a moment later, it swung a quarter turn and back within a
        // fifth of a second, twice in one vent (October 14, 19:57).
        const auto since = f.predictedDisplayTime - lastTime_;
        const bool brief =
            gameplay && lastTime_ && since < static_cast<std::int64_t>(blipSeconds * 1'000'000'000.f);
        if (!brief) {
            flip_.reset();
            rig_.tilt = {};
            surfaceTurn_ = {};
            surfaceShift_ = {};
            standing_ = false;
            // Where the surfaces were is another place after a load or a scene.
            clearance_.reset();
            climbNormal_ = {};
            wallRunning_ = false;
        }
        return out;
    }
    const bool resumed = !wasActive_;
    // A break longer than a blip puts the head back over the feet, as at the
    // start: a walk in the room meanwhile - out of the headset's play area and
    // back, its picture held - is no lean of the body's. Kept, it left the eyes
    // as far from the body as he had walked, through whatever stood there ("i
    // left my playspace and came back in ... then desyncs", October 15).
    const bool rested =
        resumed && lastTime_ &&
        f.predictedDisplayTime - lastTime_ > static_cast<std::int64_t>(blipSeconds * 1'000'000'000.f);
    // A stutter (a game frame over 100 ms closes the gameplay gate) keeps the
    // webs; after a longer break each hand squeezes again.
    const bool longBreak =
        resumed && (!lastTime_ || f.predictedDisplayTime - lastTime_ >
                                      static_cast<std::int64_t>(controlHoldMs) * 1'000'000);
    if (!initialized_) {
        const auto headForward = horizontal(f.head.orientation.rotate({0, 0, -1}));
        const float trackedYaw = length(headForward) > .9f ? std::atan2(-headForward.x, -headForward.z) : 0;
        rig_.yaw = std::atan2(-gameForward.x, -gameForward.z) - trackedYaw;
        // Stage Y remains physical standing height above the player's feet.
        rig_.origin = feet - Quat::yaw(rig_.yaw).rotate({f.head.position.x, 0, f.head.position.z});
        initialized_ = true;
    } else {
        const Vec3 travel = feet - lastFeet_;
        rig_.origin += travel;
        lastHead_.position += travel;
        if (rested) {
            // After a break: over the feet again, facing as before.
            rig_.origin = feet - Quat::yaw(rig_.yaw).rotate({f.head.position.x, 0, f.head.position.z});
            flip_.reset();
            rig_.tilt = {};
        } else if (pendingRecenter_) {
            wallRoll_.reset();
            climbNormal_ = {};
            wallRunning_ = false;
            // A recenter ends a flip: level, the head kept where it was.
            flip_.reset();
            rig_.tilt = {};
            surfaceTurn_ = {};
            surfaceShift_ = {};
            rig_.preserveHead(lastHead_, f.head);
        }
    }
    const float turn = handValid(f.hands[1]) ? f.hands[1].stickX : 0;
    // A stick held into play turns nothing until it is let go: out of a menu,
    // or through a stutter after a snap it already made. A smooth turn goes
    // on through a stutter.
    if (resumed)
        turnHeld_ = std::abs(turn) > .3f && (smooth_ <= 0 || longBreak);
    if (smooth_ > 0) {
        // Past the dead zone, as fast as the stick is tilted; full speed
        // from 0.9.
        const float tilt = std::clamp((std::abs(turn) - .2f) / .7f, 0.f, 1.f);
        if (tilt > 0 && !turnHeld_)
            rig_.turn(std::copysign(smooth_ * tilt * f.seconds, -turn), f.head.position);
    } else if (std::abs(turn) > .7f && !turnHeld_ && snap_ > 0) {
        rig_.turn(turn > 0 ? -snap_ : snap_, f.head.position);
        turnHeld_ = true;
    }
    if (std::abs(turn) < .3f)
        turnHeld_ = false;
    // On a wall the head stands off it level, under a ceiling straight down.
    // On a wall the game's actor rocks up to 14 degrees about the wall's
    // normal and snaps back, several times a second (in the game, October 8);
    // a stand-off along its up bobbed the head with it.
    const Vec3 normal = normalized(surfaceUp);
    const bool onSurface = finite(normal) && length(normal) > .5f &&
                           normal.y < (onSurface_ ? surfaceLeaveCos : surfaceEnterCos);
    const Vec3 away = normal.y < -surfaceEnterCos ? Vec3{0, -1, 0} : normalized(Vec3{normal.x, 0, normal.z});
    // The wall or ceiling that holds the player: no flips there, and A is a
    // jump off it.
    const Vec3 held = normalized(surface.normal);
    const bool holds = finite(held) && length(held) > .5f && finite(surface.anchor);
    // A flip tilts the tracking space about the head where it began.
    FlipMotion::Sample flip;
    flip.jump = f.jump;
    flip.airborne = airborne && !holds;
    flip.surface = onSurface || holds;
    if (handValid(f.hands[0])) {
        flip.stickX = f.hands[0].stickX;
        flip.stickY = f.hands[0].stickY;
    }
    flip.head = f.head.orientation;
    flip.seconds = f.seconds;
    if (flip_.level())
        rig_.pivot = f.head.position;
    if (flips_) {
        rig_.tilt = flip_.update(flip);
    } else {
        flip_.reset();
        rig_.tilt = {};
    }
    // Standing on the surface: the tracking space turns onto it about the
    // eyes, each frame the short way from where its up is, so a corner's
    // next face turns it on from the wall it is on; back level, what is left
    // is a turn about the vertical, which the tracking space keeps.
    const bool walks = handValid(f.hands[0]) && std::hypot(f.hands[0].stickX, f.hands[0].stickY) > .2f;
    // Crossing the stick's centre to reverse must not restart the camera
    // transition. A sustained release still returns to the close climb view.
    runIdleSeconds_ = walks ? 0.f : std::min(.1f, runIdleSeconds_ + f.seconds);
    // A hold may also describe ground or a walkable slope. Only wall/ceiling
    // contact may align the camera when the left stick starts moving.
    const bool runSurface = holds && held.y < surfaceEnterCos;
    const bool wallRun = handClimb_ && runSurface && handValid(f.hands[0]) && !f.jump &&
                         (walks || (wallRunning_ && runIdleSeconds_ < .1f));
    if (wallRun && !wallRunning_) {
        // Capture the direction visible BEFORE the wall-floor transition
        // resets the roll. Holding up from upside down must keep going down.
        const Quat runView = length(climbNormal_) > .5f ? climbView_ : rig_.toWorld(f.head).orientation;
        runNormal_ = held;
        // Set only the entry heading. The existing up-to-wall turn below
        // keeps its original axis, speed and placement path after this.
        const Vec3 look = runView.rotate({0, 0, -1}), top = runView.rotate({0, 1, 0});
        const Vec3 along = look - held * dot(look, held);
        const Vec3 ahead = normalized(length(along) > .4f ? along : top - held * dot(top, held));
        runAhead_ = ahead;
        runHeadYaw_ = wallRunHeadYaw(f.head.orientation, 0);
        const Vec3 side = normalized(cross(ahead, held));
        // Backing away keeps the forward-facing view, including on re-entry.
        // Only forward/sideways starts align the view with the stick.
        const Vec3 travel = f.hands[0].stickY < 0 ? ahead :
            normalized(ahead * f.hands[0].stickY + side * f.hands[0].stickX);
        const Quat floor = arc(surfaceTurn_.rotate({0, 1, 0}), held) * surfaceTurn_;
        const Vec3 level = horizontal(floor.conjugate().rotate(travel));
        const Vec3 tracked = horizontal(f.head.orientation.rotate({0, 0, -1}));
        if (length(level) > .5f && length(tracked) > .5f) {
            const float yaw = std::atan2(-level.x, -level.z) - std::atan2(-tracked.x, -tracked.z);
            rig_.turn(std::remainder(yaw - rig_.yaw, 2 * pi), f.head.position);
        }
        runYaw_ = rig_.yaw;
    }
    if (wallRun) {
        // Head yaw steers the held stick along the wall; pitch cannot switch
        // its forward direction. Keep corner transport and explicit turns.
        const float headYaw = wallRunHeadYaw(f.head.orientation, runHeadYaw_);
        runAhead_ = arc(runNormal_, held).rotate(runAhead_);
        runAhead_ = Quat::around(held, rig_.yaw - runYaw_ + headYaw - runHeadYaw_).rotate(runAhead_);
        runAhead_ = normalized(runAhead_ - held * dot(runAhead_, held));
        runHeadYaw_ = headYaw;
        runNormal_ = held;
        runYaw_ = rig_.yaw;
        wallRoll_.reset();
    }
    wallRunning_ = wallRun;
    if (handClimb_ && holds && std::abs(held.y) < .5f) {
        const Vec3 face = normalized(Vec3{held.x, 0, held.z});
        if (!wallRun && length(climbNormal_) > .5f) {
            // Carry the climbing view around a building's side without an
            // instantaneous quarter-turn or the wall-floor standing view.
            const float remaining = std::atan2(cross(climbNormal_, face).y, dot(climbNormal_, face));
            const float step = std::clamp(remaining, -4.f * f.seconds, 4.f * f.seconds);
            rig_.turn(step, f.head.position);
            climbNormal_ = Quat::yaw(step).rotate(climbNormal_);
        } else {
            climbNormal_ = face;
        }
    } else {
        climbNormal_ = {};
    }
    if (!holds || !standOnWalls_ || (handClimb_ && !wallRun))
        standing_ = false;
    else if (handClimb_ || surface.crawl ||
             (std::isfinite(surface.speed) && surface.speed < (walks ? wallStrideSpeed : wallStandSpeed)))
        standing_ = true;
    {
        const Quat rest = arc(surfaceTurn_.rotate({0, 1, 0}), standing_ ? held : Vec3{0, 1, 0});
        const float left = 2 * std::atan2(length({rest.x, rest.y, rest.z}), rest.w);
        const Vec3 shift = standing_ ? surface.anchor - feet : Vec3{};
        if (left > 1e-4f) {
            // About the eyes, not the feet: the world turns round the player
            // where he looks from, and his eyes go straight to where they
            // stand on the new floor, as much of the way as the turn goes
            // each frame, so the two end together. Turned about the feet,
            // the eyes swung round an arc as high as they stand - two and a
            // half metres in a sixth of a second, over the player's head.
            const float take = std::min(left, wallTurnRate * f.seconds);
            const Vec3 eyes = rig_.toWorld(f.head).position + standOff_ + headDrop_ - feet;
            const Vec3 from = surfaceShift_ + surfaceTurn_.rotate(eyes);
            const Vec3 to = shift + (rest * surfaceTurn_).rotate(eyes);
            surfaceTurn_ = unit(Quat::around({rest.x, rest.y, rest.z}, take) * surfaceTurn_);
            surfaceShift_ = from + (to - from) * (take / left) - surfaceTurn_.rotate(eyes);
        } else {
            surfaceShift_ += (shift - surfaceShift_) * (1 - std::exp(-f.seconds / surfaceGlideSeconds));
            if (!standing_ && (surfaceTurn_.x != 0 || surfaceTurn_.y != 0 || surfaceTurn_.z != 0)) {
                const float heading = 2 * std::atan2(surfaceTurn_.y, surfaceTurn_.w);
                rig_.origin = feet + Quat::yaw(heading).rotate(rig_.origin - feet);
                rig_.yaw += heading;
                surfaceTurn_ = {};
            }
            if (!standing_ && length(surfaceShift_) < 1e-3f)
                surfaceShift_ = {};
        }
    }
    const Vec3 unplacedHead = rig_.toWorld(f.head).position;
    Vec3 target{};
    if (handClimb_ && !standing_ && holds && std::abs(held.y) < .5f) {
        // Keep the horizon upright and crouch beside the facade, hands within
        // touching distance. This shifts the tracking space, never the collider.
        const Vec3 alongUp = normalized(Vec3{0, 1, 0} - held * held.y);
        const float lower = std::clamp(f.head.position.y - .9f, 0.f, .7f);
        target = held * (wallDistance_ - dot(unplacedHead - surface.anchor, held)) - alongUp * lower;
        surfaceDepth_ = 0;
    } else if (onSurface && !standing_) {
        const float height = dot(unplacedHead - feet, away);
        surfaceSeconds_ = onSurface_ ? surfaceSeconds_ + f.seconds : 0.f;
        const float wanted =
            (surfaceSeconds_ < surfaceSettleSeconds ? wallClearance : minWallClearance) - height;
        surfaceDepth_ = std::max(onSurface_ ? surfaceDepth_ : 0.f, wanted);
        target = away * surfaceDepth_;
        out.surfaceHeight = height;
    } else {
        surfaceDepth_ = 0;
    }
    onSurface_ = onSurface;
    standOff_ += (target - standOff_) * (1 - std::exp(-f.seconds / standOffSeconds));
    out.onSurface = onSurface;
    out.standOff = standOff_;
    if (onSurface)
        out.surfaceClearance = dot(unplacedHead + standOff_ - feet, away);
    // The head never goes through what is over the player: inside a vent or a
    // duct one placed at his own eye height over the floor is out through the
    // roof. Worked out here in the level frame, where up is the world's; the
    // surface turn below carries it onto a wall's own normal, so a player
    // standing on a wall is held off its roof by the same rule.
    Vec3 drop{};
    if (headroom_ > 0) {
        const float height = dot(unplacedHead + standOff_ - feet, {0, 1, 0});
        const float room = std::max(headroom_ - headClearance, leastHeadroom);
        if (height > room)
            drop.y = room - height;
    }
    headDrop_ += (drop - headDrop_) * (1 - std::exp(-f.seconds / standOffSeconds));
    // Back to the player's own head once there is nothing left of it: an
    // exponential never quite arrives, and two millimetres is not a drop.
    if (drop.y == 0 && std::abs(headDrop_.y) < .002f)
        headDrop_ = {};
    out.headDrop = -headDrop_.y;
    Rig placed{rig_.origin + standOff_ + headDrop_, rig_.yaw, rig_.tilt, rig_.pivot};
    const bool turned = surfaceTurn_.x != 0 || surfaceTurn_.y != 0 || surfaceTurn_.z != 0;
    if (turned || length(surfaceShift_) > 0) {
        // The level placement, turned about the feet and moved onto the
        // surface, as one tracking space (its tilt about its origin).
        const Quat yaw = Quat::yaw(rig_.yaw);
        const Vec3 pivot = rig_.pivot - rig_.tilt.rotate(rig_.pivot);
        placed.origin = feet + surfaceShift_ + surfaceTurn_.rotate(placed.origin - feet + yaw.rotate(pivot));
        placed.tilt = unit(yaw.conjugate() * surfaceTurn_ * yaw * rig_.tilt);
        placed.pivot = {};
    }
    if (!holds || f.jump)
        wallRoll_.reset();
    const bool rollAllowed = handClimb_ && holds && !standing_ && !walks && !f.jump && std::abs(held.y) < .5f;
    const float rollAngle = wallRoll_.update(rollAllowed, f);
    if (rollAllowed) {
        const Pose before = placed.toWorld(f.head);
        const Quat roll = Quat::around(length(climbNormal_) > .5f ? -climbNormal_ : -held, rollAngle);
        const Quat yaw = Quat::yaw(placed.yaw);
        placed.tilt = unit(yaw.conjugate() * roll * yaw * placed.tilt);
        placed.origin += before.position - placed.toWorld(f.head).position;
        out.wallRoll = rollAngle;
        out.wallRotate = wallRoll_.engaged();
    }
    // The eyes out of what the world is built of (HeadClearance), and the
    // hands, the webs and the body with them: they stay where the eyes see
    // them.
    if (finite(bodyCentre_)) {
        const bool closeClimb = handClimb_ && holds && !standing_ && std::abs(held.y) < .5f;
        out.headPush = clearance_.update(f.seconds, bodyCentre_, placed.toWorld(f.head).position, closeClimb);
        placed.origin += out.headPush;
    }
    out.standing = standing_;
    out.viewTilt = std::acos(std::clamp(surfaceTurn_.rotate({0, 1, 0}).y, -1.f, 1.f));
    out.standUp = surfaceTurn_.rotate({0, 1, 0});
    out.floor = surfaceShift_;
    out.flipTilt = rig_.tilt;
    out.active = true;
    out.releaseWebs = longBreak || pendingRecenter_ || buttonsChanged_;
    pendingRecenter_ = buttonsChanged_ = false;
    out.predictedDisplayTime = f.predictedDisplayTime;
    out.anchor = feet;
    out.swing = trackedSwingInput(f, placed, triggerWebs_);
    // The left stick turns the flip: the player does not walk or drift.
    if (flip_.steering())
        out.swing.move = {};
    // The game takes the stick as the player stands, level; the swing takes
    // it as they see it: on a wall they stand on, along that wall.
    const Vec3 levelMove = out.swing.move;
    if (turned)
        out.swing.move = surfaceTurn_.rotate(levelMove);
    if (wallRun) {
        const Vec3 side = normalized(cross(runAhead_, held));
        out.swing.move =
            walks ? limited(runAhead_ * f.hands[0].stickY + side * f.hands[0].stickX, 1) : Vec3{};
    }
    if (out.releaseWebs)
        for (auto& hand : out.swing.hands)
            hand.tracked = false;
    const auto head = placed.toWorld(f.head);
    out.head = worldPose(head);
    out.headPose = head;
    if (handClimb_ && holds && !wallRun)
        climbView_ = head.orientation;
    for (unsigned i = 0; i < 2; ++i) {
        out.eyes[i] = worldPose(placed.toWorld(f.eyes[i].pose));
        out.fovs[i] = f.eyes[i].fov;
        if (handValid(f.hands[i])) {
            // The mesh's fingers point along -Z. A controller's grip pose is
            // angled along its handle; its aim pose supplies the pointing axis.
            Pose hand{f.hands[i].grip.position, f.hands[i].aim.orientation};
            out.hands[i] = placed.toWorld(hand);
            out.grips[i] = placed.toWorld(f.hands[i].grip);
        }
    }
    // Existing native input bridge uses these bits for W/A/S/D/Space, the
    // virtual Xbox controller the stick itself. Rotate head-relative movement
    // into the stock camera's horizontal axes.
    //
    // Horizontal holds while the player stands on the ground: his intent is
    // level and so are the axes. On the game's own crawl neither is, and the
    // axes are the crawl's own: it takes its stick along its camera's look
    // laid onto the surface - or, where the camera faces the surface too
    // squarely for that (crawlLook), along the camera's up laid onto it - and
    // the surface's own right of that. Measured in the game on a building's
    // face with its camera turned seven ways (October 15): 24 degrees off on
    // average. The level axes this replaces (the camera's level look laid
    // onto the wall) were 42 off, and turned a push right round, 85 to 161
    // degrees, wherever the camera stood beside the player on the wall - as
    // it does in a vent's narrow shaft, where the player of October 14 pushed
    // up it and crawled sideways and down (half his pushes there under a
    // metre a second, against the crawl's 2.6). His intent goes onto the
    // surface as the view stands him on it, or as it would if it did.
    Vec3 ahead = gameForward, side = cross(gameForward, {0, 1, 0});
    Vec3 move = levelMove;
    if (surface.crawl && holds && finite(cameraForward) && finite(cameraUp)) {
        const Vec3 look = cameraForward - held * dot(cameraForward, held);
        const Vec3 top = cameraUp - held * dot(cameraUp, held);
        const Vec3 along = length(look) > crawlLook ? look : top;
        if (length(along) > 1e-3f) {
            ahead = normalized(along);
            side = cross(ahead, held);
            const Vec3 meant = (standing_ ? surfaceTurn_ : arc({0, 1, 0}, held)).rotate(levelMove);
            move = meant - held * dot(meant, held);
        }
    }
    const float forward = dot(move, ahead), right = dot(move, side);
    out.walkRight = right;
    out.walkForward = forward;
    out.walkForwardAxis = ahead;
    out.walkRightAxis = side;
    if (forward > .3f)
        out.nativeKeys |= 1u << 0;
    if (right < -.3f)
        out.nativeKeys |= 1u << 1;
    if (forward < -.3f)
        out.nativeKeys |= 1u << 2;
    if (right > .3f)
        out.nativeKeys |= 1u << 3;
    if (f.jump)
        out.nativeKeys |= 1u << 4;
    // Kept from the feet: a recenter keeps the head where it was, stand-off
    // apart, and its heading as it stands, a flip's tilt aside.
    lastHead_ = {unplacedHead, Quat::yaw(rig_.yaw) * f.head.orientation};
    lastFeet_ = feet;
    lastTime_ = f.predictedDisplayTime;
    wasActive_ = true;
    return out;
}
} // namespace spidy
