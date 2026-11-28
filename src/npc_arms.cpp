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
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"

namespace crew {
namespace {
constexpr unsigned kGameStatus=0x20B2890;
constexpr std::size_t kClass=0x6E90,kWeapons=0x6E98,kClassStride=0x18,kRecordStars=4;
constexpr int kSlotsPerClass[4]={4,4,5,6};   // src/loadout.cpp: Ranger, Wing Diver, Air Raider, Fencer
constexpr int kFencerClass=3;                 // its slots 0..3: Slot1L, 1R, 2L, 2R (docs/loadout-re.md 1.2)
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
    bool Full() const noexcept { return count>=kPathsMost; }
    bool Add(const wchar_t* p) noexcept {
        if(Has(p))return true;
        if(count>=kPathsMost)return false;
        wcscpy_s(path[count++],p);return true;
    }
};
PathSet loaded,asked;
void* mission=nullptr;   // the object manager the sets belong to

// The preload and its completion query, set once their bytes matched (tests/npc_arms_test.cpp records them instead).
int profileState=-1;
PreloadFn preloadFn=nullptr;
LoadDoneFn loadDoneFn=nullptr;
bool Profile() noexcept {
    if(profileState<0) {
        const bool ok=Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) && Matches(kLoadDone,kLoadDoneSig,sizeof(kLoadDoneSig));
        if(ok){preloadFn=reinterpret_cast<PreloadFn>(image+kPreload);loadDoneFn=reinterpret_cast<LoadDoneFn>(image+kLoadDone);}
        profileState=ok ? 1 : 0;
    }
    return profileState==1;
}
void* ObjectManager() noexcept {
    __try { return At<void*>(image,kObjectMgr); } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
// The mission boundary (PreloadNpcArms, from MissionStart): the sets begin empty whether or not the object manager moved.
void Reset() noexcept { mission=ObjectManager();loaded.count=0;asked.count=0; }
// Between mission starts, another object manager is another mission too: the sets were the last mission's.
void Follow() noexcept {
    if(ObjectManager()!=mission)Reset();
}
bool Queue(const wchar_t* path) noexcept {
    __try {
        void* const mgr=At<void*>(image,kPreloadMgr);
        if(!mgr || !preloadFn)return false;
        preloadFn(mgr,path,2,-1);
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
// The game status's weapon records, and its Fencer hand slots into `inv` (the Fencer class's, whatever class plays now).
bool ReadInventory(Records& records,npctier::Inventory& inv,bool current,int* playerClass=nullptr) noexcept {
    __try {
        records.gs=At<unsigned char*>(image,kGameStatus);
        if(!records.gs || !Readable(records.gs+kClass,4) || !Readable(records.gs+kWeapons,4*kClassStride))return false;
        const auto cls=At<std::int32_t>(records.gs,kClass);
        if(playerClass)*playerClass=cls;
        if(current && cls>=0 && cls<4)
            for(int i=0;i<kSlotsPerClass[cls];++i)inv.equipped[i]=At<std::int32_t>(records.gs,kWeapons+cls*kClassStride+i*4);
        for(int i=0;i<npctier::kFencerHandSlots;++i)inv.fencer[i]=At<std::int32_t>(records.gs,kWeapons+kFencerClass*kClassStride+i*4);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    inv.context=&records;inv.owned=&Owned;inv.stars=&Stars;
    return true;
}
// Whether `plan`'s weapon `word` (a main hand: `hand` false) for `kind` is loaded here; its path asked for when not.
enum class Load { ready,waiting,refused };
Load Ask(std::uint64_t word,SupportWeapon kind,bool hand) noexcept {
    npctier::Arms a;wchar_t path[96];
    if(!(hand ? npctier::UnpackHand(word,kind,&a) : npctier::UnpackArms(word,kind,&a)) || !a.on ||
       !(hand ? npctier::HandPath(a,path,std::size(path)) : npctier::ArmsPath(kind,a,path,std::size(path))))return Load::refused;
    if(loaded.Has(path))return Load::ready;
    if(!asked.Has(path)) {
        // Asked for only once queued: a load never queued is never taken for done by the completion query.
        if(!Profile() || asked.Full() || !Queue(path) || !asked.Add(path)) {
            Log("SUPPORT arms: %ls cannot be loaded here (the preload profile, or %d loads this mission): refused",path,kPathsMost);
            return Load::refused;
        }
        Log("SUPPORT arms: %ls not loaded this mission: its load queued",path);
    }
    return Load::waiting;
}
}  // namespace

bool PresetHandArms(int right,int left,std::uint64_t* arms,std::uint64_t* offhand) noexcept {
    if(!arms || !offhand)return false;
    *arms=0;*offhand=0;
    if(!SupportSoldierArmsReady() || !Profile())return false;
    Records records;
    npctier::Inventory inv;
    if(!ReadInventory(records,inv,false))return false;
    const npctier::Arms r=npctier::PresetHand(inv,right),l=npctier::PresetHand(inv,left);
    if(!r.on || !l.on)return false;
    *arms=npctier::PackArms(r);*offhand=npctier::PackArms(l);
    return true;
}


bool LocalPlayerLoadout(int* cls,int* equipped,int slots) noexcept {
    Records records;npctier::Inventory inv;int c=-1;
    if(!ReadInventory(records,inv,true,&c) || c<0 || c>=4)return false;
    if(cls)*cls=c;
    for(int i=0;equipped && i<slots;++i)equipped[i]=i<npctier::kWeaponSlots ? inv.equipped[i] : -1;
    return true;
}

void CommanderRequesterArms(int kind,support_net::RequesterArms* out) noexcept {
    if(!out)return;
    *out=support_net::RequesterArms{};
    if(kind<0 || kind>=static_cast<int>(support_net::kArmsKinds) || !SupportSoldierArmsReady() || !Profile())return;
    Records records;npctier::Inventory inv;
    if(!ReadInventory(records,inv,true))return;
    out->kind[kind]=npctier::PackArms(npctier::PickBase(static_cast<SupportWeapon>(kind),inv));
    Log("COMMANDER arms: kind %d carries the player's weapon %u",kind,static_cast<unsigned>(out->kind[kind]&0xFFFFu));
}

void LocalRequesterArms(support_net::RequesterArms* out) noexcept {
    if(!out)return;
    *out=support_net::RequesterArms{};
    // Without the preload (its bytes not as read, logged by PreloadNpcArms) no weapon could be loaded for a soldier:
    // none is given, as without the weapon hooks (a mission crew would otherwise ask for it every frame, refused).
    if(!SupportSoldierArmsReady() || !Profile())return;
    Records records;
    npctier::Inventory inv;
    if(!ReadInventory(records,inv,true))return;
    for(int k=0;k<static_cast<int>(support_net::kArmsKinds);++k) {
        const auto kind=static_cast<SupportWeapon>(k);
        if(!npctier::KindArmed(kind))continue;
        const npctier::Hands hands=npctier::PickHands(kind,inv);
        out->kind[k]=npctier::PackArms(hands.main);
        out->offhand[k]=npctier::PackArms(hands.off);
    }
    if(Cfg().debug) {
        for(int k=0;k<static_cast<int>(support_net::kArmsKinds);++k)if(out->kind[k])
            Log("SUPPORT arms: kind %d gets weapon %u stars %08llX, second hand %u stars %08llX",k,static_cast<unsigned>(out->kind[k]&0xFFFFu),
                static_cast<unsigned long long>((out->kind[k]>>16)&0xFFFFFFFFull),
                out->offhand[k] ? static_cast<unsigned>(out->offhand[k]&0xFFFFu) : 0xFFFFu,
                static_cast<unsigned long long>((out->offhand[k]>>16)&0xFFFFFFFFull));
    }
}

void PreloadNpcArms() noexcept {
    Reset();
    if(!Profile() || !SupportSoldierArmsReady()) {
        if(!Profile())Log("SUPPORT arms: the resource preload (EDF+%X / EDF+%X) is not as read: soldiers keep their stock weapons",kPreload,kLoadDone);
        return;
    }
    // The default crew weapon can be needed as soon as the mission starts. Preparing one of every class and hand
    // blocks the loading screen even when no support is called. The other loadouts already wait in NpcArmsLoaded.
    Records records;
    npctier::Inventory inv;
    if(!ReadInventory(records,inv,true))return;
    constexpr auto kind=SupportWeapon::rifle;
    const auto arms=npctier::PickArms(kind,inv);
    wchar_t path[96];
    if(arms.on && npctier::ArmsPath(kind,arms,path,std::size(path)) && Queue(path)) {
        loaded.Add(path);   // the game's WaitPreload completes this before the mission begins
        Log("SUPPORT arms: default rifle %ls queued with the mission's preload",path);
    }
}

support_net::PlanResult NpcArmsLoaded(const support_net::Plan& plan) noexcept {
    using support_net::PlanResult;
    Follow();
    bool waiting=false;
    for(std::uint32_t i=0;i<plan.count && i<support_net::kMaxUnits;++i) {
        const auto& unit=plan.units[i];
        if(!unit.arms && !unit.offhand)continue;
        if(!unit.arms || !IsSupportSoldierResource(unit.resourceId))return PlanResult::refused;
        const auto kind=SupportSoldierWeapon(unit.resourceId);
        for(int hand=0;hand<2;++hand) {
            const std::uint64_t word=hand ? unit.offhand : unit.arms;
            if(!word)continue;
            const Load l=Ask(word,kind,hand!=0);
            if(l==Load::refused)return PlanResult::refused;
            waiting=waiting || l==Load::waiting;
        }
    }
    if(!waiting)return PlanResult::ready;
    bool done=false;
    __try {
        void* const mgr=At<void*>(image,kPreloadMgr);
        done=mgr && loadDoneFn && loadDoneFn(mgr);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("SUPPORT arms: the load completion query faulted: refused");return PlanResult::refused;}
    if(!done)return PlanResult::pending;
    for(int i=0;i<asked.count;++i)loaded.Add(asked.path[i]);
    Log("SUPPORT arms: %d weapon loads complete",asked.count);
    asked.count=0;
    return PlanResult::ready;
}
}  // namespace crew
