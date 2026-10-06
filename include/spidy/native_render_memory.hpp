#pragma once
#include <cstdint>

// The game's frame memory for rendering: "RenderAlloc", at 7938880 in Steam
// 4.0630. The draw lists, job data and render commands of every view come out
// of one ring each frame. A frame may use the whole ring except what the frame
// before it used, which the render thread is still drawing (1872b90). The ring
// is 128 MB, and the stock game uses 6-8 MB of it a frame. With the two eye
// views and the game view moved to the head, a frame uses 50-60 MB at Times
// Square, so two frames barely fit and a heavier view does not. A request that
// does not fit returns null (1872930). The game then leaves out that view's
// work for the frame (19206a0 drops the view, 1793be0 its actors), and callers
// that do not check the result crash. In the headset that was eye images
// arriving a quarter as often, missing buildings, and finally a crash.
//
// The ring cannot change while frames exist: render commands refer to frame
// memory by 32-bit offsets from the ring's base (179db08, 1770a33), and the
// address space around the ring is taken. So Spidy replaces the ring where the
// game creates it (1872d90), about three seconds after the process starts, and
// must be loaded before that.
namespace spidy::native_render_memory {
#pragma pack(push, 4)
// The allocator from +0x10: the two free regions of the current frame, the
// ring, four flag bytes, the last frame's use, and the most two consecutive
// frames have used.
struct Fields {
    uint64_t first{};
    uint32_t firstSize{}, firstUsed{};
    uint64_t second{};
    uint32_t secondSize{}, secondUsed{};
    uint64_t ring{};
    uint32_t reserved{}, committed{};
    uint8_t flags[4]{}; // +0x41 is set when a request did not fit; the game clears it each frame
    uint32_t lastFrame{}, worstPair{};
};
#pragma pack(pop)
static_assert(sizeof(Fields) == 0x3c);
// A ring this module understands.
inline bool usable(const Fields& f) {
    const auto inside = [&](uint64_t region, uint32_t size) {
        return !region || (region >= f.ring && region - f.ring + size <= f.committed);
    };
    return f.ring > 0x10000 && f.committed >= (16u << 20) && f.committed <= f.reserved &&
           f.reserved <= (1u << 30) && inside(f.first, f.firstSize) && inside(f.second, f.secondSize);
}
// The allocator as the game's own creation leaves it: nothing allocated, the
// whole ring free for the first frame.
inline bool untouched(const Fields& f) {
    return usable(f) && f.first == f.ring && f.firstSize == f.committed && !f.firstUsed && !f.second &&
           !f.secondSize && !f.secondUsed && !f.lastFrame && !f.worstPair;
}
// The same newly created allocator on another ring of `bytes`.
inline Fields replaced(Fields f, uint64_t ring, uint32_t bytes) {
    f.first = f.ring = ring;
    f.firstSize = f.reserved = f.committed = bytes;
    return f;
}
// Data::status for the allocator as a session finds it. `installed` is the
// ring this module put in place, 0 if none: the ring outlives the session
// that installed it, and a later session on the same game must recognize it.
inline uint32_t ringStatus(const Fields& f, uint64_t installed) {
    return !f.ring ? 1 : f.ring == installed ? 3 : 2;
}
// Telemetry, read by the session tools.
struct Data {
    uint32_t magic = 0x53524d44, version = 1, bytes = sizeof(Data);
    // 0 off, 1 waiting for the game to create its ring, 2 the game's own ring
    // (Spidy came too late or was told to keep it), 3 Spidy's ring.
    uint32_t status{};
    volatile int64_t sequence{};
    uint64_t frames{}, overflowFrames{}; // frames seen; frames in which a request did not fit
    uint32_t gameRingKb{}, ringKb{};     // the ring the game created; the ring in use
    uint32_t lastFrameKb{}, worstFrameKb{}, worstPairKb{};
    uint32_t error{};
};
static_assert(sizeof(Data) == 64);
} // namespace spidy::native_render_memory
