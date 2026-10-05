// Bounded native view integration. Changes only the verified main-view submit.
#include "spidy/native_view.hpp"
#include <MinHook.h>
#include <atomic>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
using native_view::Descriptor;
struct Config { uint32_t magic,version,bytes,pid; uint64_t base; };
struct Control {
    uint32_t magic=0x53564354,version=1,bytes=sizeof(Control),modes{};
    uint64_t serial{}; uint32_t leaseMs{},reserved{};
    float offset[4]{},rotation[4]{0,0,0,1},fov[4]{}; // left,right,down,up radians
};
struct Data {
    uint32_t magic=0x53564441,version=1,bytes=sizeof(Data),state{};
    volatile LONG64 sequence{};
    uint64_t calls{},matched{},writes{},view{},caller{};
    uint32_t thread{},error{};
    Descriptor before,after;
};
static_assert(sizeof(Config)==24 && sizeof(Control)==80 && sizeof(Data)==2248);
extern "C" { __declspec(dllexport) Data SpidyViewData; }
namespace {
using Submit=void(*)(void*,const Descriptor*);
using Lens=void(*)(Descriptor*,float,float,float,float,float,float,float,float,float,float,bool,bool);
using PoseSetter=void(*)(Descriptor*,const float*);
Submit original{};
uintptr_t base{};
void* hook{};
bool created{};
std::atomic<bool> enabled{};
SRWLOCK lifecycle=SRWLOCK_INIT,commandLock=SRWLOCK_INIT,samples=SRWLOCK_INIT;
Control control; uint64_t deadline{};
bool read(uintptr_t p,void* out,size_t bytes) {
    __try { if(p<0x10000)return false;std::memcpy(out,reinterpret_cast<void*>(p),bytes);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool matches(uintptr_t rva,const unsigned char* expected,size_t n) {
    unsigned char actual[32]{};return n<=sizeof(actual) && read(base+rva,actual,n) && !std::memcmp(expected,actual,n);
}
void submit(void* view,const Descriptor* incoming) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    Descriptor before{},after{};
    uintptr_t primary{}; uint64_t mask{};
    read(base+0x7a34dd0,&primary,8);read(base+0x7a34dd8,&mask,8);
    bool matched=enabled && reinterpret_cast<uintptr_t>(view)==primary && (mask&1) &&
        caller==base+0x1647044 && read(reinterpret_cast<uintptr_t>(incoming),&before,sizeof(before)) &&
        native_view::valid(before);
    bool wrote=false;
    if(matched) {
        after=before;
        AcquireSRWLockShared(&commandLock);
        auto command=control;
        if(GetTickCount64()>=deadline) command.modes=0;
        ReleaseSRWLockShared(&commandLock);
        if(command.modes) {
            auto pose=before.pose();
            if(command.modes&1) pose=native_view::relativePose(pose,
                {{command.offset[0],command.offset[1],command.offset[2]},
                 {command.rotation[0],command.rotation[1],command.rotation[2],command.rotation[3]}});
            float left=before.values[0x400/4],right=before.values[0x404/4];
            float top=before.values[0x408/4],bottom=before.values[0x40c/4];
            if(command.modes&2) {
                left=std::tan(command.fov[0]);right=std::tan(command.fov[1]);
                top=-std::tan(command.fov[3]);bottom=-std::tan(command.fov[2]);
            }
            // These are the game's own perspective constructor and derived
            // matrix/frustum updater. Keep its jitter and reverse-Z convention.
            reinterpret_cast<Lens>(base+0x187bb00)(&after,before.nearZ(),before.farZ(),left,right,top,bottom,
                0,0,before.values[0x428/4],before.values[0x42c/4],false,false);
            reinterpret_cast<PoseSetter>(base+0x187ca10)(&after,pose.data());
            wrote=native_view::valid(after);
        }
    }
    original(view,wrote?&after:incoming);
    if(TryAcquireSRWLockExclusive(&samples)) {
        auto& d=SpidyViewData;InterlockedIncrement64(&d.sequence);
        ++d.calls;d.matched+=matched;d.writes+=wrote;
        d.view=reinterpret_cast<uintptr_t>(view);d.caller=caller;d.thread=GetCurrentThreadId();
        if(matched) { d.before=before;d.after=wrote?after:before; }
        InterlockedIncrement64(&d.sequence);ReleaseSRWLockExclusive(&samples);
    }
}
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidySubmit(void* value) {
    Control c{};
    if(!read(reinterpret_cast<uintptr_t>(value),&c,sizeof(c)) || c.magic!=0x53564354 ||
       c.version!=1 || c.bytes!=sizeof(c) || (c.modes&~7u) || c.leaseMs>500 || c.reserved) return 2001;
    float norm{};
    for(int i=0;i<4;++i) { if(!std::isfinite(c.rotation[i]))return 2002;norm+=c.rotation[i]*c.rotation[i]; }
    if(std::abs(norm-1)>.001f) return 2002;
    for(int i=0;i<3;++i) if(!std::isfinite(c.offset[i]) || std::abs(c.offset[i])>2)return 2003;
    if(c.modes&2) {
        for(float f:c.fov) if(!std::isfinite(f) || std::abs(f)>1.5f)return 2004;
        if(c.fov[0]>=-.01f || c.fov[1]<=.01f || c.fov[2]>=-.01f || c.fov[3]<=.01f)return 2004;
    }
    AcquireSRWLockExclusive(&commandLock);
    if(c.serial<=control.serial) { ReleaseSRWLockExclusive(&commandLock);return 2005; }
    control=c;deadline=GetTickCount64()+c.leaseMs;
    ReleaseSRWLockExclusive(&commandLock);return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* value) {
    AcquireSRWLockExclusive(&lifecycle);DWORD result{};
    do {
        if(enabled)break;
        Config c{};base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if(!GetModuleHandleW(L"Spider-Man.exe") || !read(reinterpret_cast<uintptr_t>(value),&c,sizeof(c)) ||
           c.magic!=0x53564346 || c.version!=1 || c.bytes!=sizeof(c) || c.pid!=GetCurrentProcessId() || c.base!=base) {
            result=1001;break;
        }
        const unsigned char submitBytes[]={0x48,0x3b,0xd1,0x74,0x7c,0x48,0x8b,0xc1};
        const unsigned char lensBytes[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x55};
        const unsigned char poseBytes[]={0x40,0x53,0x48,0x83,0xec,0x60,0x0f,0x10,0x02};
        if(!matches(0x1899ab0,submitBytes,sizeof(submitBytes)) || !matches(0x187bb00,lensBytes,sizeof(lensBytes)) ||
           !matches(0x187ca10,poseBytes,sizeof(poseBytes))) { result=1002;break; }
        if(!created) {
            auto status=MH_Initialize();if(status!=MH_OK){result=1100+status;break;}
            hook=reinterpret_cast<void*>(base+0x1899ab0);
            status=MH_CreateHook(hook,reinterpret_cast<void*>(submit),reinterpret_cast<void**>(&original));
            if(status!=MH_OK){result=1200+status;MH_Uninitialize();break;}created=true;
        }
        // Restart never revives an old command.
        AcquireSRWLockExclusive(&commandLock);deadline=0;ReleaseSRWLockExclusive(&commandLock);
        auto status=MH_EnableHook(hook);if(status!=MH_OK){result=1300+status;break;}enabled=true;
    }while(false);
    AcquireSRWLockExclusive(&samples);InterlockedIncrement64(&SpidyViewData.sequence);
    SpidyViewData.state=result?3:1;SpidyViewData.error=result;
    InterlockedIncrement64(&SpidyViewData.sequence);ReleaseSRWLockExclusive(&samples);
    ReleaseSRWLockExclusive(&lifecycle);return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);enabled=false;DWORD result{};
    if(created) { auto s=MH_DisableHook(hook);if(s!=MH_OK && s!=MH_ERROR_DISABLED)result=1400+s; }
    AcquireSRWLockExclusive(&samples);InterlockedIncrement64(&SpidyViewData.sequence);
    SpidyViewData.state=result?3:2;SpidyViewData.error=result;
    InterlockedIncrement64(&SpidyViewData.sequence);ReleaseSRWLockExclusive(&samples);
    ReleaseSRWLockExclusive(&lifecycle);return result;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);return TRUE;
}
