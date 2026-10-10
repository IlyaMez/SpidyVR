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
    // The player stands on the surface that holds them (SurfaceHold): the
    // tracking space turns onto it; and how far it leans from the world's up
    // now, radians (0 level, a quarter turn on a wall).
    bool standing{};
    float viewTilt{};
};
// The wall or ceiling that holds the player, as the swing module reports it
// (game_swing::Data's wall): the swing's own wall, or the game's wall crawl.
// The XR worker passes the swing's own walls only: how the game's crawl takes
// the stick is not measured yet, and a view turned onto a wall wants the
// stick to go where the player looks along it.
struct SurfaceHold {
    Vec3 normal{}; // out of the surface, unit; zero: none holds the player
    Vec3 anchor{}; // the point of it under the player's body, in the world
    float speed{}; // the player's speed, m/s
    bool crawl{};  // the game's wall crawl: its actor's feet are on the surface
};
// Standing on a wall (the STAND ON WALLS setting). Once the player walks the
// surface that holds them (the left stick tilted, slower than wallStrideSpeed:
// the swing's walk is 6 m/s), or has come to rest on it (slower than
// wallStandSpeed; in the game's crawl at once), the tracking space turns so
// that the surface is its floor: up along the surface's normal, the floor
// under the player's body on the surface, a quarter turn in 0.15 s
// (wallTurnRate, radians a second). Players asked for it near instant:
// looking up a wall they stick to strains the neck, and a slow turn of the
// world is the uncomfortable kind. It stays so on the wall, round its
// corners included, until the surface lets go; then it turns back level as
// fast, and keeps the heading it has. A run along a wall never turns it.
// At rest only, a player who walked up a wall kept the upright view until
// they let go of the stick (October 10 headset report).
constexpr float wallStandSpeed = 1.5f, wallStrideSpeed = 7.5f, wallTurnRate = 10.5f;
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
// A flip: in the air the left stick turns the player head over heels, the
// tracking space tilting about the head (the eyes stay where they are): ahead
// pitches forward (a front flip), back a backflip, to a side a cartwheel, as
// fast as the stick is tilted. Let go, the player turns back level the short
// way; pushed again on the way, the stick takes the turn over where it is. A
// stick held into the air (a running jump) moves the player until it is let
// go once there. A held in the air flips with the stick at once, held or not
// from the ground, and keeps the angle while the stick rests; let go of A
// and the stick, the player turns back level. A tap (let go within
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
    // On the ground, a wall or a ceiling for landingSeconds: landed. speed()
    // makes all three turns as much faster or slower.
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
    // The left stick (or A held in the air) turns the flip: the stick moves
    // the player no more.
    bool steering() const {
        return phase_ == Phase::holding;
    }
    // Radians turned since the press, all ways.
    float turned() const {
        return turned_;
    }
    // How fast the stick turns the player at full tilt, radians a second (the
    // FLIP SPEED setting), from a quarter to four times the holdTurnSeconds
    // turn; a tap's flip and the way back level go as much faster or slower.
    // A turn in holdTurnSeconds until set; reset() keeps it.
    void speed(float radiansPerSecond);
    // Level at once (a menu, a recenter, another player). A held A stays held:
    // it flips again only once let go and pressed in the air; a held stick
    // flips again only once let go in the air.
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
    float scale_ = 1; // speed(): the turns' speed over the constants'
    bool held_{}, fromAir_{};
    // The stick has been at rest in the air since the landing (it flips when
    // pushed), and this hold began with a press of A (let go soon: a tap).
    bool stickFree_{}, tap_{};
};
// Converts one complete predicted tracking sample into world-space head, eyes,
// hand aims, and physical swing input. The game adapter still owns collision and
// movement, and must only submit while its gameplay/lifetime gate is valid.
class GameTrackingRig {
  public:
    // surfaceUp: the up of the player's actor, the world's while it stands.
    // airborne: the player is in the air, so A or the left stick flips now
    // (FlipMotion). surface: the wall or ceiling that holds the player.
    GameMotionFrame update(const XrFrame&, Vec3 playerFeet, Vec3 gameForward, bool gameplay,
                           Vec3 surfaceUp = {0, 1, 0}, bool airborne = false, const SurfaceHold& surface = {});
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
    // Whether A and the left stick in the air flip the player (FlipMotion;
    // the experimental FLIPS setting); off until set, when A there does
    // nothing and the stick moves the player. Switched off mid-flip, the
    // player is level at once. reset() keeps it.
    void flips(bool on) {
        flips_ = on;
    }
    // How fast a flip turns the player at full tilt of the left stick,
    // radians a second (FlipMotion::speed; the FLIP SPEED setting). reset()
    // keeps it.
    void flipSpeed(float radiansPerSecond) {
        flip_.speed(radiansPerSecond);
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
    // Whether the view turns onto a wall the player stands on (the STAND ON
    // WALLS setting); on until set. Off, it stays upright: beside the swing's
    // wall, stood off the game's crawl. Switched off on a wall, the view turns
    // back level. reset() keeps it.
    void standOnWalls(bool on) {
        standOnWalls_ = on;
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
    // Standing on a wall: the turn of the level tracking space about the
    // player's feet, in the world's axes, and the way its floor has gone
    // from the feet onto the surface.
    Quat surfaceTurn_{};
    Vec3 surfaceShift_{};
    bool standing_{}, standOnWalls_ = true;
};
} // namespace spidy
