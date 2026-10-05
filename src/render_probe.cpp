// Passive D3D12/DXGI submission probe. Does not render, copy, or replace images.
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

struct Sample {
    uint64_t count{}, object{}, device{}, qpc{};
    uint32_t thread{}, kind{}, width{}, height{}, format{}, stackSize{};
    uint64_t stack[24]{};
};
struct ProbeData {
    uint32_t magic=0x53525042, version=1, bytes=sizeof(ProbeData), state{};
    volatile LONG64 sequence{};
    uint64_t presents{}, executes{};
    uint32_t sampleCount{}, error{};
    Sample samples[32]{};
};
static_assert(sizeof(Sample)==248);
static_assert(sizeof(ProbeData)==7984);
extern "C" { __declspec(dllexport) ProbeData SpidyRenderData; }

namespace {
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Execute=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
Present originalPresent{};
Execute originalExecute{};
void* presentTarget{};
void* executeTarget{};
SRWLOCK lifecycle=SRWLOCK_INIT, output=SRWLOCK_INIT;
std::atomic<uint64_t> presents{}, executes{};
std::atomic<bool> enabled{false};
bool created{};
uintptr_t base{};

void sample(uint32_t kind, uintptr_t object, uintptr_t device, uint32_t width=0,
            uint32_t height=0, uint32_t format=0) {
    if(!enabled || !TryAcquireSRWLockExclusive(&output)) return;
    void* stack[24]{};
    const auto size=CaptureStackBackTrace(2,24,stack,nullptr);
    // Only collect submissions whose stack contains this game's main image.
    bool game=false;
    for(unsigned i=0;i<size;++i) {
        const auto p=reinterpret_cast<uintptr_t>(stack[i]);
        game |= p>=base && p<base+140496896;
    }
    if(!game) { ReleaseSRWLockExclusive(&output); return; }
    auto& d=SpidyRenderData;
    InterlockedIncrement64(&d.sequence);
    d.presents=presents.load(); d.executes=executes.load();
    unsigned slot=d.sampleCount;
    for(unsigned i=0;i<d.sampleCount;++i) {
        auto& old=d.samples[i];
        if(old.object==object && old.kind==kind && old.stackSize==size &&
           !std::memcmp(old.stack,stack,size*sizeof(void*))) { slot=i; break; }
    }
    if(slot<32) {
        if(slot==d.sampleCount) ++d.sampleCount;
        auto& s=d.samples[slot]; ++s.count;
        s.object=object;s.device=device;s.thread=GetCurrentThreadId();s.kind=kind;
        s.width=width;s.height=height;s.format=format;s.stackSize=size;
        std::memcpy(s.stack,stack,size*sizeof(void*));
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);s.qpc=now.QuadPart;
    }
    InterlockedIncrement64(&d.sequence);
    ReleaseSRWLockExclusive(&output);
}
HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* swap,UINT interval,UINT flags) {
    ++presents;
    if(enabled) {
        DXGI_SWAP_CHAIN_DESC desc{};
        ComPtr<ID3D12Device> device;
        if(SUCCEEDED(swap->GetDesc(&desc)) && SUCCEEDED(swap->GetDevice(IID_PPV_ARGS(&device))))
            sample(1,reinterpret_cast<uintptr_t>(swap),reinterpret_cast<uintptr_t>(device.Get()),
                   desc.BufferDesc.Width,desc.BufferDesc.Height,desc.BufferDesc.Format);
    }
    return originalPresent(swap,interval,flags);
}
void STDMETHODCALLTYPE execute(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists) {
    ++executes;
    if(enabled && queue->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT) {
        ComPtr<ID3D12Device> device;
        if(SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))))
            sample(2,reinterpret_cast<uintptr_t>(queue),reinterpret_cast<uintptr_t>(device.Get()));
    }
    originalExecute(queue,count,lists);
}
DWORD createHooks() {
    // Composition swapchain needs no window or desktop input. It is never shown.
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if(FAILED(hr)) return 2101;
    ComPtr<ID3D12Device> device;
    hr=D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device));
    if(FAILED(hr)) return 2102;
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    hr=device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue));
    if(FAILED(hr)) return 2103;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width=16;desc.Height=16;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> swap;
    hr=factory->CreateSwapChainForComposition(queue.Get(),&desc,nullptr,&swap);
    if(FAILED(hr)) return 2104;
    presentTarget=(*reinterpret_cast<void***>(swap.Get()))[8];
    executeTarget=(*reinterpret_cast<void***>(queue.Get()))[10];
    auto status=MH_Initialize();
    if(status!=MH_OK) return 2200+status;
    status=MH_CreateHook(presentTarget,reinterpret_cast<void*>(present),reinterpret_cast<void**>(&originalPresent));
    if(status!=MH_OK) { MH_Uninitialize();return 2300+status; }
    status=MH_CreateHook(executeTarget,reinterpret_cast<void*>(execute),reinterpret_cast<void**>(&originalExecute));
    if(status!=MH_OK) { MH_Uninitialize();return 2400+status; }
    created=true;
    return 0;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        if(enabled) break;
        base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if(!GetModuleHandleW(L"Spider-Man.exe")) { result=1001;break; }
        if(!created) result=createHooks();
        if(result) break;
        enabled=true;
        auto status=MH_EnableHook(presentTarget);
        if(status==MH_OK) status=MH_EnableHook(executeTarget);
        if(status!=MH_OK) { result=2500+status;enabled=false;MH_DisableHook(presentTarget); }
    } while(false);
    SpidyRenderData.state=result?3:1;SpidyRenderData.error=result;
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    enabled=false;
    DWORD result{};
    if(created) for(auto target:{presentTarget,executeTarget}) {
        const auto status=MH_DisableHook(target);
        if(status!=MH_OK && status!=MH_ERROR_DISABLED) result=2600+status;
    }
    SpidyRenderData.state=result?3:2;SpidyRenderData.error=result;
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
