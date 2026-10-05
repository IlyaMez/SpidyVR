#pragma once
#include <cstdint>
#include <d3d12.h>

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
DWORD start(void* input);
DWORD stop();
} // namespace spidy::native_gpu
