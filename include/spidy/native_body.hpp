#pragma once
#include "math.hpp"
#include <cstdint>

// Spider-Man's own body, driven by the headset and the controllers
// (body_ik.hpp), in the game. The animation jobs write every character's
// model-space joints with 1601290 (pose job: rig at +0x28, local pose at
// [+0x18]+8; it returns the largest joint movement, which its callers use);
// right after it writes the hero's, the body turns them: the hips upright
// under the headset, the head at it and shrunk away, the arms to the
// controllers, standing feet on the ground. The hero's render instance (the
// actor record's first field) keeps them at +0xd8; skinning reads them later.
//
// The rig the job names: joint count at +2 (u16), 16-byte joints at +8
// (parent s16 at +0, name hash at +8: CRC-32 of the name seeded with
// 0xedb88320, no final inversion), the rest pose at +0x18 (48 bytes a joint:
// scale, rotation, translation, local to the parent).
//
// Targets are relative to the player's feet in world axes, as the eyes are
// placed (native_eye_frame): the eyes are moved to the hero's render
// position, the body is drawn from the same instance, so translation needs no
// timing; only the hero's turn between this job and the render shows.
namespace spidy::native_body {
struct Hand {
    // The controller's OpenXR grip pose: relative to the feet, world axes.
    Vec3 grip{};
    Quat orientation{};
    uint32_t tracked{};
    float fist{}; // 0 the game's fingers, 1 a closed fist
};
enum Flags : uint32_t {
    bodyOn = 1,      // the body follows the player; off blends back to the game's pose
    hideHead = 2,    // the head shrinks away (the eyes are inside it)
    handTurn = 4,    // the hands take the controllers' orientation
    airborne = 8,    // the player is in the air: the legs keep the game's pose
};
struct Command {
    uint32_t magic = 0x53424443, version = 1, bytes = sizeof(Command), flags{};
    uint64_t serial{};
    uint32_t leaseMs = 250, reserved{};
    Vec3 eyes{};   // between the eyes, relative to the feet
    Quat facing{}; // the headset, world axes (OpenXR: -z where it looks)
    Hand hands[2]{};
    // The player's standing eye height above the floor, metres (0 unknown):
    // the body is scaled to it.
    float height{};
    // The tracking space's yaw in the world (game_tracking's Rig): a snap
    // turn changes it, and turns the body with the player at once.
    float trackingYaw{};
    uint32_t reserved2{};
};
static_assert(sizeof(Hand) == 36 && sizeof(Command) == 144);

enum State : uint32_t { off = 0, waiting = 1, active = 2, failed = 3 };
// Why the body is not on the hero (Status::problem).
enum Problem : uint32_t {
    none = 0,
    noHero = 1,      // no player, or its instance has no joints
    noRig = 2,       // the hero's pose job has not run since start
    unknownRig = 3,  // a joint the body needs has no name the body knows
    badRest = 4,     // the rest pose is not an upright figure
    badInstance = 5, // the hero's instance transform is not a rotation
};
struct Status {
    uint32_t magic = 0x53424453, version = 1, bytes = sizeof(Status), state{};
    int64_t sequence{};
    // Pose jobs seen, the hero's among them, and those the body changed.
    uint64_t jobs{}, heroJobs{}, solved{};
    uint32_t problem{}, joints{};  // the hero rig's joint count
    uint64_t rig{}, instance{};    // the hero's rig and render instance
    float weight{}, scale{}, yaw{}; // blend share, body size, body yaw (model)
    uint32_t grounded{};
    // Distance left between each wrist and its controller, and the head joint
    // and its place, metres (-1 none), at the latest job.
    float handError[2]{-1, -1}, headError = -1;
    // The hero's turn between its latest pose job and the render that
    // followed, radians: what the body is off by while the hero turns.
    float turnLast{}, turnMax{};
    uint32_t reserved{};
    // Render frames seen, and pose jobs of the hero since the previous one.
    uint64_t renders{};
    uint32_t heroJobsLastFrame{}, reserved2{};
    double solveMs{}; // time spent solving, total
};
static_assert(sizeof(Status) == 136);

uint32_t start(uintptr_t base);
// The local player became another actor (zero: none).
void retarget(uintptr_t record);
void submit(const Command&);
// A render frame begins (the stereo module's render setup, main thread).
void renderFrame();
// The body turned the hero's joints in the last moments, its head shrunk:
// the eyes may draw the hero. Otherwise they hide it, as before the body.
bool drawn();
Status status();
uint32_t stop();

// Wire formats of the probe exports (SpidyBodyStart, SpidyBodySubmit,
// SpidyBodyCapture), which drive the body without the OpenXR worker.
struct ProbeConfig {
    uint32_t magic = 0x53424346, version = 1, bytes = sizeof(ProbeConfig), pid{};
    uint64_t base{}, record{};
};
static_assert(sizeof(ProbeConfig) == 32);
// SpidyBodyCapture: the next hero pose job copies its joints as the game
// wrote them and as the body left them, with its rig and the rig's rest pose
// in model space, into SpidyBodyPoses.
constexpr unsigned maxJoints = 256;
struct Poses {
    uint32_t magic = 0x53424450, version = 1, bytes = sizeof(Poses), joints{};
    int64_t sequence{};
    uint64_t rig{}, instance{}, out{}, captured{};
    float transform[16]{}; // the hero's instance transform at that job
    float game[maxJoints * 16]{};
    float body[maxJoints * 16]{};
    float rest[maxJoints * 16]{};
};
} // namespace spidy::native_body
