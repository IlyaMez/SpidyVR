#pragma once
#include "swing.hpp"

namespace spidy {
// A stroke is actual tracked motion with that palm attached to the wall.
// Reaching back with a free hand or an artificial body turn is not a stroke.
class CrawlHandSteps {
  public:
    void update(const Input& in, unsigned contacts, Vec3 normal, float seconds) {
        for (unsigned i = 0; i < 2; ++i) {
            const auto& hand = in.hands[i];
            auto& h = hands_[i];
            const bool valid = in.focused && (contacts & (1u << i)) && hand.tracked &&
                finite(hand.gripRelativeToHead) && std::isfinite(hand.wallGrip) && hand.wallGrip > .35f &&
                std::isfinite(seconds) && seconds > 0 && seconds <= .1f;
            if (!valid) {
                h = {};
                continue;
            }
            if (h.seen) {
                Vec3 pull = trackingTurn(in).rotate(h.before - hand.gripRelativeToHead);
                pull -= normal * dot(pull, normal);
                const float distance = length(pull);
                if (distance / seconds > .08f && distance < .15f) {
                    const Vec3 direction = normalized(pull);
                    if (length(h.direction) > .5f && dot(direction, h.direction) < -.2f) {
                        h.pulling = false;
                        h.travel = 0;
                    }
                    h.direction = direction;
                    h.travel += distance;
                    h.quiet = 0;
                    if (!h.pulling && h.travel >= .025f) {
                        ++strokes_[i];
                        h.pulling = true;
                    }
                } else {
                    h.quiet += seconds;
                    if (h.quiet > .09f) {
                        h.travel = 0;
                        h.pulling = false;
                    }
                }
            }
            h.before = hand.gripRelativeToHead;
            h.seen = true;
        }
    }
    const std::array<uint32_t, 2>& strokes() const { return strokes_; }
  private:
    struct Hand {
        bool seen{}, pulling{};
        Vec3 before{}, direction{};
        float travel{}, quiet{};
    };
    std::array<Hand, 2> hands_{};
    std::array<uint32_t, 2> strokes_{};
};
} // namespace spidy
