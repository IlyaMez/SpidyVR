#pragma once
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace spidy::native_gpu {
struct Config {
    uint32_t magic = 0x53475043, version = 3, bytes = sizeof(Config), pid{};
    uint64_t base{}, queue{};
    uint32_t durationMs = 10000, copyToXr{}; // 0 capture, 1 direct, 2 staged
    uint32_t captureImages = 1, reserved{};
};
struct Data {
    uint32_t magic = 0x53475044, version = 2, bytes = sizeof(Data), status{};
    int64_t sequence{};
    uint32_t error{}, reserved{};
    uint64_t executes{}, markers{}, matchedMarkers{}, pairs{}, serial{}, generation{}, fence{}, completed{};
    uint64_t begins[2]{}, ends[2]{}, resources[2]{}, pixels[2]{};
    uint32_t states[2]{}, known[2]{}, width{}, height{};
    uint64_t captured{}, reused{}, capturedSerial{};
};
static_assert(sizeof(Config) == 48 && sizeof(Data) == 208);
// Called around the engine's timestamp operation. The D3D12 EndQuery detour
// attaches the stamp to its actual command list, without adding game commands.
void enterMarker(unsigned eye, uint64_t serial, uint64_t generation, bool ended);
void leaveMarker();
void registerEye(unsigned eye, ID3D12Resource* resource);
// Targets have been acquired and waited by OpenXR. They enter and leave in
// RENDER_TARGET state. Arm before publishing the corresponding eye command.
bool arm(uint64_t serial, ID3D12Resource* left, ID3D12Resource* right);
// Wait for copy submission on the OpenXR-bound queue. GPU dependencies remain
// ordered on that queue; allocator reuse and final readback check completion.
bool waitCopy(uint64_t serial, uint32_t timeoutMs);
// Nonblocking copy of the newest complete native pair. Output identifies the
// exact poses required for projection-layer submission, including on reuse.
bool copyLatest(ID3D12Resource* left, ID3D12Resource* right, uint64_t& serial, uint64_t& generation);
uint32_t error();
// Capture mode only: copies the newest captured pair into the pixel buffers
// that Data publishes, without stopping. Returns 0, or why nothing was copied.
DWORD freeze();
// The game's own presented frame, as its window shows it. Pause menus, hint
// cards, subtitles and world markers are drawn only into this image, never
// into the eye views. While wanted, each Present of a swapchain on the game's
// device first copies its back buffer into `texture`, on the game's queue.
struct Screen {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture; // PIXEL_SHADER_RESOURCE between copies
    DXGI_FORMAT view{};                           // read it as this
    bool linear{};                                // light values (float), not display-encoded
    uint32_t width{}, height{};
    uint64_t frame{};
};
void wantScreen(bool wanted);
// Whether the screen's copies are wanted now (wantScreen).
bool screenRequested();
// Presents of any swapchain in the process since start() (diagnostic).
uint32_t presents();
// While wanted and no screen copy is, each Present of the game's swapchain
// first draws the newest left eye over its back buffer, cut to the window's
// shape: the game's window in immersive VR while the game renders no view of
// its own. mirrored() counts those presents (diagnostic).
void mirror(bool wanted);
// At most this many presents a second reach the window while it mirrors the
// left eye (monitor_rate.hpp), and how many were held back.
void monitorRate(uint32_t rate);
uint32_t heldPresents();
uint32_t mirrored();
// The left image the headset is about to show: the one copyLatest filled this
// frame, the overlay's drawings on it, in RENDER_TARGET state. The window's
// mirror shows a copy of it while one is fresh (monitor_rate.hpp), so Spidy's
// own marks are on the monitor too. A copy is made only once the window has
// shown the last one: one eye-sized copy on the game's queue for each picture
// the window gets, none while it mirrors nothing. mirroredShown() counts the
// presents that drew such a copy (diagnostic).
void keepShownEye(ID3D12Resource* left);
uint32_t mirroredShown();
// The game's adapter's local memory as DXGI gives it to the game: its budget
// and the game's use of it, bytes. False before start() or where DXGI cannot
// say. The game's main thread only.
bool videoMemory(uint64_t& budget, uint64_t& usage);
// The newest copied frame, if one was copied within maxAgeMs.
bool latestScreen(Screen& out, uint64_t maxAgeMs);
// How the copy is read: the encoded format of the back buffer's family, or
// UNKNOWN for a format the screen does not show.
DXGI_FORMAT screenView(DXGI_FORMAT backBuffer, bool& linear);
// SpidyScreenData, for probes: status 1 once Present is hooked; the copies
// made, and the rows of the newest one SpidyGpuScreen(2) read back. Also the
// presents seen while wanted, why the last one was not copied (ScreenSkip),
// and the format of the last back buffer seen.
enum ScreenSkip : uint32_t {
    screenCopied = 0,
    screenOtherDevice = 1,
    screenNoBuffer = 2,
    screenFormat = 3,
    screenShape = 4,
    screenStopped = 5,
    screenTexture = 6,
    screenBusy = 7,
};
struct ScreenData {
    uint32_t magic = 0x53475353, version = 2, bytes = sizeof(ScreenData), status{};
    int64_t sequence{};
    uint64_t frames{}, pixels{};
    uint32_t width{}, height{}, format{}, rowBytes{};
    uint64_t presents{};
    uint32_t skip{}, backBufferFormat{};
};
static_assert(sizeof(ScreenData) == 72);
// The steadier-HUD probe (hud_capture.hpp, SpidyHudCapture). native_hud (main
// thread) registers the panel's texture as it finds it (`path`: how), and
// says whether it is wanted; while it is, a Present now and then copies the
// texture back to memory, in the state the game's executed barriers left it.
// Nothing here can stop the eye views: the probe's own failures only skip.
void registerHud(ID3D12Resource* resource, uint32_t path);
void refuseHud();
void wantHud(bool wanted);
// STEADY HUD (steady_hud.hpp). While wanted, the copy of each new eye pair
// also copies the registered panel texture, as the pair's frame left it.
// latestHud: the newest such copy's size, how many were made and when
// (GetTickCount64); false before the first. fillHudLayer: that copy into
// `target` (the layer's image: RENDER_TARGET state, RGBA8, the same size), on
// the game's queue; false when it was not. hudLayerShown counts a headset
// frame that showed the layer (SpidyHudCapture).
void wantHudLayer(bool wanted);
bool latestHud(uint32_t& width, uint32_t& height, uint64_t& copies, uint64_t& copiedMs);
bool fillHudLayer(ID3D12Resource* target);
void hudLayerShown();
DWORD start(void* input);
DWORD stop();
} // namespace spidy::native_gpu
