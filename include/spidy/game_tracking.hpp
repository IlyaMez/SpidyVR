#pragma once
#include "native_rays.hpp"
#include "native_view.hpp"
#include "swing.hpp"
#include "tracking.hpp"
#include <algorithm>
#include <cmath>

namespace spidy {
// triggerWebs: the controllers' triggers are the hands' web buttons (Input's
// grip) and their grips the reels (Input's trigger), swapped from the default.
Input trackedSwingInput(const XrFrame& frame, const Rig& rig, bool triggerWebs = false);
native_rays::Command controllerAimRays(const Input&, uint64_t serial);
struct GameMotionFrame {
    bool active{}, releaseWebs = true;
    std::int64_t predictedDisplayTime{};
    Input swing{};
    Mat4 head{};
    std::array<Mat4, 2> eyes{};
    std::array<Pose, 2> hands{};
    // The headset and the controllers' OpenXR grip poses, in the world: the
    // player's body follows them (native_body). A hand's grip keeps the
    // identity orientation while it is not tracked.
    Pose headPose{};
    std::array<Pose, 2> grips{};
    std::array<EyeFov, 2> fovs{};
    uint32_t nativeKeys{};
    // The left stick in the stock camera's horizontal axes, -1..1: native
    // walking on the virtual Xbox controller, where nativeKeys has W/A/S/D.
    float walkRight{}, walkForward{};
    Vec3 anchor{}; // Player feet that placed this frame's head, eyes, and hands.
    // How far the head, eyes and hands stand off the wall or ceiling the game
    // holds the player on (zero while it stands), and the head's height over
    // that surface as placed from the feet, before and after (while onSurface).
    Vec3 standOff{};
    bool onSurface{};
    float surfaceHeight{}, surfaceClearance{};
};
// The game sticks the player to walls and ceilings (its wall crawl): the
// feet on the surface, the actor's up along its normal. A head placed upright
// from those feet is on the wall, or inside it. It stands off the surface
// instead: wallClearance once the actor has turned onto it, then never closer
// than minWallClearance; a head moved back stays where it went.
constexpr float wallClearance = .5f, minWallClearance = .25f;
// A surface: the actor's up more than 45 degrees from the world's; standing
// again within 35 degrees.
constexpr float surfaceEnterCos = .7071068f, surfaceLeaveCos = .819152f;
// The stand-off approaches its target by 63% in standOffSeconds; the first
// surfaceSettleSeconds on a surface are the actor turning onto it.
constexpr float standOffSeconds = .06f, surfaceSettleSeconds = .5f;
// A flip: A held in the air lets the left stick turn the player head over
// heels, the tracking space tilting about the head (the eyes stay where they
// are): ahead pitches forward (a front flip), back a backflip, to a side a
// cartwheel, as fast as the stick is tilted; at rest it holds the angle. Let
// go of A, the player turns back level the short way. A tap (let go within
// tapSeconds) is one whole flip the way the stick points, ahead with it at
// rest; pressed again during it, A takes the flip over where it is. Landing,
// or the game holding the player on a wall or a ceiling, brings it back level
// quickly. A press that starts on the ground, a wall or a perch is the game's
// jump (AirJumpFilter), never a flip.
class FlipMotion {
  public:
    // Held, the stick turns the player a whole turn in holdTurnSeconds at
    // full tilt (past stickDeadZone, full from stickFull, as smooth turning
    // does), the turn reaching 63% of what the stick asks in spinUpSeconds. A
    // tap's flip takes tapTurnSeconds a turn; let go, the player turns back
    // level at a turn in returnTurnSeconds, from a landing twice as fast. Over
    // the last easeAngle radians to level it slows, to no less than `slowest`.
    // On the ground, a wall or a ceiling for landingSeconds: landed.
    static constexpr float holdTurnSeconds = 1.5f, tapTurnSeconds = 1, returnTurnSeconds = 1, tapSeconds = .25f,
                           spinUpSeconds = .08f, stickDeadZone = .2f, stickFull = .9f, easeAngle = .4f,
                           slowest = .3f, landingSeconds = .08f;
    struct Sample {
        bool jump{};              // A is held
        bool airborne{};          // the player is in the air (game_swing::airborne)
        bool surface{};           // the game holds the player on a wall or a ceiling
        float stickX{}, stickY{}; // the left stick
        Quat head{};              // the headset in the tracking space
        float seconds{};          // since the sample before
    };
    // The tracking space's tilt after this sample (the identity: level).
    Quat update(const Sample&);
    Quat tilt() const {
        return tilt_;
    }
    bool level() const {
        return phase_ == Phase::level;
    }
    // A held in the air: the left stick turns the flip, not the player.
    bool steering() const {
        return phase_ == Phase::holding;
    }
    // Radians turned since the press, all ways.
    float turned() const {
        return turned_;
    }
    // Level at once (a menu, a recenter, another player). A held A stays held:
    // it flips again only once let go and pressed in the air.
    void reset();

  private:
    enum class Phase { level, holding, flipping, settling };
    // Starts the way back to level: flipping (round the way `way` turns, a
    // whole turn from level) or settling (the short way).
    void toLevel(Phase, Vec3 way = {});
    Phase phase_ = Phase::level;
    Quat tilt_{}, from_{};
    Vec3 spin_{};            // the stick's turn in the tracking space, radians a second
    Vec3 tapWay_{};          // the stick's way during a press (unit; zero: none)
    Vec3 axis_{-1, 0, 0};    // to level: the axis it turns about (unit)
    float total_{}, left_{}; // to level: radians in all, and those still to turn
    // To level, radians a second: a flip's speed before its slowing, the turn
    // now, and settling's.
    float speed_{}, rate_{}, returnRate_{};
    float turned_{}, landed_{}, heldFor_{};
    bool held_{}, fromAir_{};
};
// Converts one complete predicted tracking sample into world-space head, eyes,
// hand aims, and physical swing input. The game adapter still owns collision and
// movement, and must only submit while its gameplay/lifetime gate is valid.
class GameTrackingRig {
  public:
    // surfaceUp: the up of the player's actor, the world's while it stands.
    // airborne: the player is in the air, so A pressed now flips (FlipMotion).
    GameMotionFrame update(const XrFrame&, Vec3 playerFeet, Vec3 gameForward, bool gameplay,
                           Vec3 surfaceUp = {0, 1, 0}, bool airborne = false);
    // How far a flick of the right stick turns the player (0: it does not);
    // 30 degrees until set. reset() keeps it.
    void snapTurn(float radians) {
        snap_ = std::isfinite(radians) ? std::clamp(radians, 0.f, 3.1415927f) : .5235988f;
    }
    // How fast the right stick turns the player while held over, radians a
    // second at full tilt (slower tilted less); 0 until set: it snap turns
    // instead. reset() keeps it.
    void smoothTurn(float radiansPerSecond) {
        smooth_ = std::isfinite(radiansPerSecond) ? std::clamp(radiansPerSecond, 0.f, 6.2831853f) : 0.f;
    }
    // Whether A in the air flips the player (FlipMotion; the experimental
    // FLIPS setting); off until set, when A there does nothing. Switched off
    // mid-flip, the player is level at once. reset() keeps it.
    void flips(bool on) {
        flips_ = on;
    }
    // Which button webs (the WEB BUTTON setting): off, the grip shoots and
    // holds a hand's web and the trigger reels it in and shoots web balls; on,
    // the trigger webs and the grip reels. The grip until set; reset() keeps
    // it. Menus, fists and the T-pose calibration keep the buttons as they are.
    // A change lets go of both webs, as a long break does: a button held across
    // it does nothing until it is let go.
    void triggerWebs(bool on) {
        buttonsChanged_ |= on != triggerWebs_;
        triggerWebs_ = on;
    }
    void reset();

  private:
    Rig rig_{};
    FlipMotion flip_{};
    Pose lastHead_{};
    Vec3 lastFeet_{}, standOff_{};
    std::int64_t lastTime_{};
    bool initialized_{}, wasActive_{}, turnHeld_{}, pendingRecenter_{}, onSurface_{}, flips_{};
    // The trigger webs, and the web button changed since the last frame of play.
    bool triggerWebs_{}, buttonsChanged_{};
    float snap_ = .5235988f, smooth_{};
    // The stand-off wanted along the surface's normal, and the time on it.
    float surfaceDepth_{}, surfaceSeconds_{};
};
} // namespace spidy
