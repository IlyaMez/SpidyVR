#include "spidy/vr_settings_canvas.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace spidy::vr_settings {
namespace {
struct Rgb {
    int r, g, b;
};
// The launcher's palette, so the headset's settings look like Spidy's own.
constexpr Rgb background{13, 15, 20}, panel{21, 25, 34}, panelHigh{30, 35, 48}, border{38, 44, 59},
    text{233, 236, 242}, muted{139, 147, 167}, faint{90, 98, 117}, red{227, 38, 47}, blue{59, 130, 246},
    trackOff{44, 50, 66}, trackHover{58, 66, 86}, white{255, 255, 255};
uint32_t packed(Rgb c) {
    return 0xff000000u | static_cast<uint32_t>(c.r) << 16 | static_cast<uint32_t>(c.g) << 8 |
           static_cast<uint32_t>(c.b);
}
// Antialiased shapes from their signed distance (pixels, negative inside),
// blended over what is there. Positions and sizes are in points.
struct Painter {
    uint32_t* pixels;
    int width, height;
    float scale;
    void blend(int x, int y, Rgb c, float a) {
        auto& p = pixels[static_cast<size_t>(y) * width + x];
        const auto mix = [a](uint32_t from, int to) {
            return static_cast<uint32_t>(std::lround(static_cast<float>(from) + (to - static_cast<float>(from)) * a));
        };
        p = 0xff000000u | mix(p >> 16 & 0xff, c.r) << 16 | mix(p >> 8 & 0xff, c.g) << 8 | mix(p & 0xff, c.b);
    }
    template <class Distance> void fill(Box bounds, Rgb c, float alpha, Distance distance) {
        const int left = std::max(0, static_cast<int>(std::floor(bounds.x * scale)) - 1),
                  top = std::max(0, static_cast<int>(std::floor(bounds.y * scale)) - 1),
                  right = std::min(width, static_cast<int>(std::ceil((bounds.x + bounds.w) * scale)) + 1),
                  bottom = std::min(height, static_cast<int>(std::ceil((bounds.y + bounds.h) * scale)) + 1);
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) {
                const float cover = std::clamp(.5f - distance(x + .5f, y + .5f), 0.f, 1.f) * alpha;
                if (cover > 0)
                    blend(x, y, c, cover);
            }
    }
    void roundRect(Box b, float radius, Rgb c, float alpha = 1) {
        const float hx = b.w * scale / 2, hy = b.h * scale / 2, cx = b.x * scale + hx, cy = b.y * scale + hy,
                    r = std::min({radius * scale, hx, hy});
        fill(b, c, alpha, [&](float px, float py) {
            const float qx = std::abs(px - cx) - hx + r, qy = std::abs(py - cy) - hy + r;
            return std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;
        });
    }
    void circle(float x, float y, float radius, Rgb c, float alpha = 1) {
        const float cx = x * scale, cy = y * scale, r = radius * scale;
        fill({x - radius, y - radius, 2 * radius, 2 * radius}, c, alpha,
             [&](float px, float py) { return std::hypot(px - cx, py - cy) - r; });
    }
    void ring(float x, float y, float radius, float thickness, Rgb c, float alpha = 1) {
        const float cx = x * scale, cy = y * scale, r = radius * scale, half = thickness * scale / 2;
        const float reach = radius + thickness;
        fill({x - reach, y - reach, 2 * reach, 2 * reach}, c, alpha,
             [&](float px, float py) { return std::abs(std::hypot(px - cx, py - cy) - r) - half; });
    }
    void line(float x0, float y0, float x1, float y1, float thickness, Rgb c) {
        const float ax = x0 * scale, ay = y0 * scale, bx = x1 * scale, by = y1 * scale, half = thickness * scale / 2;
        const float dx = bx - ax, dy = by - ay, length2 = std::max(dx * dx + dy * dy, 1e-6f);
        fill({std::min(x0, x1) - thickness, std::min(y0, y1) - thickness, std::abs(x1 - x0) + 2 * thickness,
              std::abs(y1 - y0) + 2 * thickness},
             c, 1, [&](float px, float py) {
                 const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / length2, 0.f, 1.f);
                 return std::hypot(px - ax - dx * t, py - ay - dy * t) - half;
             });
    }
    // A chevron pointing left (-1) or right (+1) around its centre.
    void chevron(float x, float y, int direction, Rgb c) {
        const float back = x - 3.5f * static_cast<float>(direction), tip = x + 3.5f * static_cast<float>(direction);
        line(back, y - 7, tip, y, 2.6f, c);
        line(tip, y, back, y + 7, 2.6f, c);
    }
};
std::wstring widen(const std::string& utf8) {
    std::wstring wide(utf8.size(), L'\0');
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                                          static_cast<int>(wide.size()));
    wide.resize(count > 0 ? static_cast<size_t>(count) : 0);
    return wide;
}
HFONT font(float size, int weight) {
    return CreateFontW(-std::max(1, static_cast<int>(std::lround(size))), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}
} // namespace

Canvas::~Canvas() {
    for (HFONT f : {title_, setting_, body_, value_, heading_, tab_})
        if (f)
            DeleteObject(f);
    if (dc_) {
        if (original_)
            SelectObject(dc_, original_);
        DeleteDC(dc_);
    }
    if (bitmap_)
        DeleteObject(bitmap_);
}
void Canvas::resize(unsigned width, unsigned height) {
    if (bitmap_ && width == width_ && height == height_)
        return;
    if (!dc_) {
        dc_ = CreateCompatibleDC(nullptr);
        if (!dc_)
            throw std::runtime_error("Settings panel: no GDI device context");
        SetBkMode(dc_, TRANSPARENT);
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height); // top row first
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    HBITMAP next = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!next || !bits)
        throw std::runtime_error("Settings panel: no GDI bitmap");
    const HGDIOBJ previous = SelectObject(dc_, next);
    if (!original_)
        original_ = previous;
    if (bitmap_)
        DeleteObject(bitmap_);
    bitmap_ = next;
    bits_ = static_cast<uint32_t*>(bits);
    width_ = width;
    height_ = height;
}
void Canvas::fonts(float scale) {
    if (title_ && scale == fontScale_)
        return;
    for (HFONT* f : {&title_, &setting_, &body_, &value_, &heading_, &tab_})
        if (*f) {
            DeleteObject(*f);
            *f = nullptr;
        }
    title_ = font(24 * scale, FW_SEMIBOLD);
    setting_ = font(19 * scale, FW_SEMIBOLD);
    body_ = font(14 * scale, FW_NORMAL);
    value_ = font(17 * scale, FW_SEMIBOLD);
    heading_ = font(13 * scale, FW_BOLD);
    tab_ = font(17 * scale, FW_SEMIBOLD);
    if (!title_ || !setting_ || !body_ || !value_ || !heading_ || !tab_)
        throw std::runtime_error("Settings panel: no fonts");
    fontScale_ = scale;
}
void Canvas::draw(const Panel::Look& look, const Values& values, float scale) {
    if (!std::isfinite(scale) || scale < .25f || scale > 4)
        throw std::runtime_error("Settings panel: bad scale");
    const float wide = look.open ? panelPoints[0] : tabPoints[0], high = look.open ? panelPoints[1] : tabPoints[1];
    resize(static_cast<unsigned>(std::lround(wide * scale)), static_cast<unsigned>(std::lround(high * scale)));
    fonts(scale);
    Painter paint{bits_, static_cast<int>(width_), static_cast<int>(height_), scale};
    const auto hovered = [&](Item item, int step = 0) {
        for (const auto& h : look.hover)
            if (h.item == item && (!step || h.step == step))
                return true;
        return false;
    };
    // Shapes first; GDI then writes text over them, the cursors go on top.
    std::fill(bits_, bits_ + static_cast<size_t>(width_) * height_, packed(border));
    struct Label {
        std::string words;
        Box box;
        HFONT font;
        Rgb colour;
        UINT format;
        float spacing;
    };
    std::vector<Label> labels;
    constexpr UINT single = DT_SINGLELINE | DT_VCENTER;
    if (!look.open) {
        const bool lit = hovered(Item::tab);
        paint.roundRect({2, 2, wide - 4, high - 4}, 0, lit ? panelHigh : panel);
        paint.roundRect({0, 0, 6, high}, 0, red);
        paint.chevron(wide - 30, high / 2, 1, lit ? text : muted);
        labels.push_back({"VR SETTINGS", {26, 0, wide - 70, high}, tab_, text, single, 1.5f});
    } else {
        paint.roundRect({2, 2, wide - 4, high - 4}, 0, panel);
        paint.roundRect({0, 0, wide, 6}, 0, red);
        labels.push_back({"VR SETTINGS", {28, 22, 400, 34}, title_, text, single, 1.5f});
        labels.push_back({"Point a controller here and pull the trigger.", {28, 56, 440, 22}, body_, muted, single, 0});
        const Box close = closeBox();
        const float cx = close.x + close.w / 2, cy = close.y + close.h / 2;
        const bool closing = hovered(Item::close);
        paint.circle(cx, cy, 20, closing ? trackHover : panelHigh);
        paint.line(cx - 6, cy - 6, cx + 6, cy + 6, 2.4f, closing ? text : muted);
        paint.line(cx - 6, cy + 6, cx + 6, cy - 6, 2.4f, closing ? text : muted);
        bool firstHeading = true;
        for (const auto& line : lines()) {
            const Box& b = line.box;
            if (line.item == Item::none) {
                if (!firstHeading)
                    paint.roundRect({28, b.y, wide - 56, 1}, 0, border);
                firstHeading = false;
                labels.push_back({line.title, {28, b.y + 10, 300, 22}, heading_, faint, single, 2});
                continue;
            }
            const Box c = controlBox(line);
            if (hovered(line.item))
                paint.roundRect({8, b.y + 3, wide - 16, b.h - 6}, 10, panelHigh);
            labels.push_back({line.title, {28, b.y + 9, c.x - 40, 28}, setting_, text, single, 0});
            labels.push_back({line.help, {28, b.y + 37, c.x - 40, 22}, body_, muted, single | DT_END_ELLIPSIS, 0});
            if (!line.stepper) {
                const bool on = line.item == Item::aimMarkers ? values.aimMarkers
                                : line.item == Item::webGrab  ? values.webGrab
                                : line.item == Item::airWebs  ? values.airWebs
                                : line.item == Item::body     ? values.body
                                                              : values.punch;
                paint.roundRect(c, c.h / 2, on ? blue : hovered(line.item) ? trackHover : trackOff);
                paint.circle(on ? c.x + c.w - c.h / 2 : c.x + c.h / 2, c.y + c.h / 2, c.h / 2 - 4, white);
                continue;
            }
            paint.roundRect(c, 10, background);
            for (const int step : {-1, 1}) {
                const Box arrow{step < 0 ? c.x : c.x + c.w - 40, c.y, 40, c.h};
                const bool possible = canStep(line.item, values, step), lit = possible && hovered(line.item, step);
                if (lit)
                    paint.roundRect(arrow, 10, trackHover);
                paint.chevron(arrow.x + 20, arrow.y + 20, step, !possible ? border : lit ? white : text);
            }
            labels.push_back({valueText(line.item, values), {c.x + 40, c.y, c.w - 80, c.h}, value_, text,
                              single | DT_CENTER, 0});
        }
        const Box footer = footerBox();
        paint.roundRect({28, footer.y - 16, wide - 56, 1}, 0, border);
        labels.push_back({"Changes apply at once. Spidy Launcher starts your next session with them.", footer,
                          body_, muted, DT_WORDBREAK, 0});
    }
    GdiFlush();
    for (const auto& label : labels) {
        const auto words = widen(label.words);
        RECT r{static_cast<LONG>(std::lround(label.box.x * scale)), static_cast<LONG>(std::lround(label.box.y * scale)),
               static_cast<LONG>(std::lround((label.box.x + label.box.w) * scale)),
               static_cast<LONG>(std::lround((label.box.y + label.box.h) * scale))};
        SelectObject(dc_, label.font);
        SetTextColor(dc_, RGB(label.colour.r, label.colour.g, label.colour.b));
        SetTextCharacterExtra(dc_, static_cast<int>(std::lround(label.spacing * scale)));
        DrawTextW(dc_, words.c_str(), static_cast<int>(words.size()), &r, label.format | DT_NOPREFIX);
    }
    SetTextCharacterExtra(dc_, 0);
    GdiFlush();
    // Each hand's pointer: a ring with a dark halo, filled while its press is held.
    for (unsigned i = 0; i < 2; ++i) {
        if (!look.cursor[i])
            continue;
        const float x = look.x[i], y = look.y[i];
        paint.circle(x, y, 12, background, .55f);
        paint.ring(x, y, 7.5f, 3, white);
        if (look.held[i])
            paint.circle(x, y, 5, blue);
    }
}
} // namespace spidy::vr_settings
