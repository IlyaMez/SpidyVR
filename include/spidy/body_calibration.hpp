#pragma once
#include "body_ik.hpp"
#include "vertex.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The T-pose calibration. The player stands tall, looks ahead, holds both
// arms straight out to the sides and both triggers for a moment; Spidy
// measures how high their eyes are and how long their arms are, and the body
// takes Spider-Man's size from the one and his arms' length from the other
// (native_body). Engine independent: tracking-space samples in, measurements
// and what the headset shows out.
namespace spidy::body_calibration {
// Where the avatar's shoulders are at its own size, from its rest pose: the
// point between its eyes above its feet, each shoulder joint (the upper
// arm's) from that point along the body's forward, up and left, and each arm
// from the shoulder joint to the wrist joint, metres; 0 the left. The
// defaults are Spider-Man's (the game's rig, every suit tried, October 6).
struct Proportions {
    float eyeHeight = 1.6969f;
    std::array<Vec3, 2> shoulders{Vec3{-.1107f, -.2662f, .1704f}, Vec3{-.1107f, -.2662f, -.1704f}};
    std::array<float, 2> arms{.5586f, .5586f};
};
// A prepared rig's (body::prepare); the defaults while it is not ready.
Proportions proportions(const body::Rig&);

// A calibrated body's size is trusted over a wider range than the headset's
// running height (native_body: 0.85-1.2), and its arms within this.
constexpr float minBodyScale = .7f, maxBodyScale = 1.3f, minArmScale = .8f, maxArmScale = 1.25f;
// The body's scale for a player whose eyes are `eyeHeight` above the floor.
float bodyScale(float eyeHeight, const Proportions& = {});
// How much longer the avatar's arms get at body scale `body`, for a player
// whose arm (from the avatar's shoulder at their size to the wrist) is
// `armLength` metres; 1 for 0 (not calibrated).
float armScale(float armLength, float body, const Proportions& = {});

// What a session takes as a calibration, millimetres (XrConfig,
// run_game_vr.py, the launcher); a measurement is kept within them.
constexpr unsigned minEyeHeightMm = 1000, maxEyeHeightMm = 2500, minArmLengthMm = 250, maxArmLengthMm = 1200;

// What a calibration found.
struct Measurements {
    float eyeHeight{}; // metres above the floor, standing tall
    // Metres from the avatar's shoulder joint, at the player's size, to the
    // wrist: the arm the avatar needs to reach the player's controllers
    // (the longer of the two).
    float armLength{};
    std::array<float, 2> reach{}; // each arm's, 0 the left
};
// The headset and the controllers in the tracking space (OpenXR axes: +y up
// from the floor, -z ahead), one frame.
struct Sample {
    bool headTracked{};
    Pose head{}; // the point between the eyes
    std::array<bool, 2> handTracked{};
    std::array<Pose, 2> grips{}; // the controllers' grip poses, 0 the left
    std::array<float, 2> triggers{};
    float seconds{}; // since the previous sample
};
enum class Phase : std::uint8_t {
    idle,    // nothing shows
    waiting, // the instructions show; the pose does not count (yet)
    holding, // the pose counts: the hold is filling
    done,    // measured: the result shows
};
// Why the pose does not count, the first that applies (none: it counts).
enum class Hint : std::uint8_t {
    none,
    tracking,  // the headset or a controller is not tracked
    standUp,   // the eyes are lower than a standing player's
    armsOut,   // an arm is not out to its side
    straight,  // an arm is bent, or the two reach differently far
    lookAhead, // the head is turned away from the arms' line or tilted
    triggers,  // a trigger is not held
    still,     // the head or a hand moved
};
struct Config {
    float holdSeconds = 1.5f;
    // A lapse this short pauses the hold; a longer one starts it over.
    float graceSeconds = .25f;
    // A trigger counts past `trigger`, then while past `triggerHeld`.
    float trigger = .6f, triggerHeld = .35f;
    // Eyes below this are a seated (or kneeling) player's, metres.
    float lowestEyes = 1;
    // An arm points from the avatar's shoulder at least this cosine near its
    // side (41 degrees), and reaches at least `bent` of the avatar's arm at
    // the player's size; the two reach within `mismatch` of the longer.
    float sideways = .75f, bent = .72f, mismatch = .12f;
    // The head within these of level and of square to the arms' line, radians.
    float headPitch = .45f, headYaw = .6f;
    // Moving faster is not holding still, m/s.
    float handSpeed = .35f, headSpeed = .3f;
};
// Where the avatar's wrist goes for a controller's grip pose (side 0 the
// left), as body::solve places it.
Vec3 wrist(Pose grip, int side, const body::Config& = {});

class Calibration {
  public:
    // Shows the instructions and waits for the pose.
    void start();
    // Hides them; a result stays.
    void stop();
    // One frame. True on the frame the pose has been held long enough:
    // result() then holds the player's measurements, and the phase stays done
    // until stop() or start().
    bool update(const Sample&, const Proportions& = {}, const body::Config& = {}, const Config& = {});
    Phase phase() const {
        return phase_;
    }
    Hint hint() const {
        return hint_;
    }
    // How far the hold is, 0 to 1.
    float progress() const {
        return progress_;
    }
    // Whether each arm (0 the left) is out to its side and straight.
    std::array<bool, 2> armsReady() const {
        return ready_;
    }
    const Measurements& result() const {
        return result_;
    }

  private:
    void restart();
    Phase phase_{};
    Hint hint_ = Hint::tracking;
    float held_{}, lapse_{}, progress_{};
    std::array<bool, 2> ready_{};
    bool triggersHeld_{};
    // Sums over the hold so far, for the result's means.
    int frames_{};
    double eyeSum_{};
    std::array<double, 2> reachSum_{};
    // The previous sample's head and wrists, for the stillness check.
    bool previous_{};
    Vec3 lastHead_{};
    std::array<Vec3, 2> lastWrists_{};
    Measurements result_{};
};

// The panel's place for a head at the start, tracking space: ahead of it,
// a little below the eyes, facing it (+z toward the player, +x to their right).
Pose panelPose(Pose head);
// What the headset shows of a calibration, in world space.
struct View {
    Phase phase{};
    Hint hint{};
    float progress{};
    std::array<bool, 2> ready{};
    Measurements result{};
    Pose panel{};
    std::array<bool, 2> handTracked{};
    std::array<Pose, 2> grips{};
};
// The panel (instructions, what to change, the hold's bar; the result once
// measured) and a ring at each tracked controller that turns green while its
// arm is in place and fills with the hold. `viewer`: between the eyes.
void appendView(std::vector<Vertex>& out, const View&, Vec3 viewer);
// The panel's line for a hint while the pose is not held.
const char* hintText(Hint);
} // namespace spidy::body_calibration
