#include "spidy/native_eye_gpu.hpp"
#include "spidy/copy_eye_texture.hpp"
#include "spidy/eye_pair_state.hpp"
#include "spidy/hud_capture.hpp"
#include "spidy/monitor_rate.hpp"
#include "spidy/presentation_gate.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using namespace spidy;
extern "C" IMAGE_DOS_HEADER __ImageBase;
extern "C" {
__declspec(dllexport) native_gpu::Data SpidyGpuData;
__declspec(dllexport) native_gpu::ScreenData SpidyScreenData;
__declspec(dllexport) hud_capture::Data SpidyHudCapture;
}
namespace spidy::native_gpu {
namespace {
using Barrier = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
using Reset = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*,
                                          ID3D12PipelineState*);
using Execute = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using EndQuery = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE,
                                          UINT);
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
Barrier originalBarrier{};
Reset originalReset{};
Execute originalExecute{};
EndQuery originalEndQuery{};
Present originalPresent{};
Present1 originalPresent1{};
std::array<void*, 4> hooks{};
// DXGI Present and Present1; absent when no swapchain could be made to find them.
std::array<void*, 2> presentHooks{};
std::atomic<bool> enabled{};
std::atomic<uint32_t> fault{};
std::mutex lifecycle, submission, telemetry;
// Recursive: under Proton (vkd3d-proton) one function serves every command list, so the hooks below
// also see Spidy's own D3D12 calls and can re-enter on a thread that already holds this lock. A plain
// std::mutex then throws resource_deadlock_would_occur and the game closes.
std::recursive_mutex recording;
std::condition_variable copyReady;
uint64_t deadline{};
bool hooked{};
bool copyToXr{}, captureImages{}, staged{};
uint64_t requestedSerial{}, requestFence{};
std::array<ComPtr<ID3D12Resource>, 2> targets;
struct Marker {
    unsigned eye{};
    EyePairState::Stamp stamp{};
    bool ended{}, valid{};
};
thread_local Marker currentMarker{};
struct Event {
    unsigned eye{};
    bool marker{}, ended{}, hud{};
    EyePairState::Stamp stamp{};
    D3D12_RESOURCE_TRANSITION_BARRIER transition{};
    D3D12_RESOURCE_BARRIER_FLAGS flags{};
};
std::unordered_map<ID3D12CommandList*, std::vector<Event>> lists;
EyePairState pair;
uint64_t executes{}, matchedMarkers{}, pairs{}, lastGeneration{}, lastSerial{}, fenceValue{};
std::atomic<uint64_t> markers{};
uint64_t begins[2]{}, ends[2]{};
ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandQueue> queue;
std::array<ComPtr<ID3D12Resource>, 2> resources, readbacks;
std::array<std::atomic<ID3D12Resource*>, 2> watched{};
std::array<D3D12_RESOURCE_STATES, 2> states{};
std::array<bool, 2> known{};
std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> layouts{};
std::array<std::vector<uint8_t>, 2> pixels;
ComPtr<ID3D12CommandAllocator> allocator;
ComPtr<ID3D12GraphicsCommandList> command;
ComPtr<ID3D12Fence> fence;
HANDLE fenceEvent{};
UINT width{}, height{};
struct CopyCommands {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    uint64_t fence{};
};
std::array<CopyCommands, 3> captureCommands, presentCommands, screenCommands, mirrorCommands, shownCommands;
std::array<ComPtr<ID3D12Resource>, 2> stagedEyes;
EyePairState::Stamp capturedStamp;
uint64_t capturedPairs{}, reusedPairs{}, capturedMs{};
// The game's presented frame (submission lock). A texture replaced by a new
// size stays alive for a few seconds, for draws still reading it.
std::atomic<bool> screenWanted{};
bool screenWantedFlag() {
    return screenWanted.load();
}
Screen screen;
uint64_t screenMs{};
struct Retired {
    ComPtr<ID3D12Resource> texture;
    uint64_t releaseMs{};
};
std::vector<Retired> retiredScreens;
std::vector<uint8_t> screenPixels; // probe readback of the newest copy
std::atomic<uint64_t> screenPresents{};
// Every Present of any swapchain in the process since the hooks went in.
std::atomic<uint32_t> presentCalls{};
// The left eye on the game's window (mirrorEye): wanted while the game does
// not render its own view, and how many presents got it.
std::atomic<bool> mirrorWanted{};
std::atomic<uint32_t> mirrorCalls{};
// While the window shows the left eye, at most this many of the game's
// presents a second reach DXGI (monitor_rate.hpp; 0: all of them), the time
// the next one is due, and how many were held back.
std::atomic<uint32_t> mirrorRate{}, presentsHeld{};
std::atomic<int64_t> mirrorDue{};
// The left eye as the headset showed it, the overlay's drawings on it
// (keepShownEye; COPY_SOURCE between uses, submission lock), and when it was
// copied. shownWanted: the window has drawn since, so the next one is copied.
ComPtr<ID3D12Resource> shownEye;
uint64_t shownMs{};
bool shownFailed{};
std::atomic<bool> shownWanted{true};
std::atomic<uint32_t> shownMirrors{};
struct MirrorPipeline {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> state;
    ComPtr<ID3D12DescriptorHeap> source, target;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool failed{};
} mirrorPipeline;
std::atomic<uint32_t> screenSkipped{}, backBufferFormat{};
// The steadier-HUD probe (hud_capture.hpp; recording lock, the copies also
// under the submission lock). The panel's texture, a replaced one kept a few
// seconds for a copy still reading it, the state the game's executed barriers
// left it in, and the newest snapshot's readback and rows.
struct HudProbe {
    ComPtr<ID3D12Resource> resource, readback;
    std::vector<Retired> retired;
    uint32_t path{}, mips{};
    D3D12_RESOURCE_STATES state{};
    bool known{};
    uint64_t barriers{}, executed{}, otherQueue{}, odd{}, mismatched{}, presents{}, copies{}, snapshots{};
    uint32_t skip{}, refused{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    uint32_t shotWidth{}, shotHeight{}, shotBytes{};
    bool shotSwap{}, pending{}, taken{};
    uint64_t pendingFence{}, lastMs{};
    std::vector<uint8_t> pixels;
    uint32_t rowPitch{}, swap{};
    // STEADY HUD: the panel as the newest eye pair's frame left it
    // (COPY_SOURCE between uses), when it was copied, the copies into the
    // layer's image and the headset frames that showed the layer.
    ComPtr<ID3D12Resource> staged;
    uint64_t stagedCopies{}, stagedMs{}, layerCopies{}, layerFrames{};
    uint32_t stageSkip{};
} hud;
std::atomic<ID3D12Resource*> hudWatched{};
std::atomic<bool> hudWanted{}, hudLayerWanted{};
std::array<CopyCommands, 3> hudCommands, hudLayerCommands;
void publishHud() { // recording held by caller
    std::lock_guard lock(telemetry);
    auto& d = SpidyHudCapture;
    InterlockedIncrement64(&d.sequence);
    d.path = hud.path;
    d.resource = reinterpret_cast<uint64_t>(hud.resource.Get());
    if (hud.resource) {
        const auto desc = hud.resource->GetDesc();
        d.width = static_cast<uint32_t>(desc.Width);
        d.height = desc.Height;
        d.format = desc.Format;
        d.mips = desc.MipLevels;
        d.flags = desc.Flags;
        d.samples = desc.SampleDesc.Count;
    }
    d.barriers = hud.barriers;
    d.executed = hud.executed;
    d.otherQueue = hud.otherQueue;
    d.odd = hud.odd;
    d.mismatched = hud.mismatched;
    d.state = hud.state;
    d.known = hud.known;
    d.presents = hud.presents;
    d.copies = hud.copies;
    d.skip = hud.skip;
    d.refused = hud.refused;
    d.snapshots = hud.snapshots;
    d.pixels = hud.pixels.empty() ? 0 : reinterpret_cast<uint64_t>(hud.pixels.data());
    d.rowPitch = hud.rowPitch;
    d.swap = hud.swap;
    d.staged = hud.stagedCopies;
    d.layerCopies = hud.layerCopies;
    d.layerFrames = hud.layerFrames;
    d.stageSkip = hud.stageSkip;
    InterlockedIncrement64(&d.sequence);
}
void publishScreen(uint32_t status, const void* rows, uint32_t rowBytes) {
    std::lock_guard lock(telemetry);
    auto& d = SpidyScreenData;
    InterlockedIncrement64(&d.sequence);
    if (status)
        d.status = status;
    d.frames = screen.frame;
    d.width = screen.width;
    d.height = screen.height;
    d.format = screen.view;
    if (rows) {
        d.pixels = reinterpret_cast<uint64_t>(rows);
        d.rowBytes = rowBytes;
    }
    d.presents = screenPresents;
    d.skip = screenSkipped;
    d.backBufferFormat = backBufferFormat;
    InterlockedIncrement64(&d.sequence);
}
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
void publish() { // submission and recording held by caller
    std::lock_guard lock(telemetry);
    auto& d = SpidyGpuData;
    InterlockedIncrement64(&d.sequence);
    d.error = fault.load();
    d.executes = executes;
    d.markers = markers;
    d.matchedMarkers = matchedMarkers;
    d.pairs = pairs;
    d.serial = lastSerial;
    d.generation = lastGeneration;
    d.fence = fenceValue;
    d.completed = fence ? fence->GetCompletedValue() : 0;
    d.width = width;
    d.height = height;
    d.captured = capturedPairs;
    d.reused = reusedPairs;
    d.capturedSerial = capturedStamp.serial;
    for (unsigned i = 0; i < 2; ++i) {
        d.begins[i] = begins[i];
        d.ends[i] = ends[i];
        d.resources[i] = reinterpret_cast<uint64_t>(resources[i].Get());
        d.states[i] = states[i];
        d.known[i] = known[i];
        d.pixels[i] = pixels[i].empty() ? 0 : reinterpret_cast<uint64_t>(pixels[i].data());
    }
    InterlockedIncrement64(&d.sequence);
}
void add(ID3D12CommandList* list, const Event& event) {
    auto found = lists.find(list);
    if (found == lists.end()) {
        if (lists.size() >= 4096) {
            fault = 3101;
            return;
        }
        found = lists.emplace(list, std::vector<Event>{}).first;
    }
    if (found->second.size() >= 4096) {
        fault = 3102;
        return;
    }
    found->second.push_back(event);
}
void STDMETHODCALLTYPE barrier(ID3D12GraphicsCommandList* list, UINT count,
                               const D3D12_RESOURCE_BARRIER* bs) {
    if (enabled) {
        const auto* left = watched[0].load();
        const auto* right = watched[1].load();
        const auto* panel = hudWatched.load();
        bool relevant{};
        for (UINT n = 0; n < count; ++n)
            relevant |= bs[n].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
                        (bs[n].Transition.pResource == left || bs[n].Transition.pResource == right ||
                         (panel && bs[n].Transition.pResource == panel));
        if (!relevant) {
            originalBarrier(list, count, bs);
            return;
        }
        std::lock_guard lock(recording);
        for (UINT n = 0; n < count; ++n)
            if (bs[n].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
                for (unsigned i = 0; i < 2; ++i)
                    if (resources[i] && bs[n].Transition.pResource == resources[i].Get()) {
                        Event e{};
                        e.eye = i;
                        e.transition = bs[n].Transition;
                        e.flags = bs[n].Flags;
                        add(list, e);
                    }
                // The HUD probe's: a full list is skipped, never a fault.
                if (hud.resource && bs[n].Transition.pResource == hud.resource.Get()) {
                    ++hud.barriers;
                    auto found = lists.find(list);
                    if (found == lists.end() && lists.size() < 4096)
                        found = lists.emplace(list, std::vector<Event>{}).first;
                    if (found != lists.end() && found->second.size() < 4096) {
                        Event e{};
                        e.hud = true;
                        e.transition = bs[n].Transition;
                        e.flags = bs[n].Flags;
                        found->second.push_back(e);
                    }
                }
            }
    }
    originalBarrier(list, count, bs);
}
HRESULT STDMETHODCALLTYPE reset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* a,
                                ID3D12PipelineState* p) {
    auto result = originalReset(list, a, p);
    if (enabled && SUCCEEDED(result)) {
        std::lock_guard lock(recording);
        lists.erase(list);
    }
    return result;
}
void STDMETHODCALLTYPE endQuery(ID3D12GraphicsCommandList* list, ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type,
                                UINT index) {
    originalEndQuery(list, heap, type, index);
    if (enabled) {
        markers.fetch_add(1, std::memory_order_relaxed);
        if (!currentMarker.valid || type != D3D12_QUERY_TYPE_TIMESTAMP)
            return;
        std::lock_guard lock(recording);
        if (currentMarker.valid && type == D3D12_QUERY_TYPE_TIMESTAMP) {
            ++matchedMarkers;
            Event e{};
            e.eye = currentMarker.eye;
            e.marker = true;
            e.ended = currentMarker.ended;
            e.stamp = currentMarker.stamp;
            add(list, e);
        }
    }
}
bool makeReadbacks() {
    if (width && (!captureImages || (readbacks[0] && readbacks[1])))
        return true;
    if (!resources[0] || !resources[1])
        return false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto desc = resources[i]->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.MipLevels != 1 || desc.DepthOrArraySize != 1 ||
            desc.SampleDesc.Count != 1 || !validEyeSize(desc.Width) || !validEyeSize(desc.Height) ||
            (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)) {
            fault = 3201;
            return false;
        }
        if (i && (width != desc.Width || height != desc.Height)) {
            fault = 3202;
            return false;
        }
        width = static_cast<UINT>(desc.Width);
        height = desc.Height;
        if (!captureImages)
            continue;
        UINT64 bytes{};
        device->GetCopyableFootprints(&desc, 0, 1, 0, &layouts[i], nullptr, nullptr, &bytes);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = bytes;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&readbacks[i])))) {
            fault = 3203;
            return false;
        }
        pixels[i].resize(static_cast<size_t>(width) * height * 4);
    }
    return true;
}
CopyCommands* readyCommands(std::array<CopyCommands, 3>& pool) {
    const auto completed = fence->GetCompletedValue();
    if (completed == UINT64_MAX) {
        fault = 3301;
        return nullptr;
    }
    for (auto& slot : pool) {
        if (slot.fence > completed)
            continue;
        if (!slot.allocator) {
            if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      IID_PPV_ARGS(&slot.allocator))) ||
                FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(),
                                                 nullptr, IID_PPV_ARGS(&slot.list))) ||
                FAILED(slot.list->Close())) {
                fault = 3801;
                return nullptr;
            }
        }
        if (FAILED(slot.allocator->Reset()) ||
            FAILED(originalReset(slot.list.Get(), slot.allocator.Get(), nullptr))) {
            fault = 3802;
            return nullptr;
        }
        return &slot;
    }
    return nullptr;
}
bool submitCommands(CopyCommands& slot) {
    if (FAILED(slot.list->Close())) {
        fault = 3803;
        return false;
    }
    ID3D12CommandList* list = slot.list.Get();
    originalExecute(queue.Get(), 1, &list);
    slot.fence = ++fenceValue;
    if (FAILED(queue->Signal(fence.Get(), slot.fence))) {
        fault = 3804;
        return false;
    }
    return true;
}
// STEADY HUD: the panel's texture as the pair's frame left it, copied after
// the pair on the same list. Its own failures only skip it.
hud_capture::Skip stageHud(ID3D12GraphicsCommandList* list) { // submission and recording held by caller
    if (!hud.resource)
        return hud_capture::skipNoResource;
    if (!hud.known)
        return hud_capture::skipUnknownState;
    const auto desc = hud.resource->GetDesc();
    if (!rgba8CopyTarget(desc))
        return hud_capture::skipFormat;
    if (hud.staged) {
        const auto own = hud.staged->GetDesc();
        if (own.Width != desc.Width || own.Height != desc.Height || own.Format != desc.Format) {
            hud.retired.push_back({hud.staged, GetTickCount64() + 3000});
            hud.staged.Reset();
        }
    }
    if (!hud.staged) {
        auto copy = desc;
        copy.Flags = D3D12_RESOURCE_FLAG_NONE;
        copy.Alignment = 0;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &copy,
                                                   D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                                   IID_PPV_ARGS(&hud.staged))))
            return hud_capture::skipFailed;
    }
    copyEyeTexture(list, hud.resource.Get(), hud.state, hud.staged.Get(), originalBarrier,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
    return hud_capture::skipNone;
}
void stagePair(EyePairState::Stamp stamp) {
    if (!known[0] || !known[1] || !makeReadbacks())
        return;
    for (unsigned i = 0; i < 2; ++i)
        if (!stagedEyes[i]) {
            auto desc = resources[i]->GetDesc();
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                       D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                                       IID_PPV_ARGS(&stagedEyes[i])))) {
                fault = 3805;
                return;
            }
        }
    auto* slot = readyCommands(captureCommands);
    if (!slot)
        return;
    for (unsigned i = 0; i < 2; ++i)
        copyEyeTexture(slot->list.Get(), resources[i].Get(), states[i], stagedEyes[i].Get(), originalBarrier,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
    const auto hudSkip = hudLayerWanted ? stageHud(slot->list.Get()) : hud_capture::skipNoResource;
    if (!submitCommands(*slot))
        return;
    if (hudLayerWanted) {
        hud.stageSkip = hudSkip;
        if (hudSkip == hud_capture::skipNone) {
            ++hud.stagedCopies;
            hud.stagedMs = GetTickCount64();
        }
        publishHud();
    }
    capturedStamp = stamp;
    capturedMs = GetTickCount64();
    ++capturedPairs;
}
void captureImage(ID3D12GraphicsCommandList* list, ID3D12Resource* image, unsigned eye) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {image, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_COPY_SOURCE};
    originalBarrier(list, 1, &b);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = image;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readbacks[eye].Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layouts[eye];
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    originalBarrier(list, 1, &b);
}
void capturePair(EyePairState::Stamp stamp) {
    if (!known[0] || !known[1] || !makeReadbacks())
        return;
    if (copyToXr)
        for (unsigned i = 0; i < 2; ++i) {
            if (!targets[i]) {
                fault = 3305;
                return;
            }
            const auto destination = targets[i]->GetDesc();
            if (destination.Width != width || destination.Height != height) {
                fault = 3306;
                return;
            }
        }
    const auto completed = fence->GetCompletedValue();
    if (completed == UINT64_MAX) {
        fault = 3301;
        return;
    }
    if (completed < fenceValue)
        return; // previous allocator and readbacks still in use
    if (FAILED(allocator->Reset()) || FAILED(originalReset(command.Get(), allocator.Get(), nullptr))) {
        fault = 3302;
        return;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (copyToXr) {
            copyEyeTexture(command.Get(), resources[i].Get(), states[i], targets[i].Get(), originalBarrier);
        }
        if (!captureImages)
            continue;
        // Read the acquired XR image after its copy, so diagnostics show the
        // actual submitted texture rather than another native render generation.
        auto* captured = copyToXr ? targets[i].Get() : resources[i].Get();
        const auto capturedState = copyToXr ? D3D12_RESOURCE_STATE_RENDER_TARGET : states[i];
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {captured, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, capturedState,
                        D3D12_RESOURCE_STATE_COPY_SOURCE};
        if (capturedState != D3D12_RESOURCE_STATE_COPY_SOURCE)
            originalBarrier(command.Get(), 1, &b);
        {
            D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
            src.pResource = captured;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.pResource = readbacks[i].Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = layouts[i];
            command->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        if (capturedState != D3D12_RESOURCE_STATE_COPY_SOURCE)
            originalBarrier(command.Get(), 1, &b);
    }
    if (FAILED(command->Close())) {
        fault = 3303;
        return;
    }
    ID3D12CommandList* list = command.Get();
    originalExecute(queue.Get(), 1, &list);
    if (FAILED(queue->Signal(fence.Get(), ++fenceValue))) {
        fault = 3304;
        return;
    }
    ++pairs;
    lastSerial = stamp.serial;
    lastGeneration = stamp.generation;
    if (copyToXr)
        requestFence = fenceValue;
    copyReady.notify_all();
}
void STDMETHODCALLTYPE execute(ID3D12CommandQueue* q, UINT count, ID3D12CommandList* const* commands) {
    if (!enabled) {
        originalExecute(q, count, commands);
        return;
    }
    // Runtime/overlay queues must not serialize behind the game submission lock.
    if (q != queue.Get()) {
        originalExecute(q, count, commands);
        std::lock_guard recordLock(recording);
        for (UINT n = 0; n < count; ++n)
            if (auto found = lists.find(commands[n]); found != lists.end() && !found->second.empty()) {
                // The HUD probe's barriers elsewhere leave its state unknown.
                const auto own = std::count_if(found->second.begin(), found->second.end(),
                                               [](const Event& e) { return e.hud; });
                if (own) {
                    hud.otherQueue += own;
                    hud.known = false;
                }
                if (static_cast<size_t>(own) < found->second.size()) {
                    fault = 3401;
                    copyReady.notify_all();
                }
            }
        return;
    }
    std::lock_guard submitLock(submission);
    originalExecute(q, count, commands);
    std::lock_guard recordLock(recording);
    ++executes;
    bool relevant{};
    for (UINT n = 0; n < count; ++n)
        if (auto found = lists.find(commands[n]); found != lists.end())
            for (const auto& e : found->second) {
                if (e.hud) {
                    ++hud.executed;
                    if (e.flags != D3D12_RESOURCE_BARRIER_FLAG_NONE ||
                        (e.transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
                         (e.transition.Subresource != 0 || hud.mips != 1))) {
                        ++hud.odd;
                        hud.known = false;
                        continue;
                    }
                    hud.mismatched += hud.known && hud.state != e.transition.StateBefore;
                    hud.state = e.transition.StateAfter;
                    hud.known = true;
                    continue;
                }
                relevant = true;
                if (q != queue.Get()) {
                    fault = 3401;
                    continue;
                }
                if (e.marker) {
                    if (e.ended) {
                        pair.end(e.eye, e.stamp);
                        ++ends[e.eye];
                    } else {
                        pair.begin(e.eye, e.stamp);
                        ++begins[e.eye];
                    }
                } else {
                    if (e.flags != D3D12_RESOURCE_BARRIER_FLAG_NONE ||
                        (e.transition.Subresource != 0 &&
                         e.transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)) {
                        fault = 3402;
                        known[e.eye] = false;
                        continue;
                    }
                    if (known[e.eye] && states[e.eye] != e.transition.StateBefore) {
                        fault = 3403;
                        known[e.eye] = false;
                        continue;
                    }
                    states[e.eye] = e.transition.StateAfter;
                    known[e.eye] = true;
                }
            }
    if (!relevant)
        return;
    const auto ready = pair.ready();
    if (staged) {
        if (enabled && !fault && GetTickCount64() < deadline && ready.serial &&
            ready.generation > capturedStamp.generation)
            stagePair(ready);
    } else if (enabled && !fault && GetTickCount64() < deadline && ready.serial &&
               ready.generation > lastGeneration &&
               (!copyToXr || (requestedSerial == ready.serial && !requestFence)))
        capturePair(ready);
    publish();
    if (fault)
        copyReady.notify_all();
}
DXGI_FORMAT typelessFamily(DXGI_FORMAT view) {
    switch (view) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    default:
        return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    }
}
// Present thread: copies the back buffer about to be shown on the game's
// render queue, after the frame it rendered there and before the queue renders
// into that buffer again. The swapchain reports the queue it was created with,
// which with Streamline is a wrapper of that queue, so the game's swapchain is
// recognized by its back buffer's device.
ScreenSkip copyScreen(IDXGISwapChain* swapchain) {
    ComPtr<IDXGISwapChain3> chain;
    ComPtr<ID3D12Resource> buffer;
    if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&chain))) ||
        FAILED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer))))
        return screenNoBuffer;
    ComPtr<ID3D12Device> owner;
    ComPtr<IUnknown> ownerIdentity, gameIdentity;
    if (FAILED(buffer->GetDevice(IID_PPV_ARGS(&owner))) || FAILED(owner.As(&ownerIdentity)) ||
        FAILED(device.As(&gameIdentity)) || ownerIdentity.Get() != gameIdentity.Get())
        return screenOtherDevice;
    const auto desc = buffer->GetDesc();
    backBufferFormat = desc.Format;
    bool linear{};
    const auto view = screenView(desc.Format, linear);
    if (view == DXGI_FORMAT_UNKNOWN)
        return screenFormat;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
        desc.DepthOrArraySize != 1 || desc.MipLevels != 1 || !validEyeSize(desc.Width) || !validEyeSize(desc.Height))
        return screenShape;
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (!enabled || fault || GetTickCount64() >= deadline)
        return screenStopped;
    const auto now = GetTickCount64();
    std::erase_if(retiredScreens, [&](const Retired& old) { return now >= old.releaseMs; });
    if (!screen.texture || screen.width != desc.Width || screen.height != desc.Height || screen.view != view) {
        if (screen.texture)
            retiredScreens.push_back({screen.texture, now + 3000});
        screen = {};
        auto copy = desc;
        copy.Format = typelessFamily(view);
        copy.Flags = D3D12_RESOURCE_FLAG_NONE;
        copy.Alignment = 0;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &copy,
                                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                   IID_PPV_ARGS(&screen.texture))))
            return screenTexture;
        screen.view = view;
        screen.linear = linear;
        screen.width = static_cast<uint32_t>(desc.Width);
        screen.height = desc.Height;
    }
    auto* slot = readyCommands(screenCommands);
    if (!slot)
        return screenBusy; // earlier copies still in flight: skip this frame
    D3D12_RESOURCE_BARRIER b[2]{};
    b[0].Type = b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition = {buffer.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_COPY_SOURCE};
    b[1].Transition = {screen.texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST};
    originalBarrier(slot->list.Get(), 2, b);
    slot->list->CopyResource(screen.texture.Get(), buffer.Get());
    for (auto& barrier : b)
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    originalBarrier(slot->list.Get(), 2, b);
    if (!submitCommands(*slot))
        return screenStopped;
    ++screen.frame;
    screenMs = now;
    return screenCopied;
}
// The pipeline that draws a part of one texture over a whole target of
// `format`: a screen-covering triangle and one linear sample (submission lock).
bool prepareMirror(DXGI_FORMAT format) {
    auto& m = mirrorPipeline;
    if (m.failed)
        return false;
    if (m.state && m.format == format)
        return true;
    m.failed = true; // cleared at the end: a failure is not tried again every frame
    if (!m.root) {
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range};
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants.Num32BitValues = 8;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs{2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> signature, error;
        D3D12_DESCRIPTOR_HEAP_DESC views{};
        views.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        views.NumDescriptors = 1;
        views.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        D3D12_DESCRIPTOR_HEAP_DESC targetsHeap{};
        targetsHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        targetsHeap.NumDescriptors = 1;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)) ||
            FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                               IID_PPV_ARGS(&m.root))) ||
            FAILED(device->CreateDescriptorHeap(&views, IID_PPV_ARGS(&m.source))) ||
            FAILED(device->CreateDescriptorHeap(&targetsHeap, IID_PPV_ARGS(&m.target)))) {
            m.root.Reset();
            return false;
        }
    }
    // Both images hold display-encoded values and neither view is sRGB, so
    // what is sampled is what is stored.
    const char* shader = R"(
Texture2D source : register(t0);
SamplerState scaled : register(s0);
cbuffer Part : register(b0) { float2 targetPixel; float2 offset; float2 scale; float2 unused; };
float4 vs(uint id : SV_VertexID) : SV_POSITION {
    const float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 ps(float4 position : SV_POSITION) : SV_TARGET {
    return float4(source.SampleLevel(scaled, offset + position.xy * targetPixel * scale, 0).rgb, 1);
}
)";
    ComPtr<ID3DBlob> vs, ps, error;
    if (FAILED(D3DCompile(shader, std::strlen(shader), "spidy_mirror", nullptr, nullptr, "vs", "vs_5_0",
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, &error)) ||
        FAILED(D3DCompile(shader, std::strlen(shader), "spidy_mirror", nullptr, nullptr, "ps", "ps_5_0",
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, &error)))
        return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = m.root.Get();
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = format;
    d.SampleDesc.Count = 1;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    auto& blend = d.BlendState.RenderTarget[0];
    blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    m.state.Reset();
    if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m.state))))
        return false;
    m.format = format;
    m.failed = false;
    return true;
}
// Present thread: the newest left eye over the back buffer about to be shown,
// on the game's queue. While the game does not render its own view (the stereo
// module's views option 32), nothing else draws there, and the window would
// show whichever old frames its buffers still hold. The eye fills the window's
// width or height and is cut to its shape about the middle; it is not
// stretched. A float (HDR) back buffer is left alone. The eye is the one the
// headset showed, Spidy's drawings on it, while a copy of that is fresh
// (keepShownEye), and the game's own image of it otherwise.
void mirrorEye(IDXGISwapChain* swapchain) {
    ComPtr<IDXGISwapChain3> chain;
    ComPtr<ID3D12Resource> buffer;
    if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&chain))) ||
        FAILED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer))))
        return;
    ComPtr<ID3D12Device> owner;
    ComPtr<IUnknown> ownerIdentity, gameIdentity;
    if (FAILED(buffer->GetDevice(IID_PPV_ARGS(&owner))) || FAILED(owner.As(&ownerIdentity)) ||
        FAILED(device.As(&gameIdentity)) || ownerIdentity.Get() != gameIdentity.Get())
        return;
    const auto desc = buffer->GetDesc();
    bool linear{};
    const auto view = screenView(desc.Format, linear);
    if (view == DXGI_FORMAT_UNKNOWN || linear || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.SampleDesc.Count != 1 || !desc.Width || !desc.Height)
        return;
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    const auto now = GetTickCount64();
    if (!enabled || fault || now >= deadline || !staged || !stagedEyes[0] || !capturedStamp.serial ||
        now - capturedMs > imageHoldMs || !width || !height || !prepareMirror(view))
        return;
    auto* slot = readyCommands(mirrorCommands);
    if (!slot)
        return; // earlier draws still in flight: this present keeps what the buffer holds
    const bool shown = shownEye && shownEyeFresh(shownMs, now, mirrorRate.load());
    auto* eye = shown ? shownEye.Get() : stagedEyes[0].Get();
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(eye, &sv, mirrorPipeline.source->GetCPUDescriptorHandleForHeapStart());
    D3D12_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = view;
    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto rtv = mirrorPipeline.target->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(buffer.Get(), &rv, rtv);
    D3D12_RESOURCE_BARRIER b[2]{};
    b[0].Type = b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition = {buffer.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_RENDER_TARGET};
    b[1].Transition = {eye, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    auto* list = slot->list.Get();
    originalBarrier(list, 2, b);
    // The part of the eye with the window's shape, about its middle.
    const float window = static_cast<float>(desc.Width) / static_cast<float>(desc.Height);
    const float image = static_cast<float>(width) / static_cast<float>(height);
    const float across = window < image ? window / image : 1, down = window < image ? 1 : image / window;
    const float part[8] = {1.f / static_cast<float>(desc.Width), 1.f / static_cast<float>(desc.Height),
                           (1 - across) / 2, (1 - down) / 2, across, down, 0, 0};
    list->SetPipelineState(mirrorPipeline.state.Get());
    list->SetGraphicsRootSignature(mirrorPipeline.root.Get());
    ID3D12DescriptorHeap* heaps[] = {mirrorPipeline.source.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(0, mirrorPipeline.source->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRoot32BitConstants(1, 8, part, 0);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    const D3D12_RECT rect{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &rect);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    for (auto& barrier : b)
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    originalBarrier(list, 2, b);
    if (submitCommands(*slot)) {
        ++mirrorCalls;
        shownMirrors += shown;
    }
    shownWanted = true;
}
// The HUD probe at a Present on the game's queue: the frame's work, the
// panel's drawing with it, is queued before; the next frame's after. Reads
// a finished snapshot into memory, and when one is due copies the panel in
// the state the executed barriers left it, then puts that state back.
// Its failures skip; they never fault the eye views.
hud_capture::Skip copyHud() { // submission and recording held by caller
    using namespace hud_capture;
    if (!hud.resource)
        return skipNoResource;
    if (!hud.known)
        return skipUnknownState;
    const auto desc = hud.resource->GetDesc();
    bool swap{};
    const auto bytes = readableBytes(desc.Format, swap);
    if (!bytes || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
        desc.DepthOrArraySize != 1)
        return skipFormat;
    if (hud.pending)
        return skipBusy;
    if (!hud.readback || hud.shotWidth != desc.Width || hud.shotHeight != desc.Height) {
        hud.readback.Reset();
        UINT64 size{};
        device->GetCopyableFootprints(&desc, 0, 1, 0, &hud.layout, nullptr, nullptr, &size);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = size;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&hud.readback))))
            return skipFailed;
        hud.shotWidth = static_cast<uint32_t>(desc.Width);
        hud.shotHeight = desc.Height;
    }
    auto* slot = readyCommands(hudCommands);
    if (!slot)
        return skipBusy;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {hud.resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, hud.state,
                    D3D12_RESOURCE_STATE_COPY_SOURCE};
    const bool turn = hud.state != D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (turn)
        originalBarrier(slot->list.Get(), 1, &b);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = hud.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = hud.readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = hud.layout;
    slot->list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    if (turn)
        originalBarrier(slot->list.Get(), 1, &b);
    if (FAILED(slot->list->Close()))
        return skipFailed;
    ID3D12CommandList* list = slot->list.Get();
    originalExecute(queue.Get(), 1, &list);
    slot->fence = ++fenceValue;
    if (FAILED(queue->Signal(fence.Get(), slot->fence)))
        return skipFailed;
    hud.pending = true;
    hud.pendingFence = slot->fence;
    hud.shotBytes = bytes;
    hud.shotSwap = swap;
    ++hud.copies;
    return skipNone;
}
void readHud() { // submission and recording held by caller
    if (!hud.pending || fence->GetCompletedValue() < hud.pendingFence)
        return;
    hud.pending = false;
    const auto rowBytes = hud.shotWidth * hud.shotBytes;
    void* data{};
    D3D12_RANGE range{0, static_cast<SIZE_T>(hud.layout.Offset +
                                             static_cast<UINT64>(hud.layout.Footprint.RowPitch) *
                                                 (hud.shotHeight - 1) +
                                             rowBytes)};
    if (FAILED(hud.readback->Map(0, &range, &data))) {
        hud.skip = hud_capture::skipFailed;
        return;
    }
    hud.pixels.resize(static_cast<size_t>(rowBytes) * hud.shotHeight);
    for (uint32_t y = 0; y < hud.shotHeight; ++y)
        std::memcpy(hud.pixels.data() + static_cast<size_t>(y) * rowBytes,
                    static_cast<uint8_t*>(data) + hud.layout.Offset +
                        static_cast<size_t>(y) * hud.layout.Footprint.RowPitch,
                    rowBytes);
    D3D12_RANGE written{0, 0};
    hud.readback->Unmap(0, &written);
    hud.rowPitch = rowBytes;
    hud.swap = hud.shotSwap;
    ++hud.snapshots;
}
void captureHud(IDXGISwapChain* swapchain) {
    ComPtr<IDXGISwapChain3> chain;
    ComPtr<ID3D12Resource> buffer;
    ComPtr<ID3D12Device> owner;
    ComPtr<IUnknown> ownerIdentity, gameIdentity;
    // Only the game's own swapchain: its Present follows the frame on the game's queue.
    if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&chain))) ||
        FAILED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer))) ||
        FAILED(buffer->GetDevice(IID_PPV_ARGS(&owner))) || FAILED(owner.As(&ownerIdentity)) ||
        FAILED(device.As(&gameIdentity)) || ownerIdentity.Get() != gameIdentity.Get())
        return;
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (!enabled || fault || GetTickCount64() >= deadline)
        return;
    ++hud.presents;
    readHud();
    const auto now = GetTickCount64();
    if (hud_capture::snapshotDue(now, hud.lastMs, hud.taken)) {
        hud.skip = copyHud();
        if (hud.skip == hud_capture::skipNone) {
            hud.lastMs = now;
            hud.taken = true;
        }
    }
    std::erase_if(hud.retired, [&](const Retired& old) { return now >= old.releaseMs; });
    publishHud();
}
void captureScreen(IDXGISwapChain* swapchain) {
    ++screenPresents;
    screenSkipped = copyScreen(swapchain);
    publishScreen(0, nullptr, 0);
}
// Present reaches Present1 in some DXGI builds: one of them handles a frame.
thread_local bool presenting{};
void presentFrame(IDXGISwapChain* swapchain, UINT flags) {
    if (!enabled || (flags & DXGI_PRESENT_TEST))
        return;
    if (hudWanted)
        captureHud(swapchain);
    if (screenWanted)
        captureScreen(swapchain);
    else if (mirrorWanted)
        mirrorEye(swapchain);
}
// The game presents once a frame whether or not it rendered a view of its
// own. While it does not, the window only mirrors the left eye, and a present
// costs GPU time the eyes could have: hold back those beyond the monitor's rate.
// The game goes on without them (it paces itself by its own fences), and a
// held present draws no mirror either.
bool holdPresent(UINT flags) {
    const auto rate = mirrorRate.load();
    if (!rate || !enabled || !mirrorWanted || screenWanted || (flags & DXGI_PRESENT_TEST))
        return false;
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    auto due = mirrorDue.load();
    if (spidy::monitorPresentDue(now.QuadPart, due, frequency.QuadPart, rate)) {
        mirrorDue = due;
        return false;
    }
    ++presentsHeld;
    return true;
}
HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* swapchain, UINT interval, UINT flags) {
    ++presentCalls;
    const bool outer = !presenting;
    if (outer && holdPresent(flags))
        return S_OK;
    if (outer) {
        presenting = true;
        presentFrame(swapchain, flags);
    }
    const auto result = originalPresent(swapchain, interval, flags);
    if (outer)
        presenting = false;
    return result;
}
HRESULT STDMETHODCALLTYPE present1(IDXGISwapChain1* swapchain, UINT interval, UINT flags,
                                   const DXGI_PRESENT_PARAMETERS* parameters) {
    ++presentCalls;
    const bool outer = !presenting;
    if (outer && holdPresent(flags))
        return S_OK;
    if (outer) {
        presenting = true;
        presentFrame(swapchain, flags);
    }
    const auto result = originalPresent1(swapchain, interval, flags, parameters);
    if (outer)
        presenting = false;
    return result;
}
// Every swapchain in the process shares DXGI's Present methods. A throwaway
// 64-pixel swapchain on a hidden window, on a queue of its own, gives their
// addresses; the hooks act only on swapchains presenting on the game's queue.
bool presentEntries(void*& presentAddress, void*& present1Address) {
    using CreateFactory = HRESULT(WINAPI*)(UINT, REFIID, void**);
    const auto dxgi = GetModuleHandleW(L"dxgi.dll");
    const auto create = dxgi ? reinterpret_cast<CreateFactory>(GetProcAddress(dxgi, "CreateDXGIFactory2")) : nullptr;
    if (!create)
        return false;
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = DefWindowProcW;
    type.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
    type.lpszClassName = L"SpidyPresentEntries";
    RegisterClassExW(&type);
    const auto window = CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                                        nullptr, type.hInstance, nullptr);
    if (!window)
        return false;
    bool found{};
    {
        ComPtr<IDXGIFactory2> factory;
        ComPtr<ID3D12CommandQueue> own;
        ComPtr<IDXGISwapChain1> chain;
        D3D12_COMMAND_QUEUE_DESC q{};
        q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = d.Height = 64;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (SUCCEEDED(create(0, IID_PPV_ARGS(&factory))) && SUCCEEDED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&own))) &&
            SUCCEEDED(factory->CreateSwapChainForHwnd(own.Get(), window, &d, nullptr, nullptr, &chain))) {
            const auto table = *reinterpret_cast<void***>(chain.Get());
            presentAddress = table[8];   // IDXGISwapChain::Present
            present1Address = table[22]; // IDXGISwapChain1::Present1
            found = true;
        }
    }
    DestroyWindow(window);
    return found;
}
} // namespace
DXGI_FORMAT screenView(DXGI_FORMAT backBuffer, bool& linear) {
    linear = false;
    switch (backBuffer) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        linear = true;
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}
uint32_t presents() {
    return presentCalls.load();
}
void mirror(bool wanted) {
    mirrorWanted = wanted;
}
void monitorRate(uint32_t rate) {
    mirrorRate = rate;
}
uint32_t heldPresents() {
    return presentsHeld.load();
}
uint32_t mirrored() {
    return mirrorCalls.load();
}
uint32_t mirroredShown() {
    return shownMirrors.load();
}
bool videoMemory(uint64_t& budget, uint64_t& usage) {
    // The adapter behind the game's device, found once (the main thread asks).
    static ComPtr<IDXGIAdapter3> adapter;
    if (!adapter) {
        using CreateFactory = HRESULT(WINAPI*)(UINT, REFIID, void**);
        const auto dxgi = GetModuleHandleW(L"dxgi.dll");
        const auto create =
            dxgi ? reinterpret_cast<CreateFactory>(GetProcAddress(dxgi, "CreateDXGIFactory2")) : nullptr;
        ComPtr<IDXGIFactory4> factory;
        if (!create || !device || FAILED(create(0, IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))))
            return false;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        return false;
    budget = info.Budget;
    usage = info.CurrentUsage;
    return true;
}
void wantScreen(bool wanted) {
    screenWanted = wanted;
}
bool screenRequested() {
    return screenWantedFlag();
}
bool latestScreen(Screen& out, uint64_t maxAgeMs) {
    std::lock_guard submitLock(submission);
    const auto now = GetTickCount64();
    if (!screen.texture || !screen.frame || now < screenMs || now - screenMs > maxAgeMs)
        return false;
    out = screen;
    return true;
}
// Probes only: reads the newest copy back to memory, waiting for the GPU.
DWORD readScreen() {
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (!enabled || fault || !screen.texture || !screen.frame)
        return 3951;
    const auto desc = screen.texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT64 bytes{};
    device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = bytes;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&readback))))
        return 3952;
    auto* slot = readyCommands(screenCommands);
    if (!slot)
        return 3953;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {screen.texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE};
    originalBarrier(slot->list.Get(), 1, &b);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = screen.texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layout;
    slot->list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    originalBarrier(slot->list.Get(), 1, &b);
    if (!submitCommands(*slot))
        return 3954;
    if (fence->GetCompletedValue() < fenceValue &&
        (FAILED(fence->SetEventOnCompletion(fenceValue, fenceEvent)) ||
         WaitForSingleObject(fenceEvent, 2000) != WAIT_OBJECT_0))
        return 3955;
    void* data{};
    if (FAILED(readback->Map(0, nullptr, &data)))
        return 3956;
    const auto rowBytes = static_cast<uint32_t>(desc.Width * (screen.view == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4));
    screenPixels.resize(static_cast<size_t>(rowBytes) * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(screenPixels.data() + static_cast<size_t>(y) * rowBytes,
                    static_cast<uint8_t*>(data) + layout.Offset + static_cast<size_t>(y) * layout.Footprint.RowPitch,
                    rowBytes);
    D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
    publishScreen(1, screenPixels.data(), rowBytes);
    return 0;
}
void enterMarker(unsigned eye, uint64_t serial, uint64_t generation, bool ended) {
    currentMarker = {eye, {serial, generation}, ended, eye < 2};
}
void leaveMarker() {
    currentMarker = {};
}
uint32_t error() {
    return fault.load();
}
void registerEye(unsigned eye, ID3D12Resource* resource) {
    if (!enabled || eye >= 2 || !resource)
        return;
    std::lock_guard lock(recording);
    if (resources[eye].Get() == resource)
        return;
    if (resources[eye]) {
        fault = 3501;
        return;
    }
    ComPtr<ID3D12Device> owner;
    ComPtr<IUnknown> a, b;
    if (FAILED(resource->GetDevice(IID_PPV_ARGS(&owner))) || FAILED(owner.As(&a)) || FAILED(device.As(&b)) ||
        a.Get() != b.Get()) {
        fault = 3502;
        return;
    }
    resources[eye] = resource;
    watched[eye] = resource;
}
void registerHud(ID3D12Resource* resource, uint32_t path) {
    if (!enabled || !resource)
        return;
    std::lock_guard lock(recording);
    if (hud.resource.Get() == resource)
        return;
    ComPtr<ID3D12Device> owner;
    ComPtr<IUnknown> a, b;
    bool fits = SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&owner))) && SUCCEEDED(owner.As(&a)) &&
                SUCCEEDED(device.As(&b)) && a.Get() == b.Get();
    D3D12_RESOURCE_DESC desc{};
    if (fits) {
        desc = resource->GetDesc();
        fits = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.Width >= 64 && desc.Width <= 16384 &&
               desc.Height >= 64 && desc.Height <= 16384 && desc.DepthOrArraySize == 1;
    }
    if (!fits) {
        ++hud.refused;
        publishHud();
        return;
    }
    // A copy may still read the old one; the game may have let it go already.
    if (hud.resource)
        hud.retired.push_back({hud.resource, GetTickCount64() + 3000});
    hud.resource = resource;
    hud.path = path;
    hud.mips = desc.MipLevels;
    hud.known = false;
    hudWatched = resource;
    publishHud();
}
void refuseHud() {
    if (!enabled)
        return;
    std::lock_guard lock(recording);
    ++hud.refused;
    publishHud();
}
void wantHud(bool wanted) {
    hudWanted = wanted;
}
void wantHudLayer(bool wanted) {
    hudLayerWanted = wanted;
}
bool latestHud(uint32_t& hudWidth, uint32_t& hudHeight, uint64_t& copies, uint64_t& copiedMs) {
    std::lock_guard recordLock(recording);
    if (!enabled || !hud.staged || !hud.stagedCopies)
        return false;
    const auto desc = hud.staged->GetDesc();
    hudWidth = static_cast<uint32_t>(desc.Width);
    hudHeight = desc.Height;
    copies = hud.stagedCopies;
    copiedMs = hud.stagedMs;
    return true;
}
bool fillHudLayer(ID3D12Resource* target) {
    if (!target)
        return false;
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (!enabled || fault || !hud.staged || GetTickCount64() >= deadline)
        return false;
    const auto desc = target->GetDesc(), own = hud.staged->GetDesc();
    ComPtr<ID3D12Device> owner;
    ComPtr<IUnknown> a, b;
    if (!rgba8CopyTarget(desc) || desc.Width != own.Width || desc.Height != own.Height ||
        FAILED(target->GetDevice(IID_PPV_ARGS(&owner))) || FAILED(owner.As(&a)) || FAILED(device.As(&b)) ||
        a.Get() != b.Get())
        return false;
    auto* slot = readyCommands(hudLayerCommands);
    if (!slot)
        return false; // earlier copies still in flight: the layer keeps its last image
    copyEyeTexture(slot->list.Get(), hud.staged.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, target, originalBarrier);
    if (!submitCommands(*slot))
        return false;
    ++hud.layerCopies;
    publishHud();
    return true;
}
void hudLayerShown() {
    std::lock_guard recordLock(recording);
    ++hud.layerFrames;
    publishHud();
}
bool arm(uint64_t serial, ID3D12Resource* left, ID3D12Resource* right) {
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (!enabled || !copyToXr || staged || fault || !serial || requestedSerial ||
        GetTickCount64() >= deadline)
        return false;
    ID3D12Resource* incoming[] = {left, right};
    for (unsigned i = 0; i < 2; ++i) {
        if (!incoming[i]) {
            fault = 3701;
            return false;
        }
        ComPtr<ID3D12Device> owner;
        ComPtr<IUnknown> a, b;
        if (FAILED(incoming[i]->GetDevice(IID_PPV_ARGS(&owner))) || FAILED(owner.As(&a)) ||
            FAILED(device.As(&b)) || a.Get() != b.Get()) {
            fault = 3702;
            return false;
        }
        const auto desc = incoming[i]->GetDesc();
        if (!rgba8CopyTarget(desc)) {
            fault = 3703;
            return false;
        }
        if (resources[i]) {
            const auto source = resources[i]->GetDesc();
            if (source.Width != desc.Width || source.Height != desc.Height ||
                resources[i].Get() == incoming[i]) {
                fault = 3704;
                return false;
            }
        }
    }
    targets = {left, right};
    requestedSerial = serial;
    requestFence = 0;
    return true;
}
bool copyLatest(ID3D12Resource* left, ID3D12Resource* right, uint64_t& serial, uint64_t& generation) {
    serial = generation = 0;
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (!enabled || !staged || fault || !capturedStamp.serial || GetTickCount64() >= deadline ||
        GetTickCount64() - capturedMs > imageHoldMs)
        return false;
    ID3D12Resource* incoming[] = {left, right};
    for (unsigned i = 0; i < 2; ++i) {
        if (!incoming[i]) {
            fault = 3701;
            return false;
        }
        const auto desc = incoming[i]->GetDesc();
        if (!rgba8CopyTarget(desc) || desc.Width != width || desc.Height != height) {
            fault = 3703;
            return false;
        }
        ComPtr<ID3D12Device> owner;
        ComPtr<IUnknown> a, b;
        if (FAILED(incoming[i]->GetDevice(IID_PPV_ARGS(&owner))) || FAILED(owner.As(&a)) ||
            FAILED(device.As(&b)) || a.Get() != b.Get()) {
            fault = 3702;
            return false;
        }
    }
    auto* slot = readyCommands(presentCommands);
    if (!slot)
        return false;
    for (unsigned i = 0; i < 2; ++i) {
        copyEyeTexture(slot->list.Get(), stagedEyes[i].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, incoming[i],
                       originalBarrier);
        if (captureImages)
            captureImage(slot->list.Get(), incoming[i], i);
    }
    if (!submitCommands(*slot))
        return false;
    reusedPairs += capturedStamp.generation == lastGeneration;
    lastSerial = serial = capturedStamp.serial;
    lastGeneration = generation = capturedStamp.generation;
    ++pairs;
    publish();
    return true;
}
void keepShownEye(ID3D12Resource* left) {
    if (!left || !mirrorWanted || screenWanted || !shownWanted)
        return;
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (!enabled || !staged || fault || shownFailed || !stagedEyes[0] || GetTickCount64() >= deadline)
        return;
    const auto desc = left->GetDesc();
    if (!rgba8CopyTarget(desc) || desc.Width != width || desc.Height != height)
        return;
    if (!shownEye) {
        const auto own = stagedEyes[0]->GetDesc();
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        // Without it the window goes on with the game's own image of the eye.
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &own,
                                                   D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                                   IID_PPV_ARGS(&shownEye)))) {
            shownFailed = true; // not tried again every frame
            return;
        }
    }
    auto* slot = readyCommands(shownCommands);
    if (!slot)
        return; // earlier copies still in flight: the next frame's image instead
    copyEyeTexture(slot->list.Get(), left, D3D12_RESOURCE_STATE_RENDER_TARGET, shownEye.Get(),
                   originalBarrier, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (!submitCommands(*slot))
        return;
    shownMs = GetTickCount64();
    shownWanted = false;
}
bool waitCopy(uint64_t serial, uint32_t timeoutMs) {
    std::unique_lock lock(submission);
    copyReady.wait_for(lock, std::chrono::milliseconds(std::min(timeoutMs, 500u)),
                       [&] { return requestedSerial != serial || requestFence || fault || !enabled; });
    if (requestedSerial != serial)
        return false;
    const bool submitted = requestFence != 0;
    // Copies and overlay are submitted to the exact direct queue bound to
    // OpenXR. Queue order supplies the dependency at image release. Allocator
    // reuse still checks the GPU fence; the CPU need not drain the game queue.
    requestedSerial = requestFence = 0;
    targets = {};
    return submitted;
}
DWORD start(void* input) {
    std::lock_guard life(lifecycle);
    Config c{};
    if (enabled || hooked)
        return 1000;
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x53475043 || c.version != 3 ||
        c.bytes != sizeof(c) || c.pid != GetCurrentProcessId() ||
        c.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) ||
        !GetModuleHandleW(L"Spider-Man.exe") || (c.durationMs && c.durationMs < 500) ||
        c.durationMs > 30000 || c.copyToXr > 2 || c.captureImages > 1 || c.reserved ||
        (!c.copyToXr && !c.captureImages) || !c.queue)
        return 1001;
    copyToXr = c.copyToXr != 0;
    staged = c.copyToXr == 2;
    captureImages = c.captureImages != 0;
    queue = reinterpret_cast<ID3D12CommandQueue*>(c.queue);
    if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(queue->GetDevice(IID_PPV_ARGS(&device))))
        return 1002;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&command))) ||
        FAILED(command->Close()) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
        return 1003;
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent)
        return 1004;
    auto vt = *reinterpret_cast<void***>(command.Get());
    hooks = {vt[26], vt[10], (*reinterpret_cast<void***>(queue.Get()))[10], vt[53]}; // EndQuery
    auto s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED)
        return 1100 + s;
    s = MH_CreateHook(hooks[0], reinterpret_cast<void*>(barrier), reinterpret_cast<void**>(&originalBarrier));
    if (s == MH_OK)
        s = MH_CreateHook(hooks[1], reinterpret_cast<void*>(reset), reinterpret_cast<void**>(&originalReset));
    if (s == MH_OK)
        s = MH_CreateHook(hooks[2], reinterpret_cast<void*>(execute),
                          reinterpret_cast<void**>(&originalExecute));
    if (s == MH_OK)
        s = MH_CreateHook(hooks[3], reinterpret_cast<void*>(endQuery),
                          reinterpret_cast<void**>(&originalEndQuery));
    if (s != MH_OK) {
        for (auto h : hooks)
            MH_RemoveHook(h);
        return 1200 + s;
    }
    hooked = true;
    deadline = c.durationMs ? GetTickCount64() + c.durationMs : UINT64_MAX;
    enabled = true;
    // Under one freeze of the game's threads (stereo_probe.cpp says why).
    for (auto h : hooks)
        if (s == MH_OK)
            s = MH_QueueEnableHook(h);
    if (s == MH_OK)
        s = MH_ApplyQueued();
    if (s != MH_OK) {
        enabled = false;
        for (auto t : hooks)
            MH_QueueDisableHook(t);
        MH_ApplyQueued();
        return 1300 + s;
    }
    // The game's presented frame is optional: without it the headset shows
    // nothing while gameplay is unavailable, as before.
    void* presentAddress{};
    void* present1Address{};
    if (presentEntries(presentAddress, present1Address) &&
        MH_CreateHook(presentAddress, reinterpret_cast<void*>(present), reinterpret_cast<void**>(&originalPresent)) ==
            MH_OK) {
        presentHooks[0] = presentAddress;
        if (MH_CreateHook(present1Address, reinterpret_cast<void*>(present1),
                          reinterpret_cast<void**>(&originalPresent1)) == MH_OK)
            presentHooks[1] = present1Address;
        bool hookedPresent = true;
        for (auto h : presentHooks)
            if (h)
                hookedPresent = MH_QueueEnableHook(h) == MH_OK && hookedPresent;
        hookedPresent = MH_ApplyQueued() == MH_OK && hookedPresent;
        publishScreen(hookedPresent ? 1 : 2, nullptr, 0);
    }
    std::lock_guard lock(telemetry);
    InterlockedIncrement64(&SpidyGpuData.sequence);
    SpidyGpuData.status = 1;
    InterlockedIncrement64(&SpidyGpuData.sequence);
    return 0;
}
bool readPixels() { // submission and recording held by caller; GPU work complete
    for (unsigned i = 0; i < 2; ++i) {
        void* data{};
        const auto& layout = layouts[i];
        // The last row has no pitch padding: rows whose bytes are not a
        // multiple of 256 make the buffer shorter than RowPitch * height.
        D3D12_RANGE range{0, static_cast<SIZE_T>(layout.Offset + static_cast<UINT64>(layout.Footprint.RowPitch) *
                                                                       (height - 1) +
                                                 width * 4)};
        if (FAILED(readbacks[i]->Map(0, &range, &data)))
            return false;
        for (unsigned y = 0; y < height; ++y)
            std::memcpy(pixels[i].data() + static_cast<size_t>(y) * width * 4,
                        static_cast<uint8_t*>(data) + layout.Offset +
                            static_cast<size_t>(y) * layout.Footprint.RowPitch,
                        width * 4);
        D3D12_RANGE written{0, 0};
        readbacks[i]->Unmap(0, &written);
    }
    return true;
}
DWORD freeze() {
    std::lock_guard life(lifecycle);
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (!enabled || !captureImages || staged || copyToXr)
        return 3901;
    if (!pairs || !readbacks[0] || !readbacks[1])
        return 3902;
    if (fence->GetCompletedValue() < fenceValue &&
        (FAILED(fence->SetEventOnCompletion(fenceValue, fenceEvent)) ||
         WaitForSingleObject(fenceEvent, 1000) != WAIT_OBJECT_0))
        return 3903;
    if (!readPixels())
        return 3904;
    publish();
    return 0;
}
DWORD stop() {
    std::lock_guard life(lifecycle);
    enabled = false;
    screenWanted = false;
    hudWanted = false;
    hudLayerWanted = false;
    copyReady.notify_all();
    if (hooked) {
        for (auto h : hooks)
            MH_DisableHook(h);
        for (auto h : presentHooks)
            if (h)
                MH_DisableHook(h);
    }
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (fence && fence->GetCompletedValue() < fenceValue) {
        if (FAILED(fence->SetEventOnCompletion(fenceValue, fenceEvent)) ||
            WaitForSingleObject(fenceEvent, 5000) != WAIT_OBJECT_0)
            fault = 3601;
    }
    if (fence && (fence->GetCompletedValue() == UINT64_MAX || fence->GetCompletedValue() < fenceValue))
        fault = 3601;
    if (pairs && captureImages && !fault && !readPixels())
        fault = 3602;
    publish();
    std::lock_guard lock(telemetry);
    InterlockedIncrement64(&SpidyGpuData.sequence);
    SpidyGpuData.status = fault ? 3 : 2;
    InterlockedIncrement64(&SpidyGpuData.sequence);
    return fault.load();
}
} // namespace spidy::native_gpu
extern "C" __declspec(dllexport) DWORD WINAPI SpidyGpuStart(void* input) {
    return native_gpu::start(input);
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyGpuStop(void*) {
    return native_gpu::stop();
}
// Diagnostic: how many times the game presented since the GPU bridge started
// (Present and Present1 each count); with 1 how many presents got the left
// eye on the game's window, and with 3 how many of them got it as the headset
// showed it; with 2 how many presents were held back from the window.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyPresentCount(void* which) {
    switch (reinterpret_cast<uintptr_t>(which)) {
    case 0:
        return native_gpu::presents();
    case 2:
        return native_gpu::heldPresents();
    case 3:
        return native_gpu::mirroredShown();
    default:
        return native_gpu::mirrored();
    }
}
// While the window shows the left eye, at most this many of the game's
// presents a second reach it (1-240; 0: every one). Takes effect at once.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMonitorRate(void* rate) {
    const auto value = reinterpret_cast<uintptr_t>(rate);
    if (!spidy::validMonitorRate(static_cast<uint32_t>(std::min<uintptr_t>(value, UINT32_MAX))))
        return 3911;
    native_gpu::monitorRate(static_cast<uint32_t>(value));
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyGpuFreeze(void*) {
    return native_gpu::freeze();
}
// Probes: 0 stops and 1 starts copying the game's presented frame; 2 reads
// the newest copy back into SpidyScreenData.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyGpuScreen(void* mode) {
    const auto value = reinterpret_cast<uintptr_t>(mode);
    if (value > 2)
        return 3950;
    if (value < 2) {
        native_gpu::wantScreen(value == 1);
        return 0;
    }
    return native_gpu::readScreen();
}
