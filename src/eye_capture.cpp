// Diagnostic GPU readback for the engine-owned offscreen eye views. Tracks
// submitted transitions, copies on the same direct queue, and waits for a fence.
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <d3d12.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
struct Config {
    uint32_t magic, version, bytes, pid;
    uint64_t base, queue, views[2], resources[2];
};
struct Eye {
    uint64_t resource{}, transitions{}, pixels{};
    uint32_t state{}, known{}, width{}, height{}, bytes{}, flags{};
};
struct CommandType { uint64_t vtable{}, barrier{},reset{},count{}; };
struct Candidate { uint64_t resource{},identity{},count{};uint32_t width{},height{},format{},state{}; };
struct Data {
    uint32_t magic=0x53454350, version=4, bytes=sizeof(Data), status{};
    volatile LONG64 sequence{};
    uint64_t executes{}, barriers{};
    uint32_t error{}, captured{};
    Eye eyes[2];
    uint64_t rawBarriers{}, matchedBarriers{}, ownBarrier{}, ownReset{};
    CommandType types[8]{};
    uint64_t nativePasses{},eyePasses[2]{};
    uint64_t copies{},matchedCopies{};
    Candidate candidates[16]{};
};
static_assert(sizeof(Config)==64 && sizeof(Eye)==48 && sizeof(Data)==1112);
extern "C" { __declspec(dllexport) Data SpidyEyeData; }
namespace {
using Barrier=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
using Execute=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
using Reset=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using Copy=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12Resource*,ID3D12Resource*);
using CopyTexture=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,const D3D12_TEXTURE_COPY_LOCATION*,UINT,UINT,UINT,const D3D12_TEXTURE_COPY_LOCATION*,const D3D12_BOX*);
Barrier originalBarrier{}; Execute originalExecute{}; Reset originalReset{};
Copy originalCopy{};CopyTexture originalCopyTexture{};void* copyTarget{};void* copyTextureTarget{};
void* barrierTarget{};void* executeTarget{};void* resetTarget{};
Config config{};std::atomic<bool> enabled{};bool hooked{};
std::mutex lifecycle, submissions, records;
std::atomic<uint64_t> rawBarriers{},matchedBarriers{};
std::atomic<uint64_t> copies{},matchedCopies{};
std::atomic<uint64_t> nativePasses{},eyePasses[2]{};
struct Transition { unsigned eye;D3D12_RESOURCE_BARRIER_FLAGS flags;D3D12_RESOURCE_TRANSITION_BARRIER t; };
std::unordered_map<ID3D12CommandList*,std::vector<Transition>> recorded;
std::unordered_set<ID3D12Resource*> inspected;
std::array<Candidate,16> candidates{};
std::atomic<uint32_t> recordingError{};
bool captureAttempted{};
ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandQueue> queue;
std::array<ComPtr<ID3D12Resource>,2> resources,readbacks;
std::array<std::vector<uint8_t>,2> pixels;
std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,2> layouts{};
ComPtr<ID3D12CommandAllocator> allocator;
ComPtr<ID3D12GraphicsCommandList> command;
ComPtr<ID3D12Fence> fence;
// Diagnostic inventory: wrapper layers may substitute a different COM resource
// when commands reach the actual D3D12 command list. Inspect each pointer once.
// The caller holds records and the resource is alive for the intercepted call.
void inspect(ID3D12Resource* resource,D3D12_RESOURCE_STATES state) {
    if(!resource)return;
    for(auto& candidate:candidates)if(candidate.resource==reinterpret_cast<uint64_t>(resource)) {
        ++candidate.count;candidate.state=state;return;
    }
    if(inspected.contains(resource) || inspected.size()>=8192)return;
    inspected.insert(resource);
    const auto desc=resource->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       desc.Width!=SpidyEyeData.eyes[0].width || desc.Height!=SpidyEyeData.eyes[0].height)return;
    for(auto& candidate:candidates)if(!candidate.resource) {
        ComPtr<IUnknown> identity;resource->QueryInterface(IID_PPV_ARGS(&identity));
        candidate={reinterpret_cast<uint64_t>(resource),reinterpret_cast<uint64_t>(identity.Get()),1,
            static_cast<uint32_t>(desc.Width),desc.Height,static_cast<uint32_t>(desc.Format),static_cast<uint32_t>(state)};
        break;
    }
}
void copiedTo(ID3D12GraphicsCommandList* list,ID3D12Resource* destination) {
    if(!enabled)return;
    ++copies;
    std::lock_guard lock(records);inspect(destination,D3D12_RESOURCE_STATE_COPY_DEST);
    for(unsigned i=0;i<2;++i)if(destination==resources[i].Get()) {
        ++matchedCopies;
        // COPY_DEST is an exclusive write state required by this native command.
        // An ordinary texture promoted to COPY_DEST does not decay after a direct
        // queue submission. Track this use even when no new transition is needed.
        auto& entries=recorded[list];
        if(entries.size()<4096)entries.push_back({i,D3D12_RESOURCE_BARRIER_FLAG_NONE,
            {destination,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST}});
        else recordingError=3002;
    }
}
void STDMETHODCALLTYPE copy(ID3D12GraphicsCommandList* list,ID3D12Resource* destination,ID3D12Resource* source) {
    copiedTo(list,destination);originalCopy(list,destination,source);
}
void STDMETHODCALLTYPE copyTexture(ID3D12GraphicsCommandList* list,const D3D12_TEXTURE_COPY_LOCATION* destination,
    UINT x,UINT y,UINT z,const D3D12_TEXTURE_COPY_LOCATION* source,const D3D12_BOX* box) {
    copiedTo(list,destination->pResource);originalCopyTexture(list,destination,x,y,z,source,box);
}
bool readable(uintptr_t p,void* out,size_t bytes) {
    __try { if(p<0x10000)return false;std::memcpy(out,reinterpret_cast<void*>(p),bytes);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void begin(){InterlockedIncrement64(&SpidyEyeData.sequence);}
void end(){InterlockedIncrement64(&SpidyEyeData.sequence);}
void STDMETHODCALLTYPE barrier(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* values) {
    if(enabled) {
        ++rawBarriers;
        std::lock_guard lock(records);
        for(UINT n=0;n<count;++n)if(values[n].Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            inspect(values[n].Transition.pResource,values[n].Transition.StateAfter);
            for(unsigned i=0;i<2;++i)if(values[n].Transition.pResource==resources[i].Get()) {
                ++matchedBarriers;
                auto& listRecords=recorded[list];
                if(listRecords.size()<4096)listRecords.push_back({i,values[n].Flags,values[n].Transition});
                else recordingError=3002;
            }
        }
    }
    originalBarrier(list,count,values);
}
HRESULT STDMETHODCALLTYPE reset(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* alloc,ID3D12PipelineState* state) {
    auto hr=originalReset(list,alloc,state);
    if(enabled && SUCCEEDED(hr)){std::lock_guard lock(records);recorded.erase(list);}
    return hr;
}
void STDMETHODCALLTYPE execute(ID3D12CommandQueue* q,UINT count,ID3D12CommandList* const* lists) {
    if(!enabled){originalExecute(q,count,lists);return;}
    std::lock_guard submissionLock(submissions);
    originalExecute(q,count,lists);
    std::lock_guard recordLock(records);
    begin();++SpidyEyeData.executes;
    SpidyEyeData.rawBarriers=rawBarriers.load();SpidyEyeData.matchedBarriers=matchedBarriers.load();
    SpidyEyeData.nativePasses=nativePasses.load();for(unsigned i=0;i<2;++i)SpidyEyeData.eyePasses[i]=eyePasses[i].load();
    SpidyEyeData.copies=copies.load();SpidyEyeData.matchedCopies=matchedCopies.load();
    std::copy(candidates.begin(),candidates.end(),std::begin(SpidyEyeData.candidates));
    if(recordingError)SpidyEyeData.error=recordingError.load();
    for(UINT n=0;n<count;++n) {
        auto vt=*reinterpret_cast<uint64_t**>(lists[n]);
        for(auto& type:SpidyEyeData.types)if(!type.vtable || type.vtable==reinterpret_cast<uint64_t>(vt)) {
            type.vtable=reinterpret_cast<uint64_t>(vt);type.barrier=vt[26];type.reset=vt[10];++type.count;break;
        }
    }
    for(UINT n=0;n<count;++n)if(auto found=recorded.find(lists[n]);found!=recorded.end()) {
        for(const auto& change:found->second) {
            auto& e=SpidyEyeData.eyes[change.eye];++e.transitions;++SpidyEyeData.barriers;
            if(q!=queue.Get() || change.flags!=D3D12_RESOURCE_BARRIER_FLAG_NONE ||
               (change.t.Subresource!=0 && change.t.Subresource!=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)) {
                e.known=0;SpidyEyeData.error=3001;continue;
            }
            e.state=change.t.StateAfter;e.known=1;
            if(e.flags&D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)e.state=D3D12_RESOURCE_STATE_COMMON;
        }
    }
    end();
}
DWORD create() {
    queue=reinterpret_cast<ID3D12CommandQueue*>(config.queue);
    if(queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return 1101;
    if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device))))return 1102;
    for(unsigned i=0;i<2;++i) {
        resources[i]=reinterpret_cast<ID3D12Resource*>(config.resources[i]);
        ComPtr<ID3D12Device> owner;
        if(FAILED(resources[i]->GetDevice(IID_PPV_ARGS(&owner))))return 1103;
        ComPtr<IUnknown> a,b;device.As(&a);owner.As(&b);
        if(a.Get()!=b.Get())return 1104;
        const auto desc=resources[i]->GetDesc();
        if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize!=1 ||
           desc.MipLevels!=1 || desc.SampleDesc.Count!=1 || desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
           desc.Width>1024 || desc.Height>1024 || !desc.Width || !desc.Height)return 1105;
        auto& e=SpidyEyeData.eyes[i];e.resource=config.resources[i];e.width=static_cast<UINT>(desc.Width);
        e.height=desc.Height;e.flags=desc.Flags;e.bytes=e.width*e.height*4;
        pixels[i].resize(e.bytes);e.pixels=reinterpret_cast<uint64_t>(pixels[i].data());
        UINT64 bytes{};device->GetCopyableFootprints(&desc,0,1,0,&layouts[i],nullptr,nullptr,&bytes);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
        buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;
        buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,IID_PPV_ARGS(&readbacks[i]))))return 1106;
    }
    if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))))return 1107;
    if(FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&command))))return 1108;
    if(FAILED(command->Close()) || FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return 1109;
    barrierTarget=(*reinterpret_cast<void***>(command.Get()))[26];
    resetTarget=(*reinterpret_cast<void***>(command.Get()))[10];
    executeTarget=(*reinterpret_cast<void***>(queue.Get()))[10];
    copyTarget=(*reinterpret_cast<void***>(command.Get()))[17];copyTextureTarget=(*reinterpret_cast<void***>(command.Get()))[16];
    SpidyEyeData.ownBarrier=reinterpret_cast<uint64_t>(barrierTarget);
    SpidyEyeData.ownReset=reinterpret_cast<uint64_t>(resetTarget);
    auto s=MH_Initialize();if(s!=MH_OK)return 1200+s;
    s=MH_CreateHook(barrierTarget,reinterpret_cast<void*>(barrier),reinterpret_cast<void**>(&originalBarrier));
    if(s==MH_OK)s=MH_CreateHook(resetTarget,reinterpret_cast<void*>(reset),reinterpret_cast<void**>(&originalReset));
    if(s==MH_OK)s=MH_CreateHook(executeTarget,reinterpret_cast<void*>(execute),reinterpret_cast<void**>(&originalExecute));
    if(s==MH_OK)s=MH_CreateHook(copyTarget,reinterpret_cast<void*>(copy),reinterpret_cast<void**>(&originalCopy));
    if(s==MH_OK)s=MH_CreateHook(copyTextureTarget,reinterpret_cast<void*>(copyTexture),reinterpret_cast<void**>(&originalCopyTexture));
    // Native eye-job timing belongs to the stereo adapter. This diagnostic
    // watches D3D12 submissions only, avoiding overlapping native detours.
    if(s!=MH_OK){MH_Uninitialize();return 1300+s;}
    hooked=true;return 0;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* input) {
    std::lock_guard life(lifecycle);
    if(enabled || hooked)return 1000;
    if(!readable(reinterpret_cast<uintptr_t>(input),&config,sizeof(config)) || config.magic!=0x53454346 ||
       config.version!=1 || config.bytes!=sizeof(config) || config.pid!=GetCurrentProcessId() ||
       config.base!=reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)) || !GetModuleHandleW(L"Spider-Man.exe"))return 1001;
    DWORD result=create();
    if(!result) {
        enabled=true;
        for(auto target:{barrierTarget,resetTarget,executeTarget,copyTarget,copyTextureTarget}) {
            auto s=MH_EnableHook(target);if(s!=MH_OK){result=1400+s;break;}
        }
    }
    if(result){enabled=false;if(hooked)for(auto target:{barrierTarget,resetTarget,executeTarget,copyTarget,copyTextureTarget})MH_DisableHook(target);}
    {std::lock_guard lock(submissions);begin();SpidyEyeData.status=result?3:1;SpidyEyeData.error=result;end();}return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyCapture(void*) {
    std::lock_guard life(lifecycle);
    if(!enabled || captureAttempted)return 2001;
    for(auto view:config.views) {
        uint32_t flags{};if(!readable(view+0x14e7c,&flags,4) || !(flags&1) || (flags&6))return 2002;
        bool owned=false;for(unsigned i=0;i<8;++i) {
            uint64_t slot{};readable(config.base+0x7a34e00+i*8,&slot,8);owned|=slot==view;
        }
        if(!owned)return 2003;
    }
    {
        std::lock_guard submissionLock(submissions);
        if(SpidyEyeData.error || !SpidyEyeData.eyes[0].known || !SpidyEyeData.eyes[1].known)return 2004;
        // Never reset an allocator after a failed submission/fence attempt.
        captureAttempted=true;
        if(FAILED(allocator->Reset()) || FAILED(command->Reset(allocator.Get(),nullptr)))return 2005;
        for(unsigned i=0;i<2;++i) {
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource=resources[i].Get();b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore=static_cast<D3D12_RESOURCE_STATES>(SpidyEyeData.eyes[i].state);
            b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            if(b.Transition.StateBefore!=b.Transition.StateAfter)originalBarrier(command.Get(),1,&b);
            D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=resources[i].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.pResource=readbacks[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layouts[i];
            command->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
            if(b.Transition.StateBefore!=b.Transition.StateAfter)originalBarrier(command.Get(),1,&b);
        }
        if(FAILED(command->Close()))return 2006;
        ID3D12CommandList* list=command.Get();originalExecute(queue.Get(),1,&list);
        if(FAILED(queue->Signal(fence.Get(),1)))return 2007;
    }
    const auto deadline=GetTickCount64()+5000;
    while(fence->GetCompletedValue()<1 && GetTickCount64()<deadline)Sleep(2);
    if(fence->GetCompletedValue()!=1)return 2008;
    for(unsigned i=0;i<2;++i) {
        const auto& e=SpidyEyeData.eyes[i];const auto& layout=layouts[i];void* mapped{};
        D3D12_RANGE range{0,static_cast<SIZE_T>(layout.Offset+layout.Footprint.RowPitch*e.height)};
        if(FAILED(readbacks[i]->Map(0,&range,&mapped)))return 2009;
        for(unsigned y=0;y<e.height;++y)std::memcpy(pixels[i].data()+y*e.width*4,
            static_cast<uint8_t*>(mapped)+layout.Offset+y*layout.Footprint.RowPitch,e.width*4);
        D3D12_RANGE written{0,0};readbacks[i]->Unmap(0,&written);
    }
    {std::lock_guard lock(submissions);begin();SpidyEyeData.captured=1;end();}return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    std::lock_guard life(lifecycle);enabled=false;DWORD result{};
    if(hooked)for(auto target:{barrierTarget,resetTarget,executeTarget,copyTarget,copyTextureTarget}) {
        auto s=MH_DisableHook(target);if(s!=MH_OK&&s!=MH_ERROR_DISABLED)result=1500+s;
    }
    std::lock_guard lock(submissions);begin();SpidyEyeData.status=result?3:2;end();return result;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);return TRUE;
}
