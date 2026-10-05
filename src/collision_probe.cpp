// Six read-only rays while an existing native collision query still owns its
// world/filter lifetime. The synchronous wrapper holds its lock; the worker
// remains inside CastRayVsWorld::execute, before completion is signalled.
#include "spidy/math.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
struct Config {
    uint32_t magic,version,bytes,pid;
    uint64_t base;
    float origin[3],distance;
    uint32_t mask,durationMs;
};
struct Sample {
    float origin[3]{},direction[3]{},fraction{},normal[3]{},position[3]{};
    uint32_t count{},bodyId{},filter{},queryFilter{},reserved{};
    uint8_t body[0xc0]{};
};
struct Caller {
    uint64_t address{},world{},count{};
    uint32_t filter{},thread{};
};
struct Data {
    uint32_t magic=0x53435044,version=2,bytes=sizeof(Data),status{};
    int64_t sequence{};
    uint64_t calls{},eligible{},queries{},world{};
    uint32_t error{},thread{};
    Sample samples[6];
    Caller callers[16];
};
static_assert(sizeof(Config)==48 && sizeof(Sample)==264 && sizeof(Data)==2160);
extern "C" {__declspec(dllexport) Data SpidyCollisionData;}
namespace {
using Cast=void(*)(void*,const void*,void*);
Cast originalCast{};void* hook{};
Config config{};uintptr_t base{};
std::atomic<bool> enabled{},claimed{};
std::atomic<uint64_t> calls{},eligible{};
std::atomic<unsigned> active{};
uint64_t deadline{};bool created{};
SRWLOCK lifecycle=SRWLOCK_INIT,output=SRWLOCK_INIT;
bool read(uintptr_t address,void* out,size_t size) {
    __try {if(address<0x10000)return false;std::memcpy(out,reinterpret_cast<void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
uintptr_t pointer(uintptr_t address){uintptr_t p{};read(address,&p,8);return p;}
template<class T> void put(uint8_t* bytes,size_t offset,const T& value){std::memcpy(bytes+offset,&value,sizeof(T));}
template<class T> T get(const uint8_t* bytes,size_t offset){T value{};std::memcpy(&value,bytes+offset,sizeof(T));return value;}
void cast(void* world,const void* nativeQuery,void* nativeCollector) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    struct Guard {Guard(){++active;} ~Guard(){--active;}} guard;
    originalCast(world,nativeQuery,nativeCollector);
    if(!enabled || GetTickCount64()>=deadline)return;
    ++calls;
    const auto queryAddress=reinterpret_cast<uintptr_t>(nativeQuery);
    const bool synchronous=caller==base+0x18173d1;
    const bool worker=caller==base+0x18180fe && queryAddress>=0x10020 &&
        pointer(queryAddress-0x20)==base+0x3d0a9c8;
    const bool service=(synchronous || worker) &&
        reinterpret_cast<uintptr_t>(world)==pointer(base+0x78939e8) &&
        pointer(queryAddress+8)==pointer(reinterpret_cast<uintptr_t>(world)+0x498);
    if(service)++eligible;
    AcquireSRWLockExclusive(&output);
    auto& record=SpidyCollisionData;InterlockedIncrement64(&record.sequence);
    record.calls=calls.load();record.eligible=eligible.load();
    for(auto& c:record.callers) {
        if(c.address && (c.address!=caller || c.world!=reinterpret_cast<uintptr_t>(world)))continue;
        c.address=caller;c.world=reinterpret_cast<uintptr_t>(world);++c.count;c.thread=GetCurrentThreadId();
        read(reinterpret_cast<uintptr_t>(nativeQuery)+0x14,&c.filter,4);break;
    }
    InterlockedIncrement64(&record.sequence);ReleaseSRWLockExclusive(&output);
    if(!service)return;
    if(claimed.exchange(true))return;
    alignas(16) uint8_t query[0x70]{};
    if(!read(reinterpret_cast<uintptr_t>(nativeQuery),query,0x30))return;
    // Keep the active query's codec, filter, and transient system-group lease.
    // The wrapper/job releases that group only after this detour returns.
    auto info=get<uint32_t>(query,0x14);info=(info&~0x7ffu)|config.mask;put(query,0x14,info);
    constexpr Vec3 directions[]={{0,-1,0},{0,1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
    Sample samples[6]{};
    uint32_t error{},completed{};
    for(unsigned i=0;i<6;++i) {
        const Vec3 origin{config.origin[0],config.origin[1],config.origin[2]},direction=directions[i];
        const float from[]={origin.x,origin.y,origin.z,0};
        const float delta[]={direction.x*config.distance,direction.y*config.distance,direction.z*config.distance,1};
        float inverse[4]{};
        for(unsigned j=0;j<4;++j)inverse[j]=delta[j]!=0?1.f/delta[j]:3.402823466e38f;
        std::memcpy(query+0x30,from,16);std::memcpy(query+0x40,delta,16);std::memcpy(query+0x50,inverse,16);
        put(query,0x60,uint32_t{0});
        alignas(16) uint8_t hits[16*0x90]{};
        alignas(16) uint8_t collector[0x50]{};
        put(collector,0,base+0x3d0a948);put(collector,0x38,reinterpret_cast<uintptr_t>(hits));
        put(collector,0x40,uint32_t{16});
        // These are the native reset values used by 1817190 and 1818390.
        if(!read(base+0x6b14b40,collector+0x10,16)){error=2001;break;}
        originalCast(world,query,collector);
        ++completed;
        auto& s=samples[i];std::memcpy(s.origin,from,12);std::memcpy(s.direction,&direction,12);
        s.queryFilter=info;s.count=get<uint32_t>(collector,0xc);s.fraction=1;s.bodyId=0xffffffff;
        if(s.count>16){error=2002;break;}
        for(unsigned h=0;h<s.count;++h) {
            const auto hit=hits+h*0x90;const float fraction=get<float>(hit,0x20);
            if(!std::isfinite(fraction) || fraction<0 || fraction>1){error=2003;break;}
            if(fraction<=s.fraction) {
                s.fraction=fraction;s.bodyId=get<uint32_t>(hit,0x48);s.filter=get<uint32_t>(hit,0x78);
                std::memcpy(s.normal,hit+0x10,12);
                const auto point=origin+direction*(config.distance*fraction);std::memcpy(s.position,&point,12);
                const auto bodies=pointer(reinterpret_cast<uintptr_t>(world)+0x28);
                read(bodies+static_cast<uintptr_t>(s.bodyId&0xffffff)*0xc0,s.body,sizeof(s.body));
            }
        }
        if(error)break;
    }
    AcquireSRWLockExclusive(&output);auto& d=SpidyCollisionData;InterlockedIncrement64(&d.sequence);
    d.calls=calls.load();d.eligible=eligible.load();d.queries=completed;d.world=reinterpret_cast<uintptr_t>(world);
    d.error=error;d.thread=GetCurrentThreadId();d.status=error?3:2;std::memcpy(d.samples,samples,sizeof(samples));
    InterlockedIncrement64(&d.sequence);ReleaseSRWLockExclusive(&output);
}
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);DWORD result{};
    do {
        if(enabled || created){result=1000;break;}
        base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if(!read(reinterpret_cast<uintptr_t>(input),&config,sizeof(config)) || config.magic!=0x53435043 ||
           config.version!=1 || config.bytes!=sizeof(config) || config.pid!=GetCurrentProcessId() || config.base!=base ||
           !GetModuleHandleW(L"Spider-Man.exe") || config.durationMs<500 || config.durationMs>10000 ||
           !config.mask || config.mask>0x7ff || !std::isfinite(config.distance) || config.distance<1 || config.distance>150) {result=1001;break;}
        for(float v:config.origin)if(!std::isfinite(v) || std::abs(v)>100000)result=1001;
        if(result)break;
        const uint8_t expected[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
        uint8_t actual[sizeof(expected)]{};
        if(!read(base+0x2e67010,actual,sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){result=1002;break;}
        auto s=MH_Initialize();if(s!=MH_OK){result=1100+s;break;}
        hook=reinterpret_cast<void*>(base+0x2e67010);
        s=MH_CreateHook(hook,reinterpret_cast<void*>(cast),reinterpret_cast<void**>(&originalCast));
        if(s!=MH_OK){result=1200+s;break;}
        created=true;claimed=false;deadline=GetTickCount64()+config.durationMs;enabled=true;
        s=MH_EnableHook(hook);if(s!=MH_OK){enabled=false;result=1300+s;}
    }while(false);
    if(result)SpidyCollisionData.error=result;
    ReleaseSRWLockExclusive(&lifecycle);return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);enabled=false;DWORD result{};
    if(created){const auto s=MH_DisableHook(hook);if(s!=MH_OK && s!=MH_ERROR_DISABLED)result=1400+s;}
    const auto until=GetTickCount64()+2000;
    while(active && GetTickCount64()<until)Sleep(1);
    if(active)result=1501;
    ReleaseSRWLockExclusive(&lifecycle);return result;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);return TRUE;
}
