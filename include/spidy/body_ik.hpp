#pragma once
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
// helper joints and joint scales keep what the game animated. Joint frames
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
    // to its tip, and the thumb's.
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
    std::array<Vec3, 2> handFingers{}, handPalm{};
    // The point between the eyes from the head joint, along the head's
    // forward, up and left, in model units; its height above the feet.
    Vec3 eyesFromHead{};
    float eyeHeight{};
    std::array<float, 2> upperArm{}, forearm{}, thigh{}, shin{};
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
    // The world's up in model space: the model's own +y while the hero
    // stands, the wall's normal while it crawls on one. The body always
    // stands upright in the world, as the player does.
    Vec3 up{0, 1, 0};
    Vec3 eyes{};   // the point between the eyes
    Quat facing{}; // the headset: +x right, +y up, -z where it looks (OpenXR)
    struct Hand {
        bool tracked{};
        // The controller's OpenXR grip pose: its point is at the palm of the
        // closed hand; +x is normal to the palm (to the right on both
        // hands), -z runs from the little finger to the thumb, so -y points
        // from the wrist out past the knuckles.
        Vec3 grip{};
        Quat orientation{};
        // How far the hand is closed into a fist, 0 (the game's fingers) to 1.
        float fist{};
    };
    std::array<Hand, 2> hands{}; // 0 the left controller, 1 the right
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
    // far from the joint before it (knuckle, middle, tip, radians); the
    // thumb's joints after its base fold in this far.
    float fistBend[3]{1.45f, 1.65f, 1.1f};
    float thumbFold = .6f;
    // Feet within this height of the ground in the game's pose stand on it:
    // the legs bend to keep them there. Higher, the legs keep the game's pose.
    float groundedFoot = .3f;
    // Standing feet stay where the game put them while the hips wander up
    // to this far from over them (leaning, a step in the room); further, the
    // feet follow by the rest, so the legs never trail far behind.
    float footReach = .15f;
    // Shrinks the head to a millimetre, so the eyes are not inside it.
    bool hideHead = true;
    // The hands take the controllers' orientation; false keeps the game's.
    bool handOrientation = true;
    // How fast the body takes over from the game's pose and gives it back
    // (shares per second).
    float blendSpeed = 4;
};
struct State {
    float weight{}, yaw{};
    bool yawSet{};
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
// sizes the body about the feet (1 the game's size). Leaves the pose as it
// was while the weight is 0.
Result solve(Pose& pose, const Rig& rig, const Targets& targets, const Config& config, State& state, bool wanted,
             float dt, float scale = 1);
} // namespace spidy::body
