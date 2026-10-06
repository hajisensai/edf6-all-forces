// The split missiles (MissileBullet02: Blood Storm and kin) split before a big target's surface, not after it.
// The stock split test 0x26CF00(round, frames since ignition) is called from one place, the flight state 0x26EC10
// (0x26ED9C). Its distance mode (Ammo_CustomParameter[12] = [0, distance, angle]) splits when the distance from the
// round's position (+0x90) to its lock point (lock entry +0x10, the target's single centre bone) is under `distance`
// and the angle off its nose (+0x80) under `angle`. The DLC egg is wider than that distance: the round meets its
// surface and blasts before it splits (the user, 2026-10-06), its submissiles never fired.
// The hook casts a ray on the units' layer (11, the rounds' object layer) along the line of sight that sees the lock
// target's own bodies alone (its collector's addHit drops every other object's hit) for its first surface, and shows the stock
// test the round that far short of the lock point, on the same line of sight (split::Proxy): the stock test measures
// the distance to the surface, its angle and its time mode (CP[12][0] = 1, Hell Storm) unchanged, then the round's
// position is put back. A small target (surface beyond, or no hit) leaves the stock test as it was.
// Each machine runs this test for its own copy of the round (no network ownership in 0x26CF00 or the split state;
// docs/split-missile-re.md §5). Docs: docs/split-missile-re.md. All addresses are RVAs into EDF.dll 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include "split_fuse.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kSplitTest=0x26CF00,kSplitCall=0x26ED9C;
constexpr std::size_t kPos=0x90,kLock=0xB10,kLockCtrl=0xB18,kLockObject=0x08,kLockAim=0x10,kLockValid=0x29,kCtrlUses=8;
constexpr std::size_t kHavokGlobal=0x20B2958,kCastRay=0x11A7EE0,kHitVtbl=0x1768B78,kHitReset=0xFDF00;
constexpr std::size_t kHitSlot0=0x978880,kHitAdd=0xD93980,kBodyObject=0x108260;
constexpr std::uint32_t kUnitLayer=0x0B;   // layer 11: meets the unit layers {5..9, 14}, not the map (docs/bullet-pass-re.md §6)
constexpr ULONGLONG kLogMs=2000;

struct Signature { std::size_t rva; unsigned char bytes[16]; std::size_t size; };
const Signature kSignatures[]={
    {kSplitTest,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55},16},
    {kSplitCall-5,{0x8B,0xD7,0x48,0x8B,0xCB,0xE8,0x5F,0xE1,0xFF,0xFF,0x84,0xC0},12},           // edx = frames, call
    {0x26D08F,{0x49,0x8B,0x97,0x18,0x0B,0x00,0x00,0x48,0x85,0xD2,0x74,0x19,0x8B,0x42,0x08,0x85},16},   // lock ctrl, uses
    {0x26D0C7,{0x80,0x78,0x29,0x00,0x0F,0x84,0x4E,0x02,0x00,0x00,0x0F,0x10,0x50,0x10},14},          // valid, lock point
    {0x26D0D5,{0x0F,0x28,0xF2,0x41,0x0F,0x5C,0xB7,0x90,0x00,0x00,0x00},11},                         // minus round's +0x90
    {0x26D15C,{0x49,0x8B,0x87,0x10,0x0B,0x00,0x00},7},                                               // lock entry
    {kCastRay,{0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x48,0x8B,0x05,0x66,0x71},16},
    {kHitReset,{0x33,0xD2,0xB8,0xFF,0xFF,0x00,0x00,0x89,0x51,0x0C,0x0F,0x28,0x05,0x1F,0x4B,0xE8},16},
    {kHitAdd,{0xF3,0x0F,0x10,0x4A,0x20,0x0F,0x10,0x41,0x10,0x0F,0xC6,0xC9,0x00,0x0F,0x2E,0xC1},16},
    {kBodyObject,{0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0xED,0xA6,0xFA,0x01,0x8B,0xD1,0x48,0x8D,0x48},16},
};

using SplitFn=bool(__fastcall*)(void*,int);
using BodyObjectFn=const void*(__fastcall*)(std::uint32_t);
SplitFn nextSplit=nullptr;
bool ready=false;

struct alignas(16) RayInput { float from[4],to[4]; std::uint32_t filter,unk24; std::uint64_t pad; };
static_assert(sizeof(RayInput)==0x30,"EdfRayInput");
// The game's closest-hit collector (0xA0 bytes, docs/raycast-re.md §2.2) with the target after it: its vtable a copy of
// the game's (kHitVtbl) with addHit (slot 4) TargetAddHit, which hands the game's addHit only a hit on the target.
struct alignas(16) TargetHits { unsigned char raw[0xA0]; const void* target; unsigned char pad[8]; };
static_assert(sizeof(TargetHits)==0xB0,"TargetHits");
constexpr std::size_t kHitSlots=6,kAddHitSlot=4,kHitBody=0x48;   // hknpCollisionResult: body id (the bullets' 0x2321B0)
void* targetVtbl[kHitSlots]{};
using AddHitFn=void(__fastcall*)(void*,const unsigned char*);

void __fastcall TargetAddHit(void* collector,const unsigned char* hit) {
    const auto self=static_cast<TargetHits*>(collector);
    const std::uint32_t body=At<std::uint32_t>(hit,kHitBody)&0xFFFFFF;
    if(body==0xFFFFFF)return;
    if(split::Counts(reinterpret_cast<BodyObjectFn>(image+kBodyObject)(body),self->target))
        reinterpret_cast<AddHitFn>(image+kHitAdd)(collector,hit);
}

// split::Cast on the game's physics world: the nearest surface of `target` along a->b on the units' layer (count
// +0x0C, fraction +0x50 of a->b).
float CastTarget(void*,const float* a,const float* b,const void* target) noexcept {
    const auto g=At<unsigned char*>(image,kHavokGlobal);
    if(!Readable(g,0x70) || !At<const void*>(g,0x68))return -1.0f;
    const RayInput in{{a[0],a[1],a[2],1.0f},{b[0],b[1],b[2],1.0f},kUnitLayer,0,0};
    TargetHits col{};
    *reinterpret_cast<void* const**>(col.raw)=targetVtbl;
    col.target=target;
    reinterpret_cast<void(*)(void*)>(image+kHitReset)(&col);
    reinterpret_cast<void(*)(void*,void*,const RayInput*)>(image+kCastRay)(g+0x10,&col,&in);
    if(*reinterpret_cast<const std::int32_t*>(col.raw+0x0C)==0)return -1.0f;
    const float f=*reinterpret_cast<const float*>(col.raw+0x50);
    if(!std::isfinite(f) || f<0.0f || f>1.0f)return -1.0f;
    return f*split::Distance(a,b);
}

// The position to show the stock test (the round's lock target's surface nearer than its lock point), or false.
bool Prepare(const unsigned char* b,float* proxy,float* centre,float* surface) noexcept {
    const auto ctrl=At<const unsigned char*>(b,kLockCtrl);
    const auto entry=At<const unsigned char*>(b,kLock);
    if(!ctrl || !entry || !Readable(ctrl,kCtrlUses+4) || At<std::int32_t>(ctrl,kCtrlUses)<=0)return false;
    if(!Readable(entry,kLockValid+1) || !entry[kLockValid])return false;
    const void* const target=At<const void*>(entry,kLockObject);
    const float* const aim=reinterpret_cast<const float*>(entry+kLockAim);
    const float* const pos=reinterpret_cast<const float*>(b+kPos);
    *centre=split::Distance(pos,aim);
    *surface=split::SurfaceAlong(pos,aim,target,&CastTarget,nullptr);
    return split::Proxy(pos,aim,*surface,proxy);
}

thread_local ULONGLONG saidAt=0;

bool __fastcall SplitHook(void* round,int frames) {
    const auto b=static_cast<unsigned char*>(round);
    float proxy[3],centre=0.0f,surface=-1.0f;
    bool moved=false;
    if(Cfg().splitMissileSurface) {
        __try { moved=Prepare(b,proxy,&centre,&surface); }
        __except(EXCEPTION_EXECUTE_HANDLER){moved=false;}
    }
    if(!moved)return nextSplit(round,frames);
    float saved[3];
    std::memcpy(saved,b+kPos,12);
    std::memcpy(b+kPos,proxy,12);
    const bool splits=nextSplit(round,frames);
    std::memcpy(b+kPos,saved,12);
    const ULONGLONG now=GetTickCount64();
    if(Cfg().debug && (splits || now-saidAt>kLogMs)) {
        saidAt=now;
        Log("SPLIT missile %p: target surface %.1f m, its centre %.1f m%s",round,surface,centre,splits ? ": splits" : "");
    }
    return splits;
}
}  // namespace

bool InstallSplitMissiles() noexcept {
    bool profile=false;
    __try {
        profile=Readable(image+kHitVtbl,0x28) && At<const unsigned char*>(image,kHitVtbl)==image+kHitSlot0 &&
                At<const unsigned char*>(image,kHitVtbl+0x20)==image+kHitAdd;
        for(const auto& s:kSignatures)profile=profile && Matches(s.rva,s.bytes,s.size);
    } __except(EXCEPTION_EXECUTE_HANDLER){profile=false;}
    if(profile) {
        std::memcpy(targetVtbl,image+kHitVtbl,sizeof(targetVtbl));
        targetVtbl[kAddHitSlot]=reinterpret_cast<void*>(&TargetAddHit);
        nextSplit=reinterpret_cast<SplitFn>(image+kSplitTest);
        bool changed=false;
        ready=RedirectCall(image+kSplitCall,image+kSplitTest,reinterpret_cast<void*>(&SplitHook),changed);
        if(!ready && changed)Log("SPLIT missile call half patched");
    }
    Log("HOOK split missiles=%d (profile=%d config=%d): split distance to the target's surface",ready,profile,
        Cfg().splitMissileSurface);
    return ready;
}
}  // namespace crew
