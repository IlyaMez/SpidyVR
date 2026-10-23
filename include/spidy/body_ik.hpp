#pragma once
#include "crawl_feet.hpp"
#include "crawl_turn.hpp"
#include "math.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The player's own body driven by the headset and controllers. Engine
// independent: it works on a character's model-space joint matrices in the
// layout the game keeps them (Spider-Man's pose writer 1601290): per joint
// four rows of four floats, the joint's x, y and z axes in model space (scale
// included) and then its position. A point v in a joint's frame is at
// v.x * row0 + v.y * row1 + v.z * row2 + row3.
//
// Every change is a rigid turn of a joint and everything under it about that
// joint, a move of the whole pose, or a scale about a point, so skinning,
// helper joints and joint scales keep what the game animated - except under
// the wrists: each hand is set back to its rest pose from the wrist and posed
// from there, so nothing the game animates reaches the fingers. Joint frames
// may be mirrored (left and right limbs often are): the solver never takes a
// joint's orientation as a rotation, only where its frame sends a direction.
// Model space is the character's: +y up, the feet at the origin.
namespace spidy::body {
// Quaternion helpers (unit quaternions turning column vectors).
Quat unit(Quat q);
Quat axisAngle(Vec3 axis, float radians);
// The shortest turn taking direction a to direction b.
Quat between(Vec3 a, Vec3 b);
// The rotation whose columns (the images of x, y and z) are these
// orthonormal, right-handed axes.
Quat fromAxes(Vec3 x, Vec3 y, Vec3 z);
// The turn taking a pair of directions (forward, up) to another pair: the
// forwards exactly, the ups as near as the forwards allow.
Quat between(Vec3 forward, Vec3 up, Vec3 toForward, Vec3 toUp);
Quat slerp(Quat a, Quat b, float t);
// The same turn, `share` of the way (0 none, 1 all of it).
inline Quat partial(Quat q, float share) {
    return slerp({}, q, share);
}
float angle(Quat q);

// A view of a pose: `count` joint matrices of 16 floats.
class Pose {
  public:
    Pose(float* matrices, int count) : m_(matrices), count_(count) {}
    int count() const {
        return count_;
    }
    Vec3 position(int joint) const;
    // Where the joint's frame sends `local` (scale included).
    Vec3 direction(int joint, Vec3 local) const;
    // The vector in the joint's frame that it sends to `direction`.
    Vec3 local(int joint, Vec3 direction) const;
    // Turns the listed joints by `q` about `pivot`.
    void turn(std::span<const int16_t> joints, Quat q, Vec3 pivot);
    void move(std::span<const int16_t> joints, Vec3 by);
    // Scales the listed joints about `pivot` (their axes and positions).
    void scale(std::span<const int16_t> joints, float s, Vec3 pivot);
    float* data() {
        return m_;
    }

  private:
    float* m_;
    int count_;
};

struct ArmJoints {
    int clavicle = -1, upper = -1, lower = -1, hand = -1;
    // A finger's base in the middle of the hand (which way the fingers go
    // from the hand joint) and the thumb's base (which side the thumb is on).
    int finger = -1, thumb = -1;
    // Optional, for a fist: each finger's joints from its base in the palm
    // to its tip, from the index finger to the little one, and the thumb's.
    // Two fingers or more also say which way the palm faces.
    std::vector<std::vector<int16_t>> fingers;
    std::vector<int16_t> thumbChain;
};
struct LegJoints {
    int upper = -1, lower = -1, foot = -1;
};
// Which joints are which, and what the rest pose says about them.
struct Rig {
    std::vector<int16_t> parent; // -1 (or the joint itself) for a root
    // The body's root, which the hips (with the legs under them) and the
    // spine hang from: it may be the hips joint itself. Moving the body moves
    // what is under it; other roots of the rig keep the game's pose.
    int pelvis = -1, head = -1;
    std::vector<int16_t> spine; // from the pelvis's child up to the chest
    std::vector<int16_t> neck;  // from the chest's child up to the head's parent
    std::array<ArmJoints, 2> arms{}; // 0 the character's left, 1 its right
    std::array<LegJoints, 2> legs{};
    // Optional: joints whose midpoint is between the eyes (the eyes).
    int eyes[2]{-1, -1};

    // Filled by prepare(), from the rest pose:
    std::vector<std::vector<int16_t>> below; // each joint and everything under it
    std::vector<int16_t> all;                // every joint
    // In the body root's and the head's own frames: the character's forward
    // and up. In each hand's frame: the fingers' direction and the palm's normal
    // (out of the palm).
    Vec3 pelvisForward{}, pelvisUp{}, headForward{}, headUp{};
    Vec3 upperChestForward{};
    std::vector<Vec3> spineForward, neckForward;
    // The legs' shared parent can be below the body root: the hero has
    // a_body -> pelvis -> both legs, with the spine beside pelvis.
    // Correcting a_body alone leaves the animated pelvis tilted beneath it.
    int hipRoot = -1;
    Vec3 hipForward{}, hipUp{};
    std::array<Vec3, 2> handFingers{}, handPalm{};
    // In each hand's frame too: across the palm, from the little finger's
    // side to the thumb's (zero when the rest pose does not say).
    std::array<Vec3, 2> handThumb{};
    // The axis each finger joint after the palm's bends about, in the frame
    // of the joint before it: a positive turn closes the finger toward the
    // palm. Per arm and finger, its first three such joints (zero where a
    // finger has fewer). The thumb's two joints after its base likewise,
    // closing it across the palm toward the little finger.
    std::array<std::vector<std::array<Vec3, 3>>, 2> fingerHinges{};
    std::array<std::array<Vec3, 2>, 2> thumbHinges{};
    // Each joint under each hand (its fingers, its thumb and whatever else
    // hangs from the wrist) as the rest pose holds it from the hand: where
    // the hand's frame sends the joint's axes, and its place. What solve()
    // sets the hand back to before it poses the fingers.
    struct HandJoint {
        int16_t joint{};
        Vec3 axes[3]{}, at{};
    };
    std::array<std::vector<HandJoint>, 2> handRest{};
    // The point between the eyes from the head joint, along the head's
    // forward, up and left, in model units; its height above the feet.
    Vec3 eyesFromHead{};
    float eyeHeight{};
    // Each shoulder joint (the upper arm's) from the point between the eyes,
    // along the character's forward, up and left, model units: where the T-pose
    // calibration (body_calibration.hpp) measures the player's arms from.
    std::array<Vec3, 2> shoulders{};
    std::array<float, 2> upperArm{}, forearm{}, thigh{}, shin{};
    // Each foot as the rest pose stands on it: the character's forward and up
    // in the foot's own frame, and the foot joint's height over the ground.
    // What a planted foot (Targets::plant) is turned back to.
    std::array<Vec3, 2> footForward{}, footUp{};
    std::array<float, 2> ankleHeight{};
    // Sole contact ahead of the ankle, measured from the foot's descendants.
    // Outdoor crawling pitches about this forefoot, with the heel off the wall.
    std::array<float, 2> forefootLength{};
    bool ready{};
};
// Builds the per-joint subtrees and the rest-pose references from a rest pose
// of the rig (model space, the layout above, standing upright). False when a
// required joint is missing or the rest pose is degenerate.
bool prepare(Rig& rig, std::span<const float> restPose);

// What the player's body should do, in model space (model units).
struct Targets {
    bool head{};
    // In the air (swinging, falling): the legs keep the game's pose instead
    // of standing on the ground.
    bool airborne{};
    // The player's up in model space: the way he stands. The world's up on
    // the ground; on a wall or a ceiling the view stands him on, that
    // surface's normal, whatever the hero's own frame is doing there (turned
    // onto it in the game's crawl, upright in the air Spidy's own wall holds
    // him in). The body always stands along it, as the player does.
    Vec3 up{0, 1, 0};
    Vec3 eyes{};   // the point between the eyes
    Quat facing{}; // the headset: +x right, +y up, -z where it looks (OpenXR)
    struct Hand {
        // Untracked, the arm stays where the hand was last tracked from the
        // body, moving and turning with it.
        bool tracked{};
        // The controller's OpenXR grip pose: its point is at the palm of the
        // closed hand; +x is normal to the palm (to the right on both
        // hands), -z runs from the little finger to the thumb, so -y points
        // from the wrist out past the knuckles.
        Vec3 grip{};
        Quat orientation{};
        // How far the hand is closed into a fist, 0 (this body's own open
        // hand, Config::openBend) to 1.
        float fist{};
        // How far it is in the web-shooting pose, 0 to 1, over the fist: the
        // two middle fingers pressed into the palm, the first and the little
        // one out, the thumb out to its side.
        float shoot{};
    };
    std::array<Hand, 2> hands{}; // 0 the left controller, 1 the right
    // A flip's tilt of the player, model space (the identity while level):
    // the body stands as the player would level, then turns with them about
    // the eyes. Tilted, the legs keep the game's pose, as in the air.
    Quat tilt{};
    // The game's legs are no stance here - its own wall crawl, splayed on
    // the wall, or the air pose of the hero Spidy's own wall holds. Planted,
    // the feet go flat on the floor (the plane through `floor`, square to
    // `up`) under the hips, as the rest pose stands, and the body stands on
    // them whatever the game's pose does. Without it the legs kept the
    // crawl's shape under the upright hips, which is what the player saw on a
    // crawled wall: "i dont want the crawling animation on legs or hands
    // ever" (October 14).
    bool plant{};
    // A point on that floor, model space: the model's origin where the hero
    // stands on it himself (the ground, the game's crawl), the surface under
    // the player where Spidy holds the hero clear of it (its own wall).
    Vec3 floor{};
    // Planted under a roof the view holds the head below the player's own
    // (a vent, a duct): the legs kneel, the knees down and the shins back
    // along the floor behind him, where a squat folded them up in front of
    // his eyes. Standing tall enough, kneeling is standing.
    bool kneel{};
    // Contact-driven upper-spine motion without replacing native vent legs
    // or enabling outdoor foot placement / back-facing stance changes.
    bool surfaceHands{};
    Vec3 surfaceNormal{}, surfacePoint{};
    // Outdoor hand climbing: chest toward the surface and alternating feet.
    // The selected tracking frame supplies ahead; gaze never chooses it.
    bool crawl{};
    Vec3 crawlNormal{}, crawlAhead{}, crawlStep{};
    float crawlPhase{};
    float crawlMotion{}; // 0 compact at rest, 1 wider and farther below the hips while moving
    bool crawlPlanted{};
    Vec3 crawlOrigin{}; // world-space player feet, before body/eye offsets
    Quat crawlModelToWorld{};
    bool crawlHandSync{};
    unsigned crawlHands{}; // confirmed left/right wall attachments, not squeezed buttons
    unsigned crawlTouches{}; // solid palm contact; does not enable movement
    bool crawlTurnAway{};
    // Native play requires a free palm to leave and recontact the wall
    // after turning. Explicit pose probes retain head-led requests.
    bool crawlReachTurn{};
    std::array<uint32_t, 2> crawlStrokes{};
    float crawlDistance{}; // saved camera metres; pelvis adds 5 cm; zero keeps legacy scaled clearance
};
struct Config {
    // Where the wrist joint sits from the grip point of a right hand, in its
    // grip frame: back toward the forearm (+y) and toward the back of the
    // hand (+x on a right hand). A left hand mirrors x.
    Vec3 wristFromGrip{.02f, .09f, 0};
    // The fingers point this far from the grip's -y toward the thumb (-z),
    // radians.
    float fingerPitch = 0;
    // The body turns with the headset once it looks further away than this,
    // at up to yawSpeed radians a second, and drifts toward it at yawDrift
    // per second meanwhile.
    float yawDeadZone = .6f, yawSpeed = 6, yawDrift = .5f;
    // The torso bends this share of the way the head pitches (down to look
    // at the body, up to look up); the neck takes the rest. At most
    // maxLean radians.
    float lean = .25f, maxLean = .5f;
    // The neck joints take this share of the head's turn from the chest.
    float neckShare = .5f;
    // The forearm takes this share of the wrist's roll about it.
    float forearmRoll = .5f;
    // A closed fist: each finger joint after the one in the palm bends this
    // far from the joint before it (knuckle, middle, tip, radians), about
    // the finger's own hinge. The thumb's last joint bends thumbBend, the
    // one before it as far as lays the tip on the curled fingers (at most
    // thumbBendMax), and its base swings the tip there: thumbRest (model
    // units) out from the middle bones of the first two fingers.
    float fistBend[3]{1.45f, 1.65f, 1.1f};
    float thumbBend = 1.1f, thumbBendMax = 1.4f, thumbRest = .016f;
    // Shooting a web (Hand::shoot): of three fingers or more the first and
    // the last stay out, each joint shootOut from the bone before it, and the
    // others press into the palm, on the web shooter's trigger, each joint
    // shootPress (knuckle, middle, tip, radians, about the finger's own
    // hinge). The thumb's joints straighten to shootThumbBend and it points
    // shootThumb from the fingers' way toward its own side, in the palm's
    // plane. The pressing fingers' tips reach the palm, as deep as a fist's:
    // until October 18 (1.3, 1.55, 0.75) they stopped 2 cm short of it ("the
    // fingers ain't touching the button").
    float shootPress[3]{1.5f, 1.6f, 1.1f}, shootOut[3]{.05f, .08f, .05f};
    float shootThumbBend = .1f, shootThumb = 1;
    // The open hand, holding nothing, under the fist and the shooting pose:
    // each finger joint past the palm sits this far from the bone before it
    // (knuckle, middle, tip, radians), the thumb's first two joints
    // openThumb. A hand at rest, barely curled.
    float openBend[3]{.22f, .3f, .22f};
    float openThumb = .18f;
    // Feet within this height of the ground in the game's pose stand on it:
    // the legs bend to keep them there. Higher, the legs keep the game's pose.
    float groundedFoot = .3f;
    // Standing feet stay where the game put them while the hips wander up
    // to this far from over them (leaning, a step in the room); further, the
    // feet follow by the rest, so the legs never trail far behind.
    float footReach = .15f;
    // Kneeling (Targets::kneel), the knee's height over the floor at the
    // body's own size: where the kneecap rests on it.
    float kneeHeight = .07f;
    // Outdoor hand climbing: eyes above the avatar's usual eye anchor, metres
    // along the selected crawl-up frame. Hands and tracking space stay put.
    float crawlEyeLift = .08f;
    // Remaining relative eye lift while outward. Zero restores the body's
    // usual height by raising it through the turn, never moving the camera.
    float crawlOutwardEyeLift{};
    // Extra room in front of the chest when looking down while facing away
    // from an outdoor wall. Move the avatar anchor wallward, not the camera.
    float crawlOutwardEyeClearance = .12f;
    // Maximum upper-spine twist with one attached hand, radians (70 degrees).
    float crawlTorsoTwist = 1.2217305f;
    // Shrinks the head to a millimetre, so the eyes are not inside it.
    bool hideHead = true;
    // The hands take the controllers' orientation; false keeps the game's.
    bool handOrientation = true;
    // How fast the body takes over from the game's pose and gives it back
    // (shares per second).
    float blendSpeed = 4;
};
struct State {
    CrawlFeet crawlFeet;
    CrawlFeet outwardFeet;
    CrawlTurn crawlTurn;
    float weight{}, yaw{};
    float crawlWeight{};
    float crawlTwist{};
    float crawlOutwardMotion{};
    struct OutwardAnchor {
        bool set{};
        Vec3 pelvis{}, head{}, origin{}, normal{}, up{};
        // Let the new outward pose settle beneath the eyes once after a
        // handoff, before its height becomes a fixed look-around anchor.
        float settle{};
        bool waistSet{};
        Vec3 waistForward{}, waistUp{};
        bool shouldersSet{};
        std::array<Vec3, 2> shoulderOffsets{};
    } outwardAnchor;
    struct ReachTurn {
        float direction{}, age{};
        unsigned previousTouches{}, previousTracked{}, armed{}, cue{};
        std::array<float, 2> approach{};
        std::array<float, 2> contactDepth{.035f, .035f};
        bool pending{}, outward{};
    } crawlReach;
    bool yawSet{};
    // Each hand as last tracked: from the eyes in the body's frame (its
    // forward, up and left), and its orientation in that frame.
    struct Held {
        bool set{};
        Vec3 grip{};
        Quat orientation{};
    };
    std::array<Held, 2> held{};
};
// Turns the body's yaw by `radians` about the up, at once: the player snap
// turned (body and all), or the model turned under the body (the hero's
// actor turned in the world, which turns a still headset back as much).
void turnState(State&, float radians);
struct Result {
    bool solved{}, grounded{};
    float weight{};
    // Distance left between each wrist and its target, and between the head
    // joint and its target, model units (-1: not placed).
    std::array<float, 2> handError{-1, -1};
    float headError = -1;
    float yaw{};
};
// One frame. `wanted` turns the body on (it blends in) or off (it blends
// out, toward the same targets); `dt` seconds since the last call. `scale`
// sizes the body about the feet (1 the game's size), and `armScale` its arms
// on top of that, each about its shoulder (the player's arms, measured by
// the T-pose calibration). Leaves the pose as it was while the weight is 0.
Result solve(Pose& pose, const Rig& rig, const Targets& targets, const Config& config, State& state, bool wanted,
             float dt, float scale = 1, float armScale = 1);
} // namespace spidy::body
