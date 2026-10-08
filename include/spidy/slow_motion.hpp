#pragma once
#include <cstdint>

// Slow motion as Blade & Sorcery has it: a click of the left thumbstick slows
// the game's time, another click ends it. Focus (the meter on the left wrist)
// drains while it lasts, in real time, ends it when it runs out, and refills
// once it has ended. Time eases in and out along an S-curve in log time, so
// the world decelerates and recovers without a jolt; the headset and the
// controllers keep real time throughout. game_time.hpp slows the game.
namespace spidy {
struct SlowMotionTuning {
    // The game's time at full slow motion, per real second.
    float scale = .3f;
    // Real seconds easing into it and back out.
    float enterSeconds = .4f, exitSeconds = .55f;
    // Real seconds a full meter lasts, and refilling takes from empty to full;
    // refilling starts this long after slow motion ends.
    float drainSeconds = 7, refillSeconds = 12, refillDelay = 1.2f;
    // A press with less focus than this is refused.
    float minimum = .15f;
};
// What the headset shows of it (an eye image's share: NativeEyeFrame).
struct SlowMotionView {
    float blend{};     // 0..1: how far into slow motion, eased (the image's recolouring)
    float focus = 1;   // 0..1: the meter
    float meter{};     // 0..1: the meter's opacity (it shows while focus is spent)
    float ripple = -1; // 0..1: a ring crossing the view after a start or an end; below 0 none
    float warning{};   // 0..1: a red flash fading after a refused press or an empty meter
    bool entering{};   // the ring follows a start (else an end)
    bool active{};     // slow motion is on (easing in or holding)
};
class SlowMotion {
  public:
    // A ring crosses the view this long after a start or an end; the meter
    // flashes red this long after a refusal or running empty; a full meter
    // stays this long after it filled or a refusal, fading in and out.
    static constexpr float rippleSeconds = .5f, warningSeconds = .6f, meterHold = 1.5f;
    static constexpr float meterIn = .15f, meterOut = .5f;
    enum class Event : std::uint8_t {
        none,
        started,     // a press started it
        stopped,     // a press ended it
        emptied,     // focus ran out
        refused,     // a press with too little focus
        interrupted, // play stopped (a menu, the game screen, the flat screen)
    };
    explicit SlowMotion(const SlowMotionTuning& tuning = {});
    // One headset frame: `seconds` of real time since the last one, `pressed`
    // the button was pressed (StickPress), `allowed` while the player plays in
    // VR. Returns what happened in it.
    Event update(float seconds, bool pressed, bool allowed);
    bool active() const {
        return active_;
    }
    // 0..1: how far into slow motion, along a smoother step.
    float blend() const;
    // The game's time per real second now: 1, down to tuning.scale.
    float timeScale() const;
    float focus() const {
        return focus_;
    }
    SlowMotionView view() const;
    const SlowMotionTuning& tuning() const {
        return tuning_;
    }
    // A full meter in real time, as a session starts.
    void reset();

  private:
    SlowMotionTuning tuning_;
    bool active_{};
    float progress_{}; // 0..1 linear in time toward active_
    float focus_ = 1, meter_{};
    // Real seconds since slow motion ended (refill delay), since the last
    // start or end, since a warning, and since the meter was last needed.
    float idle_{}, sinceChange_ = 1e9f, sinceWarning_ = 1e9f, sinceNeeded_ = 1e9f;
};
// A press of one thumbstick's click that is not half of clicking both (which
// switches between VR and the flat screen): it counts once the stick has
// been held chordSeconds without the other, or when it is let go sooner.
// While unfocused nothing counts, and a stick held through it must be let go
// before it counts again.
class StickPress {
  public:
    static constexpr float chordSeconds = .12f;
    bool update(float seconds, bool focused, bool mine, bool other);

  private:
    bool down_{}, decided_{}, chord_{};
    float held_{};
};
} // namespace spidy
