// The CarBase vehicles' AI target (the tester, 2026-10-28, #29: 「AI操控载具时触发开火距离过近」). Object update 0x54BE40
// asks the vehicle's slot 14 for a target every +0x514 frames and stores it at +0x518, where the AI action 0x661440
// aims and fires on it (0x65F6F0: only within the weapon's own reach +0x224 and its aim tolerance +0x2600). CarBase's
// slot 14 0x630890 takes the single nearest enemy (0x54C4B0: the team walk 0x5E0F20 with no range limit, its distance
// times the enemy's +0x47C when the vehicle's +0x4A0 is set) and, with +0xE04 set, keeps it only when 0x62DE60 passes:
// one of its weapons reaches it (speed x alive), the round's arc can meet it and the map ray to it is clear; otherwise
// no target at all. So a vehicle whose nearest enemy stood behind a building or a hill held its fire at every other
// enemy in its guns' reach until the swarm closed in and the nearest came into view (the tester's log: the PzH 2000
// under an NPC driver without a target for a minute, then firing only at 29-99 m).
// The hook keeps the stock answer when there is one; when the stock rejected its nearest, the next nearest enemies
// (by the stock's own score) are put to the same 0x62DE60, the first that passes is the target. The reach, the arc
// and the ray stay the stock's; nothing here decides a range of its own.
#include "crew.h"
#include "memory.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace crew {
namespace {
constexpr std::size_t kSlotSearch=14;
constexpr unsigned kSearch=0x630890,kInReach=0x62DE60,kVisitEnemies=0x5E0F20,kTeams=0x20B2978;
constexpr unsigned char kSearchSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0xE8,0x0E,0xBC};
constexpr unsigned char kInReachSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
constexpr unsigned char kVisitSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x55,0x41,0x56};
// Every class whose slot 14 is 0x630890 and whose AI action (slot 72) is CarBase's 0x661440 (scanned off .rdata:
// 402, 403, 404, 503, 505, 507, 510, 511, 601, 603, 607, 60X, Car and the bases).
constexpr unsigned kVtables[]={0x17D8B50,0x17D8FA0,0x17D9458,0x17DA508,0x17DADB0,0x17DB590,0x17DB9D8,0x17DBDF8,0x17DC250,
                               0x17DC620,0x17DCAB0,0x17DCFB8,0x17DFDC8,0x17E01B0,0x17E0A80,0x17E1828};
constexpr int kVt=static_cast<int>(sizeof(kVtables)/sizeof(kVtables[0]));
constexpr std::size_t kCheckReach=0xE04,kWeighted=0x4A0,kTeam=0x314;
constexpr std::size_t kObjPos=0x90,kObjState=0x39C,kObjWeight=0x47C,kObjBones=0x368,kObjBoneCount=0x378;
constexpr int kMaxEnemies=128;   // enemies a walk keeps (the nearest by score; more than any map holds near a vehicle)
constexpr int kTries=8;          // the next nearest put to the stock check (each casts its rays)

using SearchFn=void*(__fastcall*)(void*);
using InReachFn=bool(__fastcall*)(void*,const float*);
SearchFn nextSearch[kVt]{};
bool ready=false;

struct Candidate { const unsigned char* obj; float score; };
struct Walk { const void* const* vtable; const unsigned char* self; float from[3]; bool weighted; int n; Candidate c[kMaxEnemies]; };
using VisitFn=void(__fastcall*)(void*,std::int32_t,Walk*);

// The stock walk's per-object step (0x54A3B0): an active enemy (+0x39C none but bit 1), its score its distance from
// the vehicle, times its weight when the vehicle weighs them.
void Consider(Walk& w,const unsigned char* o) noexcept {
    if(!o || o==w.self || !Readable(o,kObjWeight+4) || (At<std::uint32_t>(o,kObjState)&~2u))return;
    const float* p=reinterpret_cast<const float*>(o+kObjPos);
    const float d[3]={p[0]-w.from[0],p[1]-w.from[1],p[2]-w.from[2]};
    float score=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(w.weighted)score*=At<float>(o,kObjWeight);
    if(!std::isfinite(score))return;
    if(w.n<kMaxEnemies){w.c[w.n++]={o,score};return;}
    Candidate* worst=std::max_element(w.c,w.c+w.n,[](const Candidate& a,const Candidate& b){return a.score<b.score;});
    if(score<worst->score)*worst={o,score};
}
void __fastcall WalkNone(Walk*) noexcept {}
void __fastcall WalkStep(Walk* w,const unsigned char* object) noexcept {
    __try { Consider(*w,object); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
const void* const kWalkVtable[]={reinterpret_cast<const void*>(&WalkNone),reinterpret_cast<const void*>(&WalkStep)};

// Where the stock check aims at `o` (0x6308B3): its first bone's position when it has bones, else its own.
const float* LockPoint(const unsigned char* o) noexcept {
    if(At<std::uint64_t>(o,kObjBoneCount)==0)return reinterpret_cast<const float*>(o+kObjPos);
    const auto bones=At<const unsigned char* const*>(o,kObjBones);
    if(!Readable(bones,8) || !Readable(bones[0],0x20))return nullptr;
    return reinterpret_cast<const float*>(bones[0]+0x10);
}

void* NextInReach(unsigned char* v) noexcept {
    const auto teams=At<void*>(image,kTeams);
    if(!teams)return nullptr;
    static Walk w;   // game thread (object update); too big for the stack frame the stock gives slot 14
    w.vtable=kWalkVtable;w.self=v;std::memcpy(w.from,v+kObjPos,12);w.weighted=v[kWeighted]!=0;w.n=0;
    reinterpret_cast<VisitFn>(image+kVisitEnemies)(teams,At<std::int32_t>(v,kTeam),&w);
    const int tries=w.n<kTries ? w.n : kTries;
    std::partial_sort(w.c,w.c+tries,w.c+w.n,[](const Candidate& a,const Candidate& b){return a.score<b.score;});
    const auto inReach=reinterpret_cast<InReachFn>(image+kInReach);
    for(int i=0;i<tries;++i) {
        const float* p=LockPoint(w.c[i].obj);
        if(!p)continue;
        alignas(16) float at[4]={p[0],p[1],p[2],0.0f};
        if(inReach(v,at))return const_cast<unsigned char*>(w.c[i].obj);
    }
    return nullptr;
}

template<int I> void* __fastcall SearchHook(void* vehicle) {
    void* const stock=nextSearch[I](vehicle);
    if(stock || !Cfg().enabled)return stock;
    auto* const v=static_cast<unsigned char*>(vehicle);
    __try {
        if(!v[kCheckReach])return nullptr;   // the stock found no enemy at all (it checks nothing without +0xE04)
        return NextInReach(v);
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
template<int... I> constexpr auto Hooks(std::integer_sequence<int,I...>) {
    return std::array<void*,sizeof...(I)>{reinterpret_cast<void*>(&SearchHook<I>)...};
}
const auto kHooks=Hooks(std::make_integer_sequence<int,kVt>{});
}  // namespace

bool InstallVehicleTargetSearch() noexcept {
    if(ready)return true;
    bool matches=false;
    __try {
        matches=edf::Matches(image,kSearch,kSearchSig,sizeof(kSearchSig)) &&
                edf::Matches(image,kInReach,kInReachSig,sizeof(kInReachSig)) &&
                edf::Matches(image,kVisitEnemies,kVisitSig,sizeof(kVisitSig));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if(!matches){Log("VEHTARGET off: the stock target search / reach check / enemy walk differ");return false;}
    int hooked=0;
    for(int i=0;i<kVt;++i) {
        auto slot=reinterpret_cast<void**>(image+kVtables[i])+kSlotSearch;
        if(edf::ChainVtableSlot(slot,kHooks[i],reinterpret_cast<void**>(&nextSearch[i])))++hooked;
    }
    ready=hooked>0;
    Log("VEHTARGET CarBase target search hooked %d/%d: past an unreachable nearest enemy, the next nearest in reach",hooked,kVt);
    return ready;
}
}  // namespace crew
