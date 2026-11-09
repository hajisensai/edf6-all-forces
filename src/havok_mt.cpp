// hknp 物理步进多线程（havok_mt_logic.h、docs/havok-mt-re.md）：游戏建好了 15 个 worker 却从不让它们处理任务队列，
// 这里每帧在步进前把 pool 接到队列上，复位释放它们之前先 close 队列并等 worker 退出。
// 插件只在游戏线程上跑（步进前的空函数调用点、复位的两个调用点）；worker 上跑的全是游戏与 Havok 自己的代码，
// 回调的串行性由 Havok 的世界级任务（单实例）保证，见 docs/havok-mt-re.md §1、§3、§11。
#include "havok_mt.h"
#include "havok_mt_logic.h"
#include "crew.h"
#include "memory.h"
#include <atomic>
#include <cstring>

namespace crew {
namespace {
using namespace havokmt;

using PoolProcessFn=void(*)(void* pool,void* queue);
using PoolWaitFn=void(*)(void* pool);
using QueueCloseFn=void(*)(void* sub);
using ResetFn=void(*)(void* holder);

unsigned attaches=0,detaches=0;
StepAction lastRefusal=StepAction::none;
const void* lastRefusedPool=nullptr;

// 世界级任务探针（Debug=1）：包装在任意线程上跑，只动下面的原子量；日志由游戏线程在 BeforeStep / 复位时写。
using TaskProcessFn=std::uintptr_t(*)(void*,void*,void*,void*);
std::atomic<DWORD> gameThread{0};      // BeforeStep 每帧写（游戏线程）
std::atomic<int> worldRunning{0},worldPeak{0};
std::atomic<unsigned> worldRuns{0},worldOnWorkers{0},worldOverlaps{0};
unsigned overlapsLogged=0;
bool probeOn=false;

template<std::size_t I>
std::uintptr_t WorldTaskProbe(void* task,void* input,void* a3,void* a4) noexcept {
    const int now=worldRunning.fetch_add(1,std::memory_order_acq_rel)+1;
    for(int peak=worldPeak.load(std::memory_order_relaxed);now>peak && !worldPeak.compare_exchange_weak(peak,now,std::memory_order_relaxed);) {}
    if(now>1)worldOverlaps.fetch_add(1,std::memory_order_relaxed);
    worldRuns.fetch_add(1,std::memory_order_relaxed);
    if(GetCurrentThreadId()!=gameThread.load(std::memory_order_relaxed))worldOnWorkers.fetch_add(1,std::memory_order_relaxed);
    const std::uintptr_t r=reinterpret_cast<TaskProcessFn>(image+kWorldTasks[I].process)(task,input,a3,a4);
    worldRunning.fetch_sub(1,std::memory_order_acq_rel);
    return r;
}
void* const kWorldTaskProbes[]={reinterpret_cast<void*>(&WorldTaskProbe<0>),reinterpret_cast<void*>(&WorldTaskProbe<1>),
                                   reinterpret_cast<void*>(&WorldTaskProbe<2>)};
static_assert(sizeof(kWorldTaskProbes)/sizeof(kWorldTaskProbes[0])==kWorldTaskCount,"one probe per world task");

void LogProbe(const char* when) noexcept {
    Log("HAVOKMT world tasks %s: %u runs, %u on worker threads, %u overlapping, at most %d at once",when,
        worldRuns.load(std::memory_order_relaxed),worldOnWorkers.load(std::memory_order_relaxed),
        worldOverlaps.load(std::memory_order_relaxed),worldPeak.load(std::memory_order_relaxed));
}

// 三个 vtable 槽 6 全换或全不换；只是诊断，失败不影响多线程本身。
void InstallProbe() noexcept {
    void** slots[kWorldTaskCount]{};
    __try {
        for(std::size_t i=0;i<kWorldTaskCount;++i) {
            slots[i]=reinterpret_cast<void**>(image+kWorldTasks[i].vtable+kWorldTaskProcessSlot*8);
            if(*slots[i]!=image+kWorldTasks[i].process) {
                Log("HAVOKMT world task probe refused: %s vtable %#x slot %u is not %#x",kWorldTasks[i].name,kWorldTasks[i].vtable,
                    static_cast<unsigned>(kWorldTaskProcessSlot),kWorldTasks[i].process);
                return;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("HAVOKMT world task probe refused: vtables unreadable");return;}
    std::size_t done=0;
    for(;done<kWorldTaskCount;++done)
        if(!PatchVtableSlot(slots[done],image+kWorldTasks[done].process,kWorldTaskProbes[done]))break;
    if(done<kWorldTaskCount) {
        for(std::size_t i=done;i-->0;)PatchVtableSlot(slots[i],kWorldTaskProbes[i],image+kWorldTasks[i].process);
        Log("HAVOKMT world task probe refused: %s slot could not be written (all put back)",kWorldTasks[done].name);
        return;
    }
    probeOn=true;
    Log("HAVOKMT world task probe on (Debug=1): PostCollide / PreSolve / PostSolve counted, overlaps logged");
}

bool VtablesOk(const unsigned char* pool,const unsigned char* queue) noexcept {
    return At<const void*>(pool,0)==image+kPoolVtable && At<const void*>(queue,0)==image+kQueueVtable &&
           At<const void*>(queue,kQueueSub)==image+kQueueSubVtable;
}

// 只读游戏内存，不调游戏代码：坏指针在这里变成 false，不会落到下面的调用里。
bool ReadStep(unsigned char* holder,StepView& v,unsigned char*& pool,unsigned char*& queue) noexcept {
    __try {
        v=StepView{};
        v.holder=holder!=nullptr;
        if(!holder)return true;
        pool=At<unsigned char*>(holder,kHolderPool);
        queue=At<unsigned char*>(holder,kHolderQueue);
        v.pool=pool!=nullptr;v.queue=queue!=nullptr;
        if(!pool || !queue)return true;
        v.vtables=VtablesOk(pool,queue);
        if(!v.vtables)return true;
        v.queueAddr=reinterpret_cast<std::uintptr_t>(queue);
        v.attached=reinterpret_cast<std::uintptr_t>(At<void*>(pool,kPoolQueue));
        v.processing=At<std::uint8_t>(pool,kPoolProcessing)!=0;
        v.threads=At<std::int32_t>(pool,kPoolThreads);
        v.closed=At<std::uint8_t>(queue,kQueueClosed)!=0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

const char* RefusalText(StepAction a) noexcept {
    switch(a) {
    case StepAction::foreign: return "the pool is processing another queue";
    case StepAction::stale: return "the pool has this queue set but is not processing it (not a state the plugin leaves)";
    case StepAction::badVtable: return "the pool or queue is not the hkCpuThreadPool / hkDefaultTaskQueue read";
    case StepAction::noWorkers: return "the pool has no worker threads";
    case StepAction::closedQueue: return "the queue is closed";
    case StepAction::unreadable: return "the physics holder, pool or queue is unreadable";
    default: return "";
    }
}

// 步进前（0x119932F，原来调 ret 0 的 0x118F270），游戏线程，rcx = holder。
void BeforeStep(unsigned char* holder) noexcept {
    if(probeOn) {
        gameThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
        // 第一次重叠立刻记一行（之后的累计在每次复位时记），不每帧刷屏
        const unsigned overlaps=worldOverlaps.load(std::memory_order_relaxed);
        if(overlaps && !overlapsLogged){overlapsLogged=overlaps;LogProbe("OVERLAPPED");}
    }
    StepView v;
    unsigned char* pool=nullptr;unsigned char* queue=nullptr;
    const StepAction action=ReadStep(holder,v,pool,queue) ? DecideStep(v) : StepAction::unreadable;
    if(action==StepAction::none)return;
    if(action!=StepAction::attach) {
        if(action!=lastRefusal || pool!=lastRefusedPool)
            Log("HAVOKMT not attached (pool %p, queue %p): %s; the step stays on the game thread",pool,queue,RefusalText(action));
        lastRefusal=action;lastRefusedPool=pool;
        return;
    }
    // 0x130FF70 末尾的做法：processTaskQueue(pool, 完整的 queue 指针)。worker 从此在队列里等任务，空闲时睡在信号量上。
    reinterpret_cast<PoolProcessFn>(image+kPoolProcess)(pool,queue);
    ++attaches;lastRefusal=StepAction::none;lastRefusedPool=nullptr;
    Log("HAVOKMT %d worker threads process the physics task queue (pool %p, queue %p, attach #%u)",v.threads,pool,queue,attaches);
}

// 复位 0x118F0A0 之前（0x70E38E、0x70FEC6），游戏线程：照抄 0x130FF70 开头的 close → waitForCompletion。
void Detach(unsigned char* holder) noexcept {
    unsigned char* pool=nullptr;unsigned char* attached=nullptr;
    bool vtables=false;
    __try {
        if(!holder)return;
        pool=At<unsigned char*>(holder,kHolderPool);
        if(!pool)return;
        attached=At<unsigned char*>(pool,kPoolQueue);
        vtables=At<const void*>(pool,0)==image+kPoolVtable && attached && At<const void*>(attached,0)==image+kQueueVtable &&
                At<const void*>(attached,kQueueSub)==image+kQueueSubVtable;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("HAVOKMT physics holder %p unreadable before the reset",holder);
        return;
    }
    if(!ResetNeedsDetach(vtables,reinterpret_cast<std::uintptr_t>(attached))) {
        if(attached)Log("HAVOKMT reset: pool %p processes %p, not a queue read as hkDefaultTaskQueue: left as it is",pool,attached);
        return;
    }
    LARGE_INTEGER t0{},t1{},f{};
    QueryPerformanceCounter(&t0);
    reinterpret_cast<QueueCloseFn>(image+kQueueClose)(attached+kQueueSub);   // worker 看到 closed 就退出 process
    reinterpret_cast<PoolWaitFn>(image+kPoolWait)(pool);                     // 等每个 worker 退出，清 +0x140/+0x154/+0x155
    QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&f);
    ++detaches;
    if(probeOn)LogProbe("so far");
    Log("HAVOKMT reset: queue %p closed and the workers back in %.2f ms (detach #%u)",attached,
        f.QuadPart ? static_cast<double>(t1.QuadPart-t0.QuadPart)*1000.0/static_cast<double>(f.QuadPart) : 0.0,detaches);
}

void BeforeReset(unsigned char* holder) noexcept {
    Detach(holder);
    reinterpret_cast<ResetFn>(image+kReset)(holder);   // 原版复位：释放并重建 pool 与 queue（新 pool 下一帧再接）
}

bool CheckCode() noexcept {
    const CallSite* sites[1+kResetSiteCount]={&kStepSite};
    for(std::size_t i=0;i<kResetSiteCount;++i)sites[1+i]=&kResetSites[i];
    for(const CallSite* s:sites) {
        if(!Matches(s->context,s->stock,sizeof(s->stock))){Log("HAVOKMT refused: unexpected code at %#x",s->context);return false;}
        if(CallTarget(s->call,s->stock+(s->call-s->context))!=s->target){Log("HAVOKMT refused: %#x does not call %#x",s->call,s->target);return false;}
    }
    for(const Fingerprint& f:kFingerprints)
        if(!Matches(f.rva,f.bytes,f.size)){Log("HAVOKMT refused: unexpected code at %#x",f.rva);return false;}
    struct Slot { std::uint32_t vtable; std::size_t slot; std::uint32_t fn; };
    constexpr Slot kSlots[]={{kPoolVtable,kPoolProcessSlot,kPoolProcess},{kPoolVtable,kPoolWaitSlot,kPoolWait},
                             {kPoolVtable,kPoolSetThreadsSlot,kSetNumThreads},{kQueueSubVtable,kQueueCloseSlot,kQueueClose}};
    for(const Slot& s:kSlots)
        if(At<const void*>(image,s.vtable+s.slot*8)!=image+s.fn){Log("HAVOKMT refused: vtable %#x slot %u is not %#x",s.vtable,static_cast<unsigned>(s.slot),s.fn);return false;}
    return true;
}

// 改回原来的 E8 rel32（RedirectCall 写进去的是指向近处 thunk 的 rel32），再释放那页 thunk（AllocateNearThunk 每个
// thunk 单独一次分配）。只在安装失败时、同一次安装里刚改完就调：此时游戏线程还没跑到这些调用点。
void Restore(const CallSite& s) noexcept {
    unsigned char now[5];
    std::memcpy(now,image+s.call,sizeof(now));
    std::int32_t rel=0;
    std::memcpy(&rel,now+1,sizeof(rel));
    void* thunk=image+s.call+5+rel;
    if(edf::PatchCode(image+s.call,now,s.stock+(s.call-s.context),sizeof(now)) && thunk!=image+s.target)
        VirtualFree(thunk,0,MEM_RELEASE);
}
}  // namespace

bool InstallHavokMultithread() noexcept {
    if(!Cfg().havokMultithread){Log("HAVOKMT off (ini HavokMultithread=0): the physics step stays on the game thread");return false;}
    __try {
        if(!CheckCode())return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("HAVOKMT refused: code unreadable");return false;}
    // 先装复位保护，最后装每帧接队列：任何时刻只要 pool 可能接着队列，复位前就一定会 close + 等待。
    const CallSite* done[1+kResetSiteCount]{};
    std::size_t count=0;
    bool ok=true;
    for(std::size_t i=0;i<kResetSiteCount && ok;++i) {
        bool changed=false;
        ok=RedirectCall(image+kResetSites[i].call,image+kReset,reinterpret_cast<void*>(&BeforeReset),changed);
        if(changed)done[count++]=&kResetSites[i];
    }
    if(ok) {
        bool changed=false;
        ok=RedirectCall(image+kStepSite.call,image+kStepNop,reinterpret_cast<void*>(&BeforeStep),changed);
        if(changed)done[count++]=&kStepSite;
    }
    if(!ok) {
        for(std::size_t i=count;i-->0;)Restore(*done[i]);
        Log("HAVOKMT refused: a call site could not be redirected (all put back): the physics step stays on the game thread");
        return false;
    }
    Log("HAVOKMT on: the game's worker pool processes the physics task queue from the next step (step %#x, resets %#x %#x)",
        kStepSite.call,kResetSites[0].call,kResetSites[1].call);
    if(Cfg().debug)InstallProbe();
    return true;
}
}  // namespace crew
