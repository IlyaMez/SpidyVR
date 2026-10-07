#pragma once
#include "math.hpp"
#include <array>
#include <cstdint>
#include <string>

// Spidy's VR settings, as a section beside the game's own menus in the
// headset. While the headset shows the game screen (the pause menu, the game
// menu, loading, cutscenes), a panel hangs beside that screen's right edge:
// point a controller at it and pull the trigger to change a setting at once.
// It opens by itself when the menu button paused the game from VR; otherwise
// a tab hangs there that opens it. The game keeps every control on its
// screen, except the trigger of a hand pointing at the panel.
namespace spidy::vr_settings {
// What the panel changes during play. The launcher (or the command line)
// gives a session the values it starts with; the session reports the last
// ones, and the launcher starts the next session with them.
struct Values {
    bool aimMarkers = true, webGrab = true, punch = true, body = true;
    float swingSpeed = 32; // the swing's speed limit, m/s
    int snapTurn = 30;     // degrees per flick of the right stick; 0: no snap turning
    int haptics = 100;     // controller vibration, percent
    int screenSize = 1;    // the game screen: 0 small, 1 medium, 2 large
    bool airWebs = true;   // a web that meets nothing within reach holds in open air there
    bool webShooter = true; // a free hand's trigger shoots web balls
    bool operator==(const Values&) const = default;
};
// The steps the panel offers. A value between two steps (the launcher's
// slider sets any swing speed) steps to the next one in that direction.
inline constexpr int swingSpeeds[] = {10, 15, 20, 25, 32, 40, 48, 56, 65};
inline constexpr int snapTurns[] = {0, 15, 30, 45, 60, 90};
inline constexpr int hapticLevels[] = {0, 25, 50, 75, 100};
// The game screen's width for each size, metres, at screenDistance.
inline constexpr float screenWidths[] = {2.4f, 3.2f, 4.2f};
inline float screenWidth(int size) {
    return screenWidths[size < 0 ? 0 : size > 2 ? 2 : size];
}
// The values within the session's ranges: swing speed 1-65 m/s (what
// run_game_vr.py accepts), snap turn 0-90 degrees, vibration 0-100%.
Values sanitized(Values);

enum class Item : uint8_t {
    none,
    close, // the panel's close button: it folds to its tab
    tab,   // the folded panel: it opens again
    aimMarkers,
    webGrab,
    airWebs,
    webShooter,
    swingSpeed,
    body,
    punch,
    snapTurn,
    haptics,
    screenSize,
};
// A rectangle in the panel's points (the canvas draws a point as `scale`
// pixels), from the top-left corner.
struct Box {
    float x{}, y{}, w{}, h{};
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};
// The open panel's and the tab's size in points.
inline constexpr float panelPoints[2] = {560, 940}, tabPoints[2] = {280, 64};
// One line of the panel below its header: a section's heading (item none)
// or a setting.
struct Line {
    Item item{};
    const char* title{};
    const char* help{}; // a setting's one-line explanation
    bool stepper{};     // a value with arrows to either side; else an on/off switch
    Box box{};
};
const std::array<Line, 13>& lines();
// The close button in the header, and the footer's text.
Box closeBox();
Box footerBox();
// A setting's control at the right of its line: the switch, or the stepper.
Box controlBox(const Line&);
// What a point of the panel (open) or of its tab is on. A stepper's left
// half steps down, its right half up; anywhere on a switch's line switches it.
struct Hit {
    Item item = Item::none;
    int step{}; // a stepper's half: -1 or +1
    bool operator==(const Hit&) const = default;
};
Hit hit(bool open, float x, float y);
// Whether a stepper has a step that way from the current value.
bool canStep(Item, const Values&, int step);
// A stepper's value as the panel shows it, UTF-8: "32 m/s", "30°", "Off".
std::string valueText(Item, const Values&);
// A press on a setting; true when it changed a value.
bool press(const Hit&, Values&);

// Widths in the headset, metres, and the game screen's distance from the
// viewer it was placed for (presentation_gate's screenAhead).
inline constexpr float panelMetres = .9f, tabMetres = .42f, screenDistance = 2.5f;
// Where the panel (open) or its tab hangs: beside the right edge of the game
// screen at `screen`, `screenWidth` wide, turned to face the viewer the
// screen was placed for; the panel centred on the screen's height, the tab
// level with the panel's top.
Pose placement(const Pose& screen, float screenWidth, bool open);
// Where an aim ray (along the pose's -z) meets a quad `width` x `height`
// metres (its pose at its centre, its face along +z), as 0..1 from the quad's
// top-left corner. False when it misses the quad or meets its back.
bool aimAt(const Pose& aim, const Pose& quad, float width, float height, float& u, float& v);

// A controller, as the panel sees it.
struct Pointer {
    bool tracked{};
    Pose aim{}; // its aim pose, in the space the game screen is placed in
    float trigger{};
};
class Panel {
  public:
    // What the canvas draws.
    struct Look {
        bool open{};
        Hit hover[2]{};       // what each hand points at
        bool cursor[2]{};     // the hand points at the panel (or the tab)
        float x[2]{}, y[2]{}; // where, in whole points
        bool held[2]{};       // a press the panel took is still held
        bool operator==(const Look&) const = default;
    };
    struct Frame {
        // Hands whose trigger belongs to the panel, not the game: pointing at
        // it, or holding a press it took.
        uint32_t pointing{};
        uint32_t clicked{}; // hands whose press did something (a tick in the hand)
        bool changed{};     // a value changed
    };
    // The game screen came up. paused: the menu button paused the game from
    // VR just before, and the panel opens, unless it was closed this session.
    void shown(bool paused);
    // The game screen went away: presses end, the panel folds to its tab.
    void hidden();
    // Once per headset frame while the game screen shows.
    Frame update(const std::array<Pointer, 2>&, const Pose& screen, float screenWidth, Values&);
    const Look& look() const {
        return look_;
    }
    // Where the panel or its tab hangs after the latest update, and its size.
    const Pose& pose() const {
        return pose_;
    }
    float metresWide() const {
        return look_.open ? panelMetres : tabMetres;
    }
    float metresHigh() const {
        return look_.open ? panelMetres * panelPoints[1] / panelPoints[0] : tabMetres * tabPoints[1] / tabPoints[0];
    }

  private:
    Look look_{};
    Pose pose_{};
    bool closedByUser_{}, primed_{};
    // Each trigger, pulled (with hysteresis), and whether its press began on the panel.
    bool down_[2]{}, taken_[2]{};
};
} // namespace spidy::vr_settings
