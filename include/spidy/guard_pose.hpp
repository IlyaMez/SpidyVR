#pragma once
#include "math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

// The guarding enemy is seen to guard: when the hero punches him or pulls him with a web and his guard stops
// it, the block animation plays.
//
// The game's own block (BotStateBlock: the clips Block_Intro, Block, Block_Hit, Block_Outro) is a state of
// the enemy's AI, and the mod's punches are direct damage that never go through the attacks the AI answers
// with a block (the hero's blows in the game do), so an enemy never enters it here; asking for it would have
// the AI override the request, or leave him blocking for seconds with the guard system and the damage code at
// odds with it. So the mod poses the enemy's skeleton itself, in the pose job after the game's animation has
// written it (the way the hero's own body is placed, native_body.cpp): both arms come up to the face, the
// upper body leans in behind them, a blocked blow jolts it, and the counter is a swing of the right arm and a
// twist of the body.
//
// This file is the whole of it with no game in it (tests/guard_tests.cpp): the cue that says what an enemy is
// doing at a moment, the table of cues, the enemy's rig found by joint names, and the pose written onto
// model-space joints (16 floats a joint: the x, y and z axis rows, then the position row - the layout the
// pose job gives).
//
// Model space is the game's: +x the character's left, +y up, +z forward. The pose is made in a frame measured
// from the pose itself (the shoulders, and the pelvis to the head), so it follows whatever the animation does
// with the body - a crouch, a lean, a turn - and a rig that does not follow that layout is not posed
// (Why::convention).
namespace spidy::guard_pose {
// What the headset side tells the pose job.
enum Move : std::uint32_t {
    moveNone = 0,
    moveBlock = 1,   // a punch was blocked: arms up, and the body takes the blow
    moveRefuse = 2,  // a web pull was refused: arms up
    moveCounter = 3, // his counter: the swing
    moveDrop = 4     // his guard is open: arms down
};

// Seconds. The guard comes up in riseSeconds, stays holdSeconds after the latest blow (or refusal) and goes
// down in lowerSeconds; a blocked blow jolts the body (peak at joltPeak, gone by joltSeconds); the counter
// winds up for windSeconds, strikes for strikeSeconds and comes back in recoverSeconds.
constexpr double riseSeconds = .09, lowerSeconds = .30, holdSeconds = .85;
constexpr double joltPeak = .05, joltSeconds = .40;
constexpr double windSeconds = .20, strikeSeconds = .10, recoverSeconds = .38;
constexpr double swingSeconds = windSeconds + strikeSeconds + recoverSeconds;
// The moment the swing reaches the hero: the arm is out four fifths of the way into the strike. game_punch
// staggers the hero then.
constexpr double contactSeconds = windSeconds + strikeSeconds * .8;

inline float smooth01(double x) {
    if (!(x > 0))
        return 0.f;
    if (x >= 1)
        return 1.f;
    return static_cast<float>(x * x * (3 - 2 * x));
}
// The x in 0..1 whose smooth01 is w (smooth01 is 3x^2 - 2x^3).
inline double unsmooth(double w) {
    w = std::clamp(w, 0., 1.);
    return .5 - std::sin(std::asin(1 - 2 * w) / 3);
}

// What an enemy is doing, by the times it began (seconds on one clock).
struct Cue {
    double raisedAt = -1e9;  // the guard began to come up
    double heldUntil = -1e9; // and goes down from here
    double hitAt = -1e9;     // the latest blocked blow
    double swingAt = -1e9;   // the counter began
    float hitStrength{};
};
// How far up his guard is, 0..1.
inline float guardWeight(const Cue& c, double now) {
    return smooth01((now - c.raisedAt) / riseSeconds) * (1 - smooth01((now - c.heldUntil) / lowerSeconds));
}
// The guard comes up (or stays up) and is held `hold` seconds from now. Coming up from a guard half down, it
// goes on from where it is.
inline void raise(Cue& c, double now, double hold = holdSeconds) {
    const double w = guardWeight(c, now);
    c.raisedAt = w > 1e-4 ? now - riseSeconds * unsmooth(w) : now;
    c.heldUntil = (std::max)(c.heldUntil, now + hold);
}
// A blow he blocked: the guard, and the jolt of the blow.
inline void hit(Cue& c, double now, float strength) {
    raise(c, now);
    c.hitAt = now;
    c.hitStrength = std::isfinite(strength) ? std::clamp(strength, .25f, 1.f) : .5f;
}
// His counter begins now.
inline void swing(Cue& c, double now) {
    raise(c, now, swingSeconds + .3);
    c.swingAt = now;
}
// His guard is open: the arms go down from here, and a counter in the making is off.
inline void drop(Cue& c, double now) {
    c.heldUntil = (std::min)(c.heldUntil, now);
    c.swingAt = -1e9;
}
// Nothing of it is left to show.
inline bool done(const Cue& c, double now) {
    return now > c.heldUntil + lowerSeconds + .05 && now > c.hitAt + joltSeconds &&
           now > c.swingAt + swingSeconds;
}

// The pose at a moment.
struct Eval {
    float guard{}; // 0..1: how far both arms are in their guard
    float jolt{};  // 0..1: the body takes a blocked blow
    float wind{};  // the right arm's share in its wind-up, and in its strike; the guard has the rest
    float strike{};
    float twist{}; // -1..1 the body's twist for the swing (the right shoulder forward is +)
    bool any() const {
        return guard > 1e-4f || jolt > 1e-4f || wind > 1e-4f || strike > 1e-4f;
    }
};
inline Eval evaluate(const Cue& c, double now) {
    Eval e;
    e.guard = guardWeight(c, now);
    if (const double t = now - c.hitAt; t >= 0 && t < joltSeconds) {
        const double x = t / joltPeak;
        e.jolt = c.hitStrength * static_cast<float>(x * std::exp(1 - x));
    }
    if (const double t = now - c.swingAt; t >= 0 && t < swingSeconds) {
        if (t < windSeconds) {
            e.wind = smooth01(t / windSeconds);
        } else if (t < windSeconds + strikeSeconds) {
            const double x = (t - windSeconds) / strikeSeconds;
            const float p = static_cast<float>(1 - (1 - x) * (1 - x)); // out fast, then easing
            e.wind = 1 - p;
            e.strike = p;
        } else {
            e.strike = 1 - smooth01((t - windSeconds - strikeSeconds) / recoverSeconds);
        }
        // Coiled back in the wind-up, thrown forward in the strike.
        e.twist = -.8f * e.wind + e.strike;
    }
    return e;
}

// The cues of the enemies about, by actor record (the headset side posts, the pose job reads: the caller
// locks).
class Table {
  public:
    static constexpr size_t slots = 8;
    struct Slot {
        std::uint64_t record{};
        Cue cue;
        double used{};
    };
    // A move for an enemy. A new enemy takes a free slot, else the one used longest ago.
    void post(std::uint64_t record, std::uint32_t move, float strength, double now) {
        if (!record || move == moveNone || move > moveDrop)
            return;
        Slot* slot = find(record);
        if (!slot) {
            if (move == moveDrop)
                return;
            for (auto& s : slots_)
                if (!s.record || done(s.cue, now))
                    slot = &s;
            if (!slot)
                slot = &*std::min_element(slots_.begin(), slots_.end(),
                                          [](const Slot& a, const Slot& b) { return a.used < b.used; });
            *slot = {};
            slot->record = record;
        }
        slot->used = now;
        switch (move) {
        case moveBlock:
            hit(slot->cue, now, strength);
            break;
        case moveRefuse:
            raise(slot->cue, now);
            break;
        case moveCounter:
            swing(slot->cue, now);
            break;
        default:
            drop(slot->cue, now);
            break;
        }
    }
    // Forgets the cues with nothing left to show; how many are left.
    size_t prune(double now) {
        size_t live{};
        for (auto& s : slots_) {
            if (s.record && done(s.cue, now))
                s = {};
            live += s.record != 0;
        }
        return live;
    }
    // Copies out the cues that have something to show at `now`: (record, cue) pairs, at most `slots`.
    size_t live(double now, std::array<Slot, slots>& out) const {
        size_t n{};
        for (const auto& s : slots_)
            if (s.record && !done(s.cue, now))
                out[n++] = s;
        return n;
    }
    const Cue* cue(std::uint64_t record) const {
        for (const auto& s : slots_)
            if (s.record == record)
                return &s.cue;
        return nullptr;
    }
    void clear() {
        slots_ = {};
    }

  private:
    Slot* find(std::uint64_t record) {
        for (auto& s : slots_)
            if (s.record == record)
                return &s;
        return nullptr;
    }
    std::array<Slot, slots> slots_{};
};

// The game's name hash (a CRC-32 table step per character, seeded with 0xedb88320, no final inversion): the
// same as native_body.cpp's, so a rig's joints are found by name.
inline std::uint32_t nameHash(const char* name) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xedb88320u;
    for (; *name; ++name)
        c = table[(c ^ static_cast<std::uint8_t>(*name)) & 0xff] ^ (c >> 8);
    return c;
}

// An arm: its joints, and what is below the upper arm and the forearm (each with itself first).
struct Arm {
    int upper = -1, lower = -1, hand = -1;
    std::vector<std::int16_t> belowUpper, belowLower;
};
// An enemy's rig as the pose needs it.
struct Rig {
    bool ready{};
    int joints{};
    int pelvis = -1, chest = -1, head = -1;
    std::array<Arm, 2> arms; // 0 the left (LF_), 1 the right (RT_)
    std::vector<std::int16_t> belowChest, belowHead;
};
// Why a rig cannot be posed.
enum class Why : std::uint8_t {
    ok = 0,
    shape,      // a table that is not a rig (too few joints, a parent after its child)
    names,      // a joint the pose needs is not there, or not where a body has it
    convention, // the rest pose does not stand +x left, +y up, +z forward
    noPose      // (posing) the joints at hand were no good
};
inline const char* whyName(Why why) {
    switch (why) {
    case Why::ok:
        return "ok";
    case Why::shape:
        return "not a rig";
    case Why::names:
        return "joints not found";
    case Why::convention:
        return "rig axes unlike the hero's";
    default:
        return "pose unusable";
    }
}

inline Vec3 at(const float* joints, int joint) {
    const float* p = joints + joint * 16 + 12;
    return {p[0], p[1], p[2]};
}
// The joint and all below it, parents before children.
inline std::vector<std::int16_t> subtree(std::span<const std::int16_t> parent, int root) {
    std::vector<std::int16_t> out{static_cast<std::int16_t>(root)};
    std::vector<char> in(parent.size(), 0);
    in[static_cast<size_t>(root)] = 1;
    for (size_t j = static_cast<size_t>(root) + 1; j < parent.size(); ++j)
        if (parent[j] >= 0 && in[static_cast<size_t>(parent[j])]) {
            in[j] = 1;
            out.push_back(static_cast<std::int16_t>(j));
        }
    return out;
}
// Finds the joints by their names (the hero's: a_body or pelvis, chest, head, and LF_ / RT_ with uparm, loarm
// and wrist), checks the arms hang from the chest and that the rest pose has the layout this works in.
inline Why prepare(Rig& rig, std::span<const std::int16_t> parent, std::span<const std::uint32_t> hash,
                   std::span<const float> rest) {
    rig = {};
    const size_t n = parent.size();
    if (n < 16 || n > 1024 || hash.size() != n || rest.size() < n * 16)
        return Why::shape;
    for (size_t j = 0; j < n; ++j)
        if (parent[j] < -1 || parent[j] >= static_cast<int>(j))
            return Why::shape;
    const auto find = [&](std::initializer_list<std::string> names) {
        for (const auto& name : names) {
            const auto h = nameHash(name.c_str());
            for (size_t j = 0; j < n; ++j)
                if (hash[j] == h)
                    return static_cast<int>(j);
        }
        return -1;
    };
    rig.pelvis = find({"pelvis", "a_body"});
    rig.chest = find({"chest"});
    rig.head = find({"head"});
    const char* side[2] = {"LF_", "RT_"};
    for (int i = 0; i < 2; ++i) {
        const std::string s = side[i];
        rig.arms[static_cast<size_t>(i)].upper = find({s + "uparm", s + "upperarm"});
        rig.arms[static_cast<size_t>(i)].lower = find({s + "loarm", s + "forearm", s + "lowerarm"});
        rig.arms[static_cast<size_t>(i)].hand = find({s + "wrist", s + "hand"});
    }
    if (rig.pelvis < 0 || rig.chest < 0 || rig.head < 0)
        return Why::names;
    rig.belowChest = subtree(parent, rig.chest);
    const auto under = [&](const std::vector<std::int16_t>& below, int joint) {
        return std::find(below.begin(), below.end(), joint) != below.end();
    };
    if (!under(rig.belowChest, rig.head))
        return Why::names;
    for (auto& arm : rig.arms) {
        if (arm.upper < 0 || arm.lower < 0 || arm.hand < 0)
            return Why::names;
        arm.belowUpper = subtree(parent, arm.upper);
        arm.belowLower = subtree(parent, arm.lower);
        if (!under(rig.belowChest, arm.upper) || !under(arm.belowUpper, arm.lower) ||
            !under(arm.belowLower, arm.hand))
            return Why::names;
    }
    rig.belowHead = subtree(parent, rig.head);
    // The rest pose stands as the hero's does: his left is +x, up is +y, and so forward is +z.
    const float* p = rest.data();
    const Vec3 left = at(p, rig.arms[0].upper) - at(p, rig.arms[1].upper),
               up = at(p, rig.head) - at(p, rig.pelvis);
    if (!finite(left) || !finite(up) || length(left) < .1f || length(up) < .2f)
        return Why::convention;
    const Vec3 l = normalized(left), u = normalized(up);
    if (l.x < .7f || u.y < .7f || cross(l, u).z < .5f)
        return Why::convention;
    rig.joints = static_cast<int>(n);
    rig.ready = true;
    return Why::ok;
}

// ---- the pose
// ------------------------------------------------------------------------------------------------

// The shortest turn from one direction to another (any lengths; none for zero or non-finite ones).
inline Quat between(Vec3 a, Vec3 b) {
    if (!finite(a) || !finite(b) || length(a) < 1e-4f || length(b) < 1e-4f)
        return {};
    return arc(normalized(a), normalized(b));
}
// The part `w` (0..1) of a turn, the short way round.
inline Quat partial(Quat q, float w) {
    if (!(w > 0) || !std::isfinite(q.x + q.y + q.z + q.w))
        return {};
    if (q.w < 0)
        q = {-q.x, -q.y, -q.z, -q.w};
    const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(s > 1e-6f))
        return {};
    return Quat::around({q.x, q.y, q.z}, 2 * std::atan2(s, q.w) * (std::min)(w, 1.f));
}
// Turns the joints about `pivot` (their axes rows and their positions).
inline void turn(float* joints, std::span<const std::int16_t> list, Quat q, Vec3 pivot) {
    for (const auto joint : list) {
        float* r = joints + joint * 16;
        for (int row = 0; row < 3; ++row) {
            const Vec3 v = q.rotate({r[row * 4], r[row * 4 + 1], r[row * 4 + 2]});
            r[row * 4] = v.x;
            r[row * 4 + 1] = v.y;
            r[row * 4 + 2] = v.z;
        }
        const Vec3 moved = pivot + q.rotate(Vec3{r[12], r[13], r[14]} - pivot);
        r[12] = moved.x;
        r[13] = moved.y;
        r[14] = moved.z;
    }
}
// Where the elbow goes for a hand at `target`: on the circle the two bones allow, on the side `pole` points
// to.
inline Vec3 elbowFor(Vec3 shoulder, Vec3 target, float upper, float lower, Vec3 pole) {
    Vec3 d = target - shoulder;
    const float dist = length(d);
    if (!(dist > 1e-4f))
        return shoulder + normalized(pole) * upper;
    const Vec3 dir = d / dist;
    const float reach = std::clamp(dist, std::abs(upper - lower) + .02f, upper + lower - .005f);
    const float along = (upper * upper - lower * lower + reach * reach) / (2 * reach);
    const float across = std::sqrt((std::max)(0.f, upper * upper - along * along));
    Vec3 side = pole - dir * dot(pole, dir);
    if (length(side) < 1e-3f)
        side = cross(dir, std::abs(dir.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
    return shoulder + dir * along + normalized(side) * across;
}
// Brings an arm's hand to `target` with the elbow towards `pole`, `weight` of the way from where the
// animation has it. The bones keep their lengths: the upper arm and then the forearm are turned, each with
// all that hangs on it.
inline bool reach(float* joints, const Arm& arm, Vec3 target, Vec3 pole, float weight) {
    if (!(weight > 1e-4f) || !finite(target) || !finite(pole))
        return false;
    const Vec3 shoulder = at(joints, arm.upper), elbow = at(joints, arm.lower), wrist = at(joints, arm.hand);
    const float upper = length(elbow - shoulder), lower = length(wrist - elbow);
    if (!(upper > .05f && lower > .05f))
        return false;
    const Vec3 bent = elbowFor(shoulder, target, upper, lower, pole);
    turn(joints, arm.belowUpper, partial(between(elbow - shoulder, bent - shoulder), weight), shoulder);
    const Vec3 elbow2 = at(joints, arm.lower), wrist2 = at(joints, arm.hand);
    turn(joints, arm.belowLower, partial(between(wrist2 - elbow2, target - elbow2), weight), elbow2);
    return true;
}

// The frame the pose is made in, measured from the joints at hand.
struct Frame {
    bool ok{};
    Vec3 left{}, up{}, forward{};
    Vec3 pelvis{}, chest{}, head{};
    float scale = 1.f; // the arms' length against a man's 0.62 m
};
inline Frame measure(const float* joints, const Rig& rig) {
    Frame f;
    f.pelvis = at(joints, rig.pelvis);
    f.chest = at(joints, rig.chest);
    f.head = at(joints, rig.head);
    const Vec3 across = at(joints, rig.arms[0].upper) - at(joints, rig.arms[1].upper);
    Vec3 up = f.head - f.pelvis;
    if (!finite(across) || !finite(up) || length(across) < .12f || length(up) < .25f)
        return f;
    f.left = normalized(across);
    up = up - f.left * dot(up, f.left);
    if (length(up) < .1f)
        return f;
    f.up = normalized(up);
    f.forward = cross(f.left, f.up);
    float arms{};
    for (const auto& arm : rig.arms)
        arms += length(at(joints, arm.lower) - at(joints, arm.upper)) +
                length(at(joints, arm.hand) - at(joints, arm.lower));
    f.scale = std::clamp(arms / 2 / .62f, .6f, 1.8f);
    f.ok = std::isfinite(f.scale);
    return f;
}

// How far the body leans and twists, radians: leaning in behind the guard, thrown back by a blocked blow,
// thrown forward by the strike, coiled and sprung by the swing.
constexpr float leanGuard = .12f, leanStrike = .16f, joltBack = .15f, twistMax = .40f;

// Poses the enemy's joints (model space, 16 floats a joint) as `e` says. False and the joints as they were
// when there is nothing to do or they are no good.
inline bool apply(float* joints, const Rig& rig, const Eval& e) {
    if (!joints || !rig.ready || !e.any() || !std::isfinite(e.guard + e.jolt + e.wind + e.strike + e.twist))
        return false;
    // The frame the pose is made in: the way he stands and faces before the pose moves him. The strike goes
    // where he faces, however far the body turns to throw it.
    const Frame f = measure(joints, rig);
    if (!f.ok)
        return false;
    // What the pose may move, to be put back whole if it ends up nowhere.
    static thread_local std::vector<float> saved;
    saved.resize(rig.belowChest.size() * 16);
    for (size_t i = 0; i < rig.belowChest.size(); ++i)
        std::copy_n(joints + rig.belowChest[i] * 16, 16, saved.begin() + static_cast<std::ptrdiff_t>(i * 16));
    // The body: the chest joint stays where it is and all above it leans and twists about it (the head keeps
    // most of its heading).
    const float lean = leanGuard * e.guard + leanStrike * e.strike - joltBack * e.jolt,
                twist = twistMax * e.twist;
    if (std::abs(lean) > 1e-4f || std::abs(twist) > 1e-4f) {
        const Quat q = Quat::around(f.up, twist) * Quat::around(f.left, lean);
        turn(joints, rig.belowChest, q, f.chest);
        turn(joints, rig.belowHead, partial(q.conjugate(), .6f), at(joints, rig.head));
    }
    // The arms. Both come up to the face, the elbows low; in the swing the right one goes back beside the
    // shoulder, then out at the end of its length, and back.
    const float k = f.scale;
    for (int i = 0; i < 2; ++i) {
        const Arm& arm = rig.arms[static_cast<size_t>(i)];
        const Vec3 out = f.left * (i == 0 ? 1.f : -1.f), shoulder = at(joints, arm.upper),
                   head = at(joints, rig.head);
        Vec3 target = head + f.forward * (.20f * k) + out * (.085f * k);
        Vec3 pole = f.up * -1.f + out * .35f + f.forward * .1f;
        if (i == 1 && (e.wind > 1e-4f || e.strike > 1e-4f)) {
            const float armLength = length(at(joints, arm.lower) - shoulder) +
                                    length(at(joints, arm.hand) - at(joints, arm.lower));
            const float g = (std::max)(0.f, 1 - e.wind - e.strike);
            const Vec3 windTarget = shoulder + out * (.07f * k) - f.forward * (.10f * k) - f.up * (.02f * k),
                       strikeTarget = shoulder + f.forward * (armLength * .96f) - f.up * (.04f * k);
            target = target * g + windTarget * e.wind + strikeTarget * e.strike;
            pole = pole * g + (out * .7f - f.forward * .6f - f.up * .3f) * e.wind +
                   (out * .5f - f.up * .8f) * e.strike;
        }
        reach(joints, arm, target, pole, e.guard);
    }
    // Nothing may come out of it that is not a number.
    for (const auto joint : rig.belowChest) {
        const float* r = joints + joint * 16;
        for (int i = 0; i < 15; ++i)
            if (!std::isfinite(r[i])) {
                for (size_t n = 0; n < rig.belowChest.size(); ++n)
                    std::copy_n(saved.begin() + static_cast<std::ptrdiff_t>(n * 16), 16,
                                joints + rig.belowChest[n] * 16);
                return false;
            }
    }
    return true;
}
} // namespace spidy::guard_pose
