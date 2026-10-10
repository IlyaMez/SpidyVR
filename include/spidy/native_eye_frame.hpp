#pragma once
#include "native_view.hpp"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace spidy::native_eyes {
struct EyePose {
    Mat4 world{};
    float fov[4]{};
}; // left, right, down, up
struct Command {
    uint32_t magic = 0x53455043, version = 2, bytes = sizeof(Command), enabled{};
    uint64_t serial{};
    uint32_t leaseMs = 250, reserved{};
    std::array<EyePose, 2> eyes{};
    // Player position used to place the eyes. The render thread moves both eyes
    // by the player's travel since that sample, so camera and body share a frame.
    Vec3 anchor{};
    uint32_t anchored{};
};
static_assert(sizeof(Command) == 208);
// Player travel between the tracking sample and the rendered simulation frame.
// Larger jumps are teleports or bad reads; those frames keep the sampled pose.
inline bool reanchorOffset(Vec3 sampled, Vec3 rendered, Vec3& offset, float limit = 20) {
    offset = rendered - sampled;
    if (finite(sampled) && finite(offset) && length(offset) <= limit)
        return true;
    offset = {};
    return false;
}
inline bool valid(const Command& c) {
    if (c.magic != 0x53455043 || c.version != 2 || c.bytes != sizeof(c) || c.enabled > 2 || !c.serial ||
        c.leaseMs > 500 || c.reserved || c.anchored > 1 || !finite(c.anchor) || std::abs(c.anchor.x) > 1e6f ||
        std::abs(c.anchor.y) > 1e6f || std::abs(c.anchor.z) > 1e6f)
        return false;
    if (!c.enabled)
        return true;
    if (!c.leaseMs)
        return false;
    if (c.enabled == 2)
        return true; // Flat mode clones the engine's own camera descriptor.
    for (const auto& e : c.eyes) {
        if (!native_view::validPose(e.world))
            return false;
        for (float f : e.fov)
            if (!std::isfinite(f) || std::abs(f) > 1.5f)
                return false;
        if (e.fov[0] >= -.01f || e.fov[1] <= .01f || e.fov[2] >= -.01f || e.fov[3] <= .01f)
            return false;
    }
    // Reject a malformed pair rather than rendering unrelated cameras.
    const auto& l = c.eyes[0].world;
    const auto& r = c.eyes[1].world;
    return length(Vec3{l[12] - r[12], l[13] - r[13], l[14] - r[14]}) <= .2f;
}
// Native lens tangents (descriptor +400..+40c); +y points down.
struct Bounds {
    float left{}, right{}, top{}, bottom{};
};
// Head camera for the engine's active view: centred between the eyes, oriented
// as their average, with the smallest lens that holds both eye frustums
// (widened by margin), so culling done for that view covers both eye images.
inline bool headView(const Command& c, Mat4& pose, Bounds& lens, float margin = 1.05f) {
    const auto& a = c.eyes[0].world;
    const auto& b = c.eyes[1].world;
    // Rows right, down, forward, position, as native_view::validPose expects.
    const Vec3 z = normalized(Vec3{a[8] + b[8], a[9] + b[9], a[10] + b[10]});
    const Vec3 sum{a[0] + b[0], a[1] + b[1], a[2] + b[2]};
    const Vec3 x = normalized(sum - z * dot(sum, z)), y = cross(z, x);
    const Vec3 p{(a[12] + b[12]) / 2, (a[13] + b[13]) / 2, (a[14] + b[14]) / 2};
    pose = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1};
    if (!native_view::validPose(pose))
        return false;
    lens = {1e9f, -1e9f, 1e9f, -1e9f};
    for (const auto& eye : c.eyes) {
        const auto& m = eye.world;
        const Vec3 ex{m[0], m[1], m[2]}, ey{m[4], m[5], m[6]}, ez{m[8], m[9], m[10]};
        // Each corner of the eye's image (fov: left, right, down, up), seen from the head.
        for (float h : {std::tan(eye.fov[0]), std::tan(eye.fov[1])})
            for (float v : {-std::tan(eye.fov[3]), -std::tan(eye.fov[2])}) {
                const Vec3 d = ex * h + ey * v + ez;
                const float depth = dot(d, z);
                if (!(depth > .05f))
                    return false;
                lens.left = std::min(lens.left, dot(d, x) / depth);
                lens.right = std::max(lens.right, dot(d, x) / depth);
                lens.top = std::min(lens.top, dot(d, y) / depth);
                lens.bottom = std::max(lens.bottom, dot(d, y) / depth);
            }
    }
    if (!(lens.left < 0 && lens.right > 0 && lens.top < 0 && lens.bottom > 0))
        return false;
    lens = {lens.left * margin, lens.right * margin, lens.top * margin, lens.bottom * margin};
    return lens.left > -100 && lens.right < 100 && lens.top > -100 && lens.bottom < 100;
}
struct FrameData {
    uint32_t magic = 0x53455044, version = 4, bytes = sizeof(FrameData), error{};
    int64_t sequence{};
    uint64_t accepted{}, latched{}, jobCopies[2]{}, jobBegins[2]{}, jobEnds[2]{};
    uint64_t copiedSerial[2]{}, begunSerial[2]{}, endedSerial[2]{};
    uint64_t copiedJob[2]{};
    // Eye job copies the game dropped without rendering, reclaimed by age.
    uint64_t reclaimed{};
    // Per eye, at each job copy: whether the eye's previous camera position
    // (view +560, shifted by the engine at the start of the frame) differs
    // from the one being rendered, and whether that pose was placed in the
    // frame being rendered or in an earlier one. Eyes placed in view
    // maintenance were always a frame late and never had a moving history.
    uint64_t historyMoved[2]{}, historyStill[2]{}, sameFramePoses[2]{}, lateFramePoses[2]{};
    // Left-eye job copies made while the left hand held a game rope, and the
    // newest rope's first point relative to that eye's rendered position. With
    // a fixed hand and head this stays constant; an eye placed a frame late
    // shifts it by the player's travel in one frame.
    uint64_t ropeFrames{};
    float ropeFromEye[3]{};
    uint32_t reserved{};
};
static_assert(sizeof(FrameData) == 248);
// Hero position a frame's views are placed from. The game's web lines start
// where its hero rope update read the hero, which can trail the render
// transform by a simulation step; eyes placed from that same sample keep the
// webs on the tracked wrists at any speed. A sample is used only by the first
// frame after it was taken. Call once per frame, before any early exit.
class FrameHero {
  public:
    // `samples` counts rope updates so far; `sample` is the newest position.
    bool fresh(uint64_t samples, Vec3 sample) {
        const bool unused = samples != seen_;
        seen_ = samples;
        return unused && samples && finite(sample) && std::abs(sample.x) < 1e6f && std::abs(sample.y) < 1e6f &&
               std::abs(sample.z) < 1e6f;
    }

  private:
    uint64_t seen_{};
};
// Where a frame will draw the hero, asked before its gameplay has moved him
// (the HUD places its world markers first): where the last frame's views were
// placed from, moved on by the travel that frame made. A longer step is a
// teleport and is not carried on.
class HeroTravel {
  public:
    // The hero position frame `frame`'s views were placed from.
    void placed(uint64_t frame, Vec3 hero, float limit = 5) {
        const Vec3 step = hero - hero_;
        travel_ = known_ && frame == frame_ + 1 && finite(step) && length(step) <= limit ? step : Vec3{};
        hero_ = hero;
        frame_ = frame;
        known_ = finite(hero);
    }
    // Where frame `frame` will draw him; false unless the frame before it was placed.
    bool ahead(uint64_t frame, Vec3& hero) const {
        if (!known_ || frame != frame_ + 1)
            return false;
        hero = hero_ + travel_;
        return true;
    }

  private:
    Vec3 hero_{}, travel_{};
    uint64_t frame_{};
    bool known_{};
};
// World offset the native views applied to the command rendered in a scene
// generation. Overlays add it to their eye and hand poses for that image.
bool renderOffset(uint64_t generation, Vec3& offset);
// First point of a hand's game rope in the frame a scene generation rendered.
bool renderWebStart(uint64_t generation, unsigned hand, Vec3& start);
// Largest distance, in one new image, from a tracked web shooter to the game
// rope it holds. The session report shows whether webs leave the hands.
void reportWebGap(float metres);
} // namespace spidy::native_eyes
