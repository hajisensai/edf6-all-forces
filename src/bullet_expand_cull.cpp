// The bullets' broadphase group expansion cut to the parts the sweep can reach (bullet_expand_cull.h,
// docs/bullet-expand-cull-re.md): the cave at addBody's walk 0x109F6F, the bullet step's three cast calls, the batch
// step's call (the box cache's generation) and, with debug, the step-3 build's call (the shadow check).
#include "bullet_expand_cull.h"
#include "bullet_expand_cull_logic.h"
#include "config.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace crew {
namespace {
using namespace expand_cull;
using AppendFn=void(__fastcall*)(void*,unsigned);
using FilterFn=bool(__fastcall*)(void*,unsigned*);
using CastFn=std::uintptr_t(__fastcall*)(void*,void*,void*,void*,void*);
using StepFn=std::uintptr_t(__fastcall*)(void*,void*);
using BuildFn=std::uint64_t(__fastcall*)(unsigned char*,unsigned char*);
using BatchFn=void(__fastcall*)(void*,void*,unsigned);
using ValidFn=int(__fastcall*)(void*,unsigned);
using BodyFn=const unsigned char*(__fastcall*)(void*,unsigned);
using AabbFn=void(__fastcall*)(const void*,const void*,float*);
using HitsFn=const unsigned char*(__fastcall*)(void*);

bool counting=false,shadowing=false;
std::atomic<unsigned> stepGen{1};   // the batch step's calls (0 never a generation)
LARGE_INTEGER qpc0{},qpcFreq{};
unsigned long long tsc0=0;

// The world's body reader as the cast sees it, and the shape types' calcAabb table.
struct Bodies {
    void* reader=nullptr;
    ValidFn valid=nullptr;
    BodyFn body=nullptr;
    const unsigned char* table=nullptr;
};
// The bullet cast running on this thread (the innermost).
struct Cast {
    unsigned char* collector;
    Bodies bodies;
    Sweep sweep;
    bool cut;   // false: left whole (ExpandCull=0, a blast, no world, a shadow sample)
};
// A shadow sample: a cast left whole, its sweep for the core's step-3 build this step.
struct Sample { const unsigned char* core; unsigned gen; Bodies bodies; Sweep sweep; };

struct Thread {
    const Cast* cast=nullptr;
    BoxCache cache;
    Stats stats;
    unsigned tick=0,nextSample=0;
    Sample samples[16]{};
    explicit Thread(BoxEntry* storage) noexcept : cache(storage) {}
};
thread_local Thread* self=nullptr;

Thread* Self() noexcept {
    if(self)return self;
    void* const storage=VirtualAlloc(nullptr,sizeof(BoxEntry)*BoxCache::kSlots,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!storage)return nullptr;
    Thread* const t=new(std::nothrow) Thread(static_cast<BoxEntry*>(storage));
    if(!t){VirtualFree(storage,0,MEM_RELEASE);return nullptr;}
    return self=t;
}

struct Totals {
    std::atomic<unsigned long long> casts{0},culling{0},blast{0},off{0},walks{0},parts{0},kept{0},culled{0},keptOne{0},
        lookups{0},misses{0},unbounded{0},walkTicks{0},boxTicks{0},outside{0},filtered{0},sampled{0},shadowed{0},mismatches{0},
        unreadDiffs{0};
    std::atomic<unsigned> unreadMask{0};
} totals;
std::atomic<unsigned> loggedMismatches{0},loggedUnread{0};

void Flush(Stats& s) noexcept {
    const auto add=[](std::atomic<unsigned long long>& to,unsigned long long v) noexcept { if(v)to.fetch_add(v,std::memory_order_relaxed); };
    add(totals.casts,s.casts);add(totals.culling,s.culling);add(totals.blast,s.blast);add(totals.off,s.off);
    add(totals.walks,s.walks);add(totals.parts,s.parts);add(totals.kept,s.kept);add(totals.culled,s.culled);
    add(totals.keptOne,s.keptOne);add(totals.lookups,s.lookups);add(totals.misses,s.misses);add(totals.unbounded,s.unbounded);
    add(totals.walkTicks,s.walkTicks);add(totals.boxTicks,s.boxTicks);add(totals.outside,s.outside);
    add(totals.filtered,s.filtered);add(totals.sampled,s.sampled);add(totals.shadowed,s.shadowed);
    add(totals.mismatches,s.mismatches);add(totals.unreadDiffs,s.unreadDiffs);
    if(s.unreadMask)totals.unreadMask.fetch_or(s.unreadMask,std::memory_order_relaxed);
    s=Stats{};
}

bool NativeCodeMatches(const Fingerprint& f) noexcept {
    const auto* const code=image+f.begin;
    const unsigned bytes=f.end-f.begin;
    if(!edf::Readable(code,bytes))return false;
    std::uint64_t fingerprint=14695981039346656037ull;
    for(unsigned i=0;i<bytes;++i)fingerprint=(fingerprint^code[i])*1099511628211ull;
    return fingerprint==f.fnv;
}

// The body's world box as the narrowphase sees it: its shape (+0x78) in its transform, through the shape type's
// calcAabb. False (unbounded: kept) when the id is not a live body (the reader's own check, as the filter 0x1079D0
// asks it before the fetch), or the shape, its calcAabb or the box is missing / not a box.
bool ComputeBox(const Bodies& b,unsigned id,float lo[3],float hi[3]) noexcept {
    if(b.valid(b.reader,id)==0)return false;
    const unsigned char* const body=b.body(b.reader,id);
    if(!body)return false;
    const auto* const shape=Rd<const unsigned char*>(body,kBodyShape);
    if(!shape)return false;
    const auto aabb=Rd<AabbFn>(b.table,static_cast<std::size_t>(shape[kShapeType])*kAabbStride+kAabbEntry);
    if(!aabb)return false;
    alignas(16) float box[8]{};
    aabb(shape,body,box);
    for(int k=0;k<3;++k) {
        lo[k]=box[k];hi[k]=box[4+k];
        if(!std::isfinite(lo[k]) || !std::isfinite(hi[k]) || lo[k]>hi[k])return false;
    }
    return true;
}

// A slot filled with the id's box this step (a miss: once a body a step on a thread), counted.
__declspec(noinline) const BoxEntry& FillBox(Thread& t,const Bodies& b,BoxEntry& e,unsigned id,unsigned gen) noexcept {
    Stats& s=t.stats;
    const unsigned long long a=counting ? __rdtsc() : 0;
    const bool ok=ComputeBox(b,id,e.lo,e.hi);
    if(counting)s.boxTicks+=__rdtsc()-a;
    ++s.misses;
    if(!ok)++s.unbounded;
    BoxCache::Fill(e,id,gen,ok);
    return e;
}
// The id's box this step (cached on this thread). The lookups are counted by the callers (a walk's parts at once).
inline const BoxEntry& BoxOf(Thread& t,const Bodies& b,unsigned id,unsigned gen) noexcept {
    BoxEntry& e=t.cache.Slot(id);
    return BoxCache::Current(e,id,gen) ? e : FillBox(t,b,e,id,gen);
}

// `n` ids to the collector's list as `n` appends (0x11A9EC0) would put them: with the room for all (no append would
// grow the list) one copy and the count; else each through the append (its growth as stock's).
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

// A walk's ids gathered 256 at a time and appended in order.
struct Gather {
    unsigned char* collector;
    unsigned pending=0;
    unsigned chunk[256];
    void operator()(unsigned v) {
        chunk[pending++]=v;
        if(pending==256){AppendIds(collector,chunk,pending);pending=0;}
    }
    void Done() { AppendIds(collector,chunk,pending);pending=0; }
};

// The group's active parts' ids in list order, each to `out` (stock's walk: the same fields of the same nodes).
template<class Out> void Walk(const unsigned char* group,Out&& out) {
    const auto* const head=Rd<const unsigned char*>(group,kGroupList);
    for(const auto* node=Rd<const unsigned char*>(head,0);node!=head;node=Rd<const unsigned char*>(node,0))
        if(node[kNodeOn])out(Rd<unsigned>(Rd<const unsigned char*>(node,kNodeBody),kBodyId));
}

// The cave's call, for stock's walk at 0x109F6F: (the collector, the group, the overlapped body's id). Returns to
// addBody's epilogue. Not noexcept: an exception out of the functor or the append goes on (the cave has unwind info).
void __fastcall Expand(unsigned char* collector,const unsigned char* group,unsigned id) {
    Thread* const t=self;
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
        if(t)++t->stats.filtered;
        return;
    }
    const Cast* const c=t ? t->cast : nullptr;
    if(!c || c->collector!=collector || !c->cut) {
        Gather out{collector};
        Walk(group,out);
        out.Done();
        if(t && (!c || c->collector!=collector))++t->stats.outside;
        return;
    }
    Stats& s=t->stats;
    const unsigned long long t0=counting ? __rdtsc() : 0;
    const unsigned gen=stepGen.load(std::memory_order_relaxed);
    Gather out{collector};
    unsigned first=0,parts=0,kept=0;
    Walk(group,[&](unsigned v) {
        if(!parts++)first=v;
        const BoxEntry& b=BoxOf(*t,c->bodies,v,gen);
        if(c->sweep.Touches(b.lo,b.hi)){out(v);++kept;}
    });
    out.Done();
    ++s.walks;s.parts+=parts;s.kept+=kept;s.culled+=parts-kept;s.lookups+=parts;
    // Stock would have put parts in the list; an empty list takes the step another way (no cast, b80 = to).
    if(parts && !kept && Rd<int>(collector,kCollectorCount)==0){AppendIds(collector,&first,1);++s.keptOne;}
    if(counting)s.walkTicks+=__rdtsc()-t0;
}

// The world's reader and the calcAabb table, or false.
bool BodiesOf(void* world,Bodies& b) noexcept {
    if(!world)return false;
    auto* const hknp=Rd<unsigned char*>(world,kWorldHknp);
    const auto* const table=Rd<const unsigned char*>(image,kAabbTable);
    if(!hknp || !table)return false;
    b.reader=hknp+kHknpReader;
    const auto* const vt=Rd<void* const*>(b.reader,0);
    if(!vt)return false;
    b.valid=reinterpret_cast<ValidFn>(vt[kReaderValidSlot]);
    b.body=reinterpret_cast<BodyFn>(vt[kReaderBodySlot]);
    b.table=table;
    return b.valid && b.body;
}

// The bullet step's casts (rcx its collector, core+0x800; rdx the world; r8 / r9 from / to; the 5th the extent).
// Not noexcept, as Expand: the scope puts the outer cast back.
std::uintptr_t __fastcall BulletCast(unsigned char* collector,void* world,float* from,float* to,float* extent) {
    const auto cast=reinterpret_cast<CastFn>(image+kCast);
    Thread* const t=Self();
    if(!t)return cast(collector,world,from,to,extent);
    Stats& s=t->stats;
    ++s.casts;
    Cast c{};
    c.collector=collector;
    const unsigned char* const core=collector-kCollectorCore;
    if(!Cfg().expandCull || !from || !to || !extent || !BodiesOf(world,c.bodies) || !c.sweep.Set(from,to,extent))++s.off;
    else if(Rd<unsigned>(core,kCoreFlags)&kFlagBlast)++s.blast;
    else if(shadowing && (++t->tick&255)==0) {
        Sample& m=t->samples[t->nextSample++%(sizeof(t->samples)/sizeof(t->samples[0]))];
        m={core,stepGen.load(std::memory_order_relaxed),c.bodies,c.sweep};
        ++s.sampled;
    } else {
        c.cut=true;
        ++s.culling;
    }
    struct Scope {
        Thread* t;
        const Cast* prev;
        ~Scope() { t->cast=prev;if(counting)Flush(t->stats); }
    } scope{t,t->cast};
    t->cast=&c;
    return cast(collector,world,from,to,extent);
}

// The batch step (0x237CF0 from 0x704ED4): a new generation of body boxes for its casts.
std::uintptr_t __fastcall Step(void* a,void* b) {
    unsigned g=stepGen.load(std::memory_order_relaxed)+1;
    if(!g)g=1;
    stepGen.store(g,std::memory_order_relaxed);
    return reinterpret_cast<StepFn>(image+kStep)(a,b);
}

// ---- the debug shadow check (the step-3 build's call 0x2382B6) ----
// The collector's hits after a cast: (count, then count * 0x70 bytes).
void TakeHits(unsigned char* core,std::vector<unsigned char>& out) {
    auto* const collector=Rd<unsigned char*>(core,kCoreHits);
    const int count=Rd<int>(collector,kHitCount);
    const unsigned n=count>0 ? static_cast<unsigned>(count) : 0u;
    out.assign(sizeof(n),0);
    std::memcpy(out.data(),&n,sizeof(n));
    if(!n)return;
    const auto hits=reinterpret_cast<HitsFn>(Rd<void* const*>(collector,0)[kHitsSlot])(collector);
    out.insert(out.end(),hits,hits+static_cast<std::size_t>(n)*kHitBytes);
}

// The narrowphase on `ids` (the core's list for the time), its hits into `out`; the command as it was after.
void CastOn(unsigned char* core,unsigned char* cmd,const unsigned* ids,unsigned n,const unsigned char* saved,
            std::vector<unsigned char>& out) {
    std::memcpy(Rd<unsigned*>(core,kCoreIds),ids,static_cast<std::size_t>(n)*sizeof(unsigned));
    std::memcpy(core+kCoreCount,&n,sizeof(n));
    out.clear();
    if(reinterpret_cast<BuildFn>(image+kBuild)(core,cmd)) {
        reinterpret_cast<BatchFn>(image+kShapeBatch)(Rd<unsigned char*>(image,kSystem)+0x10,cmd,1);
        TakeHits(core,out);
    }
    std::memcpy(cmd,saved,kCommandBodies);
    std::memcpy(cmd+kCommandBodiesEnd,saved+kCommandBodiesEnd,kCommandBytes-kCommandBodiesEnd);
}

bool ReadDword(unsigned dw) noexcept {
    for(const std::size_t r:kHitReadDwords)if(r==dw)return true;
    return false;
}

// The sampled core's step-3 list whole and cut by the sample's sweep, the narrowphase on each; the hits' read fields
// compared (a difference: EXPANDCULL MISMATCH), the unread dwords that differ noted. The whole list stays the core's.
void ShadowBuild(Thread& t,const Sample& m,unsigned char* core,unsigned char* cmd) {
    const unsigned n=Rd<unsigned>(core,kCoreCount);
    unsigned* const ids=Rd<unsigned*>(core,kCoreIds);
    if(!n || !ids)return;
    thread_local std::vector<unsigned> whole,cut;
    thread_local std::vector<unsigned char> a,b;
    whole.assign(ids,ids+n);
    cut.clear();
    const unsigned gen=stepGen.load(std::memory_order_relaxed);
    for(const unsigned id:whole) {
        const BoxEntry& e=BoxOf(t,m.bodies,id,gen);
        if(m.sweep.Touches(e.lo,e.hi))cut.push_back(id);
    }
    t.stats.lookups+=n;
    if(cut.empty())cut.push_back(whole[0]);
    if(cut.size()==whole.size())return;   // nothing the cut would take out of this list
    unsigned char saved[kCommandBytes];
    std::memcpy(saved,cmd,sizeof(saved));
    CastOn(core,cmd,whole.data(),n,saved,a);
    CastOn(core,cmd,cut.data(),static_cast<unsigned>(cut.size()),saved,b);
    std::memcpy(ids,whole.data(),static_cast<std::size_t>(n)*sizeof(unsigned));
    std::memcpy(core+kCoreCount,&n,sizeof(n));
    Stats& s=t.stats;
    ++s.shadowed;
    const auto countOf=[](const std::vector<unsigned char>& v) noexcept { unsigned c=0;if(v.size()>=sizeof(c))std::memcpy(&c,v.data(),sizeof(c));return c; };
    const unsigned ha=countOf(a),hb=countOf(b);
    const auto dword=[](const std::vector<unsigned char>& v,unsigned hit,unsigned dw) noexcept {
        unsigned x=0;std::memcpy(&x,v.data()+4+hit*kHitBytes+dw*4,sizeof(x));return x; };
    if(ha!=hb) {
        ++s.mismatches;
        if(loggedMismatches.fetch_add(1,std::memory_order_relaxed)<32)
            Log("EXPANDCULL MISMATCH core %p: %u parts, %u cut; hits %u whole, %u cut",static_cast<void*>(core),n,
                static_cast<unsigned>(cut.size()),ha,hb);
        return;
    }
    unsigned mask=0;
    for(unsigned h=0;h<ha;++h)
        for(unsigned dw=0;dw<kHitBytes/4;++dw) {
            const unsigned x=dword(a,h,dw),y=dword(b,h,dw);
            if(x==y)continue;
            if(ReadDword(dw)) {
                ++s.mismatches;
                if(loggedMismatches.fetch_add(1,std::memory_order_relaxed)<32)
                    Log("EXPANDCULL MISMATCH core %p: %u parts, %u cut; hit %u of %u, +0x%02X %08X whole, %08X cut",
                        static_cast<void*>(core),n,static_cast<unsigned>(cut.size()),h,ha,dw*4,x,y);
                return;
            }
            mask|=1u<<dw;
        }
    if(!mask)return;
    ++s.unreadDiffs;
    s.unreadMask|=mask;
    if(loggedUnread.fetch_add(1,std::memory_order_relaxed)<8) {
        char line[256];
        int at=0;
        for(unsigned dw=0;dw<kHitBytes/4 && at<200;++dw)
            if(mask&(1u<<dw))at+=std::snprintf(line+at,sizeof(line)-static_cast<std::size_t>(at)," +0x%02X %08X/%08X",dw*4,dword(a,0,dw),dword(b,0,dw));
        line[at<0 ? 0 : at]=0;
        Log("EXPANDCULL UNREAD core %p: %u hits the same in the read fields; unread dwords differ (hit 0 whole/cut):%s",
            static_cast<void*>(core),ha,line);
    }
}

// The batch's step-3 build call (debug only). Not noexcept: the build's and the casts' exceptions go on.
std::uint64_t __fastcall Build(unsigned char* core,unsigned char* cmd) {
    if(Thread* const t=self) {
        const unsigned gen=stepGen.load(std::memory_order_relaxed);
        for(Sample& m:t->samples)
            if(m.core==core && m.gen==gen) {
                const Sample copy=m;
                m.core=nullptr;
                ShadowBuild(*t,copy,core,cmd);
                if(counting)Flush(t->stats);
                break;
            }
    }
    return reinterpret_cast<BuildFn>(image+kBuild)(core,cmd);
}

// `mov rcx,rbp; mov rdx,rdi; mov r8d,esi; call [rip+11h] (Expand); mov rax,image+0x109FED; jmp rax`: in addBody's frame
// as it is at the walk. The address at 32, the unwind info at 40, the function table entry at 64.
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

// A rel32 call at `site` to `target`.
bool CallsTo(unsigned site,unsigned target) noexcept {
    const auto* const p=image+site;
    return edf::Readable(p,5) && p[0]==0xE8 && static_cast<long long>(site)+5+Rd<std::int32_t>(p,1)==static_cast<long long>(target);
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

bool InstallBulletExpandCull() noexcept {
    if(!image)return false;
    for(const auto& f:kFingerprints)
        if(!NativeCodeMatches(f)){Log("EXPANDCULL REFUSED: %X..%X not the stock code, nothing installed",f.begin,f.end);return false;}
    if(!edf::Matches(image,kAddBodyUnwind,kAddBodyUnwindStock,sizeof(kAddBodyUnwindStock)) ||
       !edf::Matches(image,kWalk,kWalkStock,sizeof(kWalkStock))) {
        Log("EXPANDCULL REFUSED: addBody's walk or unwind info not the stock, nothing installed");
        return false;
    }
    bool sites=CallsTo(kStepCall,kStep) && CallsTo(kBuildCall,kBuild);
    for(const unsigned site:kCastCalls)sites=sites && CallsTo(site,kCast);
    if(!sites){Log("EXPANDCULL REFUSED: a call site not the stock call, nothing installed");return false;}
    counting=Cfg().perfProbe!=0;
    shadowing=Cfg().debug;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpc0);
    tsc0=__rdtsc();
    unsigned char* const cave=MakeWalkCave();
    if(!cave){Log("EXPANDCULL REFUSED: no cave page, nothing installed");return false;}
    Done done[6]{};
    unsigned n=0;
    const auto redirect=[&](unsigned site,unsigned target,void* to) noexcept {
        Done& d=done[n];
        d.rva=site;d.size=5;std::memcpy(d.stock,image+site,5);
        bool changed=false;
        if(!edf::RedirectCall(image+site,image+target,to,changed) || !changed)return false;
        ++n;
        return true;
    };
    // The generation first (the cache's steps), the walk, the casts' scope; the shadow's build last.
    bool ok=redirect(kStepCall,kStep,reinterpret_cast<void*>(&Step));
    if(ok) {
        Done& d=done[n];
        d.rva=kWalk;d.size=sizeof(kWalkStock);std::memcpy(d.stock,kWalkStock,sizeof(kWalkStock));
        ok=edf::RedirectToThunk(image+kWalk,kWalkStock,sizeof(kWalkStock),cave,edf::Branch::kJump);
        if(ok)++n;
    }
    for(const unsigned site:kCastCalls)ok=ok && redirect(site,kCast,reinterpret_cast<void*>(&BulletCast));
    if(ok && shadowing)ok=redirect(kBuildCall,kBuild,reinterpret_cast<void*>(&Build));
    if(ok) {
        Log("EXPANDCULL bullets' sweeps put only the parts they can reach in their lists (ini ExpandCull, now %d)%s",
            Cfg().expandCull ? 1 : 0,shadowing ? " (shadow check every 256th cast)" : "");
        return true;
    }
    const bool undone=Undo(done,n);
    Log("EXPANDCULL REFUSED: hooks not all in (%u), %s",n,undone ? "all put back" : "NOT all put back");
    return false;
}

void ReportBulletExpandCull(unsigned frames) noexcept {
    const auto take=[](std::atomic<unsigned long long>& v) noexcept { return v.exchange(0,std::memory_order_relaxed); };
    const unsigned long long casts=take(totals.casts),culling=take(totals.culling),blast=take(totals.blast),off=take(totals.off),
        walks=take(totals.walks),parts=take(totals.parts),kept=take(totals.kept),culled=take(totals.culled),
        keptOne=take(totals.keptOne),lookups=take(totals.lookups),misses=take(totals.misses),unbounded=take(totals.unbounded),
        walkTicks=take(totals.walkTicks),boxTicks=take(totals.boxTicks),outside=take(totals.outside),
        filtered=take(totals.filtered),sampled=take(totals.sampled),shadowed=take(totals.shadowed),
        mismatches=take(totals.mismatches),unreadDiffs=take(totals.unreadDiffs);
    const unsigned mask=totals.unreadMask.exchange(0,std::memory_order_relaxed);
    if(!counting || !Cfg().perfProbe || !(casts+outside+filtered))return;
    // TSC ticks to ms: the counter's rate since the install.
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const unsigned long long tsc=__rdtsc();
    const double seconds=qpcFreq.QuadPart ? static_cast<double>(now.QuadPart-qpc0.QuadPart)/static_cast<double>(qpcFreq.QuadPart) : 0.0;
    const double msPerTick=tsc>tsc0 && seconds>0 ? seconds*1000.0/static_cast<double>(tsc-tsc0) : 0.0;
    const double n=frames ? frames : 1.0;
    const double walkMs=static_cast<double>(walkTicks)*msPerTick,boxMs=static_cast<double>(boxTicks)*msPerTick;
    Log("PERF EXPANDCULL %u frames: %llu casts (%llu cut, %llu blast, %llu off, %llu shadow samples); %llu cut walks, %llu parts, "
        "%llu kept, %llu culled (%.1f%%), %llu kept one; cut %.3f ms (%.3f a frame, %.2f ns a part), of it box computes %.3f ms; "
        "box cache %llu hits %llu misses (%llu unbounded); walks as stock %llu outside casts, %llu filtered; shadow %llu checked "
        "%llu mismatched, %llu with unread dwords differing (mask %08X)",
        frames,casts,culling,blast,off,sampled,walks,parts,kept,culled,parts ? 100.0*static_cast<double>(culled)/static_cast<double>(parts) : 0.0,
        keptOne,walkMs,walkMs/n,parts ? walkMs*1e6/static_cast<double>(parts) : 0.0,boxMs,lookups-(std::min)(lookups,misses),misses,unbounded,outside,filtered,
        shadowed,mismatches,unreadDiffs,mask);
}
}  // namespace crew
