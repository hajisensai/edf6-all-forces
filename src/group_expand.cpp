// The broadphase collectors' group expansion, walked once per group in a bullet's cast (group_expand.h,
// docs/group-expand-re.md).
#include "group_expand.h"
#include "group_expand_logic.h"
#include "config.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <new>

namespace crew {
namespace {
using namespace group_expand;
using AppendFn=void(__fastcall*)(void*,unsigned);
using FilterFn=bool(__fastcall*)(void*,unsigned*);
using CastFn=std::uintptr_t(__fastcall*)(void*,void*,void*,void*,void*);

struct Fingerprint { unsigned begin,end; std::uint64_t fnv; };
constexpr Fingerprint kFingerprints[]{{kAddBody,kAddBodyEnd,kAddBodyFnv},{kAppend,kAppendEnd,kAppendFnv}};

// One thread's casts: their depth (a cast inside a cast's callback starts a memo of its own), the memo, the counts.
struct Thread {
    int depth=0;
    unsigned shadowTick=0;
    Stats stats;
    Memo memo;
};
thread_local Thread* self=nullptr;
bool counting=false,shadowing=false;
double tickMs=0;
// Totals since the last report: the threads' casts add theirs at the end of each; the walks outside a cast here.
struct Totals {
    std::atomic<unsigned long long> casts{0},castTicks{0},walks{0},nodes{0},walkedIds{0},copies{0},copiedIds{0},outside{0},
        filtered{0},full{0},shadowed{0},mismatches{0};
} totals;
std::atomic<unsigned> loggedMismatches{0};

bool NativeCodeMatches(unsigned begin,unsigned end,std::uint64_t expected) noexcept {
    const auto* const code=image+begin;
    const unsigned bytes=end-begin;
    if(!edf::Readable(code,bytes))return false;
    std::uint64_t fingerprint=14695981039346656037ull;
    for(unsigned i=0;i<bytes;++i)fingerprint=(fingerprint^code[i])*1099511628211ull;
    return fingerprint==expected;
}

// The group's active bodies' ids in list order, each to `out`: stock's walk (the same fields of the same nodes).
// Nothing `out` does (the collector's list) touches the group.
template<class Out> void Walk(const unsigned char* group,unsigned& nodes,Out&& out) {
    const auto* const head=Rd<const unsigned char*>(group,kGroupList);
    for(const auto* node=Rd<const unsigned char*>(head,0);node!=head;node=Rd<const unsigned char*>(node,0)) {
        ++nodes;
        if(node[kNodeOn])out(Rd<unsigned>(Rd<const unsigned char*>(node,kNodeBody),kBodyId));
    }
}

// `n` ids to the collector's list as `n` appends (0x11A9EC0) would put them: with the room for all of them (no append
// would grow the list) one copy and the count; else each through the append (its growth as stock's, one at a time).
void AppendIds(unsigned char* collector,const unsigned* ids,unsigned n) {
    if(!n)return;
    auto* const list=Rd<unsigned*>(collector,kCollectorList);
    const int count=Rd<int>(collector,kCollectorCount);
    const int capacity=static_cast<int>(Rd<unsigned>(collector,kCollectorCapacity)&kCapacityMask);
    if(list && count>=0 && static_cast<long long>(capacity)>=static_cast<long long>(count)+n) {
        std::memcpy(list+count,ids,static_cast<std::size_t>(n)*sizeof(unsigned));
        const int now=count+static_cast<int>(n);
        std::memcpy(collector+kCollectorCount,&now,sizeof(now));
        return;
    }
    const auto append=reinterpret_cast<AppendFn>(image+kAppend);
    for(unsigned i=0;i<n;++i)append(collector,ids[i]);
}

// A walk's ids gathered 256 at a time and appended in order; `keep` (the memo's room, if any) gets the first `room`.
struct Gather {
    unsigned char* collector;
    unsigned* keep;
    unsigned room;
    unsigned n=0,pending=0;
    unsigned chunk[256];
    void operator()(unsigned v) {
        if(n<room)keep[n]=v;
        ++n;
        chunk[pending++]=v;
        if(pending==256){AppendIds(collector,chunk,pending);pending=0;}
    }
    void Done() { AppendIds(collector,chunk,pending);pending=0; }
};

void Flush(Stats& s) noexcept {
    totals.casts.fetch_add(s.casts,std::memory_order_relaxed);totals.castTicks.fetch_add(s.castTicks,std::memory_order_relaxed);
    totals.walks.fetch_add(s.walks,std::memory_order_relaxed);totals.nodes.fetch_add(s.nodes,std::memory_order_relaxed);
    totals.walkedIds.fetch_add(s.walkedIds,std::memory_order_relaxed);totals.copies.fetch_add(s.copies,std::memory_order_relaxed);
    totals.copiedIds.fetch_add(s.copiedIds,std::memory_order_relaxed);totals.full.fetch_add(s.full,std::memory_order_relaxed);
    totals.shadowed.fetch_add(s.shadowed,std::memory_order_relaxed);totals.mismatches.fetch_add(s.mismatches,std::memory_order_relaxed);
    s=Stats{};
}

// The cave's call, for stock's walk at 0x109F6F: (the collector, the group, the overlapped body's id). Returns to
// addBody's epilogue. Not noexcept: an exception out of the filter functor or the append goes on (the cave has unwind
// info).
void __fastcall Expand(unsigned char* collector,const unsigned char* group,unsigned id) {
    if(void* const functor=Rd<void*>(collector,kCollectorFilter)) {
        // Stock's loop as it is: the functor (it may keep state) asked for each active node, with the overlapped id.
        const auto append=reinterpret_cast<AppendFn>(image+kAppend);
        const auto filter=reinterpret_cast<FilterFn>(Rd<void* const*>(functor,0)[2]);
        const auto* const head=Rd<const unsigned char*>(group,kGroupList);
        for(const auto* node=Rd<const unsigned char*>(head,0);node!=head;node=Rd<const unsigned char*>(node,0)) {
            if(!node[kNodeOn])continue;
            unsigned arg=id;
            if(filter(functor,&arg))append(collector,Rd<unsigned>(Rd<const unsigned char*>(node,kNodeBody),kBodyId));
        }
        if(counting){totals.outside.fetch_add(1,std::memory_order_relaxed);totals.filtered.fetch_add(1,std::memory_order_relaxed);}
        return;
    }
    Thread* const t=self;
    if(!t || t->depth<=0) {
        unsigned nodes=0;
        Gather out{collector,nullptr,0};
        Walk(group,nodes,out);
        out.Done();
        if(counting)totals.outside.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    Stats& s=t->stats;
    if(const Memo::Span* const span=t->memo.Find(group)) {
        const Memo::Span copy=*span;
        const unsigned* const ids=t->memo.Ids(copy);
        if(shadowing && (++t->shadowTick&63)==0) {
            unsigned n=0,nodes=0;
            bool same=true;
            Walk(group,nodes,[&](unsigned v){ if(n>=copy.count || ids[n]!=v)same=false; ++n; });
            ++s.shadowed;
            if(n!=copy.count)same=false;
            if(!same) {
                ++s.mismatches;
                if(loggedMismatches.fetch_add(1,std::memory_order_relaxed)<32)
                    Log("GROUPEXPAND MISMATCH group %p: memo %u ids, walk %u",static_cast<const void*>(group),copy.count,n);
                nodes=0;
                Gather out{collector,nullptr,0};
                Walk(group,nodes,out);   // the walk's ids, as stock
                out.Done();
                return;
            }
        }
        AppendIds(collector,ids,copy.count);
        ++s.copies;s.copiedIds+=copy.count;
        return;
    }
    unsigned room=0,nodes=0;
    unsigned* const keep=t->memo.Room(room);
    Gather out{collector,keep,room};
    Walk(group,nodes,out);
    out.Done();
    if(keep)t->memo.Commit(group,out.n);   // more than the room: Commit marks the memo full, keeps nothing
    ++s.walks;s.nodes+=nodes;s.walkedIds+=out.n;
}

// The bullet step's casts (rcx its collector): one memo for the cast's overlaps. Not noexcept, as Expand.
std::uintptr_t __fastcall BulletCast(void* collector,void* world,void* from,void* to,void* shape) {
    const auto cast=reinterpret_cast<CastFn>(image+kCast);
    Thread* t=self;
    if(!t)self=t=new(std::nothrow) Thread;
    if(!t)return cast(collector,world,from,to,shape);
    struct Scope {
        Thread* t;
        explicit Scope(Thread* at) noexcept : t(at) { t->memo.Begin();++t->depth; }
        ~Scope() {
            --t->depth;
            if(t->memo.Full())++t->stats.full;
            t->memo.Begin();   // the outer cast's (a cast inside a callback) walks again
            ++t->stats.casts;
            if(counting)Flush(t->stats);
        }
    };
    LARGE_INTEGER a{};
    if(counting)QueryPerformanceCounter(&a);
    std::uintptr_t r;
    {
        const Scope scope(t);
        r=cast(collector,world,from,to,shape);
        if(counting){LARGE_INTEGER b;QueryPerformanceCounter(&b);t->stats.castTicks+=static_cast<unsigned long long>(b.QuadPart-a.QuadPart);}
    }
    return r;
}

// `mov rcx,rbp; mov rdx,rdi; mov r8d,esi; call [rip+11h] (Expand); mov rax,image+0x109FED; jmp rax`: in addBody's frame
// as it is at the walk (rsp 16-aligned, the callee home slots [rsp, rsp+20h) its own). The address at 32, the unwind
// info (addBody's codes at offset 0, no prolog: the unwinder skips a code whose offset is past the pc) at 40, the function table entry at 64. Not a jmp [rip] / rel32 jmp after the call:
// the unwinder reads those as an epilogue.
unsigned char* MakeWalkCave() noexcept {
    unsigned char page[80]={};
    unsigned char code[]={0x48,0x8B,0xCD,0x48,0x8B,0xD7,0x44,0x8B,0xC6,0xFF,0x15,0x11,0,0,0,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    static_assert(sizeof(code)<=32,"the cave's code fits before its target");
    const auto resume=reinterpret_cast<std::uintptr_t>(image+kResume);
    std::memcpy(code+17,&resume,sizeof(resume));
    std::memcpy(page,code,sizeof(code));
    const auto target=reinterpret_cast<std::uintptr_t>(&Expand);
    std::memcpy(page+32,&target,sizeof(target));
    std::memcpy(page+40,kCaveUnwind,sizeof(kCaveUnwind));
    const RUNTIME_FUNCTION entry{0,static_cast<DWORD>(sizeof(code)),40};
    std::memcpy(page+64,&entry,sizeof(entry));
    auto* const at=static_cast<unsigned char*>(edf::AllocateNearCode(image+kWalk,page,sizeof(page)));
    if(!at)return nullptr;
    if(!RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(at+64),1,reinterpret_cast<DWORD64>(at))) {
        VirtualFree(at,0,MEM_RELEASE);
        return nullptr;
    }
    return at;
}

struct Done { unsigned rva; std::size_t size; unsigned char stock[sizeof(kWalkStock)]; };
bool Undo(const Done* done,unsigned n) noexcept {
    bool ok=true;
    while(n) {
        const Done& d=done[--n];
        unsigned char now[sizeof(kWalkStock)];
        std::memcpy(now,image+d.rva,d.size);
        ok=edf::PatchCode(image+d.rva,now,d.stock,d.size) && ok;
    }
    return ok;
}
}  // namespace

bool InstallGroupExpand() noexcept {
    if(!image)return false;
    if(!Cfg().groupExpandMemo){Log("GROUPEXPAND off (ini GroupExpandMemo=0)");return false;}
    for(const auto& f:kFingerprints)
        if(!NativeCodeMatches(f.begin,f.end,f.fnv)){Log("GROUPEXPAND %X..%X not the stock code: off",f.begin,f.end);return false;}
    if(!edf::Matches(image,kAddBodyUnwind,kAddBodyUnwindStock,sizeof(kAddBodyUnwindStock))){Log("GROUPEXPAND addBody unwind info not the stock: off");return false;}
    counting=Cfg().perfProbe || Cfg().debug;
    shadowing=Cfg().debug;
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);tickMs=1000.0/static_cast<double>(f.QuadPart);
    unsigned char* const cave=MakeWalkCave();
    if(!cave){Log("GROUPEXPAND no cave page: off");return false;}
    Done done[4]{};
    unsigned n=0;
    // The walk first (the same ids for every collector), the bullet casts' scope after.
    {
        Done& d=done[n];
        d.rva=kWalk;d.size=sizeof(kWalkStock);std::memcpy(d.stock,kWalkStock,sizeof(kWalkStock));
        if(edf::RedirectToThunk(image+kWalk,kWalkStock,sizeof(kWalkStock),cave,edf::Branch::kJump))++n;
    }
    bool ok=n==1;
    for(const unsigned site:kCastCalls) {
        if(!ok)break;
        Done& d=done[n];
        d.rva=site;d.size=5;std::memcpy(d.stock,image+site,5);
        bool changed=false;
        ok=edf::RedirectCall(image+site,image+kCast,reinterpret_cast<void*>(&BulletCast),changed) && changed;
        if(ok)++n;
    }
    if(ok){Log("GROUPEXPAND group ids appended in bulk, each group walked once in a bullet's cast%s",shadowing ? " (shadow check every 64th copy)" : "");return true;}
    const bool undone=Undo(done,n);
    Log("GROUPEXPAND hooks not all in (%u): off%s",n,undone ? "" : ", NOT all put back");
    return false;
}

void ReportGroupExpand(unsigned frames) noexcept {
    if(!counting)return;
    const auto take=[](std::atomic<unsigned long long>& v) noexcept { return v.exchange(0,std::memory_order_relaxed); };
    const unsigned long long casts=take(totals.casts),ticks=take(totals.castTicks),walks=take(totals.walks),nodes=take(totals.nodes),
        walked=take(totals.walkedIds),copies=take(totals.copies),copied=take(totals.copiedIds),outside=take(totals.outside),
        filtered=take(totals.filtered),full=take(totals.full),shadowed=take(totals.shadowed),mismatches=take(totals.mismatches);
    if(!Cfg().perfProbe || !(casts+outside))return;
    const double n=frames ? frames : 1.0;
    Log("PERF GROUPEXPAND %u frames: %llu casts (%.1f a frame, %.3f ms, %.3f a frame); %llu walks (%llu nodes, %llu ids), "
        "%llu copies (%llu ids), %.2f expansions a walk; %llu outside casts (%llu filtered); %llu full; shadow %llu checked %llu mismatched",
        frames,casts,static_cast<double>(casts)/n,static_cast<double>(ticks)*tickMs,static_cast<double>(ticks)*tickMs/n,walks,nodes,walked,
        copies,copied,walks ? static_cast<double>(walks+copies)/static_cast<double>(walks) : 0.0,outside,filtered,full,shadowed,mismatches);
}
}  // namespace crew
