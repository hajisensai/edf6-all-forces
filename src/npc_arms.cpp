// npc_arms.h. EDF.dll TimeDateStamp 0x678CCB46, RVAs. GameStatus (*(EDF+0x20B2890)) slot 0: its class (+0x6E90), its 6
// weapon ids per class (+0x6E98 + class*0x18), the weapon records (weapon_records.h: u32 flags, bit 0 owned, then 8 star
// bytes): docs/loadout-re.md, as src/loadout.cpp reads them. The preload (0x7A3780 on *(EDF+0x20B29A8)) and its
// completion query (0x7A3600, the script's IsCompleteResource) are debug_spawn.cpp's, checked by the same bytes.
#include "npc_arms.h"
#include "npc_tier.h"
#include "support_soldier.h"
#include "weapon_records.h"
#include "memory.h"
#include <cstring>
#include <iterator>

namespace crew {
namespace {
constexpr unsigned kGameStatus=0x20B2890;
constexpr std::size_t kClass=0x6E90,kWeapons=0x6E98,kClassStride=0x18,kRecordStars=4;
constexpr int kSlotsPerClass[4]={4,4,5,6};   // src/loadout.cpp: Ranger, Wing Diver, Air Raider, Fencer
constexpr unsigned kPreload=0x7A3780,kLoadDone=0x7A3600;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
// mov [rsp+18h],rbx; push rdi; sub rsp,20h; mov rbx,rcx; add rcx,20h; call 0x51E70 (the queue empty); test al,al
const unsigned char kLoadDoneSig[]={0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x83,0xC1,0x20,
                                    0xE8,0x5A,0xE8,0x8A,0xFF,0x84,0xC0};
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using LoadDoneFn=bool(*)(void*);

// The weapon paths of this mission: loaded (the mission's own preload, or a load this asked for that completed) or
// asked for and not yet reported done. Bounded: a mission sees few distinct support weapons (each requester's 14 kinds).
constexpr int kPathsMost=128;
struct PathSet {
    int count=0;
    wchar_t path[kPathsMost][96]{};
    bool Has(const wchar_t* p) const noexcept {
        for(int i=0;i<count;++i)if(!_wcsicmp(path[i],p))return true;
        return false;
    }
    bool Add(const wchar_t* p) noexcept {
        if(Has(p))return true;
        if(count>=kPathsMost)return false;
        wcscpy_s(path[count++],p);return true;
    }
};
PathSet loaded,asked;
void* mission=nullptr;   // the object manager the sets belong to

bool Profile() noexcept {
    static int ok=-1;
    if(ok<0)ok=Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) && Matches(kLoadDone,kLoadDoneSig,sizeof(kLoadDoneSig)) ? 1 : 0;
    return ok==1;
}
// A new mission's object manager: the sets were the last mission's.
void Follow() noexcept {
    void* now=nullptr;
    __try { now=At<void*>(image,kObjectMgr); } __except(EXCEPTION_EXECUTE_HANDLER){now=nullptr;}
    if(now!=mission){mission=now;loaded.count=0;asked.count=0;}
}
bool Queue(const wchar_t* path) noexcept {
    __try {
        void* const mgr=At<void*>(image,kPreloadMgr);
        if(!mgr)return false;
        reinterpret_cast<PreloadFn>(image+kPreload)(mgr,path,2,-1);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("SUPPORT arms: queuing the load of %ls faulted",path);return false;}
}

struct Records { unsigned char* gs=nullptr; };
bool Owned(const void* context,int id) noexcept {
    auto* gs=static_cast<const Records*>(context)->gs;
    unsigned char* const r=WeaponRecord(gs,static_cast<std::uint32_t>(id));
    return r && Readable(r,4) && (At<std::uint32_t>(r,0)&1u);
}
bool Stars(const void* context,int id,std::uint8_t* out) noexcept {
    auto* gs=static_cast<const Records*>(context)->gs;
    unsigned char* const r=WeaponRecord(gs,static_cast<std::uint32_t>(id));
    if(!r || !Readable(r+kRecordStars,npctier::kStarCount))return false;
    std::memcpy(out,r+kRecordStars,npctier::kStarCount);
    return true;
}
}  // namespace

void LocalRequesterArms(support_net::RequesterArms* out) noexcept {
    if(!out)return;
    *out=support_net::RequesterArms{};
    if(!SupportSoldierArmsReady())return;
    Records records;
    npctier::Inventory inv;
    __try {
        records.gs=At<unsigned char*>(image,kGameStatus);
        if(!records.gs || !Readable(records.gs+kClass,4) || !Readable(records.gs+kWeapons,4*kClassStride))return;
        const auto cls=At<std::int32_t>(records.gs,kClass);
        if(cls>=0 && cls<4)
            for(int i=0;i<kSlotsPerClass[cls];++i)inv.equipped[i]=At<std::int32_t>(records.gs,kWeapons+cls*kClassStride+i*4);
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    inv.context=&records;inv.owned=&Owned;inv.stars=&Stars;
    for(int k=0;k<kSupportWeaponCount;++k) {
        const auto kind=static_cast<SupportWeapon>(k);
        if(!npctier::KindArmed(kind))continue;
        out->kind[k]=npctier::PackArms(npctier::PickArms(kind,inv));
    }
    if(Cfg().debug) {
        for(int k=0;k<kSupportWeaponCount;++k)if(out->kind[k])
            Log("SUPPORT arms: kind %d gets weapon %u stars %08llX",k,static_cast<unsigned>(out->kind[k]&0xFFFFu),
                static_cast<unsigned long long>((out->kind[k]>>16)&0xFFFFFFFFull));
    }
}

void PreloadNpcArms() noexcept {
    Follow();
    if(!Profile() || !SupportSoldierArmsReady())return;
    support_net::RequesterArms arms;
    LocalRequesterArms(&arms);
    int queued=0;
    for(int k=0;k<kSupportWeaponCount;++k) {
        npctier::Arms a;wchar_t path[96];
        const auto kind=static_cast<SupportWeapon>(k);
        if(!arms.kind[k] || !npctier::UnpackArms(arms.kind[k],kind,&a) || !npctier::ArmsPath(kind,a,path,std::size(path)))continue;
        if(loaded.Has(path) || !Queue(path))continue;
        loaded.Add(path);++queued;   // with the mission's preload: done by the game's WaitPreload
    }
    Log("SUPPORT arms: %d weapons of this player's support soldiers queued with the mission's preload",queued);
}

support_net::PlanResult NpcArmsLoaded(const support_net::Plan& plan) noexcept {
    using support_net::PlanResult;
    Follow();
    bool waiting=false;
    for(std::uint32_t i=0;i<plan.count && i<support_net::kMaxUnits;++i) {
        const auto& unit=plan.units[i];
        if(!unit.arms)continue;
        npctier::Arms a;wchar_t path[96];
        if(!IsSupportSoldierResource(unit.resourceId))return PlanResult::refused;
        const auto kind=SupportSoldierWeapon(unit.resourceId);
        if(!npctier::UnpackArms(unit.arms,kind,&a) || !npctier::ArmsPath(kind,a,path,std::size(path)))return PlanResult::refused;
        if(loaded.Has(path))continue;
        if(!asked.Has(path)) {
            if(!Profile() || !asked.Add(path) || !Queue(path)) {
                Log("SUPPORT arms: %ls cannot be loaded here (the preload profile, or %d loads this mission): refused",path,kPathsMost);
                return PlanResult::refused;
            }
            Log("SUPPORT arms: %ls not loaded this mission: its load queued",path);
        }
        waiting=true;
    }
    if(!waiting)return PlanResult::ready;
    bool done=false;
    __try {
        void* const mgr=At<void*>(image,kPreloadMgr);
        done=mgr && reinterpret_cast<LoadDoneFn>(image+kLoadDone)(mgr);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("SUPPORT arms: the load completion query faulted: refused");return PlanResult::refused;}
    if(!done)return PlanResult::pending;
    for(int i=0;i<asked.count;++i)loaded.Add(asked.path[i]);
    Log("SUPPORT arms: %d weapon loads complete",asked.count);
    asked.count=0;
    return PlanResult::ready;
}
}  // namespace crew
