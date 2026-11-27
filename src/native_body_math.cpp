#include "native_body_math.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include <algorithm>
#include "perf_probe.h"
#include "team_native_logic.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <array>
#include <atomic>
#include <new>
#include <vector>
#include <cfloat>
#include <cstring>
#include <emmintrin.h>

namespace crew {
namespace {
constexpr unsigned kTransformBox=0x652F0,kBoxArguments=0x109BFD,kBoxCall=0x109C09,kSignMask=0x1C36A90;
constexpr unsigned kBasis=0x511C0;
constexpr unsigned kBoxMerge=kBoxArguments,kBoxMergeEnd=0x109C9D,kBoxMergeCall=kBoxMerge+7;
// Exact straight-line region through the last store to the union; the native body update follows it.
constexpr unsigned char kBoxMergeStock[]={
    0x48,0x8D,0x53,0x20,0x4C,0x8D,0x43,0x60,0x48,0x8D,0x4B,0x40,0xE8,0xE2,0xB6,0xF5,
    0xFF,0x0F,0x10,0x43,0x40,0x4C,0x8D,0x4D,0x20,0x0F,0x10,0x63,0x50,0x4C,0x8D,0x43,
    0x60,0x0F,0x10,0x5B,0x40,0x0F,0x28,0xD0,0x0F,0x28,0x4D,0x00,0x0F,0x5C,0xD4,0x0F,
    0xC6,0xC0,0xFF,0x0F,0x58,0xE3,0x0F,0xC6,0xDB,0xFF,0xC7,0x44,0x24,0x20,0x01,0x00,
    0x00,0x00,0x0F,0xC6,0xD2,0x93,0xF3,0x0F,0x10,0xD0,0x0F,0xC6,0xE4,0x93,0x0F,0xC6,
    0xD2,0x39,0xF3,0x0F,0x10,0xE3,0x0F,0x5D,0xCA,0x0F,0x29,0x55,0x20,0x0F,0x28,0x55,
    0x10,0x0F,0x28,0xC1,0x0F,0xC6,0xC1,0x55,0xF3,0x0F,0x11,0x45,0x04,0xF3,0x0F,0x11,
    0x4D,0x00,0x0F,0xC6,0xC9,0xAA,0xF3,0x0F,0x11,0x4D,0x08,0x0F,0xC6,0xE4,0x39,0x0F,
    0x5F,0xD4,0x0F,0x29,0x65,0x20,0xF3,0x0F,0x11,0x55,0x10,0x0F,0x28,0xC2,0x0F,0xC6,
    0xC2,0x55,0xF3,0x0F,0x11,0x45,0x14,0x0F,0xC6,0xD2,0xAA,0xF3,0x0F,0x11,0x55,0x18,
};
static_assert(sizeof(kBoxMergeStock)==kBoxMergeEnd-kBoxMerge);
constexpr unsigned char kBoxSig[]={0x48,0x83,0xEC,0x18,0xF3,0x0F,0x10,0x1D,0x94,0x17,0xBD,0x01};
constexpr unsigned char kBoxArgumentsSig[]={0x48,0x8D,0x53,0x20,0x4C,0x8D,0x43,0x60,0x48,0x8D,0x4B,0x40};
constexpr unsigned char kBasisSig[]={0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0xD0,0x00,0x00,0x00};
struct BasisCall {
    unsigned arguments,call;
    unsigned char argumentSig[5],callSig[5];
};
// Each mode builds its own 4x4 scratch matrix and normalizes it before passing it to a different consumer.
constexpr BasisCall kAttachedBasisCalls[]={
    {0x6ED902,0x6ED9EB,{0x48,0x8D,0x4C,0x24,0x20},{0xE8,0xD0,0x37,0x96,0xFF}},
    {0x6EDB05,0x6EDBF4,{0x48,0x8D,0x4C,0x24,0x60},{0xE8,0xC7,0x35,0x96,0xFF}},
};
constexpr unsigned kAvoidanceSift=0xD5E020,kAvoidanceSiftEnd=0xD5E0F7;
constexpr unsigned kAvoidanceRaise=0xD5FF10,kAvoidanceRaiseEnd=0xD5FF72;
struct AvoidanceCall {
    unsigned call;
    unsigned char stock[10];
};
constexpr AvoidanceCall kAvoidanceCalls[]{
    {0xD5F5A5,{0x8B,0xD7,0x48,0x8B,0xCB,0xE8,0x76,0xEA,0xFF,0xFF}},
    {0xD5F604,{0x75,0x08,0x48,0x8B,0xCB,0xE8,0x17,0xEA,0xFF,0xFF}},
};
constexpr AvoidanceCall kAvoidancePush{0xD5F5BA,{0x8B,0xD6,0x48,0x8B,0xCB,0xE8,0x51,0x09,0,0}};
constexpr unsigned kAvoidanceOverflow=kAvoidancePush.call-5,kAvoidancePop=0xD5F5BF,kAvoidanceDone=0xD5F609;
constexpr unsigned kAvoidanceOverflowCall=kAvoidanceOverflow+3;
constexpr unsigned char kAvoidanceFirstHeapSkip[]{0xEB,0x0A};
constexpr unsigned char kAvoidanceDoneSig[]{0x4C,0x8B,0x55,0xF7,0x4C,0x8B,0x5D,0xEF};


struct AvoidanceNeighbor { std::uint64_t handle; float distanceSquared; std::uint32_t padding; };
struct AvoidanceHeap { AvoidanceNeighbor* entries; int count; };
static_assert(sizeof(AvoidanceNeighbor)==0x10 && offsetof(AvoidanceNeighbor,distanceSquared)==8 &&
              offsetof(AvoidanceHeap,count)==8);

int __fastcall SiftAvoidanceNeighbors(AvoidanceHeap* heap,int root) noexcept {
    const int inner=heap->count/2;
    if(root>=inner)return root;
    auto* const entries=heap->entries;
    const auto handle=entries[root].handle;
    const float distance=entries[root].distanceSquared;
    int hole=root;
    while(hole<inner) {
        const int left=hole*2+1,right=left+1;
        // Stock picks the right child on a tie or unordered comparison.
        const int child=right<heap->count && !(entries[left].distanceSquared>entries[right].distanceSquared) ?
            right : left;
        if(!(entries[child].distanceSquared>distance))break;
        entries[hole].handle=entries[child].handle;
        entries[hole].distanceSquared=entries[child].distanceSquared;
        hole=child;
    }
    if(hole!=root) {
        // The native swap only moves the handle and distance. Leave the trailing word in its original slot.
        entries[hole].handle=handle;
        entries[hole].distanceSquared=distance;
    }
    return hole;
}

void __fastcall RaiseAvoidanceNeighbor(AvoidanceHeap* heap,int index) noexcept {
    if(index<=0)return;
    auto* const entries=heap->entries;
    const float distance=entries[index].distanceSquared;
    int parent=(index-1)/2;
    if(entries[parent].distanceSquared>distance)return;
    const auto handle=entries[index].handle;
    do {
        entries[index].handle=entries[parent].handle;
        entries[index].distanceSquared=entries[parent].distanceSquared;
        index=parent;
        if(!index)break;
        parent=(index-1)/2;
        // The native push advances on ties and unordered distances.
    } while(!(entries[parent].distanceSquared>distance));
    entries[index].handle=handle;
    entries[index].distanceSquared=distance;
}

bool DiscardAvoidanceTail(AvoidanceHeap& heap,int last) noexcept {
    auto* const entries=heap.entries;
    const float added=entries[last].distanceSquared;
    if(!(added>entries[0].distanceSquared))return false;
    const int tail=(last-1)/2;
    const float returning=entries[tail].distanceSquared;
    if(!(added>returning))return false;
    // If the final slot is a right child, its left sibling stays in the heap.
    if((last&1)==0 && entries[last-1].distanceSquared>returning)return false;
    // Verify that stock's pop would retrace the push path, including its right-child tie rule.
    for(int child=tail;child>0;child=(child-1)/2) {
        const int parent=(child-1)/2;
        const float lifted=entries[parent].distanceSquared;
        if(!(added>lifted) || !(lifted>returning))return false;
        const bool left=(child&1)!=0;
        const int sibling=left ? child+1 : child-1;
        if(sibling<last && (left ? !(lifted>entries[sibling].distanceSquared) :
                                    entries[sibling].distanceSquared>lifted))return false;
    }
    // Stock leaves the displaced parent in the expired slot after the drop.
    entries[last].handle=entries[tail].handle;
    entries[last].distanceSquared=returning;
    heap.count=last;
    return true;
}

void __fastcall PushAvoidanceNeighbor(AvoidanceHeap* heap) noexcept {
    const int last=heap->count-1;
    if(last==0) {heap->count=0;return;}
    if(DiscardAvoidanceTail(*heap,last))return;
    RaiseAvoidanceNeighbor(heap,last);
    auto* const entries=heap->entries;
    const bool farther=entries[last].distanceSquared>entries[0].distanceSquared;
    entries[0].handle=entries[last].handle;
    entries[0].distanceSquared=entries[last].distanceSquared;
    heap->count=last;
    if(!farther)SiftAvoidanceNeighbors(heap,0);
}

bool NativeCodeMatches(unsigned begin,unsigned end,std::uint64_t expected) noexcept {
    const auto* const code=image+begin;
    const unsigned bytes=end-begin;
    if(!edf::Readable(code,bytes))return false;
    std::uint64_t fingerprint=14695981039346656037ull;
    for(unsigned i=0;i<bytes;++i)fingerprint=(fingerprint^code[i])*1099511628211ull;
    return fingerprint==expected;
}

constexpr unsigned kTargetScore=0x54A3B0,kTargetScoreFunctionEnd=0x54A4A4;
constexpr unsigned kTargetScoreStart=0x54A3E3,kTargetScoreEnd=0x54A494;
constexpr unsigned kReadWeakTarget=0x30C0F0,kReadWeakTargetEnd=0x30C1DA;
constexpr unsigned char kTargetScoreStock[]{
    0x0F,0xC6,0xD2,0xFF,0x0F,0x57,0xF6,0x0F,0x28,0xC3,0x0F,0x28,0xCB,0x0F,0xC6,0xCB,
    0xAA,0x0F,0xC6,0xC3,0x55,0x0F,0xC6,0xDB,0xE1,0xF3,0x0F,0x10,0xD8,0x0F,0xC6,0xDB,
    0xC6,0xF3,0x0F,0x10,0xD9,0x0F,0xC6,0xDB,0x27,0xF3,0x0F,0x10,0xDA,0x0F,0xC6,0xDB,
    0x39,0x0F,0x59,0xDB,0x0F,0x28,0xCB,0x0F,0xC6,0xCB,0x55,0xF3,0x0F,0x58,0xCB,0x0F,
    0xC6,0xDB,0xAA,0xF3,0x0F,0x58,0xCB,0x0F,0x2E,0xCE,0x7A,0x02,0x74,0x1C,0x0F,0x57,
    0xC0,0x0F,0x2E,0xC1,0x77,0x09,0x0F,0x57,0xF6,0xF3,0x0F,0x51,0xF1,0xEB,0x0B,0x0F,
    0x28,0xC1,0xE8,0x74,0x04,0xD9,0x00,0x0F,0x28,0xF0,0x80,0x7B,0x38,0x00,0x74,0x08,
    0xF3,0x0F,0x59,0xB7,0x7C,0x04,0x00,0x00,0x48,0x8B,0x43,0x28,0x48,0x8B,0x88,0x48,
    0x05,0x00,0x00,0x48,0x85,0xC9,0x74,0x16,0xE8,0x80,0x1C,0xDC,0xFF,0x48,0x3B,0xC7,
    0x75,0x0C,0x48,0x8B,0x43,0x28,0xF3,0x0F,0x5E,0xB0,0x38,0x05,0x00,0x00,0xF3,0x0F,
    0x10,0x43,0x20,0x0F,0x2F,0xC6,0x76,0x09,0xF3,0x0F,0x11,0x73,0x20,0x48,0x89,0x7B,
    0x30,
};
static_assert(sizeof(kTargetScoreStock)==kTargetScoreEnd-kTargetScoreStart);
constexpr unsigned char kTargetScorePacked[]{
    0x0F,0xC6,0xD3,0xAF,             // [w,w,dz,dz] from the original point and the difference
    0x0F,0xC6,0xDA,0x24,             // [dx,dy,dz,w], including the original fourth lane
    0x0F,0x57,0xF6,                 // xorps xmm6,xmm6
    // Native square and ordered sum. Positive distances fall through to sqrtss.
    0x0F,0x59,0xDB,0x0F,0x28,0xCB,0x0F,0xC6,0xCB,0x55,0xF3,0x0F,0x58,0xCB,0x0F,0xC6,
    0xDB,0xAA,0xF3,0x0F,0x58,0xCB,0x0F,0x2E,0xCE,0x76,0x40,0xF3,0x0F,0x51,0xF1,
    // Apply the optional weight. A different held-target address proceeds directly to the score comparison.
    0x80,0x7B,0x38,0x00,0x74,0x08,0xF3,0x0F,0x59,0xB7,0x7C,0x04,0x00,0x00,0x48,0x8B,
    0x43,0x28,0x48,0x8B,0x88,0x48,0x05,0x00,0x00,0x48,0x85,0xC9,0x74,0x09,0x48,0x39,
    0xB9,0x18,0x05,0x00,0x00,0x74,0x26,
    // Stock strict comparison and writeback. Ties keep the first candidate.
    0xF3,0x0F,0x10,0x43,0x20,0x0F,0x2F,0xC6,0x76,0x56,0xF3,0x0F,0x11,0x73,0x20,0x48,
    0x89,0x7B,0x30,0xEB,0x4B,
    // NaN still uses sqrtss, both signs of zero keep +0, and negative distances call the native fallback.
    0x7A,0xBE,0x74,0xC0,0x0F,0x28,0xC1,0xE8,0x69,0x04,0xD9,0x00,0x0F,0x28,0xF0,0xEB,
    0xB3,
    // A matching address still has to survive the native weak lookup before applying the bonus.
    0xE8,0x91,0x1C,0xDC,0xFF,0x48,0x3B,0xC7,0x75,0xD0,0x48,0x8B,0x43,0x28,0xF3,0x0F,
    0x5E,0xB0,0x38,0x05,0x00,0x00,0xEB,0xC2,
};

bool InstallTargetScore() noexcept {
    if(!NativeCodeMatches(kTargetScore,kTargetScoreFunctionEnd,0xA0D9D4BC5DF2ACCFull) ||
       !NativeCodeMatches(kReadWeakTarget,kReadWeakTargetEnd,0x367523FA969A4231ull))return false;
    // Keep the original unwind range, the epilogue and saved registers.
    unsigned char code[sizeof(kTargetScoreStock)];
    static_assert(sizeof(kTargetScorePacked)<sizeof(code));
    std::memset(code,0xCC,sizeof(code));
    std::memcpy(code,kTargetScorePacked,sizeof(kTargetScorePacked));
    return edf::PatchCode(image+kTargetScoreStart,kTargetScoreStock,code,sizeof(code));
}

constexpr unsigned kHostileWalk=0x5E0F20,kHostileWalkEnd=0x5E1090;
constexpr unsigned kHostileVisit=0x5E0FC8,kHostileVisitEnd=0x5E0FD5;
constexpr unsigned kHostileAdapterSlot=0x17D7210,kHostileAdapter=0x5E2CC0;
constexpr unsigned char kHostileAdapterStock[]{
    0x48,0x8B,0x49,0x08,0x48,0x8B,0x01,0x48,0xFF,0x60,0x08,
};
constexpr unsigned char kHostileVisitStock[]{
    0x48,0x8B,0x44,0x24,0x30,      // mov rax,[rsp+30h]
    0x48,0x8D,0x4C,0x24,0x30,      // lea rcx,[rsp+30h]
    0xFF,0x50,0x08,                 // call qword ptr [rax+8]
};
constexpr unsigned char kHostileVisitDirect[]{
    0x48,0x8B,0x4C,0x24,0x38,      // mov rcx,[rsp+38h]
    0x48,0x8B,0x01,                 // mov rax,[rcx]
    0x66,0x90,                      // keep the call and its return address at the original offsets
    0xFF,0x50,0x08,                 // call qword ptr [rax+8]
};
static_assert(sizeof(kHostileVisitStock)==kHostileVisitEnd-kHostileVisit &&
              sizeof(kHostileVisitDirect)==sizeof(kHostileVisitStock));

bool InstallHostileVisit() noexcept {
    if(!NativeCodeMatches(kHostileWalk,kHostileWalkEnd,0xF4245D2AF5D26C84ull) ||
       !edf::Readable(image+kHostileAdapterSlot,sizeof(void*)) ||
       edf::At<const void*>(image,kHostileAdapterSlot)!=image+kHostileAdapter ||
       !edf::Readable(image+kHostileAdapter,sizeof(kHostileAdapterStock)) ||
       !edf::Matches(image,kHostileAdapter,kHostileAdapterStock,sizeof(kHostileAdapterStock)))return false;
    // The stock adapter only loads the original visitor and tail-calls its slot. RDX still holds the candidate.
    return edf::PatchCode(image+kHostileVisit,kHostileVisitStock,kHostileVisitDirect,sizeof(kHostileVisitStock));
}

constexpr unsigned kRosterCapacity=team_native::kTeamCapacity;
constexpr unsigned kNearestRosterSite=0x54C506,kSoldierRosterSite=0x59777B;
constexpr unsigned kNearestRosterVtable=0x17C2948,kSoldierRosterVtable=0x17D24C0,kSoldierRosterVisitor=0x598C50;
constexpr unsigned kRosterInsert=0x5E2350,kRosterErase=0x5E2A70,kRosterConstruct=0x5E03C0;
std::atomic<std::uint64_t> rosterEpoch{1};
std::atomic<bool> rosterReady{false};
thread_local unsigned rosterMutationDepth=0;

struct RosterNode {
    const RosterNode *left,*parent,*right;
    unsigned char color,nil;
    const void* actor;
};
static_assert(offsetof(RosterNode,nil)==0x19 && offsetof(RosterNode,actor)==0x20);
struct RosterRow {
    const RosterNode* head;
    std::size_t size,reserved;
    const int* relations;
    std::size_t reservedCount,relationCount,tail;
};
static_assert(sizeof(RosterRow)==team_native::kRowStride && offsetof(RosterRow,relationCount)==team_native::kRelSize);
using RosterVisitor=void(__fastcall*)(void*,const void*);
using RosterWalk=void(__fastcall*)(void*,int,void*);

struct RosterSnapshot {
    const RosterNode* head{};
    std::uint64_t epoch{};
    std::vector<const void*> actors;
};
struct RosterScratch {
    const void* manager{};
    const RosterRow* rows{};
    std::array<RosterSnapshot,kRosterCapacity> teams;
    bool busy=false;
};
struct RosterScope {
    CRITICAL_SECTION* lock;
    bool& busy;
    RosterScope(void* manager,bool& inUse) noexcept:
        lock(reinterpret_cast<CRITICAL_SECTION*>(static_cast<unsigned char*>(manager)+team_native::kLock)),busy(inUse) {
        EnterCriticalSection(lock);
        busy=true;
    }
    ~RosterScope() noexcept {
        busy=false;
        LeaveCriticalSection(lock);
    }
};
struct RosterMutation {
    RosterMutation() noexcept {++rosterMutationDepth;}
    ~RosterMutation() noexcept {
        rosterEpoch.fetch_add(1,std::memory_order_relaxed);
        --rosterMutationDepth;
    }
};

const RosterNode* NextRosterNode(const RosterNode* node) noexcept {
    if(!node->right->nil) {
        node=node->right;
        while(!node->left->nil)node=node->left;
        return node;
    }
    auto* parent=node->parent;
    while(!parent->nil && node==parent->right){node=parent;parent=parent->parent;}
    return parent;
}

const RosterNode* AfterRosterActor(const RosterNode* head,const void* actor) noexcept {
    const auto key=reinterpret_cast<std::uintptr_t>(actor);
    auto* bound=head;
    for(auto* node=head->parent;!node->nil;) {
        if(key<reinterpret_cast<std::uintptr_t>(node->actor)) {bound=node;node=node->left;}
        else node=node->right;
    }
    return bound;
}

bool RosterTargetable(const void* actor) noexcept {
    return !edf::At<unsigned char>(actor,0x2E8) && !(edf::At<unsigned char>(actor,0x380)&0x80);
}

__forceinline void VisitRosterActor(void* callback,const void* actor) {
    const auto* const table=edf::At<const void* const*>(callback,0);
    reinterpret_cast<RosterVisitor>(table[1])(callback,actor);
}

class NearestRosterScan {
public:
    NearestRosterScan(void* context,bool active) noexcept:callback(context) {
        enabled=active;
        if(enabled) {ReadMode();ReadObserver();}
    }

    bool Enabled() const noexcept {return enabled;}

    __forceinline bool Visit(const void* actor) {
        const auto* const table=edf::At<const void* const*>(callback,0);
        const auto visitor=reinterpret_cast<RosterVisitor>(table[1]);
        const bool stock=table==reinterpret_cast<const void* const*>(image+kNearestRosterVtable) &&
                         table[1]==image+kTargetScore;
        if(enabled && stock && (edf::At<std::uint32_t>(actor,0x39C)&~2u))return true;
        if(!enabled || !stock || (weighted && edf::At<std::uint32_t>(actor,0x47C)!=0x3F800000u)) {
            // Ordinary unit-weight scans use the bound. General weighted scans revert after their first non-unit weight.
            enabled=false;
            visitor(callback,actor);
            return false;
        }
        if(Score(actor))return true;
        const auto best=edf::At<std::uint32_t>(callback,0x20);
        visitor(callback,actor);
        if(best!=edf::At<std::uint32_t>(callback,0x20))limit=kInfinity;
        ReadMode();
        ReadObserver();
        return enabled;
    }

    void Reset() noexcept {
        limit=kInfinity;
        if(enabled) {ReadMode();ReadObserver();}
    }

private:
    // A winning unweighted, non-held candidate certifies this bound in the current rounding mode.
    // The native score represents sqrt(d2). Equal scores keep the first candidate.
    static constexpr unsigned kNormal=0x00800000u,kInfinity=0x7F800000u;
    void* callback;
    const void* observer{};
    unsigned limit=kInfinity,mode{};
    bool enabled=false,precision=false,weighted=false;

    void ReadObserver() noexcept {
        observer=edf::At<const void*>(callback,0x28);
        weighted=edf::At<unsigned char>(callback,0x38)!=0;
    }
    void ReadMode() noexcept {
        const unsigned csr=_mm_getcsr();
        const unsigned next=csr&0xFFC0u;
        if(next!=mode)limit=kInfinity;
        mode=next;
        enabled=(csr&0x1F80u)==0x1F80u;
        precision=(csr&0x20)!=0;
    }

    __forceinline bool Score(const void* actor) noexcept {
        if(!observer)return false;
        const auto* const holder=edf::At<const void*>(observer,0x548);
        if(holder && edf::At<const void*>(holder,0x518)==actor)return false;

        const auto p=_mm_loadu_ps(reinterpret_cast<const float*>(static_cast<const unsigned char*>(actor)+0x90));
        const auto origin=_mm_load_ps(reinterpret_cast<const float*>(static_cast<const unsigned char*>(callback)+0x10));
        const auto delta=_mm_sub_ps(p,origin);
        // Preserve the native subtraction, fourth lane, multiply, and ordered sum before testing the bound.
        const auto lanes=_mm_shuffle_ps(delta,_mm_shuffle_ps(p,delta,0xAF),0x24);
        const auto d2=_mm_mul_ps(lanes,lanes);
        const auto sum=_mm_add_ss(_mm_add_ss(_mm_shuffle_ps(d2,d2,0x55),d2),_mm_shuffle_ps(d2,d2,0xAA));
        const auto bits=static_cast<unsigned>(_mm_cvtsi128_si32(_mm_castps_si128(sum)));
        if(bits<kNormal || bits>=kInfinity)return false;
        // On a normal positive operand the square root can only add inexact. That flag is already set here.
        if(precision && bits>=limit)return true;

        const auto distance=_mm_sqrt_ss(sum);
        const auto best=_mm_load_ss(reinterpret_cast<const float*>(static_cast<const unsigned char*>(callback)+0x20));
        if(_mm_comigt_ss(best,distance)) {
            edf::Put(callback,0x20,_mm_cvtss_f32(distance));
            edf::Put(callback,0x30,actor);
            limit=bits;
        }
        if(!precision)precision=(_mm_getcsr()&0x20u)!=0;
        return true;
    }
};

void VisitLiveRoster(const RosterRow& row,const RosterNode* node,void* callback) {
    while(node!=row.head) {
        const void* const actor=node->actor;
        if(RosterTargetable(actor)) {
            const auto epoch=rosterEpoch.load(std::memory_order_relaxed);
            VisitRosterActor(callback,actor);
            if(rosterEpoch.load(std::memory_order_relaxed)!=epoch) {
                node=AfterRosterActor(row.head,actor);
                continue;
            }
        }
        node=NextRosterNode(node);
    }
}

bool SnapshotRoster(const RosterRow& row,RosterSnapshot& list,std::uint64_t epoch) noexcept {
    if(list.epoch==epoch && list.head==row.head && list.actors.size()==row.size)return true;
    list.epoch=0;
    if(row.size>0x20000)return false;
    try {list.actors.resize(row.size);}
    catch(const std::bad_alloc&) {return false;}
    auto* node=row.head->left;
    for(auto& actor:list.actors) {
        if(node==row.head || node->nil)return false;
        actor=node->actor;
        node=NextRosterNode(node);
    }
    if(node!=row.head)return false;
    list.head=row.head;
    list.epoch=epoch;
    return true;
}

void VisitRosterRow(const RosterRow& row,RosterSnapshot& list,void* callback,NearestRosterScan* nearest) {
    if(!row.size)return;
    const auto epoch=rosterEpoch.load(std::memory_order_relaxed);
    if(!SnapshotRoster(row,list,epoch)) {
        VisitLiveRoster(row,row.head->left,callback);
        if(nearest)nearest->Reset();
        return;
    }
    for(const void* actor:list.actors) {
        if(!RosterTargetable(actor))continue;
        if(nearest) {
            if(!nearest->Visit(actor))nearest=nullptr;
        } else VisitRosterActor(callback,actor);
        if(rosterEpoch.load(std::memory_order_relaxed)!=epoch) {
            // A re-entrant visitor can change registration. Continue with the next key in the live tree.
            VisitLiveRoster(row,AfterRosterActor(row.head,actor),callback);
            if(nearest)nearest->Reset();
            return;
        }
    }
}

bool TryVisitRoster(void* manager,int observer,void* callback) {
    if(!manager || observer<0 || rosterMutationDepth)return false;
    static thread_local RosterScratch scratch;
    if(scratch.busy)return false;
    RosterScope scope(manager,scratch.busy);
    const auto count=edf::At<std::size_t>(manager,team_native::kRowCount);
    const auto* const rows=edf::At<const RosterRow*>(manager,team_native::kRows);
    if(!rows || count>kRosterCapacity || static_cast<unsigned>(observer)>=count ||
       rows[observer].relationCount!=count || !rows[observer].relations)return false;
    for(unsigned i=0;i<count;++i)
        if(!rows[i].head)return false;
    if(scratch.manager!=manager || scratch.rows!=rows) {
        for(auto& team:scratch.teams)team.epoch=0;
        scratch.manager=manager;
        scratch.rows=rows;
    }
    const bool nearest=edf::At<const void*>(callback,0)==image+kNearestRosterVtable;
    NearestRosterScan scan(callback,nearest);
    for(unsigned i=0;i<count;++i) {
        if(rows[observer].relations[i]==team_native::kEnemy)
            VisitRosterRow(rows[i],scratch.teams[i],callback,scan.Enabled() ? &scan : nullptr);
    }
    return true;
}

void __fastcall WalkSelectedRoster(void* manager,int observer,void* callback) {
    if(rosterReady.load(std::memory_order_acquire) && callback &&
       manager==edf::At<const void*>(image,team_native::kManager)) {
        const auto* const table=edf::At<const unsigned char*>(callback,0);
        if((table==image+kNearestRosterVtable || table==image+kSoldierRosterVtable) &&
           TryVisitRoster(manager,observer,callback))return;
    }
    reinterpret_cast<RosterWalk>(image+kHostileWalk)(manager,observer,callback);
}

void* __fastcall InsertRosterMember(void* row,const void* position,void* node) {
    RosterMutation mutation;
    return reinterpret_cast<void*(__fastcall*)(void*,const void*,void*)>(image+kRosterInsert)(row,position,node);
}
std::size_t __fastcall EraseRosterMember(void* row,const void* key) {
    RosterMutation mutation;
    return reinterpret_cast<std::size_t(__fastcall*)(void*,const void*)>(image+kRosterErase)(row,key);
}
void* __fastcall ConstructRoster(void* manager) {
    RosterMutation mutation;
    return reinterpret_cast<void*(__fastcall*)(void*)>(image+kRosterConstruct)(manager);
}

struct RosterRedirect {
    unsigned site,original;
    void* replacement;
    void* thunk{};
    unsigned char stock[5]{0xE8},call[5]{0xE8};
};

bool InstallRosterRedirects(bool nearestReady) noexcept {
    if(!nearestReady ||
       !NativeCodeMatches(0x5E0B70,0x5E0C72,0xE976DC69F86D8A89ull) ||
       !NativeCodeMatches(0x5E1C60,0x5E1CF2,0x1C44C065D87FF4E4ull) ||
       !NativeCodeMatches(0x5E03C0,0x5E03FC,0xA1740DAB80082318ull) ||
       !NativeCodeMatches(0x54C4B0,0x54C516,0x1023F8BA55E2B94Eull) ||
       !NativeCodeMatches(0x59773A,0x597780,0x33EAA4C410AA11ABull) ||
       !NativeCodeMatches(kSoldierRosterVisitor,0x598D07,0xC398307F9149D408ull))return false;
    RosterRedirect redirects[]{
        {0x5E0C52,kRosterInsert,reinterpret_cast<void*>(&InsertRosterMember)},
        {0x5E1CD8,kRosterErase,reinterpret_cast<void*>(&EraseRosterMember)},
        {0x2988B9,kRosterConstruct,reinterpret_cast<void*>(&ConstructRoster)},
        {0x70149F,kRosterConstruct,reinterpret_cast<void*>(&ConstructRoster)},
        {kNearestRosterSite,kHostileWalk,reinterpret_cast<void*>(&WalkSelectedRoster)},
        {kSoldierRosterSite,kHostileWalk,reinterpret_cast<void*>(&WalkSelectedRoster)},
    };
    bool ready=true;
    for(auto& r:redirects) {
        const auto stock=static_cast<std::int32_t>(r.original)-static_cast<std::int32_t>(r.site+5);
        std::memcpy(r.stock+1,&stock,sizeof(stock));
        auto* const site=image+r.site;
        if(!edf::Readable(site,sizeof(r.stock)) || std::memcmp(site,r.stock,sizeof(r.stock))!=0) {ready=false;break;}
        r.thunk=edf::AllocateNearThunk(site,r.replacement);
        if(!r.thunk) {ready=false;break;}
        const auto delta=reinterpret_cast<std::intptr_t>(r.thunk)-reinterpret_cast<std::intptr_t>(site+5);
        if(delta<INT32_MIN || delta>INT32_MAX) {ready=false;break;}
        const auto rel32=static_cast<std::int32_t>(delta);
        std::memcpy(r.call+1,&rel32,sizeof(rel32));
    }
    std::size_t installed=0;
    if(ready) {
        for(auto& r:redirects) {
            if(!edf::PatchCode(image+r.site,r.stock,r.call,sizeof(r.call)))break;
            ++installed;
        }
    }
    if(installed==std::size(redirects)) {
        rosterReady.store(true,std::memory_order_release);
        return true;
    }
    for(std::size_t i=0;i<std::size(redirects);++i) {
        auto& r=redirects[i];
        if(i<installed && !edf::PatchCode(image+r.site,r.call,r.stock,sizeof(r.stock))) {
            Log("AI native hostile roster: rollback failed at %x",r.site);
            continue;
        }
        if(r.thunk)VirtualFree(r.thunk,0,MEM_RELEASE);
    }
    return false;
}

constexpr unsigned kSoldierScoreStart=0x598C7E,kSoldierScoreEnd=0x598CE9,kSoldierScoreFunctionEnd=0x598D07;
constexpr unsigned char kSoldierScoreStock[]{
    0x0F,0xC6,0xD2,0xFF,0x0F,0x5C,0x98,0x90,0x00,0x00,0x00,0x0F,0x28,0xC3,0x0F,0x28,
    0xCB,0x0F,0xC6,0xCB,0xAA,0x0F,0xC6,0xC3,0x55,0x0F,0xC6,0xDB,0xE1,0xF3,0x0F,0x10,
    0xD8,0x0F,0x57,0xC0,0x0F,0xC6,0xDB,0xC6,0xF3,0x0F,0x10,0xD9,0x0F,0xC6,0xDB,0x27,
    0xF3,0x0F,0x10,0xDA,0x0F,0xC6,0xDB,0x39,0x0F,0x59,0xDB,0x0F,0x28,0xCB,0x0F,0xC6,
    0xCB,0x55,0xF3,0x0F,0x58,0xCB,0x0F,0xC6,0xDB,0xAA,0xF3,0x0F,0x58,0xCB,0x0F,0x2E,
    0xC8,0x7A,0x02,0x74,0x16,0x0F,0x2E,0xC1,0x77,0x09,0x0F,0x57,0xC0,0xF3,0x0F,0x51,
    0xC1,0xEB,0x08,0x0F,0x28,0xC1,0xE8,0xD5,0x1B,0xD4,0x00,
};
static_assert(sizeof(kSoldierScoreStock)==kSoldierScoreEnd-kSoldierScoreStart);
constexpr unsigned char kSoldierScorePacked[]{
    0x0F,0x5C,0x98,0x90,0x00,0x00,0x00, // subps xmm3,[rax+90h]
    0x0F,0xC6,0xD3,0xAF,                 // [w,w,dz,dz]
    0x0F,0xC6,0xDA,0x24,                 // [dx,dy,dz,w]
    0x0F,0x57,0xC0,                     // zero in xmm0 for the zero score and float comparison
    // Preserve the native squared sum and fall through to sqrtss for positive distances.
    0x0F,0x59,0xDB,0x0F,0x28,0xCB,0x0F,0xC6,0xCB,0x55,0xF3,0x0F,0x58,0xCB,0x0F,0xC6,
    0xDB,0xAA,0xF3,0x0F,0x58,0xCB,0x0F,0x2E,0xC8,0x76,0x06,0xF3,0x0F,0x51,0xC1,0xEB,
    0x38,
    // Unordered scores use sqrtss, zero keeps +0 and the stock fallback handles negative inputs.
    0x7A,0xF8,0x74,0x34,0x0F,0x28,0xC1,0xE8,0x01,0x1C,0xD4,0x00,0xEB,0x2A,
};

bool InstallSoldierScore() noexcept {
    if(!NativeCodeMatches(kSoldierRosterVisitor,kSoldierScoreFunctionEnd,0xC398307F9149D408ull))return false;
    // Leave the winner comparison at 0x598CE9 for both the stock path and Proteus' callback guard.
    unsigned char code[sizeof(kSoldierScoreStock)];
    static_assert(sizeof(kSoldierScorePacked)<sizeof(code));
    std::memset(code,0xCC,sizeof(code));
    std::memcpy(code,kSoldierScorePacked,sizeof(kSoldierScorePacked));
    return edf::PatchCode(image+kSoldierScoreStart,kSoldierScoreStock,code,sizeof(code));
}

bool InstallAvoidanceOverflow() noexcept {
    if(!edf::Matches(image,kAvoidanceOverflow-2,kAvoidanceFirstHeapSkip,sizeof(kAvoidanceFirstHeapSkip)) ||
       !edf::Matches(image,kAvoidanceDone,kAvoidanceDoneSig,sizeof(kAvoidanceDoneSig)) ||
       !NativeCodeMatches(kAvoidancePop,kAvoidanceDone,0x01EC66192E650870ull))return false;
    auto* const start=image+kAvoidanceOverflow;
    auto* const thunk=edf::AllocateNearThunk(start,reinterpret_cast<void*>(&PushAvoidanceNeighbor));
    if(!thunk)return false;
    constexpr auto next=kAvoidanceOverflowCall+5;
    constexpr auto size=kAvoidancePop-kAvoidanceOverflow;
    static_assert(kAvoidanceDone-kAvoidancePop<=127);
    unsigned char code[size]{
        0x48,0x8B,0xCB,                                      // mov rcx,rbx
        0xE8,0,0,0,0,                                       // call combined push and pop
        0xEB,static_cast<unsigned char>(kAvoidanceDone-kAvoidancePop),
    };
    static_assert(sizeof(code)==sizeof(kAvoidancePush.stock));
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(image+next);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        const auto relative=static_cast<std::int32_t>(delta);
        std::memcpy(code+kAvoidanceOverflowCall-kAvoidanceOverflow+1,&relative,sizeof(relative));
        if(edf::PatchCode(start,kAvoidancePush.stock,code,sizeof(code)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}

unsigned InstallAvoidanceHeap() noexcept {
    const auto redirect=[](const AvoidanceCall& site,unsigned stock,void* replacement) {
        if(!edf::Matches(image,site.call-5,site.stock,sizeof(site.stock)))return false;
        bool changed=false;
        edf::RedirectCall(image+site.call,image+stock,replacement,changed);
        return changed;
    };
    const bool siftMatches=NativeCodeMatches(kAvoidanceSift,kAvoidanceSiftEnd,0xD8C5803BAE3519BBull);
    const bool raiseMatches=NativeCodeMatches(kAvoidanceRaise,kAvoidanceRaiseEnd,0x9561D33EE8875421ull);
    // The first overflow builds the heap in-place and still reaches the original pop.
    const bool combined=siftMatches && raiseMatches && InstallAvoidanceOverflow();
    unsigned installed=combined;
    if(siftMatches) {
        for(const auto& site:kAvoidanceCalls)
            installed+=redirect(site,kAvoidanceSift,reinterpret_cast<void*>(&SiftAvoidanceNeighbors));
    }
    if(raiseMatches && !combined)
        installed+=redirect(kAvoidancePush,kAvoidanceRaise,reinterpret_cast<void*>(&RaiseAvoidanceNeighbor));
    return installed;
}

// The sweep already limits the sorted X axis. Compare both remaining axes before reporting a pair.
constexpr unsigned kAvoidanceScan=0xD5F62E,kAvoidanceScanNext=0xD5F666,kAvoidanceScanOverlap=0xD5F6C8;
constexpr unsigned kStockOverlapDisp=46,kPackedOverlapDisp=42;
constexpr unsigned char kAvoidanceScanEntry[]{0x45,0x8B,0x48,0x10,0x44,0x39,0x0A,0x73,0x38};
constexpr unsigned char kAvoidanceScanAdvance[]{0x45,0x85,0xFF,0x48,0x8D,0x4D,0xF7};
constexpr unsigned char kAvoidanceScanFound[]{0x48,0x89,0x55,0xE7,0x4C,0x89,0x45,0xDF,0x44,0x89,0x7D,0xD7};
constexpr unsigned char kAvoidanceScanStock[]{
    0x41,0x8B,0x58,0x08,0x41,0x8B,0x78,0x04,0x41,0x8B,0x70,0x18,0x45,0x8B,0x70,0x14,
    0x66,0x90,0x41,0x8B,0xCE,0x8B,0xC6,0x2B,0x42,0x08,0x2B,0x4A,0x04,0x0B,0xC8,0x8B,
    0x42,0x14,0x2B,0xC7,0x0B,0xC8,0x8B,0x42,0x18,0x2B,0xC3,0x0B,0xC8,0x7D,0x6B,0x48,
    0x83,0xC2,0x20,0x44,0x39,0x0A,0x72,0xDA,
};
constexpr unsigned char kAvoidanceScanPacked[]{
    0xF3,0x41,0x0F,0x7E,0x40,0x04,     // movq xmm0,[r8+4]    reference min Y/Z
    0xF3,0x41,0x0F,0x7E,0x48,0x14,     // movq xmm1,[r8+14h]  reference max Y/Z
    0x0F,0x28,0xD1,                    // movaps xmm2,xmm1    scan loop
    0xF3,0x0F,0x7E,0x5A,0x04,          // movq xmm3,[rdx+4]   candidate min Y/Z
    0x66,0x0F,0xFA,0xD3,               // psubd xmm2,xmm3     keep native 32-bit wraparound
    0xF3,0x0F,0x7E,0x5A,0x14,          // movq xmm3,[rdx+14h] candidate max Y/Z
    0x66,0x0F,0xFA,0xD8,               // psubd xmm3,xmm0
    0x0F,0x56,0xD3,                    // orps xmm2,xmm3
    0x0F,0x50,0xC2,                    // movmskps eax,xmm2
    0x85,0xC0,                         // test eax,eax
    0x74,static_cast<unsigned char>(kAvoidanceScanOverlap-kAvoidanceScan-kPackedOverlapDisp-1),
    0x48,0x83,0xC2,0x20,               // add rdx,20h
    0x44,0x39,0x0A,                    // cmp [rdx],r9d       native X cutoff
    0x72,static_cast<unsigned char>(12-52),
    0x0F,0x1F,0x40,0x00,               // nop
};
static_assert(sizeof(kAvoidanceScanStock)==kAvoidanceScanNext-kAvoidanceScan);
static_assert(sizeof(kAvoidanceScanPacked)==sizeof(kAvoidanceScanStock));
static_assert(kAvoidanceScanOverlap-kAvoidanceScan-kPackedOverlapDisp-1<=127);

bool InstallAvoidanceScan() noexcept {
    if(!edf::Matches(image,kAvoidanceScan-sizeof(kAvoidanceScanEntry),kAvoidanceScanEntry,sizeof(kAvoidanceScanEntry)) ||
       !edf::Matches(image,kAvoidanceScanNext,kAvoidanceScanAdvance,sizeof(kAvoidanceScanAdvance)) ||
       !edf::Matches(image,kAvoidanceScanOverlap,kAvoidanceScanFound,sizeof(kAvoidanceScanFound)))return false;
    return edf::PatchCode(image+kAvoidanceScan,kAvoidanceScanStock,kAvoidanceScanPacked,sizeof(kAvoidanceScanStock));
}

constexpr unsigned kStreamCall=0xE782CB,kStockStream=0xE776F0,kContactCommand=0x12FE960;
constexpr unsigned char kStreamCaller[]={0x4C,0x8D,0x4D,0x07,0x49,0x8B,0xD6,0x49,0x8B,0xCC,0xE8,0x20,0xF4,0xFF,0xFF};
constexpr unsigned char kStockStreamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
constexpr unsigned char kContactCommandSig[]={0x0F,0xB6,0x42,0x03,0x48,0x8B,0x4C,0xC1,0x08,0x48,0x8B,0x01,0x48,0xFF,0x60,0x18};
constexpr unsigned kStreamPayload=0x20;

struct ContactRange {
    unsigned char* block;
    std::uint16_t offset,firstCount;
    int count;
    const ContactRange* next;
};
static_assert(offsetof(ContactRange,count)==0xC && offsetof(ContactRange,next)==0x10);
using ContactStreamFn=void(__fastcall*)(void*,void*,const ContactRange*,void*);
using ContactCommandFn=void(__fastcall*)(void*,unsigned char*);

template<bool tracing>
void ContactRanges(void* storage,void* dispatcher,const ContactRange* range,void* writer) {
    const auto stock=reinterpret_cast<ContactStreamFn>(image+kStockStream);
    for(;range;range=range->next) {
        int inBlock=range->firstCount;
        if(!range->count || !inBlock)continue;
        auto* block=range->block;
        if(!block || range->count<inBlock) {
            stock(storage,dispatcher,range,writer);
            return;
        }
        auto* command=block+kStreamPayload+range->offset;
        int remaining=range->count-inBlock;
        for(;;) {
            for(int i=0;i<inBlock;++i) {
                if(command[2]&1) {
                    const unsigned kind=command[3];
                    if constexpr(tracing)perfprobe::ObserveContactCommand(kind);
                    auto* const handler=edf::At<void*>(dispatcher,(kind+1)*sizeof(void*));
                    edf::At<ContactCommandFn>(edf::At<const void*>(handler,0),0x18)(handler,command);
                } else {
                    // Deferred records still use the original writer and its allocation path.
                    const auto offset=static_cast<std::uint16_t>(command-(block+kStreamPayload));
                    const ContactRange single{block,offset,1,1,nullptr};
                    stock(storage,dispatcher,&single,writer);
                }
                // Subscribers run first: read the record length after they have returned.
                command+=edf::At<std::uint16_t>(command,0);
            }
            if(!remaining)break;
            block=edf::At<unsigned char*>(block,8);
            if(!block)break;
            const int available=edf::At<std::uint16_t>(block,0);
            inBlock=available<remaining ? available : remaining;
            if(!inBlock)break;
            remaining-=inBlock;
            command=block+kStreamPayload;
            _mm_prefetch(edf::At<const char*>(block,8),_MM_HINT_NTA);
            _mm_prefetch(reinterpret_cast<const char*>(block+0x80),_MM_HINT_NTA);
        }
    }
}

void __fastcall DispatchContactRanges(void* storage,void* dispatcher,const ContactRange* range,void* writer) {
    if(perfprobe::ContactCommandsActive())ContactRanges<true>(storage,dispatcher,range,writer);
    else ContactRanges<false>(storage,dispatcher,range,writer);
}

bool InstallContactStream() noexcept {
    if(!edf::Matches(image,kStreamCall-10,kStreamCaller,sizeof(kStreamCaller)) ||
       !edf::Matches(image,kStockStream,kStockStreamSig,sizeof(kStockStreamSig)) ||
       !edf::Matches(image,kContactCommand,kContactCommandSig,sizeof(kContactCommandSig)))return false;
    bool changed=false;
    edf::RedirectCall(image+kStreamCall,image+kStockStream,reinterpret_cast<void*>(&DispatchContactRanges),changed);
    return changed;
}

constexpr unsigned kClingGate=0x6DAC9B,kClingBody=0x6DACD5;
constexpr unsigned char kClingFirstCall[]={0xE8,0x45,0x26,0xAD,0x00};
constexpr unsigned char kClingDecodeCall[]={0xE8,0xAB,0x77,0x6F,0x00};
constexpr unsigned char kClingGateStock[]={
    0x80,0x78,0x28,0x00,0x0F,0x85,0x87,0x01,0x00,0x00,0x0F,0xB6,0x43,0x10,
    0x84,0xC0,0x0F,0x84,0x7B,0x01,0x00,0x00,0x3C,0x02,0x76,0x08,0x3C,0x04,
    0x0F,0x85,0x6F,0x01,0x00,0x00,
};
constexpr unsigned char kClingGatePatched[]={
    0x48,0x89,0x44,0x24,0x30, // Save the first lookup in the existing body slot.
    0x80,0x78,0x28,0,0x75,0x10,0x0F,0xB6,0x43,0x10,0x84,0xC0,0x74,0x08,
    0x3C,0x02,0x76,0x0B,0x3C,0x04,0x74,0x07,0xE9,0x71,0x01,0x00,0x00,0x90,0x90,
};
constexpr unsigned char kClingBodyStock[]={
    0x48,0x8B,0x0D,0x7C,0x7C,0x9D,0x01,0x4C,0x8B,0xE0,0x48,0x83,0xC1,0x10,0x41,0x83,
    0x7F,0x20,0x00,0x48,0x0F,0x44,0xF5,0x8B,0x14,0x1E,0xE8,0xEC,0x25,0xAD,0x00,
};
constexpr unsigned char kClingBodyPatched[]={
    0x4C,0x8B,0xE0,0x48,0x8B,0x44,0x24,0x30,0xEB,0x15, // Preserve the manifold result, then reuse the body.
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
};
static_assert(sizeof(kClingGateStock)==sizeof(kClingGatePatched));
static_assert(sizeof(kClingBodyStock)==sizeof(kClingBodyPatched));

bool InstallClingBodyReuse() noexcept {
    if(!edf::Matches(image,kClingGate-5,kClingFirstCall,sizeof(kClingFirstCall)) ||
       !edf::Matches(image,kClingBody-5,kClingDecodeCall,sizeof(kClingDecodeCall)) ||
       !edf::Matches(image,kClingGate,kClingGateStock,sizeof(kClingGateStock)) ||
       !edf::Matches(image,kClingBody,kClingBodyStock,sizeof(kClingBodyStock)))return false;
    // Nothing between the two lookups changes the event side or body mapping: the call only decodes its manifold.
    if(!edf::PatchCode(image+kClingGate,kClingGateStock,kClingGatePatched,sizeof(kClingGateStock)))return false;
    if(edf::PatchCode(image+kClingBody,kClingBodyStock,kClingBodyPatched,sizeof(kClingBodyStock)))return true;
    edf::PatchCode(image+kClingGate,kClingGatePatched,kClingGateStock,sizeof(kClingGateStock));
    return false;
}

constexpr unsigned kDecodeContact=0xDD2480,kUnpackContact=0xDD4230,kContactWorldVtable=0x18BC9C0;
constexpr unsigned kIsBodyValid=0xDAC200,kBodyById=0xDAB170,kMotionById=0xDABA20;
constexpr unsigned kContactDecoders[]{0x6DACD0,0x6F127A,0x11B9552};
static_assert(sizeof(kAttachedBasisCalls)/sizeof(kAttachedBasisCalls[0])+
              sizeof(kAvoidanceCalls)/sizeof(kAvoidanceCalls[0])+
              sizeof(kContactDecoders)/sizeof(kContactDecoders[0])+13==kNativeBodyMathSites);

using DecodeContactFn=const void*(__fastcall*)(const unsigned char*,const unsigned char*,void*);
using UnpackContactFn=void(__fastcall*)(const void*,const void*,const void*,const void*,unsigned,void*);

__forceinline bool IsContactWorld(const void* table) noexcept {
    return table==image+kContactWorldVtable && edf::At<const void*>(table,0x68)==image+kIsBodyValid &&
           edf::At<const void*>(table,0x80)==image+kBodyById && edf::At<const void*>(table,0xF8)==image+kMotionById;
}

__forceinline const unsigned char* FindContactBody(const unsigned char* world,std::uint32_t id) noexcept {
    const auto index=id&0xFFFFFFu;
    if(index==0xFFFFFFu || index>=edf::At<std::uint32_t>(world,0x40))return nullptr;
    const auto* const body=edf::At<const unsigned char*>(world,0x38)+index*0xB0ull;
    return (body[0x54]&3) && edf::At<std::uint32_t>(body,0x50)==id ? body : nullptr;
}

const void* __fastcall DecodeContact(const unsigned char* event,const unsigned char* world,void* out) noexcept {
    if(edf::At<std::uint32_t>(event,0x30)!=0xFFFFFFFFu)return event+0x30;
    if(!IsContactWorld(edf::At<const void*>(world,0x20)))
        return reinterpret_cast<DecodeContactFn>(image+kDecodeContact)(event,world,out);

    // Validate the full generation ID and retain the row for its motion lookup.
    const auto* const bodyB=FindContactBody(world,edf::At<std::uint32_t>(event,0xC));
    if(!bodyB)return nullptr;
    const auto* const bodyA=FindContactBody(world,edf::At<std::uint32_t>(event,8));
    if(!bodyA)return nullptr;
    const auto* const motions=edf::At<const unsigned char*>(world,0x178);
    const auto* const motionB=motions+static_cast<std::ptrdiff_t>(edf::At<std::int32_t>(bodyB,0x80))*0x80;
    const auto* const motionA=motions+static_cast<std::ptrdiff_t>(edf::At<std::int32_t>(bodyA,0x80))*0x80;
    const auto* const cache=edf::At<const void*>(event,0x20);
    const auto* const compressed=edf::At<const unsigned char*>(cache,0x20)+edf::At<std::uint16_t>(cache,0x28);
    reinterpret_cast<UnpackContactFn>(image+kUnpackContact)(world+0x540,compressed,motionB,motionA,
                                                          (world[0x898]&0x18)==0,out);
    return out;
}

unsigned InstallContactDecoders() noexcept {
    constexpr unsigned char decoderSig[]{0x40,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x30,
        0x83,0x79,0x30,0xFF,0x48,0x8D,0x41,0x30};
    constexpr unsigned char unpackCall[]{0xE8,0x84,0x7D,0xF9,0xFF};
    constexpr unsigned char unpackThunk[]{0x4C,0x8B,0xD2,0x48,0x8B,0xD1,0x49,0x8B,0xCA,0xE9,0x42,0x9F,0x06,0};
    constexpr unsigned char isBodyValidSig[]{0x8B,0xC2,0x25,0xFF,0xFF,0xFF,0,0x3D,0xFF,0xFF,0xFF,0,
        0x74,0x23,0x3B,0x41,0x20,0x73,0x1E,0x4C,0x69,0xC0,0xB0,0,0,0,0x4C,0x03,0x41,0x18,
        0x41,0xF6,0x40,0x54,0x03,0x74,0x0C,0x41,0x39,0x50,0x50,0x75,0x06,0xB8,1,0,0,0,0xC3,0x33,0xC0,0xC3};
    constexpr unsigned char bodyByIdSig[]{0x8B,0xC2,0x25,0xFF,0xFF,0xFF,0,0x48,0x69,0xC0,0xB0,0,0,0,
        0x48,0x03,0x41,0x18,0xC3};
    constexpr unsigned char motionByIdSig[]{0x48,0x63,0xC2,0x48,0xC1,0xE0,7,0x48,0x03,0x81,0x58,1,0,0,0xC3};
    if(!edf::Matches(image,kDecodeContact,decoderSig,sizeof(decoderSig)) ||
       !edf::Matches(image,0xDD2557,unpackCall,sizeof(unpackCall)) ||
       !edf::Matches(image,0xD6A2E0,unpackThunk,sizeof(unpackThunk)) ||
       !edf::Matches(image,kIsBodyValid,isBodyValidSig,sizeof(isBodyValidSig)) ||
       !edf::Matches(image,kBodyById,bodyByIdSig,sizeof(bodyByIdSig)) ||
       !edf::Matches(image,kMotionById,motionByIdSig,sizeof(motionByIdSig)))return 0;
    unsigned installed=0;
    for(const auto call:kContactDecoders) {
        if(image[call]!=0xE8)continue;
        bool changed=false;
        edf::RedirectCall(image+call,image+kDecodeContact,reinterpret_cast<void*>(&DecodeContact),changed);
        installed+=changed;
    }
    return installed;
}

constexpr unsigned kBodyQuaternionCall=0xE51651,kNativeBodyQuaternion=0x98BF30;
constexpr unsigned char kBodyQuaternionStock[]{0xE8,0xDA,0xA8,0xB3,0xFF};

bool InstallStaticBodyQuaternionSkip() noexcept {
    // The changed-pose path leaves the hknpBody in rdi until it notifies the world.
    constexpr unsigned char setter[]{
        0x48,0x8D,0x4C,0x24,0x30,0x48,0x8B,0xD6,0x4C,0x89,0xBC,0x24,0xA8,0,0,0,
        0xE8,0xDA,0xA8,0xB3,0xFF,0x4D,0x8B,0xC6,0x48,0x8B,0xD6,0x48,0x8B,0xCF,
        0xE8,0x5C,0x80,0xF9,0xFF,0x8B,0x84,0x24,0xB0,0,0,0,0x4C,0x8D,0x44,0x24,0x30,
        0x45,0x33,0xC9,0x89,0x44,0x24,0x20,0x48,0x8B,0xD7,0x49,0x8D,0x4D,0xE8,0xE8,0xDD,0xEF,0xF5,0xFF,
    };
    constexpr unsigned char motionId[]{0x8B,0xBA,0x80,0,0,0};
    constexpr unsigned char motionBranch[]{0x85,0xFF,0x0F,0x84,0x2E,0x07,0,0};
    constexpr unsigned char staticUpdate[]{0x49,0x8B,0xCE,0xE8,0x73,0x88,0x03,0};
    constexpr unsigned char dynamicQuaternion[]{0x4D,0x85,0xC9,0x0F,0x84,0x64,1,0,0,0x41,0x0F,0x10,0x9E,
        0xA0,0,0,0,0x41,0x0F,0x10,0x29};
    constexpr unsigned char quaternionSig[]{0x40,0x53,0x48,0x83,0xEC,0x10,0xF3,0x0F,0x10,0x1A,0x0F,0x57,0xC0};
    if(!edf::Matches(image,0xE51641,setter,sizeof(setter)) ||
       !edf::Matches(image,0xDB06CB,motionId,sizeof(motionId)) ||
       !edf::Matches(image,0xDB07DF,motionBranch,sizeof(motionBranch)) ||
       !edf::Matches(image,0xDB0F15,staticUpdate,sizeof(staticUpdate)) ||
       !edf::Matches(image,0xDB0911,dynamicQuaternion,sizeof(dynamicQuaternion)) ||
       !edf::Matches(image,kNativeBodyQuaternion,quaternionSig,sizeof(quaternionSig)))return false;

    // Motion ID zero goes directly to the static update without reading the supplied quaternion.
    // Use the home space for MXCSR and keep the native math when FP exceptions are unmasked.
    unsigned char code[]{
        0x83,0xBF,0x80,0,0,0,0,          // cmp dword ptr [rdi+80h],0
        0x75,0x16,                     // jne stock
        0x0F,0xAE,0x5C,0x24,0x08,       // stmxcsr [rsp+8]
        0x8B,0x44,0x24,0x08,            // mov eax,[rsp+8]
        0x25,0x80,0x1F,0,0,            // and eax,1f80h
        0x3D,0x80,0x1F,0,0,            // cmp eax,1f80h
        0x75,0x01,0xC3,                // jne stock; ret
        0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0,
    };
    auto* const site=image+kBodyQuaternionCall;
    auto* const original=image+kNativeBodyQuaternion;
    std::memcpy(code+33,&original,sizeof(original));
    auto* const thunk=edf::AllocateNearCode(site,code,sizeof(code));
    if(!thunk)return false;
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(site+5);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        unsigned char call[]{0xE8,0,0,0,0};
        const auto rel32=static_cast<std::int32_t>(delta);
        std::memcpy(call+1,&rel32,sizeof(rel32));
        if(edf::PatchCode(site,kBodyQuaternionStock,call,sizeof(call)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}



constexpr unsigned kBodySignalStart=0xE51683,kBodySignalDone=0xE51793;
constexpr unsigned char kBodySignalStock[]{
    0x49,0x8B,0x85,0x60,0x07,0,0,0x4D,0x8D,0xB5,0x60,0x07,0,0,
    0x48,0x83,0xE0,0xFD,0x48,0x83,0xC8,0x01,0x48,0x8B,0xC8,
    0x49,0x89,0x85,0x60,0x07,0,0,0x48,0x83,0xE1,0xFC,0x74,0x50,0x0F,0x1F,0x80,0,0,0,0,
};
constexpr unsigned char kBodySignalEmptyGate[]{
    0x4D,0x8D,0xB5,0x60,0x07,0,0,  // lea r14,[r13+760h]
    0x49,0x8B,0x06,                // mov rax,[r14]
    0x48,0x85,0xC0,0x75,0x0E,      // test rax,rax; jne dispatch
    0x49,0x83,0xBD,0xB8,0x07,0,0,0,// cmp qword ptr [r13+7b8h],0
    0x0F,0x84,0xF3,0,0,0,          // je 0xE51793, the shared epilogue
    0x24,0xFD,0x0C,0x01,           // dispatch: clear bit 1 and set bit 0
    0x48,0x8B,0xC8,0x49,0x89,0x06, // mov rcx,rax; mov [r14],rax
    0x48,0x83,0xE1,0xFC,0x74,0x49, // and rcx,-4; je 0xE516F9
};
static_assert(sizeof(kBodySignalStock)==sizeof(kBodySignalEmptyGate));
static_assert(kBodySignalStart+sizeof(kBodySignalStock)==0xE516B0);
static_assert(kBodySignalStart+29+kBodySignalEmptyGate[25]==kBodySignalDone);

bool InstallEmptyBodySignalGate() noexcept {
    constexpr unsigned char second[]{
        0x49,0x8B,0x85,0xB8,0x07,0,0,0x48,0x83,0xE0,0xFD,0x48,0x83,0xC8,1,0x48,0x8B,0xC8,
        0x49,0x89,0x85,0xB8,0x07,0,0,0x48,0x83,0xE1,0xFC,0x74,0x4E,
    };
    constexpr unsigned char epilogue[]{
        0x0F,0x28,0x74,0x24,0x50,0x0F,0x28,0x7C,0x24,0x40,0x48,0x83,0xC4,0x60,0x41,0x5E,0x41,0x5D,
        0x5F,0x5E,0x5B,0xC3,
    };
    if(!edf::Matches(image,0xE5170B,second,sizeof(second)) ||
       !edf::Matches(image,kBodySignalDone,epilogue,sizeof(epilogue)))return false;
    // The empty heads have no dispatch state. A listener or any flag keeps both original traversals.
    // The shared epilogue is also used by the stock tolerance check. r12 and r15 have only been saved.
    return edf::PatchCode(image+kBodySignalStart,kBodySignalStock,kBodySignalEmptyGate,sizeof(kBodySignalStock));
}



constexpr unsigned kMotionSearch=0xE151B5,kMotionSearchHit=0xE151F1,kMotionSearchMiss=0xE15213;
constexpr unsigned kMotionEntrySize=0x70;
constexpr int kMotionSearchLimit=0xFFFF;
bool boundedMotionSearch=false;
constexpr unsigned char kMotionSearchStock[]{
    0x45,0x33,0xED,0x44,0x39,0x6D,0,0x75,0x5E,
    0x4C,0x63,0x5B,0x48,0x45,0x8B,0xD5,0x4C,0x8B,0x4B,0x40,0x4D,0x85,0xDB,0x7E,0x4E,
    0x45,0x8B,0xC5,0x4C,0x2B,0xCD,0x0F,0x1F,0x44,0,0,
    0x49,0x8B,0xD5,0x48,0x8B,0xCD,0x8B,0x01,0x41,0x39,0x04,0x09,0x75,0x26,
    0x48,0xFF,0xC2,0x48,0x83,0xC1,0x04,0x48,0x83,0xFA,0x1C,0x7C,0xEB,0x41,0x83,0xFA,0xFF,0x74,0x22,
    0x66,0x45,0x89,0x14,0x24,0x49,0x8B,0xC4,0x48,0x83,0xC4,0x28,0x41,0x5D,0x41,0x5C,0x5D,0x5B,0xC3,
    0x41,0xFF,0xC2,0x49,0xFF,0xC0,0x49,0x83,0xC1,0x70,0x4D,0x3B,0xC3,0x7C,0xBD,
};
static_assert(kMotionSearch-9+sizeof(kMotionSearchStock)==kMotionSearchMiss);

__forceinline __m128i MotionBytes(const unsigned char* row) noexcept {
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(row));
}

__forceinline bool SameMotionProperties(const unsigned char* row,const unsigned char* key,__m128i first) noexcept {
    if(_mm_movemask_epi8(_mm_cmpeq_epi32(MotionBytes(row),first))!=0xFFFF)return false;
    const auto a=_mm_xor_si128(MotionBytes(row+0x10),MotionBytes(key+0x10));
    const auto b=_mm_xor_si128(MotionBytes(row+0x20),MotionBytes(key+0x20));
    const auto c=_mm_xor_si128(MotionBytes(row+0x30),MotionBytes(key+0x30));
    const auto d=_mm_xor_si128(MotionBytes(row+0x40),MotionBytes(key+0x40));
    const auto e=_mm_xor_si128(MotionBytes(row+0x50),MotionBytes(key+0x50));
    const auto f=_mm_xor_si128(MotionBytes(row+0x60),MotionBytes(key+0x60));
    const auto changed=_mm_or_si128(_mm_or_si128(_mm_or_si128(a,b),_mm_or_si128(c,d)),_mm_or_si128(e,f));
    return _mm_movemask_epi8(_mm_cmpeq_epi32(changed,_mm_setzero_si128()))==0xFFFF;
}

__declspec(noinline) int FindMotionTail(const unsigned char* rows,int i,int count,
                                            const unsigned char* key) noexcept {
    const auto first=MotionBytes(key);
    for(;i<=count-4;i+=4,rows+=4*kMotionEntrySize) {
        const auto w0=edf::At<std::uint32_t>(rows,0),w1=edf::At<std::uint32_t>(rows,kMotionEntrySize);
        const auto w2=edf::At<std::uint32_t>(rows,2*kMotionEntrySize),w3=edf::At<std::uint32_t>(rows,3*kMotionEntrySize);
        if((w0&w1&w2&w3)!=0)continue;
        // This path is called only when the key's exclusive word is zero.
        if(!w0 && SameMotionProperties(rows,key,first))return i;
        if(!w1 && SameMotionProperties(rows+kMotionEntrySize,key,first))return i+1;
        if(!w2 && SameMotionProperties(rows+2*kMotionEntrySize,key,first))return i+2;
        if(!w3 && SameMotionProperties(rows+3*kMotionEntrySize,key,first))return i+3;
    }
    for(;i<count;++i,rows+=kMotionEntrySize)
        if(SameMotionProperties(rows,key,first))return i;
    return -1;
}

int __fastcall FindMotionProperties(const unsigned char* rows,int count,const unsigned char* key) noexcept {
    if(count<=0)return -1;
    if(count>kMotionSearchLimit && boundedMotionSearch)count=kMotionSearchLimit;
    const auto first=MotionBytes(key);
    // Most presets are in the first few slots. Large runs of exclusive and free rows use the tail scan.
    const int prefix=count<32 ? count : 32;
    for(int i=0;i<prefix;++i,rows+=kMotionEntrySize)
        if(SameMotionProperties(rows,key,first))return i;
    return prefix==count ? -1 : FindMotionTail(rows,prefix,count,key);
}

bool InstallMotionPropertiesSearch() noexcept {
    if(!edf::Matches(image,kMotionSearch-9,kMotionSearchStock,sizeof(kMotionSearchStock)))return false;
    auto* const site=image+kMotionSearch;
    auto* const thunk=edf::AllocateNearThunk(site,reinterpret_cast<void*>(&FindMotionProperties));
    if(!thunk)return false;
    unsigned char code[]{
        0x48,0x8B,0x4B,0x40,0x8B,0x53,0x48,0x4C,0x8B,0xC5, // table, count, key
        0xE8,0,0,0,0,                     // call scanner
        0x85,0xC0,0x78,0x4B,               // test eax,eax; js stock insertion
        0x44,0x8B,0xD0,0xEB,0x24,          // mov r10d,eax; jmp stock ID store
    };
    static_assert(kMotionSearch+19+0x4B==kMotionSearchMiss && kMotionSearch+sizeof(code)+0x24==kMotionSearchHit);
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(site+15);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        const auto displacement=static_cast<std::int32_t>(delta);
        std::memcpy(code+11,&displacement,sizeof(displacement));
        if(edf::PatchCode(site,kMotionSearchStock+9,code,sizeof(code)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}



constexpr unsigned kMotionAdd=0xE15190,kMotionReserve=0xE15C40;
constexpr int kMotionSlots=kMotionSearchLimit;   // ID 0xFFFF is the failure sentinel.
constexpr unsigned kMotionPop=0xE152B5;
constexpr unsigned char kMotionPopStock[]{
    0x81,0xFF,0xFF,0xFF,0xFF,0,0x74,0x3B,0x0F,0x1F,0,0x48,0x8B,0x4B,0x40,0x44,0x8B,0xCF,
    0x49,0x6B,0xD1,0x70,0x8B,0x0C,0x0A,0x8D,0x90,0,0,0,1,0x33,0xC8,0x81,0xE1,0xFF,0xFF,0xFF,0,
    0x33,0xCA,0xF0,0x0F,0xB1,0x4B,0x5C,0x74,0x3F,0x8B,0x43,0x5C,0x8B,0xF8,0x81,0xE7,0xFF,0xFF,0xFF,0,
    0x81,0xFF,0xFF,0xFF,0xFF,0,0x75,0xC8,
};
constexpr unsigned kMotionRetryCompare=0xE152F0-kMotionPop;
constexpr unsigned kMotionGrowthCall=0x11AE93C;
constexpr unsigned kMotionObjectCall=0x11ACF89;
constexpr unsigned kMotionWeakStart=0x11AD047,kMotionWeakExpire=0x11AD060,kMotionWeakCreate=0x11AD080;
constexpr unsigned char kMotionWeakStock[]{
    0x74,0x2B,0x41,0x8B,0x40,0x08,0x85,0xC0,0x74,0x23,0x8D,0x50,1,0xF0,0x41,0x0F,0xB1,0x50,8,
    0x74,0x10,0x85,0xC0,0x75,0xF1,0x49,0x89,0x36,0x4D,0x89,0x4E,8,0xE9,0x71,1,0,0,
};
constexpr unsigned char kMotionWeakRenew[]{
    0x74,0x17,0x41,0x8B,0x40,0x08,0x85,0xC0,0x74,0x0F,0x8D,0x50,1,0xF0,0x41,0x0F,0xB1,0x50,8,
    0x74,0x10,0x85,0xC0,0x75,0xF1,
    0x83,0xBC,0x24,0x98,0,0,0,5, // cmp dword ptr [rsp+98h],5
    0x72,0x16,0xEB,0x08,         // jb create wrapper; jmp stock empty result
};
static_assert(sizeof(kMotionWeakStock)==sizeof(kMotionWeakRenew));
static_assert(kMotionWeakStart+2+kMotionWeakRenew[1]==kMotionWeakExpire);
static_assert(kMotionWeakExpire+10+kMotionWeakRenew[34]==kMotionWeakCreate);

constexpr unsigned kMotionRelease=0xE15670,kMotionReleaseCall=0xDA90CB;
using AddMotionFn=std::uint16_t*(__fastcall*)(unsigned char*,std::uint16_t*,const unsigned char*);
using ReserveMotionFn=int*(__fastcall*)(unsigned char*,int*,int);
using ReleaseMotionFn=void(__fastcall*)(unsigned char*,std::uint16_t);

int* __fastcall CapMotionGrowth(unsigned char* library,int* out,int wanted) {
    if(edf::At<int>(library,0x48)>=kMotionSlots) {
        *out=static_cast<int>(0x80040200u);   // The native reserve function's failure value.
        return out;
    }
    return reinterpret_cast<ReserveMotionFn>(image+kMotionReserve)(library,out,wanted<kMotionSlots ? wanted : kMotionSlots);
}

std::uint16_t* __fastcall AddObjectMotionProperties(unsigned char* library,std::uint16_t* out,const unsigned char* key) {
    auto* const result=reinterpret_cast<AddMotionFn>(image+kMotionAdd)(library,out,key);
    if(*out!=kMotionSlots || edf::At<int>(library,0x48)<kMotionSlots)return result;

    static std::atomic<bool> reported{false};
    if(!reported.load(std::memory_order_relaxed) && !reported.exchange(true,std::memory_order_relaxed))
        Log("PHYSICS motion property IDs exhausted: using built-in presets for new objects");

    // These constructors consume the ID immediately. Only the built-in slots outlive both
    // xgs-owned bodies and native controllers that do not retain an xgs property handle.
    const auto* row=edf::At<const unsigned char*>(library,0x40);
    for(std::uint16_t i=0;i<5;++i,row+=kMotionEntrySize) {
        if(!std::memcmp(row+4,key+4,kMotionEntrySize-4)) {
            *out=i;
            return result;
        }
    }
    // The native stationary, dynamic and zero-gravity presets remain valid past exhaustion.
    *out=edf::At<float>(key,0x10)==0.0f && edf::At<float>(key,0x14)==0.0f ? 0 :
         edf::At<float>(key,8)==0.0f ? 2 : 1;
    return result;
}

void __fastcall ReleaseMotionProperties(unsigned char* library,std::uint16_t id) {
    // Native controllers can hold a preset after exhaustion, but unlike xgs they use the raw release API.
    if((id<5 || id==kMotionSlots) && edf::At<int>(library,0x48)>=kMotionSlots)return;
    reinterpret_cast<ReleaseMotionFn>(image+kMotionRelease)(library,id);
}

bool InstallMotionIdLimit() noexcept {
    enum class Target { add,grow,release };
    struct Call {
        unsigned site;
        Target target;
        unsigned char stock[5];
        unsigned char code[5]{0xE8,0,0,0,0};
    };
    Call calls[]{
        {kMotionObjectCall,Target::add,{0xE8,0x02,0x82,0xC6,0xFF}},
        {0xDE39E2,Target::add,{0xE8,0xA9,0x17,0x03,0}},
        {0xE167E6,Target::add,{0xE8,0xA5,0xE9,0xFF,0xFF}},
        {kMotionGrowthCall,Target::grow,{0xE8,0xFF,0x72,0xC6,0xFF}},
        {kMotionReleaseCall,Target::release,{0xE8,0xA0,0xC5,0x06,0}},
    };
    constexpr unsigned last=static_cast<unsigned>(sizeof(calls)/sizeof(calls[0]))-1;
    constexpr unsigned char savedId[]{0x44,0x0F,0xB7,0x8C,0x24,0x80,0,0,0,0x44,0x89,0x8C,0x24,0x98,0,0,0};
    if(!edf::Matches(image,kMotionPop,kMotionPopStock,sizeof(kMotionPopStock)) ||
       !edf::Matches(image,kMotionObjectCall+5,savedId,sizeof(savedId)) ||
       !edf::Matches(image,kMotionWeakStart,kMotionWeakStock,sizeof(kMotionWeakStock)))return false;
    for(const auto& call:calls)
        if(!edf::Matches(image,call.site,call.stock,sizeof(call.stock)))return false;
    void* const thunks[]{
        edf::AllocateNearThunk(image+kMotionObjectCall,reinterpret_cast<void*>(&AddObjectMotionProperties)),
        edf::AllocateNearThunk(image+kMotionGrowthCall,reinterpret_cast<void*>(&CapMotionGrowth)),
        edf::AllocateNearThunk(image+kMotionReleaseCall,reinterpret_cast<void*>(&ReleaseMotionProperties)),
    };
    if(!thunks[0] || !thunks[1] || !thunks[2]) {
        for(auto* thunk:thunks)if(thunk)VirtualFree(thunk,0,MEM_RELEASE);
        return false;
    }

    unsigned installed=0;
    for(;installed<=last;++installed) {
        auto& call=calls[installed];
        const auto delta=reinterpret_cast<std::intptr_t>(thunks[static_cast<unsigned>(call.target)])-
                         reinterpret_cast<std::intptr_t>(image+call.site+sizeof(call.code));
        if(delta<INT32_MIN || delta>INT32_MAX)break;
        const auto offset=static_cast<std::int32_t>(delta);
        std::memcpy(call.code+1,&offset,sizeof(offset));
        if(!edf::PatchCode(image+call.site,call.stock,call.code,sizeof(call.code)))break;
    }
    unsigned char guarded[sizeof(kMotionPopStock)];
    std::memcpy(guarded,kMotionPopStock,sizeof(guarded));
    guarded[4]=guarded[kMotionRetryCompare+4]=0;
    guarded[6]=0x73;                     // jae: the sentinel and higher indices cannot be allocated.
    guarded[kMotionRetryCompare+6]=0x72; // jb: recheck the same bound after a failed compare/exchange.
    // Preset wrappers leave their weak map entry behind when the last owner is destroyed.
    // Renew expired preset wrappers in the stock creation path before allowing those IDs as fallbacks.
    const bool weakPatched=installed>last && edf::PatchCode(image+kMotionWeakStart,kMotionWeakStock,
                                                           kMotionWeakRenew,sizeof(kMotionWeakStock));
    if(weakPatched && edf::PatchCode(image+kMotionPop,kMotionPopStock,guarded,sizeof(guarded)))return true;

    bool restored=!weakPatched || edf::PatchCode(image+kMotionWeakStart,kMotionWeakRenew,kMotionWeakStock,
                                               sizeof(kMotionWeakStock));
    while(installed) {
        const auto& call=calls[--installed];
        restored=edf::PatchCode(image+call.site,call.code,call.stock,sizeof(call.stock)) && restored;
    }
    if(restored)for(auto* thunk:thunks)VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}



constexpr unsigned kMotionGrowSignal=0xE15236,kMotionGrowReload=0xE152AA;
constexpr unsigned kMotionGrowClear=0xE152A2,kMotionGrowSlot=0x1AE2EC0,kMotionGrowCallback=0x11AE920;
constexpr unsigned char kMotionGrowStock[]{
    0x48,0x8B,0x43,0x38,0x4C,0x8D,0x73,0x38,0x48,0x83,0xE0,0xFD,0x48,0x83,0xC8,1,
    0x48,0x8B,0xC8,0x48,0x89,0x43,0x38,0x48,0x83,0xE1,0xFC,0x74,0x4F,
    0x0F,0x1F,0x40,0,0x66,0x0F,0x1F,0x84,0,0,0,0,0,
};
static_assert(kMotionGrowSignal+sizeof(kMotionGrowStock)==0xE15260);

bool __fastcall SkipExhaustedMotionGrowth(const unsigned char* library) noexcept {
    if(edf::At<int>(library,0x48)<kMotionSlots)return false;
    const auto head=edf::At<std::uintptr_t>(library,0x38);
    if(!head)return true;
    if(head&3u)return false;
    const auto* const node=reinterpret_cast<const void*>(head);
    const auto* const vtable=edf::At<const void*>(node,0);
    // The single stock listener only calls the capped reserve function with a larger request.
    return !edf::At<std::uintptr_t>(node,8) && vtable==image+kMotionGrowSlot &&
           edf::At<const void*>(vtable,0x18)==image+kMotionGrowCallback;
}

bool InstallExhaustedMotionGrowthGate() noexcept {
    constexpr unsigned char callbackStart[]{
        0x48,0x89,0x4C,0x24,8,0x48,0x83,0xEC,0x28,0x44,0x8B,0x42,0x48,0x48,0x8B,0xCA,
        0x41,0x81,0xC0,0,4,0,0,0x48,0x8D,0x54,0x24,0x30,
    };
    constexpr unsigned char callbackEnd[]{0x48,0x83,0xC4,0x28,0xC3};
    if(!edf::Matches(image,kMotionGrowSignal,kMotionGrowStock,sizeof(kMotionGrowStock)) ||
       !edf::Matches(image,kMotionGrowCallback,callbackStart,sizeof(callbackStart)) ||
       !edf::Matches(image,kMotionGrowthCall+5,callbackEnd,sizeof(callbackEnd)))return false;
    auto* const site=image+kMotionGrowSignal;
    auto* const thunk=edf::AllocateNearThunk(site,reinterpret_cast<void*>(&SkipExhaustedMotionGrowth));
    if(!thunk)return false;
    constexpr auto reload=static_cast<unsigned char>(kMotionGrowReload-(kMotionGrowSignal+12));
    constexpr auto clear=static_cast<unsigned char>(kMotionGrowClear-(kMotionGrowSignal+35));
    unsigned char patched[]{
        0x48,0x8B,0xCB,0xE8,0,0,0,0,   // mov rcx,rbx; call capacity/listener check
        0x84,0xC0,0x75,reload,           // test al,al; jnz reload the free index
        0x4C,0x8D,0x73,0x38,           // lea r14,[rbx+38h]
        0x49,0x8B,0x06,                // mov rax,[r14]
        0x24,0xFD,0x0C,1,              // set the native dispatch tag
        0x48,0x8B,0xC8,0x49,0x89,0x06, // mov rcx,rax; mov [r14],rax
        0x48,0x83,0xE1,0xFC,0x74,clear, // and rcx,-4; jz stock cleanup
        0x0F,0x1F,0x80,0,0,0,0,
    };
    static_assert(sizeof(patched)==sizeof(kMotionGrowStock));
    static_assert(reload<=INT8_MAX);
    static_assert(clear<=INT8_MAX);
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(site+8);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        const auto offset=static_cast<std::int32_t>(delta);
        std::memcpy(patched+4,&offset,sizeof(offset));
        if(edf::PatchCode(site,kMotionGrowStock,patched,sizeof(patched)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}



constexpr unsigned kBodyGrowthListener=0x11AE980,kBodyGrowthCall=0x11AE99F;
constexpr unsigned kBodyResize=0xE42730,kBodyArrayResize=0xE42FE0;
constexpr unsigned kBodyGrowthStep=0x1000,kBodyGrowthMaxStep=0x8000,kBodyRecordBytes=0xB0;
constexpr unsigned kBodyCapacityLimit=(INT32_MAX-0x20u)/kBodyRecordBytes;
constexpr unsigned kBodyCommitBytesPerSlot=512;
using BodyResizeFn=void*(__fastcall*)(void*,void*,void*,int);

// Only the stock automatic-growth request is eligible. Explicit reserves and unusual sizes retain their arguments.
int BodyGrowthCapacity(int current,int requested,std::uint64_t availableCommit) noexcept {
    if(current<=0 || static_cast<std::int64_t>(current)+kBodyGrowthStep!=requested)return requested;
    const auto count=static_cast<unsigned>(current);
    const auto proportional=(count/8+kBodyGrowthStep-1)&~(kBodyGrowthStep-1);
    if(proportional<=kBodyGrowthStep)return requested;
    const auto step=(std::min)(proportional,kBodyGrowthMaxStep);
    const auto target=static_cast<std::uint64_t>(count)+step;
    // The native body-buffer byte count is signed. Also allow room for side tables during relocation.
    if(target>kBodyCapacityLimit || target*kBodyCommitBytesPerSlot>availableCommit)return requested;
    return static_cast<int>(target);
}

void* __fastcall GrowBodyCapacity(void* manager,void* result,void* external,int requested) {
    if(!external) {
        const int current=edf::At<int>(manager,0x18);
        if(BodyGrowthCapacity(current,requested,UINT64_MAX)!=requested) {
            MEMORYSTATUSEX memory{};
            memory.dwLength=sizeof(memory);
            if(GlobalMemoryStatusEx(&memory))
                requested=BodyGrowthCapacity(current,requested,memory.ullAvailPageFile);
        }
    }
    return reinterpret_cast<BodyResizeFn>(image+kBodyResize)(manager,result,external,requested);
}

bool InstallBodyCapacityGrowth() noexcept {
    if(!NativeCodeMatches(kBodyGrowthListener,0x11AE9A9,0xA18F36A1E1457D94ull) ||
       !NativeCodeMatches(kBodyResize,0xE42782,0x93EA381687190B60ull) ||
       !NativeCodeMatches(0xE43190,0xE432AE,0xDFAC3BCDCEE283E0ull))return false;
    bool changed=false;
    edf::RedirectCall(image+kBodyGrowthCall,image+kBodyResize,reinterpret_cast<void*>(&GrowBodyCapacity),changed);
    return changed;
}

inline __m128 XyzMask() noexcept { return _mm_castsi128_ps(_mm_set_epi32(0,-1,-1,-1)); }

inline __m128 KeepW(__m128 xyz,__m128 original) noexcept {
    const auto mask=XyzMask();
    return _mm_or_ps(_mm_and_ps(mask,xyz),_mm_andnot_ps(mask,original));
}

struct BoxVectors { __m128 centre,half; };

// The verified call passes disjoint centre/extent, matrix and output records. Preserve the native y+x+z(+w) sums.
__forceinline BoxVectors BoxTransform(const float* box,const float* matrix) noexcept {
    const auto centre=_mm_loadu_ps(box),half=_mm_loadu_ps(box+4);
    const auto m0=_mm_loadu_ps(matrix),m1=_mm_loadu_ps(matrix+4),m2=_mm_loadu_ps(matrix+8),m3=_mm_loadu_ps(matrix+12);
    const auto absBits=_mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
    const auto hx=_mm_and_ps(_mm_mul_ps(_mm_shuffle_ps(half,half,0x00),m0),absBits);
    const auto hy=_mm_and_ps(_mm_mul_ps(_mm_shuffle_ps(half,half,0x55),m1),absBits);
    const auto hz=_mm_and_ps(_mm_mul_ps(_mm_shuffle_ps(half,half,0xAA),m2),absBits);
    const auto x=_mm_mul_ps(_mm_shuffle_ps(centre,centre,0x00),m0);
    const auto y=_mm_mul_ps(_mm_shuffle_ps(centre,centre,0x55),m1);
    const auto z=_mm_mul_ps(_mm_shuffle_ps(centre,centre,0xAA),m2);
    const auto w=_mm_mul_ps(_mm_shuffle_ps(centre,centre,0xFF),m3);
    return {_mm_add_ps(_mm_add_ps(_mm_add_ps(y,x),z),w),
            KeepW(_mm_add_ps(_mm_add_ps(hy,hx),hz),_mm_set1_ps(1.0f))};
}

void __fastcall TransformBox(float* out,const float* box,const float* matrix) noexcept {
    const auto transformed=BoxTransform(box,matrix);
    _mm_storeu_ps(out+4,transformed.half);
    _mm_storeu_ps(out,transformed.centre);
}

void __fastcall TransformAndMergeBox(float* bounds,float* proxy) noexcept {
    // The continuation reads the body ID and world reference after the box math.
    const auto body=edf::At<std::uintptr_t>(proxy,0x10);
    _mm_prefetch(reinterpret_cast<const char*>(body+0xF0),_MM_HINT_T0);
    _mm_prefetch(reinterpret_cast<const char*>(body+0x100),_MM_HINT_T0);
    const auto transformed=BoxTransform(proxy+0x20/sizeof(float),proxy+0x60/sizeof(float));
    const auto lo=_mm_loadu_ps(bounds),hi=_mm_loadu_ps(bounds+4);
    const auto shrink=_mm_sub_ps(transformed.centre,transformed.half);
    const auto grow=_mm_add_ps(transformed.half,transformed.centre);
    _mm_storeu_ps(proxy+0x50/sizeof(float),transformed.half);
    _mm_storeu_ps(proxy+0x40/sizeof(float),transformed.centre);
    _mm_storeu_ps(bounds,KeepW(_mm_min_ps(lo,shrink),lo));
    _mm_storeu_ps(bounds+4,KeepW(_mm_max_ps(hi,grow),hi));
}

bool InstallBoxMerge() noexcept {
    constexpr unsigned char submit[]{
        0x48,0x8B,0x53,0x10,0x0F,0x10,0x8B,0x90,0,0,0,0x0F,0x5A,0xC1,
        0x48,0x8B,0x82,0x00,0x01,0,0,0x8B,0x92,0xF0,0,0,0,
    };
    if(!edf::Matches(image,kBoxMerge,kBoxMergeStock,sizeof(kBoxMergeStock)) ||
       !edf::Matches(image,kBoxMergeEnd,submit,sizeof(submit)))return false;
    auto* const site=image+kBoxMerge;
    auto* const target=edf::AllocateNearThunk(site,reinterpret_cast<void*>(&TransformAndMergeBox));
    if(!target)return false;
    unsigned char code[]={
        0x48,0x8D,0x4D,0x00,       // lea rcx,[rbp]      bounds
        0x48,0x8B,0xD3,            // mov rdx,rbx        proxy
        0xE8,0,0,0,0,              // call fused transform and union
        0x4C,0x8D,0x4D,0x20,       // lea r9,[rbp+0x20]  native position arguments
        0x4C,0x8D,0x43,0x60,       // lea r8,[rbx+0x60]
        0xC7,0x44,0x24,0x20,1,0,0,0,
        0xE9,0,0,0,0,              // resume the native body update
    };
    constexpr unsigned call=kBoxMergeCall-kBoxMerge;
    constexpr auto resume=static_cast<std::int32_t>(kBoxMergeEnd-kBoxMerge-sizeof(code));
    const auto delta=reinterpret_cast<std::intptr_t>(target)-reinterpret_cast<std::intptr_t>(site+call+5);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        const auto rel32=static_cast<std::int32_t>(delta);
        std::memcpy(code+call+1,&rel32,sizeof(rel32));
        std::memcpy(code+sizeof(code)-sizeof(resume),&resume,sizeof(resume));
        if(edf::PatchCode(site,kBoxMergeStock,code,sizeof(code)))return true;
    }
    VirtualFree(target,0,MEM_RELEASE);
    return false;
}

constexpr unsigned kPoseSetter=0x109B50,kPoseSetterCall=0x6EDC0C;
constexpr unsigned char kPoseSetterStock[]{
    0x41,0x0F,0x28,0x00,0x0F,0x29,0x42,0x60,0x41,0x0F,0x28,0x48,0x10,0x0F,0x29,0x4A,
    0x70,0x41,0x0F,0x28,0x40,0x20,0x0F,0x29,0x82,0x80,0x00,0x00,0x00,0x41,0x0F,0x28,
    0x48,0x30,0x0F,0x29,0x8A,0x90,0x00,0x00,0x00,0xC3,
};
constexpr unsigned char kPoseSetterSite[]{
    0x48,0x8B,0x93,0xB0,0x00,0x00,0x00,0x48,0x8D,0x8E,0x10,0x01,0x00,0x00,0x4C,0x8D,
    0x44,0x24,0x60,0xE8,0x3F,0xBF,0xA1,0xFF,
};
constexpr unsigned char kPoseCompoundSubmit[]{
    0x48,0x8D,0x8E,0x10,0x01,0x00,0x00,0xE8,0x28,0xBF,0xA1,0xFF,0x48,0x8B,0x8E,0xC0,
    0x00,0x00,0x00,0x48,0x8B,0x49,0x58,0xE8,0x48,0x28,0x6C,0x00,
};

bool InstallEarlyProxyBodyPrefetch() noexcept {
    constexpr unsigned callOffset=kPoseSetterCall-0x6EDBF9;
    if(!edf::Matches(image,kPoseSetter,kPoseSetterStock,sizeof(kPoseSetterStock)) ||
       !edf::Matches(image,kPoseSetterCall-callOffset,kPoseSetterSite,sizeof(kPoseSetterSite)) ||
       !edf::Matches(image,0x6EDC4C,kPoseCompoundSubmit,sizeof(kPoseCompoundSubmit)))return false;
    // Warm the body ID and world reference during pose publication, before the later compound walk.
    constexpr unsigned char prefetch[]{
        0x48,0x8B,0x42,0x10,
        0x0F,0x18,0x88,0xF0,0,0,0,
        0x0F,0x18,0x88,0,1,0,0,
    };
    unsigned char code[sizeof(prefetch)+sizeof(kPoseSetterStock)];
    std::memcpy(code,prefetch,sizeof(prefetch));
    std::memcpy(code+sizeof(prefetch),kPoseSetterStock,sizeof(kPoseSetterStock));
    auto* const site=image+kPoseSetterCall;
    auto* const thunk=edf::AllocateNearCode(site,code,sizeof(code));
    if(!thunk)return false;
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(site+5);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        unsigned char call[]{0xE8,0,0,0,0};
        const auto rel32=static_cast<std::int32_t>(delta);
        std::memcpy(call+1,&rel32,sizeof(rel32));
        if(edf::PatchCode(site,kPoseSetterSite+callOffset,call,sizeof(call)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}

constexpr unsigned kBoundsCheck=0x109CF4,kBoundsInvalid=0x109D14,kBoundsValid=0x109D2A,kScalarBoundsCheck=0x650E0;
constexpr unsigned kBoundsCheckCall=kBoundsCheck+4;
constexpr unsigned char kBoundsCheckStock[]{
    0x48,0x8D,0x4D,0x00,0xE8,0xE3,0xB3,0xF5,0xFF,0x84,0xC0,0x75,0x13,0x48,0x8D,0x4D,
    0x10,0xE8,0xD6,0xB3,0xF5,0xFF,0x84,0xC0,0x75,0x06,0x0F,0x28,0x55,0x10,0xEB,0x16,
};
constexpr unsigned char kBoundsInvalidSig[]{0x0F,0x28,0x05,0x15,0xBF,0x65,0x01};
constexpr unsigned char kBoundsValidSig[]{0x0F,0x28,0x45,0x00,0x48,0x8D,0x55,0x20};
constexpr unsigned char kScalarBoundsSig[]{
    0x33,0xC0,0xF3,0x0F,0x10,0x04,0x81,0x0F,0x2E,0xC0,0x7A,0x0C,0x48,0xFF,0xC0,
    0x48,0x83,0xF8,0x04,0x75,0xED,0x32,0xC0,0xC3,0xB0,0x01,0xC3,
};
bool __fastcall BoundsHaveNaN(const float* bounds) noexcept {
    const auto signless=_mm_set1_epi32(0x7FFFFFFF);
    const auto infinity=_mm_set1_epi32(0x7F800000);
    const auto low=_mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(bounds)),signless);
    const auto high=_mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(bounds+4)),signless);
    const auto minimumNormal=_mm_set1_epi32(0x00800000);
    const auto zero=_mm_setzero_si128();
    const auto exceptional=[&](__m128i bits) noexcept {
        const auto subnormal=_mm_andnot_si128(_mm_cmpeq_epi32(bits,zero),_mm_cmpgt_epi32(minimumNormal,bits));
        return _mm_or_si128(subnormal,_mm_cmpgt_epi32(bits,infinity));
    };
    if(!_mm_movemask_epi8(_mm_or_si128(exceptional(low),exceptional(high))))return false;
    // Retain the scalar order for signaling NaNs and denormal-operand flags.
    const auto stock=reinterpret_cast<bool(__fastcall*)(const float*)>(image+kScalarBoundsCheck);
    return stock(bounds) || stock(bounds+4);
}

bool InstallBoundsCheck() noexcept {
    if(!edf::Matches(image,kBoundsCheck,kBoundsCheckStock,sizeof(kBoundsCheckStock)) ||
       !edf::Matches(image,kBoundsInvalid,kBoundsInvalidSig,sizeof(kBoundsInvalidSig)) ||
       !edf::Matches(image,kBoundsValid,kBoundsValidSig,sizeof(kBoundsValidSig)) ||
       !edf::Matches(image,kScalarBoundsCheck,kScalarBoundsSig,sizeof(kScalarBoundsSig)))return false;
    auto* const site=image+kBoundsCheck;
    auto* const thunk=edf::AllocateNearThunk(site,reinterpret_cast<void*>(&BoundsHaveNaN));
    if(!thunk)return false;
    constexpr unsigned call=kBoundsCheckCall-kBoundsCheck;
    unsigned char code[]{
        0x48,0x8D,0x4D,0x00,                             // lea rcx,[rbp]
        0xE8,0,0,0,0,                                    // call packed check
        0x84,0xC0,                                       // test al,al
        0x75,static_cast<unsigned char>(kBoundsInvalid-kBoundsCheck-13),
        0x0F,0x28,0x55,0x10,                             // movaps xmm2,[rbp+10h]
        0xEB,static_cast<unsigned char>(kBoundsValid-kBoundsCheck-19),
    };
    static_assert(sizeof(code)==19 && sizeof(code)<sizeof(kBoundsCheckStock));
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(site+call+5);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        const auto relative=static_cast<std::int32_t>(delta);
        std::memcpy(code+call+1,&relative,sizeof(relative));
        if(edf::PatchCode(site,kBoundsCheckStock,code,sizeof(code)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}

inline bool Normalize3(__m128& v) noexcept {
    v=KeepW(v,_mm_set1_ps(1.0f));
    const auto squared=_mm_mul_ps(v,v);
    const auto xy=_mm_add_ss(_mm_shuffle_ps(squared,squared,0x55),squared);
    const auto total=_mm_add_ss(xy,_mm_shuffle_ps(squared,squared,0xAA));
    const auto bits=static_cast<std::uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(total)));
    // A positive normal already satisfies the native ordered and nonzero comparisons.
    if(bits-0x00800000u>0x7EFFFFFFu) {
        if((_mm_movemask_ps(_mm_cmple_ss(total,_mm_set_ss(FLT_MAX)))&1)==0)return false;
        if(_mm_cvtss_f32(total)==0.0f)return true;
    }
    const auto len=_mm_sqrt_ss(total);
    v=_mm_div_ps(v,_mm_shuffle_ps(len,len,0));
    return true;
}

inline __m128 Cross3(__m128 a,__m128 b) noexcept {
    const auto mask=XyzMask();
    a=_mm_and_ps(a,mask);b=_mm_and_ps(b,mask);
    const auto ayzx=_mm_shuffle_ps(a,a,_MM_SHUFFLE(3,0,2,1));
    const auto byzx=_mm_shuffle_ps(b,b,_MM_SHUFFLE(3,0,2,1));
    const auto rotated=_mm_sub_ps(_mm_mul_ps(a,byzx),_mm_mul_ps(ayzx,b));
    return _mm_shuffle_ps(rotated,rotated,_MM_SHUFFLE(3,0,2,1));
}

void __fastcall NormalizeAttachedBasis(float* matrix) noexcept {
    const auto savedX=_mm_loadu_ps(matrix),savedY=_mm_loadu_ps(matrix+4),savedZ=_mm_loadu_ps(matrix+8);
    auto x=savedX;
    if(Normalize3(x)) {
        auto y=Cross3(savedZ,x);
        if(Normalize3(y)) {
            auto z=Cross3(x,y);
            if(Normalize3(z)) {
                _mm_storeu_ps(matrix,KeepW(x,savedX));
                _mm_storeu_ps(matrix+4,KeepW(y,savedY));
                _mm_storeu_ps(matrix+8,KeepW(z,savedZ));
                return;
            }
        }
    }
    // Keep the stock handling of overflowing or unordered intermediates, before writing any row.
    reinterpret_cast<void(__fastcall*)(float*)>(image+kBasis)(matrix);
}
}

bool InstallNativeTargetScore() noexcept {
    return image && InstallTargetScore();
}

bool InstallNativeHostileVisit() noexcept {
    return image && InstallHostileVisit();
}

bool InstallNativeHostileRoster(bool nearestReady) noexcept {
    return image && InstallRosterRedirects(nearestReady);
}

bool InstallNativeSoldierScore() noexcept {
    return image && InstallSoldierScore();
}

unsigned InstallNativeBodyMath() noexcept {
    if(!image)return 0;
    unsigned installed=0;
    if(edf::Matches(image,kTransformBox,kBoxSig,sizeof(kBoxSig)) &&
       edf::Matches(image,kBoxArguments,kBoxArgumentsSig,sizeof(kBoxArgumentsSig)) &&
       edf::At<std::uint32_t>(image,kSignMask)==0x7fffffffu) {
        if(InstallBoxMerge()) {
            ++installed;
            installed+=InstallEarlyProxyBodyPrefetch();
        } else {
            bool changed=false;
            edf::RedirectCall(image+kBoxCall,image+kTransformBox,reinterpret_cast<void*>(&TransformBox),changed);
            installed+=changed;
        }
    }
    installed+=InstallBoundsCheck();
    installed+=InstallAvoidanceHeap();
    installed+=InstallAvoidanceScan();
    installed+=InstallClingBodyReuse();
    installed+=InstallContactDecoders();
    installed+=InstallStaticBodyQuaternionSkip();
    installed+=InstallEmptyBodySignalGate();
    if(InstallMotionPropertiesSearch()) {
        ++installed;
        boundedMotionSearch=InstallMotionIdLimit();
        installed+=boundedMotionSearch;
        if(boundedMotionSearch)installed+=InstallExhaustedMotionGrowthGate();
    }
    installed+=InstallContactStream();
    installed+=InstallBodyCapacityGrowth();
    if(!edf::Matches(image,kBasis,kBasisSig,sizeof(kBasisSig)))return installed;
    for(const auto& site:kAttachedBasisCalls) {
        if(!edf::Matches(image,site.arguments,site.argumentSig,sizeof(site.argumentSig)) ||
           !edf::Matches(image,site.call,site.callSig,sizeof(site.callSig)))continue;
        bool changed=false;
        edf::RedirectCall(image+site.call,image+kBasis,reinterpret_cast<void*>(&NormalizeAttachedBasis),changed);
        installed+=changed;
    }
    return installed;
}
}
