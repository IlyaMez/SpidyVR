#include "spidy/phone.hpp"
#include "spidy/overlay_text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace spidy {
namespace {
constexpr float pi = 3.14159265358979f;
// Sizes in metres and colours (linear RGB), as the other panels have them.
constexpr float width = .085f, height = .17f, corner = .012f, bezel = .005f;
// In the FNSM app's colours (Spider-Man 2's phone): near-black, cards a shade
// up, one bold red for what is picked and for the bar along the top, and a
// deeper red behind the picked app's icon.
constexpr Vec3 body{.012f, .012f, .016f}, screen{.008f, .008f, .01f}, white{1, 1, 1}, soft{.55f, .55f, .6f},
    dim{.055f, .055f, .065f}, red{.85f, .04f, .05f}, deepRed{.62f, .02f, .03f};
// A circle of `segments` as each segment's start, sin and cos from the top
// clockwise, the first again at the end: worked out once.
template <int segments> const std::array<std::array<float, 2>, segments + 1>& circle() {
    static const auto points = [] {
        std::array<std::array<float, 2>, segments + 1> p{};
        for (int i = 0; i <= segments; ++i) {
            const float a = 2 * pi * static_cast<float>(i % segments) / segments;
            p[static_cast<size_t>(i)] = {std::sin(a), std::cos(a)};
        }
        return p;
    }();
    return points;
}
constexpr int discSegments = 24, ringSegments = 32;
struct Plane {
    WheelFrame f;
    float scale = 1;
    Vec3 at(float x, float y, float z = 0) const {
        return f.centre + f.right * (x * scale) + f.up * (y * scale) + f.out * z;
    }
    void triangle(std::vector<Vertex>& v, float x0, float y0, float x1, float y1, float x2, float y2, float z,
                  Vec3 color) const {
        v.insert(v.end(), {Vertex{at(x0, y0, z), color}, Vertex{at(x1, y1, z), color}, Vertex{at(x2, y2, z), color}});
    }
    void rect(std::vector<Vertex>& v, float x, float y, float w, float h, float z, Vec3 color) const {
        triangle(v, x - w / 2, y - h / 2, x + w / 2, y - h / 2, x + w / 2, y + h / 2, z, color);
        triangle(v, x - w / 2, y - h / 2, x + w / 2, y + h / 2, x - w / 2, y + h / 2, z, color);
    }
    // The disc's segments `first` up to `last`, of discSegments clockwise from the top.
    void fan(std::vector<Vertex>& v, float x, float y, float radius, float z, Vec3 color, int first = 0,
             int last = discSegments) const {
        const auto& c = circle<discSegments>();
        for (int i = first; i < last; ++i) {
            const auto& a = c[static_cast<size_t>(i)];
            const auto& b = c[static_cast<size_t>(i) + 1];
            triangle(v, x, y, x + radius * a[0], y + radius * a[1], x + radius * b[0], y + radius * b[1], z,
                     color);
        }
    }
    void disc(std::vector<Vertex>& v, float x, float y, float radius, float z, Vec3 color) const {
        fan(v, x, y, radius, z, color);
    }
    void ring(std::vector<Vertex>& v, float x, float y, float inner, float outer, float z, Vec3 color) const {
        for (const auto* p = circle<ringSegments>().data(), *end = p + ringSegments; p != end; ++p) {
            const float s0 = p[0][0], c0 = p[0][1], s1 = p[1][0], c1 = p[1][1];
            triangle(v, x + inner * s0, y + inner * c0, x + outer * s0, y + outer * c0, x + outer * s1, y + outer * c1,
                     z, color);
            triangle(v, x + inner * s0, y + inner * c0, x + outer * s1, y + outer * c1, x + inner * s1, y + inner * c1,
                     z, color);
        }
    }
    // A rectangle with round corners of radius r: three rectangles, and each
    // corner the quarter of its disc they leave (the rest of the disc lies
    // on them).
    void rounded(std::vector<Vertex>& v, float x, float y, float w, float h, float r, float z, Vec3 color) const {
        rect(v, x, y, w - 2 * r, h, z, color);
        rect(v, x - w / 2 + r / 2, y, r, h - 2 * r, z, color);
        rect(v, x + w / 2 - r / 2, y, r, h - 2 * r, z, color);
        constexpr int quarter = discSegments / 4;
        // Clockwise from the top: the top right corner's quarter first.
        const float corners[4][2]{{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
        for (int k = 0; k < 4; ++k)
            fan(v, x + corners[k][0] * (w / 2 - r), y + corners[k][1] * (h / 2 - r), r, z, color, k * quarter,
                (k + 1) * quarter);
    }
    // Text on its baseline at (x, y), shrunk to `w`: `align` 0 from x, .5
    // centred on it, 1 ending at it.
    void text(std::vector<Vertex>& v, const std::string& s, float x, float y, float h, float w, Vec3 color,
              float align = .5f) const {
        TextStyle style;
        style.height = fittedHeight(s, h * scale, w * scale);
        style.color = color;
        appendText(v, s, at(x, y, .004f) - f.right * (textWidth(s, style.height) * align), f.right, f.up, style);
    }
};
// A button's symbol, white on its disc: previous and next two triangles,
// play one, pause two bars.
void symbol(std::vector<Vertex>& v, const Plane& p, PhoneButton b, bool playing, float x, float y, Vec3 color,
            float s = .0045f) {
    constexpr float z = .0035f;
    switch (b) {
    case PhoneButton::previous:
        p.triangle(v, x, y, x + 2 * s, y + s * 1.2f, x + 2 * s, y - s * 1.2f, z, color);
        p.triangle(v, x - 2 * s, y, x, y + s * 1.2f, x, y - s * 1.2f, z, color);
        break;
    case PhoneButton::next:
        p.triangle(v, x, y, x - 2 * s, y - s * 1.2f, x - 2 * s, y + s * 1.2f, z, color);
        p.triangle(v, x + 2 * s, y, x, y - s * 1.2f, x, y + s * 1.2f, z, color);
        break;
    case PhoneButton::play:
        if (playing) {
            p.rect(v, x - s * .7f, y, s * .8f, s * 2.6f, z, color);
            p.rect(v, x + s * .7f, y, s * .8f, s * 2.6f, z, color);
        } else {
            p.triangle(v, x + s * 1.6f, y, x - s, y - s * 1.5f, x - s, y + s * 1.5f, z, color);
        }
        break;
    default:
        break;
    }
}
// A spider, its body at (x, y) about `s` across: two discs and eight legs.
void spider(std::vector<Vertex>& v, const Plane& p, float x, float y, float s, Vec3 c) {
    constexpr float z = .0035f;
    p.disc(v, x, y + s * .22f, s * .16f, z, c);
    p.disc(v, x, y - s * .08f, s * .24f, z, c);
    for (const float side : {-1.f, 1.f})
        for (int k = 0; k < 4; ++k) {
            // Each leg out from the body, then bent down or up at its knee.
            const float a = (k - 1.5f) * .55f, kx = x + side * s * .45f, ky = y + std::sin(a) * s * .35f;
            const float fx = x + side * s * .62f, fy = ky + (k < 2 ? -1 : 1) * s * .28f;
            p.triangle(v, x, y, kx, ky + s * .03f, kx, ky - s * .03f, z, c);
            p.triangle(v, kx, ky + s * .03f, kx, ky - s * .03f, fx, fy, z, c);
        }
}
// The apps' icons, about `s` across, centred on (x, y).
void noteIcon(std::vector<Vertex>& v, const Plane& p, float x, float y, float s, Vec3 c) {
    constexpr float z = .0035f;
    p.disc(v, x - s * .18f, y - s * .25f, s * .2f, z, c);
    p.rect(v, x, y + s * .05f, s * .07f, s * .6f, z, c);
    p.triangle(v, x + s * .03f, y + s * .35f, x + s * .32f, y + s * .2f, x + s * .03f, y + s * .1f, z, c);
}
// The game's menu: three bars, as a menu's sign has them.
void menuIcon(std::vector<Vertex>& v, const Plane& p, float x, float y, float s, Vec3 c) {
    constexpr float z = .0035f;
    for (const float row : {-.22f, 0.f, .22f})
        p.rounded(v, x, y + row * s, s * .62f, s * .11f, s * .05f, z, c);
}
void gearIcon(std::vector<Vertex>& v, const Plane& p, float x, float y, float s, Vec3 c, Vec3 hole) {
    constexpr float z = .0035f;
    p.disc(v, x, y, s * .3f, z, c);
    for (int k = 0; k < 8; ++k) {
        const float a = 2 * pi * k / 8;
        p.disc(v, x + std::sin(a) * s * .32f, y + std::cos(a) * s * .32f, s * .09f, z, c);
    }
    p.disc(v, x, y, s * .12f, z + .0003f, hole);
}
// A camera: its body, the bump of its viewfinder on top, the lens a ring.
void cameraIcon(std::vector<Vertex>& v, const Plane& p, float x, float y, float s, Vec3 c, Vec3 hole) {
    constexpr float z = .0035f;
    p.rounded(v, x, y - s * .05f, s * .8f, s * .52f, s * .08f, z, c);
    p.rect(v, x - s * .12f, y + s * .26f, s * .3f, s * .12f, z, c);
    p.disc(v, x, y - s * .05f, s * .17f, z + .0003f, hole);
    p.disc(v, x, y - s * .05f, s * .09f, z + .0006f, c);
}
// What the music is doing, as the screens say it.
const char* musicState(const PhoneView& view) {
    return view.failed ? "CAN'T PLAY" : view.playing ? "PLAYING" : "PAUSED";
}
// The home screen: a card for each app, the picked one red; under them what plays.
void homeScreen(std::vector<Vertex>& v, const Plane& p, const PhoneView& view) {
    const char* const names[phoneApps] = {"MUSIC", "MENU", "GEAR", "CAMERA"};
    const char* const notes[phoneApps] = {"YOUR SONGS", "MAP, SUITS, SKILLS", "GADGETS AND XP",
                                          "THE GAME'S CAMERA"};
    for (int i = 0; i < phoneApps; ++i) {
        const float y = .045f - i * .0235f;
        const bool lit = view.app == i;
        p.rounded(v, 0, y, .068f, .0195f, .004f, .002f, lit ? red : dim);
        const Vec3 ink = white, back = lit ? red : dim;
        p.rounded(v, -.0235f, y, .014f, .014f, .003f, .0025f, lit ? deepRed : Vec3{.1f, .1f, .12f});
        if (i == 0)
            noteIcon(v, p, -.0235f, y, .012f, ink);
        else if (i == phoneMenuApp)
            menuIcon(v, p, -.0235f, y, .012f, ink);
        else if (i == 2)
            gearIcon(v, p, -.0235f, y, .012f, ink, back);
        else
            cameraIcon(v, p, -.0235f, y, .012f, ink, back);
        p.text(v, names[i], -.012f, y, .0045f, .04f, white, 0);
        p.text(v, notes[i], -.012f, y - .0058f, .0025f, .04f, lit ? white : soft, 0);
    }
    if (view.tracks > 0) {
        p.rounded(v, 0, -.05f, .068f, .016f, .004f, .002f, dim);
        p.text(v, view.playing ? "NOW PLAYING" : musicState(view), -.03f, -.0465f, .0024f, .04f, red, 0);
        p.text(v, view.song, -.03f, -.0545f, .0038f, .06f, white, 0);
    }
}
// MUSIC: the cover, the song and where it is, the volume, the buttons, AUTOPLAY.
void musicScreen(std::vector<Vertex>& v, const Plane& p, const PhoneView& view) {
    p.rounded(v, 0, .037f, .034f, .034f, .004f, .002f, deepRed);
    noteIcon(v, p, 0, .037f, .026f, white);
    if (view.tracks <= 0) {
        p.text(v, "NO SONGS YET", 0, .008f, .0055f, .066f, white);
        p.text(v, "ADD SONGS TO THE MUSIC FOLDER", 0, -.002f, .003f, .066f, soft);
    } else {
        p.text(v, view.song, 0, .008f, .006f, .066f, white);
        char line[48];
        std::snprintf(line, sizeof line, "%d/%d  %s", view.track + 1, view.tracks, musicState(view));
        p.text(v, line, 0, -.0025f, .0034f, .06f, view.failed ? red : soft);
        // The volume: a bar filling to it, VOL before it.
        const float barLeft = -.019f, barWidth = .05f, barY = -.012f;
        const float filled = barWidth * std::clamp(view.volume, 0, 100) / 100.f;
        p.text(v, "VOL", -.0255f, barY - .0014f, .0028f, .012f, soft);
        p.rect(v, barLeft + barWidth / 2, barY, barWidth, .0022f, .002f, dim);
        if (filled > 0)
            p.rect(v, barLeft + filled / 2, barY, filled, .0022f, .0025f, red);
    }
    // PREVIOUS, PLAY and NEXT in a row, AUTOPLAY under them.
    const float row = -.033f, gap = .024f;
    const PhoneButton row3[] = {PhoneButton::previous, PhoneButton::play, PhoneButton::next};
    for (int i = 0; i < 3; ++i) {
        const float x = (i - 1) * gap, r = row3[i] == PhoneButton::play ? .011f : .0085f;
        const bool lit = view.lit == row3[i];
        p.disc(v, x, row, r, .002f, lit ? red : dim);
        if (lit)
            p.ring(v, x, row, r, r + .0013f, .0025f, white);
        // Inside its disc: two triangles span four times this.
        symbol(v, p, row3[i], view.playing, x, row, white, r * (row3[i] == PhoneButton::play ? .4f : .32f));
    }
    const bool litAuto = view.lit == PhoneButton::autoplay;
    p.rounded(v, 0, -.055f, .056f, .01f, .005f, .002f, litAuto ? red : dim);
    p.text(v, view.autoplay ? "AUTOPLAY ON" : "AUTOPLAY OFF", 0, -.0569f, .0038f, .046f, white);
}
// GEAR: each gadget, the equipped one marked red, its charges; the level under them.
void gearScreen(std::vector<Vertex>& v, const Plane& p, const PhoneView& view) {
    if (view.gearCount <= 0) {
        p.text(v, "NO GADGETS YET", 0, .03f, .005f, .066f, soft);
    } else {
        for (int r = 0; r < PhoneUi::gearShown; ++r) {
            const int i = view.gearTop + r;
            if (i >= view.gearCount || i >= gadgetPlaces)
                break;
            const auto& g = view.gear[i];
            const float y = .049f - r * .0125f;
            p.rounded(v, 0, y, .068f, .0105f, .003f, .002f, dim);
            if (g.equipped)
                p.rect(v, -.0315f, y, .002f, .0075f, .0025f, red);
            p.text(v, g.name, -.028f, y - .0017f, .0034f, .044f, g.equipped ? white : soft, 0);
            if (g.most > 0) {
                char count[16];
                std::snprintf(count, sizeof count, "%d/%d", g.ammo, g.most);
                p.text(v, count, .031f, y - .0017f, .0034f, .016f, white, 1);
            }
        }
        // More above or below: a small red arrow at that end.
        if (view.gearTop > 0)
            p.triangle(v, -.003f, .0565f, .003f, .0565f, 0, .0595f, .003f, red);
        if (view.gearTop + PhoneUi::gearShown < view.gearCount)
            p.triangle(v, -.003f, -.0225f, .003f, -.0225f, 0, -.0255f, .003f, red);
    }
    p.rounded(v, 0, -.046f, .068f, .03f, .004f, .002f, dim);
    p.text(v, "LEVEL", -.03f, -.038f, .003f, .03f, soft, 0);
    p.text(v, "--", -.03f, -.048f, .007f, .03f, white, 0);
    p.text(v, "XP", .005f, -.038f, .003f, .03f, soft, 0);
    p.text(v, "COMING SOON", .005f, -.047f, .0032f, .024f, red, 0);
}
// PhoneHold's controls: the buttons by their own bits (A is the frame's jump
// too; never the menu button), and from bit 12 on each hand's trigger, grip,
// stick and stick click, four a hand.
constexpr uint32_t holdButtons = buttonA | buttonB | buttonX | buttonY | buttonView | buttonDpadUp |
                                 buttonDpadDown | buttonDpadLeft | buttonDpadRight;
enum HandControl : unsigned { triggerControl, gripControl, stickControl, clickControl };
constexpr uint32_t handControl(unsigned hand, HandControl c) {
    return 1u << (12 + 4 * hand + c);
}
constexpr uint32_t holdAll = holdButtons | 0xffu << 12;
void letGo(XrFrame& f, uint32_t controls) {
    f.buttons &= ~(controls & holdButtons);
    if (controls & buttonA)
        f.jump = false;
    for (unsigned i = 0; i < 2; ++i) {
        auto& h = f.hands[i];
        if (controls & handControl(i, triggerControl))
            h.trigger = 0;
        if (controls & handControl(i, gripControl))
            h.squeeze = 0;
        if (controls & handControl(i, stickControl))
            h.stickX = h.stickY = 0;
        if (controls & handControl(i, clickControl))
            h.stickClick = false;
    }
}
// Latin-1's letters and Latin Extended-A's (U+00C0 to U+017F) as the font has
// them: each its letter without its accents; '+' two letters (latinPair), ' '
// a sign (multiplication, division).
constexpr char latinBase[] = "AAAAAA+CEEEEIIIIDNOOOOO OUUUUY++aaaaaa+ceeeeiiiidnooooo ouuuuy+y"
                             "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIi++JjKkkLlLlLlL"
                             "lLlNnNnNnnNnOoOoOo++RrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
static_assert(sizeof latinBase == 0x180 - 0xc0 + 1);
const char* latinPair(uint32_t c) {
    switch (c) {
    case 0xc6:
        return "AE";
    case 0xde:
        return "TH";
    case 0xdf:
        return "ss";
    case 0xe6:
        return "ae";
    case 0xfe:
        return "th";
    case 0x132:
        return "IJ";
    case 0x133:
        return "ij";
    case 0x152:
        return "OE";
    case 0x153:
        return "oe";
    default:
        return " ";
    }
}
// The next code point of UTF-8 `s` from `i` (and `i` past it); a byte that
// begins none, U+FFFD.
uint32_t nextCodePoint(const std::string& s, size_t& i) {
    const auto byte = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char b = byte(i++);
    if (b < 0x80)
        return b;
    const size_t more = b >= 0xc0 && b < 0xe0 ? 1 : b >= 0xe0 && b < 0xf0 ? 2 : b >= 0xf0 && b < 0xf8 ? 3 : 0;
    if (!more || i + more > s.size())
        return 0xfffd;
    uint32_t c = b & (0x3fu >> more);
    for (size_t k = 0; k < more; ++k) {
        if ((byte(i + k) & 0xc0) != 0x80)
            return 0xfffd;
        c = c << 6 | (byte(i + k) & 0x3fu);
    }
    i += more;
    return c;
}
// In front of the head (tracking space), level with the chest, upright and
// facing it: the phone of a hand not to be had (phoneFrameAt).
WheelFrame phoneFrame(Pose head) {
    Vec3 ahead = head.orientation.rotate({0, 0, -1});
    ahead.y = 0;
    ahead = length(ahead) > .1f ? normalized(ahead) : Vec3{0, 0, -1};
    const Vec3 up{0, 1, 0}, out = ahead * -1.f;
    return {head.position + ahead * .4f + Vec3{0, -.22f, 0}, cross(up, out), up, out};
}
} // namespace

PhoneControls::Event PhoneControls::update(float stickX, float stickY, bool a, bool b, float leftTrigger,
                                           float rightTrigger) {
    Event e;
    // A trigger counts as it is pulled, once, until let go.
    const auto pull = [](float t, bool& held) {
        t = std::isfinite(t) ? t : 0.f;
        const bool was = held;
        held = t > (held ? released : pulled);
        return held && !was;
    };
    const bool down = pull(leftTrigger, leftHeld_), up = pull(rightTrigger, rightHeld_);
    e.volume = (up ? 1 : 0) - (down ? 1 : 0);
    if (!std::isfinite(stickX))
        stickX = 0;
    if (!std::isfinite(stickY))
        stickY = 0;
    // One step a push, along the way it is pushed most.
    if (std::max(std::abs(stickX), std::abs(stickY)) < centred)
        stickHeld_ = false;
    if (!stickHeld_ && std::max(std::abs(stickX), std::abs(stickY)) > pushed) {
        if (std::abs(stickX) >= std::abs(stickY))
            e.x = stickX > 0 ? 1 : -1;
        else
            e.y = stickY > 0 ? 1 : -1;
        stickHeld_ = true;
    }
    e.pressed = a && !aHeld_;
    e.closed = b && !bHeld_;
    aHeld_ = a;
    bHeld_ = b;
    return e;
}

PhoneAction PhoneUi::update(const PhoneControls::Event& e, int gearRows) {
    switch (screen_) {
    case PhoneScreen::home:
        if (e.closed)
            return PhoneAction::close;
        // The apps stand in a column: up and down, or left and right, go through them.
        if (const int step = e.y ? -e.y : e.x)
            app_ = (app_ + step + phoneApps) % phoneApps;
        if (e.pressed && app_ == phoneMenuApp)
            return PhoneAction::menu;
        if (e.pressed && app_ == phoneCameraApp)
            return PhoneAction::camera;
        // MUSIC and GEAR, the apps with screens.
        if (e.pressed)
            screen_ = app_ == 0 ? PhoneScreen::music : PhoneScreen::gear;
        return PhoneAction::none;
    case PhoneScreen::music:
        if (e.closed) {
            screen_ = PhoneScreen::home;
            return PhoneAction::none;
        }
        if (e.x)
            button_ = static_cast<PhoneButton>((static_cast<int>(button_) + e.x + phoneButtons) % phoneButtons);
        if (e.y < 0)
            button_ = PhoneButton::autoplay;
        else if (e.y > 0 && button_ == PhoneButton::autoplay)
            button_ = PhoneButton::play;
        if (!e.pressed)
            return PhoneAction::none;
        switch (button_) {
        case PhoneButton::previous:
            return PhoneAction::previous;
        case PhoneButton::play:
            return PhoneAction::play;
        case PhoneButton::next:
            return PhoneAction::next;
        case PhoneButton::autoplay:
            return PhoneAction::autoplay;
        }
        return PhoneAction::none;
    case PhoneScreen::gear:
        gearTop_ = std::clamp(gearTop_ - e.y, 0, std::max(0, gearRows - gearShown));
        if (e.closed)
            screen_ = PhoneScreen::home;
        return PhoneAction::none;
    }
    return PhoneAction::none;
}

WheelFrame phoneFrameAt(Pose head, Vec3 hand) {
    if (!finite(hand) || !finite(head.position))
        return phoneFrame(head);
    const Vec3 centre = hand + Vec3{0, .07f, 0};
    const Vec3 toEyes = head.position - centre;
    if (length(toEyes) < .1f)
        return phoneFrame(head);
    const Vec3 out = normalized(toEyes);
    // Its top as near world up as facing the eyes leaves it.
    Vec3 up = Vec3{0, 1, 0} - out * out.y;
    if (length(up) < .1f)
        return phoneFrame(head);
    up = normalized(up);
    return {centre, cross(up, out), up, out};
}

void PhoneHold::update(bool live, XrFrame& frame, XrFrame& phone) {
    const auto held = [](float v) { return std::isfinite(v) && v > released; };
    uint32_t down = (frame.buttons & holdButtons) | (frame.jump ? buttonA : 0u);
    for (unsigned i = 0; i < 2; ++i) {
        const auto& h = frame.hands[i];
        if (held(h.trigger))
            down |= handControl(i, triggerControl);
        if (held(h.squeeze))
            down |= handControl(i, gripControl);
        if (held(std::hypot(h.stickX, h.stickY)))
            down |= handControl(i, stickControl);
        if (h.stickClick)
            down |= handControl(i, clickControl);
    }
    // A control held stays with whoever has it; one let go of goes to whoever
    // wants the controllers now.
    mine_ = (mine_ & down) | (live ? holdAll & ~down : 0u);
    phone = frame;
    letGo(frame, mine_);
    letGo(phone, holdAll & ~mine_);
}

Phone::Use Phone::take(XrFrame& frame, int gearRows) {
    Use use;
    XrFrame mine;
    hold_.update(live_, frame, mine);
    if (!live_)
        return use;
    // Either stick: the one pushed further.
    const auto& h = mine.hands;
    use.stick = std::hypot(h[0].stickX, h[0].stickY) > std::hypot(h[1].stickX, h[1].stickY) ? 0u : 1u;
    const bool a = mine.jump || (mine.buttons & buttonA), b = (mine.buttons & buttonB) != 0;
    const auto& stick = h[use.stick];
    use.controls = controls_.update(stick.stickX, stick.stickY, a, b, h[0].trigger, h[1].trigger);
    use.action = ui_.update(use.controls, gearRows);
    if (use.action == PhoneAction::close || use.action == PhoneAction::menu || use.action == PhoneAction::camera)
        open_ = live_ = false;
    return use;
}

bool Phone::update(float seconds, bool play, const Pose& head, const Pose grips[2], const bool tracked[2],
                   int drawHand, Vec3 drawnAt) {
    bool opened{};
    if ((drawHand == 0 || drawHand == 1) && play && !open_) {
        open_ = opened = true;
        age_ = 0;
        controls_.reset();
        ui_.reset();
        hand_ = drawHand;
        place_ = phoneFrameAt(head, drawnAt);
    }
    if (open_) {
        age_ += std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        // It stays in the hand holding it; a hand the headset loses leaves
        // it where it last was.
        if (hand_ >= 0 && tracked[hand_] && validTrackedPose(grips[hand_]) && validTrackedPose(head))
            place_ = phoneFrameAt(head, grips[hand_].position);
    }
    live_ = open_ && play;
    return opened;
}

bool PocketDraw::update(float seconds, bool play, const Pose& head, const Pose grips[2], const bool tracked[2],
                        const float squeezes[2]) {
    const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
    const bool headSeen = validTrackedPose(head);
    // The body's way: the head's level look, kept within yawDeadZone of it
    // and drifting toward it.
    Vec3 look = head.orientation.rotate({0, 0, -1});
    look.y = 0;
    float lookOff{};
    if (headSeen && length(look) > .1f) {
        const float headYaw = std::atan2(-look.x, -look.z);
        if (!yawSet_) {
            yaw_ = headYaw;
            yawSet_ = true;
        }
        float off = std::remainder(headYaw - yaw_, 2 * pi);
        if (const float excess = std::abs(off) - yawDeadZone; excess > 0)
            yaw_ = std::remainder(yaw_ + std::copysign(std::min(excess, yawSpeed * dt), off), 2 * pi);
        off = std::remainder(headYaw - yaw_, 2 * pi);
        yaw_ = std::remainder(yaw_ + off * std::min(1.f, yawDrift * dt), 2 * pi);
        lookOff = std::remainder(headYaw - yaw_, 2 * pi);
    }
    const Vec3 forward{-std::sin(yaw_), 0, -std::cos(yaw_)};
    const Vec3 right = cross(forward, {0, 1, 0});
    const Vec3 neck = head.position - head.orientation.rotate({0, neckUp, -neckAhead});
    bool out{};
    for (int i = 0; i < 2; ++i) {
        // The grip is the squeeze's alone, wherever the headset has the hand.
        const float squeeze = std::isfinite(squeezes[i]) ? squeezes[i] : 0.f;
        const bool down = squeeze > (down_[i] ? released : pressed);
        const bool press = down && !down_[i];
        down_[i] = down;
        if (tracked[i] && finite(grips[i].position)) {
            if (lost_[i] == 0 && dt > 0) {
                const Vec3 velocity = (grips[i].position - seen_[i]) * (1 / dt);
                going_[i] = going_[i] + (velocity - going_[i]) * std::min(1.f, dt / smoothing);
            } else {
                going_[i] = {};
            }
            seen_[i] = grips[i].position;
            lost_[i] = 0;
        } else {
            lost_[i] = std::min(lost_[i] + dt, lostSeconds + 1);
        }
        const unsigned bit = 1u << i;
        if (!down) {
            hands_ &= ~bit;
            if (drawing_ == i) {
                drawing_ = -1;
                ++stats_.letGo;
            }
            continue;
        }
        if (drawing_ == i && (time_ += dt) > drawSeconds) {
            drawing_ = -1;
            ++stats_.tooSlow;
            continue;
        }
        if (!play || !headSeen)
            continue;
        const bool lostLong = lost_[i] > lostSeconds;
        // The grip in the body's level axes from the neck: x right, y up, z
        // ahead; a lost one carried on its way (carrySeconds).
        const Vec3 at = lost_[i] > 0
                            ? seen_[i] + limited(going_[i], carrySpeed) * std::min(lost_[i], carrySeconds)
                            : seen_[i];
        const Vec3 n = at - neck;
        const float x = dot(n, right), y = n.y, z = dot(n, forward);
        if (press && drawing_ < 0) {
            const float above = seated_ ? pocketAboveSeated : pocketAbove;
            const Miss miss = lostLong                   ? lost
                              : -y < above               ? high
                              : -y > pocketBelow         ? low
                              : std::abs(x) < pocketSide ? central
                              : std::abs(x) > pocketOut  ? wide
                              : z > -behind              ? ahead
                                                         : misses;
            if (miss == misses) {
                drawing_ = i;
                time_ = 0;
                hands_ |= bit;
                ++stats_.presses;
                ++stats_.begun;
            } else if (lostLong || (-y >= (seated_ ? reachBelowSeated : reachBelow) && z <= reachAhead)) {
                ++stats_.presses;
                ++stats_.missed[miss];
                stats_.lastMiss = miss;
                stats_.lastHand = {x, y, z};
                stats_.lastLost = std::min(lost_[i], lostSeconds + 1);
                stats_.lastLook = lookOff;
            }
            continue;
        }
        if (lostLong)
            continue;
        // Out in front: from the eyes.
        const Vec3 d = at - head.position;
        if (drawing_ == i && lost_[i] == 0 && dot(d, forward) >= front && -d.y <= raised) {
            out = true;
            hand_ = seen_[i];
            drewWith_ = i;
            ++draws_;
            ++stats_.draws;
            drawing_ = -1; // the grip stays the draw's until let go
        }
    }
    return out;
}

std::string phoneSongName(const std::string& fileName, int track) {
    std::string name = fileName;
    if (const auto slash = name.find_last_of("\\/"); slash != std::string::npos)
        name.erase(0, slash + 1);
    if (const auto dot = name.find_last_of('.'); dot != std::string::npos && dot > 0)
        name.erase(dot);
    std::string letters;
    for (size_t i = 0; i < name.size();) {
        const uint32_t c = nextCodePoint(name, i);
        if (c >= 0x300 && c < 0x370)
            continue; // an accent of the letter before (a name in NFD, as macOS writes them)
        if (c >= 0xc0 && c < 0x180) {
            const char base = latinBase[c - 0xc0];
            letters += base == '+' ? latinPair(c) : std::string(1, base);
        } else {
            letters += c < 0x80 && c != '_' && drawable(static_cast<char>(c)) ? static_cast<char>(c) : ' ';
        }
    }
    // Runs of spaces as one, none at either end.
    std::string out;
    for (const char c : letters)
        if (c != ' ' || (!out.empty() && out.back() != ' '))
            out += c;
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    if (!out.empty())
        return out;
    return track >= 0 ? "TRACK " + std::to_string(track + 1) : "SONG";
}

namespace {
// The body's depth, and how far its rounded edges reach in from the face
// and the back; the camera's bump on the back.
constexpr float depth = .009f, edge = .0018f, bump = .0012f;
// Its parts' colours: the face's black glass, the frame's graphite, the
// back's deep red, the camera's black and its lenses' rims.
constexpr Vec3 glassColor{.006f, .006f, .008f}, frameColor{.045f, .045f, .052f}, backColor{.15f, .016f, .02f},
    cameraColor{.012f, .012f, .014f}, rimColor{.2f, .2f, .22f};
// How a part shines: its share of the light that only reflects, and how
// tight that is.
struct Finish {
    float shine, tightness;
};
constexpr Finish glassFinish{.5f, 60}, metalFinish{.35f, 24}, paintFinish{.12f, 12};
// It grows out of its middle over an eighth of a second.
float growth(const PhoneView& view) {
    return std::clamp(std::isfinite(view.age) ? view.age / .12f : 1.f, .2f, 1.f);
}
// The body in the world, lit: a point and a normal in the phone's own frame
// (x right, y up, z out of the face, metres) to a vertex.
struct Body {
    const WheelFrame& f;
    float scale;
    Vec3 viewer, light;
    Vec3 world(float x, float y, float z) const {
        return f.centre + (f.right * x + f.up * y + f.out * z) * scale;
    }
    Vertex lit(float x, float y, float z, Vec3 n, Vec3 color, Finish finish) const {
        const Vec3 at = world(x, y, z);
        const Vec3 normal = normalized(f.right * n.x + f.up * n.y + f.out * n.z);
        const Vec3 toEye = normalized(viewer - at);
        const float diffuse = std::max(0.f, dot(normal, light));
        const float glint = std::pow(std::max(0.f, dot(normal, normalized(light + toEye))), finish.tightness);
        // A rim catching the light the way a phone's edge does, seen side on.
        const float rim = std::pow(1 - std::abs(dot(normal, toEye)), 3.f) * .12f;
        const float shone = finish.shine * glint + rim;
        return {at, color * (.3f + .7f * diffuse) + Vec3{shone, shone, shone}};
    }
    void quad(std::vector<Vertex>& out, const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) const {
        out.insert(out.end(), {a, b, c, a, c, d});
    }
};
// A rounded rectangle's outline, half `hw` by `hh`, its corners of radius
// `r`, `inset` in from it: points with their outward normals, anticlockwise
// from the top right corner's start.
struct Rim {
    float x, y, nx, ny;
};
constexpr int cornerSteps = 6;
std::array<Rim, 4 * (cornerSteps + 1)> outline(float hw, float hh, float r, float inset) {
    std::array<Rim, 4 * (cornerSteps + 1)> rim{};
    const float signs[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    for (int k = 0; k < 4; ++k)
        for (int s = 0; s <= cornerSteps; ++s) {
            const float a = (static_cast<float>(k) + static_cast<float>(s) / cornerSteps) * pi / 2;
            const float nx = std::cos(a), ny = std::sin(a);
            rim[static_cast<size_t>(k * (cornerSteps + 1) + s)] = {
                signs[k][0] * (hw - r) + nx * (r - inset), signs[k][1] * (hh - r) + ny * (r - inset), nx, ny};
        }
    return rim;
}
// A rounded slab on the frame's plane: its outline `hw` by `hh` (corner
// `r`), from z0 out to z1, its sides one colour and its face toward z1
// another. `rounded`: its edges eased by `edge` toward both faces, as the
// phone's own are; else straight (the camera's bump).
void slab(std::vector<Vertex>& out, const Body& b, float cx, float cy, float hw, float hh, float r, float z0,
          float z1, Vec3 side, Finish sideFinish, Vec3 face, Finish faceFinish, bool rounded, bool backFirst) {
    // The profile from the back round to the face: in from the outline, how
    // far along z, and the normal's share along the outline's and along z.
    struct Step {
        float inset, z, across, along;
    };
    const float e = rounded ? std::min(edge, (z1 - z0) / 2) : 0.f;
    const Step steps[] = {{e, z0, 0, -1},          {e * .3f, z0 + e * .3f, .7f, -.7f}, {0, z0 + e, 1, 0},
                          {0, z1 - e, 1, 0},       {e * .3f, z1 - e * .3f, .7f, .7f},  {e, z1, 0, 1}};
    constexpr int stepCount = 6;
    std::array<std::array<Rim, 4 * (cornerSteps + 1)>, stepCount> rims;
    for (int s = 0; s < stepCount; ++s)
        rims[static_cast<size_t>(s)] = outline(hw, hh, r, steps[s].inset);
    const auto at = [&](int s, size_t i, Vec3 color, Finish finish) {
        const Rim& p = rims[static_cast<size_t>(s)][i];
        const Step& st = steps[s];
        return b.lit(cx + p.x, cy + p.y, st.z, {p.nx * st.across, p.ny * st.across, st.along}, color, finish);
    };
    const size_t n = rims[0].size();
    const auto cap = [&](int s, Vec3 color, Finish finish, float z, float facing) {
        const Vertex middle = b.lit(cx, cy, z, {0, 0, facing}, color, finish);
        for (size_t i = 0; i < n; ++i)
            out.insert(out.end(), {middle, at(s, i, color, finish), at(s, (i + 1) % n, color, finish)});
    };
    if (backFirst)
        cap(0, side, sideFinish, z0, -1);
    for (int s = 0; s + 1 < stepCount; ++s)
        for (size_t i = 0; i < n; ++i) {
            const size_t j = (i + 1) % n;
            b.quad(out, at(s, i, side, sideFinish), at(s, j, side, sideFinish), at(s + 1, j, side, sideFinish),
                   at(s + 1, i, side, sideFinish));
        }
    cap(stepCount - 1, face, faceFinish, z1, 1);
}
// A box on the frame's axes, lit on its faces: a side button.
void button(std::vector<Vertex>& out, const Body& b, float x0, float x1, float y0, float y1, float z0, float z1) {
    const float x[2] = {x0, x1}, y[2] = {y0, y1}, z[2] = {z0, z1};
    // Each face: the axis it faces along, which way, and its four corners.
    const auto face = [&](Vec3 n, int a, int c, int d, int e) {
        const auto point = [&](int k) {
            return b.lit(x[k & 1], y[(k >> 1) & 1], z[(k >> 2) & 1], n, frameColor, metalFinish);
        };
        b.quad(out, point(a), point(c), point(d), point(e));
    };
    face({-1, 0, 0}, 0, 2, 6, 4);
    face({1, 0, 0}, 1, 3, 7, 5);
    face({0, -1, 0}, 0, 1, 5, 4);
    face({0, 1, 0}, 2, 3, 7, 6);
    face({0, 0, -1}, 0, 1, 3, 2);
    face({0, 0, 1}, 4, 5, 7, 6);
}
// A disc facing the back (-z), lit: the camera's lenses.
void lensDisc(std::vector<Vertex>& out, const Body& b, float cx, float cy, float r, float z, Vec3 color, Finish f) {
    constexpr int sides = 20;
    const Vertex middle = b.lit(cx, cy, z, {0, 0, -1}, color, f);
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2 * pi * static_cast<float>(i) / sides, a1 = 2 * pi * static_cast<float>(i + 1) / sides;
        out.insert(out.end(), {middle, b.lit(cx + r * std::cos(a0), cy + r * std::sin(a0), z, {0, 0, -1}, color, f),
                               b.lit(cx + r * std::cos(a1), cy + r * std::sin(a1), z, {0, 0, -1}, color, f)});
    }
}
} // namespace

Vec3 phoneScreenGlass() {
    return glassColor;
}

void appendPhone(std::vector<Vertex>& out, const PhoneView& view, Vec3 viewer) {
    if (!view.open || !finite(view.frame.centre) || !finite(view.frame.right) || !finite(view.frame.up) ||
        !finite(view.frame.out))
        return;
    const WheelFrame& f = view.frame;
    const Vec3 eye = finite(viewer) ? viewer : f.centre + f.out;
    // Lit as from above and a little behind the player, toward the phone.
    const Vec3 light = normalized(Vec3{0, 1, 0} * .8f + normalized(eye - f.centre) * .55f);
    const Body b{f, growth(view), eye, finite(light) ? light : Vec3{0, 1, 0}};
    const float hw = width / 2, hh = height / 2, h = depth / 2;
    // The back's camera and its red first, then the body, its face last: from
    // the front the face covers them, whether or not anything sorts by depth.
    const float cx = -hw + .019f, cy = hh - .022f;
    slab(out, b, cx, cy, .012f, .012f, .0045f, -h - bump, -h + .0004f, cameraColor, metalFinish, cameraColor,
         glassFinish, false, true);
    for (const float lens : {.0045f, -.0045f}) {
        lensDisc(out, b, cx, cy + lens, .0042f, -h - bump - .0001f, rimColor, metalFinish);
        lensDisc(out, b, cx, cy + lens, .0031f, -h - bump - .0002f, cameraColor, glassFinish);
    }
    // The red of the back, a hair behind the body's own back, inside a rim
    // of its graphite.
    {
        constexpr float z = -depth / 2 - .0003f;
        const auto back = outline(hw, hh, corner, edge * 1.4f);
        const Vertex middle = b.lit(0, 0, z, {0, 0, -1}, backColor, paintFinish);
        for (size_t i = 0; i < back.size(); ++i) {
            const auto& p = back[i];
            const auto& q = back[(i + 1) % back.size()];
            out.insert(out.end(), {middle, b.lit(p.x, p.y, z, {0, 0, -1}, backColor, paintFinish),
                                   b.lit(q.x, q.y, z, {0, 0, -1}, backColor, paintFinish)});
        }
    }
    slab(out, b, 0, 0, hw, hh, corner, -h, h, frameColor, metalFinish, glassColor, glassFinish, true, true);
    // The volume's two buttons on the left side, the power button on the right.
    for (const float y : {.034f, .018f})
        button(out, b, -hw - .0008f, -hw + .0004f, y - .006f, y + .006f, -.0012f, .0012f);
    button(out, b, hw - .0004f, hw + .0008f, .018f, .034f, -.0012f, .0012f);
}

std::array<Vec3, 4> phoneScreenCorners(const PhoneView& view) {
    const WheelFrame& f = view.frame;
    const float s = growth(view), z = (depth / 2 + .0003f) * s;
    const Vec3 x = f.right * (phoneScreenHalfWidth * s), y = f.up * (phoneScreenHalfHeight * s), c = f.centre + f.out * z;
    return {c - x - y, c + x - y, c + x + y, c - x + y};
}

void placePhoneScreen(std::vector<Vertex>& picture, const PhoneView& view) {
    const WheelFrame& f = view.frame;
    const float s = growth(view), lift = (depth / 2 + .0003f) * s;
    // Its layers a hundredth of a millimetre apart, for a drawing that sorts nothing.
    for (auto& v : picture)
        v.position = f.centre + (f.right * v.position.x + f.up * v.position.y) * s +
                     f.out * (lift + v.position.z * .01f);
}

void appendPhoneScreen(std::vector<Vertex>& out, const PhoneView& view) {
    if (!view.open)
        return;
    const Plane p{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, 1};
    p.rounded(out, 0, 0, width - 2 * bezel, height - 2 * bezel, corner - bezel / 2, .001f, screen);
    // The red bar along the top, the screen's name in it; the controls' hint
    // along the bottom.
    const char* const titles[] = {"FNSM", "MUSIC", "GEAR"};
    p.rounded(out, 0, .0695f, width - 2 * bezel, .019f, corner - bezel / 2, .0015f, red);
    p.text(out, titles[std::clamp(static_cast<int>(view.screen), 0, 2)], -.031f, .0655f, .0065f, .05f, white,
           0);
    spider(out, p, .027f, .0695f, .006f, white);
    const char* hint = view.screen == PhoneScreen::home    ? "A OPEN   B PUT AWAY"
                       : view.screen == PhoneScreen::music ? "A PRESS   B HOME   TRIGGERS VOL"
                                                           : "B HOME";
    p.text(out, hint, 0, -.0755f, .0026f, .066f, soft);
    switch (view.screen) {
    case PhoneScreen::home:
        homeScreen(out, p, view);
        break;
    case PhoneScreen::music:
        musicScreen(out, p, view);
        break;
    case PhoneScreen::gear:
        gearScreen(out, p, view);
        break;
    }
}
} // namespace spidy
