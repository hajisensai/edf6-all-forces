// The Vehicler's V4..V9 at run time (zw_vehicle_gear.h; docs/newclass-re/vehicler-v4v9-plan.md).
#include "zw_vehicle_gear.h"
#include "zw_heat_logic.h"
#include "zw_unlock_logic.h"
#include "zw_vehicle_catalog.h"
#include "zw_vehicle_dispatch.h"
#include "campaign.h"
#include "edf41classes.h"
#include "edf_layout.h"
#include "firstperson.h"
#include "memory.h"
#include "mission_packs.h"
#include "netprobe.h"
#include "objref.h"
#include "online_authority.h"
#include "plugin_log.h"
#include "ride_native.h"
#include "scene.h"
#include "soldier_classes_logic.h"
#include "soldier_factory.h"
#include "unlock_ported.h"
#include "weapon_equip.h"
#include "weapon_records.h"
#include <cmath>
#include <cstdint>

namespace crew {
namespace {
constexpr unsigned kGameStatus=0x20B2890;
constexpr std::size_t kRecordSize=12;
// Vehicle weapon holders (support_firepower.cpp): +0x638 the array, +0x648 its count, 0x48 apart; a holder's weapon at +0x10,
// its damage multiplier at +0x40. The weapon: +0xBE8 rounds left (int), +0xE0C cooldown frames (float; katyusha_rack.h).
constexpr std::size_t kHolders=0x638,kHolderCount=0x648,kHolderStride=0x48,kHolderWeapon=0x10,kHolderDamage=0x40;
constexpr std::size_t kWeaponRounds=0xBE8,kWeaponCooldown=0xE0C;
constexpr int kLocalMost=2,kHullsMost=16,kZonesMost=8;

bool ok=false;

// ---- unlocks ----
// Own row `id`'s record of `gs`; true when it was not owned before.
bool OwnRow(unsigned char* gs,const wchar_t* id) noexcept {
    const int row=wequip::FindRow(id);
    if(row<0 || static_cast<std::uint32_t>(row)>=WeaponRecordCapacity())return false;
    unsigned char* const record=WeaponRecord(gs,static_cast<std::uint32_t>(row));
    if(!record || !Readable(record,kRecordSize,true))return false;
    return ported::Own(record);
}
unsigned char* LiveStatus() noexcept {
    return image && Readable(image+kGameStatus,8) ? At<unsigned char*>(image,kGameStatus) : nullptr;
}

// ---- capture ----
bool captureLogged[kZwVehicleMost]{};

void CaptureBy(unsigned char* human) noexcept {
    if(!human || soldier::ZwType(human)!=zwclass::kVehicler || human[kDead])return;
    unsigned char* hulls[kHullsMost]{};int kinds[kHullsMost]{};
    const int n=ZwMadeHulls(hulls,kinds,kHullsMost);
    const void* riding=At<const void*>(human,ride_native::kHumanRiding);
    const float* me=reinterpret_cast<const float*>(human+kPosition);
    for(int i=0;i<n;++i) {
        zwunlock::CaptureFacts f;
        f.kind=kinds[i];
        f.vehicler=true;
        f.alive=!hulls[i][kDead];
        f.aboard=riding==hulls[i];
        const float* at=reinterpret_cast<const float*>(hulls[i]+kPosition);
        const float dx=at[0]-me[0],dy=at[1]-me[1],dz=at[2]-me[2];
        f.distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        f.owned=ZwUnlockedKinds(0);
        if(!zwunlock::Captures(f))continue;
        unsigned char* const gs=LiveStatus();
        const bool fresh=gs && OwnRow(gs,kZwVehicles[f.kind].row);
        NoteZwUnlocked(f.kind);
        if(!captureLogged[f.kind]) {
            captureLogged[f.kind]=true;
            Log("ZW gear: %ls captured (%s, %.1f m): %s",kZwVehicles[f.kind].row,f.aboard ? "aboard" : "beside it",f.distance,
                fresh ? "owned now" : gs ? "its record owned already / not found" : "no game status");
        }
    }
}

// ---- V7 heat ----
struct HeatRec { WeakRef hull{}; zwheat::Heat heat{}; std::int32_t rounds=-1; float base[8]{}; int holders=0; };
HeatRec heats[kLocalMost]{};

HeatRec* HeatFor(unsigned char* v) noexcept {
    for(auto& h:heats)if(h.hull && h.hull.Is(v))return &h;
    for(auto& h:heats)if(!h.hull || !objref::Exists(h.hull)){h=HeatRec{};h.hull=ObjRef::Of(v);return &h;}
    return nullptr;
}

void HeatStep(unsigned char* v) noexcept {
    HeatRec* r=HeatFor(v);
    if(!r)return;
    __try {
        auto* const holders=At<unsigned char*>(v,kHolders);
        const auto count=At<std::size_t>(v,kHolderCount);
        if(!holders || !count || count>8 || !Readable(holders,count*kHolderStride))return;
        if(!r->holders) {   // the first frame: the holders' multipliers as the setup / firepower left them
            r->holders=static_cast<int>(count);
            for(std::size_t i=0;i<count;++i)r->base[i]=At<float>(holders+i*kHolderStride,kHolderDamage);
        }
        unsigned char* const weapon=At<unsigned char*>(holders,kHolderWeapon);
        if(!weapon || !Readable(weapon+kWeaponCooldown,4,true))return;
        const std::int32_t rounds=At<std::int32_t>(weapon,kWeaponRounds);
        const bool firing=r->rounds>=0 && rounds<r->rounds;
        r->rounds=rounds;
        const bool wasOver=r->heat.overheated;
        zwheat::Step(r->heat,firing);
        if(r->heat.overheated && At<float>(weapon,kWeaponCooldown)<zwheat::kHoldCooldown)
            Put<float>(weapon,kWeaponCooldown,zwheat::kHoldCooldown);
        const float k=zwheat::Damage(r->heat);
        for(int i=0;i<r->holders && i<static_cast<int>(count);++i)Put<float>(holders+i*kHolderStride,kHolderDamage,r->base[i]*k);
        if(r->heat.overheated!=wasOver)Log("ZW gear: 1K17 %p %s",static_cast<void*>(v),r->heat.overheated ? "overheated" : "cooled, fires again");
    } __except(EXCEPTION_EXECUTE_HANDLER){r->hull=ObjRef{};}
}

void HeatBy(unsigned char* human) noexcept {
    if(!human)return;
    auto* const v=At<unsigned char*>(human,ride_native::kHumanRiding);
    if(!v)return;
    const ZwVehicleSpec* s=ZwVehicleInfo(ZwVehicleKind(v));
    if(s && s->behavior==ZwBehavior::heat)HeatStep(v);
}

// ---- V8 fallout ----
struct Watch { WeakRef hull{}; bool zoned=false; };
Watch watches[kHullsMost]{};
struct ZoneRec { WeakRef wreck{}; zwheat::Zone zone{}; };
ZoneRec zones[kZonesMost]{};

void Fallout() noexcept {
    unsigned char* hulls[kHullsMost]{};int kinds[kHullsMost]{};
    const int n=ZwMadeHulls(hulls,kinds,kHullsMost);
    for(int i=0;i<n;++i) {
        const ZwVehicleSpec* s=ZwVehicleInfo(kinds[i]);
        if(!s || s->behavior!=ZwBehavior::fallout)continue;
        Watch* w=nullptr;
        for(auto& x:watches)if(x.hull && x.hull.Is(hulls[i])){w=&x;break;}
        if(!w)for(auto& x:watches)if(!x.hull || !objref::Exists(x.hull)){x=Watch{};x.hull=ObjRef::Of(hulls[i]);w=&x;break;}
        if(!w || w->zoned || !hulls[i][kDead])continue;
        w->zoned=true;
        ZoneRec* z=nullptr;
        for(auto& x:zones)if(x.zone.left<=0){z=&x;break;}
        if(!z){Log("ZW gear: TV-8 %p destroyed: %d zones already, none here",static_cast<void*>(hulls[i]),kZonesMost);continue;}
        z->wreck=ObjRef::Of(hulls[i]);
        z->zone=zwheat::StartZone(reinterpret_cast<const float*>(hulls[i]+kPosition));
        Log("ZW gear: TV-8 %p destroyed: radiation zone at (%.1f, %.1f, %.1f), %.0f m, %d s",static_cast<void*>(hulls[i]),
            z->zone.pos[0],z->zone.pos[1],z->zone.pos[2],zwheat::kZoneRadius,zwheat::kZoneFrames/60);
    }
    for(auto& z:zones) {
        if(z.zone.left<=0)continue;
        if(!objref::Exists(z.wreck)){z.zone.left=0;Log("ZW gear: radiation zone over (its wreck left the scene)");continue;}
        if(!zwheat::Tick(z.zone))continue;
        if(!edf41c::Blast(z.wreck.Obj(),z.zone.pos,zwheat::kZoneRadius,zwheat::kZoneDamage)){z.zone.left=0;Log("ZW gear: radiation zone over (its blast was refused)");}
        else if(z.zone.left<=0)Log("ZW gear: radiation zone over");
    }
}
}  // namespace

bool InstallZwVehicleGear() noexcept {
    ok=true;
    Log("ZW gear on: %d vehicles (V4..V9 unlocks, 1K17 heat, TV-8 fallout)",kZwVehicleCount);
    return true;
}

int GrantZwProgress(unsigned char* gs) noexcept {
    if(!gs)gs=LiveStatus();
    if(!gs || !wequip::Ready())return -1;
    __try {
        if(GameContentId()!=zwunlock::kStoryContent)return -1;
        const unsigned char* const save=gs+packs::kModeSave;
        if(!Readable(save,packs::kModeSaveSize))return -1;
        const auto* clear=save+packs::kClear;
        const int rows=zwunlock::kStoryMissions;
        const int cleared=zwunlock::Cleared(clear,packs::kClearClasses,packs::kMaxRows,rows);
        bool last=false;
        for(std::size_t c=0;c<packs::kClearClasses && !last;++c)last=clear[c*packs::kMaxRows+static_cast<std::size_t>(rows-1)]!=0;
        const std::uint32_t kinds=zwunlock::ProgressKinds(cleared,rows,last);
        int owned=0;
        for(int k=0;k<kZwVehicleCount;++k)
            if(kinds>>k&1u && OwnRow(gs,kZwVehicles[k].row)) {
                ++owned;
                Log("ZW gear: %ls unlocked (EDF6 story %d / %d cleared%s)",kZwVehicles[k].row,cleared,rows,last ? ", the last one" : "");
            }
        return owned;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("ZW gear: the progress grant faulted");return -1;}
}

void ZwVehicleGearTick() noexcept {
    if(!ok || !soldier::FactoriesReady())return;
    unsigned char* humans[kLocalMost]{};
    int n=0;
    if(InSession()){humans[0]=PlayerHuman();n=humans[0] ? 1 : 0;}
    else n=LocalPlayers(humans,kLocalMost);
    if(!n && PlayerHuman()){humans[0]=PlayerHuman();n=1;}
    __try {
        for(int i=0;i<n;++i){CaptureBy(humans[i]);HeatBy(humans[i]);}
        if(OnlineHostOnly())Fallout();
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

void ResetZwVehicleGear() noexcept {
    for(auto& h:heats)h=HeatRec{};
    for(auto& w:watches)w=Watch{};
    for(auto& z:zones)z=ZoneRec{};
    for(auto& c:captureLogged)c=false;
}
}  // namespace crew
