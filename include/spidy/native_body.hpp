#pragma once
#include "body_calibration.hpp"
#include "math.hpp"
#include <cstddef>
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
    // height is from the T-pose calibration (body_calibration.hpp), not the
    // headset's running height: the body takes it over a wider range.
    calibrated = 16,
    // The player stands on a wall or a ceiling (the view turned onto it):
    // Command::standUp and Command::floor say which. The body stands along
    // that surface, its feet planted on it, and its fingers are its own -
    // whether the game's crawl holds the hero there (its pose splayed on the
    // wall) or Spidy's own wall does (its air pose). The player, October 14:
    // "i dont want the crawling animation on legs or hands ever"; and of a
    // body solved upright in the world while he stood on the wall, "i dont
    // want it to be lying at all".
    standing = 32,
    // Standing so, the view holds the head below the player's own under a
    // roof (a vent, a duct): the legs kneel (body::Targets::kneel).
    kneel = 64,
    crawl = 128, // version 5: visual outdoor crawl; locomotion remains unchanged
    crawlPlanted = 256, // version 7: persistent alternating foot contacts
    crawlHandSync = 512, // version 8: contralateral hand strokes and a stable pelvis
    crawlLeftHeld = 1024, crawlRightHeld = 2048, // version 9: confirmed wall support, for upper-spine twist
    crawlLeftTouch = 4096, crawlRightTouch = 8192, // version 9: collision contact without a grip press
    crawlTurnAway = 16384, // version 9: feet-supported outward pose
    kick = 65536, // official v11: feet-first dive target
    wallRun = 131072, // version 16: travel-driven gait in crawlNormal/Ahead/Phase/Motion
    ventHands = 32768, // version 10: upper-body contact response; native vent legs stay in control
};
inline uint32_t crawlHandFlags(unsigned hands) { return (hands & 3u) << 10; }
inline unsigned crawlHandMask(uint32_t flags) { return (flags >> 10) & 3u; }
inline uint32_t crawlTouchFlags(unsigned hands) { return (hands & 3u) << 12; }
inline unsigned crawlTouchMask(uint32_t flags) { return (flags >> 12) & 3u; }
struct Command {
    uint32_t magic = 0x53424443, version = 16, bytes = sizeof(Command), flags{};
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
    // The player's arm from the hero's shoulder at their size to the wrist,
    // metres (the T-pose calibration; 0: the hero's own arms).
    float armLength{};
    // Version 2: a flip's tilt of the player, world axes (the identity while
    // level): the whole body turns with it about the eyes. Version 1
    // (commandBytesV1, the probe's) ends before it: level.
    Quat tilt{};
    // Version 3: how far each hand is in the web-shooting pose, 0 to 1
    // (body::Targets::Hand::shoot). Version 2 (commandBytesV2) ends before
    // it: fists only.
    float shoot[2]{};
    // Version 4, with Flags::standing: the surface the player stands on -
    // its up, world axes, and a point on it relative to the feet. The tilt
    // above is then a flip's alone (none on a wall). Version 3
    // (commandBytesV3) ends before them: the world's floor.
    Vec3 standUp{};
    Vec3 floor{};
    // Version 5 visual crawl frame, world axes: outward normal, the chosen
    // body direction along the wall, travel direction and stride phase 0..2pi.
    Vec3 crawlNormal{}, crawlAhead{}, crawlStep{};
    float crawlPhase{};
    // Version 6: 0 the compact resting crouch, 1 the wider, lower moving stance.
    float crawlMotion{};
    uint32_t motionReserved{}; // preserve version 6's trailing alignment bytes
    Vec3 crawlOrigin{}; // version 7: world anchor for persistent planted feet
    uint32_t originReserved{}; // preserve version 7's trailing alignment bytes
    uint32_t crawlStrokes[2]{}; // version 8: left/right attached-hand stroke counters
    // Version 9 crawl clearance (metres); version 14 wallRun uses this
    // otherwise-unused field for the joystick torso angle (radians).
    float crawlDistance = .25f;
    uint32_t distanceReserved{};
    // Official version 11's kick target stays at byte 272. Version 16
    // appends run tuning after it, keeping dive and running packets distinct.
    Vec3 kick{};
    uint32_t kickReserved{};
    // Version 16: momentum horizontal pose tuning; no locomotion changes.
    float runBank = .85f, runLean = .5f, runStride = 1.f;
    uint32_t runReserved{};
};
constexpr uint32_t commandBytesV1 = 144, commandBytesV2 = 160, commandBytesV3 = 168, commandBytesV4 = 192,
                   commandBytesV5 = 232, commandBytesV6 = 240, commandBytesV7 = 256, commandBytesV8 = 264,
                   commandBytesV10 = 272, commandBytesV14 = 272,
                   commandBytesV11 = 288, commandBytesV15 = 288;
static_assert(sizeof(Hand) == 36 && sizeof(Command) == 304 && offsetof(Command, tilt) == commandBytesV1 &&
              offsetof(Command, shoot) == commandBytesV2 && offsetof(Command, standUp) == commandBytesV3);
static_assert(offsetof(Command, crawlNormal) == commandBytesV4 && offsetof(Command, crawlPhase) == 228);
static_assert(offsetof(Command, crawlMotion) == commandBytesV5);
static_assert(offsetof(Command, crawlOrigin) == commandBytesV6);
static_assert(offsetof(Command, crawlStrokes) == commandBytesV7);
static_assert(offsetof(Command, crawlDistance) == commandBytesV8);
static_assert(offsetof(Command, kick) == commandBytesV10);
static_assert(offsetof(Command, runBank) == commandBytesV11);
inline bool commandFormat(uint32_t version, uint32_t bytes) {
    return (version == 1 && bytes == commandBytesV1) || (version == 2 && bytes == commandBytesV2) ||
           (version == 3 && bytes == commandBytesV3) || (version == 4 && bytes == commandBytesV4) ||
           (version == 5 && bytes == commandBytesV5) || (version == 6 && bytes == commandBytesV6) ||
           (version == 7 && bytes == commandBytesV7) || (version == 8 && bytes == commandBytesV8) ||
           ((version >= 9 && version <= 14) && bytes == commandBytesV14) ||
           ((version == 11 || version == 15) && bytes == commandBytesV11) ||
           (version == 16 && bytes == sizeof(Command));
}
// Earlier custom previews used the official kick bit/offset for run data.
// Version and byte size identify those packets; new commands use a new bit.
inline bool legacyRunFormat(const Command& c) {
    return (c.version == 11 && c.bytes == commandBytesV10) ||
           (c.version >= 12 && c.version <= 14 && c.bytes == commandBytesV14) ||
           (c.version == 15 && c.bytes == commandBytesV15);
}
inline bool wallRunCommand(const Command& c) {
    return (c.version == 16 && (c.flags & wallRun)) || (legacyRunFormat(c) && (c.flags & kick));
}
inline bool kickCommand(const Command& c) {
    return (c.flags & kick) && !legacyRunFormat(c) &&
           ((c.version == 11 && c.bytes == commandBytesV11) || c.version == 16);
}
inline Vec3 runTuning(const Command& c) {
    return c.version == 15 ? c.kick : Vec3{c.runBank, c.runLean, c.runStride};
}
inline uint32_t runPadding(const Command& c) { return c.version == 15 ? c.kickReserved : c.runReserved; }
inline bool validCrawl(const Command& c) {
    if (c.flags & ~0x3ffffu) return false;
    if ((c.flags & wallRun) && c.version != 16) return false;
    if ((c.flags & kick) && !legacyRunFormat(c) && !kickCommand(c)) return false;
    if (kickCommand(c) && (!finite(c.kick) || c.kickReserved)) return false;
    const Vec3 tuning = runTuning(c);
    if (wallRunCommand(c))
        return (c.version < 15 || (std::isfinite(tuning.x) && tuning.x >= 0 && tuning.x <= 2.747478f &&
                                   std::isfinite(tuning.y) && tuning.y >= 0 && tuning.y <= 1.047198f &&
                                   std::isfinite(tuning.z) && tuning.z >= .5f && tuning.z <= 1.5f &&
                                   !runPadding(c))) &&
               (c.version < 14 || (std::isfinite(c.crawlDistance) && c.crawlDistance >= 0 &&
                                   c.crawlDistance <= 1.396264f)) && (c.version >= 11 && c.version <= 16) &&
            !(c.flags & (standing | kneel | crawl | crawlPlanted | crawlHandSync | crawlLeftHeld |
                crawlRightHeld | crawlLeftTouch | crawlRightTouch | crawlTurnAway | ventHands)) &&
            !kickCommand(c) &&
            (c.version == 11 || (finite(c.crawlNormal) && finite(c.crawlAhead) && finite(c.floor) &&
                std::abs(length(c.crawlNormal) - 1) < .01f &&
                std::abs(length(c.crawlAhead) - 1) < .01f &&
                std::abs(dot(c.crawlNormal, c.crawlAhead)) < .01f &&
                std::isfinite(c.crawlPhase) && c.crawlPhase >= 0 && c.crawlPhase <= 6.283186f &&
                std::isfinite(c.crawlMotion) && c.crawlMotion >= .25f && c.crawlMotion <= 80.f));
    if (c.flags & ventHands)
        return (c.version >= 10 && c.version <= 16) &&
            !(c.flags & (crawl | crawlPlanted | crawlHandSync | crawlTurnAway)) &&
            finite(c.crawlNormal) && std::abs(length(c.crawlNormal) - 1) < .01f && finite(c.floor);
    if ((crawlTouchMask(c.flags) || (c.flags & crawlTurnAway)) &&
        (!(c.flags & crawlHandSync) || c.version < 9 || c.version > 16))
        return false;
    if (crawlHandMask(c.flags) && (!(c.flags & crawlHandSync) || c.version < 9 || c.version > 16))
        return false;
    if ((c.flags & crawlHandSync) && (!(c.flags & crawlPlanted) || c.version < 8 || c.version > 16))
        return false;
    if ((c.flags & crawlHandSync) && c.version >= 9 &&
        (!std::isfinite(c.crawlDistance) || c.crawlDistance < .1f || c.crawlDistance > 1.f))
        return false;
    if ((c.flags & crawlPlanted) && (!(c.flags & crawl) || c.version < 7 || c.version > 16 ||
                                   !finite(c.crawlOrigin)))
        return false;
    return !(c.flags & crawl) || ((c.version >= 5 && c.version <= 16) &&
        (c.version < 6 || (std::isfinite(c.crawlMotion) && c.crawlMotion >= 0 && c.crawlMotion <= 1)) &&
        finite(c.crawlNormal) && finite(c.crawlAhead) &&
        finite(c.crawlStep) && std::abs(length(c.crawlNormal) - 1) < .01f &&
        std::abs(length(c.crawlAhead) - 1) < .01f && std::abs(length(c.crawlStep) - 1) < .01f &&
        std::abs(dot(c.crawlNormal, c.crawlAhead)) < .01f &&
        std::abs(dot(c.crawlNormal, c.crawlStep)) < .01f && finite(c.floor) &&
        std::isfinite(c.crawlPhase) && c.crawlPhase >= 0 && c.crawlPhase <= 6.283186f);
}

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
    uint32_t magic = 0x53424453, version = 2, bytes = sizeof(Status), state{};
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
    // Hero pose jobs that named another rig than the one before.
    uint32_t rigSwitches{};
    // Render frames seen, and pose jobs of the hero since the previous one
    // (and the most in one frame).
    uint64_t renders{};
    uint32_t heroJobsLastFrame{}, heroJobsMax{};
    double solveMs{}; // time spent solving, total
    // The arms' scale on top of the body's (the player's arm length), and
    // whether the body's size is from the T-pose calibration (Flags).
    float armScale = 1;
    uint32_t calibrated{};
};
static_assert(sizeof(Status) == 144);

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
// Internal VR-worker feedback; no exported packet or protocol layout changes.
float crawlOutwardWeight();
// Where the hero's shoulders and wrists are at his own size, from the rig of
// his latest pose job; false (and `out` untouched) before one was read.
bool proportions(body_calibration::Proportions& out);
uint32_t stop();

// MODEL FOREARM web aim (vr_settings::Values::webAimSource; web_aim::
// ForearmAim): each arm as the latest hero pose job's solve left it, world
// axes relative to the hero instance's position (the model's rows turned by
// its transform; no scale). Filled at the end of every solve the body made
// while on, so a sample says nothing when the body is off (-NoBody, BODY
// off), when it did not solve, or once `job` stops advancing. Read by the VR
// worker (forearms()) and by tools/probe_game_body.py (SpidyForearms, a
// seqlock like SpidyBodyData). arm 0 is the character's left (the left
// controller), 1 its right.
struct Forearm {
    uint32_t valid{};
    Vec3 elbow{}, wrist{}; // the lower arm joint and the hand joint
    Vec3 palm{};           // out of the palm (Rig::handPalm through the hand joint), unit; zero unknown
    Quat grip{};           // the controller grip orientation the solve was given (Command::hands)
    float handError = -1;  // the wrist's miss of its target, model units (Result::handError)
};
struct Forearms {
    uint32_t magic = 0x53424641, version = 1, bytes = sizeof(Forearms), reserved{};
    int64_t sequence{};
    uint64_t job{};    // counts the solves that published one (stale once it stops)
    uint32_t tilted{}; // the solve had a flip's tilt (Command::tilt not the identity)
    uint32_t reserved2{};
    Forearm arms[2]{};
};
static_assert(sizeof(Forearm) == 60 && sizeof(Forearms) == 160);
// The latest Forearms (a copy); `job` 0 before any.
Forearms forearms();

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
