// The group expansion's addresses and its per-cast memo (group_expand.cpp; tests/group_expand_test.cpp runs the hooks
// on a private EDF.dll mapping against the stock addBody). docs/group-expand-re.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::group_expand {
// ---- stock EDF.dll (TimeDateStamp 0x678CCB46) ----
constexpr unsigned kAddBody=0x109DA0,kAddBodyEnd=0x10A002;        // hit_query::NpTargetCollector::addBody(this, body id)
constexpr std::uint64_t kAddBodyFnv=0x4cfbba1b6be3fbd5ull;          // FNV-1a over [kAddBody, kAddBodyEnd)
constexpr unsigned kAppend=0x11A9EC0,kAppendEnd=0x11A9F30;          // the collector's list append(this, id)
constexpr std::uint64_t kAppendFnv=0xd95186e44c367004ull;
// The walk: `mov rdi,[rdi+0A0h]; mov rbx,[rdi]; cmp rbx,rdi; je 109FEDh` and the loop after it. rbp the collector,
// rdi the group, esi the overlapped body's id; 0x109FED the epilogue (restores rbx / rbp / rsi from the home slots).
constexpr unsigned kWalk=0x109F6F,kResume=0x109FED;
constexpr unsigned char kWalkStock[]{0x48,0x8B,0xBF,0xA0,0x00,0x00,0x00,0x48,0x8B,0x1F,0x48,0x3B,0xDF,0x74,0x6F};
// addBody's unwind info (its .pdata entry's): saves rsi/rbp/rbx in the home slots, push rdi, sub rsp,30h. The cave's is
// the same codes with no prolog: it runs in addBody's frame as it is at the walk (it moves no rsp).
constexpr unsigned kAddBodyUnwind=0x1D2C420;
constexpr unsigned char kAddBodyUnwindStock[]{0x01,0x14,0x08,0x00,0x14,0x64,0x0B,0x00,0x14,0x54,0x0A,0x00,0x14,0x34,0x09,
                                              0x00,0x14,0x52,0x10,0x70};
// The cave's: no prolog, each code at offset 0 (RtlVirtualUnwind skips the codes whose offset is past the pc's, in or
// out of the prolog; the cave's pc is 15). The save slots (0x0B / 0x0A / 0x09 x 8) as addBody's.
constexpr unsigned char kCaveUnwind[]{0x01,0x00,0x08,0x00,0x00,0x64,0x0B,0x00,0x00,0x54,0x0A,0x00,0x00,0x34,0x09,
                                      0x00,0x00,0x52,0x00,0x70};
static_assert(sizeof(kCaveUnwind)==sizeof(kAddBodyUnwindStock));
// xgs World::CastShape (box sweep, broadphase only: the query filter adds each overlapped body and refuses it) and
// the bullet step's three calls of it, rcx = the bullet's collector (core+0x800): move types 2 / 1 / 0.
constexpr unsigned kCast=0x11A70B0;
constexpr unsigned kCastCalls[]{0x234334,0x234982,0x234B98};
// The collector, the group, its nodes (a std::list: +0 next), the body.
// The list (the append's): +0x18 the ids, +0x20 the count, +0x24 the capacity (low 30 bits).
constexpr std::size_t kCollectorList=0x18,kCollectorCount=0x20,kCollectorCapacity=0x24;
constexpr unsigned kCapacityMask=0x3FFFFFFF;
constexpr std::size_t kCollectorFilter=0x80;   // a functor (vtable +0x10: bool(functor, unsigned* id)) or null
constexpr std::size_t kGroupList=0xA0;         // the list's head node
constexpr std::size_t kNodeBody=0x10,kNodeOn=0xA0;
constexpr std::size_t kBodyId=0xF0;

template<class T> inline T Rd(const void* p,std::size_t off) noexcept { T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof(T));return v; }

// One cast's walks: group -> the ids its walk appended. Open addressing over a power-of-two table, reset by a
// generation number (no clearing per cast); the ids in one pool. Full (table or pool): no more entries this cast (the
// later groups are walked each time, as stock).
class Memo {
public:
    static constexpr unsigned kSlots=512,kMostEntries=384,kMostIds=1u<<16;
    struct Span { unsigned first,count; };
    void Begin() noexcept { if(++gen==0){std::memset(slots,0,sizeof(slots));gen=1;} used=0;entries=0;full=false; }
    // The ids a walk of `group` appended this cast, or null.
    const Span* Find(const void* group) noexcept {
        for(unsigned i=Hash(group);;i=(i+1)&(kSlots-1)) {
            const Slot& s=slots[i];
            if(s.gen!=gen)return nullptr;
            if(s.group==group)return &s.span;
        }
    }
    const unsigned* Ids(const Span& s) const noexcept { return ids+s.first; }
    // Where a walk writes its ids (up to `room`, then Commit), or null (full: no more entries this cast).
    unsigned* Room(unsigned& room) noexcept {
        if(full || entries>=kMostEntries || used>=kMostIds){full=true;room=0;return nullptr;}
        room=kMostIds-used;
        return ids+used;
    }
    // The walk wrote `count` ids; more than the room: none kept, full.
    void Commit(const void* group,unsigned count) noexcept {
        if(count>kMostIds-used){full=true;return;}
        unsigned i=Hash(group);
        while(slots[i].gen==gen)i=(i+1)&(kSlots-1);
        slots[i]={group,gen,{used,count}};
        used+=count;++entries;
    }
    bool Full() const noexcept { return full; }
private:
    struct Slot { const void* group;unsigned gen;Span span; };
    static unsigned Hash(const void* p) noexcept {
        const auto v=reinterpret_cast<std::uintptr_t>(p)>>4;
        return static_cast<unsigned>((v*0x9E3779B97F4A7C15ull)>>(64-9));
    }
    Slot slots[kSlots]{};
    unsigned ids[kMostIds];
    unsigned gen=0,used=0,entries=0;
    bool full=false;
};
static_assert((Memo::kSlots&(Memo::kSlots-1))==0 && Memo::kSlots==1u<<9,"Hash takes the top 9 bits");

struct Stats {
    unsigned casts=0;                       // bullet casts (the scope)
    unsigned long long castTicks=0;         // their QPC ticks (PerfProbe)
    unsigned walks=0,nodes=0,walkedIds=0;   // in a cast: groups walked, their nodes, the ids appended
    unsigned copies=0,copiedIds=0;          // in a cast: groups appended from the memo, their ids
    unsigned outside=0,filtered=0;          // walks outside a cast (any other query), of them with a filter functor
    unsigned full=0;                        // casts whose memo filled up
    unsigned shadowed=0,mismatches=0;
};
}  // namespace crew::group_expand
