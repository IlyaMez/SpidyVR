#include "spidy/realistic_flip.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace spidy;
void check(bool ok, const char* reason) {
    if (!ok) {
        std::cerr << reason << '\n';
        std::exit(1);
    }
}
float distance(Quat a, Quat b) {
    const Quat q = a * b.conjugate();
    return 2 * std::atan2(length({q.x, q.y, q.z}), std::abs(q.w));
}
XrFrame hands() {
    XrFrame f;
    f.valid = f.focused = true;
    f.seconds = 1.f / 90;
    f.head.position = {0, 1.7f, 0};
    for (unsigned i = 0; i < 2; ++i) {
        f.hands[i].valid = true;
        f.hands[i].grip.position = {i == 0 ? -.3f : .3f, 1.3f, -.5f};
    }
    return f;
}
int main() {
    RealisticFlip m;
    RealisticFlipSettings s;
    RealisticFlight f{{0, 0, -15}, {1, 0, 0}, true, true};
    check(m.update(f, true, false, 0, .01f, s).w == 1, "default off");
    s.enabled = true;
    s.strength = 100;
    auto q = m.update(f, true, false, 0, .01f, s);
    check(distance(q, {}) > 0 && distance(q, {}) < .001f, "gentle acceleration at attach");
    for (int i = 0; i < 3000; ++i) {
        f.pull = {std::sin(i * .01f), std::cos(i * .01f), 0};
        const auto old = q;
        const auto w = m.angularVelocity();
        q = m.update(f, true, false, 0, 1.f / 90, s);
        check(distance(q, old) < .047f, "rotation speed cap");
        check(length(m.angularVelocity() - w) < .095f, "angular acceleration cap");
        const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        check(std::isfinite(n) && std::abs(n - 1) < .0001f, "quaternion stability");
    }
    // Reversing flight direction with an upright web must not force a 180 yaw.
    m.reset();
    f = {{0, 0, 15}, {0, 1, 0}, true, true};
    for (int i = 0; i < 100; ++i)
        m.update(f, true, false, 0, 1.f / 90, s);
    check(distance(m.orientation(), {}) < .0001f, "no velocity-driven forced yaw");
    // A gesture adds continuous momentum, not a one-turn operation.
    auto gesture = [&](float amount, int falloff) {
        RealisticFlip a;
        auto cfg = s;
        cfg.falloffMs = falloff;
        RealisticFlight sample{{}, {}, true, false};
        float total = 0;
        Quat previous{};
        for (int i = 0; i < 1350; ++i) {
            sample.handMomentum = i < 30 ? Vec3{amount, 0, 0} : Vec3{};
            auto next = a.update(sample, true, false, 0, 1.f / 90, cfg);
            total += distance(next, previous);
            previous = next;
        }
        return total;
    };
    const float weak = gesture(.3f, 3000), strong = gesture(2.f, 3000), damped = gesture(2.f, 500);
    check(strong > 6.283f, "strong gesture must be able to pass a full 360 without a flip trigger");
    check(strong > weak * 1.5f && damped < strong * .6f, "gesture strength and drag determine total angle");
    check(std::abs(weak / 6.2831853f - std::round(weak / 6.2831853f)) > .03f,
          "no automatic whole-turn quantization");
    // Spin continues below the flight-speed gate, and opposite gestures brake it.
    m.reset();
    f = {{}, {}, true, false, {2, 0, 0}};
    for (int i = 0; i < 30; ++i)
        m.update(f, true, false, 0, 1.f / 90, s);
    const float rotating = m.angularVelocity().x;
    check(rotating > .2f, "hand gesture works at zero translational speed");
    f.handMomentum = {-2, 0, 0};
    for (int i = 0; i < 15; ++i)
        m.update(f, true, false, 0, 1.f / 90, s);
    check(m.angularVelocity().x < rotating, "opposite gesture brakes momentum");
    // Sample loss holds briefly, then recovers smoothly, never snaps upright.
    f.valid = false;
    q = m.orientation();
    for (int i = 0; i < 10; ++i)
        m.update(f, true, false, 0, 1.f / 90, s);
    check(distance(q, m.orientation()) < .00001f, "brief sample loss preserves orientation");
    f.valid = true;
    f.handMomentum = {};
    for (int i = 0; i < 900; ++i)
        m.update(f, false, true, 0, 1.f / 90, s);
    check(length(m.orientation().rotate({0, 1, 0}) - Vec3{0, 1, 0}) < .005f, "smooth landing recovery");
    // A tap clears spin and completes a smooth upright recovery after release.
    m.reset();
    f = {{}, {}, true, false, {2, 0, 0}};
    for (int i = 0; i < 30; ++i)
        m.update(f, true, false, 0, 1.f / 90, s);
    const auto beforeBrake = m.orientation();
    RealisticControls brake;
    brake.brake = true;
    auto first = m.update(f, true, false, 0, 1.f / 90, s, brake);
    check(distance(first, beforeBrake) < .06f, "upright recovery must not snap");
    f.handMomentum = {};
    for (int i = 0; i < 240; ++i)
        m.update(f, true, false, 0, 1.f / 90, s);
    check(length(m.orientation().rotate({0, 1, 0}) - Vec3{0, 1, 0}) < .001f,
          "tap finishes upright after release");
    check(length(m.angularVelocity()) < .0001f, "upright recovery removes old momentum");
    const auto heldAngle = m.orientation();
    f.handMomentum = {2, 0, 0};
    for (int i = 0; i < 90; ++i)
        m.update(f, true, false, 0, 1.f / 90, s, brake);
    check(distance(heldAngle, m.orientation()) < .001f, "held brake rejects continued arm momentum");
    f.handMomentum = {};
    RealisticControls stick;
    stick.manualActive = true;
    stick.manualAngular = {-2, 0, 0};
    for (int i = 0; i < 45; ++i)
        m.update(f, true, false, 0, 1.f / 90, s, stick);
    check(distance(heldAngle, m.orientation()) > .2f, "manual stick can steer the momentum orientation");
    stick.brake = true;
    for (int i = 0; i < 240; ++i)
        m.update(f, true, false, 0, 1.f / 90, s, stick);
    check(length(m.angularVelocity()) < .0001f, "brake has priority over manual steering");
    // Physical input is read before virtual movement and excludes common translation.
    AutomaticHandMotion input;
    auto frame = hands();
    input.update(frame, 100);
    for (int i = 0; i < 20; ++i) {
        frame.head.position += Vec3{.02f, .01f, -.1f};
        for (auto& h : frame.hands)
            h.grip.position += Vec3{.02f, .01f, -.1f};
        check(length(input.update(frame, 100)) < .0001f, "locomotion cannot create arm momentum");
    }
    input.reset();
    frame = hands();
    input.update(frame, 100);
    Vec3 impulse{};
    for (int i = 0; i < 12; ++i) {
        for (auto& h : frame.hands)
            h.grip.position.y += .035f;
        impulse = input.update(frame, 100);
    }
    check(impulse.x > .1f, "two upward arms produce pitch momentum");
    input.reset();
    frame = hands();
    input.update(frame, 100);
    for (auto& h : frame.hands)
        h.grip.position.y += 5;
    check(length(input.update(frame, 100)) == 0, "tracking teleport is not a gesture");
    // Same sustained input for the same elapsed time at two frame rates.
    RealisticFlip a, b;
    f = {{}, {}, true, false, {1, 0, 0}};
    for (int i = 0; i < 90; ++i)
        a.update(f, true, false, 0, 1.f / 90, s);
    for (int i = 0; i < 180; ++i)
        b.update(f, true, false, 0, 1.f / 180, s);
    check(distance(a.orientation(), b.orientation()) < .04f, "frame-rate consistency");
    std::cout << "Momentum rotation: attach easing, rate/acceleration caps, stability, free heading, gesture "
                 "strength, full 360, no preset turn, drag, braking, sample loss, landing, common "
                 "translation, upward arms, tracking jumps, frame rate passed\n";
    std::cout << "Simulated travel: weak=" << weak << ", strong=" << strong << ", damped=" << damped
              << " radians\n";
}
