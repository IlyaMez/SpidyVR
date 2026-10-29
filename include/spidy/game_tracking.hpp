#pragma once
#include "head_clearance.hpp"
#include "hand_collision.hpp"
#include "native_rays.hpp"
#include "native_view.hpp"
#include "realistic_flip.hpp"
#include "swing.hpp"
#include "tracking.hpp"
#include "wall_roll.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace spidy {
// triggerWebs: the controllers' triggers are the hands' web buttons (Input's
// grip) and their grips the reels (Input's trigger), swapped from the default.
Input trackedSwingInput(const XrFrame& frame, const Rig& rig, bool triggerWebs = false);
native_rays::Command controllerAimRays(const Input&, uint64_t serial);
// Keeps both aiming rays and the roof probe. During hand contact, body-to-eye
// and forward head probes run every frame; side/up probes share one slot.
void appendTrackingRays(native_rays::Command&, const std::array<HeadClearance::Ray, 5>& head,
                        const std::array<native_rays::Ray, 2>& hands, native_rays::Ray roof,
                        unsigned phase);
struct GameMotionFrame {
    bool active{}, releaseWebs = true;
    bool controlledFlipControls{}; // A-toggled left-stick mode, separate from basic right-stick flips
    bool basicFlipControls{}; // airborne basic flips own the right stick, except prioritized zip/aim
    // Why the frame could not be placed (GameTrackingRig::Inactive bits;
    // zero while active).
    uint32_t inactive{};
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
    // Where the eyes look, in the world (XrFrame::gaze), while tracked.
    Pose gazePose{};
    bool gazeValid{};
    std::array<EyeFov, 2> fovs{};
    uint32_t nativeKeys{};
    // The left stick in the stock camera's horizontal axes, -1..1: native
    // walking on the virtual Xbox controller, where nativeKeys has W/A/S/D.
    float walkRight{}, walkForward{};
    // The axes those two were measured along, in the world. The game camera's
    // forward and right, level - or laid into the surface, while the game's
    // own crawl holds the player on one and the view has turned onto it.
    Vec3 walkForwardAxis{}, walkRightAxis{};
    Vec3 anchor{}; // Player feet that placed this frame's head, eyes, and hands.
    // How far the head was brought down to keep it under the surface over the
    // player, metres (zero where nothing is over him).
    float headDrop{};
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
    bool joystickWallRun{}; // locomotion, independent of the optional camera transition
    float wallRoll{};
    bool wallRotate{};
    bool ventCrawl{};
    float viewTilt{};
    // For the player's body: the up the view stands him along (the world's
    // until it turns onto a surface), the point of that surface under his
    // feet, from the feet (zero on the ground), and a flip's tilt alone, in
    // the tracking space's axes as swing.tilt is (swing.tilt also carries
    // the surface's turn).
    Vec3 standUp{0, 1, 0}, floor{};
    Quat flipTilt{};
    // How far the eyes, and the hands and the body placed with them, moved
    // out of what the world is built of (HeadClearance), metres in the world.
    Vec3 headPush{};
    Vec3 handPush{}; // Camera placement from pressing solid building surfaces.
    unsigned wallTouches{}; // Ray-confirmed palm contact/pressure, independent of grip buttons.
    bool climbTracking{};
    std::array<Vec3, 2> climbGrips{}; // tracking metres relative to the head at hold entry
    std::array<float, 2> climbHold{};
    Quat bodyTilt(bool runningPose = false) const {
        // The travel-driven wall-run gait stays in the wall frame. The camera
        // may stand on that wall, but it must not select a different body pose.
        return runningPose ? Quat{} : standing ? flipTilt : swing.tilt;
    }
};
// Only wall-climb locomotion/stride cues use the fixed tracking reference.
// Web, punch and other head-relative gestures keep frame.swing unchanged.
inline Input wallClimbInput(const GameMotionFrame& frame) {
    Input result = frame.swing;
    if (frame.climbTracking)
        for (unsigned i = 0; i < 2; ++i) {
            result.hands[i].gripRelativeToHead = frame.climbGrips[i];
            result.hands[i].wallGrip = frame.climbHold[i];
            result.hands[i].wallPosition = frame.grips[i].position;
        }
    return result;
}
// The wall or ceiling that holds the player, as the swing module reports it
// (game_swing::Data's wall): the swing's own wall, or the game's wall crawl.
//
// Both of them, since October 14. The crawl was held back while how it takes
// the stick went unmeasured, and that left the one state whose own interacts
// work on a wall - the crawl, the only one the game calls supported there -
// looking nothing like the wall walking the player asked to keep ("i like how
// it feel to walk on building and stand on their side"), so a vent cost him
// the view. The stick's own axes are laid into the surface for it below, and
// CrawlStick (crawl_stick.hpp) measures what the game does with them.
struct SurfaceHold {
    Vec3 normal{}; // out of the surface, unit; zero: none holds the player
    Vec3 anchor{}; // the point of it under the player's body, in the world
    float speed{}; // the player's speed, m/s
    bool crawl{};  // the game's wall crawl: its actor's feet are on the surface
    bool vent{};   // enclosed native crawl: never switch into wall running
};
// A grounded dodge can briefly report the same native pose flags as a duct
// crawl. Do not let that animation acquire a prone camera or a crawl cap.
class CrawlCameraGate {
  public:
    SurfaceHold update(float seconds, bool enabled, bool dodge, bool grounded, SurfaceHold candidate) {
        if (!enabled) {
            *this = {};
            return candidate;
        }
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        if (dodge && !vent_ && (grounded || groundedBefore_))
            dodgeSeconds_ = .9f;
        else
            dodgeSeconds_ = std::max(0.f, dodgeSeconds_ - dt);
        groundedBefore_ = grounded && !vent_;
        if (dodgeSeconds_ > 0 && candidate.crawl)
            candidate = {};
        // Unconfirmed native floor flags also occur while turning/running.
        // They must not start the vent camera or its separate head-height cap.
        if (candidate.crawl && !candidate.vent && normalized(candidate.normal).y > .866f)
            candidate = {};
        vent_ = candidate.vent;
        return candidate;
    }
  private:
    float dodgeSeconds_{};
    bool groundedBefore_{}, vent_{};
};
// The native collision capsule's centre is clear even when a projecting
// pillar covers the point half a metre off the facade used by the crawl.
inline Vec3 headCollisionOrigin(Vec3 feet, Vec3 bodyUp, const SurfaceHold& surface, bool physicalClimb) {
    const Vec3 n = normalized(surface.normal);
    const bool holds = finite(n) && length(n) > .5f && finite(surface.anchor);
    if (holds && !(physicalClimb && std::abs(n.y) < .5f))
        return surface.anchor + n * .5f;
    return feet + (finite(bodyUp) && length(bodyUp) > .5f ? normalized(bodyUp) : Vec3{0, 1, 0});
}
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
// The head never goes through what is over the player. A ray from his feet
// along the surface he stands on says how far there is, and the head stops
// headClearance short of that, never closer to the feet than leastHeadroom -
// a crouch, which is what a duct is. Inside the first mission's vent the head
// stood at the player's own eye height over its floor and so out through its
// roof (the player, October 14: "the camera clips through the top of it when
// inside"). Measured along the surface's own normal, so it is one rule on a
// wall, on a ceiling and on the ground; `headroomReach` is as far as the ray
// bothers to look, past which nothing is over him that matters.
constexpr float headClearance = .2f, leastHeadroom = .5f, headroomReach = 3.f;
// The game's crawl on a floor is a vent or a duct: the game will not stand
// the hero up there, so neither does the view. Once it has crawled him on the
// floor for crawlSettleSeconds (its crawl turning him over a wall's top onto
// a roof stands him up within 0.3 s) the head goes no higher than a crouch,
// crawlRoom less headClearance over the floor, whatever a ray finds over it:
// part of the first mission's duct has no roof the rays can hit, and the head
// stood out through it at the player's own eye height (October 14, 19:57).
constexpr float crawlRoom = 1.2f, crawlSettleSeconds = .4f;
// That crawl is only a duct under a duct's roof, though. The game sets the
// same flag (0x8001, standing) for a hero ducking under anything low: in the
// user's session of October 24 it came on for seconds at a time on a rooftop
// under a construction frame's beams 1.7 m up, and in bursts on the street,
// and the cap put the view at a crouch and knelt the body there ("walking
// around on the street sucks ... constantly getting stuck"). So the cap waits
// for a roof seen no higher than ductRoof over the feet, and once one has
// been it holds for the rest of that crawl, through a part of a duct the rays
// miss. Elsewhere the ray's own roof (headClearance under it) is the only
// limit, as it is on foot.
constexpr float ductRoof = 1.5f;
class FloorCrawlRoof {
  public:
    // crawling: the game's crawl on a floor this frame. seconds: since the
    // last. roof: how far the head's ray found the surface over the feet (0
    // for none). Returns the height the head may reach in the crawl, 0 for
    // no cap.
    float update(bool crawling, float seconds, float roof) {
        if (!crawling) {
            *this = {};
            return 0;
        }
        held_ += std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        duct_ = duct_ || (std::isfinite(roof) && roof > 0 && roof <= ductRoof);
        return held_ >= crawlSettleSeconds && duct_ ? crawlRoom : 0.f;
    }
    // The crawl has had a duct's roof over it.
    bool duct() const {
        return duct_;
    }

  private:
    float held_{};
    bool duct_{};
};
// On the game's crawl the stick goes along the game camera's own look laid
// onto the surface; where less than crawlLook of that look lies along the
// surface (the camera faces it), along the camera's up laid onto it instead.
constexpr float crawlLook = .2f;
// A break in play this short keeps the view standing where it stood
// (GameTrackingRig::update); a floor that moves under a player the view has
// turned onto it follows him in surfaceGlideSeconds (63%).
constexpr float blipSeconds = .5f, surfaceGlideSeconds = .05f;
// A surface: the actor's up more than 45 degrees from the world's; standing
// again within 35 degrees.
constexpr float surfaceEnterCos = .7071068f, surfaceLeaveCos = .819152f;
// The stand-off approaches its target by 63% in standOffSeconds; the first
// surfaceSettleSeconds on a surface are the actor turning onto it.
constexpr float standOffSeconds = .06f, surfaceSettleSeconds = .5f;
// Basic FLIPS: the right stick rotates about the tracked head while airborne.
// Releasing the stick freezes the angle. A fresh A press returns upright;
// landing or surface support also returns upright. A ground jump stays a jump.
// REALISTIC FLIPS use their own controller and retain left-stick steering.
class FlipMotion {
  public:
    static constexpr float holdTurnSeconds = 1.5f, returnTurnSeconds = 1,
                           spinUpSeconds = .08f, stickDeadZone = .2f, stickFull = .9f, easeAngle = .4f,
                           slowest = .3f, landingSeconds = .08f;
    struct Sample {
        bool jump{}, airborne{}, surface{};
        bool controlled{}; // explicit A-toggle mode; false preserves the original basic flips
        float stickX{}, stickY{}; // the right stick for basic flips
        Quat head{};
        float seconds{};
        bool stickTracked = true;
    };
    Quat update(const Sample&);
    Quat tilt() const { return tilt_; }
    bool level() const { return phase_ == Phase::level; }
    bool steering() const { return phase_ == Phase::rotating; }
    bool active() const { return active_; }
    float turned() const { return turned_; }
    // FLIP SPEED also supplies the unchanged manual rate for realistic flips.
    void speed(float radiansPerSecond);
    float steeringRate() const { return 6.2831853f / holdTurnSeconds * scale_; }
    // Menus/recentering clear the tilt and require a new neutral stick.
    // Preserve the A latch so a button held across the reset is not a press.
    void reset();
    void reset(bool jump) { reset(); held_ = jump; }

  private:
    enum class Phase { level, rotating, held, settling };
    void toLevel();
    Phase phase_ = Phase::level;
    Quat tilt_{}, from_{};
    Vec3 spin_{}, axis_{-1, 0, 0};
    float total_{}, left_{}, returnRate_{}, turned_{}, landed_{};
    float scale_ = 1;
    bool held_{}, resetHeld_{}, stickReady_{}, active_{};
};
// Converts one complete predicted tracking sample into world-space head, eyes,
// hand aims, and physical swing input. The game adapter still owns collision and
// movement, and must only submit while its gameplay/lifetime gate is valid.
class GameTrackingRig {
  public:
    // gameForward and gameUp: the game camera's look and its up, as it
    // holds them (its third and second rows). surfaceUp: the up of the
    // player's actor, the world's while it stands. airborne: the player is in
    // the air, so the basic flip can use the right stick. surface: the
    // wall or ceiling that holds the player.
    GameMotionFrame update(const XrFrame&, Vec3 playerFeet, Vec3 gameForward, bool gameplay,
                           Vec3 surfaceUp = {0, 1, 0}, bool airborne = false, const SurfaceHold& surface = {},
                           Vec3 gameUp = {0, 1, 0});
    // Why a frame was not placed (GameMotionFrame::inactive).
    enum Inactive : uint32_t {
        notPlaying = 1,     // a menu, a scene, the game's own camera
        unfocused = 2,      // the headset not worn or not focused, or no frame
        noHead = 4,         // the head's pose was not tracked
        noFeet = 8,         // the player's feet were not a place
        noCamera = 16,      // the game camera had no level look before the start
        badFrameTime = 32,  // a frame of no time, or over a tenth of a second
        staleDisplay = 64,  // the headset gave a display time no later than the last
        noEyes = 128,       // an eye's pose or lens was not usable
    };
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
    // Basic FLIPS use the airborne right stick; release holds the tilt and
    // A returns upright. Off until set; disabling clears the tilt immediately.
    void flips(bool on) {
        flips_ = on;
    }
    // Controlled Flips overrides basic flips; realistic flips retains priority.
    void controlledFlips(bool on) {
        if (controlledFlips_ != on)
            flip_.reset();
        controlledFlips_ = on;
    }
    // How fast a flip turns the player at full tilt of its steering stick,
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
    // How far it is from the player's feet to the surface over them, along
    // the one he stands on, metres. Zero: nothing is, or nothing was measured,
    // and the head goes where the player's own head is.
    void headroom(float metres) {
        headroom_ = std::isfinite(metres) && metres > 0 ? metres : 0.f;
    }
    void standOnWalls(bool on) {
        standOnWalls_ = on;
    }
    // The eyes out of what the world is built of (HeadClearance). Before an
    // update: a point inside the player's body, where no wall is (the rays'
    // start), and each surface found since the last one - by the rays the
    // last clearanceRays() asked for, and the wall that holds the player
    // (held). Without a body point the eyes go where the headset puts them.
    void bodyCentre(Vec3 at) {
        bodyCentre_ = at;
    }
    void surfaceFound(HeadClearance::Surface s, bool held = false) {
        clearance_.found(s, held);
    }
    // After an update: the rays for the next batch, along the head's own
    // look, side and up.
    std::array<HeadClearance::Ray, HeadClearance::rays> clearanceRays(Vec3 look, Vec3 side, Vec3 up) const {
        const bool closeClimb = handClimb_ && !standing_ &&
                                (length(climbNormal_) > .5f || ventViewActive_);
        return clearance_.next(look, side, up, closeClimb ? .75f : 0.f);
    }
    const HeadClearance& clearance() const {
        return clearance_;
    }
    void handFeedback(const native_rays::Data& data, int64_t now, int64_t frequency) {
        handCollision_.feedback(data, now, frequency);
    }
    std::array<native_rays::Ray, 2> handCollisionRays(Vec3 head) const {
        return handCollision_.next(head);
    }
    void handClimb(bool on) {
        handClimb_ = on;
        if (!on)
            wallRoll_.reset();
    }
    // Which hands hold the wall, as the swing last had them (bit 0 the left;
    // game_swing::ClimbData): the wall roll turns the view only while both
    // do. Two grips held otherwise are webs, and the roll took both of them,
    // the second web's press and the first web with it (October 22). Zero
    // until set; reset() clears it.
    void wallHands(unsigned hands) {
        wallHands_ = hands;
    }
    void wallDistance(float metres) {
        wallDistance_ = std::isfinite(metres) ? std::clamp(metres, .1f, 1.f) : .4f;
    }
    // REALISTIC FLIPS (RealisticFlip), from the VR settings; off, the flips
    // are FlipMotion's. reset() keeps them.
    void automaticFlips(RealisticFlipSettings settings) {
        autoSettings_ = settings;
    }
    // The swing's flight for the next update: its velocity and its webs' pull.
    void flight(RealisticFlight sample) {
        flight_ = sample;
    }
    void momentumWallRun(float distance, float tilt, float bank, float lean) {
        momentumView_.distance = std::isfinite(distance) ? std::clamp(distance, .4f, 1.6f) : .95f;
        momentumView_.tilt = std::isfinite(tilt) ? std::clamp(tilt, -.523599f, .523599f) : .17453293f;
        momentumView_.bank = std::isfinite(bank) ? std::clamp(bank, 0.f, 2.747478f) : .85f;
        momentumView_.lean = std::isfinite(lean) ? std::clamp(lean, 0.f, 1.047198f) : .5f;
    }
    void wallRunCameraDistance(float metres) {
        runCameraDistance_ = std::isfinite(metres) ? std::clamp(metres, .4f, 1.6f) : .9f;
    }
    void joystickWallRunCamera(bool enabled) { joystickRunCamera_ = enabled; }
    void wallRunCameraTilt(bool enabled) { runCameraTilt_ = enabled; }
    // Actual solved body stance, not the player's momentary look direction.
    void outwardCrawl(float weight) {
        outwardCrawl_ = std::isfinite(weight) ? std::clamp(weight, 0.f, 1.f) : 0.f;
    }
    // The running avatar needs a separate camera clearance from close hand crawling.
    void wallRunBody(bool enabled, unsigned supports, Vec3 velocity) {
        runViewEnabled_ = enabled; runViewSupports_ = supports; runViewVelocity_ = velocity;
    }
    void reset();

  private:
    RealisticFlipSettings autoSettings_{};
    RealisticFlight flight_{};
    RealisticFlip automatic_{};
    AutomaticHandMotion automaticHands_{};
    bool automaticStickReady_{}, automaticBrakeHeld_{}, automaticBrakeFromAir_{};
    Rig rig_{};
    FlipMotion flip_{};
    Pose lastHead_{};
    Vec3 lastFeet_{}, standOff_{}, lastGameForward_{0, 0, -1};
    std::int64_t lastTime_{};
    bool initialized_{}, wasActive_{}, turnHeld_{}, pendingRecenter_{}, onSurface_{}, flips_{};
    bool controlledFlips_{};
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
    bool handClimb_{};
    unsigned wallHands_{};
    float runCameraDistance_ = .9f;
    struct MomentumView { float distance = .95f, tilt = .17453293f, bank = .85f, lean = .5f; } momentumView_;

    float wallDistance_ = .4f;
    float outwardCrawl_{};
    float outwardOffset_{};
    WallRollGesture wallRoll_;
    Vec3 climbNormal_{}, runNormal_{}, runAhead_{};
    Quat climbView_{};
    // Prone vent frame: neutral headset looks into native support. Its
    // orientation follows floor/wall/ceiling changes, about the placed eyes.
    Quat ventTurn_{};
    Vec3 ventForward_{0, 0, -1};
    bool ventViewActive_{};
    float runYaw_{}, runHeadYaw_{};
    bool wallRunning_{};
    bool joystickRunCamera_ = true;
    bool runViewEnabled_{}, runViewActive_{}, runCameraTilt_{};
    float runCameraBank_{};
    unsigned runViewSupports_{};
    Vec3 runViewVelocity_{}, runViewOffset_{};
    bool climbTrackingSet_{};
    Vec3 climbTrackingHead_{};
    float runIdleSeconds_{};
    float headroom_{};
    Vec3 headDrop_{};
    HeadClearance clearance_{};
    HandCollision handCollision_{};
    Vec3 bodyCentre_{std::numeric_limits<float>::quiet_NaN(), 0, 0};
};
} // namespace spidy
