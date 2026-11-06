#pragma once
#include <cstdint>
#include <dxgiformat.h>

namespace spidy::hud_capture {
// A probe for a steadier HUD: can Spidy take the game's HUD panel texture
// (native_hud.hpp) on the GPU, so that it could draw the panel itself every
// headset frame instead of the game drawing it into each eye pair? Only with
// STEADY HUD on; nothing the headset shows changes. While immersive, the
// panel's colour texture is found from the stream native_hud resizes, its
// D3D12 resource is followed through the game's barriers (native_eye_gpu.cpp),
// and every few seconds, at a Present of the game's frame, it is copied back
// to memory for the session report (SpidyHudCapture; the report saves it as a
// picture).
// STEADY HUD (steady_hud.hpp) copies the same texture with every eye pair.

// How the D3D12 resource was reached from the stream's colour texture (+10).
enum Path : uint32_t {
    pathNone = 0,
    pathTexture = 1, // the colour texture is the game's texture object itself
    pathWrapped = 2, // a wrapper whose +40 is the texture object, as the eye views' are
    pathTarget = 3,  // the same through the render target (+20)
};
// Why the last copy did not happen (0: it did).
enum Skip : uint32_t {
    skipNone = 0,
    skipNoResource = 1, // no panel texture registered
    skipUnknownState = 2,
    skipOtherDevice = 3,
    skipBusy = 4,   // the last copy is still on the GPU
    skipFormat = 5, // not a format the probe reads back
    skipFailed = 6, // a D3D12 call failed
    skipStale = 7,  // the game stopped drawing into the followed texture (remade): the eyes show the panel
    skipBlank = 8,  // the followed texture is empty (alpha 0) for two snapshots: the eyes show the panel
};
// The followed texture got no barrier for this long while its copies were wanted: the game draws the panel
// into another texture, and the copy would show the layer an empty panel.
inline constexpr uint64_t staleMs = 2000;
// Snapshots taken at most this often. Each is also looked at (below): a panel that stays empty is not copied.
inline constexpr uint64_t snapshotMs = 1000;
// Whether a snapshot of 8-bit colour (4 bytes a pixel, alpha last) has any pixel that is not transparent.
// Looks at every 5th pixel of every 3rd row: a panel with something on it has far more than that.
inline bool anyAlpha(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t rowBytes) {
    for (uint32_t y = 0; y < height; y += 3)
        for (uint32_t x = 0; x < width; x += 5)
            if (pixels[static_cast<size_t>(y) * rowBytes + static_cast<size_t>(x) * 4 + 3])
                return true;
    return false;
}
// Snapshots in a row that found nothing before the panel counts as empty (a HUD fading out for a
// moment is not).
inline constexpr uint32_t blankScans = 2;
inline bool snapshotDue(uint64_t nowMs, uint64_t lastMs, bool taken) {
    return !taken || nowMs - lastMs >= snapshotMs;
}
// Bytes per pixel of a format the probe copies back, 0 for others. `swap`:
// stored blue first, so the picture needs red and blue swapped.
inline uint32_t readableBytes(DXGI_FORMAT format, bool& swap) {
    swap = false;
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return 4;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        swap = true;
        return 4;
    default:
        return 0;
    }
}
// SpidyHudCapture: what the probe found and its newest snapshot.
struct Data {
    uint32_t magic = 0x53484350, version = 2, bytes = sizeof(Data), path{};
    int64_t sequence{};
    // The resource and its description (DXGI format, mips, flags).
    uint64_t resource{};
    uint32_t width{}, height{}, format{}, mips{}, flags{}, samples{};
    // The game's barriers on it: seen in its command lists, executed on the
    // game's queue and on another queue, those with flags or one subresource,
    // and those whose state before was not the one followed. The state after
    // the newest executed one, and whether it is known.
    uint64_t barriers{}, executed{}, otherQueue{}, odd{}, mismatched{};
    uint32_t state{}, known{};
    // Presents of the game's frame seen while wanted, copies made, why the
    // last was skipped, and failed registrations (a texture object of another
    // kind, a resource of another device or shape).
    uint64_t presents{}, copies{};
    uint32_t skip{}, refused{};
    // The newest snapshot: RGBA8 rows (`swap`: BGRA) of `rowPitch` bytes,
    // unchanged until `snapshots` grows.
    uint64_t snapshots{}, pixels{};
    uint32_t rowPitch{}, swap{};
    // STEADY HUD: the panel copied with the eye pairs for its layer, the
    // copies into the layer's image, the headset frames that showed the
    // layer, and why the last pair's copy was skipped (0: it was made).
    // `registered`: how many times the panel's texture was followed anew (a
    // death, a checkpoint or a scene's end makes the game remake it): a
    // session whose `executed` stopped growing while `registered` did not has
    // a panel nobody follows.
    uint64_t staged{}, layerCopies{}, layerFrames{};
    uint32_t stageSkip{}, registered{};
};
static_assert(sizeof(Data) == 184);
} // namespace spidy::hud_capture
