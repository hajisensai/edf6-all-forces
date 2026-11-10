#include "mission_crew_support.h"
#include "mission_crew.h"
#include "mission_crew_team.h"
#include "mission_crew_quiet.h"
#include "mission_crew_radar.h"
#include "support_soldier.h"
#include "support_loadout.h"
#include "support_variants.h"
#include "real_driver_native.h"
#include "online_authority.h"
#include "memory.h"
#include "layout.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kSoldier=1,kLeader=2;
constexpr std::size_t kRiding=0x1548;   // Human: the vehicle it rides (weak_ptr object; control block +0x1550)
static_assert(kSoldier==kSupportRangerResource && kLeader==kSupportLeaderResource,"the crew are the rifle templates' soldier resources");
// quiet[i]: this machine cleared crew[i]'s random-talk bit while it sat in the vehicle and owes it back
// (mission_crew_quiet.h; 测试站 #41). aboard[i]: crew[i] sat in the deployment's own live vehicle at the last look (no
// radar dot of its own then: mission_crew_radar.h).
struct Deployment { std::uint64_t transaction; ObjRef vehicle; ObjRef crew[15]; unsigned count; bool applied,remote,restorePending; bool boarded[15],quiet[15],aboard[15]; };
Deployment deployments[128]{};
static_assert(sizeof(deployments)/sizeof(deployments[0])*15==crewradar::kMost,"the radar's hidden set holds every soldier");
bool teamReady=false;
std::uint64_t nextOffline=0x8000000000000000ULL;
bool Exists(const ObjRef& r) noexcept {return r.obj && Readable(r.obj,kObjectFlags+4) && r.Is(r.obj);}
bool Alive(const ObjRef& r) noexcept {
    return r.obj && Readable(r.obj,kSelfCtrl+8) && r.Is(r.obj) && !At<unsigned char>(r.obj,kDead);
}
// Each of the deployment's soldiers: quiet while it sits in the deployment's own vehicle, as it was once off it.
// `release`: the deployment lets them go (rolled back): every quieted one gets its bit back now.
void QuietCrew(Deployment& d,bool release) noexcept {
    const bool vehicleLive=!release && d.applied && Cfg().enabled && Alive(d.vehicle);
    for(unsigned i=0;i<d.count;++i) {
        const bool exists=Exists(d.crew[i]);
        auto* h=static_cast<unsigned char*>(const_cast<void*>(d.crew[i].obj));
        const bool aboard=vehicleLive && exists && !h[kDead] && Readable(h,kRiding+8) &&
            At<const void*>(h,kRiding)==d.vehicle.obj;
        const std::uint32_t flags=exists ? At<std::uint32_t>(h,kObjectFlags) : 0;
        const crewquiet::Step step=crewquiet::Next(exists,aboard,flags,d.quiet[i]);
        if(step.write)Put<std::uint32_t>(h,kObjectFlags,step.flags);
        d.quiet[i]=step.quieted;
        d.aboard[i]=aboard;
    }
}
// The radar's hidden set: every soldier seated in its deployment's vehicle now, published whole (mission_crew_radar.h).
void PublishRadar() noexcept {
    static const void* objects[crewradar::kMost];
    unsigned count=0;
    for(const auto& d:deployments) {
        if(!d.transaction || !d.applied)continue;
        for(unsigned i=0;i<d.count;++i)if(d.aboard[i] && d.crew[i].obj)objects[count++]=d.crew[i].obj;
    }
    MissionCrewRadarPublish(objects,count);
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
        // Its colours (SupportCrewColour_MISSION, else SupportCrewColour): a look variant as a support crew's
        // (support_variants.h SupportCrewVariant). An older peer ignores it and makes the stock soldier.
        unit.variant=SupportCrewVariant(support_net::kMissionCrewCatalog,unit.resourceId);
        unit.matrix[0]=unit.matrix[5]=unit.matrix[10]=unit.matrix[15]=1;
        std::memcpy(unit.matrix+12,at,12);
    }
    return out->count>1;
}

void BoardPending(Deployment& d) noexcept {
    if(!d.applied || !Alive(d.vehicle) || (InSession() && !SupportTransactionActive(d.transaction)))return;
    for(unsigned i=0;i<d.count;++i)if(Alive(d.crew[i]))HoldSupportSoldier(d.crew[i],false);
    if(d.remote)return;
    auto* v=static_cast<unsigned char*>(const_cast<void*>(d.vehicle.obj));
    if(d.restorePending) {
        for(unsigned i=0;i<d.count;++i)if(Alive(d.crew[i]))
            NpcRestoreMissionSeat(v,static_cast<unsigned char*>(const_cast<void*>(d.crew[i].obj)));
        d.restorePending=false;
    }
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
        // Coloured when the host applied its look and the file is here; else stock (and listed): colours never refuse.
        wchar_t path[128];
        const wchar_t* look=SupportSoldierLookPath(u.resourceId,u.variant,remote,path,sizeof(path)/sizeof(path[0]));
        if(!ApplySupportSoldierResource(u.matrix,u.resourceId,InSession() ? u.netId : nullptr,false,&soldier,look)) {
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
        if(d.applied){
            d.restorePending=d.restorePending || restored;
            BoardPending(d);return true;
        }
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
        if(u.variant && !LookOfVariant(u.variant,nullptr))return false;   // a crew carries a look or nothing
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
    QuietCrew(*d,true);   // its soldiers' talk as it was, before they go (symmetric with the aboard clear)
    // The release cleared every aboard[i]: the radar's hidden set drops them before they are deleted, so the radar's
    // thread never compares against an object already gone (or reused).
    PublishRadar();
    for(unsigned i=0;i<d->count;++i)DeleteSupportSoldier(d->crew[i]);
    *d=Deployment{};
    if(Alive(v))RetryMissionCrew(static_cast<unsigned char*>(const_cast<void*>(v.obj)));
}
void MissionCrewQuietTick() noexcept {
    for(auto& d:deployments)if(d.transaction && d.applied)QuietCrew(d,false);
    PublishRadar();
}
bool MissionCrewRiding(const unsigned char* v,const unsigned char* h) noexcept {
    if(!v || !h)return false;
    for(const auto& d:deployments) {
        if(!d.transaction || !d.applied || !d.vehicle.Is(v))continue;
        for(unsigned i=0;i<d.count;++i)if(d.crew[i].Is(h))return true;
    }
    return false;
}
void ResetMissionCrewSupport() noexcept {
    // Mission teardown owns scene deletion; discard borrowed refs rather than deleting unrelated new objects.
    // A quieted soldier's bit is not given back either: its object is the teardown's (mission_crew_quiet.h).
    for(auto& d:deployments)d=Deployment{};
    MissionCrewRadarPublish(nullptr,0);
    nextOffline=0x8000000000000000ULL;
}
void InstallMissionCrewSupport() noexcept {
    teamReady=InstallMissionCrewTeam(&TeamChanged);
    InstallMissionCrewRadar();
    ConfigureMissionCrew(&Request,&ReadNativeObjectId,&Tick);
    SetNpcMissionCrew(&MissionCrewRiding);
}
}

