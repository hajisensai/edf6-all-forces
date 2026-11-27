// The fork-join pool (src/worker_pool.h has the contract).
#include "worker_pool.h"
#include <atomic>
#include <cwchar>
#include <immintrin.h>

namespace crew::workers {
namespace {
constexpr int kMostWorkers=8;
constexpr int kChunk=2;          // indices a thread takes at a time: soldiers cost unevenly, so small pieces

struct Job {
    TaskFn fn=nullptr;
    void* ctx=nullptr;
    int count=0;
    unsigned mxcsr=0;
    std::atomic<int> next{0};        // the next index not taken
    std::atomic<int> out{0};         // woken workers not yet checked out
    std::atomic<int> faults{0};
    std::atomic<bool> faultTaken{false};
    Fault first{};
};

Job job;
HANDLE wake=nullptr;                 // one count per worker to wake
HANDLE done=nullptr;                 // the last woken worker checked out
HANDLE threads[kMostWorkers]{};
int made=0;                          // workers running
std::atomic<bool> quit{false};
std::atomic<int> limit{0};
std::atomic<DumpWriter> writer{nullptr};
std::atomic<MINIDUMP_TYPE> dumpType{MiniDumpNormal};
std::atomic<bool> dumpedOnce{false};

int Workers() noexcept {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    int n=static_cast<int>(si.dwNumberOfProcessors)-1;
    if(n>kMostWorkers)n=kMostWorkers;
    if(n<0)n=0;
    const int cap=limit.load();
    if(cap>0 && n>cap-1)n=cap-1;
    return n;
}

void Dump(EXCEPTION_POINTERS* ep) noexcept {
    const DumpWriter write=writer.load();
    if(!write || dumpedOnce.exchange(true))return;
    wchar_t dir[MAX_PATH];
    const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",dir,MAX_PATH);
    if(!n || n>=MAX_PATH-16)return;
    wcscat_s(dir,L"\\CrashDumps");
    CreateDirectoryW(dir,nullptr);
    wchar_t exe[MAX_PATH];
    const DWORD e=GetModuleFileNameW(nullptr,exe,MAX_PATH);
    if(!e || e>=MAX_PATH)return;
    const wchar_t* name=wcsrchr(exe,L'\\');
    name=name ? name+1 : exe;
    wchar_t path[MAX_PATH];
    if(swprintf_s(path,L"%ls\\%ls.%lu.npcmt.dmp",dir,name,GetCurrentProcessId())<=0)return;
    const HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),ep,FALSE};
    write(GetCurrentProcess(),GetCurrentProcessId(),file,dumpType.load(),&info,nullptr,nullptr);
    CloseHandle(file);
    job.first.dumped=true;
}

// The fault's facts kept (the first one's in full) before the stack unwinds; the call is then abandoned.
int Caught(EXCEPTION_POINTERS* ep,int index) noexcept {
    job.faults.fetch_add(1);
    if(!job.faultTaken.exchange(true)) {
        job.first.code=ep->ExceptionRecord->ExceptionCode;
        job.first.address=ep->ExceptionRecord->ExceptionAddress;
        job.first.index=index;
        job.first.thread=GetCurrentThreadId();
        Dump(ep);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void Call(int index) noexcept {
    __try {
        job.fn(job.ctx,index);
    } __except(Caught(GetExceptionInformation(),index)) {}
}

void Drain() noexcept {
    for(;;) {
        const int at=job.next.fetch_add(kChunk);
        if(at>=job.count)return;
        const int end=at+kChunk<job.count ? at+kChunk : job.count;
        for(int i=at;i<end;++i)Call(i);
    }
}

DWORD WINAPI Worker(void*) {
    for(;;) {
        WaitForSingleObject(wake,INFINITE);
        if(quit.load())return 0;
        const unsigned mine=_mm_getcsr();
        _mm_setcsr(job.mxcsr);
        Drain();
        _mm_setcsr(mine);
        if(job.out.fetch_sub(1)==1)SetEvent(done);
    }
}

bool Make(int want) noexcept {
    if(!wake) {
        wake=CreateSemaphoreW(nullptr,0,kMostWorkers,nullptr);
        done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!wake || !done)return false;
    }
    while(made<want) {
        const HANDLE t=CreateThread(nullptr,256*1024,&Worker,nullptr,0,nullptr);
        if(!t)break;
        threads[made++]=t;
    }
    return true;
}
}  // namespace

void SetDumpWriter(DumpWriter w,MINIDUMP_TYPE type) noexcept { dumpType.store(type);writer.store(w); }
void SetLimit(int n) noexcept { limit.store(n<0 ? 0 : n); }
int Threads() noexcept { return Workers()+1; }

int Run(TaskFn fn,void* ctx,int count,Fault* fault) noexcept {
    if(fault)*fault={};
    if(!fn || count<=0)return 0;
    job.fn=fn;job.ctx=ctx;job.count=count;job.mxcsr=_mm_getcsr();
    job.next.store(0);job.faults.store(0);job.faultTaken.store(false);job.first={};
    int wakeN=Workers();
    const int pieces=(count+kChunk-1)/kChunk;
    if(wakeN>pieces-1)wakeN=pieces-1;
    if(wakeN>0 && Make(wakeN)) {
        if(wakeN>made)wakeN=made;
    } else wakeN=0;
    job.out.store(wakeN);
    if(wakeN>0)ReleaseSemaphore(wake,wakeN,nullptr);
    Drain();
    if(wakeN>0)WaitForSingleObject(done,INFINITE);   // every woken worker checked out: the job is free again
    const int faults=job.faults.load();
    if(fault){*fault=job.first;fault->count=faults;}
    return faults;
}

void Shutdown() noexcept {
    if(!made)return;
    quit.store(true);
    ReleaseSemaphore(wake,made,nullptr);
    WaitForMultipleObjects(static_cast<DWORD>(made),threads,TRUE,INFINITE);
    for(int i=0;i<made;++i){CloseHandle(threads[i]);threads[i]=nullptr;}
    made=0;
    quit.store(false);
}
}  // namespace crew::workers
