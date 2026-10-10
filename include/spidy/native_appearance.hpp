#pragma once
#include <cstdint>

namespace spidy::native_appearance {
// Set before starting native eyes. The local actor record belongs to the same
// verified game process as the tracking and movement adapters.
void setPlayerRecord(uint64_t record);
// heroState values: the record's first field must be a registered render
// instance before the game's own actor visibility switch is used on it.
enum HeroState : uint32_t {
    heroUnknown = 0,
    heroValid = 1,
    heroNoInstance = 2,
    heroUnregistered = 3,
    heroBadTransform = 4,
};
struct Data {
    uint32_t magic = 0x53415044, version = 5, bytes = sizeof(Data), heroState{};
    int64_t sequence{};
    uint64_t hiddenAvatar[2]{}, srgbOverlay[2]{}, playerActor{};
    // Frames rendered with the hero's native visibility switched off for
    // immersive VR, and the number of off/on transitions this module made.
    uint64_t nativeHiddenFrames{}, nativeHides{}, nativeRestores{};
    uint32_t heroHandle{}, heroFlags{};
    // Small eye-view instances still drawn within 1.1 m of the hero's body
    // centre (0.9 m above the feet), with the last one's identity.
    uint64_t nearInstances[2]{}, nearInstance{}, nearModel{};
    float nearDistance{}, nearRadius{};
    uint32_t nearHandle{}, nearFlags{};
    // Tracked eyes moved to the player position of the rendered frame.
    uint64_t anchoredFrames{}, anchorRejected{};
    float anchorLast{}, anchorMax{};
    double anchorSum{};
    // Game-drawn web lines (native_webs::Status): state, hands with a rope,
    // and rope creations, releases, failures, and hero rope-manager updates.
    uint32_t webState{}, webLive{};
    uint64_t webCreates{}, webReleases{}, webFailures{}, webUpdates{};
    // Stock-camera submits moved to the tracked head while immersive (the
    // engine's active view), immersive submits left unchanged because no
    // valid head view existed, the last head lens (tangents, +y down), and
    // how far that moved the active view from the stock camera, in metres.
    uint64_t activeAligned{}, activeRejected{};
    float activeBounds[4]{};
    float activeShift{};
    // Eye render jobs that read the main view's adapted luminance (auto
    // exposure) instead of the eye's own.
    uint32_t exposureShared{};
    // Anchored frames whose eyes were placed from the hero position the game's
    // web lines started from that frame, and how far the hero's render
    // transform was from it when the eyes were placed, in metres.
    uint64_t sharedHeroFrames{};
    float heroLagLast{}, heroLagMax{};
    double heroLagSum{};
    // native_webs::Status start error: rope-built start to requested start.
    float webStartLast = -1, webStartMax = -1;
    // New eye images whose overlay drew a hand holding a game web, and the
    // distance in that image's frame from the hand's web shooter to the first
    // point of the game's rope, in metres. Eyes placed a frame late put this
    // at one frame of player travel (0.7 m at 32 m/s) until October 5.
    uint64_t webGapFrames{};
    float webGapLast{}, webGapMax{};
    double webGapSum{};
    // Frames in which the engine's active (culling) view was placed from a
    // hero position other than the eyes', and that distance in metres.
    uint64_t activeLagFrames{};
    float activeLagLast{}, activeLagMax{};
};
static_assert(sizeof(Data) == 328);
} // namespace spidy::native_appearance
