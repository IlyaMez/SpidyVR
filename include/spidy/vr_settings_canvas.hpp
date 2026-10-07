#pragma once
#include "vr_settings.hpp"
#include <cstdint>
#include <windows.h>

namespace spidy::vr_settings {
// Paints the settings panel (or its tab) into display-encoded BGRA8 pixels
// for the headset: text with GDI (Segoe UI, antialiased), the switches,
// buttons and pointer cursors as antialiased shapes. One per worker thread.
class Canvas {
  public:
    Canvas() = default;
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    // Paints `look` with `values` at `scale` pixels a point. Throws when GDI
    // cannot make its bitmap or fonts.
    void draw(const Panel::Look& look, const Values& values, float scale);
    // The latest painting, rows of width() pixels top to bottom, until the next draw.
    const uint32_t* pixels() const {
        return bits_;
    }
    unsigned width() const {
        return width_;
    }
    unsigned height() const {
        return height_;
    }
    unsigned rowPitch() const {
        return width_ * 4;
    }

  private:
    void resize(unsigned width, unsigned height);
    void fonts(float scale);
    HDC dc_{};
    HBITMAP bitmap_{};
    HGDIOBJ original_{};
    uint32_t* bits_{};
    unsigned width_{}, height_{};
    // Title, setting, body, value, heading and tab text at fontScale_.
    HFONT title_{}, setting_{}, body_{}, value_{}, heading_{}, tab_{};
    float fontScale_{};
};
} // namespace spidy::vr_settings
