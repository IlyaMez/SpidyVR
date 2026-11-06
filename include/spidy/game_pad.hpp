#pragma once
#include "prompt_buttons.hpp"
#include "tracking.hpp"
#include <algorithm>
#include <array>
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
// prompt_buttons holds a demand in these same bits (it must not include this
// header: the pad is built on the vocabulary, not the other way round).
static_assert(prompt_buttons::padDpadUp == dpadUp && prompt_buttons::padDpadDown == dpadDown &&
              prompt_buttons::padDpadLeft == dpadLeft && prompt_buttons::padDpadRight == dpadRight &&
              prompt_buttons::padStart == start && prompt_buttons::padBack == back &&
              prompt_buttons::padLeftThumb == leftThumb && prompt_buttons::padRightThumb == rightThumb &&
              prompt_buttons::padLeftShoulder == leftShoulder &&
              prompt_buttons::padRightShoulder == rightShoulder &&
              prompt_buttons::padA == a && prompt_buttons::padB == b && prompt_buttons::padX == x &&
              prompt_buttons::padY == y);
// The two bumpers together: the game's web pull and throw.
constexpr uint16_t bumpers = leftShoulder | rightShoulder;
enum class Mapping {
    none,
    // VR gameplay: the controllers swing, walk and jump. The menu button
    // (Start: pause) reaches the game as a button; the game's menu (its Back:
    // map, suits, skills) is the phone's MENU (phone.hpp), no button's (X's
    // until October 23). Walking, jumping, B (the game's B: dodge), Y (the
    // game's Y: interact), X where the game shows its LB + RB (its web pull
    // and throw: NamedPress; the right stick's click until October 24), the
    // right stick pushed down (the game's D-pad down: heal) and
    // up (its LT + RT: the zip to a point; held into the game's aim mode,
    // the zip as it is let go: ZipPress) reach it as the swing leaves them
    // (Walk). The virtual controller's own right stick turns the
    // game's camera to where the player looks (CameraLook, CameraPitch). A
    // held on the ground charges the swing's own leap (ChargeJump), a
    // trigger pulled with its hand at the temple is the game's R3, its scan
    // (TemplePress), and both triggers pulled with both hands at the chest
    // are its L3 + R3, its suit power (SuitPowerPress). The Steam Frame's
    // View button and D-pad, which the other controllers lack, are the
    // game's own Back and D-pad here as on the game screen: its D-pad down
    // heals as the stick's push does. While the game names a button in a
    // prompt, the VR control of that name sends it instead of doing its job
    // in play (NamedPress).
    gameplay,
    // The game screen: every control as on an Xbox controller. A, B, X and Y
    // sit where Xbox has them; grips are the bumpers.
    menus,
    // The pad layer, latched in VR play (PadLayer): the whole controller, for
    // the controls play has no room for. Like the game screen's, with the
    // left stick still walking as the swing leaves it and the right stick the
    // game's own, a decisive push of it the game's D-pad as well.
    pad,
};
// Native walking and jumping in VR gameplay: the left stick in the game
// camera's horizontal axes (-1..1) and the jump, both as the swing leaves
// them to the game. Once this controller has been pressed (the VR menus are
// played with it), the game plays the player with it and ignores the
// keyboard, the input bridge's W/A/S/D/Space included: on October 6 those
// moved the player 0 m while this controller's A jumped him 2.9 m.
// interact: the game's Y, held. The game interacts with it (a backpack, a
// door, an on-screen prompt) and web-strikes with it in a fight; held on, it
// walks the player up to the thing (InteractStep). dodge: the
// game's B, held: its dodge, the way the stick points (a perfect dodge and
// all that follows it are the game's). The VR controllers' Y and B give
// them, where the game's own prompts show them; until October 11 B was the
// interact, Y the game menu and X the aim markers' switch. pull: the game's
// LB and RB together, held: its web pull, where it shows the two (a door or
// a grate to tear off). The grips are no bumpers in VR play (they shoot the
// webs), so both web buttons on the thing, yanked, give them (WebPull), and
// X does wherever the game shows the two (NamedPress, October 24: the
// throw's prompt by every trash can on a street). The right thumbstick's
// click gave them from October 11, when a door of the first mission that
// asks for LB + RB could not be opened in VR, until then. heal: the game's D-pad down, held: its
// heal. zip: the game's LT and RT together, held: its zip to a point, where
// it shows the two (a perch, a vent to enter). Touch controllers have no
// D-pad, and their triggers and grips are the webs in VR play, so the right
// thumbstick pushed down and up gives them (StickPush): until October 12
// nothing did, and the first mission stopped at the heal it asks for and at
// its vent. aim: the game's LT alone, held: its aim mode, which a zip held
// on in it turns into (ZipPress). look and lookUp: the virtual controller's
// own right stick, its X and Y, which turn the game's camera (CameraLook,
// CameraPitch). scan: the game's R3, held: its scan of the surroundings
// (TemplePress). suit: its L3 + R3, held: its suit power (SuitPowerPress).
// Until October 23 nothing sent the suit power's: the gesture buzzed the
// hands and counted, and the game never saw it (21 tries in one session).
// menu: its Back, held: its menu of the map, suits and skills, from the
// phone's MENU (phone.hpp); X sent it until October 23.
struct Walk {
    float right{}, forward{};
    bool jump{}, interact{}, dodge{}, pull{}, heal{}, zip{}, aim{};
    // The game's RT, alone: its parkour. With the jump on the game's own wall
    // crawl it is the only way off the wall - A by itself, in any direction
    // of the stick, only leaps the hero along it (October 14, measured: back,
    // left, right, forward and none all kept him on the wall; B, the bumpers,
    // either trigger and a held A did nothing; RT + A left it in 0.09 s).
    bool parkour{};
    float look{}, lookUp{};
    bool scan{}, suit{}, menu{};
    // What a pull at a thing the game is prompting for sends instead of the
    // pull's own LB + RB: the pair it names (its LT + RT for a vent). {}: the
    // pull is as it is. The buttons a prompt names otherwise go out from the
    // controls of their own names (NamedPress).
    prompt_buttons::Demand demand{};
};
// NATIVE RUN (vr_settings::Values::nativeRun, off by default): the game's
// RT, its parkour run, held while the player runs on the ground, so the game
// sprints, vaults and runs up walls as it does on a pad. Without it the left
// stick only jogs: the game's RT went out on its wall crawl alone (Walk::
// parkour). The stick past onAbove of its full tilt holds RT; it stays held
// until the stick falls under offBelow, so walking stays a walk and a run
// does not stutter at the edge. Anything but the ground drops it at once and
// it starts over (allowed false): game_xr allows it only where
// game_swing::gameOwnsGround holds, no web, grip or trigger is in use and no
// other of the game's buttons that RT would change goes out.
class NativeRun {
  public:
    static constexpr float defaultOnAbove = .7f, defaultOffBelow = .5f;
    NativeRun(float onAbove = defaultOnAbove, float offBelow = defaultOffBelow) {
        thresholds(onAbove, offBelow);
    }
    // onAbove within .2..1; offBelow within 0..onAbove.
    void thresholds(float onAbove, float offBelow) {
        on_ = std::isfinite(onAbove) ? std::clamp(onAbove, .2f, 1.f) : defaultOnAbove;
        off_ = std::isfinite(offBelow) ? std::clamp(offBelow, 0.f, on_) : std::min(defaultOffBelow, on_);
    }
    float onAbove() const {
        return on_;
    }
    float offBelow() const {
        return off_;
    }
    // allowed: the setting is on and the player runs on the ground (above).
    // right, forward: the stick the game gets (Walk). Returns RT.
    bool update(bool allowed, float right, float forward) {
        const float tilt = std::hypot(right, forward);
        if (!allowed || !std::isfinite(tilt)) {
            held_ = false;
            return false;
        }
        held_ = held_ ? tilt >= off_ : tilt > on_;
        return held_;
    }
    bool held() const {
        return held_;
    }

  private:
    float on_{defaultOnAbove}, off_{defaultOffBelow};
    bool held_{};
};
// NATIVE RUN's gate, in one place (game_xr builds it each frame): every
// state in which the game's RT may go out for its run on the ground. The
// player's own (setting, playing, not calibrating, not in the pad layer), the
// body's (the game, not the swing, has him on his feet: gameOwnsGround, from
// game_swing::gameOwnsGround; no mount, vent, surface or game crawl) and his
// hands' (no grip or trigger past handBusy, no grab). Then none of the
// game's buttons that RT turns into something else: A, Y, zip, aim, pull,
// heal or a prompt's demand (nativeRunAllowed).
struct NativeRunGate {
    static constexpr float handBusy = .3f;
    bool setting{}, playing{}, calibrating{}, padLayer{}, mounting{}, ventCrawl{}, onSurface{}, onCrawl{};
    bool gameOwnsGround{}, grabbing{};
    // STEADY STATES (off by default; ground_state.hpp): with `steady`, no RT
    // in a fight (`fighting`: the combat camera, or an enemy in reach lately).
    bool steady{}, fighting{};
    float grips[2]{}, triggers[2]{};
    bool handsIdle() const {
        return !grabbing && grips[0] < handBusy && triggers[0] < handBusy && grips[1] < handBusy &&
               triggers[1] < handBusy;
    }
};
inline bool nativeRunAllowed(const NativeRunGate& g, const Walk& w) {
    return g.setting && g.playing && !g.calibrating && !g.padLayer && !g.mounting && !g.ventCrawl &&
           !g.onSurface && !g.onCrawl && g.gameOwnsGround && g.handsIdle() && !w.jump && !w.interact &&
           !w.zip && !w.aim && !w.pull && !w.heal && !w.demand.any() && !(g.steady && g.fighting);
}
// A held on the ground: a charged leap, Spidy's own. The game has one, its
// Charge Jump ("Hold RT + A while running or standing to build up charge,
// then release A for a huge leap"), but as a Webslinger skill: a hero
// without it jumps as RT + A go down, and RT held on in the air is the
// game's own web swing (in the game, October 12, on a save with no
// Webslinger skill: off the ground 0.08 s after A, a jump like a tap's, and
// 17 m away on the game's web a second later). The fourth build of that day
// sent RT + A; the user: "hold jump jumps like normal". So the swing leaps
// itself (Input::leap, Swing's Leap), with or without the skill.
//
// The game's A waits for the player's: let go within tapSeconds it is the
// jump as before, made then (a pulse of pulseSeconds). Held longer it
// charges, to full in fullSeconds more, and the game gets no A; let go, the
// leap goes out with its charge (leastCharge at least) for leapSeconds: the
// swing takes it once. Only for a press that starts on the ground in VR play
// with no web holding: any other press is the game's A as it is held (a
// wall, a perch, the swing's own takeoff). Off the ground meanwhile, a
// charge is dropped.
class ChargeJump {
  public:
    static constexpr float tapSeconds = .18f, pulseSeconds = .09f, fullSeconds = .8f, leastCharge = .15f,
                           leapSeconds = .15f;
    // EARLY JUMP (vr_settings::Values::earlyJump): 0 as above; 1 a tap is
    // one let go within quickTapSeconds; 2 that, and a press while the
    // player runs (update's `running`: NATIVE RUN holds RT) is the game's A
    // as it is held, at once, with no charge.
    static constexpr float quickTapSeconds = .1f;
    static constexpr int pulseFrames = 3;
    void earlyJump(int mode) {
        early_ = std::clamp(mode, 0, 2);
        tap_ = early_ ? quickTapSeconds : tapSeconds;
    }
    struct Keys {
        bool jump{};  // the game's A
        float leap{}; // a leap let go of: its charge, above 0 up to 1 (Input::leap)
    };
    // seconds: since the previous update. held: the player's A as the game
    // would get it. ground: he stands on the ground in VR play, free to jump.
    // atOnce (COMBAT RESPONSIVE in a fight, v5): a press is the game's A as
    // it is held, whatever EARLY JUMP is.
    Keys update(float seconds, bool held, bool ground, bool running = false, bool atOnce = false) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        const bool pressed = held && !down_;
        down_ = held;
        if (pressed) {
            phase_ = ground && !(early_ == 2 && running) && !atOnce ? Phase::waiting : Phase::plain;
            time_ = 0;
        } else {
            time_ += dt;
        }
        switch (phase_) {
        case Phase::plain:
            if (!held)
                phase_ = Phase::idle;
            return {held, 0};
        case Phase::waiting:
            // Off the ground meanwhile (an edge, a web): the press is the game's A as it is.
            if (!ground) {
                phase_ = held ? Phase::plain : Phase::idle;
                return {held, 0};
            }
            if (!held) {
                phase_ = Phase::pulse;
                time_ = 0;
                pulseFrames_ = 1;
                return {true, 0};
            }
            if (time_ < tap_)
                return {};
            phase_ = Phase::charging;
            ++charges_;
            return {};
        case Phase::pulse:
            // At least pulseFrames updates as well (JUMP ALWAYS, v5): at
            // under 33 fps 0.09 s is two frames or one, and a game frame can
            // miss it.
            if (++pulseFrames_ <= pulseFrames || time_ < pulseSeconds)
                return {true, 0};
            phase_ = Phase::idle;
            return {};
        case Phase::charging:
            if (!ground) {
                phase_ = held ? Phase::spent : Phase::idle;
                return {};
            }
            if (held)
                return {};
            leap_ = std::max(charge(), leastCharge);
            phase_ = Phase::leaping;
            time_ = 0;
            ++leaps_;
            return {false, leap_};
        case Phase::leaping:
            if (time_ < leapSeconds)
                return {false, leap_};
            phase_ = Phase::idle;
            return {};
        case Phase::spent: // held on into the air: nothing until it is let go
            if (!held)
                phase_ = Phase::idle;
            return {};
        case Phase::idle:
            break;
        }
        return {};
    }
    // A is held and a leap charges.
    bool charging() const {
        return phase_ == Phase::charging;
    }
    // How far: 0 to 1.
    float charge() const {
        return phase_ == Phase::charging ? std::clamp((time_ - tap_) / fullSeconds, 0.f, 1.f) : 0.f;
    }
    // Charges begun so far, and leaps let go of.
    uint32_t charges() const {
        return charges_;
    }
    uint32_t leaps() const {
        return leaps_;
    }

  private:
    enum class Phase { idle, plain, waiting, pulse, charging, leaping, spent };
    Phase phase_{};
    bool down_{};
    int early_{};
    float time_{}, leap_{}, tap_{tapSeconds};
    int pulseFrames_{};
    uint32_t charges_{}, leaps_{};
};
// JUMP ALWAYS (v5, always on): while the swing runs the body on the ground
// (SPIDY RUN, SMOOTH WEB LANDING's carry: the swing owns a grounded body),
// the game's mover follows the swing and its jump goes nowhere. The press
// ends that run in the swing (Swing::jumpCancels), and the game's A waits
// here until the game has the body back (`swingGround` false), waitSeconds
// at most, then goes out as a fresh press: held on while the player holds
// it, or a pulse of pulseSeconds and pulseFrames updates at least if he
// already let go. Any other press is the game's A as it is held.
class JumpRelay {
  public:
    static constexpr float waitSeconds = .2f, pulseSeconds = .09f;
    static constexpr int pulseFrames = 3;
    // swingJumps (SPIDY RUN JUMP, v6): the swing makes that press's jump
    // itself, at once; the game gets no A for it (in the air A would be its
    // zip) until the press is let go (swallowed()).
    bool update(float seconds, bool held, bool swingGround, bool swingJumps = false) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        const bool pressed = held && !down_;
        down_ = held;
        if (pressed && swingGround && swingJumps) {
            phase_ = Phase::swallowed;
            ++swallowed_;
            return false;
        }
        if (pressed && swingGround) {
            phase_ = Phase::waiting;
            time_ = 0;
            tapped_ = false;
            ++deferred_;
            return false;
        }
        switch (phase_) {
        case Phase::waiting:
            time_ += dt;
            tapped_ |= !held;
            if (swingGround && time_ < waitSeconds)
                return false;
            waited_ = time_;
            ++relayed_;
            if (held && !tapped_) {
                phase_ = Phase::idle;
                return true;
            }
            phase_ = Phase::pulse;
            time_ = 0;
            frames_ = 1;
            return true;
        case Phase::pulse:
            time_ += dt;
            if (++frames_ <= pulseFrames || time_ < pulseSeconds)
                return true;
            phase_ = Phase::idle;
            return held;
        case Phase::swallowed:
            if (held)
                return false;
            phase_ = Phase::idle;
            return false;
        case Phase::idle:
            break;
        }
        return held;
    }
    uint32_t swallowed() const {
        return swallowed_;
    }
    bool waiting() const {
        return phase_ == Phase::waiting;
    }
    // Presses held back so far, those sent on, and how long the last one waited.
    uint32_t deferred() const {
        return deferred_;
    }
    uint32_t relayed() const {
        return relayed_;
    }
    float waited() const {
        return waited_;
    }

  private:
    enum class Phase { idle, waiting, pulse, swallowed };
    Phase phase_{};
    bool down_{}, tapped_{};
    int frames_{};
    float time_{}, waited_{};
    uint32_t deferred_{}, relayed_{}, swallowed_{};
};
// A hand at the temple with its trigger pulled: the game's R3, its scan of
// the surroundings, for as long as the trigger stays pulled (minSeconds at
// least). The hand: its grip within `reach` of a point `side` to either side
// of the head (the controller sits a hand's width out from the temple it
// touches), as the trigger goes down; it may move away while it holds. That
// trigger is the scan's alone meanwhile: no web ball, no reel (hands()).
// Outside VR play nothing holds, and a trigger held through that must be let
// go first.
class TemplePress {
  public:
    static constexpr float side = .15f, reach = .19f, pressed = .6f, released = .3f, minSeconds = .15f;
    // seconds: since the previous update. play: the controllers play VR.
    // head and grips: the headset's and each hand's grip pose, in one space;
    // tracked and triggers: each hand's. Returns the game's R3: down or not.
    bool update(float seconds, bool play, const Pose& head, const Pose grips[2], const bool tracked[2],
                const float triggers[2]) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        left_ = std::max(0.f, left_ - dt);
        for (unsigned i = 0; i < 2; ++i) {
            const float trigger = std::isfinite(triggers[i]) ? triggers[i] : 0.f;
            const bool down = tracked[i] && trigger > (down_[i] ? released : pressed);
            const bool press = down && !down_[i];
            down_[i] = down;
            const uint32_t bit = 1u << i;
            if (!down || !play) {
                hands_ &= ~bit;
                continue;
            }
            if (!press || !finite(head.position) || !finite(grips[i].position))
                continue;
            // The grip in the head's own axes: x to its right.
            const Vec3 local = head.orientation.conjugate().rotate(grips[i].position - head.position);
            if (!finite(local))
                continue;
            for (const float x : {-side, side})
                if (length(local - Vec3{x, 0, 0}) <= reach) {
                    hands_ |= bit;
                    scans_ += !held();
                    left_ = minSeconds;
                    break;
                }
        }
        if (!play)
            left_ = 0;
        held_ = hands_ != 0 || left_ > 0;
        return held_;
    }
    bool held() const {
        return held_;
    }
    // A bit per hand whose trigger is the scan's.
    uint32_t hands() const {
        return hands_;
    }
    // Scans begun so far.
    uint32_t scans() const {
        return scans_;
    }

  private:
    bool down_[2]{}, held_{};
    uint32_t hands_{}, scans_{};
    float left_{};
};
// Both hands at the chest with both triggers pulled: the game's L3 + R3, its
// suit power. A player's idea, October 15 ("both your hands to your chest and
// hit trigger like the scan"): VR play had no way to it at all, as clicking
// both sticks is the pad layer and only the game screen sent the game its L3
// + R3. The chest: a point `below` the headset and `ahead` of it, level with
// where it looks. A trigger is the gesture's (hands(): no web, web ball or
// reel meanwhile) only if it goes down with both hands there, each within
// `reach` of the point and no more than `apart` from the other, and with no
// web out of its hand. It waits `together` for the other trigger; not joined,
// it is the web's again, still held: a press that much late. Both down within
// `together` of each other are the power, and both triggers are its until let
// go. Until November 5 one trigger pulled within 22 cm of the point was the
// gesture's until let go: a player's hands at rest were 13 to 19 cm from it,
// and his October 28 session made 38 suit powers and not one web from its
// triggers. Outside VR play nothing holds, and a trigger held through that
// must be let go first.
class SuitPowerPress {
  public:
    static constexpr float below = .38f, ahead = .15f, reach = .12f, apart = .2f, together = .15f,
                           pressed = .6f, released = .3f, holdSeconds = .15f;
    // seconds: since the previous update. play: the controllers play VR.
    // head and grips: the headset's and each hand's grip pose, in one space;
    // tracked and triggers: each hand's; webs: each hand's web is out (none:
    // neither). Returns the game's L3 + R3: down or not.
    bool update(float seconds, bool play, const Pose& head, const Pose grips[2], const bool tracked[2],
                const float triggers[2], const bool webs[2] = nullptr) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        left_ = std::max(0.f, left_ - dt);
        Vec3 chest{};
        bool placed = play && finite(head.position);
        if (placed) {
            Vec3 look = head.orientation.rotate({0, 0, -1});
            look.y = 0;
            look = length(look) > .1f ? normalized(look) : Vec3{0, 0, -1};
            chest = head.position + Vec3{0, -below, 0} + look * ahead;
            placed = finite(chest);
        }
        // Both hands at the chest, together.
        bool at[2]{};
        for (unsigned i = 0; i < 2; ++i)
            at[i] = placed && tracked[i] && finite(grips[i].position) &&
                    length(grips[i].position - chest) <= reach;
        const bool posed = at[0] && at[1] && length(grips[0].position - grips[1].position) <= apart;
        for (unsigned i = 0; i < 2; ++i) {
            const float trigger = std::isfinite(triggers[i]) ? triggers[i] : 0.f;
            const bool down = tracked[i] && trigger > (down_[i] ? released : pressed);
            const bool press = down && !down_[i];
            down_[i] = down;
            since_[i] += dt;
            const uint32_t bit = 1u << i;
            if (!down || !play) {
                hands_ &= ~bit;
                continue;
            }
            if (press && posed && !(webs && webs[i])) {
                hands_ |= bit;
                since_[i] = 0;
                spent_ &= ~bit;
            }
        }
        // Both triggers pulled at the chest, close together and not spent
        // on a power already: the power.
        if (hands_ == 3 && !spent_ && std::abs(since_[0] - since_[1]) <= together) {
            spent_ = 3;
            ++powers_;
            left_ = holdSeconds;
        }
        // A trigger the other did not join in time is the web's again.
        for (unsigned i = 0; i < 2; ++i)
            if ((hands_ & (1u << i)) && !(spent_ & (1u << i)) && since_[i] > together)
                hands_ &= ~(1u << i);
        if (!play)
            left_ = 0;
        held_ = left_ > 0;
        return held_;
    }
    bool held() const {
        return held_;
    }
    // A bit per hand whose trigger is the gesture's.
    uint32_t hands() const {
        return hands_;
    }
    // Suit powers asked for so far.
    uint32_t powers() const {
        return powers_;
    }

  private:
    bool down_[2]{}, held_{};
    float since_[2]{100, 100};
    uint32_t hands_{}, spent_{}, powers_{};
    float left_{};
};
// The right thumbstick pushed straight down or up, as Walk::heal and
// Walk::zip. In VR play that stick turns the player with its left and right;
// its down and up did nothing. A push counts from the stick at rest, once it
// has been `pushed` far and no more than `aside` off the line for
// holdSeconds, and holds until the stick is back within `released`: a turn
// rolled round to the top, a diagonal and a flick through the line are none.
// Outside VR play nothing holds, and a stick held through that must be let
// go first (on the game screen it is the game's own right stick) - but a
// push already under way keeps through a moment out of play, as long as
// graceSeconds (a long frame, a blink of the headset's focus), and goes on
// counting after it. Until October 18 one such moment spent it.
// Reserve an intentional upward zip command before basic flips read the
// stick. Keep ownership through a diagonal drag until neutral is seen.
class FlipZipPriority {
  public:
    bool update(bool play, bool tracked, float x, float y) {
        began_ = false;
        if (!play) { owned_ = ready_ = false; return false; }
        if (!tracked || !std::isfinite(x) || !std::isfinite(y)) return owned_;
        if (std::hypot(x, y) < .5f) { owned_ = false; ready_ = true; return false; }
        if (!owned_ && ready_ && y >= .75f && std::abs(x) <= .45f) {
            owned_ = began_ = true; ready_ = false;
        }
        return owned_;
    }
    bool began() const { return began_; }
  private:
    bool ready_{}, owned_{}, began_{};
};
class StickPush {
  public:
    // Another control mode owns the stick. Require neutral before a new shortcut.
    void cancel() {
        way_ = Way::none;
        spent_ = true;
        held_ = lapse_ = 0;
    }
    static constexpr float pushed = .75f, aside = .45f, released = .5f, holdSeconds = .15f,
                           graceSeconds = .25f;
    enum class Way { none, down, up };
    // seconds: since the previous update. play: the controllers play VR.
    // x, y: the right thumbstick, -1..1.
    Way update(float seconds, bool play, float x, float y) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        // A bad reading is a stick at rest.
        const bool rest = !std::isfinite(x) || !std::isfinite(y) || std::hypot(x, y) < released;
        if (!play && !rest && way_ != Way::none && lapse_ + dt < graceSeconds) {
            lapse_ += dt;
            return Way::none;
        }
        lapse_ = 0;
        if (!play || rest) {
            way_ = Way::none;
            held_ = 0;
            spent_ = !rest;
            return Way::none;
        }
        if (spent_)
            return Way::none;
        if (way_ == Way::none) {
            if (std::abs(x) > aside) {
                spent_ = true;
                return Way::none;
            }
            if (std::abs(y) < pushed)
                return Way::none;
            way_ = y < 0 ? Way::down : Way::up;
        } else if ((way_ == Way::down) != (y < 0) || std::abs(y) < released) {
            way_ = Way::none;
            held_ = 0;
            spent_ = true;
            return Way::none;
        }
        held_ += dt;
        return held_ >= holdSeconds ? way_ : Way::none;
    }

  private:
    Way way_{};
    bool spent_{};
    float held_{}, lapse_{};
};
// The right stick held up (StickPush) as the game's LT + RT, and what letting
// go of it does. The two going down at once are the game's zip to a point,
// where it has one. Where it has none, LT held on is its aim mode (its L2:
// "the standard aim mode functionality (L2)"), whose aim snaps to the points
// it can zip to ("If true, we'll snap to zip-to-perch targets") and which
// slows the game's own time once it has lasted a moment ("How long to delay
// at the start of aim-mode before triggering time-scale"). Holding the stick
// up waits for a zip, so that is where a player who held it ended up: in the
// user's session of October 18 the game's time went down to 0.2 eight times,
// 0.56 to 0.64 s after the zip went out, for up to 3 s (once for a single
// frame, the stick let go just then), and letting go zipped nowhere ("when
// holding up (zip) and it enters the slowmo aiming mode, on release we will
// zip").
//
// So a hold the game slows its time in (its own scale below slowBelow, after
// LT + RT have been down pressSeconds) is aiming: from then on LT alone goes
// out, the aim mode staying and RT coming up, and letting go of the stick
// presses RT again beside it for pressSeconds (after RT has been up
// gapSeconds): LT + RT in the aim mode, the zip to the point its aim holds.
// A press the game zips from at once, and a hold let go before its time
// slows, are the zip as before. Out of VR play nothing goes out; a hold keeps
// through graceSeconds of that (as StickPush's push does) and is dropped
// after, letting go sending nothing.
class ZipPress {
  public:
    bool active() const { return phase_ != Phase::idle; }
    // Cancel ownership without releasing an aimed zip or clearing telemetry.
    void cancel() {
        phase_ = Phase::idle;
        time_ = lapse_ = 0;
    }
    static constexpr float slowBelow = .9f, pressSeconds = .2f, gapSeconds = .05f, graceSeconds = .25f;
    struct Keys {
        bool zip{}; // the game's LT + RT
        bool aim{}; // its LT alone: its aim mode
    };
    // seconds: since the previous update. up: the stick held up for the
    // game's zip (StickPush). play: VR play. slow: the game's own time scale
    // is below slowBelow now.
    Keys update(float seconds, bool up, bool play, bool slow) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        if (!play) {
            lapse_ += dt;
            if (lapse_ >= graceSeconds || phase_ == Phase::waiting || phase_ == Phase::pressing)
                phase_ = Phase::idle;
            return {};
        }
        lapse_ = 0;
        time_ += dt;
        switch (phase_) {
        case Phase::idle:
            if (!up)
                return {};
            phase_ = Phase::held;
            time_ = 0;
            return {true, false};
        case Phase::held:
            if (!up) {
                phase_ = Phase::idle;
                return {};
            }
            if (!slow || time_ < pressSeconds)
                return {true, false};
            phase_ = Phase::aiming;
            time_ = 0;
            ++aims_;
            return {false, true};
        case Phase::aiming:
            if (up)
                return {false, true};
            ++releases_;
            if (time_ < gapSeconds) {
                phase_ = Phase::waiting;
                return {false, true};
            }
            phase_ = Phase::pressing;
            time_ = 0;
            return {true, false};
        case Phase::waiting:
            if (time_ < gapSeconds)
                return {false, true};
            phase_ = Phase::pressing;
            time_ = 0;
            return {true, false};
        case Phase::pressing:
            if (time_ < pressSeconds)
                return {true, false};
            phase_ = Phase::idle;
            return {};
        }
        return {};
    }
    // Holds that became aiming, and zips sent as one was let go, so far.
    uint32_t aims() const {
        return aims_;
    }
    uint32_t releases() const {
        return releases_;
    }

  private:
    // held: LT + RT down; aiming: LT alone, RT up for time_; waiting: let
    // go, RT not up long enough yet; pressing: the zip let go of.
    enum class Phase { idle, held, aiming, waiting, pressing };
    Phase phase_{};
    float time_{}, lapse_{};
    uint32_t aims_{}, releases_{};
};
// The game's own camera, kept looking where the player looks. VR shows the
// world from the headset; the game's follow camera is still there, turns
// with the right stick alone (which in VR play turns the player's view with
// its left and right, and heals and zips with its down and up: StickPush)
// and otherwise stays where the last run left it. The game offers
// its Y only for a thing that camera has in view. In the game, October 12,
// 0.4 m from the red door at the end of the first mission's hall: "Open
// Door" showed with the camera within 30 degrees of the door and not from 50
// (to one side) or 70 (to the other) on, and there Y did nothing; in the
// headset the evening before, 25 presses at that door opened nothing.
//
// So the virtual controller's right stick turns that camera until it looks
// along the head's level direction. The game's camera answers the stick
// steeply (measured there, degrees a second: 180 at full tilt after a fifth
// of a second, 22 at 0.76, 9 at 0.6, 3 at 0.5), so the stick is the cube
// root of the turn wanted, above `floor`: from 170 degrees off the camera
// was within 4 in 1.4 s and stayed there. It starts beyond startDegrees and
// rests within stopDegrees: a head that looks about turns nothing.
//
// precise: EYE AIM's zip (EyeZip), the camera brought onto what the eyes look
// at, which is the point the game zips to: it starts beyond
// preciseStartDegrees and rests within preciseStopDegrees.
class CameraLook {
  public:
    static constexpr float startDegrees = 12, stopDegrees = 4, fullRate = 180, seconds = .25f, floor = .45f;
    static constexpr float preciseStartDegrees = 2.5f, preciseStopDegrees = 1;
    // look: where the head looks; camera: the game camera's forward (both in
    // the world). steer: VR play, with that camera the player's own. Returns
    // the right stick's X, -1..1.
    float update(Vec3 look, Vec3 camera, bool steer, bool precise = false) {
        look.y = camera.y = 0;
        // Straight up or down says no way to look.
        if (!steer || !finite(look) || !finite(camera) || length(look) < .3f || length(camera) < .3f) {
            turning_ = false;
            error_ = 0;
            return 0;
        }
        look = normalized(look);
        camera = normalized(camera);
        // To the camera's right, as the game takes the stick: cross(forward, up).
        error_ = std::atan2(dot(look, cross(camera, {0, 1, 0})), dot(look, camera)) * 57.29578f;
        const float off = std::abs(error_);
        const bool turn = off > (precise ? (turning_ ? preciseStopDegrees : preciseStartDegrees)
                                         : (turning_ ? stopDegrees : startDegrees));
        turns_ += turn && !turning_;
        turning_ = turn;
        if (!turn)
            return 0;
        const float rate = std::min(1.f, off / (fullRate * seconds));
        return std::copysign(floor + (1 - floor) * std::cbrt(rate), error_);
    }
    // From the camera's forward to the head's level direction, degrees (to
    // the camera's right: more than 0); 0 while it is not steered.
    float error() const {
        return error_;
    }
    // The times the stick began to turn the camera.
    uint32_t turns() const {
        return turns_;
    }

  private:
    float error_{};
    bool turning_{};
    uint32_t turns_{};
};
// The same for the camera's pitch. The game offers a thing only with its
// camera on it, and that camera rests 10 degrees below level wherever the
// player looks: a vent at his feet or a grate overhead was never in its view
// (the user, October 12: "there is a closed vent i need to go through and
// cant interact with", on a roof, looking down). The stick's Y pitches it
// as its X turns it (in the game, October 12: 73 degrees a second over 0.6 s
// at full tilt, 3 a second at 0.6, up for up; it stays where it is left,
// from 85 degrees down to 68 up), so the same curve brings it to the head's
// pitch, within the camera's own range.
class CameraPitch {
  public:
    static constexpr float lowest = -80, highest = 62;
    // look: where the head looks; camera: the game camera's forward (both in
    // the world). steer and precise: as CameraLook's. Returns the right
    // stick's Y, -1..1.
    float update(Vec3 look, Vec3 camera, bool steer, bool precise = false) {
        if (!steer || !finite(look) || !finite(camera) || length(look) < .5f || length(camera) < .5f) {
            turning_ = false;
            error_ = 0;
            return 0;
        }
        const auto pitch = [](Vec3 v) { return std::asin(std::clamp(v.y / length(v), -1.f, 1.f)) * 57.29578f; };
        error_ = std::clamp(pitch(look), lowest, highest) - pitch(camera);
        const float off = std::abs(error_);
        const bool turn =
            off > (precise ? (turning_ ? CameraLook::preciseStopDegrees : CameraLook::preciseStartDegrees)
                           : (turning_ ? CameraLook::stopDegrees : CameraLook::startDegrees));
        turns_ += turn && !turning_;
        turning_ = turn;
        if (!turn)
            return 0;
        const float rate = std::min(1.f, off / (CameraLook::fullRate * CameraLook::seconds));
        return std::copysign(CameraLook::floor + (1 - CameraLook::floor) * std::cbrt(rate), error_);
    }
    // From the camera's pitch to the head's, degrees (the head higher: more
    // than 0); 0 while it is not steered.
    float error() const {
        return error_;
    }
    uint32_t turns() const {
        return turns_;
    }

  private:
    float error_{};
    bool turning_{};
    uint32_t turns_{};
};
// EYE AIM's zips (vr_settings::eyeAimZips). The game zips to the point its own
// camera has in view (its aim "snaps to zip-to-perch targets": ZipPress), and
// that camera follows the head's level look loosely (CameraLook,
// CameraPitch). With the eyes aiming zips it follows them instead, closely
// (`precise`), from the moment the stick goes up for one to the zip's end,
// at the point the eyes meet as seen from where that camera stands. A zip
// made at once (a tap of the stick) would leave before the camera got
// there, so the zip waits for it - within settledDegrees each way - for
// waitSeconds at most past the stick's own holdSeconds; a hold that turned
// into the game's aim mode zips when let go, as before, with the camera on
// the eyes all the while.
class EyeZip {
  public:
    static constexpr float settledDegrees = 3, waitSeconds = .35f;
    // seconds: since the previous update. up: the stick held up for a zip
    // (StickPush). eyes: the camera follows the eyes now. yaw, pitch: how far
    // it is from them (CameraLook::error, CameraPitch::error), degrees.
    // Whether the zip may go out: once it may, it may until the stick comes
    // down.
    bool update(float seconds, bool up, bool eyes, float yaw, float pitch) {
        if (!up) {
            waited_ = 0;
            ready_ = false;
            return false;
        }
        if (ready_)
            return true;
        if (!eyes)
            return ready_ = true;
        waited_ += std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        const bool settled = std::abs(yaw) <= settledDegrees && std::abs(pitch) <= settledDegrees;
        if (settled || waited_ >= waitSeconds) {
            ready_ = true;
            ++(settled ? settled_ : late_);
        }
        return ready_;
    }
    // Zips the eyes aimed that went out with the camera on them, and those
    // that went out at waitSeconds without.
    uint32_t settled() const {
        return settled_;
    }
    uint32_t late() const {
        return late_;
    }

  private:
    float waited_{};
    bool ready_{};
    uint32_t settled_{}, late_{};
};
// Where the game's camera must look for its centre to be on `target`, seen
// from where it stands: the eyes' point (game_swing::AimData::gaze) from the
// camera, which stands behind and above the head. A point too near the camera
// to say (under a metre), or none, is the eyes' own direction.
inline Vec3 cameraLookAt(Vec3 camera, Vec3 target, bool hasTarget, Vec3 gaze) {
    const Vec3 to = target - camera;
    if (hasTarget && finite(to) && length(to) >= 1)
        return normalized(to);
    return gaze;
}
// Y held, to reach what it is for. The game takes its Y only close to the
// thing: a door of the first mission showed "Open Door" 1.1 m from its middle
// and not 1.2 m, nearer than a VR player stands to a wall (October 11: four
// presses from 1.9 to 2.5 m did nothing, in the headset and in the plain
// game alike). Y held longer than holdSeconds walks the player on where they
// look, at walkStick of the stick, for walkSeconds at most; the game takes a
// Y held on the way in (in the game, October 11), and the scene that follows
// ends the walk. A walk that meets a wall (slower than stallSpeed for
// stallSeconds, once it has had startSeconds to get going) ends too. A walk
// that ended starts again only with another press.
//
// The game also takes its Y only from a hero who faces the thing, and in VR
// he faces wherever he last walked: a player who slid along that door to its
// middle, or dropped off the wall above it, stood with his back or his side
// to it. In the game, October 12, at the red door of the first mission's
// hall, the camera on it: no "Open Door" for a hero turned away, and two Y
// taps did nothing. So every press begins with the stick where the player
// looks for turnSeconds (at walkStick the hero turned right round within
// 0.55 s and took a step of 0.13 m; a tilt of 0.3 turned nobody), and the
// game's Y stays down for pressSeconds at least, the turn's time: such a tap
// opened the door.
class InteractStep {
  public:
    static constexpr float holdSeconds = .3f, walkSeconds = 2, walkStick = .5f, startSeconds = .4f,
                           stallSeconds = .25f, stallSpeed = .3f, turnSeconds = .15f, pressSeconds = .6f;
    // seconds: since the previous update. held: the player's Y. allowed: the
    // game has the player on the ground, out of a fight (there Y is the web
    // strike, as long as the player presses it). speed: the player's over
    // the ground, m/s. Returns how hard the stick points where the player
    // looks: 0, or walkStick.
    float update(float seconds, bool held, bool allowed, float speed) {
        const bool timed = std::isfinite(seconds);
        const float dt = timed ? std::clamp(seconds, 0.f, .1f) : 0.f;
        if (held && !down_) {
            since_ = 0;
            turns_ = allowed;
        } else {
            since_ = std::min(since_ + dt, pressSeconds);
        }
        down_ = held;
        // A frame without a time turns nobody: no turn that never ends.
        turns_ = turns_ && allowed && timed && since_ < pressSeconds;
        const float turn = turns_ && since_ < turnSeconds ? walkStick : 0.f;
        if (!held) {
            held_ = walked_ = stalled_ = 0;
            walking_ = done_ = false;
            return turn;
        }
        held_ += dt;
        done_ = done_ || !allowed;
        if (!done_ && walking_) {
            walked_ += dt;
            stalled_ = walked_ > startSeconds && !(speed >= stallSpeed) ? stalled_ + dt : 0.f;
            done_ = walked_ >= walkSeconds || stalled_ >= stallSeconds;
        }
        if (done_ || held_ < holdSeconds) {
            walking_ = false;
            return turn;
        }
        steps_ += !walking_;
        walking_ = true;
        return walkStick;
    }
    // The game's Y is down: the player's, or the rest of pressSeconds after
    // a press that turned the hero.
    bool pressed() const {
        return down_ || turns_;
    }
    // Walks begun so far.
    uint32_t steps() const {
        return steps_;
    }

  private:
    float held_{}, walked_{}, stalled_{}, since_ = pressSeconds;
    bool walking_{}, done_{}, down_{}, turns_{};
    uint32_t steps_{};
};
// Where B, X and Y went down. On the game screen they are the game's own B,
// X and Y; in VR play B dodges, Y interacts and X opens the gadget wheel. Held
// from one into the other, a button stays what it was until it is let go:
// Back held into play is no dodge, and an interact held into a menu the game
// opened is no Y there (which resets a Settings tab).
class PressOrigins {
  public:
    // buttons: XrFrame::buttons. play: the controllers play VR
    // (Mapping::gameplay); else a press counts for the game screen.
    void update(uint32_t buttons, bool play) {
        play_ &= buttons;
        screen_ &= buttons;
        (play ? play_ : screen_) |= buttons & ~(play_ | screen_);
    }
    // The buttons held since a press in VR play, and on the game screen.
    uint32_t play() const {
        return play_;
    }
    uint32_t screen() const {
        return screen_;
    }
    // `frame` with B, X and Y held only where they went down for `mapping`.
    XrFrame filtered(XrFrame frame, Mapping mapping) const {
        constexpr uint32_t face = buttonB | buttonX | buttonY;
        // The pad layer is VR play: a press made there went down in play.
        const uint32_t kept = mapping == Mapping::gameplay || mapping == Mapping::pad ? play_
                              : mapping == Mapping::menus                            ? screen_
                                                                                     : 0;
        frame.buttons = (frame.buttons & ~face) | (kept & face);
        return frame;
    }

  private:
    uint32_t play_{}, screen_{};
};
// The whole pad, for the controls VR play has no room for.
//
// Play sends nine of the pad's sixteen controls, and the game picks the one a
// moment wants out of all of them, so each story beat or encounter that asked
// for another one stopped a player until it was patched on its own: the right
// stick's click for the game's LB + RB on October 11 (a door of the first
// mission), that stick's pushes for its heal and its LT + RT on October 12
// (that mission's heal and its vent). The reports left over all read the same
// way - "Can't pull objects with LB + RB", "cant use Vents", "it says tap x
// repeat but tapping x opens the menu", Air Launch, the Fisk turrets - and
// the game's X, its L3, each bumper and each trigger on its own and three
// ways of its D-pad could not be sent at all.
//
// Both thumbsticks held in this long are the game's Start (PausePress); let
// go sooner, a click of both is the pad layer's (PadLayer) or, with the game
// played on its screen, the game's L3 + R3 (ScreenControls).
constexpr float pauseHoldSeconds = .7f;
// Clicking both thumbsticks together latches this layer, and every control of
// the pad can be sent while it is on. The chord is free: neither stick's own
// press has been half of clicking both since October 12 (slow_motion's
// StickPress drops a chord, and the right stick's click alone sends nothing
// since October 24, unless the game names its R3), and a tap leaves the thumbs
// free for the face buttons, which holding the chord would not. It never
// latches while a web carries the player, and it lets go by itself after
// idleSeconds of nothing pressed, so a player cannot be left in it.
//
// The click toggles it as it is let go, once held chordSeconds: held on to
// pauseHoldSeconds it is the pause instead (PausePress), and toggles nothing.
// Until October 18 it toggled at chordSeconds, under the thumbs.
class PadLayer {
  public:
    static constexpr float chordSeconds = .12f, idleSeconds = 20;
    // seconds: since the previous update. allowed: VR play with the player
    // free of a web. left, right: each thumbstick's click. busy: a control of
    // the layer is in use now (a button, a trigger, a grip, the right stick),
    // which keeps it open.
    bool update(float seconds, bool allowed, bool left, bool right, bool busy) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        const bool chord = left && right;
        if (!allowed) {
            latched_ = false;
            held_ = 0;
            // A chord held through this must be let go of first.
            spent_ = chord;
            return false;
        }
        if (!chord) {
            if (!spent_ && held_ >= chordSeconds) {
                latched_ = !latched_;
                idle_ = 0;
                if (latched_)
                    ++entries_;
            }
            held_ = 0;
            spent_ = false;
        } else if (!spent_) {
            held_ += dt;
            // Held into the pause: it is PausePress's.
            spent_ = held_ >= pauseHoldSeconds;
        }
        if (latched_) {
            idle_ = busy ? 0.f : idle_ + dt;
            if (idle_ >= idleSeconds)
                latched_ = false;
        }
        return latched_;
    }
    bool latched() const {
        return latched_;
    }
    // Lets go of it at once: the phone drawn from a back pocket does, as the
    // phone is play's. On October 25 a click of both sticks with the game's
    // camera out latched it unseen, and for 110 s the phone would not come
    // out and the grips and triggers shot no web; every press kept it on,
    // until the pause menu let it go.
    void leave() {
        latched_ = false;
        idle_ = 0;
    }
    // The times the layer was latched on.
    uint32_t entries() const {
        return entries_;
    }

  private:
    bool latched_{}, spent_{};
    float held_{}, idle_{};
    uint32_t entries_{};
};
// The game's Start from both thumbsticks held in for pauseHoldSeconds, for a
// controller whose menu button the runtime keeps for itself. Under SteamVR a
// Quest's left menu button is SteamVR's own system button: its Touch profile
// (drivers/oculus/resources/input/touch_profile.json) has no menu input, and
// SteamVR opens its dashboard with that button (vrcompositor_bindings_
// touch.json), so the game's pause never came from it (October 16: "with
// steamvr if you're on quest you can't open the pause menu"); an Index
// controller's system button is SteamVR's too. The menu button stays Start
// wherever it arrives. Start goes out for tapSeconds, once a hold; the chord
// is spent until both sticks are let go, and meanwhile neither stick's click
// goes out (spent): the pause menu opening under the thumbs would read them.
class PausePress {
  public:
    static constexpr float tapSeconds = .1f;
    // seconds: since the previous update. allowed: the controllers drive the
    // game (any mapping but none). left, right: each thumbstick's click.
    // True while Start goes out.
    bool update(float seconds, bool allowed, bool left, bool right) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        const bool chord = left && right;
        tap_ = std::max(0.f, tap_ - dt);
        if (!left && !right)
            spent_ = false;
        if (!allowed) {
            // A chord held through this must be let go of first.
            held_ = 0;
            spent_ = spent_ || chord;
            tap_ = 0;
            return false;
        }
        if (!chord) {
            held_ = 0;
        } else if (!spent_) {
            held_ += dt;
            if (held_ >= pauseHoldSeconds) {
                spent_ = true;
                tap_ = tapSeconds;
                ++pauses_;
            }
        }
        return tap_ > 0;
    }
    // A hold reached the pause, and a stick is still down.
    bool spent() const {
        return spent_;
    }
    // The pauses sent so far.
    uint32_t pauses() const {
        return pauses_;
    }

  private:
    bool spent_{};
    float held_{}, tap_{};
    uint32_t pauses_{};
};
// The game played on its own screen (the SPIDY VR tab's VIEW: GAME SCREEN):
// every control as on an Xbox controller, as on the game screen
// (Mapping::menus), and the two that no VR controller but the Steam Frame's
// has. Back (the game's map) is the menu button held holdSeconds; a shorter
// press is Start (pause), sent as it is let go. The D-pad (its down heals) is
// the right thumbstick pushed decisively while the left one is clicked down,
// which then turns no camera; a click of the left stick that pushed no D-pad
// is the game's L3 as it is let go. Both sticks clicked are the game's L3 +
// R3, as both are let go (held on to pauseHoldSeconds they are its Start
// instead: PausePress), and neither goes out while they are down. A press
// that began before the game was played on its screen (a pause menu's Start)
// does nothing here until it is let go.
class ScreenControls {
  public:
    static constexpr float holdSeconds = .5f, tapSeconds = .1f, decisive = .7f;
    // seconds: since the previous update. play: the game is played on its
    // screen now. `s`, built for Mapping::menus from `frame`, is rewritten.
    void apply(float seconds, bool play, const XrFrame& frame, State& s) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        const bool menu = (frame.buttons & buttonMenu) != 0;
        const auto& right = frame.hands[1];
        const bool click = frame.hands[0].stickClick;
        startTap_ = std::max(0.f, startTap_ - dt);
        thumbTap_ = std::max(0.f, thumbTap_ - dt);
        chordTap_ = std::max(0.f, chordTap_ - dt);
        if (!play) {
            menuDown_ = clickDown_ = dpad_ = chord_ = false;
            menuHeld_ = chordHeld_ = 0;
            menuSpent_ = menu;
            clickSpent_ = click || right.stickClick;
        } else {
            s.buttons &= static_cast<uint16_t>(~start);
            if (menu && !menuSpent_) {
                menuDown_ = true;
                menuHeld_ += dt;
                if (menuHeld_ >= holdSeconds)
                    s.buttons |= back;
            } else if (!menu) {
                if (menuDown_ && menuHeld_ < holdSeconds)
                    startTap_ = tapSeconds;
                menuDown_ = menuSpent_ = false;
                menuHeld_ = 0;
            }
            if (click && right.stickClick && !clickSpent_)
                chord_ = true;
            if (chord_) {
                // Both sticks: nothing of either until both are let go.
                s.buttons &= static_cast<uint16_t>(~(leftThumb | rightThumb));
                s.thumbRX = s.thumbRY = 0;
                if (click && right.stickClick) {
                    chordHeld_ += dt;
                } else if (!click && !right.stickClick) {
                    if (chordHeld_ < pauseHoldSeconds)
                        chordTap_ = tapSeconds;
                    clickDown_ = dpad_ = chord_ = clickSpent_ = false;
                    chordHeld_ = 0;
                }
            } else if (click && !clickSpent_) {
                clickDown_ = true;
                s.buttons &= static_cast<uint16_t>(~leftThumb);
                const float rx = right.valid && std::isfinite(right.stickX) ? right.stickX : 0.f;
                const float ry = right.valid && std::isfinite(right.stickY) ? right.stickY : 0.f;
                if (std::abs(rx) >= decisive && std::abs(rx) > std::abs(ry)) {
                    s.buttons |= rx > 0 ? dpadRight : dpadLeft;
                    dpad_ = true;
                } else if (std::abs(ry) >= decisive) {
                    s.buttons |= ry > 0 ? dpadUp : dpadDown;
                    dpad_ = true;
                }
                s.thumbRX = s.thumbRY = 0;
            } else if (!click) {
                if (clickDown_ && !dpad_)
                    thumbTap_ = tapSeconds;
                clickDown_ = dpad_ = false;
                clickSpent_ = clickSpent_ && right.stickClick;
            }
        }
        // A tap let go of keeps its button down long enough for the game to
        // read it, whatever comes next (a Start pauses: the game screen).
        if (startTap_ > 0)
            s.buttons |= start;
        if (thumbTap_ > 0)
            s.buttons |= leftThumb;
        if (chordTap_ > 0)
            s.buttons |= leftThumb | rightThumb;
    }

  private:
    bool menuDown_{}, menuSpent_{}, clickDown_{}, clickSpent_{}, dpad_{}, chord_{};
    float menuHeld_{}, startTap_{}, thumbTap_{}, chordHeld_{}, chordTap_{};
};
// What the game asks for, pressed with the VR control of the same name
// ("press what you see", October 20).
//
// The game names the button of every prompt it raises (game_prompts reads it,
// prompt_buttons spells it), and its names are an Xbox controller's: the
// virtual pad it plays with is one. Four of them are on the Touch and Index
// controllers under the very same letters, but VR play gave those other jobs:
// X opens the gadget wheel (and opened the game menu until October 23), A
// charges a leap and flips in the air, B dodges only off the swing, and the
// triggers and grips shoot webs. So
// "Press [BTN_X] for Vent Takedown" opened the map, and "Hold [BTN_L2] and tap
// [BTN_R1] to fire webs and seal the leaks" could not be answered at all. One
// player's whole playthrough (October 14-19: the Oscorp research stations, the
// Shocker bank's columns, the museum's takedowns, the Web Bomb's lesson, the
// hostage's "aim, and RB to disarm") came down to that; the game screen did
// every one of them, because there the controllers are an Xbox controller.
//
// So while the game names a button, the control with its name sends it:
//   A, B, X, Y         the button itself, as long as it is held - no gadget
//                      wheel, no leap or flip, no dodge held back for the swing
//   LB + RB            X, where the game names no X of its own (its throw by
//                      every trash can, its pull of a door; October 24)
//   LT or LB, RT or RB each hand's web-ball button (the trigger, or the grip
//                      with WEB BUTTON: TRIGGER), the left hand's the left
//                      ones, a bumper only where it is named alone: the
//                      swing's webs stay where they are, and a mission that
//                      asks for its triggers still swings
//   L3, R3             each thumbstick's click
//   D-pad up, left,    the right thumbstick pushed that way (pushed down it is
//   right              the heal's D-pad down already: StickPush)
// and while it names X, a fist thrown fast is its X (a takedown, an attack;
// held, where the prompt says hold: the Air Launch), and with X and A named in
// the air a fist thrown down is both (the Ground Strike).
//
// The Ground Strike also needs no prompt: both free fists thrown down together
// in the air are its X and A, held. Its hint shows once a save, and hantop's
// Shaking Harlem (October 22) asked for Ground Strikes with none on screen.
// Two hands at once (chosen that day), so no single punch makes one; not
// while REALISTIC FLIPS turns the player by his arms' swings (Input::slams),
// where both arms swung down is a flip. Nor out of a fight (Input::slams),
// nor from a hand that let go of its web a moment ago (letGoSeconds): 0.5.0's
// players pulled both webs down hard to climb and were dashed to the ground
// mid-swing (October 25-26, "Can't Swing").
//
// A control is the game's only when it goes down while the game names it, and
// it stays the game's, sending what it began with, until it is let go: a web
// button held into a prompt still holds its web, and a prompt that ends under
// the thumb still gets its button. Out of VR play nothing is claimed, and a
// control held through that must be let go first.
class NamedPress {
  public:
    // A trigger or a grip: down past `pressed`, up again below `released`.
    // The right thumbstick: pushed `pushed` far and no more than `aside` off
    // the way, back once within `rest`.
    static constexpr float pressed = .6f, released = .3f, pushed = .7f, aside = .45f, rest = .5f;
    // A fist thrown: a free hand at blowSpeed (the punch's own least:
    // PunchConfig::minSpeed), measured over about `smoothing`, and not pulled
    // back toward the head as squarely as `inward` (the cosine between its way
    // and the line to the head): a jab and an uppercut both, the recoil after
    // either neither. Ready again below rearmSpeed or after rearmSeconds.
    // Thrown down this steeply (the sine of its pitch) it is a slam. A blow
    // sends its buttons for tapSeconds, or holdSeconds for a hold or two at once.
    static constexpr float blowSpeed = 2.2f, rearmSpeed = 1.2f, rearmSeconds = .45f, smoothing = .01f,
                           inward = .7f, steep = .6f, tapSeconds = .15f, holdSeconds = .6f;
    // Both fists thrown down within this of each other in the air: a slam.
    static constexpr float slamSeconds = .25f;
    // A hand whose web let go this recently throws no blow: the pull on it is
    // still in the arm.
    static constexpr float letGoSeconds = .4f;
    // The VR controls a named button can claim, a bit each.
    enum Control : uint32_t {
        controlA = 1u << 0,
        controlB = 1u << 1,
        controlX = 1u << 2,
        controlY = 1u << 3,
        leftBall = 1u << 4,
        rightBall = 1u << 5,
        leftClick = 1u << 6,
        rightClick = 1u << 7,
        stickUp = 1u << 8,
        stickLeft = 1u << 9,
        stickRight = 1u << 10,
        fist = 1u << 11,
    };
    // Each hand's web-ball button: 0 the left, 1 the right.
    static constexpr uint32_t ball(unsigned hand) {
        return hand ? rightBall : leftBall;
    }
    struct Input {
        uint32_t buttons{}; // XrFrame::buttons
        // Each hand's web-ball button (Input::HandInput::trigger before the
        // swing sees it), 0 to 1, and whether a gesture has it (the temple's
        // scan, the back pocket's draw): one that has is no press here.
        float balls[2]{};
        bool ballsTaken[2]{};
        bool clicks[2]{};    // each thumbstick's click
        float stickX{}, stickY{}; // the right thumbstick, -1..1, y up
        // Each grip from the head, in tracking space (y up), whether it is
        // tracked, and whether its web holds something (no blow from it).
        Vec3 hands[2]{};
        bool tracked[2]{}, busy[2]{};
        bool airborne{};
        // Both fists thrown down in the air are the Ground Strike's (in a
        // fight, and not where they are a realistic flip).
        bool slams = true;
    };
    // seconds: since the previous update. play: the controllers play VR, the
    // pad layer off. demand: what the game names now, {} for nothing. Returns
    // what the claimed controls send the game.
    State update(float seconds, bool play, const Input& in, prompt_buttons::Demand demand) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        uint32_t down = 0;
        const std::pair<uint32_t, uint32_t> faces[] = {
            {buttonA, controlA}, {buttonB, controlB}, {buttonX, controlX}, {buttonY, controlY}};
        for (const auto& [button, control] : faces)
            if (in.buttons & button)
                down |= control;
        for (unsigned h = 0; h < 2; ++h) {
            const uint32_t ball = h ? rightBall : leftBall;
            const float value = std::isfinite(in.balls[h]) ? in.balls[h] : 0.f;
            if (!in.ballsTaken[h] && value > ((down_ & ball) ? released : pressed))
                down |= ball;
            if (in.clicks[h])
                down |= h ? rightClick : leftClick;
        }
        down |= stickWay(in.stickX, in.stickY);
        const uint32_t pressedNow = down & ~down_;
        down_ = down;
        if (!play) {
            claimed_ = 0;
            fistLeft_ = 0;
            for (auto& hand : hands_)
                hand = {};
            return {};
        }
        // A claim lasts as long as its control is held.
        claimed_ &= down;
        for (unsigned c = 0; c < buttonControls; ++c) {
            const uint32_t bit = 1u << c;
            if (!(pressedNow & bit))
                continue;
            const State s = sends(bit, demand);
            if (s == State{})
                continue;
            claimed_ |= bit;
            sent_[c] = s;
            ++presses_;
        }
        throwFists(dt, in, demand);
        State out;
        for (unsigned c = 0; c < buttonControls; ++c)
            if (claimed_ & (1u << c))
                add(out, sent_[c]);
        if (fistLeft_ > 0) {
            claimed_ |= fist;
            add(out, fistSent_);
        }
        return out;
    }
    // The controls the game has now: Spidy's own use of them waits until they
    // are let go.
    uint32_t claimed() const {
        return claimed_;
    }
    // Presses that went to a named button so far, thrown fists among them.
    uint32_t presses() const {
        return presses_;
    }
    uint32_t blows() const {
        return blows_;
    }
    // Slams of both fists in the air (the Ground Strike with no prompt), among blows.
    uint32_t slams() const {
        return slams_;
    }
    // What `control` sends while the game names what `demand` holds: {} where
    // the game names nothing of its.
    static State sends(uint32_t control, prompt_buttons::Demand d) {
        State s;
        switch (control) {
        case controlA:
            s.buttons = static_cast<uint16_t>(d.buttons & a);
            break;
        case controlB:
            s.buttons = static_cast<uint16_t>(d.buttons & b);
            break;
        case controlX:
            // Its own X first; else the two bumpers together, which have no
            // control of their own in VR play.
            if (d.buttons & x)
                s.buttons = x;
            else if ((d.buttons & bumpers) == bumpers)
                s.buttons = bumpers;
            break;
        case controlY:
            s.buttons = static_cast<uint16_t>(d.buttons & y);
            break;
        // A web-ball button is its side's bumper only while the game names
        // that bumper alone: the pair is X's, and every trash can on a street
        // names the pair (the throw's "[ACTION_WEB_YANK] (Hold) Throw").
        case leftBall:
            if (d.leftTrigger)
                s.leftTrigger = 255;
            else if ((d.buttons & bumpers) == leftShoulder)
                s.buttons = leftShoulder;
            break;
        case rightBall:
            if (d.rightTrigger)
                s.rightTrigger = 255;
            else if ((d.buttons & bumpers) == rightShoulder)
                s.buttons = rightShoulder;
            break;
        case leftClick:
            s.buttons = static_cast<uint16_t>(d.buttons & leftThumb);
            break;
        case rightClick:
            s.buttons = static_cast<uint16_t>(d.buttons & rightThumb);
            break;
        case stickUp:
            s.buttons = static_cast<uint16_t>(d.buttons & dpadUp);
            break;
        case stickLeft:
            s.buttons = static_cast<uint16_t>(d.buttons & dpadLeft);
            break;
        case stickRight:
            s.buttons = static_cast<uint16_t>(d.buttons & dpadRight);
            break;
        default:
            break;
        }
        return s;
    }

  private:
    // The controls with a button of their own (all but the fist).
    static constexpr unsigned buttonControls = 11;
    static void add(State& to, const State& s) {
        to.buttons |= s.buttons;
        to.leftTrigger = std::max(to.leftTrigger, s.leftTrigger);
        to.rightTrigger = std::max(to.rightTrigger, s.rightTrigger);
    }
    // The way the right thumbstick is pushed, held until it is back at rest.
    uint32_t stickWay(float x, float y) const {
        x = std::isfinite(x) ? x : 0.f;
        y = std::isfinite(y) ? y : 0.f;
        if (std::hypot(x, y) < rest)
            return 0;
        if (const uint32_t was = down_ & (stickUp | stickLeft | stickRight))
            return was;
        if (std::abs(x) >= pushed && std::abs(y) <= aside)
            return x > 0 ? stickRight : stickLeft;
        if (y >= pushed && std::abs(x) <= aside)
            return stickUp;
        return 0;
    }
    void throwFists(float dt, const Input& in, prompt_buttons::Demand d) {
        fistLeft_ = std::max(0.f, fistLeft_ - dt);
        const bool askedX = (d.buttons & x) != 0;
        // X and A named in the air: the Ground Strike, a fist thrown down.
        const bool strike = askedX && (d.buttons & a) && in.airborne;
        for (unsigned h = 0; h < 2; ++h) {
            auto& hand = hands_[h];
            if (!in.tracked[h] || !finite(in.hands[h]) || dt <= 0) {
                hand = {};
                continue;
            }
            if (hand.seen) {
                const Vec3 velocity = (in.hands[h] - hand.at) * (1 / dt);
                hand.velocity = hand.velocity + (velocity - hand.velocity) * std::min(1.f, dt / smoothing);
            }
            hand.at = in.hands[h];
            hand.seen = true;
            hand.since += dt;
            hand.down += dt;
            hand.free = in.busy[h] ? 0.f : hand.free + dt;
            const float speed = length(hand.velocity);
            if (speed < rearmSpeed || hand.since >= rearmSeconds)
                hand.armed = true;
            if (hand.free < letGoSeconds || !hand.armed || speed < blowSpeed)
                continue;
            const bool thrownDown = hand.velocity.y <= -steep * speed;
            // Half a slam, whatever the game names.
            if (thrownDown && in.airborne)
                hand.down = 0;
            if (!askedX)
                continue;
            const float reach = length(hand.at);
            const bool pulledIn = reach > 1e-3f && -dot(hand.velocity, hand.at) >= inward * speed * reach;
            if (strike ? !thrownDown : pulledIn)
                continue;
            hand.armed = false;
            hand.since = 0;
            fistSent_ = {};
            fistSent_.buttons = strike ? static_cast<uint16_t>(x | a) : static_cast<uint16_t>(x);
            fistLeft_ = strike || d.hold ? holdSeconds : tapSeconds;
            ++blows_;
            ++presses_;
        }
        // Both fists down together in the air: the game's X and A, held, as
        // its Ground Strike asks; one blow, both hands spent.
        if (in.slams && in.airborne && fistLeft_ <= 0 && hands_[0].down < slamSeconds &&
            hands_[1].down < slamSeconds) {
            for (auto& hand : hands_) {
                hand.armed = false;
                hand.since = 0;
                hand.down = slamSeconds;
            }
            fistSent_ = {};
            fistSent_.buttons = static_cast<uint16_t>(x | a);
            fistLeft_ = holdSeconds;
            ++slams_;
            ++blows_;
            ++presses_;
        }
    }
    struct Hand {
        Vec3 at{}, velocity{};
        // since: its latest blow; down: since it was last thrown down in the
        // air; free: since its web let go.
        float since = rearmSeconds, down = slamSeconds, free = letGoSeconds;
        bool seen{}, armed = true;
    };
    std::array<Hand, 2> hands_{};
    State sent_[buttonControls]{}, fistSent_{};
    uint32_t down_{}, claimed_{}, presses_{}, blows_{}, slams_{};
    float fistLeft_{};
};
// Handing the player back to the game for an interact.
//
// Measured in the game on October 13, at the closed vent of the first mission
// (profile 3, straight up the Fisk Tower elevator shaft). The vent asks
// through POIInteractAvailable(5, 1, '') and its text is "[BTN_Y] Enter", so
// the demand reads as the pad's Y and the press lands: sent at 153188 ms,
// taken at 153203. Then, standing still at it with the interact live, the
// probe's A alone - no stick, no drift:
//     54797  >> the probe's a
//     54860  POIInteractAvailable(5, 0, '[BTN_Y] Enter')
// The game took its own interact away 63 ms after the player left the
// surface. It never ignored the button; there was no interact left to press.
//
// The swing's wall running drives the hero in the game's AIR state - that is
// how it moves him, and probe_wall_run asserts exactly that - so while Spidy
// holds a player on a wall the game offers nothing, and no button can reach
// it: not the interact, not the pad layer, not the prompt's own demand. That
// is "cant use Vents", and it was never about which button.
//
// So the press hands him back. While the player asks to interact on a wall of
// the swing's own and the game is offering nothing, the walls go off for a
// moment: Swing::allowWalls(false) leaves the wall it holds, the game's own
// crawl takes him where he stands, its interact comes back, and the button it
// names is already down. The walls come back when the moment ends, however it
// went.
//
// Nothing else changes. The hand-over needs a press of the interact, the
// swing's own wall under the player, and the game offering nothing - and in
// exactly that case the press does nothing at all today, because there is
// nothing to press. Where the game IS offering something the ordinary path
// already works and is left alone.
class WallHandover {
  public:
    // Long enough for the game to offer the interact again (it withdrew one in
    // 63 ms) and for the press to land on it; short enough that a press at
    // nothing costs a moment of the game's own crawl and no more.
    static constexpr float window = .9f, least = .25f;
    // `asking`: the interact is pressed (Walk::interact). `onOwnWall`: the
    // swing holds the player on a wall of its own (game_swing::Surface::wall).
    // `offered`: the game is already offering an interact. Returns whether the
    // swing's walls go off this frame.
    bool update(float seconds, bool asking, bool onOwnWall, bool offered) {
        // Since the previous update, as every other rule here takes it, not a
        // clock: see the note in Enclosure for what taking it for one cost.
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        const bool rising = asking && !asked_;
        asked_ = asking;
        if (handing_) {
            heldFor_ += dt;
            if (heldFor_ >= window || (!asking && heldFor_ >= least))
                handing_ = false;
        } else if (rising && onOwnWall && !offered) {
            handing_ = true;
            heldFor_ = 0;
            offerSeen_ = false;
            ++handovers_;
        }
        // The game offering something while the player is handed back is the
        // hand-over having worked: its crawl took him and its interact came
        // back, with the button already down. Counted once for each.
        if (handing_ && offered && !offerSeen_) {
            offerSeen_ = true;
            ++offered_;
        }
        return handing_;
    }
    bool handing() const {
        return handing_;
    }
    // Presses that handed the player back to the game.
    uint32_t handovers() const {
        return handovers_;
    }
    // Those of them the game then offered an interact in: the hand-over
    // worked, and the two counts apart say whether it is worth its moment.
    uint32_t offered() const {
        return offered_;
    }

  private:
    bool handing_{}, asked_{}, offerSeen_{};
    float heldFor_{};
    uint32_t handovers_{}, offered_{};
};
// What the pad serves the game this frame.
//
// `viewing`: the session has the headset and the focus. `headSeen`: it has
// given a tracked head pose at least once. Until it has, nothing Spidy draws
// has reached the player's eyes - the game's screen is not in the headset yet -
// so a button they press is a button pressed blind, and it must not reach the
// game.
//
// October 13 is why. A session ran 414 frames with `tracked` at 0, the headset
// never tracking once while it was being put on; the player's Touch B went out
// three times and his A twice, as the pad's B and A, into the title screen -
// where QUIT GAME is two moves from the top. The game saved and closed, 20
// seconds after it started ("Meet crashed on game load"). The report had it
// exactly: `tracked` 0 in all 148 samples, `vr_buttons` ['b'] then ['a'],
// `pad_buttons` 0x2000 then 0x1000, the last one four frames before the end.
//
// After that first tracked pose this is what it always was, so a session that
// reaches the headset at all is unchanged.
inline Mapping mappingFor(bool viewing, bool headSeen, bool gameScreen, bool padLatched) {
    if (!viewing || !headSeen)
        return Mapping::none;
    if (gameScreen)
        return Mapping::menus;
    return padLatched ? Mapping::pad : Mapping::gameplay;
}
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
    // The Steam Frame's View button and D-pad are the game's own wherever
    // they are pressed; no other controller sets them.
    const std::pair<ControllerButton, Button> own[] = {{buttonView, back},
                                                       {buttonDpadUp, dpadUp},
                                                       {buttonDpadDown, dpadDown},
                                                       {buttonDpadLeft, dpadLeft},
                                                       {buttonDpadRight, dpadRight}};
    for (const auto& [held, button] : own)
        if (frame.buttons & held)
            s.buttons |= button;
    if (mapping == Mapping::gameplay) {
        if (walk.jump)
            s.buttons |= a;
        if (walk.dodge)
            s.buttons |= b;
        // A pull's pair goes in place of the interact's Y; the game's Y does
        // something else in a fight, so it does not go out beside it.
        if (walk.demand.any()) {
            s.buttons |= walk.demand.buttons;
            if (walk.demand.leftTrigger)
                s.leftTrigger = 255;
            if (walk.demand.rightTrigger)
                s.rightTrigger = 255;
        } else if (walk.interact) {
            s.buttons |= y;
        }
        if (walk.pull)
            s.buttons |= leftShoulder | rightShoulder;
        if (walk.heal)
            s.buttons |= dpadDown;
        if (walk.zip)
            s.leftTrigger = s.rightTrigger = 255;
        if (walk.aim)
            s.leftTrigger = 255;
        if (walk.parkour)
            s.rightTrigger = 255;
        if (walk.scan)
            s.buttons |= rightThumb;
        if (walk.suit)
            s.buttons |= leftThumb | rightThumb;
        if (walk.menu)
            s.buttons |= back;
        s.thumbLX = stick(walk.right);
        s.thumbLY = stick(walk.forward);
        s.thumbRX = stick(walk.look);
        s.thumbRY = stick(walk.lookUp);
        // A stick a prompt asks to be pushed (a stick-motion QTE), where the
        // player is not pushing it themselves: theirs goes first.
        const auto asked = [&](int8_t way, int16_t own) {
            return way && !own ? static_cast<int16_t>(way > 0 ? 32767 : -32767) : own;
        };
        s.thumbLX = asked(walk.demand.leftX, s.thumbLX);
        s.thumbLY = asked(walk.demand.leftY, s.thumbLY);
        s.thumbRX = asked(walk.demand.rightX, s.thumbRX);
        s.thumbRY = asked(walk.demand.rightY, s.thumbRY);
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
    // In the pad layer the left stick walks as the swing leaves it, and the
    // right stick is the game's own with a decisive push of it the game's
    // D-pad as well: no VR controller but the Steam Frame's has one, and the
    // gadget wheel and some of the game's menus want it.
    const bool padLayer = mapping == Mapping::pad;
    if (padLayer) {
        s.thumbLX = stick(walk.right);
        s.thumbLY = stick(walk.forward);
        constexpr float decisive = .7f;
        const float rx = right.valid && std::isfinite(right.stickX) ? right.stickX : 0.f;
        const float ry = right.valid && std::isfinite(right.stickY) ? right.stickY : 0.f;
        if (std::abs(rx) >= decisive && std::abs(rx) > std::abs(ry))
            s.buttons |= rx > 0 ? dpadRight : dpadLeft;
        else if (std::abs(ry) >= decisive)
            s.buttons |= ry > 0 ? dpadUp : dpadDown;
    }
    // Clicking both sticks is the layer's own chord (it lets go of the layer),
    // so only one at a time is a thumb button there.
    const bool chord = padLayer && left.stickClick && right.stickClick;
    const std::pair<bool, Button> pressed[] = {
        {(frame.buttons & buttonA) != 0, a}, {(frame.buttons & buttonB) != 0, b},
        {(frame.buttons & buttonX) != 0, x}, {(frame.buttons & buttonY) != 0, y},
        {left.squeeze > .6f, leftShoulder},  {right.squeeze > .6f, rightShoulder},
        {left.stickClick && !chord, leftThumb}, {right.stickClick && !chord, rightThumb}};
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
