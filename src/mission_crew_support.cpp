#include "mission_crew_support.h"
#include "mission_crew.h"
#include "mission_crew_team.h"
#include "support_soldier.h"
#include "real_driver_native.h"
#include "online_authority.h"
#include "memory.h"
#include "layout.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kSoldier=1,kLeader=2;
struct Deployment { std::uint64_t transaction; ObjRef vehicle; ObjRef crew[15]; unsigned count; bool applied,remote; bool boarded[15]; };
Deployment deployments[128]{};
bool teamReady=false;
std::uint64_t nextOffline=0x8000000000000000ULL;
bool Alive(const ObjRef& r) noexcept {
    return r.obj && Readable(r.obj,kSelfCtrl+8) && r.Is(r.obj) && !At<unsigned char>(r.obj,kDead);
}
Deployment* Find(std::uint64_t tx) noexcept {
    for(auto& d:deployments)if(d.transaction==tx)return &d;
    return nullptr;
}
Deployment* Free() noexcept {for(auto& d:deployments)if(!d.transaction)return &d;return nullptr;}

bool Build(unsigned char* v,SupportPlan* out) noexcept {
    if(!v || !out || !Readable(v,kMatrix+64) || v[kDead] || !SupportSoldiersReady() || !teamReady)return false;
    *out=SupportPlan{};out->catalogId=support_net::kMissionCrewCatalog;out->count=1;
    auto& vehicle=out->units[0];vehicle.resourceId=support_net::kExistingVehicle;
    std::memcpy(vehicle.matrix,v+kMatrix,64);std::memcpy(out->target,v+kPosition,12);
    if(InSession() && !ReadNativeObjectId(v,vehicle.netId))return false;
    for(unsigned seat=0;seat<SeatCount(v);++seat) {
        const auto* s=SeatAt(v,seat);
        if((At<unsigned>(s,0x30)&At<unsigned>(s,0x34)&1)==0)continue;
        if(SeatRider(s)!=Rider::none && SeatRider(s)!=Rider::dummy)continue;
        if(out->count>=support_net::kMaxUnits)return false; // never silently leave an armed seat staffed by a Dummy
        float at[3],reach=0;
        if(!SeatPoint(v,seat,at,&reach) || !std::isfinite(at[0]+at[1]+at[2]+reach) || reach<=0)return false;
        auto& unit=out->units[out->count++];unit.resourceId=seat==0 ? kLeader : kSoldier;unit.role=1;
        unit.matrix[0]=unit.matrix[5]=unit.matrix[10]=unit.matrix[15]=1;
        std::memcpy(unit.matrix+12,at,12);
    }
    return out->count>1;
}

void BoardPending(Deployment& d) noexcept {
    if(!d.applied || d.remote || !Alive(d.vehicle))return;
    auto* v=static_cast<unsigned char*>(const_cast<void*>(d.vehicle.obj));
    unsigned char* humans[15]{};int n=0;
    for(unsigned i=0;i<d.count;++i)if(Alive(d.crew[i])) {
        auto* h=static_cast<unsigned char*>(const_cast<void*>(d.crew[i].obj));
        if(At<const void*>(h,0x1548)==v)d.boarded[i]=true;
        else if(!d.boarded[i])humans[n++]=h;
    }
    if(n)NpcBoardCrew(v,humans,n);
}
void TeamChanged(void* object,int team) noexcept {
    for(auto& d:deployments)if(d.applied && d.vehicle.Is(object))
        for(unsigned i=0;i<d.count;++i)if(Alive(d.crew[i])) {
            auto* h=static_cast<unsigned char*>(const_cast<void*>(d.crew[i].obj));
            if(!d.boarded[i] || At<const void*>(h,0x1548)==object)SetObjectTeam(h,team);
        }
}
void Tick(unsigned char* v) noexcept {for(auto& d:deployments)if(d.transaction && d.vehicle.Is(v))BoardPending(d);}

bool Apply(std::uint64_t tx,const SupportPlan& plan,bool remote,unsigned char* v) noexcept {
    if(!v || !tx || !SupportSoldiersReady())return false;
    Deployment* d=Find(tx);
    if(d && d->applied)return d->vehicle.Is(v);
    if(!d)d=Free();
    if(!d)return false;
    *d=Deployment{};d->transaction=tx;d->vehicle=ObjRef::Of(v);d->remote=remote;
    for(unsigned i=1;i<plan.count;++i) {
        const auto& u=plan.units[i];ObjRef soldier;
        if(!ApplySupportSoldierSpawn(u.matrix,u.resourceId==kLeader,InSession() ? u.netId : nullptr,&soldier)) {
            DestroyMissionCrewPlan(tx);return false;
        }
        d->crew[d->count++]=soldier;
        const int team=At<int>(v,kTeam);
        SetObjectTeam(static_cast<unsigned char*>(const_cast<void*>(soldier.obj)),team==kTeamVehicle ? kTeamFriend : team);
    }
    // Only after all real actors exist. The old route, script bindings and vehicle object are untouched.
    if(!remote)ReleaseLegacyMissionRiders(v);
    d->applied=true;BoardPending(*d);return true;
}

bool Request(unsigned char* v,bool restored) noexcept {
    for(auto& d:deployments)if(d.transaction && d.vehicle.Is(v)) {
        if(d.applied){if(restored)for(auto& seated:d.boarded)seated=false;BoardPending(d);return true;}
        return true; // the same unified network transaction is still being prepared
    }
    SupportPlan plan;if(!Build(v,&plan))return false;
    if(!InSession())return Apply(++nextOffline,plan,false,v);
    unsigned char anchor[32]{};
    if(!OnlineHostOnly() || !PlayerHuman() || !ReadNativeObjectId(PlayerHuman(),anchor))return false;
    Deployment* slot=Free();if(!slot)return false;
    const auto tx=SubmitPreparedSupportPlan(plan);
    if(!tx)return false;
    // Single-peer commits may run Apply synchronously from SubmitPrepared; do not erase their real objects.
    if(!Find(tx)){*slot=Deployment{};slot->transaction=tx;slot->vehicle=ObjRef::Of(v);}
    return true;
}
}

bool ValidateMissionCrewPlan(const SupportPlan& p) noexcept {
    if(p.catalogId!=support_net::kMissionCrewCatalog || p.count<2 || p.count>support_net::kMaxUnits ||
       p.units[0].resourceId!=support_net::kExistingVehicle || p.units[0].role)return false;
    auto* v=MissionVehicleById(p.units[0].netId);
    if(!v || p.count-1>SeatCount(v))return false;
    for(unsigned i=1;i<p.count;++i) {
        const auto& u=p.units[i];
        if((u.resourceId!=kSoldier && u.resourceId!=kLeader) || u.role!=1)return false;
        bool entrance=false;
        for(unsigned seat=0;seat<SeatCount(v);++seat) {
            float at[3],reach=0;
            if(!SeatPoint(v,seat,at,&reach) || reach<=0)continue;
            const float dx=u.matrix[12]-at[0],dy=u.matrix[13]-at[1],dz=u.matrix[14]-at[2];
            if(std::isfinite(dx+dy+dz) && dx*dx+dy*dy+dz*dz<=(reach+1)*(reach+1))entrance=true;
        }
        if(!entrance)return false;
    }
    return true;
}

bool ApplyMissionCrewPlan(std::uint64_t tx,const SupportPlan& p,bool remote) noexcept {
    return ValidateMissionCrewPlan(p) && Apply(tx,p,remote,MissionVehicleById(p.units[0].netId));
}
void DestroyMissionCrewPlan(std::uint64_t tx) noexcept {
    Deployment* d=Find(tx);if(!d)return;
    const ObjRef v=d->vehicle;
    for(unsigned i=0;i<d->count;++i)DeleteSupportSoldier(d->crew[i]);
    *d=Deployment{};
    if(Alive(v))RetryMissionCrew(static_cast<unsigned char*>(const_cast<void*>(v.obj)));
}
void ResetMissionCrewSupport() noexcept {
    // Mission teardown owns scene deletion; discard borrowed refs rather than deleting unrelated new objects.
    for(auto& d:deployments)d=Deployment{};
    nextOffline=0x8000000000000000ULL;
}
void InstallMissionCrewSupport() noexcept {
    teamReady=InstallMissionCrewTeam(&TeamChanged);
    ConfigureMissionCrew(&Request,&ReadNativeObjectId,&Tick);
}
}

