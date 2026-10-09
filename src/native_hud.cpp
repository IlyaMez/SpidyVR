// The game's HUD in VR (native_hud.hpp): the panel's texture at the window's
// size, the second movie (markers, subtitles, prompts) drawn on it, and world
// markers projected from the head onto it. The panel's placement is in
// stereo_probe.cpp, with the eye views it must match.
#include "spidy/native_hud.hpp"
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
extern "C" {
__declspec(dllexport) native_hud::Data SpidyHudData;
}
namespace {
uintptr_t base{};
// ScaleformRTTStream's update (vtable 4f87960, slot 3): main thread, after the
// marker projections and before the HUD follower. For the panel's stream
// (+a0 set) it makes its colour texture (+10), depth texture (+18) and the
// render target over them (+20) at int(+78 * factor) x +7c when the factor
// (7a4ae50) differs from the one it was made with (+9c).
using StreamUpdate = void (*)(void*);
// The "Scaleform" render command: a render worker draws the frame's list of
// movies, each into its texture or, without one, into the game's view.
using RenderMovies = void (*)(void*, uint32_t);
// The game's marker projections: player view index, world point, then x and y
// (1f10ad0) or a 16-byte result whose first two floats they are (1f10b60),
// and the margins an on-screen point keeps from the edges.
using ProjectXY = bool (*)(int, const float*, float*, float*, float, float);
using Project = bool (*)(int, const float*, float*, float, float);
StreamUpdate originalStream{};
RenderMovies originalRender{};
ProjectXY originalProjectXY{};
Project originalProject{};
void* streamHook{};
void* renderHook{};
void* projectXYHook{};
void* projectHook{};
bool hooked{};
std::atomic<bool> enabled{};
SRWLOCK lifecycle = SRWLOCK_INIT, dataLock = SRWLOCK_INIT;
// Main thread: the follower seen last, the stream Spidy resized and the
// game's own base size for it.
uintptr_t hudFollower{}, resizedStream{};
uint32_t gameBase[2]{};
// The render target Spidy's size made (the render worker draws the second
// movie into it), when the headset last showed the eye views (immersive or
// flat; not the game screen of menus, cutscenes and loads, which copies the
// game's view), whether stop() asked for the game's size back, and whether it
// is back. The panel keeps the window's size all session: on the game screen
// it maps onto the window pixel for pixel, as the second movie does.
std::atomic<uintptr_t> vrTexture{};
std::atomic<uint64_t> eyesTick{};
std::atomic<bool> stopping{}, restored{true};
bool eyesRecent() {
    return GetTickCount64() - eyesTick.load() < 100;
}
// The HUD row (0 off, 1-3 its size; medium until the XR worker says).
std::atomic<int> hudSize{2};
// The panel's orientation from the head for recent eye commands (XR worker),
// and one a probe set for commands without (SpidyHudSet; w 0: none).
struct Followed {
    uint64_t serial{};
    Quat relative{};
};
std::array<Followed, 64> followRing{};
Quat probeRelative{0, 0, 0, 0};
SRWLOCK followLock = SRWLOCK_INIT;
struct Item {
    uint64_t movie, target;
    uint32_t stamp, clear;
};
static_assert(sizeof(Item) == 24);

bool read(uintptr_t p, void* out, size_t n) {
    __try {
        if (p < 0x10000)
            return false;
        std::memcpy(out, reinterpret_cast<void*>(p), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool write(uintptr_t p, const void* in, size_t n) {
    __try {
        if (p < 0x10000)
            return false;
        std::memcpy(reinterpret_cast<void*>(p), in, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
uintptr_t pointer(uintptr_t p) {
    uintptr_t v{};
    read(p, &v, 8);
    return v;
}
bool entry(uintptr_t rva, const unsigned char* expected, size_t n) {
    unsigned char b[32]{};
    return n <= sizeof(b) && read(base + rva, b, n) && !std::memcmp(b, expected, n);
}
// The second movie's window: the size its viewport (+a0) was given.
bool movieSize(uint32_t size[2]) {
    const auto movie = pointer(base + 0x7be3e00);
    int32_t viewport[2]{};
    if (!movie || !read(movie + 0xa0, viewport, sizeof(viewport)) || viewport[0] < 64 || viewport[1] < 64 ||
        viewport[0] > 16384 || viewport[1] > 16384)
        return false;
    size[0] = static_cast<uint32_t>(viewport[0]);
    size[1] = static_cast<uint32_t>(viewport[1]);
    return true;
}
void stream(void* self) {
    const auto s = reinterpret_cast<uintptr_t>(self);
    uint8_t panel{};
    if (!enabled || pointer(s) != base + 0x4f87960 || !read(s + 0xa0, &panel, 1) || !panel) {
        originalStream(self);
        return;
    }
    uint32_t size[2]{}, want[2]{}, window[2]{};
    read(s + 0x78, size, sizeof(size));
    if (s != resizedStream) {
        // A new stream (a load) has the game's own size.
        resizedStream = 0;
        gameBase[0] = size[0];
        gameBase[1] = size[1];
    }
    const bool vr = !stopping && movieSize(window);
    if (vr) {
        float factor{};
        read(base + 0x7a4ae50, &factor, 4);
        want[0] = native_hud::textureBase(window[0], std::max(factor, 1.f));
        want[1] = window[1];
    } else {
        want[0] = gameBase[0];
        want[1] = gameBase[1];
    }
    bool resized{};
    if ((size[0] != want[0] || size[1] != want[1]) && want[0] && want[1]) {
        // The stream makes a new texture when the factor it used differs.
        const float again = -1;
        resized = write(s + 0x78, want, sizeof(want)) && write(s + 0x9c, &again, 4);
        resizedStream = vr ? s : 0;
    }
    const auto before = pointer(s + 0x10);
    originalStream(self);
    // The colour texture (+10) the panel's material samples, and the render
    // target over it (+20, size at +1cc) the render command draws into.
    const auto texture = pointer(s + 0x10), target = pointer(s + 0x20);
    uint32_t made[2]{};
    read(target + 0x1cc, made, sizeof(made));
    if (texture != before && hudFollower) {
        // The panel's material still holds the old texture: the follower's
        // update, later this frame, binds the new one when +c0 is clear.
        const uint8_t bind{};
        write(hudFollower + 0xc0, &bind, 1);
    }
    vrTexture = vr && target && made[0] == window[0] && made[1] == window[1] ? target : 0;
    const bool back = !vr && size[0] == gameBase[0] && size[1] == gameBase[1] && !resized;
    restored = back;
    native_hud::publish([&](native_hud::Data& d) {
        d.textureSize[0] = made[0];
        d.textureSize[1] = made[1];
        d.gameTextureSize[0] = gameBase[0];
        d.gameTextureSize[1] = gameBase[1];
        d.restored = back;
        d.size = static_cast<uint32_t>(hudSize.load());
        if (resized)
            ++(vr ? d.textureResizes : d.textureRestores);
    });
}
// Render worker: while the headset shows the eye views, the second movie is
// drawn onto the panel's texture, after the panel's own movie cleared and drew
// it, not into the game's view (which only the game screen shows).
void render(void* payload, uint32_t count) {
    const auto texture = vrTexture.load();
    Item* moved{};
    // The HUD off: the second movie stays in the game's view, which the
    // headset shows only on the game screen.
    if (texture && enabled && hudSize.load() && eyesRecent() &&
        !(count && payload && pointer(reinterpret_cast<uintptr_t>(payload) + 8))) {
        // The list this frame draws, as the command picks it.
        const uint32_t frame = *reinterpret_cast<const uint32_t*>(base + 0x7a30b00);
        int list = -1;
        if (*reinterpret_cast<const uint32_t*>(base + 0x7be5740) == frame)
            list = 0;
        else if (*reinterpret_cast<const uint32_t*>(base + 0x7be5744) == frame)
            list = 1;
        if (list >= 0) {
            const uint32_t n = std::min(*reinterpret_cast<const uint32_t*>(base + 0x7be3f38 + list * 4), 128u);
            auto* items = reinterpret_cast<Item*>(base + 0x7be3f40 + list * 128 * sizeof(Item));
            const auto movie = pointer(base + 0x7be3e00);
            int panel = -1;
            for (uint32_t i = 0; i < n; ++i) {
                if (items[i].target == texture && panel < 0)
                    panel = static_cast<int>(i);
                else if (panel >= 0 && !items[i].target && items[i].movie == movie && !moved)
                    moved = &items[i];
            }
            if (moved)
                moved->target = texture;
        }
    }
    originalRender(payload, count);
    if (moved) {
        moved->target = 0;
        native_hud::publish([](native_hud::Data& d) { ++d.layerFrames; });
    }
}
// The HUD's own markers (POIs and the HUD elements that follow a point), not
// gameplay's on-screen checks, which call the same functions.
bool hudCaller(uintptr_t returnAddress) {
    const auto rva = returnAddress - base;
    return (rva >= 0x72a000 && rva < 0x7c0000) || (rva >= 0x1f00000 && rva < 0x1f10980);
}
// While immersive, the second movie's markers are placed for the panel in
// front of the head, not for the game's view: where the line from the head to
// the point crosses the panel, which faces the way Follow turned it. (On the
// flat screen the panel is where the game put it, before the game's camera:
// its own projection fits.)
bool panelPoint(const float* world, float& x, float& y, bool& front) {
    native_hud::Head head;
    uint32_t window[2]{};
    Vec3 point{};
    if (!vrTexture.load() || !hudSize.load() || native_hud::eyes(head) != 1 || !movieSize(window) ||
        !read(reinterpret_cast<uintptr_t>(world), &point, sizeof(point)) ||
        !native_hud::project(head.panel, head.halfWidth, static_cast<float>(window[0]) / static_cast<float>(window[1]),
                             point, .05f, x, y, front))
        return false;
    native_hud::publish([](native_hud::Data& d) { ++d.markerProjections; });
    return true;
}
bool onPanel(float x, float y, bool front, float margin, float marginY) {
    return front && x >= margin && x <= 1 - margin && y >= marginY && y <= 1 - marginY;
}
bool markerXY(int index, const float* world, float* x, float* y, float margin, float marginY) {
    const bool shown = originalProjectXY(index, world, x, y, margin, marginY);
    float px{}, py{};
    bool front{};
    if (!enabled || !hudCaller(reinterpret_cast<uintptr_t>(_ReturnAddress())) || !panelPoint(world, px, py, front))
        return shown;
    *x = px;
    *y = py;
    return onPanel(px, py, front, margin, marginY);
}
bool marker(int index, const float* world, float* out, float margin, float marginY) {
    const bool shown = originalProject(index, world, out, margin, marginY);
    float px{}, py{};
    bool front{};
    if (!enabled || !hudCaller(reinterpret_cast<uintptr_t>(_ReturnAddress())) || !panelPoint(world, px, py, front))
        return shown;
    out[0] = px;
    out[1] = py;
    return onPanel(px, py, front, margin, marginY);
}
} // namespace

native_hud::Data& native_hud::beginEdit() {
    AcquireSRWLockExclusive(&dataLock);
    InterlockedIncrement64(&SpidyHudData.sequence);
    return SpidyHudData;
}
void native_hud::endEdit() {
    InterlockedIncrement64(&SpidyHudData.sequence);
    ReleaseSRWLockExclusive(&dataLock);
}
uint32_t native_hud::start(uintptr_t imageBase) {
    AcquireSRWLockExclusive(&lifecycle);
    uint32_t result{};
    do {
        if (enabled)
            break;
        base = imageBase;
        const unsigned char streamBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                             0x48, 0x89, 0x7c, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x56};
        const unsigned char renderBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08, 0x55,
                                             0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
        const unsigned char projectXYBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                                                0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48};
        const unsigned char projectBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                                              0xec, 0x40, 0x48, 0x8b, 0xfa, 0x0f, 0x29, 0x74};
        if (!base || !entry(0x2104400, streamBytes, sizeof(streamBytes)) ||
            !entry(0x1d2b6c0, renderBytes, sizeof(renderBytes)) ||
            !entry(0x1f10ad0, projectXYBytes, sizeof(projectXYBytes)) ||
            !entry(0x1f10b60, projectBytes, sizeof(projectBytes)) ||
            pointer(base + 0x4f87960 + 0x18) != base + 0x2104400) {
            result = 9501;
            break;
        }
        if (!hooked) {
            auto s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
                result = 9600 + s;
                break;
            }
            streamHook = reinterpret_cast<void*>(base + 0x2104400);
            renderHook = reinterpret_cast<void*>(base + 0x1d2b6c0);
            projectXYHook = reinterpret_cast<void*>(base + 0x1f10ad0);
            projectHook = reinterpret_cast<void*>(base + 0x1f10b60);
            s = MH_CreateHook(streamHook, reinterpret_cast<void*>(stream), reinterpret_cast<void**>(&originalStream));
            if (s == MH_OK)
                s = MH_CreateHook(renderHook, reinterpret_cast<void*>(render),
                                  reinterpret_cast<void**>(&originalRender));
            if (s == MH_OK)
                s = MH_CreateHook(projectXYHook, reinterpret_cast<void*>(markerXY),
                                  reinterpret_cast<void**>(&originalProjectXY));
            if (s == MH_OK)
                s = MH_CreateHook(projectHook, reinterpret_cast<void*>(marker),
                                  reinterpret_cast<void**>(&originalProject));
            if (s != MH_OK) {
                for (auto hook : {streamHook, renderHook, projectXYHook, projectHook})
                    MH_RemoveHook(hook);
                result = 9700 + s;
                break;
            }
            hooked = true;
        }
        stopping = false;
        enabled = true;
        for (auto hook : {streamHook, renderHook, projectXYHook, projectHook})
            if (const auto s = MH_EnableHook(hook); s != MH_OK && !result)
                result = 9800 + s;
        if (result) {
            enabled = false;
            for (auto hook : {streamHook, renderHook, projectXYHook, projectHook})
                MH_DisableHook(hook);
        }
    } while (false);
    publish([&](Data& d) { d.layerStatus = result; });
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
void native_hud::eyesShown() {
    eyesTick = GetTickCount64();
}
void native_hud::setSize(int size) {
    hudSize = std::clamp(size, 0, 3);
}
int native_hud::size() {
    return hudSize.load();
}
void native_hud::follow(uint64_t serial, const Quat& relative) {
    AcquireSRWLockExclusive(&followLock);
    followRing[serial % followRing.size()] = {serial, relative};
    ReleaseSRWLockExclusive(&followLock);
}
bool native_hud::followed(uint64_t serial, Quat& relative) {
    AcquireSRWLockShared(&followLock);
    const auto entry = followRing[serial % followRing.size()];
    const auto probe = probeRelative;
    ReleaseSRWLockShared(&followLock);
    if (serial && entry.serial == serial) {
        relative = entry.relative;
        return true;
    }
    relative = probe.w != 0 ? probe : Quat{};
    return false;
}
void native_hud::hiddenDraw() {
    publish([](Data& d) { ++d.hiddenDraws; });
}
namespace {
// SpidyHudSet (probes without a VR session): the HUD row, and how the panel
// faces from the head for eye commands the XR worker did not follow (a unit
// quaternion in the head's OpenXR axes; all 0: the way the head does).
struct ProbeSet {
    uint32_t magic = 0x53554853, version = 1, bytes = sizeof(ProbeSet), size{};
    float relative[4]{};
};
static_assert(sizeof(ProbeSet) == 32);
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidyHudSet(void* input) {
    ProbeSet s;
    if (!read(reinterpret_cast<uintptr_t>(input), &s, sizeof(s)))
        return 9601;
    const ProbeSet expected;
    Quat q{s.relative[0], s.relative[1], s.relative[2], s.relative[3]};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (s.magic != expected.magic || s.version != expected.version || s.bytes != expected.bytes || s.size > 3 ||
        !std::isfinite(n) || (n != 0 && std::abs(n - 1) > .01f))
        return 9602;
    native_hud::setSize(static_cast<int>(s.size));
    AcquireSRWLockExclusive(&followLock);
    probeRelative = n != 0 ? q : Quat{0, 0, 0, 0};
    ReleaseSRWLockExclusive(&followLock);
    return 0;
}
void native_hud::follower(uintptr_t address) {
    if (pointer(address) == base + 0x38929c8)
        hudFollower = address;
}
uint32_t native_hud::stop(uint32_t waitMs) {
    AcquireSRWLockExclusive(&lifecycle);
    uint32_t result{};
    if (enabled) {
        // The game's next frames make the panel's texture at its own size
        // again and bind it.
        stopping = true;
        vrTexture = 0;
        const auto limit = GetTickCount64() + waitMs;
        while (!restored && GetTickCount64() < limit)
            Sleep(10);
        if (!restored)
            result = 9901;
        enabled = false;
        vrTexture = 0;
        for (auto hook : {streamHook, renderHook, projectXYHook, projectHook})
            if (const auto s = MH_DisableHook(hook); s != MH_OK && s != MH_ERROR_DISABLED)
                result = 9910 + s;
    }
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
