#pragma once
#include "native_eye_frame.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// What the game streams and swaps in for the city, decided for the headset
// (stereo_probe.cpp) rather than for one eye's exact picture.
//
// Far buildings are impostors, cards that stand in for the model. Which of
// them draw their full models the game decides for one view, from the
// impostors that view's frustum query returns (16faf10, "ImpostorSubmitQueries
// in frustum") within its reach: each frame's wanted states start at none
// (16fadbe clears them), the occlusion job sets them for the returned
// impostors within reach (16f8ae0), and the registration job (16f8940, 16f7f30)
// takes away the full model of every other one that same frame. In VR the left
// eye stands in for the game's resting view (isActiveView), so a building that
// left that eye's picture became its card at once, the right eye's outer edge
// never counted, and each look to the side swapped buildings back in front of
// you, the full model fading in and then streaming its textures.
//
// The left eye's query now takes a cone about the head: the lens covering both
// eyes, widened by a margin each way. Its result goes no further than choosing
// the full models and the left eye's cards, which its own culling still clips.
namespace spidy::native_stream {
// The margin the cone is widened by each way, and the most it reaches from
// straight ahead, degrees.
inline constexpr float coneMargin = 30, coneMost = 80;
// `lens` (tangents, +y down, as native_eyes::headView gives them) widened by
// `margin` radians each way, to at most `most` radians from straight ahead,
// and never narrower than it was.
inline native_eyes::Bounds widened(const native_eyes::Bounds& lens, float margin, float most) {
    const auto side = [&](float tangent, float sign) {
        const float angle = std::atan(tangent * sign);
        return sign * std::tan(std::max(angle, std::min(angle + margin, most)));
    };
    for (float t : {lens.left, lens.right, lens.top, lens.bottom})
        if (!std::isfinite(t))
            return lens;
    if (!std::isfinite(margin) || !std::isfinite(most) || margin <= 0 || most <= 0 || most >= 1.5f)
        return lens;
    return {side(lens.left, -1), side(lens.right, 1), side(lens.top, -1), side(lens.bottom, 1)};
}
// The full models take their slots from a pool of the scene's (7a42f80: slots
// in use at +1cc, its size at +1c8, from the game's Scene settings) that also
// registers the models of the props and details near the head (hibernate
// zones, 1a22530), each checked for room first (190cb80): a full pool leaves
// a zone's details unregistered until slots free up. The cone's margin eases
// off as the pool fills past `easeFrom` of it, to none at `none`, so buildings
// to the side never hold slots the nearby details need. 0 for a pool that does
// not read as one.
inline float poolMargin(int32_t used, int32_t capacity, float margin, float easeFrom = .6f, float none = .85f) {
    if (capacity <= 0 || used < 0 || used > capacity || !(margin > 0))
        return 0;
    const float fill = static_cast<float>(used) / static_cast<float>(capacity);
    return margin * std::clamp((none - fill) / (none - easeFrom), 0.f, 1.f);
}
// The texture streamer's budget (7a793c0, bytes: the Texture Quality setting's
// TextureAlloc size, 3,874 MB at Very High) is an account it evicts by
// (19b50b0: room = budget - its heap's use - 16 MB), not memory set aside: the
// heap grows as textures load (19b2e50 makes it with no size). The eyes see
// more of the city at once than the monitor, and in VR the budget was full in
// every minute of play (3,858 to 3,872 MB of 3,874 on October 26, a 32 GB card
// with 22 GB of it unused), so a texture came in sharp only once another went.
// Twice the budget was full as well from the fourth minute on (7,741 to 7,747
// MB of 7,748 on October 30, 18 GB of the card unused). Where the card has
// room, the eyes get up to `most` times the game's budget, leaving `keep`
// free: an eighth of the card's budget for the game (DXGI's), at least 2 GB.
// `game`, `videoBudget` and `videoUsage` in bytes; the game's own where there
// is no room.
inline constexpr uint64_t textureMost = 4;
inline uint64_t textureBudget(uint64_t game, uint64_t videoBudget, uint64_t videoUsage,
                              uint64_t most = textureMost) {
    constexpr uint64_t mb = 1ull << 20;
    if (!game || !videoBudget || videoUsage >= videoBudget || most < 1)
        return game;
    const uint64_t keep = std::max<uint64_t>(2048 * mb, videoBudget / 8);
    const uint64_t room = videoBudget - videoUsage;
    return room > keep ? game + std::min(game * (most - 1), (room - keep) / mb * mb) : game;
}
// The card running short with the raised budget: less than 1 GB of DXGI's
// budget left. The game's own budget then comes back for the session.
inline bool textureShort(uint64_t videoBudget, uint64_t videoUsage) {
    return !videoBudget || videoUsage + (1024ull << 20) > videoBudget;
}
// SpidyStreamData: what stereo_probe.cpp did about both, for the session report.
struct Data {
    // features: 1 the cone, 2 the texture budget, 4 the pool's room checks
    // counted, where the game's code read as stereo_probe.cpp expects it.
    uint32_t magic = 0x53545244, version = 2, bytes = sizeof(Data), features{};
    int64_t sequence{};
    // Frames the left eye's impostor query took the head's cone, and frames
    // it kept the eye's own lens while standing in (no cone placed that frame).
    uint64_t coneFrames{}, ownFrames{};
    // The cone's margin and its widest half angles, across and up and down,
    // in the last such frame, degrees.
    float margin{}, across{}, upDown{};
    // The scene's slot pool (poolMargin): its size, slots in use at the last
    // query, the most seen, and queries that found it 85% full or more.
    int32_t poolCapacity{}, poolUsed{}, poolMost{};
    uint32_t poolFullFrames{};
    // The texture budget: the game's and the eyes' (MB), the card's budget for
    // the game and the game's use of it when it was raised (MB), and how many
    // times the game's own came back because the card ran short.
    uint32_t textureGame{}, textureBudget{}, videoBudget{}, videoUsage{}, textureDrops{};
    // The pool's room checks (190cb80) by who asked: a far building's full
    // model (16f7f30) and a hibernate zone's props and details (1a22530);
    // the refused ones left that building a card, or those props unshown,
    // until a later frame found room. The most slots one check asked for.
    uint64_t buildingChecks{}, buildingRefusals{}, zoneChecks{}, zoneRefusals{};
    int32_t mostAsked{};
    uint32_t reserved{};
};
static_assert(sizeof(Data) == 128);
} // namespace spidy::native_stream
