#pragma once
#include "presentation_gate.hpp"
#include "swing.hpp"
#include "swing_takeoff.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>

namespace spidy::game_swing {
// Gravity and air steering are the strengths the October 4-5 test builds
// actually applied. Those builds configured 18 and 12, but steered from
// one-step-old state (see InFlightStep), which applied about a third of each.
// The VR settings' weight sets the gravity, at start and during play
// (Settings), up to maxGravity.
struct Config {
    uint32_t magic = 0x53574346, version = 1, bytes = sizeof(Config), pid{};
    uint64_t base{}, record{}, mover{}, motionModule{};
    uint32_t durationMs = 20000;
    float maxSpeed = 32, gravity = 6;
    // Web grab: what a web may catch (bits 1 << game_targets::Kind; 0 = no
    // grabbing, as before). Only kinds the game adapter can move take effect.
    uint32_t grabKinds{};
};
inline SwingConfig physicsConfig(const Config& c) {
    SwingConfig physics;
    physics.maxSpeed = c.maxSpeed;
    physics.gravity = c.gravity;
    physics.reelSpeed = 16;
    physics.airAcceleration = 4;
    physics.maxZipImpulse = 18;
    physics.zipMultiplier = 4.5f;
    // Walls are the swing's own from the start (Settings::walls).
    physics.walls = true;
    return physics;
}
// MoverStandard applies a velocity command during the physics step after the
// one Spidy observes: the observed position starts the step already in flight,
// and the observed velocity is the command before last. Steering from those
// made even and odd steps two independent trajectories. Each received gravity
// and steering every other step, and a yank kicked only one of them, so the
// body alternated between two velocities and the view shook until landing.
// Predict from where the step in flight leaves the body instead.
class InFlightStep {
  public:
    struct Sample {
        uint64_t step{}, serial{};   // native step and the command serial it applies
        Vec3 position{}, achieved{}; // start of this step; velocity of the previous one
        float dt{};                  // duration of this step
        bool controlled{};           // this step applies Spidy's command `serial`
    };
    // Body at the start of the step that the next command will govern.
    Body predict(const Sample& s, bool grounded) {
        const Record* flight = s.controlled ? find(s.serial) : nullptr;
        // The command that produced the observed velocity. Without an
        // observation in between, the step in flight repeats the same command.
        const Record* produced = s.step == step_ + 1 ? (controlled_ ? find(serial_) : nullptr) : flight;
        Vec3 velocity = flight ? flight->end : s.achieved;
        Vec3 travel = (flight ? flight->requested : s.achieved) * s.dt;
        if (produced) {
            // Collision took this part of the command away. Remove motion into
            // that surface; a projection cannot remove one contact twice.
            const Vec3 lost = produced->requested - s.achieved;
            if (length(lost) > .25f) {
                const Vec3 into = normalized(lost);
                velocity -= into * std::max(0.f, dot(velocity, into));
                travel -= into * std::max(0.f, dot(travel, into));
            }
        }
        step_ = s.step;
        serial_ = s.serial;
        controlled_ = s.controlled;
        return {s.position + travel, velocity, grounded};
    }
    // A submitted command: its velocity and the solver's velocity at its end.
    void issued(uint64_t serial, Vec3 requested, Vec3 end) {
        records_[next_++ % records_.size()] = {serial, requested, end};
    }
    void reset() {
        *this = {};
    }

  private:
    struct Record {
        uint64_t serial{};
        Vec3 requested{}, end{};
    };
    const Record* find(uint64_t serial) const {
        for (const auto& record : records_)
            if (serial && record.serial == serial)
                return &record;
        return nullptr;
    }
    std::array<Record, 8> records_{};
    size_t next_{};
    uint64_t step_{}, serial_{};
    bool controlled_{};
};
struct Hand {
    Pose aim{};
    Vec3 relative{};
    uint32_t tracked{};
    float trigger{}, grip{};
};
struct Command {
    uint32_t magic = 0x5357434d, version = 3, bytes = sizeof(Command), focused{};
    uint64_t serial{};
    uint32_t leaseMs = 100, jump{};
    Hand hands[2];
    Vec3 move{};
    float trackingYaw{};
    float sampleSeconds = 1.f / 90.f;
    uint32_t reserved{};
    uint64_t sampleTimeNs = 1;
    // Version 3: a flip's tilt of the tracking space (Input::tilt). Version 2
    // (commandBytesV2, the probes') ends before it: level.
    Quat tilt{};
};
constexpr uint32_t commandBytesV2 = 168;
struct WebState {
    uint32_t attached{}, bodyId{};
    Vec3 anchor{};
    float length{}, tension{};
};
// Data::wall: what holds the player on a wall or a ceiling now.
enum class Surface : uint32_t {
    none,
    wall, // the swing's own wall (SwingConfig's walls): wall running, walking
    game, // the game's wall crawl (its actor turned onto the surface)
};
struct Data {
    uint32_t magic = 0x53574441, version = 4, bytes = sizeof(Data), status{};
    int64_t sequence{};
    uint64_t qpc{}, steps{}, controlled{}, serial{}, attaches{}, releases{}, zips{}, world{}, sourceStep{};
    Vec3 position{}, velocity{}, requested{};
    float dt{};
    uint32_t owned{}, error{};
    WebState webs[2];
    uint32_t grounded{}, collisionFlags{}, takeoff{}, misses{}, obstructed{}, trackingLost{};
    uint32_t takeoffPhase{}, takeoffAttempts{}, takeoffTimeouts{}, nativeContact{};
    // Version 4: the surface that holds the player (Surface), its normal out
    // of it as the world's rays find it (the game's actor rocks on a wall), and
    // the body's centre's distance from it (the swing's wall; the game's crawl
    // has the feet on it: 0). Then the walls the swing took the player onto
    // so far, and the jumps off them. mount: a standing player is going up
    // onto the swing's wall by the game's jump (Swing::mountBegun), 1 walked
    // at a wall, 2 out of the game's own crawl; the player's stick stays away
    // from the game meanwhile (it walked him on into the wall, and into its
    // crawl).
    uint32_t wall{};
    Vec3 wallNormal{};
    float wallDistance{};
    uint32_t walls{}, wallJumps{}, mount{};
};
static_assert(sizeof(Config) == 64 && sizeof(Hand) == 52 && sizeof(Command) == 184 &&
              offsetof(Command, tilt) == commandBytesV2);
// SpidySwingSettings: what the VR settings (the SPIDY VR tab in the game's
// Settings) change during play.
// grab: webs catch props and thugs (a swing started without them starts
// them here); off lets go of what they hold. maxSpeed: the speed limit, up to
// 65 m/s, which the movement module is started with for that reason.
// airWebs: a web that meets nothing within reach holds in open air there;
// off, it misses (Swing::allowAirAnchors). gravity: the swing's, m/s^2 (the
// VR settings' weight), up to maxGravity. Version 4, walls: a player in the
// air stays on the walls it comes into (Swing::allowWalls); off, the game's
// own wall crawl takes it as before. Version 3 (settingsBytesV3, the
// probes') ends before it and leaves the walls as they are.
struct Settings {
    uint32_t magic = 0x53575354, version = 4, bytes = sizeof(Settings), grab = 1;
    float maxSpeed = 32;
    uint32_t airWebs = 1;
    float gravity = 6;
    uint32_t walls = 1;
};
constexpr uint32_t settingsBytesV3 = 28;
static_assert(sizeof(Settings) == 32 && offsetof(Settings, walls) == settingsBytesV3);
// The movement module's own limit: every speed the VR settings offer.
constexpr float motionSpeedLimit = 65;
// The most gravity a swing takes, m/s^2: every weight the VR settings offer.
constexpr float maxGravity = 30;
static_assert(sizeof(WebState) == 28 && sizeof(Data) == 272 && offsetof(Data, wall) == 240);
// The player is in the air: Spidy's own flight, or the game's jump or fall,
// where the mover is unsupported and in its airborne mode (collision flags
// 0x10: the steps the game's air state runs). A perch, a wall crawl and a
// wall run are neither, and a sample whose step is over 100 ms older than
// `nowQpc` says nothing (a perch or ledge the game animates without steps).
inline bool airborne(const Data& d, int64_t nowQpc, int64_t qpcFrequency) {
    const bool recent = d.qpc && static_cast<int64_t>(d.qpc) > nowQpc - qpcFrequency / 10;
    return recent && (d.status == 1 || d.status == 2) && (d.owned || (!d.grounded && (d.collisionFlags & 0x10u)));
}
// What a grip press would do now with each hand (SpidyAimSample), for the
// headset's aim markers: worked out in the world-query callback with the rays
// and target picks the press itself uses, from the latest input. It costs a
// few rays a step, so only while someone samples it within aimLeaseMs.
enum class AimKind : uint32_t {
    none,      // the hand is untracked, or its web holds something already; or nothing is
               // within reach and webs do not hold in open air (Settings::airWebs)
    anchor,    // the web attaches to this surface
    air,       // nothing within reach: the web attaches in the air at maximum reach
    blocked,   // the web misses: what the ray meets cannot hold it, or the body has no clear line
    prop,      // the web catches this prop
    character, // the web catches this character
};
struct Aim {
    uint32_t kind{}; // AimKind
    float radius{};  // a target's radius
    Vec3 point{};    // where the web goes: the surface or air point, or the target's centre
    Vec3 normal{};   // the surface's normal (anchor, blocked); zero for none
};
struct AimData {
    uint32_t magic = 0x5357414d, version = 1, bytes = sizeof(AimData), status{};
    int64_t sequence{};
    uint64_t serial{}; // the input command the aims were worked out from
    Aim hands[2];
};
static_assert(sizeof(Aim) == 32 && sizeof(AimData) == 96);
constexpr uint32_t aimLeaseMs = 250;
inline bool valid(const Command& c) {
    const auto& t = c.tilt;
    const float tilt = t.x * t.x + t.y * t.y + t.z * t.z + t.w * t.w;
    if (c.magic != 0x5357434d ||
        !((c.version == 2 && c.bytes == commandBytesV2) || (c.version == 3 && c.bytes == sizeof(c))) ||
        !c.serial || c.focused > 1 || c.jump > 1 || c.leaseMs > 250 || (c.focused && !c.leaseMs) ||
        !finite(c.move) || length(c.move) > 1.001f || !std::isfinite(c.trackingYaw) || c.reserved ||
        !std::isfinite(c.sampleSeconds) || c.sampleSeconds <= 0 || c.sampleSeconds > .1f ||
        (c.focused && !c.sampleTimeNs) || !std::isfinite(tilt) || tilt < .99f || tilt > 1.01f)
        return false;
    for (const auto& h : c.hands) {
        const auto q = h.aim.orientation;
        const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (h.tracked > 1 || !finite(h.aim.position) || !finite(h.relative) || !std::isfinite(norm) ||
            norm < .99f || norm > 1.01f || !std::isfinite(h.trigger) || !std::isfinite(h.grip) ||
            h.trigger < 0 || h.trigger > 1 || h.grip < 0 || h.grip > 1)
            return false;
    }
    return true;
}
class InputSampleClock {
  public:
    float consume(const Command& c) {
        if (c.serial == serial_)
            return 0;
        const float seconds =
            time_ ? (c.sampleTimeNs > time_ ? static_cast<float>((c.sampleTimeNs - time_) * 1e-9)
                                            : std::numeric_limits<float>::quiet_NaN())
                  : c.sampleSeconds;
        serial_ = c.serial;
        time_ = c.sampleTimeNs;
        return seconds;
    }
    void reset() {
        serial_ = time_ = 0;
    }

  private:
    uint64_t serial_{}, time_{};
};
// The input the swing acts on. A focused sample stays in use for
// controlHoldMs after input stops coming or turns unfocused, as if no new
// sample had arrived: a game frame of over 100 ms closes the XR worker's
// gameplay gate. Letting go there handed a swinging player to the game's own
// fall, which counts the time airborne through Spidy's flight and starts at
// 36-48 m/s down (October 6-7 headset reports).
class InputHold {
  public:
    // `live`: c is focused and within its lease. Whether c, replaced by the
    // held sample while holding, is input to act on.
    bool update(Command& c, bool live, uint64_t nowMs) {
        if (live) {
            held_ = c;
            heldMs_ = nowMs;
            holding_ = true;
            return true;
        }
        if (holding_ && nowMs >= heldMs_ && nowMs - heldMs_ <= controlHoldMs) {
            c = held_;
            return true;
        }
        holding_ = false;
        return false;
    }
    void reset() {
        holding_ = false;
    }

  private:
    Command held_{};
    uint64_t heldMs_{};
    bool holding_{};
};
inline Input input(const Command& c) {
    Input out;
    out.focused = c.focused != 0;
    out.jump = c.jump != 0;
    out.move = c.move;
    out.trackingYaw = c.trackingYaw;
    const float n =
        std::sqrt(c.tilt.x * c.tilt.x + c.tilt.y * c.tilt.y + c.tilt.z * c.tilt.z + c.tilt.w * c.tilt.w);
    if (c.version == 3 && n > .5f)
        out.tilt = {c.tilt.x / n, c.tilt.y / n, c.tilt.z / n, c.tilt.w / n};
    for (unsigned i = 0; i < 2; ++i)
        out.hands[i] = {c.hands[i].aim, c.hands[i].relative, c.hands[i].tracked != 0, c.hands[i].trigger,
                        c.hands[i].grip};
    return out;
}
} // namespace spidy::game_swing
