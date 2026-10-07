#include "spidy/vr_settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>

namespace spidy::vr_settings {
namespace {
constexpr float margin = 28, headerHeight = 92, headingHeight = 36, rowHeight = 66, footerHeight = 80;
std::array<Line, 13> layout() {
    struct Entry {
        Item item;
        const char* title;
        const char* help;
        bool stepper;
    };
    // The help lines fit beside their controls at the canvas's 14-point body text.
    const Entry entries[] = {
        {Item::none, "WEBS", nullptr, false},
        {Item::aimMarkers, "Aim markers", "Where each web would land. X also switches them.", false},
        {Item::webGrab, "Webs catch props and thugs", "Grab, yank and throw them with your webs.", false},
        {Item::airWebs, "Webs hold in open air", "With nothing in reach, a web still holds 100 m out.", false},
        {Item::webShooter, "Web shooter", "A free hand's trigger shoots web balls at thugs.", false},
        {Item::swingSpeed, "Swing speed limit", "How fast a swing can carry you.", true},
        {Item::none, "BODY", nullptr, false},
        {Item::body, "Your own body", "Spider-Man's body and hands; off draws gloves.", false},
        {Item::punch, "Punch thugs", "A fist that hits hard enough knocks one back.", false},
        {Item::none, "COMFORT", nullptr, false},
        {Item::snapTurn, "Snap turn", "Turn per flick of the right stick.", true},
        {Item::haptics, "Controller vibration", "How strongly webs and punches buzz.", true},
        {Item::screenSize, "Game screen size", "For menus, cutscenes and flat mode.", true},
    };
    std::array<Line, 13> out{};
    float y = headerHeight;
    for (size_t i = 0; i < out.size(); ++i) {
        const auto& e = entries[i];
        const float h = e.item == Item::none ? headingHeight : rowHeight;
        out[i] = {e.item, e.title, e.help, e.stepper, {0, y, panelPoints[0], h}};
        y += h;
    }
    return out;
}
// The next step above (direction +1) or below (-1) a value, or the value
// when there is none.
float stepIn(std::span<const int> steps, float value, int direction) {
    if (direction > 0) {
        for (const int s : steps)
            if (static_cast<float>(s) > value + .01f)
                return static_cast<float>(s);
    } else if (direction < 0) {
        for (auto s = steps.rbegin(); s != steps.rend(); ++s)
            if (static_cast<float>(*s) < value - .01f)
                return static_cast<float>(*s);
    }
    return value;
}
constexpr int screenSizes[] = {0, 1, 2};
// A stepper's value after a step, and the value now.
std::pair<float, float> stepped(Item item, const Values& v, int step) {
    switch (item) {
    case Item::swingSpeed:
        return {stepIn(swingSpeeds, v.swingSpeed, step), v.swingSpeed};
    case Item::snapTurn:
        return {stepIn(snapTurns, static_cast<float>(v.snapTurn), step), static_cast<float>(v.snapTurn)};
    case Item::haptics:
        return {stepIn(hapticLevels, static_cast<float>(v.haptics), step), static_cast<float>(v.haptics)};
    case Item::screenSize:
        return {stepIn(screenSizes, static_cast<float>(v.screenSize), step), static_cast<float>(v.screenSize)};
    default:
        return {0.f, 0.f};
    }
}
} // namespace

Values sanitized(Values v) {
    v.swingSpeed = std::isfinite(v.swingSpeed) ? std::clamp(v.swingSpeed, 1.f, 65.f) : 32.f;
    v.snapTurn = std::clamp(v.snapTurn, 0, 90);
    v.haptics = std::clamp(v.haptics, 0, 100);
    v.screenSize = std::clamp(v.screenSize, 0, 2);
    return v;
}
const std::array<Line, 13>& lines() {
    static const auto all = layout();
    return all;
}
Box closeBox() {
    return {panelPoints[0] - margin - 46, 22, 48, 48};
}
Box footerBox() {
    const float top = panelPoints[1] - footerHeight;
    return {margin, top + 16, panelPoints[0] - 2 * margin, footerHeight - 24};
}
Box controlBox(const Line& line) {
    const auto& b = line.box;
    return line.stepper ? Box{b.x + b.w - margin - 184, b.y + 13, 184, 40}
                        : Box{b.x + b.w - margin - 58, b.y + 17, 58, 32};
}
Hit hit(bool open, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y))
        return {};
    if (!open)
        return Box{0, 0, tabPoints[0], tabPoints[1]}.contains(x, y) ? Hit{Item::tab} : Hit{};
    if (closeBox().contains(x, y))
        return {Item::close};
    for (const auto& line : lines()) {
        if (line.item == Item::none || !line.box.contains(x, y))
            continue;
        if (!line.stepper)
            return {line.item};
        // A stepper's halves reach a little past it and over its line's height.
        const auto c = controlBox(line);
        if (!Box{c.x - 8, line.box.y, c.w + 16, line.box.h}.contains(x, y))
            return {};
        return {line.item, x < c.x + c.w / 2 ? -1 : 1};
    }
    return {};
}
bool canStep(Item item, const Values& v, int step) {
    const auto [next, now] = stepped(item, v, step);
    return next != now;
}
std::string valueText(Item item, const Values& v) {
    char text[32]{};
    switch (item) {
    case Item::swingSpeed:
        std::snprintf(text, sizeof(text), "%ld m/s", std::lround(v.swingSpeed));
        break;
    case Item::snapTurn:
        if (v.snapTurn <= 0)
            return "Off";
        std::snprintf(text, sizeof(text), "%d\xC2\xB0", v.snapTurn);
        break;
    case Item::haptics:
        if (v.haptics <= 0)
            return "Off";
        std::snprintf(text, sizeof(text), "%d%%", v.haptics);
        break;
    case Item::screenSize:
        return v.screenSize <= 0 ? "Small" : v.screenSize == 1 ? "Medium" : "Large";
    default:
        break;
    }
    return text;
}
bool press(const Hit& h, Values& v) {
    switch (h.item) {
    case Item::aimMarkers:
        v.aimMarkers = !v.aimMarkers;
        return true;
    case Item::webGrab:
        v.webGrab = !v.webGrab;
        return true;
    case Item::airWebs:
        v.airWebs = !v.airWebs;
        return true;
    case Item::webShooter:
        v.webShooter = !v.webShooter;
        return true;
    case Item::body:
        v.body = !v.body;
        return true;
    case Item::punch:
        v.punch = !v.punch;
        return true;
    case Item::swingSpeed:
    case Item::snapTurn:
    case Item::haptics:
    case Item::screenSize: {
        const auto [next, now] = stepped(h.item, v, h.step);
        if (next == now)
            return false;
        if (h.item == Item::swingSpeed)
            v.swingSpeed = next;
        else if (h.item == Item::snapTurn)
            v.snapTurn = static_cast<int>(next);
        else if (h.item == Item::haptics)
            v.haptics = static_cast<int>(next);
        else
            v.screenSize = static_cast<int>(next);
        return true;
    }
    default:
        return false;
    }
}
Pose placement(const Pose& screen, float screenWidth, bool open) {
    // In the screen's own frame the viewer is at +z and the right edge at +x.
    // The panel starts just past that edge, square to the viewer's line of
    // sight there, so it neither leans away nor hides the screen.
    const float edge = screenWidth / 2 + .08f, turn = std::atan2(edge, screenDistance);
    const Vec3 along{std::cos(turn), 0, std::sin(turn)};
    const float high = panelMetres * panelPoints[1] / panelPoints[0];
    Vec3 centre = Vec3{edge, 0, 0} + along * (panelMetres / 2);
    if (!open) {
        const float tabHigh = tabMetres * tabPoints[1] / tabPoints[0];
        centre = Vec3{edge, high / 2 - tabHigh / 2, 0} + along * (tabMetres / 2);
    }
    return {screen.position + screen.orientation.rotate(centre), screen.orientation * Quat::yaw(-turn)};
}
bool aimAt(const Pose& aim, const Pose& quad, float width, float height, float& u, float& v) {
    const Vec3 direction = aim.orientation.rotate({0, 0, -1});
    const Vec3 normal = quad.orientation.rotate({0, 0, 1});
    const float facing = dot(direction, normal);
    // Along the face, or from behind it.
    if (!(facing < -.05f) || !finite(aim.position) || !(width > 0) || !(height > 0))
        return false;
    const float distance = dot(quad.position - aim.position, normal) / facing;
    if (!(distance > 0 && distance < 20))
        return false;
    const Vec3 local = quad.orientation.conjugate().rotate(aim.position + direction * distance - quad.position);
    u = local.x / width + .5f;
    v = .5f - local.y / height;
    return u >= 0 && u <= 1 && v >= 0 && v <= 1;
}

void Panel::shown(bool paused) {
    look_ = {};
    look_.open = paused && !closedByUser_;
    taken_[0] = taken_[1] = false;
    primed_ = false;
}
void Panel::hidden() {
    look_ = {};
    taken_[0] = taken_[1] = false;
    primed_ = false;
}
Panel::Frame Panel::update(const std::array<Pointer, 2>& pointers, const Pose& screen, float screenWidth,
                           Values& values) {
    Frame frame;
    const bool wasOpen = look_.open;
    pose_ = placement(screen, screenWidth, wasOpen);
    const float wide = wasOpen ? panelPoints[0] : tabPoints[0], high = wasOpen ? panelPoints[1] : tabPoints[1];
    for (unsigned i = 0; i < 2; ++i) {
        const auto& p = pointers[i];
        const uint32_t bit = 1u << i;
        float u{}, v{};
        const bool on = p.tracked && aimAt(p.aim, pose_, metresWide(), metresHigh(), u, v);
        const Hit at = on ? hit(wasOpen, u * wide, v * high) : Hit{};
        look_.hover[i] = at;
        look_.cursor[i] = on;
        look_.x[i] = on ? std::round(u * wide) : 0;
        look_.y[i] = on ? std::round(v * high) : 0;
        const float trigger = std::isfinite(p.trigger) ? p.trigger : 0.f;
        const bool was = down_[i];
        if (!primed_) {
            // A trigger already pulled as the screen came up is not a press.
            down_[i] = trigger > .35f;
            taken_[i] = false;
        } else {
            down_[i] = was ? trigger > .35f : trigger > .75f;
            if (down_[i] && !was) {
                taken_[i] = on;
                // A press acts on what it points at, on the shape the frame began
                // with: once one hand opened or folded the panel, the other waits.
                if (on && look_.open == wasOpen) {
                    if (at.item == Item::close) {
                        look_.open = false;
                        closedByUser_ = true;
                        frame.clicked |= bit;
                    } else if (at.item == Item::tab) {
                        look_.open = true;
                        closedByUser_ = false;
                        frame.clicked |= bit;
                    } else if (press(at, values)) {
                        frame.changed = true;
                        frame.clicked |= bit;
                    }
                }
            } else if (!down_[i]) {
                taken_[i] = false;
            }
        }
        look_.held[i] = taken_[i] && down_[i];
        if (on || look_.held[i])
            frame.pointing |= bit;
    }
    primed_ = true;
    if (look_.open != wasOpen) {
        // Opened or folded: where the hands point is found again next frame.
        pose_ = placement(screen, screenWidth, look_.open);
        for (unsigned i = 0; i < 2; ++i) {
            look_.hover[i] = {};
            look_.cursor[i] = false;
            look_.x[i] = look_.y[i] = 0;
        }
    }
    return frame;
}
} // namespace spidy::vr_settings
