// The bullets' broadphase group expansion cut to the parts the sweep can reach (bullet_expand_cull.h,
// docs/bullet-expand-cull-re.md): the cave at addBody's walk 0x109F6F, the bullet step's three cast calls, the batch
// step's call (the box cache's generation), step 1's damage settle call (chained at the load's end: a new generation
// after a settle with entries) and, with debug, the step-3 build's and the ray build's calls (the shadow checks).
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
using SettleFn=std::uintptr_t(__fastcall*)(unsigned char*,void*,void*,std::uintptr_t);
using ValidFn=int(__fastcall*)(void*,unsigned);
using BodyFn=const unsigned char*(__fastcall*)(void*,unsigned);
using AabbFn=void(__fastcall*)(const void*,const void*,float*);
using HitsFn=const unsigned char*(__fastcall*)(void*);

bool counting=false,shadowing=false,installed=false;
bool flatCopies=true;   // the cut walks off the groups' flat copies (the test turns it off to time the list walk)
// Casts are cut only once the settle is chained (InstallBulletExpandCullSettle): before, a settle would not end the
// boxes' generation.
std::atomic<bool> armed{false};
std::atomic<unsigned> boxGen{1};     // the box cache's generation: up per batch step and per settle with entries (0 never one)
std::atomic<unsigned> stepStart{1};  // the generation the batch step began with
std::atomic<unsigned> stepNo{0};     // the batch steps (a sample's step)
std::atomic<unsigned long long> settleCount{0};   // the settles with entries since the load (a sample's)
SettleFn settleNext=nullptr;
LARGE_INTEGER qpc0{},qpcFreq{};
unsigned long long tsc0=0;

// A new generation (never 0).
unsigned Bump() noexcept {
    unsigned g=boxGen.fetch_add(1,std::memory_order_relaxed)+1;
    if(!g)g=boxGen.fetch_add(1,std::memory_order_relaxed)+1;
    return g;
}

// The world's body reader as the cast sees it, and the shape types' calcAabb table.
struct Bodies {
    void* reader=nullptr;
    ValidFn valid=nullptr;
    BodyFn body=nullptr;
    const unsigned char* table=nullptr;
};
// A shadow sample: a cast left whole, its list (core+0x818) after the cast and, per position of it, whether the cut
// would have kept it (decided at the cast, with the boxes the cut would have used; a position no cut walk put there:
// kept). The step-3 build and the ray build of the same step check the cut lists against the whole ones.
struct Sample {
    const unsigned char* core=nullptr;   // null: free / being recorded
    unsigned step=0;
    unsigned long long settles=0;        // settleCount at the cast
    bool afterSettle=false,rayDone=false,bad=false;
    std::vector<unsigned> list;
    std::vector<unsigned char> keep;
};
// The bullet cast running on this thread (the innermost).
struct Cast {
    unsigned char* collector;
    Bodies bodies;
    Sweep sweep;
    bool cut;                 // false: left whole (ExpandCull=0, a blast, no world, not armed, a shadow sample)
    Sample* record;           // a shadow sample's: the cut's decisions recorded, the list left whole
    unsigned dropped;         // the record's positions the cut would have left out so far
};

constexpr unsigned kSamples=16;
struct Thread {
    Cast* cast=nullptr;
    BoxCache cache;
    Stats stats;
    unsigned tick=0,settleTick=0,nextSample=0;
    bool afterSettle=false;   // a settle with entries since this thread's last bullet cast
    Sample samples[kSamples];
    FlatGroup* flats=nullptr;   // kFlatSlots copies, direct-mapped by the group; null: none (walk the list)
    explicit Thread(BoxEntry* storage) noexcept : cache(storage) {}
};
constexpr unsigned kFlatBits=13,kFlatSlots=1u<<kFlatBits;
thread_local Thread* self=nullptr;

Thread* Self() noexcept {
    if(self)return self;
    void* const storage=VirtualAlloc(nullptr,sizeof(BoxEntry)*BoxCache::kSlots,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!storage)return nullptr;
    Thread* const t=new(std::nothrow) Thread(static_cast<BoxEntry*>(storage));
    if(!t){VirtualFree(storage,0,MEM_RELEASE);return nullptr;}
    t->flats=new(std::nothrow) FlatGroup[kFlatSlots];
    return self=t;
}

struct Totals {
    std::atomic<unsigned long long> casts{0},culling{0},blast{0},off{0},walks{0},parts{0},kept{0},culled{0},keptOne{0},
        lookups{0},misses{0},unbounded{0},walkTicks{0},boxTicks{0},outside{0},filtered{0},sampled{0},shadowed{0},mismatches{0},
        unreadDiffs{0},unarmed{0},rechecked{0},stale{0},settleSampled{0},settleShadowed{0},settleMismatches{0},spanShadowed{0},
        spanMismatches{0},rayShadowed{0},rayMismatches{0},unmatched{0},settleCalls{0},settles{0},flatHits{0},flatBuilds{0},
        flatChanged{0},flatChecked{0},flatMismatches{0};
    std::atomic<unsigned> unreadMask{0};
} totals;
std::atomic<unsigned> loggedMismatches{0},loggedUnread{0},loggedStale{0},loggedFlat{0};

void Flush(Stats& s) noexcept {
    const auto add=[](std::atomic<unsigned long long>& to,unsigned long long v) noexcept { if(v)to.fetch_add(v,std::memory_order_relaxed); };
    add(totals.casts,s.casts);add(totals.culling,s.culling);add(totals.blast,s.blast);add(totals.off,s.off);
    add(totals.walks,s.walks);add(totals.parts,s.parts);add(totals.kept,s.kept);add(totals.culled,s.culled);
    add(totals.keptOne,s.keptOne);add(totals.lookups,s.lookups);add(totals.misses,s.misses);add(totals.unbounded,s.unbounded);
    add(totals.walkTicks,s.walkTicks);add(totals.boxTicks,s.boxTicks);add(totals.outside,s.outside);
    add(totals.filtered,s.filtered);add(totals.sampled,s.sampled);add(totals.shadowed,s.shadowed);
    add(totals.mismatches,s.mismatches);add(totals.unreadDiffs,s.unreadDiffs);
    add(totals.unarmed,s.unarmed);add(totals.rechecked,s.rechecked);add(totals.stale,s.stale);
    add(totals.settleSampled,s.settleSampled);add(totals.settleShadowed,s.settleShadowed);add(totals.settleMismatches,s.settleMismatches);
    add(totals.spanShadowed,s.spanShadowed);add(totals.spanMismatches,s.spanMismatches);
    add(totals.rayShadowed,s.rayShadowed);add(totals.rayMismatches,s.rayMismatches);add(totals.unmatched,s.unmatched);
    add(totals.flatHits,s.flatHits);add(totals.flatBuilds,s.flatBuilds);add(totals.flatChanged,s.flatChanged);
    add(totals.flatChecked,s.flatChecked);add(totals.flatMismatches,s.flatMismatches);
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

// A slot filled with the id's box this generation (a miss), counted. The stale probe: a slot this id filled earlier in
// the same batch step (before a settle's new generation) is compared with the box now (EXPANDCULL STALE: the settle
// changed the body's box, the generation's end was needed).
__declspec(noinline) const BoxEntry& FillBox(Thread& t,const Bodies& b,BoxEntry& e,unsigned id,unsigned gen) noexcept {
    Stats& s=t.stats;
    const bool again=e.id==id && e.gen && e.gen!=gen &&
                     static_cast<int>(e.gen-stepStart.load(std::memory_order_relaxed))>=0;
    const BoxEntry old=e;
    const unsigned long long a=counting ? __rdtsc() : 0;
    const bool ok=ComputeBox(b,id,e.lo,e.hi);
    if(counting)s.boxTicks+=__rdtsc()-a;
    ++s.misses;
    if(!ok)++s.unbounded;
    BoxCache::Fill(e,id,gen,ok);
    if(again) {
        ++s.rechecked;
        if(std::memcmp(old.lo,e.lo,sizeof(e.lo)) || std::memcmp(old.hi,e.hi,sizeof(e.hi))) {
            ++s.stale;
            if(loggedStale.fetch_add(1,std::memory_order_relaxed)<8)
                Log("EXPANDCULL STALE body %08X: its box changed in the step after a damage settle: (%g %g %g)..(%g %g %g) -> "
                    "(%g %g %g)..(%g %g %g)",id,old.lo[0],old.lo[1],old.lo[2],old.hi[0],old.hi[1],old.hi[2],e.lo[0],e.lo[1],
                    e.lo[2],e.hi[0],e.hi[1],e.hi[2]);
        }
    }
    return e;
}
// The id's box this generation (cached on this thread). The lookups are counted by the callers (a walk's parts at once).
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

// The group's flat copy this generation on this thread: the slot's when it is the group's, this generation's, and the
// live list is still its (Same); else made from the live list now (its active parts' boxes the cache's, counted as
// lookups). Null (walk the list) without the table or when the copy could not be allocated.
const FlatGroup* FlatOf(Thread& t,const Bodies& b,const unsigned char* group,unsigned gen) {
    if(!t.flats || !flatCopies)return nullptr;
    Stats& s=t.stats;
    const std::uintptr_t key=reinterpret_cast<std::uintptr_t>(group);
    FlatGroup& f=t.flats[static_cast<std::size_t>((static_cast<unsigned long long>(key>>4)*0x9E3779B97F4A7C15ull)>>(64-kFlatBits))];
    const auto* const head=Rd<const unsigned char*>(group,kGroupList);
    if(f.group==group && f.gen==gen) {
        if(f.Same(head)){++s.flatHits;return &f;}
        ++s.flatChanged;
        if(loggedFlat.fetch_add(1,std::memory_order_relaxed)<8)
            Log("EXPANDCULL FLAT group %p: its list changed in the generation (%u active parts in the copy); copied again",
                static_cast<const void*>(group),f.active);
    }
    ++s.flatBuilds;
    f.group=nullptr;
    try {
        f.nodes.clear();f.bodies.clear();f.on.clear();f.ids.clear();
        thread_local std::vector<float> boxes;
        boxes.clear();
        for(const auto* node=Rd<const unsigned char*>(head,0);node!=head;node=Rd<const unsigned char*>(node,0)) {
            const auto* const body=Rd<const unsigned char*>(node,kNodeBody);
            const unsigned char on=node[kNodeOn];
            f.nodes.push_back(node);f.bodies.push_back(body);f.on.push_back(on);
            if(!on)continue;
            const unsigned id=Rd<unsigned>(body,kBodyId);
            const BoxEntry& e=BoxOf(t,b,id,gen);
            f.ids.push_back(id);
            boxes.insert(boxes.end(),{e.lo[0],e.lo[1],e.lo[2],e.hi[0],e.hi[1],e.hi[2]});
        }
        f.Pack(boxes);
    } catch(const std::bad_alloc&) {
        return nullptr;
    }
    s.lookups+=f.ids.size();
    f.group=group;f.head=head;f.gen=gen;
    return &f;
}

// A shadow sample's walk: the whole group appended (as stock), the cut's decision for each position recorded, the
// keep-one as the cut list would take it (its count: the whole one's less the positions dropped).
void RecordWalk(Thread& t,Cast& c,unsigned char* collector,const unsigned char* group) {
    Sample& m=*c.record;
    const int before=Rd<int>(collector,kCollectorCount);
    if(before<0 || static_cast<std::size_t>(before)<m.keep.size())m.bad=true;
    else m.keep.resize(static_cast<std::size_t>(before),1);
    const unsigned gen=boxGen.load(std::memory_order_relaxed);
    Gather out{collector};
    std::size_t first=m.keep.size();
    unsigned parts=0,kept=0;
    Walk(group,[&](unsigned v) {
        const BoxEntry& b=BoxOf(t,c.bodies,v,gen);
        const bool k=c.sweep.Touches(b.lo,b.hi);
        if(!parts)first=m.keep.size();
        ++parts;kept+=k ? 1u : 0u;
        m.keep.push_back(k ? 1 : 0);
        out(v);
    });
    out.Done();
    t.stats.lookups+=parts;
    // The flat copy's cut against the walk's decisions: the same parts kept, in order.
    if(const FlatGroup* const f=FlatOf(t,c.bodies,group,gen)) {
        ++t.stats.flatChecked;
        std::size_t at=first,i=0;
        bool same=f->active==parts;
        const unsigned cut=f->Cut(c.sweep,[&](unsigned v) {
            while(same && at<m.keep.size() && !m.keep[at]){++at;++i;}
            same=same && at<m.keep.size() && v==f->ids[i];
            ++at;++i;
        });
        if(!same || cut!=kept) {
            ++t.stats.flatMismatches;
            if(loggedFlat.fetch_add(1,std::memory_order_relaxed)<8)
                Log("EXPANDCULL FLAT MISMATCH group %p: the copy's cut kept %u of %u, the walk %u of %u",
                    static_cast<const void*>(group),cut,f->active,kept,parts);
        }
    }
    if(parts && !kept && before-static_cast<int>(c.dropped)==0){m.keep[first]=1;kept=1;}
    c.dropped+=parts-kept;
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
    Cast* const c=t ? t->cast : nullptr;
    if(!c || c->collector!=collector || !c->cut) {
        if(c && c->collector==collector && c->record){RecordWalk(*t,*c,collector,group);return;}
        Gather out{collector};
        Walk(group,out);
        out.Done();
        if(t && (!c || c->collector!=collector))++t->stats.outside;
        return;
    }
    Stats& s=t->stats;
    const unsigned long long t0=counting ? __rdtsc() : 0;
    const unsigned gen=boxGen.load(std::memory_order_relaxed);
    Gather out{collector};
    unsigned first=0,parts=0,kept=0;
    if(const FlatGroup* const f=FlatOf(*t,c->bodies,group,gen)) {
        parts=f->active;
        if(parts)first=f->ids[0];
        kept=f->Cut(c->sweep,out);
    } else {
        Walk(group,[&](unsigned v) {
            if(!parts++)first=v;
            const BoxEntry& b=BoxOf(*t,c->bodies,v,gen);
            if(c->sweep.Touches(b.lo,b.hi)){out(v);++kept;}
        });
        s.lookups+=parts;
    }
    out.Done();
    ++s.walks;s.parts+=parts;s.kept+=kept;s.culled+=parts-kept;
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
    const bool afterSettle=t->afterSettle;
    t->afterSettle=false;
    Cast c{};
    c.collector=collector;
    unsigned char* const core=collector-kCollectorCore;
    if(!Cfg().expandCull || !from || !to || !extent || !BodiesOf(world,c.bodies) || !c.sweep.Set(from,to,extent))++s.off;
    else if(Rd<unsigned>(core,kCoreFlags)&kFlagBlast)++s.blast;
    else if(!armed.load(std::memory_order_relaxed))++s.unarmed;
    else {
        // The debug samples: every 256th cast, and the first cast after every 8th settle with entries on this thread.
        const bool settleSample=shadowing && afterSettle && (++t->settleTick&7)==0;
        if(shadowing && (settleSample || (++t->tick&255)==0)) {
            Sample& m=t->samples[t->nextSample++%kSamples];
            m.core=nullptr;
            m.step=stepNo.load(std::memory_order_relaxed);
            m.settles=settleCount.load(std::memory_order_relaxed);
            m.afterSettle=settleSample;
            m.rayDone=m.bad=false;
            m.list.clear();m.keep.clear();
            c.record=&m;
            ++s.sampled;
            if(settleSample)++s.settleSampled;
        } else {
            c.cut=true;
            ++s.culling;
        }
    }
    struct Scope {
        Thread* t;
        Cast* prev;
        ~Scope() { t->cast=prev;if(counting)Flush(t->stats); }
    } scope{t,t->cast};
    t->cast=&c;
    const std::uintptr_t r=cast(collector,world,from,to,extent);
    if(Sample* const m=c.record) {
        const int count=Rd<int>(collector,kCollectorCount);
        const auto* const list=Rd<const unsigned*>(collector,kCollectorList);
        if(count>0 && list && !m->bad)m->list.assign(list,list+count);
        if(!m->bad && m->keep.size()<=m->list.size())m->core=core;   // complete: the step's builds may check it
    }
    return r;
}

// The batch step (0x237CF0 from 0x704ED4): a new generation of body boxes for its casts.
std::uintptr_t __fastcall Step(void* a,void* b) {
    stepNo.fetch_add(1,std::memory_order_relaxed);
    stepStart.store(Bump(),std::memory_order_relaxed);
    return reinterpret_cast<StepFn>(image+kStep)(a,b);
}

// Step 1's damage settle (0x23426F; the queue core+0x6E0). One with entries may have changed bodies (a death:
// removed / re-added, moved, another shape): a new generation after it, the casts after it fill their boxes again.
// Not noexcept: the settle's exceptions go on (the generation still ends).
std::uintptr_t __fastcall Settle(unsigned char* queue,void* flag,void* hitSet,std::uintptr_t r9) {
    totals.settleCalls.fetch_add(1,std::memory_order_relaxed);
    struct After {
        bool any;
        ~After() {
            if(!any)return;
            Bump();
            settleCount.fetch_add(1,std::memory_order_relaxed);
            totals.settles.fetch_add(1,std::memory_order_relaxed);
            if(self)self->afterSettle=true;
        }
    } after{queue && Rd<std::uint64_t>(queue,kQueueCount)!=0};
    return settleNext(queue,flag,hitSet,r9);
}

// ---- the debug shadow checks (the step-3 build's call 0x2382B6, the ray build's 0x238014) ----
// A hit collector's hits: (count, then count * 0x70 bytes).
void TakeHits(unsigned char* collector,std::vector<unsigned char>& out) {
    const int count=Rd<int>(collector,kHitCount);
    const unsigned n=count>0 ? static_cast<unsigned>(count) : 0u;
    out.assign(sizeof(n),0);
    std::memcpy(out.data(),&n,sizeof(n));
    if(!n)return;
    const auto hits=reinterpret_cast<HitsFn>(Rd<void* const*>(collector,0)[kHitsSlot])(collector);
    out.insert(out.end(),hits,hits+static_cast<std::size_t>(n)*kHitBytes);
}

// The sample's cut of a list filtered from its 0x818 list (0x1079D0: a per-id test, in order, every copy of an id that
// passes): the list's ids at the positions the cut kept. False: the list is not a subsequence of the record's.
bool CutList(const Sample& m,const unsigned* ids,unsigned n,std::vector<unsigned>& cut) {
    cut.clear();
    unsigned j=0;
    for(std::size_t p=0;p<m.list.size() && j<n;++p)
        if(m.list[p]==ids[j]) {
            if(p>=m.keep.size() || m.keep[p])cut.push_back(ids[j]);
            ++j;
        }
    return j==n;
}

bool ReadDword(unsigned dw) noexcept {
    for(const std::size_t r:kHitReadDwords)if(r==dw)return true;
    return false;
}

// The whole list's hits `a` against the cut one's `b`: the fields the game reads (a difference: EXPANDCULL MISMATCH,
// true), the unread dwords that differ noted.
bool Differs(Stats& s,const char* what,const unsigned char* core,unsigned n,unsigned cutN,const std::vector<unsigned char>& a,
             const std::vector<unsigned char>& b) {
    const auto countOf=[](const std::vector<unsigned char>& v) noexcept { unsigned c=0;if(v.size()>=sizeof(c))std::memcpy(&c,v.data(),sizeof(c));return c; };
    const unsigned ha=countOf(a),hb=countOf(b);
    const auto dword=[](const std::vector<unsigned char>& v,unsigned hit,unsigned dw) noexcept {
        unsigned x=0;std::memcpy(&x,v.data()+4+hit*kHitBytes+dw*4,sizeof(x));return x; };
    if(ha!=hb) {
        ++s.mismatches;
        if(loggedMismatches.fetch_add(1,std::memory_order_relaxed)<32)
            Log("EXPANDCULL MISMATCH %s core %p: %u parts, %u cut; hits %u whole, %u cut",what,static_cast<const void*>(core),n,cutN,ha,hb);
        return true;
    }
    unsigned mask=0;
    for(unsigned h=0;h<ha;++h)
        for(unsigned dw=0;dw<kHitBytes/4;++dw) {
            const unsigned x=dword(a,h,dw),y=dword(b,h,dw);
            if(x==y)continue;
            if(ReadDword(dw)) {
                ++s.mismatches;
                if(loggedMismatches.fetch_add(1,std::memory_order_relaxed)<32)
                    Log("EXPANDCULL MISMATCH %s core %p: %u parts, %u cut; hit %u of %u, +0x%02X %08X whole, %08X cut",what,
                        static_cast<const void*>(core),n,cutN,h,ha,dw*4,x,y);
                return true;
            }
            mask|=1u<<dw;
        }
    if(!mask)return false;
    ++s.unreadDiffs;
    s.unreadMask|=mask;
    if(loggedUnread.fetch_add(1,std::memory_order_relaxed)<8) {
        char line[256];
        int at=0;
        for(unsigned dw=0;dw<kHitBytes/4 && at<200;++dw)
            if(mask&(1u<<dw))at+=std::snprintf(line+at,sizeof(line)-static_cast<std::size_t>(at)," +0x%02X %08X/%08X",dw*4,dword(a,0,dw),dword(b,0,dw));
        line[at<0 ? 0 : at]=0;
        Log("EXPANDCULL UNREAD %s core %p: %u hits the same in the read fields; unread dwords differ (hit 0 whole/cut):%s",what,
            static_cast<const void*>(core),ha,line);
    }
    return false;
}

// The step-3 narrowphase on `ids` (the core's list for the time), its hits into `out`; the command as it was after.
void CastOn(unsigned char* core,unsigned char* cmd,const unsigned* ids,unsigned n,const unsigned char* saved,
            std::vector<unsigned char>& out) {
    if(n)std::memcpy(Rd<unsigned*>(core,kCoreIds),ids,static_cast<std::size_t>(n)*sizeof(unsigned));
    std::memcpy(core+kCoreCount,&n,sizeof(n));
    out.clear();
    if(reinterpret_cast<BuildFn>(image+kBuild)(core,cmd)) {
        reinterpret_cast<BatchFn>(image+kShapeBatch)(Rd<unsigned char*>(image,kSystem)+0x10,cmd,1);
        TakeHits(Rd<unsigned char*>(core,kCoreHits),out);
    }
    std::memcpy(cmd,saved,kCommandBodies);
    std::memcpy(cmd+kCommandBodiesEnd,saved+kCommandBodiesEnd,kCommandBytes-kCommandBodiesEnd);
}

// The ray on `ids` (the core's ray list for the time): its build, the rays' batch on its command alone, its hits and
// (with any) the closest's position core+0x920 as a last 0x70-byte record; the command as it was after.
void RayOn(unsigned char* core,unsigned char* cmd,const unsigned* ids,unsigned n,const unsigned char* saved,
           std::vector<unsigned char>& out) {
    if(n)std::memcpy(Rd<unsigned*>(core,kCoreRayIds),ids,static_cast<std::size_t>(n)*sizeof(unsigned));
    std::memcpy(core+kCoreRayCount,&n,sizeof(n));
    out.clear();
    if(reinterpret_cast<BuildFn>(image+kRayBuild)(core,cmd)) {
        reinterpret_cast<BatchFn>(image+kRayBatch)(Rd<unsigned char*>(image,kSystem)+0x10,cmd,1);
        TakeHits(core+kCoreRayHits,out);
        unsigned count=0;
        std::memcpy(&count,out.data(),sizeof(count));
        if(count) {
            // the closest as one more hit: its xyz in the position's read dwords
            const std::size_t at=out.size();
            out.resize(at+kHitBytes,0);
            std::memcpy(out.data()+at,core+kCoreRayBest,12);
            ++count;
            std::memcpy(out.data(),&count,sizeof(count));
        }
    }
    std::memcpy(cmd,saved,kCommandBodies);
    std::memcpy(cmd+kCommandBodiesEnd,saved+kCommandBodiesEnd,kRayCommandBytes-kCommandBodiesEnd);
}

// The sampled core's step-3 list whole and as the cut would have made it at the cast (the record), the narrowphase on
// each, compared. The whole list stays the core's.
void ShadowBuild(Thread& t,const Sample& m,unsigned char* core,unsigned char* cmd) {
    const unsigned n=Rd<unsigned>(core,kCoreCount);
    unsigned* const ids=Rd<unsigned*>(core,kCoreIds);
    if(!n || !ids)return;
    Stats& s=t.stats;
    thread_local std::vector<unsigned> whole,cut;
    thread_local std::vector<unsigned char> a,b;
    whole.assign(ids,ids+n);
    if(!CutList(m,whole.data(),n,cut)){++s.unmatched;return;}
    if(cut.size()==whole.size())return;   // nothing the cut would take out of this list
    unsigned char saved[kCommandBytes];
    std::memcpy(saved,cmd,sizeof(saved));
    CastOn(core,cmd,whole.data(),n,saved,a);
    CastOn(core,cmd,cut.data(),static_cast<unsigned>(cut.size()),saved,b);
    std::memcpy(ids,whole.data(),static_cast<std::size_t>(n)*sizeof(unsigned));
    std::memcpy(core+kCoreCount,&n,sizeof(n));
    // a settle after the cast (a later bullet's, this step): the window between the step-1 cut and this narrowphase
    const bool span=settleCount.load(std::memory_order_relaxed)!=m.settles;
    ++s.shadowed;
    if(m.afterSettle)++s.settleShadowed;
    if(span)++s.spanShadowed;
    const char* const what=m.afterSettle ? (span ? "step 3 (after a settle, one since)" : "step 3 (after a settle)")
                                          : (span ? "step 3 (a settle since)" : "step 3");
    if(Differs(s,what,core,n,static_cast<unsigned>(cut.size()),a,b)) {
        if(m.afterSettle)++s.settleMismatches;
        if(span)++s.spanMismatches;
    }
}

// The sampled core's ray list (0x8C8) whole and cut by the record, each ray cast alone, compared. The whole list stays.
void ShadowRay(Thread& t,const Sample& m,unsigned char* core,unsigned char* cmd) {
    if(!core[kCoreRayOn])return;
    const unsigned n=Rd<unsigned>(core,kCoreRayCount);
    unsigned* const ids=Rd<unsigned*>(core,kCoreRayIds);
    if(!n || !ids)return;
    Stats& s=t.stats;
    thread_local std::vector<unsigned> whole,cut;
    thread_local std::vector<unsigned char> a,b;
    whole.assign(ids,ids+n);
    if(!CutList(m,whole.data(),n,cut)){++s.unmatched;return;}
    if(cut.size()==whole.size())return;
    unsigned char saved[kRayCommandBytes];
    std::memcpy(saved,cmd,sizeof(saved));
    RayOn(core,cmd,whole.data(),n,saved,a);
    RayOn(core,cmd,cut.data(),static_cast<unsigned>(cut.size()),saved,b);
    std::memcpy(ids,whole.data(),static_cast<std::size_t>(n)*sizeof(unsigned));
    std::memcpy(core+kCoreRayCount,&n,sizeof(n));
    ++s.rayShadowed;
    if(m.afterSettle)++s.settleShadowed;
    if(Differs(s,m.afterSettle ? "ray (after a settle)" : "ray",core,n,static_cast<unsigned>(cut.size()),a,b)) {
        ++s.rayMismatches;
        if(m.afterSettle)++s.settleMismatches;
    }
}

Sample* SampleOf(Thread& t,const unsigned char* core) noexcept {
    const unsigned step=stepNo.load(std::memory_order_relaxed);
    for(Sample& m:t.samples)if(m.core==core && m.step==step)return &m;
    return nullptr;
}

// The batch's step-3 build call (debug only). Not noexcept: the build's and the casts' exceptions go on.
std::uint64_t __fastcall Build(unsigned char* core,unsigned char* cmd) {
    if(Thread* const t=self)
        if(Sample* const m=SampleOf(*t,core)) {
            ShadowBuild(*t,*m,core,cmd);
            m->core=nullptr;
            if(counting)Flush(t->stats);
        }
    return reinterpret_cast<BuildFn>(image+kBuild)(core,cmd);
}
// The batch's ray build call (debug only): right after the bullet's step 1. The sample stays for its step-3 build.
std::uint64_t __fastcall RayBuild(unsigned char* core,unsigned char* cmd) {
    if(Thread* const t=self)
        if(Sample* const m=SampleOf(*t,core))
            if(!m->rayDone) {
                m->rayDone=true;
                ShadowRay(*t,*m,core,cmd);
                if(counting)Flush(t->stats);
            }
    return reinterpret_cast<BuildFn>(image+kRayBuild)(core,cmd);
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

// Points the call at `rva` to `hook` over whatever it calls now: the stock function, or another hook's near thunk
// (common/memory.cpp AllocateNearThunk: mov rax,imm64; jmp rax) that ends in it (perf_probe.cpp Chain's way).
// `*next` gets that target; `*thunk` whether it was another's.
bool Chain(unsigned rva,unsigned stock,void* hook,void** next,bool* thunk) noexcept {
    __try {
        unsigned char* const site=image+rva;
        if(!edf::Readable(site,5) || site[0]!=0xE8)return false;
        std::int32_t rel=0;std::memcpy(&rel,site+1,sizeof(rel));
        unsigned char* const now=site+5+rel;
        *thunk=now!=image+stock && edf::Readable(now,12) && now[0]==0x48 && now[1]==0xB8 && now[10]==0xFF && now[11]==0xE0;
        if(now!=image+stock && !*thunk)return false;
        *next=now;
        bool changed=false;
        if(edf::RedirectCall(site,now,hook,changed,edf::Claim::kLayered) || changed)return changed;
        *next=nullptr;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
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
    bool sites=CallsTo(kStepCall,kStep) && CallsTo(kBuildCall,kBuild) && CallsTo(kRayBuildCall,kRayBuild) &&
               CallsTo(kSettleCall,kSettle);
    for(const unsigned site:kCastCalls)sites=sites && CallsTo(site,kCast);
    if(!sites){Log("EXPANDCULL REFUSED: a call site not the stock call, nothing installed");return false;}
    counting=Cfg().perfProbe!=0;
    shadowing=Cfg().debug;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpc0);
    tsc0=__rdtsc();
    unsigned char* const cave=MakeWalkCave();
    if(!cave){Log("EXPANDCULL REFUSED: no cave page, nothing installed");return false;}
    Done done[8]{};
    unsigned n=0;
    const auto redirect=[&](unsigned site,unsigned target,void* to) noexcept {
        Done& d=done[n];
        d.rva=site;d.size=5;std::memcpy(d.stock,image+site,5);
        bool changed=false;
        if(!edf::RedirectCall(image+site,image+target,to,changed) || !changed)return false;
        ++n;
        return true;
    };
    // The generation first (the cache's steps), the walk, the casts' scope; the shadow's builds last.
    bool ok=redirect(kStepCall,kStep,reinterpret_cast<void*>(&Step));
    if(ok) {
        Done& d=done[n];
        d.rva=kWalk;d.size=sizeof(kWalkStock);std::memcpy(d.stock,kWalkStock,sizeof(kWalkStock));
        ok=edf::RedirectToThunk(image+kWalk,kWalkStock,sizeof(kWalkStock),cave,edf::Branch::kJump);
        if(ok)++n;
    }
    for(const unsigned site:kCastCalls)ok=ok && redirect(site,kCast,reinterpret_cast<void*>(&BulletCast));
    if(ok && shadowing)ok=redirect(kBuildCall,kBuild,reinterpret_cast<void*>(&Build)) &&
                          redirect(kRayBuildCall,kRayBuild,reinterpret_cast<void*>(&RayBuild));
    if(ok) {
        installed=true;
        Log("EXPANDCULL bullets' sweeps put only the parts they can reach in their lists (ini ExpandCull, now %d; cut once the "
            "damage settle is chained)%s",Cfg().expandCull ? 1 : 0,
            shadowing ? " (shadow check: every 256th cast and the first after every 8th settle, its step-3 and ray lists)" : "");
        return true;
    }
    const bool undone=Undo(done,n);
    Log("EXPANDCULL REFUSED: hooks not all in (%u), %s",n,undone ? "all put back" : "NOT all put back");
    return false;
}

bool InstallBulletExpandCullSettle() noexcept {
    if(!installed)return false;   // InstallBulletExpandCull logged why
    void* next=nullptr;
    bool thunk=false;
    if(!Chain(kSettleCall,kSettle,reinterpret_cast<void*>(&Settle),&next,&thunk)) {
        Log("EXPANDCULL REFUSED: the damage settle call %X neither stock's nor a near thunk: every cast left whole",kSettleCall);
        return false;
    }
    settleNext=reinterpret_cast<SettleFn>(next);
    armed.store(true,std::memory_order_release);
    Log("EXPANDCULL armed: the damage settle %X chained (over %s): a settle with entries ends the boxes' generation",kSettleCall,
        thunk ? "another hook" : "the stock call");
    return true;
}

void ReportBulletExpandCull(unsigned frames) noexcept {
    const auto take=[](std::atomic<unsigned long long>& v) noexcept { return v.exchange(0,std::memory_order_relaxed); };
    const unsigned long long casts=take(totals.casts),culling=take(totals.culling),blast=take(totals.blast),off=take(totals.off),
        walks=take(totals.walks),parts=take(totals.parts),kept=take(totals.kept),culled=take(totals.culled),
        keptOne=take(totals.keptOne),lookups=take(totals.lookups),misses=take(totals.misses),unbounded=take(totals.unbounded),
        walkTicks=take(totals.walkTicks),boxTicks=take(totals.boxTicks),outside=take(totals.outside),
        filtered=take(totals.filtered),sampled=take(totals.sampled),shadowed=take(totals.shadowed),
        mismatches=take(totals.mismatches),unreadDiffs=take(totals.unreadDiffs),unarmed=take(totals.unarmed),
        rechecked=take(totals.rechecked),stale=take(totals.stale),settleSampled=take(totals.settleSampled),
        settleShadowed=take(totals.settleShadowed),settleMismatches=take(totals.settleMismatches),
        spanShadowed=take(totals.spanShadowed),spanMismatches=take(totals.spanMismatches),rayShadowed=take(totals.rayShadowed),
        rayMismatches=take(totals.rayMismatches),unmatched=take(totals.unmatched),settleCalls=take(totals.settleCalls),
        settles=take(totals.settles),flatHits=take(totals.flatHits),flatBuilds=take(totals.flatBuilds),
        flatChanged=take(totals.flatChanged),flatChecked=take(totals.flatChecked),flatMismatches=take(totals.flatMismatches);
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
    Log("PERF EXPANDCULL %u frames: %llu casts (%llu cut, %llu blast, %llu off, %llu unarmed, %llu shadow samples, %llu of them "
        "after a settle); %llu cut walks, %llu parts, %llu kept, %llu culled (%.1f%%), %llu kept one; cut %.3f ms (%.3f a frame, "
        "%.2f ns a part), of it box computes %.3f ms; box cache %llu hits %llu misses (%llu unbounded); settles %llu, %llu with "
        "entries (a new generation); boxes filled again after a settle %llu, changed %llu; walks as stock %llu outside casts, "
        "%llu filtered; shadow step 3 %llu checked (%llu with a settle since the cast: %llu mismatched), ray %llu checked "
        "(%llu mismatched), after a settle %llu checked (%llu mismatched), %llu mismatched in all, %llu lists not the record's, "
        "%llu with unread dwords differing (mask %08X); flat copies %.1f%% hits (%llu hits, %llu made, %llu of them the list "
        "changed in the generation), %llu checked against the walk (%llu mismatched)",
        frames,casts,culling,blast,off,unarmed,sampled,settleSampled,walks,parts,kept,culled,
        parts ? 100.0*static_cast<double>(culled)/static_cast<double>(parts) : 0.0,keptOne,walkMs,walkMs/n,
        parts ? walkMs*1e6/static_cast<double>(parts) : 0.0,boxMs,lookups-(std::min)(lookups,misses),misses,unbounded,
        settleCalls,settles,rechecked,stale,outside,filtered,shadowed,spanShadowed,spanMismatches,rayShadowed,rayMismatches,
        settleShadowed,settleMismatches,mismatches,unmatched,unreadDiffs,mask,
        flatHits+flatBuilds ? 100.0*static_cast<double>(flatHits)/static_cast<double>(flatHits+flatBuilds) : 0.0,flatHits,
        flatBuilds,flatChanged,flatChecked,flatMismatches);
}
}  // namespace crew
