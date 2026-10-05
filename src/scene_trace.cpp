// Passive trace of native secondary-view scheduling and render-job identities.
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <windows.h>
struct Config { uint32_t magic,version,bytes,pid;uint64_t base,eyes[2]; };
struct Sample { uint64_t view{},owner{},count{},caller{};uint32_t kind{},flags{},active{},thread{};float pose[16]{};
    uint8_t viewBytes[0x1f70]{},toneSettings[0x240]{}; };
struct Data { uint32_t magic=0x53545243,version=2,bytes=sizeof(Data),state{};volatile LONG64 sequence{};
    uint64_t loops{};uint32_t count{},error{};Sample samples[32]{}; };
static_assert(sizeof(Config)==40 && sizeof(Sample)==8736 && sizeof(Data)==279592);
extern "C" { __declspec(dllexport) Data SpidyTraceData; }
namespace {
using Loop=bool(*)();using Offscreen=bool(*)(void*,bool);using Scene=void*(*)(void*,void*,uint32_t,bool);using Pass=void(*)(void*);
using Tone=uint64_t(*)(void*,void*,void*,void*,void*,uint64_t);
Loop originalLoop{};Offscreen originalOffscreen{};Scene originalScene{};Pass originalPass{};
Tone originalTone{};
void* targets[5]{};Config config{};std::atomic<bool> enabled{};bool created{};
// The stereo adapter now owns tone mapping. Trace only the independent scene
// entries; do not install a second detour over an adapter-owned entry.
constexpr unsigned hookCount=3;
SRWLOCK lifecycle=SRWLOCK_INIT,telemetry=SRWLOCK_INIT;
uint64_t keys[32]{};
bool read(uint64_t address,void* out,size_t n) {
    __try{if(address<0x10000)return false;std::memcpy(out,reinterpret_cast<void*>(address),n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void record(uint32_t kind,void* view,void* caller,void* settings=nullptr,uint32_t flags=0) {
    if(!enabled)return;
    AcquireSRWLockExclusive(&telemetry);auto& d=SpidyTraceData;InterlockedIncrement64(&d.sequence);
    if(!kind)++d.loops;
    else {
        const auto address=reinterpret_cast<uint64_t>(view);uint64_t owner{};read(address+0x1f40,&owner,8);
        // Group copied render jobs by their stable offscreen owner, retaining
        // the most recent job address. A copied view is not the original pointer.
        uint64_t name{};if(kind>=3)read(address+0x1f30,&name,8);
        const auto key=owner?owner:(kind>=3?name:address);unsigned slot=d.count;
        for(unsigned i=0;i<d.count;++i)if(d.samples[i].kind==kind &&
            keys[i]==key){slot=i;break;}
        if(slot<32){if(slot==d.count)++d.count;keys[slot]=key;auto& s=d.samples[slot];s.kind=kind;s.view=address;s.owner=owner;
            ++s.count;s.caller=reinterpret_cast<uint64_t>(caller);s.thread=GetCurrentThreadId();
            read(address,s.pose,sizeof(s.pose));uint8_t active{};read(address+0x1208,&active,1);s.active=active;
            bool ours=false;for(auto eye:config.eyes)ours|=eye==address;
            if(ours)read(address+0x14e7c,&s.flags,4);
            if(kind==2)s.flags=flags;
            read(address,s.viewBytes,sizeof(s.viewBytes));
            if(settings)read(reinterpret_cast<uint64_t>(settings),s.toneSettings,sizeof(s.toneSettings));
        }
    }
    InterlockedIncrement64(&d.sequence);ReleaseSRWLockExclusive(&telemetry);
}
bool loop(){record(0,nullptr,nullptr);return originalLoop();}
bool offscreen(void* view,bool preview){record(1,view,_ReturnAddress());return originalOffscreen(view,preview);}
void* scene(void* view,void* context,uint32_t flags,bool last){record(2,view,_ReturnAddress(),nullptr,flags);return originalScene(view,context,flags,last);}
void pass(void* view){record(3,view,_ReturnAddress());originalPass(view);}
uint64_t tone(void* a,void* b,void* settings,void* view,void* e,uint64_t f){
    record(4,view,_ReturnAddress(),settings);return originalTone(a,b,settings,view,e,f);
}
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* input) {
    AcquireSRWLockExclusive(&lifecycle);DWORD result{};
    do {
        if(enabled)break;
        if(!read(reinterpret_cast<uint64_t>(input),&config,sizeof(config)) || config.magic!=0x53544346 || config.version!=1 ||
           config.bytes!=sizeof(config) || config.pid!=GetCurrentProcessId() || config.base!=reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr))){result=1001;break;}
        if(!created){
            const uint32_t rvas[]={0x1920240,0x186d050,0x19206a0,0x189e310,0x1846c20};
            const uint8_t signatures[5][5]={{0x48,0x89,0x5c,0x24,0x08},{0x40,0x55,0x56,0x41,0x55},
                {0x44,0x88,0x4c,0x24,0x20},{0x48,0x89,0x5c,0x24,0x08},{0x48,0x8b,0xc4,0x48,0x89}};
            for(unsigned i=0;i<hookCount;++i){uint8_t bytes[5]{};targets[i]=reinterpret_cast<void*>(config.base+rvas[i]);
                if(!read(config.base+rvas[i],bytes,5)||std::memcmp(bytes,signatures[i],5))result=1002+i;}
            if(result)break;
            auto s=MH_Initialize();if(s!=MH_OK){result=1100+s;break;}
            void* detours[]={reinterpret_cast<void*>(loop),reinterpret_cast<void*>(offscreen),reinterpret_cast<void*>(scene),reinterpret_cast<void*>(pass),reinterpret_cast<void*>(tone)};
            void** originals[]={reinterpret_cast<void**>(&originalLoop),reinterpret_cast<void**>(&originalOffscreen),reinterpret_cast<void**>(&originalScene),reinterpret_cast<void**>(&originalPass),reinterpret_cast<void**>(&originalTone)};
            for(unsigned i=0;i<hookCount && s==MH_OK;++i)s=MH_CreateHook(targets[i],detours[i],originals[i]);
            if(s!=MH_OK){MH_Uninitialize();result=1200+s;break;}created=true;
        }
        enabled=true;
        for(unsigned i=0;i<hookCount;++i){auto s=MH_EnableHook(targets[i]);if(s!=MH_OK){result=1300+s;break;}}
        if(result){enabled=false;for(unsigned i=0;i<hookCount;++i)MH_DisableHook(targets[i]);}
    }while(false);
    AcquireSRWLockExclusive(&telemetry);InterlockedIncrement64(&SpidyTraceData.sequence);
    SpidyTraceData.state=result?3:1;SpidyTraceData.error=result;
    InterlockedIncrement64(&SpidyTraceData.sequence);ReleaseSRWLockExclusive(&telemetry);
    ReleaseSRWLockExclusive(&lifecycle);return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);enabled=false;DWORD result{};
    if(created)for(unsigned i=0;i<hookCount;++i){auto s=MH_DisableHook(targets[i]);if(s!=MH_OK&&s!=MH_ERROR_DISABLED)result=1400+s;}
    AcquireSRWLockExclusive(&telemetry);InterlockedIncrement64(&SpidyTraceData.sequence);SpidyTraceData.state=result?3:2;
    InterlockedIncrement64(&SpidyTraceData.sequence);ReleaseSRWLockExclusive(&telemetry);
    ReleaseSRWLockExclusive(&lifecycle);return result;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);return TRUE;}
