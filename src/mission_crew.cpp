#include "mission_crew.h"
#include "real_driver_native.h"
#include "crew.h"
#include "memory.h"
#include "online_authority.h"
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kStockRide=0x633030,kProteusRide=0x6490C0;
constexpr std::size_t kRideSlot=50;
constexpr unsigned char kRideSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8D};
using RideFn=void(__fastcall*)(void*,bool);
struct Hooked { unsigned vtable; RideFn original; };
Hooked hooks[64]{};std::size_t hookCount=0;
bool installed=false;
MissionCrewFactoryFn factory=nullptr;MissionVehicleIdFn readId=nullptr;MissionCrewTickFn tick=nullptr;
struct Entry { ObjRef vehicle; bool requested,dispatched,restored; };
Entry entries[256]{};

Entry* Observe(unsigned char* v) noexcept {
    Entry* free=nullptr;
    for(auto& e:entries) {
        if(e.vehicle.Is(v))return &e;
        if(!free && (!e.vehicle || !Readable(e.vehicle.obj,kSelfCtrl+8) || !e.vehicle.Is(e.vehicle.obj)))free=&e;
    }
    if(!free)return nullptr;
    *free=Entry{};free->vehicle=ObjRef::Of(v);return free;
}

void __fastcall RideHook(void* object,bool spawned) noexcept {
    auto* v=static_cast<unsigned char*>(object);
    if(!Cfg().enabled) {
        for(std::size_t i=0;i<hookCount;++i)if(At<const void*>(v,0)==image+hooks[i].vtable) {
            hooks[i].original(v,spawned);return;
        }
        return;
    }
    EnsureInputs(); // original scripts can create a vehicle before the first per-frame input
    if(!installed || !PrepareNpcVehicle(v,spawned))return;
    Entry* e=Observe(v);
    if(!e)return;
    // Native scripts and snapshot restore both use slot 50. Keep that origin mode only once all
    // participating vtables are hooked, so a restore can never manufacture a Dummy again.
    Put<int>(v,0xE30,1);
    if(NpcDriver(v) && SeatRider(SeatAt(v,0))!=Rider::dummy)return;
    e->requested=true;e->dispatched=false;e->restored=!spawned;
}
}

void ConfigureMissionCrew(MissionCrewFactoryFn f,MissionVehicleIdFn ids,MissionCrewTickFn step) noexcept {factory=f;readId=ids;tick=step;}
void RetryMissionCrew(unsigned char* v) noexcept {if(Entry* e=Observe(v))e->dispatched=false;}

bool InstallMissionCrewHooks(const unsigned* tables,std::size_t count) noexcept {
    if(installed)return true;
    if(!tables || count==0 || count>64 || !Matches(kStockRide,kRideSig,sizeof(kRideSig)))return false;
    // Check the complete set before any mutation. Proteus creates extra Dummies inside its own
    // wrapper, so patch that wrapper too rather than just its call to the common implementation.
    for(std::size_t i=0;i<count;++i) {
        auto* slot=reinterpret_cast<void**>(image+tables[i])+kRideSlot;
        if(!Readable(slot,8) || (*slot!=image+kStockRide && *slot!=image+kProteusRide))return false;
    }
    for(std::size_t i=0;i<count;++i) {
        auto* slot=reinterpret_cast<void**>(image+tables[i])+kRideSlot;
        void* old=*slot;hooks[i]={tables[i],reinterpret_cast<RideFn>(old)};
        if(!PatchVtableSlot(slot,old,reinterpret_cast<void*>(&RideHook))) {
            for(std::size_t j=0;j<i;++j)PatchVtableSlot(reinterpret_cast<void**>(image+hooks[j].vtable)+kRideSlot,
                reinterpret_cast<void*>(&RideHook),reinterpret_cast<void*>(hooks[j].original));
            hookCount=0;return false;
        }
        hookCount=i+1;
    }
    installed=true;Log("MISSION CREW %zu stock RideAi slots replaced by real-crew deployment",count);return true;
}

void MissionCrewVehicleFrame(unsigned char* v) noexcept {
    if(!installed || !Cfg().enabled || !v || v[kDead])return;
    Entry* e=Observe(v);if(!e)return;
    if(tick)tick(v);
    // Handles a mission already running when the plugin initialized, without disturbing its route.
    for(unsigned i=0;i<SeatCount(v);++i)if(SeatRider(SeatAt(v,i))==Rider::dummy)e->requested=true;
    if(e->requested && !e->dispatched && factory && OnlineMaySeatNpc(v))e->dispatched=factory(v,e->restored);
}

unsigned char* MissionVehicleById(const unsigned char* id) noexcept {
    if(!id || !readId)return nullptr;
    for(const auto& e:entries) {
        auto* v=static_cast<unsigned char*>(const_cast<void*>(e.vehicle.obj));
        if(!v || !Readable(v,kSelfCtrl+8) || !e.vehicle.Is(v) || v[kDead])continue;
        unsigned char current[32]{};
        if(readId(v,current) && std::memcmp(current,id,32)==0)return v;
    }
    return nullptr;
}

void ReleaseLegacyMissionRiders(unsigned char* v) noexcept {
    if(!v || !OnlineMaySeatNpc(v))return;
    for(unsigned i=0;i<SeatCount(v);++i) {
        auto* seat=SeatAt(v,i);
        if(SeatRider(seat)==Rider::dummy)reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kSeatKick)(v,seat);
    }
}
void ResetMissionCrew() noexcept {for(auto& e:entries)e=Entry{};}
}
