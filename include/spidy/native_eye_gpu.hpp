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
DWORD start(void* input);
DWORD stop();
} // namespace spidy::native_gpu
