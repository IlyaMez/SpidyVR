#pragma once
// A few presses on Spidy's virtual pad that walk the game's own menus for the player: the first-time setup
// opens the game's Settings the way a player does, as A on SETTINGS (game_menu::beginSetup). The game drops a
// press that comes while a screen is still moving in, so the run waits a moment first, and each press is
// held long enough for a frame of the menu to see it and let go before the next.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace spidy {
class MenuPresses {
  public:
    static constexpr std::size_t most = 8;
    static constexpr float holdSeconds = .12f, gapSeconds = .3f;
    // The buttons (game_pad's bits), one press each, after `waitSeconds`.
    void start(std::initializer_list<uint16_t> buttons, float waitSeconds) {
        count_ = std::min(buttons.size(), most);
        std::copy_n(buttons.begin(), count_, buttons_.begin());
        next_ = 0;
        clock_ = -std::max(waitSeconds, 0.f);
    }
    void stop() {
        count_ = next_ = 0;
    }
    // One frame: the buttons to hold now.
    uint16_t update(float seconds) {
        if (next_ >= count_)
            return 0;
        clock_ += std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : .1f;
        if (clock_ < 0)
            return 0;
        if (clock_ < holdSeconds)
            return buttons_[next_];
        if (clock_ >= holdSeconds + gapSeconds) {
            ++next_;
            clock_ = 0;
        }
        return 0;
    }
    bool active() const {
        return next_ < count_;
    }

  private:
    std::array<uint16_t, most> buttons_{};
    std::size_t count_{}, next_{};
    float clock_{};
};
} // namespace spidy
