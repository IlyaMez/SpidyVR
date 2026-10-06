#pragma once
#include "tracking.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

// A virtual Xbox controller for the game's menus, cutscene prompts and hint
// cards, played with the VR controllers, and for native walking and jumping
// in VR gameplay. The game reads controllers through
// XInput (it loads XInput1_4.dll itself); Spidy serves controller 0 there,
// merged with a real controller in that slot.
namespace spidy::game_pad {
// XINPUT_GAMEPAD's layout and button bits.
struct State {
    uint16_t buttons{};
    uint8_t leftTrigger{}, rightTrigger{};
    int16_t thumbLX{}, thumbLY{}, thumbRX{}, thumbRY{};
    bool operator==(const State&) const = default;
};
static_assert(sizeof(State) == 12);
enum Button : uint16_t {
    dpadUp = 0x1,
    dpadDown = 0x2,
    dpadLeft = 0x4,
    dpadRight = 0x8,
    start = 0x10,
    back = 0x20,
    leftThumb = 0x40,
    rightThumb = 0x80,
    leftShoulder = 0x100,
    rightShoulder = 0x200,
    a = 0x1000,
    b = 0x2000,
    x = 0x4000,
    y = 0x8000,
};
enum class Mapping {
    none,
    // VR gameplay: the controllers swing, walk and jump. The menu button
    // (Start: pause) and Y (Back: map, suits, skills) reach the game as
    // buttons; walking and jumping reach it as the swing leaves them (Walk).
    gameplay,
    // The game screen: every control as on an Xbox controller. A, B, X and Y
    // sit where Xbox has them; grips are the bumpers.
    menus,
};
// Native walking and jumping in VR gameplay: the left stick in the game
// camera's horizontal axes (-1..1) and the jump, both as the swing leaves
// them to the game. Once this controller has been pressed (the VR menus are
// played with it), the game plays the player with it and ignores the
// keyboard, the input bridge's W/A/S/D/Space included: on October 6 those
// moved the player 0 m while this controller's A jumped him 2.9 m.
struct Walk {
    float right{}, forward{};
    bool jump{};
};
inline State fromControllers(const XrFrame& frame, Mapping mapping, const Walk& walk = {}) {
    State s;
    if (mapping == Mapping::none)
        return s;
    // Quest sticks rest a few percent off centre; the game adds its own dead zone.
    const auto stick = [](float v) {
        v = std::isfinite(v) ? std::clamp(v, -1.f, 1.f) : 0;
        return static_cast<int16_t>(std::abs(v) < .1f ? 0 : std::lround(v * 32767));
    };
    if (frame.buttons & buttonMenu)
        s.buttons |= start;
    if (mapping == Mapping::gameplay) {
        if (frame.buttons & buttonY)
            s.buttons |= back;
        if (walk.jump)
            s.buttons |= a;
        s.thumbLX = stick(walk.right);
        s.thumbLY = stick(walk.forward);
        return s;
    }
    const auto trigger = [](float v) {
        return static_cast<uint8_t>(std::isfinite(v) ? std::lround(std::clamp(v, 0.f, 1.f) * 255) : 0);
    };
    const auto& left = frame.hands[0];
    const auto& right = frame.hands[1];
    s.thumbLX = stick(left.stickX);
    s.thumbLY = stick(left.stickY);
    s.thumbRX = stick(right.stickX);
    s.thumbRY = stick(right.stickY);
    s.leftTrigger = trigger(left.trigger);
    s.rightTrigger = trigger(right.trigger);
    const std::pair<bool, Button> pressed[] = {
        {(frame.buttons & buttonA) != 0, a}, {(frame.buttons & buttonB) != 0, b},
        {(frame.buttons & buttonX) != 0, x}, {(frame.buttons & buttonY) != 0, y},
        {left.squeeze > .6f, leftShoulder},  {right.squeeze > .6f, rightShoulder},
        {left.stickClick, leftThumb},        {right.stickClick, rightThumb}};
    for (const auto& [on, button] : pressed)
        if (on)
            s.buttons |= button;
    return s;
}

// Hooks XInputGetState and XInputGetCapabilities in the game's XInput DLL.
// 0 when hooked; 8001 while the game has not loaded XInput yet (try again).
uint32_t install();
// The game reads `state` from controller 0 until `leaseMs` runs out, then a
// controller at rest. From the first submit on, controller 0 stays
// connected, so the game never reports it unplugged between menus.
void submit(const State& state, uint32_t leaseMs);
void uninstall();
struct Telemetry {
    // The game's reads of controller 0 while Spidy served it, and how many of
    // them had a button, trigger or stick in use.
    uint64_t reads{}, active{};
    uint32_t installed{}, buttons{};
};
Telemetry telemetry();
} // namespace spidy::game_pad
