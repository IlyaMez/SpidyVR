// Bounded diagnostic watchpoint on the verified camera thread. The worker
// restores that thread's original debug-register state on timeout or stop.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <windows.h>

struct WatchConfig {
    uint32_t magic,version,bytes,pid,thread,milliseconds;
    uint64_t address,imageBase;
};
struct Hit {
    uint64_t rip{},count{},rcx{},rdx{},r8{},r9{},stack[16]{};
};
struct WatchData {
    uint32_t magic=0x53574154,version=1,bytes=sizeof(WatchData),state{};
    volatile LONG64 sequence{};
    uint64_t hits{};
    uint32_t count{},error{},restored{},thread{};
    Hit samples[128]{};
};
static_assert(sizeof(WatchConfig)==40);
static_assert(sizeof(Hit)==176);
static_assert(sizeof(WatchData)==22576);
extern "C" { __declspec(dllexport) WatchData SpidyWatchData; }

namespace {
std::atomic<bool> stopping{false};
HANDLE worker{};
PVOID handler{};
WatchConfig config{};
CONTEXT saved{};
SRWLOCK lifecycle=SRWLOCK_INIT;
bool read(uintptr_t address,void* out,size_t bytes) {
    __try { std::memcpy(out,reinterpret_cast<void*>(address),bytes);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void unwind(const CONTEXT& source,uint64_t* out) {
    __try {
        CONTEXT context=source;
        for(int i=0;i<16 && context.Rip;++i) {
            out[i]=context.Rip;
            DWORD64 image{};
            const auto fn=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
            if(fn) {
                void* data{};DWORD64 frame{};
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,fn,&context,&data,&frame,nullptr);
            } else {
                if(!read(context.Rsp,&context.Rip,8)) break;
                context.Rsp+=8;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
LONG CALLBACK exception(EXCEPTION_POINTERS* pointers) {
    auto& ctx=*pointers->ContextRecord;
    if(pointers->ExceptionRecord->ExceptionCode!=EXCEPTION_SINGLE_STEP ||
       GetCurrentThreadId()!=config.thread || ctx.Dr0!=config.address || !(ctx.Dr6&1))
        return EXCEPTION_CONTINUE_SEARCH;
    auto& data=SpidyWatchData;
    InterlockedIncrement64(&data.sequence);
    ++data.hits;
    unsigned slot=data.count;
    for(unsigned i=0;i<data.count;++i) if(data.samples[i].rip==ctx.Rip) { slot=i;break; }
    if(slot<128) {
        if(slot==data.count) ++data.count;
        auto& h=data.samples[slot];h.rip=ctx.Rip;++h.count;
        h.rcx=ctx.Rcx;h.rdx=ctx.Rdx;h.r8=ctx.R8;h.r9=ctx.R9;
        if(h.count==1) unwind(ctx,h.stack);
    }
    InterlockedIncrement64(&data.sequence);
    ctx.Dr6&=~1ull;
    if(stopping.load()) {
        ctx.Dr0=saved.Dr0;
        ctx.Dr6=(ctx.Dr6&~1ull)|(saved.Dr6&1ull);
        ctx.Dr7=(ctx.Dr7&~0xf0003ull)|(saved.Dr7&0xf0003ull);
    } else if(data.hits>=20000) ctx.Dr7&=~3ull;
    return EXCEPTION_CONTINUE_EXECUTION;
}
bool change(HANDLE thread,bool install) {
    if(SuspendThread(thread)==DWORD(-1)) return false;
    CONTEXT context{};context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
    bool okay=GetThreadContext(thread,&context)!=FALSE;
    if(okay && install) {
        // Never displace a debugger's existing watchpoints.
        if(context.Dr7&0xff) okay=false;
        else {
            saved=context;
            context.Dr0=config.address;
            context.Dr6=0;
            context.Dr7=(context.Dr7&~0xf0003ull)|0xf0001; // local R/W, 4 bytes
            okay=SetThreadContext(thread,&context)!=FALSE;
        }
    } else if(okay) {
        // Restore only while our address and slot configuration remain intact.
        if(context.Dr0==saved.Dr0 && (context.Dr7&0xf0003)==(saved.Dr7&0xf0003)) {}
        else if(context.Dr0!=config.address || (context.Dr7&0xf0000)!=0xf0000) okay=false;
        else {
            context.Dr0=saved.Dr0;
            context.Dr6=(context.Dr6&~1ull)|(saved.Dr6&1ull);
            context.Dr7=(context.Dr7&~0xf0003ull)|(saved.Dr7&0xf0003ull);
            okay=SetThreadContext(thread,&context)!=FALSE;
        }
    }
    if(ResumeThread(thread)==DWORD(-1)) okay=false;
    return okay;
}
DWORD WINAPI run(void*) {
    HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,
                             FALSE,config.thread);
    if(!thread || GetProcessIdOfThread(thread)!=GetCurrentProcessId()) {
        SpidyWatchData.error=3001;SpidyWatchData.state=3;
        if(thread) CloseHandle(thread);return 0;
    }
    if(!handler) handler=AddVectoredExceptionHandler(1,exception);
    if(!handler || !change(thread,true)) {
        SpidyWatchData.error=3002;SpidyWatchData.state=3;
        CloseHandle(thread);return 0;
    }
    SpidyWatchData.state=1;
    const auto end=GetTickCount64()+config.milliseconds;
    while(GetTickCount64()<end && !stopping.load()) Sleep(10);
    stopping=true;
    const bool restored=change(thread,false);
    SpidyWatchData.restored=restored;
    SpidyWatchData.error=restored?0:3003;
    SpidyWatchData.state=restored?2:3;
    // Remain resident so a pending exception can still restore its saved context
    // after the worker's register restore. Ignore all unrelated exceptions.
    CloseHandle(thread);
    return 0;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStart(void* parameter) {
    AcquireSRWLockExclusive(&lifecycle);
    DWORD result{};
    do {
        if(worker && WaitForSingleObject(worker,0)==WAIT_TIMEOUT) { result=1002;break; }
        if(worker) { CloseHandle(worker);worker=nullptr; }
        WatchConfig next{};
        if(!GetModuleHandleW(L"Spider-Man.exe") ||
           !read(reinterpret_cast<uintptr_t>(parameter),&next,sizeof(next)) ||
           next.magic!=0x53574346 || next.version!=1 || next.bytes!=sizeof(next) ||
           next.pid!=GetCurrentProcessId() || next.imageBase!=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) ||
           next.address<0x10000 || next.address%4 || next.milliseconds<100 || next.milliseconds>10000) {
            result=1001;break;
        }
        config=next;SpidyWatchData=WatchData{};SpidyWatchData.thread=config.thread;
        stopping=false;worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);
        if(!worker) result=1003;
    } while(false);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyStop(void*) {
    AcquireSRWLockExclusive(&lifecycle);
    stopping=true;
    DWORD result{};
    if(worker && WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0) result=3004;
    if(!result && SpidyWatchData.error) result=SpidyWatchData.error;
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
