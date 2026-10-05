#include "spidy/native_eye_gpu.hpp"
#include "spidy/copy_eye_texture.hpp"
#include "spidy/eye_pair_state.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using namespace spidy;
extern "C" {
__declspec(dllexport) native_gpu::Data SpidyGpuData;
}
namespace spidy::native_gpu {
namespace {
using Barrier = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
using Reset = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*,
                                          ID3D12PipelineState*);
using Execute = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using EndQuery = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE,
                                          UINT);
Barrier originalBarrier{};
Reset originalReset{};
Execute originalExecute{};
EndQuery originalEndQuery{};
std::array<void*, 4> hooks{};
std::atomic<bool> enabled{};
std::atomic<uint32_t> fault{};
std::mutex lifecycle, submission, recording, telemetry;
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
    bool marker{}, ended{};
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
std::array<CopyCommands, 3> captureCommands, presentCommands;
std::array<ComPtr<ID3D12Resource>, 2> stagedEyes;
EyePairState::Stamp capturedStamp;
uint64_t capturedPairs{}, reusedPairs{}, capturedMs{};
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
        bool relevant{};
        for (UINT n = 0; n < count; ++n)
            relevant |= bs[n].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
                        (bs[n].Transition.pResource == left || bs[n].Transition.pResource == right);
        if (!relevant) {
            originalBarrier(list, count, bs);
            return;
        }
        std::lock_guard lock(recording);
        for (UINT n = 0; n < count; ++n)
            if (bs[n].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
                for (unsigned i = 0; i < 2; ++i)
                    if (resources[i] && bs[n].Transition.pResource == resources[i].Get()) {
                        Event e{};
                        e.eye = i;
                        e.transition = bs[n].Transition;
                        e.flags = bs[n].Flags;
                        add(list, e);
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
    if (!submitCommands(*slot))
        return;
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
                fault = 3401;
                copyReady.notify_all();
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
} // namespace
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
        GetTickCount64() - capturedMs > 150)
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
    for (auto h : hooks)
        if ((s = MH_EnableHook(h)) != MH_OK) {
            enabled = false;
            for (auto t : hooks)
                MH_DisableHook(t);
            return 1300 + s;
        }
    std::lock_guard lock(telemetry);
    InterlockedIncrement64(&SpidyGpuData.sequence);
    SpidyGpuData.status = 1;
    InterlockedIncrement64(&SpidyGpuData.sequence);
    return 0;
}
DWORD stop() {
    std::lock_guard life(lifecycle);
    enabled = false;
    copyReady.notify_all();
    if (hooked)
        for (auto h : hooks)
            MH_DisableHook(h);
    std::lock_guard submitLock(submission);
    std::lock_guard recordLock(recording);
    if (fence && fence->GetCompletedValue() < fenceValue) {
        if (FAILED(fence->SetEventOnCompletion(fenceValue, fenceEvent)) ||
            WaitForSingleObject(fenceEvent, 5000) != WAIT_OBJECT_0)
            fault = 3601;
    }
    if (fence && (fence->GetCompletedValue() == UINT64_MAX || fence->GetCompletedValue() < fenceValue))
        fault = 3601;
    if (pairs && captureImages && !fault)
        for (unsigned i = 0; i < 2; ++i) {
            void* data{};
            const auto& layout = layouts[i];
            D3D12_RANGE range{0, static_cast<SIZE_T>(layout.Offset + layout.Footprint.RowPitch * height)};
            if (FAILED(readbacks[i]->Map(0, &range, &data))) {
                fault = 3602;
                break;
            }
            for (unsigned y = 0; y < height; ++y)
                std::memcpy(pixels[i].data() + static_cast<size_t>(y) * width * 4,
                            static_cast<uint8_t*>(data) + layout.Offset +
                                static_cast<size_t>(y) * layout.Footprint.RowPitch,
                            width * 4);
            D3D12_RANGE written{0, 0};
            readbacks[i]->Unmap(0, &written);
        }
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
