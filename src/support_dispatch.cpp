// Support requests select and verify an entry before any object exists. The host sends the resulting
// matrices to peers. Real soldiers board stationary hulls, then the existing vehicle controllers enter.
#include "crew.h"
#include "support_call.h"
#include "support_aircraft.h"
#include "support_policy.h"
#include "support_soldier.h"
#include "support_spawn.h"
#include "support_net.h"
#include "mission_crew_support.h"
#include "mission_participants.h"
#include "mission_participant_gate.h"
#include "ground_navigation.h"
#include "npcai.h"
#include "online_authority.h"
#include "memory.h"
#include "hudtext.h"
#include <cwchar>
#include <cstring>

namespace crew {
// Implemented by the real-crew adapter; no imaginary rider participates in deployment.
int NpcBoardCrew(unsigned char*,unsigned char* const*,int) noexcept;
bool NpcPrepareVehicleRoutePost(unsigned char*,const float*,float) noexcept;
bool NpcReleaseVehicleCrew(unsigned char*) noexcept;
bool RegisterSupportObject(const void*,const unsigned char*) noexcept;
void ReportSupportFailure(std::uint64_t) noexcept;
bool SupportTransactionActive(std::uint64_t) noexcept;
bool HoldSupportSoldier(const ObjRef&,bool) noexcept;
namespace {
constexpr std::uint32_t kSoldier=kSupportRangerResource,kLeader=kSupportLeaderResource,
    kAircraft=kSupportAircraftResource,kVehicle=kSupportVehicleResource;
constexpr int kDeployments=16;
constexpr ULONGLONG kBoardLimit=120000,kCallCooldown=30000;
constexpr float kVehicleWaypointRadius=1.5f,kVehicleDriverHold=1.0f,kVehicleArrival=5.0f;
constexpr float kInfantryWaypointRadius=1.5f,kInfantryRouteStop=1.0f,kInfantryArrival=3.0f;
static_assert(kInfantryRouteStop<kInfantryWaypointRadius,"leader must advance before quiet movement stops");
static_assert(kVehicleDriverHold<kVehicleWaypointRadius,"a route must advance before its driver stops");
struct Planning {
    bool active=false;std::uint32_t catalog=0;float target[3]{};
    int edge=0;npc::navigation::State navigation{};
};
Planning planning{};
struct InfantryRoute {
    npc::navigation::State navigation{};
    bool given=false,done=false,released=false;
};
struct Deployment {
    bool used=false,remote=false,started=false,delivered=false,networked=false,assigned=false;
    std::uint64_t id=0;ULONGLONG born=0;
    SupportPlan plan{};ObjRef objects[support_net::kMaxUnits]{};
    npc::navigation::State navigation{};
    float vehicleSample[3]{};ULONGLONG vehicleSampleAt=0;
    InfantryRoute infantry[3]{}; // one bounded coarse route per leader, not per follower
};
Deployment deployments[kDeployments]{};
bool offlinePending=false;std::uint64_t nextOffline=1;ULONGLONG callAt=0;
wchar_t status[128]{};
struct LocalRequest {
    bool shown=false,acceptNotices=false,hasNotice=false;
    std::uint32_t id=0;
    support_net::RequestStatus state=support_net::RequestStatus::accepted;
    wchar_t text[128]{};
};
LocalRequest localRequest{};
bool configured=false;
ULONGLONG dispatchFrame=~ULONGLONG{0};
void Status(const wchar_t* text) noexcept {_snwprintf_s(status,_countof(status),_TRUNCATE,L"%ls",text);}
void RequestNotice(std::uint32_t request,support_net::RequestStatus state) noexcept {
    using support_net::RequestStatus;
    if(!localRequest.acceptNotices || !request || request<localRequest.id)return;
    if(request==localRequest.id && localRequest.hasNotice) {
        if(localRequest.state>=RequestStatus::refused)return;
        if(localRequest.state==RequestStatus::active && state==RequestStatus::accepted)return;
    }
    const wchar_t* text=nullptr;
    switch(state) {
    case RequestStatus::accepted:text=L"本机支援请求已受理，等待房主及全员确认";break;
    case RequestStatus::active:text=L"本机支援已获全员对象确认，开始部署";break;
    case RequestStatus::refused:text=L"房主拒绝了本机支援请求：环境、资源或当前部署条件不满足";break;
    case RequestStatus::timeout:text=L"本机支援请求超时：未及时获得房主及全员确认";break;
    case RequestStatus::cancelled:text=L"本机这次支援部署已取消";break;
    case RequestStatus::interrupted:text=L"联机会话中断，本机支援请求未完成";break;
    }
    if(!text)return;
    localRequest.id=request;localRequest.state=state;localRequest.shown=localRequest.hasNotice=true;
    _snwprintf_s(localRequest.text,_countof(localRequest.text),_TRUNCATE,L"%ls",text);
}
int AirCount() noexcept {return SupportAirCallCount();}
int GroundStart() noexcept {return AirCount()+2;}
bool InfantryCatalog(std::uint32_t catalog) noexcept {
    return catalog==static_cast<unsigned>(AirCount()) || catalog==static_cast<unsigned>(AirCount()+1);
}
npc::navigation::Profile InfantryRouteProfile() noexcept {
    npc::navigation::Profile profile;profile.cell=4;profile.waypointRadius=kInfantryWaypointRadius;return profile;
}
bool GroundCatalog(std::uint32_t id,SupportVehicleKind& kind,SupportCrewMode& mode) noexcept {
    const int index=static_cast<int>(id)-GroundStart();
    if(index<0 || index>=kSupportVehicleCount*2)return false;
    kind=static_cast<SupportVehicleKind>(index/2);
    mode=index%2 ? SupportCrewMode::unmanned : SupportCrewMode::soldiers;return true;
}
npc::navigation::Profile VehicleRouteProfile(const SupportVehicleSpec& spec) noexcept {
    npc::navigation::Profile profile;
    profile.radius=std::hypot(spec.halfWidth,spec.halfLength);profile.height=spec.height;profile.cell=4;
    profile.waypointRadius=kVehicleWaypointRadius;
    return profile;
}
bool Live(const ObjRef& ref) noexcept {
    return ref && Readable(ref.obj,kDead+1) && ref.Is(ref.obj) && !static_cast<const unsigned char*>(ref.obj)[kDead];
}
void Matrix(const float* at,const float* heading,float* out) noexcept {
    const float matrix[16]={heading[2],0,-heading[0],0,0,1,0,0,heading[0],0,heading[2],0,at[0],at[1],at[2],1};
    std::memcpy(out,matrix,sizeof(matrix));
}
bool Foot(float x,float z,float level,float& floor,float height=2.0f) noexcept {
    if(!MapGroundNear(x,z,level,&floor,true) || !std::isfinite(floor))return false;
    float surface=0.0f;const Sea sea=SeaAt(x,z,&surface);
    if(sea==Sea::unknown || (sea==Sea::water && (!std::isfinite(surface) || surface-floor>npc::navigation::Profile{}.maxWaterDepth)))return false;
    const float from[3]={x,floor+0.08f,z},to[3]={x,floor+height,z};float hit[3];
    return MapRay(from,to,hit)<0;
}
bool AddUnit(SupportPlan& plan,std::uint32_t resource,std::uint32_t parent,const float* at,const float* heading) noexcept {
    if(plan.count>=support_net::kMaxUnits)return false;
    auto& unit=plan.units[plan.count++];unit={};unit.resourceId=resource;unit.role=parent;Matrix(at,heading,unit.matrix);return true;
}
bool AddCrew(SupportPlan& plan,unsigned parent,const float* entry,const float* heading,unsigned count,float width) noexcept {
    for(unsigned i=0;i<count;++i) {
        const float side=width+2.0f,back=-3.0f-static_cast<float>(i)*2.0f;
        float at[3]={entry[0]+heading[2]*side+heading[0]*back,entry[1],entry[2]-heading[0]*side+heading[2]*back};
        if(!Foot(at[0],at[2],entry[1],at[1]) || std::fabs(at[1]-entry[1])>0.55f)return false;
        if(!AddUnit(plan,kSoldier,parent,at,heading))return false;
    }
    return true;
}
unsigned AirCrew(const SupportAircraft& spec) noexcept {
    return spec.heli==static_cast<int>(HeliBody::brute410) || spec.heli==static_cast<int>(HeliBody::medic410) ? 3u : 1u;
}
support_net::PlanResult Plan(std::uint32_t catalog,const float* target,SupportPlan* out) noexcept {
    using support_net::PlanResult;
    if(!out || !target || catalog>=static_cast<unsigned>(SupportCallCount())) {
        Status(L"支援请求或目标无效");return PlanResult::refused;
    }
    if(!Cfg().customNpcAi){Status(L"NPC 指挥功能未启用，无法调度支援机组");return PlanResult::refused;}
    if(!SupportSoldiersReady()){Status(SupportSoldierFailureText());return PlanResult::refused;}
    if(!planning.active || planning.catalog!=catalog || std::memcmp(planning.target,target,12)!=0) {
        planning={};planning.active=true;planning.catalog=catalog;std::memcpy(planning.target,target,12);
    }
    SupportPlan plan{};plan.catalogId=catalog;std::memcpy(plan.target,target,12);
    if(catalog<static_cast<unsigned>(AirCount())) {
        if(!support::Allowed(SupportMissionPolicy(),support::Capability::air)) {
            Status(L"本关限制外部航空支援，或处于地下环境");return PlanResult::refused;
        }
        SupportAircraft spec;support::Route route;
        if(!SupportAircraftSpec(static_cast<int>(catalog),&spec)) {Status(L"该单位尚无可用的实际入场方式");return PlanResult::refused;}
        if(!Cfg().jetAirRaider || !Cfg().npcBoarding || (spec.heli>=0 ? !Cfg().heliPilot : !Cfg().jetPilot)) {
            Status(L"航空支援或真实机组驾驶功能未启用");return PlanResult::refused;
        }
        const auto refusal=PlanAirSupport(static_cast<int>(catalog),target,player.pos,&route);
        if(refusal!=support::Refusal::none) {
            Status(refusal==support::Refusal::noSky ? L"此处没有开放天空，航空支援无法进入" : L"找不到可用的场外跑道或直升机起降点");
            return PlanResult::refused;
        }
        // Aircraft are queued along a verified entry strip, not created above the destination.
        for(int i=0;i<spec.count;++i) {
            float at[3]={route.from[0]+route.heading[0]*static_cast<float>(i)*65.0f,route.from[1],
                         route.from[2]+route.heading[2]*static_cast<float>(i)*65.0f};
            if(!Foot(at[0],at[2],route.from[1],at[1],25))return PlanResult::refused;
            at[1]+=0.1f;
            const unsigned parent=plan.count+1;
            if(!AddUnit(plan,kAircraft+catalog,0,at,route.heading) ||
               !AddCrew(plan,parent,at,route.heading,AirCrew(spec),spec.heli>=0 ? 18.0f : 45.0f))return PlanResult::refused;
        }
        *out=plan;planning.active=false;return PlanResult::ready;
    }
    const auto area=MapPlayArea();
    if(!area.ground){Status(L"地图入口尚未测定");return PlanResult::refused;}
    const float cx=(area.lo[0]+area.hi[0])*0.5f,cz=(area.lo[1]+area.hi[1])*0.5f;
    const float edges[8][2]={{area.lo[0]+30,cz},{area.hi[0]-30,cz},{cx,area.lo[1]+30},{cx,area.hi[1]-30},
        {area.lo[0]+30,area.lo[1]+30},{area.hi[0]-30,area.lo[1]+30},{area.lo[0]+30,area.hi[1]-30},{area.hi[0]-30,area.hi[1]-30}};
    if(planning.edge>=8){Status(L"找不到与目的地连通的地图边缘入口");return PlanResult::refused;}
    float entry[3]={edges[planning.edge][0],target[1],edges[planning.edge][1]};
    const auto nextEdge=[&]() {++planning.edge;planning.navigation={};return PlanResult::pending;};
    if(support::FlatDistance(entry,target)<600 || support::FlatDistance(entry,player.pos)<800 ||
       !Foot(entry[0],entry[2],target[1],entry[1]))return nextEdge();
    SupportVehicleKind kind{};SupportCrewMode mode{};const bool vehicle=GroundCatalog(catalog,kind,mode);
    const auto* spec=vehicle ? SupportVehicleInfo(kind) : nullptr;
    if(vehicle && !support::Allowed(SupportMissionPolicy(),spec && spec->wasteland ? support::Capability::civilianGround : support::Capability::militaryGround)) {
        Status(L"本关限制外部军事支援；可选择步兵或民用轻卡");return PlanResult::refused;
    }
    if(vehicle && (!spec || !Cfg().npcBoarding || !SupportVehicleReady(kind,mode))){Status(L"该车辆的资源或交付能力不可用");return PlanResult::refused;}
    auto profile=InfantryRouteProfile();
    if(spec)profile=VehicleRouteProfile(*spec);
    float waypoint[3];
    const auto path=GroundNavigate(planning.navigation,entry,target,2.0f,GameMs(),waypoint,profile);
    if(path==npc::navigation::Result::pending){Status(L"正在核实支援入口及可达路线");return PlanResult::pending;}
    if(path==npc::navigation::Result::blocked)return nextEdge();
    const float distance=support::FlatDistance(entry,target);
    const float heading[3]={(target[0]-entry[0])/distance,0,(target[2]-entry[2])/distance};
    if(vehicle) {
        entry[1]+=0.1f;
        if(!AddUnit(plan,kVehicle+static_cast<unsigned>(kind),0,entry,heading) ||
           !AddCrew(plan,1,entry,heading,mode==SupportCrewMode::unmanned ? 1u : spec->seats,spec->halfWidth))return nextEdge();
    } else {
        const unsigned count=catalog==static_cast<unsigned>(AirCount()) ? 4u : 12u;
        for(unsigned i=0;i<count;++i) {
            float at[3]={entry[0]+static_cast<float>(i%4)*2.0f,entry[1],entry[2]+static_cast<float>(i/4)*2.0f};
            if(!Foot(at[0],at[2],entry[1],at[1]) || std::fabs(at[1]-entry[1])>0.55f)return nextEdge();
            if(!AddUnit(plan,i%4 ? kSoldier : kLeader,0,at,heading))return nextEdge();
        }
    }
    *out=plan;planning.active=false;return PlanResult::ready;
}

bool Validate(const SupportPlan& plan) noexcept {
    if(plan.catalogId==support_net::kMissionCrewCatalog)return ValidateMissionCrewPlan(plan);
    if(!support_net::ValidPlan(plan,false) || plan.catalogId>=static_cast<unsigned>(SupportCallCount()) || !SupportSoldiersReady())return false;
    SupportAircraft aircraft;SupportVehicleKind kind{};SupportCrewMode mode{};
    const bool air=plan.catalogId<static_cast<unsigned>(AirCount()),ground=GroundCatalog(plan.catalogId,kind,mode);
    const auto capability=air ? support::Capability::air : ground ?
        (SupportVehicleInfo(kind)->wasteland ? support::Capability::civilianGround : support::Capability::militaryGround) : support::Capability::infantry;
    if(!support::Allowed(SupportMissionPolicy(),capability))return false;
    if(air && !SupportAircraftSpec(static_cast<int>(plan.catalogId),&aircraft))return false;
    if(ground && !SupportVehicleReady(kind,mode))return false;
    if(air) {
        const unsigned group=1+AirCrew(aircraft);
        if(plan.count!=static_cast<unsigned>(aircraft.count)*group)return false;
        for(unsigned i=0;i<plan.count;++i) {
            const auto& unit=plan.units[i];
            if(i%group==0) {if(unit.resourceId!=kAircraft+plan.catalogId || unit.role)return false;}
            else if(unit.resourceId!=kSoldier || unit.role!=i-i%group+1)return false;
        }
    } else if(ground) {
        const unsigned crew=mode==SupportCrewMode::unmanned ? 1u : SupportVehicleInfo(kind)->seats;
        if(plan.count!=crew+1 || plan.units[0].role || plan.units[0].resourceId!=kVehicle+static_cast<unsigned>(kind))return false;
        for(unsigned i=1;i<plan.count;++i)if(plan.units[i].resourceId!=kSoldier || plan.units[i].role!=1)return false;
    } else {
        const unsigned expected=plan.catalogId==static_cast<unsigned>(AirCount()) ? 4u : 12u;
        if(plan.count!=expected)return false;
        for(unsigned i=0;i<expected;++i)
            if(plan.units[i].role || plan.units[i].resourceId!=(i%4 ? kSoldier : kLeader))return false;
    }
    for(unsigned i=0;i<plan.count;++i) {
        const auto& unit=plan.units[i];
        if(unit.role && (unit.role>i || plan.units[unit.role-1].resourceId<kAircraft))return false;
        if(unit.resourceId==kSoldier || unit.resourceId==kLeader)continue;
        if(unit.resourceId==kAircraft+plan.catalogId && plan.catalogId<static_cast<unsigned>(AirCount()))continue;
        if(unit.resourceId>=kVehicle && unit.resourceId<kVehicle+kSupportVehicleCount &&
           GroundCatalog(plan.catalogId,kind,mode) && unit.resourceId==kVehicle+static_cast<unsigned>(kind))continue;
        return false;
    }
    return true;
}
void Destroy(std::uint64_t id) noexcept {
    for(auto& deployed:deployments)if(deployed.used && deployed.id==id) {
        bool preserve[support_net::kMaxUnits]{};
        bool occupied=false;
        // A player can board a newly visible hull before support becomes Active. Cancellation must
        // not invalidate that player's live ride, including a player replicated from another peer.
        for(unsigned i=0;i<deployed.plan.count;++i) {
            if(!Live(deployed.objects[i]))continue;
            auto* object=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj));
            if(deployed.plan.units[i].resourceId<kAircraft)preserve[i]=edf::IsAnyPlayer(object);
            else for(unsigned seat=0;seat<SeatCount(object);++seat)
                preserve[i]=preserve[i] || AnyPlayerIn(SeatAt(object,seat));
            occupied=occupied || preserve[i];
        }
        // Keep the real crew attached to a retained hull, releasing deployment holds. Do not kick
        // or delete its passengers as a side effect of rolling back unrelated, still-empty units.
        for(unsigned i=0;i<deployed.plan.count;++i) {
            const auto parent=deployed.plan.units[i].role;
            if(parent && parent<=deployed.plan.count && preserve[parent-1])preserve[i]=true;
            if(preserve[i] && deployed.plan.units[i].resourceId<kAircraft)HoldSupportSoldier(deployed.objects[i],false);
        }
        // Remove crew first so native seat ownership is released before deleting hulls.
        for(unsigned i=0;i<deployed.plan.count;++i)
            if(!preserve[i] && deployed.plan.units[i].resourceId<kAircraft)DeleteSupportSoldier(deployed.objects[i]);
        for(unsigned i=0;i<deployed.plan.count;++i) {
            if(preserve[i])continue;
            if(deployed.plan.units[i].resourceId>=kVehicle && Live(deployed.objects[i]))DeleteSupportVehicle(static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj)));
            else if(deployed.plan.units[i].resourceId>=kAircraft)DeleteSupportAircraft(deployed.objects[i]);
        }
        deployed={};Status(occupied ? L"部署已取消，保留玩家已经登乘的车辆及其机组" : L"支援部署取消，已撤销未完成的部署");return;
    }
    DestroyMissionCrewPlan(id);
}
bool Spawn(std::uint64_t id,const SupportPlan& plan,bool remote) noexcept {
    if(plan.catalogId==support_net::kMissionCrewCatalog)return ApplyMissionCrewPlan(id,plan,remote);
    if(!Validate(plan))return false;
    Deployment* deployed=nullptr;for(auto& row:deployments)if(!row.used){deployed=&row;break;}
    if(!deployed)return false;
    deployed->used=true;deployed->id=id;deployed->plan=plan;deployed->remote=remote;deployed->born=GameMs();deployed->networked=InSession();
    for(unsigned i=0;i<plan.count;++i) {
        const auto& unit=plan.units[i];ObjRef object;
        if(unit.resourceId<kAircraft) {
            if(!ApplySupportSoldierSpawn(unit.matrix,unit.resourceId==kLeader,InSession() ? unit.netId : nullptr,&object)){Destroy(id);return false;}
            deployed->objects[i]=object;
            if(!HoldSupportSoldier(object,true)){Destroy(id);return false;}
        } else {
            unsigned char* vehicle=nullptr;
            if(unit.resourceId<kVehicle) {
                SupportAircraft spec;
                if(SupportAircraftSpec(static_cast<int>(plan.catalogId),&spec))vehicle=PrepareSupportAircraft(spec,unit.matrix);
            } else {
                SupportVehicleKind kind{};SupportCrewMode mode{};
                if(GroundCatalog(plan.catalogId,kind,mode))vehicle=SpawnSupportVehicle(kind,mode,unit.matrix+12,unit.matrix+8,nullptr);
            }
            if(!vehicle){Destroy(id);return false;}
            object=ObjRef::Of(vehicle);deployed->objects[i]=object;
            if(InSession() && !RegisterSupportObject(vehicle,unit.netId)){Destroy(id);return false;}
        }
        deployed->objects[i]=object;
    }
    Status(L"支援已在入口集结，等待所有玩家确认对象");return true;
}
bool Assign(Deployment& deployed) noexcept {
    const auto& plan=deployed.plan;ObjRef leader;
    // Native ride/follow events use a different channel. Never emit one until every peer has ACKed
    // construction, or it can arrive before that peer knows the soldier/vehicle's native identity.
    for(unsigned i=0;i<plan.count;++i)if(plan.units[i].resourceId<kAircraft)
        if(!HoldSupportSoldier(deployed.objects[i],false))return false;
    for(unsigned i=0;i<plan.count;++i) {
        const auto& unit=plan.units[i];
        if(unit.resourceId==kLeader){leader=deployed.objects[i];continue;}
        if(unit.resourceId==kSoldier && !unit.role && leader && !FollowSupportSoldier(deployed.objects[i],leader))return false;
        if(unit.resourceId<kAircraft || deployed.remote)continue;
        unsigned char* crew[support_net::kMaxUnits];int count=0;
        for(unsigned k=0;k<plan.count;++k)if(plan.units[k].role==i+1)crew[count++]=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[k].obj));
        if(NpcBoardCrew(static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj)),crew,count)!=count)return false;
    }
    deployed.assigned=true;Status(L"全员已确认，真实机组正在登车");return true;
}
void Configure() noexcept {
    if(configured)return;
    InstallMissionParticipantGate(&SupportMissionPlayerAllowed,&NoteSupportMissionPlayerCreated);
    ConfigureSupportNet({Plan,Validate,Spawn,Destroy,
        [](std::uint32_t ordinal,unsigned char* out) noexcept {
            return OnlineHostOnly() && DeriveSupportSoldierNetId(PlayerHuman(),ordinal,out);
        },&ReadMissionParticipants,&MissionParticipantGateReady,&MissionParticipantCreationsMatch,&RequestNotice});
    InstallMissionCrewSupport();configured=true;
}
}
int SupportCallCount() noexcept {return AirCount()+2+kSupportVehicleCount*2;}
const wchar_t* SupportCallName(int index) noexcept {
    if(index<AirCount())return SupportAirCallName(index);
    if(index==AirCount())return L"步兵小队（4人）";
    if(index==AirCount()+1)return L"步兵大队（12人）";
    // Returned static strings can safely be copied into draw-thread snapshots.
    static const wchar_t* labels[]={L"坦克·有人",L"坦克·空车交付",L"装甲运兵车·有人",L"装甲运兵车·空车交付",L"民用轻卡·有人",L"民用轻卡·空车交付"};
    const int ground=index-GroundStart();return ground>=0 && ground<6 ? labels[ground] : L"支援";
}
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept {
    if(!note || !capacity)return false;
    // Clear even when Submit refuses before allocating a request id. That attempt must not retain
    // the previous request's successful/failed notice. Offline calls use their detailed local status.
    localRequest={};note[0]=0;
    Configure();bool accepted=false;
    if(!Cfg().enabled || !target || index<0 || index>=SupportCallCount())Status(L"支援请求不可用");
    else if(InSession()) {
        localRequest.acceptNotices=true;
        accepted=SubmitSupportRequest(index,target,note,capacity);
        if(!accepted || !localRequest.hasNotice) {
            localRequest.shown=true;localRequest.acceptNotices=accepted;localRequest.hasNotice=false;
            _snwprintf_s(localRequest.text,_countof(localRequest.text),_TRUNCATE,L"%ls",
                note[0] ? note : accepted ? L"本机支援请求已排队，等待房主确认" : L"本机支援请求未受理");
        }
        _snwprintf_s(note,capacity,_TRUNCATE,L"%ls",localRequest.text);
        return accepted;
    }
    else if(offlinePending)Status(L"上一项支援仍在核实入场路线");
    else if(callAt && GameMs()-callAt<kCallCooldown)Status(L"支援调度冷却中（30 秒）");
    else {
        planning={};planning.active=true;planning.catalog=static_cast<unsigned>(index);std::memcpy(planning.target,target,12);
        offlinePending=true;accepted=true;Status(L"正在安排支援入口及路线");
    }
    _snwprintf_s(note,capacity,_TRUNCATE,L"%ls",status);return accepted;
}
SupportIcon SupportCallIcon(int index) noexcept {
    SupportAircraft spec;
    if(index<AirCount()) {
        if(!SupportAircraftSpec(index,&spec))return SupportIcon::sub;
        if(spec.heli>=0)return SupportIcon::heli;
        const auto role=static_cast<JetRole>(spec.jet);
        if(role==JetRole::gunship)return SupportIcon::gunship;
        return role==JetRole::carrier || role==JetRole::blastCarrier || role==JetRole::dollCarrier ? SupportIcon::carrier : SupportIcon::jet;
    }
    if(InfantryCatalog(static_cast<std::uint32_t>(index)))return index==AirCount() ? SupportIcon::squad : SupportIcon::platoon;
    SupportVehicleKind kind{};SupportCrewMode mode{};
    if(!GroundCatalog(static_cast<std::uint32_t>(index),kind,mode))return SupportIcon::squad;
    return kind==SupportVehicleKind::tank ? SupportIcon::tank : kind==SupportVehicleKind::transport ? SupportIcon::apc : SupportIcon::truck;
}
SupportReadiness SupportCallReadiness() noexcept {
    // The same tests SupportCallAt makes before it plans, read without asking: online the host decides.
    if(!Cfg().enabled)return {SupportReady::off,0};
    if(InSession())return {SupportReady::ready,0};
    if(offlinePending)return {SupportReady::planning,0};
    const ULONGLONG now=GameMs();
    if(callAt && now-callAt<kCallCooldown)return {SupportReady::cooldown,static_cast<int>((kCallCooldown-(now-callAt)+999)/1000)};
    return {SupportReady::ready,0};
}
void SupportCallStatus(wchar_t* out,std::size_t capacity) noexcept {
    if(out && capacity)_snwprintf_s(out,capacity,_TRUNCATE,L"%ls",localRequest.shown ? localRequest.text : status);
}
void SupportDispatchTick() noexcept {
    if(dispatchFrame==GameFrame())return;
    dispatchFrame=GameFrame();
    Configure();SupportNetTick();
    if(InSession() && offlinePending){offlinePending=false;planning.active=false;Status(L"已取消离线请求，请通过房主重新调度");}
    if(offlinePending) {
        SupportPlan plan;
        const auto result=Plan(planning.catalog,planning.target,&plan);
        if(result!=support_net::PlanResult::pending) {
            offlinePending=false;
            if(result==support_net::PlanResult::ready) {
                if(Spawn(nextOffline++,plan,false))callAt=GameMs();else Status(L"支援资源、真实机组或席位分配失败");
            }
        }
    }
    for(auto& deployed:deployments) {
        if(!deployed.used)continue;
        bool anyLive=false;
        for(unsigned i=0;i<deployed.plan.count;++i)anyLive=anyLive || Live(deployed.objects[i]);
        if(!anyLive){deployed={};continue;}
        if(!deployed.assigned) {
            if(deployed.networked && !SupportTransactionActive(deployed.id))continue;
            if(!Assign(deployed)) {
                const auto id=deployed.id;if(deployed.networked)ReportSupportFailure(id);else Destroy(id);continue;
            }
        }
        if(deployed.remote || deployed.delivered)continue;
        bool alive=true,boarded=true;
        for(unsigned i=0;i<deployed.plan.count;++i) {
            alive=alive && Live(deployed.objects[i]);
            const auto parent=deployed.plan.units[i].role;if(!parent || !alive)continue;
            auto* vehicle=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[parent-1].obj));
            bool seated=false;
            for(unsigned seat=0;seat<SeatCount(vehicle);++seat)
                seated=seated || At<const void*>(SeatAt(vehicle,seat),kSeatRider)==deployed.objects[i].obj;
            boarded=boarded && seated;
        }
        if(!alive && deployed.started) {
            // Once in the field, losses are gameplay. Never despawn the surviving deployed units.
            deployed.delivered=true;Status(L"支援途中遭受损失，存活单位继续执行任务");continue;
        }
        if(!alive || (!deployed.started && GameMs()-deployed.born>kBoardLimit)) {
            const auto id=deployed.id;if(deployed.networked)ReportSupportFailure(id);else Destroy(id);continue;
        }
        if(!deployed.started && boarded) {
            bool started=true;
            for(unsigned i=0;i<deployed.plan.count;++i) {
                const auto resource=deployed.plan.units[i].resourceId;
                auto* object=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj));
                if(resource>=kAircraft && resource<kVehicle) {
                    SupportAircraft spec;SupportAircraftSpec(static_cast<int>(deployed.plan.catalogId),&spec);
                    started=ActivateSupportAircraft(object,spec,deployed.plan.target) && started;
                } else if(resource>=kVehicle)started=NpcPrepareVehicleRoutePost(object,
                    reinterpret_cast<const float*>(object+kPosition),kVehicleDriverHold) && started;
                else if(resource==kLeader)started=NpcPrepareSquadRoute(object,
                    reinterpret_cast<const float*>(object+kPosition),kInfantryRouteStop) && started;
            }
            if(started){deployed.started=true;Status(L"支援已出发，正在沿路线入场");}
        }
        if(!deployed.started)continue;
        // Ground hull follows verified route waypoints, not a line through walls. Arrival releases real
        // drivers only for empty delivery; the same objects remain in the mission and can return on foot.
        SupportVehicleKind kind{};SupportCrewMode mode{};
        if(GroundCatalog(deployed.plan.catalogId,kind,mode)) {
            auto* vehicle=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[0].obj));
            const float* position=reinterpret_cast<const float*>(vehicle+kPosition);float waypoint[3];
            const auto profile=VehicleRouteProfile(*SupportVehicleInfo(kind));
            const ULONGLONG now=GameMs();
            float speed=1e9f;
            if(deployed.vehicleSampleAt && now>=deployed.vehicleSampleAt) {
                const float dt=now>deployed.vehicleSampleAt ? std::fmin(static_cast<float>(now-deployed.vehicleSampleAt)*0.001f,1.0f/60.0f) : 1.0f/60.0f;
                const float dx=position[0]-deployed.vehicleSample[0],dy=position[1]-deployed.vehicleSample[1],dz=position[2]-deployed.vehicleSample[2];
                speed=std::sqrt(dx*dx+dy*dy+dz*dz)/dt;
            }
            std::memcpy(deployed.vehicleSample,position,12);deployed.vehicleSampleAt=now;
            const auto path=GroundNavigate(deployed.navigation,position,deployed.plan.target,kVehicleArrival,now,waypoint,profile);
            if(path==npc::navigation::Result::moving)NpcPrepareVehicleRoutePost(vehicle,waypoint,kVehicleDriverHold);
            else if(path==npc::navigation::Result::arrived) {
                if(!NpcPrepareVehicleRoutePost(vehicle,position,kVehicleDriverHold) || !(speed<=0.5f))continue;
                if(mode==SupportCrewMode::unmanned && !NpcReleaseVehicleCrew(vehicle))continue;
                if(mode==SupportCrewMode::unmanned) {
                    const auto playerRef=ObjRef::Of(PlayerHuman());
                    for(unsigned i=0;i<deployed.plan.count;++i)if(deployed.plan.units[i].role==1)
                        FollowSupportSoldier(deployed.objects[i],playerRef);
                }
                deployed.delivered=true;Status(mode==SupportCrewMode::unmanned ? L"空车已到达交付点，司机已下车" : L"支援车辆已抵达目的地");
            } else NpcPrepareVehicleRoutePost(vehicle,position,kVehicleDriverHold); // wait for a verified route; never drive through the obstacle
        } else if(InfantryCatalog(deployed.plan.catalogId)) {
            int leader=0;bool complete=true,released=false;
            for(unsigned i=0;i<deployed.plan.count;++i)if(deployed.plan.units[i].resourceId==kLeader && leader<3) {
                auto& route=deployed.infantry[leader++];
                if(!route.done) {
                    auto* object=static_cast<unsigned char*>(const_cast<void*>(deployed.objects[i].obj));
                    const auto* position=reinterpret_cast<const float*>(object+kPosition);float waypoint[3];
                    const auto path=GroundNavigate(route.navigation,position,deployed.plan.target,kInfantryArrival,GameMs(),waypoint,InfantryRouteProfile());
                    if(path==npc::navigation::Result::arrived) {
                        route.done=NpcFinishSquadRoute(object,deployed.plan.target);
                    } else {
                        // Pending/blocked requests hold only quiet movement; combat and native follower
                        // links stay owned by npcai. Never issue a far ordinary guard or reset AI per frame.
                        const bool accepted=NpcPrepareSquadRoute(object,path==npc::navigation::Result::moving ? waypoint : position,kInfantryRouteStop);
                        if(accepted)route.given=true;
                        else if(route.given){route.done=route.released=true;}
                    }
                }
                complete=complete && route.done;released=released || route.released;
            }
            if(complete && leader>0) {
                deployed.delivered=true;
                Status(hudtext::Tr(released ? hudtext::Tx::supportInfantryTransferred : hudtext::Tx::supportInfantryArrived));
            }
        } else {
            bool arrived=true;
            for(unsigned i=0;i<deployed.plan.count;++i)if(!deployed.plan.units[i].role) {
                const auto* pos=reinterpret_cast<const float*>(static_cast<const unsigned char*>(deployed.objects[i].obj)+kPosition);
                arrived=arrived && support::FlatDistance(pos,deployed.plan.target)<150;
            }
            if(arrived) {
                for(unsigned i=0;i<deployed.plan.count;++i)if(deployed.plan.units[i].resourceId>=kAircraft) {
                    SupportAircraft spec;SupportAircraftSpec(static_cast<int>(deployed.plan.catalogId),&spec);
                    if(spec.heli>=0)HeliCommand(deployed.objects[i].obj,Command{});
                    else JetCommand(deployed.objects[i].obj,Command{});
                }
                deployed.delivered=true;Status(L"支援已抵达并进入任务区域");
            }
        }
    }
}
void ResetSupportDispatch() noexcept {
    // Mission reset invalidates the old objects; do not delete through last mission's borrowed pointers.
    planning={};for(auto& row:deployments)row={};offlinePending=false;callAt=0;nextOffline=1;status[0]=0;
    localRequest={};
    dispatchFrame=~ULONGLONG{0};
    ResetMissionCrewSupport();
    ResetSupportNet();
    // MissionStart runs during preload, before native players are created. Installing lazily on the
    // first player frame would miss the creation callback and discard its admission observation.
    Configure();
}
} // namespace crew
