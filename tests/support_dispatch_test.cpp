// Execute the production dispatch/boarding lifecycle using inert stand-ins for EDF objects.
#include "../src/support_dispatch.cpp"
#include "../src/support_config.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;Config config{};PlayerFix player{{0,0,0},2,1};
const Config& Cfg() noexcept {return config;}
bool testOnline=false;ULONGLONG fixtureMs=1000;
support::Policy SupportMissionPolicy() noexcept {return {support::Environment::normal,false};}
bool InSession() noexcept {return testOnline;}
bool OnlineHostOnly() noexcept {return true;}
ULONGLONG GameMs() noexcept {return fixtureMs;}
std::uint64_t GameFrame() noexcept {return fixtureMs;}
void Log(const char*,...) noexcept {}
unsigned char objects[32][0x3000]{},controls[32][32]{},seats[32][edf::kSeatStride*14]{};
int made=0,deleted=0,boardRequests=0,activated=0,orders=0,followed=0,netRequests=0,releases=0,routeOrders=0;
int held=0;bool transactionActive=false;
bool SupportTransactionActive(std::uint64_t) noexcept {return transactionActive;}
bool peersNew=true;
bool SupportPeersAcceptVariants() noexcept {return peersNew;}
bool HoldSupportSoldier(const ObjRef&,bool hold) noexcept {held+=hold ? 1 : -1;return true;}
bool allReady=true,terrain=true,nativeFail=false;
#ifndef SUPPORT_ROUTE_NATIVE_TEST
Sea terrainSea=Sea::land;float waterSurface=0;
Sea SeaAt(float,float,float* out) noexcept {*out=waterSurface;return terrainSea;}
#endif
npc::navigation::Result routeResult=npc::navigation::Result::pending;
ObjRef Make(bool vehicle=false) noexcept {
    const int slot=made++;auto* o=objects[slot];std::memset(o,0,sizeof(objects[slot]));
    Put<void*>(o,kSelfCtrl,controls[slot]);Put<long>(controls[slot],8,1);Put<long>(controls[slot],12,1);
    if(vehicle){Put<void*>(o,kSeats,seats[slot]);Put<std::uint64_t>(o,kSeatCount,3);std::memset(seats[slot],0,sizeof(seats[slot]));}
    return ObjRef::Of(o);
}
unsigned char* PlayerHuman() noexcept {return objects[31];}
int SupportAirCallCount() noexcept {return 21;}
const wchar_t* SupportAirCallName(int) noexcept {return L"Heli";}
const wchar_t* airKeys[21]={L"INTERCEPTOR",L"INTERCEPTOR_F",L"STRIKE",L"STRIKE_F",L"MULTIROLE",L"MULTIROLE_F",L"FIGHTER",L"FIGHTER_F",
    L"CARRIER",L"CARRIER_F",L"HELI",L"HELI_F",L"BLAST_CARRIER",L"BLAST_CARRIER_F",L"DOLL_CARRIER",L"DOLL_CARRIER_F",L"SUB",
    L"GUNSHIP",L"GUNSHIP_F",L"MEDIC_HELI",L"MEDIC_HELI_F"};
const wchar_t* SupportAirCallKey(int index) noexcept {return index>=0 && index<21 ? airKeys[index] : nullptr;}
bool soloHost=false;
bool SupportSoloHostWorld() noexcept {return soloHost;}
bool SupportAircraftSpec(int id,SupportAircraft* out) noexcept {
    if(id<0 || id>=21 || id==16)return false;
    *out={-1,static_cast<int>(HeliBody::eros506),1,300,false};return true;
}
int plannedAircraft=0;
support::Refusal PlanAirSupport(int,const float*,const float*,support::Route* route,int count) noexcept {
    plannedAircraft=count;
    *route={{-1400,150,0},{1,0,0}};return terrain ? support::Refusal::none : support::Refusal::noEntry;
}
SupportAircraft lastPrepared{};
unsigned char* PrepareSupportAircraft(const SupportAircraft& spec,const float* matrix) noexcept {
    lastPrepared=spec;
    auto ref=Make(true);std::memcpy(static_cast<unsigned char*>(const_cast<void*>(ref.obj))+kPosition,matrix+12,12);
    auto* hull=static_cast<unsigned char*>(const_cast<void*>(ref.obj));
    // The transports' hulls seat their pilot and twelve (tools/make_jets.py TRANSPORT_*).
    if(spec.transportPlane || spec.heli==static_cast<int>(HeliBody::transport410))Put<std::uint64_t>(hull,kSeatCount,13);
    return hull;
}
// The transports' entry points (transport.cpp, jet.cpp, heli.cpp, airstrike.cpp, jet_spawn.cpp, support_net.cpp).
int delivered=0,deliveredSquads=0,paradrops=0,ferries=0,jetWithdrawals=0,heliLeaves=0,plannedTransports=0;
const void* deliveredTops[3]{};const void* deliveredHull=nullptr;
bool transportReady=true,peersTransports=true,heliHull=false,driverAboard=true;
bool TransportDeliver(const void* v,const void* const* tops,int n,const float*) noexcept {
    ++delivered;deliveredSquads=n;deliveredHull=v;for(int i=0;i<n && i<3;++i)deliveredTops[i]=tops[i];return n>0;
}
bool TransportParadrop(const void*,const float*) noexcept {++paradrops;return true;}
bool JetFerry(const void*,const float*) noexcept {++ferries;return true;}
bool JetWithdrawNow(const void*,const char*) noexcept {++jetWithdrawals;return true;}
bool HeliStartLeaving(const void*) noexcept {++heliLeaves;return true;}
bool IsHelicopter(const void*) noexcept {return heliHull;}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
bool NpcDriver(const unsigned char*) noexcept {return driverAboard;}
#endif
support::Refusal PlanAirSupportFor(SupportAircraft,const float*,const float*,support::Route* route,int) noexcept {
    ++plannedTransports;*route={{-1400,150,0},{1,0,0}};return terrain ? support::Refusal::none : support::Refusal::noEntry;
}
bool SupportAircraftReady(const SupportAircraft&) noexcept {return transportReady;}
bool SupportPeersAcceptTransports() noexcept {return peersTransports;}
bool peersLoadout=true;
bool SupportPeersAcceptLoadout() noexcept {return peersLoadout;}
bool lastAirborne=false;
bool ActivateSupportAircraft(unsigned char*,const SupportAircraft&,const float*,bool airborne) noexcept {++activated;lastAirborne=airborne;return true;}
bool peersAirborne=true;
bool SupportPeersAcceptAirborne() noexcept {return peersAirborne;}
bool DeleteSupportAircraft(const ObjRef& ref) noexcept {if(ref)++deleted;return true;}
bool aircraftLeft=false;
bool SupportAircraftLeft(const ObjRef&) noexcept {return aircraftLeft;}
bool SupportSoldiersReady() noexcept {return allReady;}
const wchar_t* soldierFailure=L"支援兵员创建发生异常，本局已停用";
const wchar_t* SupportSoldierFailureText() noexcept {return soldierFailure;}
std::uint32_t lastResources[16]{};int resourceCount=0;bool lastLocal=false;const unsigned char* lastId=nullptr;
bool ApplySupportSoldierResource(const float* matrix,std::uint32_t resource,const unsigned char* id,bool local,ObjRef* out) noexcept {
    if(nativeFail)return false;
    if(resourceCount<16)lastResources[resourceCount++]=resource;lastLocal=local;lastId=id;
    *out=Make();std::memcpy(static_cast<unsigned char*>(const_cast<void*>(out->obj))+kPosition,matrix+12,12);return true;
}
bool CreateSupportSoldierUnregistered(const float* matrix,std::uint32_t resource,const unsigned char* id,bool local,ObjRef* out) noexcept {
    return ApplySupportSoldierResource(matrix,resource,id,local,out);
}
int seatNow=0,registeredBeforeSeat=0;bool seatFail=false;
int NpcSeatCrewNow(unsigned char* v,unsigned char* const* crew,int count) noexcept {
    ++seatNow;if(seatFail)return 0;
    int seated=0;
    for(int i=0;i<count && i<static_cast<int>(SeatCount(v));++i){Put<const void*>(SeatAt(v,static_cast<unsigned>(i)),kSeatRider,crew[i]);++seated;}
    return seated;
}
bool DeriveSupportSoldierNetId(const void*,unsigned,unsigned char*) noexcept {return true;}
bool FollowSupportSoldier(const ObjRef&,const ObjRef&) noexcept {++followed;return true;}
bool DeleteSupportSoldier(const ObjRef& ref) noexcept {if(ref)++deleted;return true;}
int registered=0;
bool RegisterSupportObject(const void*,const unsigned char*) noexcept {++registered;if(!seatNow)++registeredBeforeSeat;return true;}
bool SupportVehicleReady(SupportVehicleKind,SupportCrewMode) noexcept {return true;}
TankRound lastRound=TankRound::he;bool apReady=false,lookReady=false;
unsigned char* SpawnSupportVehicle(SupportVehicleKind,SupportCrewMode,const float*,const float*,const void*,TankRound round) noexcept {
    lastRound=round;return nullptr;
}
bool SupportTankRoundReady(TankRound round) noexcept {return round==TankRound::he || apReady;}
// support_soldier.cpp's look table: every look "preloaded" as entry 1 when lookReady.
std::uint32_t SupportSoldierWithLook(std::uint32_t resource,const SupportLook& look) noexcept {
    return lookReady && look.On() ? resource|(1u<<kSupportLookShift) : resource;
}
int looksQueued=0;
bool PreloadSupportLook(SupportWeapon,bool,const SupportLook&) noexcept {++looksQueued;return true;}
bool lookFiles=true;
namespace jet {bool ModFileThere(const wchar_t*) noexcept {return lookFiles;}}
bool DeleteSupportVehicle(unsigned char*) noexcept {++deleted;return true;}
support_net::Hooks configuredHooks{};int netConfigured=0,gatesInstalled=0;
bool creationSeen=false,gateBeforeNetwork=false;
MissionPlayerCreated installedCreatedObserver=nullptr;
bool InstallMissionParticipantGate(MissionParticipantAdmission,MissionPlayerCreated created) noexcept {
    ++gatesInstalled;installedCreatedObserver=created;return true;
}
bool MissionParticipantGateReady() noexcept {return true;}
bool SupportMissionPlayerAllowed(int) noexcept {return true;}
void NoteSupportMissionPlayerCreated(int,const ObjRef&) noexcept {creationSeen=true;}
bool MissionParticipantCreationsMatch(const ObjRef*,unsigned) noexcept {return true;}
bool ReadMissionParticipants(void**,unsigned,unsigned*,unsigned*) noexcept {return true;}
void ConfigureSupportNet(const support_net::Hooks& hooks) noexcept {
    configuredHooks=hooks;++netConfigured;gateBeforeNetwork=gatesInstalled>0;creationSeen=false;
}
bool submitReady=true;unsigned noticeRequest=0;
std::uint64_t lastNetLoadout=0;
bool SubmitSupportRequest(int,const float*,wchar_t* note,std::size_t size,std::uint64_t loadout) noexcept {
    ++netRequests;lastNetLoadout=loadout;
    if(!submitReady){_snwprintf_s(note,size,_TRUNCATE,L"联机扩展尚未就绪");return false;}
    if(configuredHooks.notice)configuredHooks.notice(++noticeRequest,support_net::RequestStatus::accepted);
    _snwprintf_s(note,size,_TRUNCATE,L"request queued");return true;
}
void SupportNetTick() noexcept {}
void ResetSupportNet() noexcept {creationSeen=false;}
bool ValidateMissionCrewPlan(const SupportPlan&) noexcept {return false;}
bool ApplyMissionCrewPlan(std::uint64_t,const SupportPlan&,bool) noexcept {return false;}
void DestroyMissionCrewPlan(std::uint64_t) noexcept {}
void InstallMissionCrewSupport() noexcept {}
void ResetMissionCrewSupport() noexcept {}
void ReportSupportFailure(std::uint64_t id) noexcept {Destroy(id);}
namespace support_net {bool ValidPlan(const Plan& p,bool) noexcept {return p.count>0 && p.count<=kMaxUnits;}}
int NpcBoardCrew(unsigned char*,unsigned char* const*,int count) noexcept {++boardRequests;return count;}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
bool NpcPrepareVehicleRoutePost(unsigned char*,const float*,float) noexcept {return true;}
#endif
bool NpcReleaseVehicleCrew(unsigned char*) noexcept {++releases;return true;}
bool SquadCommand(const void*,const Command&) noexcept {++orders;return true;}
bool NpcPrepareSquadRoute(unsigned char*,const float*,float) noexcept {++routeOrders;return true;}
bool NpcFinishSquadRoute(unsigned char*,const float*) noexcept {return true;}
bool HeliCommand(const void*,const Command&,const ObjRef&) noexcept {return true;}
bool JetCommand(const void*,const Command&,const ObjRef&) noexcept {return true;}
bool areaMeasured=true;
bool PlayAreaMeasured() noexcept {return areaMeasured;}
PlayArea MapPlayArea() noexcept {return {{-1500,-1500},{1500,1500},true,0,true};}
bool MapGroundNear(float,float,float,float* y,bool) noexcept {*y=0;return terrain;}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
float MapRay(const float*,const float*,float*) noexcept {return -1;}
#endif
#ifndef SUPPORT_ROUTE_NATIVE_TEST
npc::navigation::Result GroundNavigate(npc::navigation::RouteState&,const float* from,const float*,float,std::uint64_t,float* waypoint,npc::navigation::Profile) noexcept {
    std::memcpy(waypoint,from,12);return routeResult;
}
#endif
}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
int main() {
    using namespace crew;
    int checks=0;const auto check=[&](bool condition,const char* why){++checks;if(!condition){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}};
    float target[3]={0,0,0};wchar_t note[128];
    ResetSupportDispatch();
    check(configured && netConfigured==1 && gateBeforeNetwork,"mission preload installs admission gate before configuring support networking");
    check(configuredHooks.participants==&ReadMissionParticipants && configuredHooks.admissionReady==&MissionParticipantGateReady,
          "network quorum reads actual mission actors and the verified upper admission gate");
    check(installedCreatedObserver==&NoteSupportMissionPlayerCreated && configuredHooks.createdMatches==&MissionParticipantCreationsMatch,
          "successful native actor creation observer and exact identity matcher are both wired before creation");
    installedCreatedObserver(0,ObjRef{});
    check(SupportCallAt(0,target,note,128) && made==0,"request only queues: no objects materialise on click");
    SupportDispatchTick();
    check(creationSeen && netConfigured==1,"first frame does not reset creation observations already collected during mission start");
    // 2026-10-09: air support flies in from the edge. Created in the air, its real crew made inside it and seated at
    // once, the flight on at spawn: no runway, no walk aboard, no takeoff.
    check(made==2 && boardRequests==0 && seatNow==1 && activated==1 && lastAirborne,
          "hull and real crew created, crew seated at once, airborne flight starts at spawn");
    check(At<float>(objects[0],kPosition)==-1400 && At<float>(objects[0],kPosition+4)==150,"hull starts in the air at the planned edge");
    check(At<float>(objects[1],kPosition+4)==150 && At<const void*>(seats[0],kSeatRider)==objects[1],
          "the real soldier is made inside its aircraft and sits in the driver's seat at spawn");
    check(IsSupportAirborneAircraft(deployments[0].plan.units[0].resourceId),"the plan marks the hull airborne");
    fixtureMs+=100;SupportDispatchTick();
    fixtureMs+=100;SupportDispatchTick();check(activated==1,"activation is once, not every frame");
    // 2026-10-09 16:59: out of fuel, two support helis flew off and hung at the map's edge for the rest of the mission
    // (the plugin's reaps never delete under real soldiers). Its owner retires it: crew first, the hull once settled.
    check(deleted==0,"a flying support aircraft is never retired");
    aircraftLeft=true;fixtureMs+=1;SupportDispatchTick();
    check(deleted==1 && deployments[0].used,"left: the dispatcher deletes the real crew it made, the hull not yet");
    fixtureMs+=1;SupportDispatchTick();
    check(deleted==1,"the hull waits for the game to take it as empty (a delete before that leaves it in team 5's set)");
    Put<std::int32_t>(objects[0],kTeam,kTeamVehicle);fixtureMs+=1;SupportDispatchTick();
    check(deleted==2,"once empty (team 5) the hull is deleted");
    objects[0][kDead]=1;objects[1][kDead]=1;fixtureMs+=1;SupportDispatchTick();
    check(deleted==2 && !deployments[0].used,"gone: nothing deleted twice, the deployment row is free");
    aircraftLeft=false;
    auto invalid=deployments[0].plan;invalid.units[1].role=0;
    check(!Validate(invalid),"a manifest cannot detach the specified crew from its aircraft");
    invalid=deployments[0].plan;invalid.count=1;
    check(!Validate(invalid),"a manned aircraft manifest cannot omit its real pilot");
    check(!SupportCallAt(0,target,note,128),"successful deployment consumes cooldown");
    ResetSupportDispatch();made=deleted=boardRequests=activated=0;terrain=false;fixtureMs+=40000;
    check(SupportCallAt(0,target,note,128),"invalid terrain request can be queued for explicit validation");
    SupportDispatchTick();check(!made && !offlinePending,"no safe entry refuses without spawning at target");
    // 2026-10-09 16:52:26: an air call made 50 ms before the play area was measured was refused ("no clear air corridor"),
    // the same call worked later. Until the area is in, a request waits; then it is planned as usual.
    ResetSupportDispatch();terrain=true;areaMeasured=false;made=0;
    check(SupportCallAt(0,target,note,128),"a call before the area is measured is queued");
    SupportDispatchTick();fixtureMs+=16;SupportDispatchTick();
    check(!made && offlinePending && SupportCallReadiness().state==SupportReady::planning,"it waits (dispatching), never refused for it");
    areaMeasured=true;fixtureMs+=16;SupportDispatchTick();
    check(made==2 && !offlinePending,"once the area is in, the same request is planned and flies in");
    ResetSupportDispatch();terrain=true;nativeFail=true;made=deleted=0;
    SupportCallAt(0,target,note,128);SupportDispatchTick();
    check(made==1 && deleted==1 && !deployments[0].used,"partial crew construction rolls the hull back");
    ResetSupportDispatch();nativeFail=false;made=deleted=0;
    seatFail=true;activated=0;
    SupportCallAt(0,target,note,128);SupportDispatchTick();seatFail=false;
    check(deleted==2 && !deployments[0].used && activated==0,"a crew that cannot take its seats removes crew and hull before any flight");
    ResetSupportDispatch();nativeFail=false;made=deleted=0;routeResult=npc::navigation::Result::pending;
    SupportCallAt(21,target,note,128);SupportDispatchTick();
    check(!made && offlinePending,"infantry waits for full ground route before creation");
    routeResult=npc::navigation::Result::moving;++fixtureMs;SupportDispatchTick();
    check(made==4 && followed==3 && routeOrders>0 && orders==0,"verified infantry entry creates real squad with three native followers and a short-route order");
    ResetSupportDispatch();made=0;routeResult=npc::navigation::Result::pending;
    SupportCallAt(21,target,note,128);testOnline=true;SupportDispatchTick();
    check(!offlinePending && !made,"entering an online session cancels an uncommitted offline request");
    ResetSupportDispatch();testOnline=true;made=0;
    check(SupportCallAt(0,target,note,128) && netRequests==1 && !made,"online click uses reliable request rather than local spawn");
    ResetSupportDispatch();made=boardRequests=0;held=0;seatNow=0;registered=0;registeredBeforeSeat=0;activated=0;SupportPlan networkPlan;
    check(Plan(0,target,0,&networkPlan)==support_net::PlanResult::ready && Spawn(10,networkPlan,false),"committed online plan creates registered stand-ins");
    check(seatNow==1 && registered==2 && registeredBeforeSeat==0,"airborne hull and crew are registered only after the crew is seated");
    check(activated==1,"the host flies its airborne copy at once: a hull in the air never waits unflown for the barrier");
    SupportDispatchTick();
    check(held==1 && boardRequests==0 && !deployments[0].assigned,"spawn before all-peer ACK holds native AI and emits no boarding request");
    transactionActive=true;++fixtureMs;SupportDispatchTick();
    check(held==0 && boardRequests==0 && deployments[0].assigned,"all-peer active barrier releases held crew; nobody walks aboard");
    {const int before=activated;const auto seated=seatNow;
     check(Spawn(11,networkPlan,true) && seatNow==seated+1 && activated==before,"a peer's copy seats its crew identically but never flies it");}
    // A room with an older peer (no kCapAirborneAir): refused with its reason, never half in the air.
    peersAirborne=false;made=0;
    check(Plan(0,target,0,&networkPlan)==support_net::PlanResult::refused && !made,"older peer: air support refused before anything exists");
    SupportCallStatus(note,128);
    check(std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportAirNeedsUpdate))!=nullptr,"older peer: the HUD says why");
    peersAirborne=true;
    // An older host's runway plan (kAircraft + catalog) is applied as that host planned it: hull, crew walking aboard.
    {SupportPlan legacy;check(Plan(0,target,0,&legacy)==support_net::PlanResult::ready,"plan for the legacy fixture");
     legacy.units[0].resourceId=kSupportAircraftResource+legacy.catalogId;
     check(Validate(legacy),"an older host's runway plan still validates");
     ResetSupportDispatch();transactionActive=false;boardRequests=0;activated=0;seatNow=0;
     check(SpawnDeployment(30,legacy,false,false) && seatNow==0 && activated==0,"legacy hull: no seating at spawn, no flight yet");
     SupportDispatchTick();check(boardRequests==1,"legacy hull: its crew walks aboard as before");}
    // Rollback must respect a real player's independent boarding action, on either machine.
    for(bool remotePlayer:{false,true}) {
        ResetSupportDispatch();made=deleted=held=0;transactionActive=false;
        check(Spawn(20,networkPlan,false),"prepare rollback occupancy fixture");
        unsigned char human[0x400]{},control[32]{};
        Put<unsigned char>(human,kHumanPlayer,1);Put<void*>(human,kHumanPad,human);
        if(remotePlayer)Put<unsigned char>(human,edf::kRiderNet+edf::kNetFlags,1);
        Put<long>(control,edf::kCtrlUses,1);Put<void*>(seats[0],kSeatRider,human);Put<void*>(seats[0],kSeatRiderCtrl,control);
        Destroy(20);
        check(!deleted && !held && !deployments[0].used,"cancel preserves occupied hull and real crew, releases their holds, and drops deployment ownership");
    }
    ResetSupportDispatch();testOnline=true;submitReady=true;
    SupportCallAt(0,target,note,128);const auto firstRequest=noticeRequest;
    check(configuredHooks.notice!=nullptr,"local request status callback is wired");
    Status(L"other peer planning");SupportCallStatus(note,128);
    check(std::wcsstr(note,L"等待房主")!=nullptr,"accepted means waiting and is not overwritten by another peer's planner");
    configuredHooks.notice(firstRequest,support_net::RequestStatus::refused);
    Status(L"other peer deployment active");SupportCallStatus(note,128);
    check(std::wcsstr(note,L"房主拒绝")!=nullptr,"local refusal is persistent despite unrelated host deployment progress");
    configuredHooks.notice(firstRequest,support_net::RequestStatus::active);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"房主拒绝")!=nullptr,"late active cannot reopen the same rejected request");
    SupportCallAt(0,target,note,128);const auto secondRequest=noticeRequest;
    configuredHooks.notice(firstRequest,support_net::RequestStatus::cancelled);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"等待房主")!=nullptr,"old cancellation cannot overwrite a newer local request");
    configuredHooks.notice(secondRequest,support_net::RequestStatus::active);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"全员对象确认")!=nullptr,"active reports object confirmation rather than claiming arrival");
    submitReady=false;check(!SupportCallAt(0,target,note,128),"pre-id transport refusal returns failure");
    SupportCallStatus(note,128);
    check(std::wcsstr(note,L"联机扩展尚未就绪")!=nullptr,"new pre-id refusal clears the previous active result");
    configuredHooks.notice(secondRequest,support_net::RequestStatus::cancelled);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"联机扩展尚未就绪")!=nullptr,"older allocated request cannot cover the newest unallocated attempt");
    submitReady=true;SupportCallAt(0,target,note,128);
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::timeout);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"请求超时")!=nullptr,"timeout is visible instead of waiting forever");
    SupportCallAt(0,target,note,128);
    configuredHooks.notice(noticeRequest,support_net::RequestStatus::interrupted);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"会话中断")!=nullptr,"interrupted session has a distinct terminal message");
    testOnline=false;SupportCallAt(0,target,note,128);SupportCallStatus(note,128);
    check(std::wcsstr(note,L"正在安排")!=nullptr && !localRequest.shown,"offline request restores its detailed planner channel");
    ResetSupportDispatch();SupportCallStatus(note,128);check(!note[0],"mission reset clears old request notices");
    allReady=false;
    const int madeBeforeFailure=made;
    check(SupportCallAt(0,target,note,128),"unavailable-soldier request still enters the real planner");
    SupportDispatchTick();SupportCallStatus(note,128);
    check(std::wcscmp(note,soldierFailure)==0 && made==madeBeforeFailure,
          "native creation failure survives the planner/UI boundary without becoming resource unavailable");
    soldierFailure=L"支援兵员资源尚未预载";
    ++fixtureMs;SupportCallAt(0,target,note,128);SupportDispatchTick();SupportCallStatus(note,128);
    check(std::wcscmp(note,soldierFailure)==0,"actual preload failure retains its separate diagnostic");
    config.customNpcAi=false;
    ++fixtureMs;SupportCallAt(0,target,note,128);SupportDispatchTick();SupportCallStatus(note,128);
    check(std::wcsstr(note,L"NPC 指挥功能未启用")!=nullptr && std::wcscmp(note,soldierFailure)!=0 && made==madeBeforeFailure,
          "disabled NPC configuration takes precedence over soldier resource or constructor failure");
    config.customNpcAi=true;allReady=true;
    float footY=0;
    terrainSea=Sea::water;waterSurface=1;
    check(!Foot(0,0,0,footY),"soldier collection point cannot use a submerged floor as dry ground");
    waterSurface=0.2f;check(Foot(0,0,0,footY),"shallow wading matches the navigation allowance");
    terrainSea=Sea::unknown;check(!Foot(0,0,0,footY),"unknown water state is not treated as a safe entry");
    terrainSea=Sea::land;
    // 2026-10-09: the host of a world with no other participant deploys locally (no EDF6Coop transport needed, no peer
    // to replicate to); any other online machine still requests through the host.
    ResetSupportDispatch();testOnline=true;soloHost=true;made=0;netRequests=0;registered=0;resourceCount=0;fixtureMs+=40000;
    check(SupportCallAt(0,target,note,128) && netRequests==0 && offlinePending,"solo-world host plans locally, no transport request");
    SupportDispatchTick();
    check(made==2 && registered==0 && lastLocal && lastId==nullptr && !deployments[0].networked,
          "solo-world host creates unregistered local support: no ID, no native registration, no all-peer barrier");
    soloHost=false;ResetSupportDispatch();netRequests=0;
    check(SupportCallAt(0,target,note,128) && netRequests==1,"a host with other participants still uses the reliable protocol");
    testOnline=false;
    // Out-of-mission configuration (support_config.h), read from a real ini as plugin.cpp does.
    wchar_t ini[MAX_PATH];GetTempPathW(MAX_PATH,ini);wcscat_s(ini,L"edf6_support_dispatch_test.ini");
    FILE* f=nullptr;_wfopen_s(&f,ini,L"wb");check(f!=nullptr,"config ini written");
    const char text[]="[VehicleCrew]\r\nSupportDisabled=FIGHTER\r\nSupportSquadWeapon=shotgun\r\nSupportSquadLeaderWeapon=sniper\r\n"
                      "SupportPlatoonWeapons=flame,rocket,rifle\r\nSupportAircraftCrewWeapon=rocket\r\nSupportAircraftCount_HELI=3\r\n";
    std::fwrite(text,1,sizeof(text)-1,f);std::fclose(f);
    LoadSupportConfig(ini);DeleteFileW(ini);
    ResetSupportDispatch();made=0;fixtureMs+=40000;
    check(!SupportCallAt(6,target,note,128) && std::wcsstr(note,L"已在配置中停用") && !offlinePending && !made,
          "a unit disabled in the ini is refused with its reason before any planning");
    ResetSupportDispatch();made=0;resourceCount=0;routeResult=npc::navigation::Result::moving;
    check(SupportCallAt(21,target,note,128),"squad request accepted");SupportDispatchTick();
    check(made==4 && resourceCount==4 && lastResources[0]==SupportSoldierResource(SupportWeapon::sniper,true) &&
          lastResources[1]==SupportSoldierResource(SupportWeapon::shotgun,false) && lastResources[3]==SupportSoldierResource(SupportWeapon::shotgun,false),
          "the configured squad leader and member weapons are the soldiers actually created");
    ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
    SupportCallAt(22,target,note,128);SupportDispatchTick();
    check(made==12 && lastResources[0]==SupportSoldierResource(SupportWeapon::sniper,true) &&
          lastResources[1]==SupportSoldierResource(SupportWeapon::flame,false) && lastResources[5]==SupportSoldierResource(SupportWeapon::rocket,false) &&
          lastResources[9]==SupportSoldierResource(SupportWeapon::rifle,false),"each platoon squad gets its configured weapon");
    ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
    SupportCallAt(10,target,note,128);SupportDispatchTick();   // the stub's aircraft: one pilot each
    check(plannedAircraft==3 && made==6 && lastResources[0]==SupportSoldierResource(SupportWeapon::rocket,false),
          "the configured aircraft count reaches the runway planner and the crew carry the configured weapon");
    SupportPlan hostPlan=deployments[0].plan;
    check(Validate(hostPlan),"a peer accepts the host's configured weapons and count (structure, not its own ini)");
    hostPlan.units[1].resourceId=0x503;check(!Validate(hostPlan),"a non-template soldier resource is refused");
    // A room with an older All Forces peer (no kCapSoldierVariants): the host plans the protocol v2 plan an older
    // Validate accepts (rifles, each call's own aircraft count) and says why, instead of a silent mid-transaction refusal.
    peersNew=false;
    ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;routeResult=npc::navigation::Result::moving;
    SupportCallAt(21,target,note,128);SupportDispatchTick();
    bool rifles=made==4;for(int i=0;i<resourceCount;++i)rifles=rifles && lastResources[i]==(i%4 ? kSupportRangerResource : kSupportLeaderResource);
    SupportCallStatus(note,128);
    check(rifles && std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportLegacyPeers))!=nullptr,
          "older peer: rifle leader/members (ids 2/1) and the HUD says the configured weapons were not synchronised");
    ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
    SupportCallAt(10,target,note,128);SupportDispatchTick();
    check(plannedAircraft==1 && made==2 && lastResources[0]==kSupportRangerResource,"older peer: the call's own aircraft count, rifle crew");
    const auto old=deployments[0].plan;bool v2=true;
    for(unsigned i=0;i<old.count;++i)v2=v2 && (old.units[i].resourceId>=kSupportAircraftResource || old.units[i].resourceId<=2);
    check(v2,"the whole plan uses only resources a protocol v2 Validate knows");
    peersNew=true;ResetSupportDispatch();SupportCallStatus(note,128);
    check(!std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportLegacyPeers)),"the older-peer notice ends with the mission");
    // The transports (transport.cpp; the user, 2026-10-09: "飞机和直升机应该也有运输机", "飞机就空降"): four entries after the
    // ground ones, every older index and key as it was; a hull in the air, its pilot and the soldiers made inside it.
    check(SupportCallCount()==33 && !std::wcscmp(SupportCallKey(28),L"TRUCK_DELIVERY") && !std::wcscmp(SupportCallKey(29),L"SQUAD_HELI") &&
          !std::wcscmp(SupportCallKey(32),L"PLATOON_AIRDROP"),"the transports appended: every older index and key stands");
    check(SupportCallIcon(29)==SupportIcon::heli && SupportCallIcon(31)==SupportIcon::jet && SupportCallVariant(30)==SupportVariant::platoon &&
          SupportCallVariant(31)==SupportVariant::squad,
          "a helicopter row and a plane row, each with a squad and a platoon chip");
    ResetSupportDispatch();made=deleted=seatNow=activated=delivered=followed=resourceCount=0;fixtureMs+=40000;peersNew=true;
    check(SupportCallAt(30,target,note,128),"a helicopter assault (a platoon) queued");
    SupportDispatchTick();
    const SupportPlan assault=deployments[0].plan;
    check(made==14 && assault.count==14 && lastPrepared.heli==static_cast<int>(HeliBody::transport410) && seatNow==1 && activated==1,
          "a platoon in one transport helicopter: hull, pilot and twelve soldiers made inside it, seated, flying in at spawn");
    check(At<const void*>(SeatAt(objects[0],0),kSeatRider)==objects[1] && At<const void*>(SeatAt(objects[0],12),kSeatRider)==objects[13],
          "the pilot at the stick, the twelve in its other seats");
    check(delivered==1 && deliveredSquads==3 && followed==9 && deliveredHull==objects[0] && deliveredTops[0]==objects[2] &&
          deliveredTops[1]==objects[6] && deliveredTops[2]==objects[10],
          "three squads (a leader every four, the three after it following him) handed to their helicopter");
    check(deployments[0].delivered && Validate(assault),"handed over: the dispatcher leaves them to transport.cpp; a peer accepts the plan");
    SupportPlan bad=assault;bad.units[3].resourceId=kSupportLeaderResource;check(!Validate(bad),"a leader out of place is refused");
    bad=assault;bad.units[1].role=0;check(!Validate(bad),"a pilot detached from its hull is refused");
    // A composed load (support_call.h SupportLoadout): any number of squads up to the hull's twelve riders.
    bad=assault;bad.count=10;check(Validate(bad),"two squads (a composed load) are a valid plan");
    bad=assault;bad.units[14]=bad.units[2];bad.count=15;check(!Validate(bad),"thirteen riders, past the hull's seats, are refused");
    bad=assault;bad.units[0].resourceId=kSupportAircraftResource+kSupportAirborneOffset+31;check(!Validate(bad),"another entry's hull is refused");
    ResetSupportDispatch();made=0;ferries=paradrops=0;fixtureMs+=40000;
    SupportCallAt(31,target,note,128);SupportDispatchTick();
    check(made==6 && lastPrepared.transportPlane && ferries==1 && paradrops==1 && delivered==1,
          "a paratroop plane: four soldiers aboard, it flies on to the point attacking nothing and drops them there");
    ResetSupportDispatch();made=0;transportReady=false;fixtureMs+=40000;
    SupportCallAt(29,target,note,128);SupportDispatchTick();
    SupportCallStatus(note,128);
    check(!made && !offlinePending && std::wcsstr(note,L"运输机资源未安装"),"the transport's SGO not installed: refused with why, nothing made");
    transportReady=true;peersTransports=false;ResetSupportDispatch();made=0;fixtureMs+=40000;
    SupportCallAt(29,target,note,128);SupportDispatchTick();
    check(!made && !offlinePending,"a peer without the transports: refused before anything is made");
    peersTransports=true;
    // Retired with its soldiers got off: only the pilot still aboard is deleted (Retire), never the ones fighting on.
    ResetSupportDispatch();made=deleted=0;fixtureMs+=40000;
    SupportCallAt(29,target,note,128);SupportDispatchTick();
    check(made==6,"a squad's helicopter");
    for(unsigned seat=1;seat<6;++seat)Put<const void*>(SeatAt(objects[0],seat),kSeatRider,nullptr);
    aircraftLeft=true;fixtureMs+=1;SupportDispatchTick();aircraftLeft=false;
    check(deleted==1,"left: its pilot deleted, the four who got off left alone");
    heliHull=true;
    check(SupportWithdrawVehicle(objects[0]) && heliLeaves==1,"WITHDRAW: a support helicopter flies off");
    heliHull=false;
    check(!SupportWithdrawVehicle(objects[3]),"a soldier is no support vehicle");
    // A crewed APC arrived (Broken Arrow, the user 2026-10-09: "卡车之类的运输载具改成断剑那种操作方式"): the soldiers in its
    // passenger seats a squad, handed to it as their transport; its driver stays at the wheel.
    ResetSupportDispatch();made=deleted=followed=delivered=0;fixtureMs+=40000;
    {
        auto apc=Make(true),driver=Make(),first=Make(),second=Make();
        auto& d=deployments[0];d.used=d.assigned=d.started=true;d.id=7;d.born=fixtureMs;
        d.plan.catalogId=static_cast<unsigned>(GroundStart()+2);d.plan.count=4;
        d.plan.units[0].resourceId=kVehicle+1;
        for(unsigned i=1;i<4;++i){d.plan.units[i].resourceId=kSoldier;d.plan.units[i].role=1;}
        d.objects[0]=apc;d.objects[1]=driver;d.objects[2]=first;d.objects[3]=second;
        auto* v=static_cast<unsigned char*>(const_cast<void*>(apc.obj));
        for(unsigned i=0;i<3;++i){Put<const void*>(SeatAt(v,i),kSeatRider,d.objects[i+1].obj);}
        routeResult=npc::navigation::Result::arrived;
        SupportDispatchTick();fixtureMs+=16;SupportDispatchTick();
        check(d.delivered && delivered==1 && deliveredSquads==1 && deliveredTops[0]==first.obj && deliveredHull==apc.obj && followed==1,
              "arrived: its passengers one squad (the second following the first), handed to the APC; the driver not among them");
        // WITHDRAW: back to its entry (here at once: nobody drives it), its own crew aboard deleted, then the hull.
        driverAboard=false;deleted=0;
        check(SupportWithdrawVehicle(apc.obj) && d.withdrawing,"WITHDRAW: a support APC goes back to its entry");
        fixtureMs+=16;SupportDispatchTick();
        check(deleted==3,"out: its crew still aboard deleted (all three here)");
        fixtureMs+=16;SupportDispatchTick();
        check(deleted==4 && !d.withdrawing,"then the hull");
        driverAboard=true;routeResult=npc::navigation::Result::pending;
    }
    // A composed load (the bar's composition panel; the user, 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人"; a click
    // a whole squad). The wire form first: PackSupportLoadout / UnpackSupportLoadout round trip, malformed refused.
    {
        SupportLoadout l{};l.count=8;
        for(int i=0;i<4;++i){l.soldier[i]=SupportWeapon::wingLance;l.soldier[4+i]=SupportWeapon::fencerCannon;}
        SupportLoadout back{};
        check(UnpackSupportLoadout(PackSupportLoadout(l),&back) && back.count==8 && back.soldier[0]==SupportWeapon::wingLance &&
              back.soldier[7]==SupportWeapon::fencerCannon,"a load packs into a request's 64 bits and back");
        check(PackSupportLoadout(SupportLoadout{})==0 && !UnpackSupportLoadout(0,&back) && back.count==0,"no load: 0, the call's own");
        check(!UnpackSupportLoadout(0xDu,&back) && !UnpackSupportLoadout(0x1u|(0xFull<<4),&back) &&
              !UnpackSupportLoadout(PackSupportLoadout(l)|(1ull<<60),&back),
              "malformed: thirteen soldiers, a kind past the table, bits past the count");
        check(SupportLoadoutResource(l,0)==SupportSoldierResource(SupportWeapon::wingLance,true) &&
              SupportLoadoutResource(l,4)==SupportSoldierResource(SupportWeapon::fencerCannon,true) &&
              SupportLoadoutResource(l,5)==SupportSoldierResource(SupportWeapon::fencerCannon,false),"a leader every four, each squad its own");
    }
    check(SupportCallSeats(21)==12 && SupportCallSeats(22)==12 && SupportCallSeats(29)==12 && SupportCallSeats(32)==12 &&
          SupportCallSeats(GroundStart()+2)==4 && SupportCallSeats(GroundStart()+4)==4 && SupportCallSeats(GroundStart())==0 &&
          SupportCallSeats(GroundStart()+3)==0 && SupportCallSeats(0)==0,
          "seats: the infantry and the transports twelve, a crewed APC / truck its four passenger rows; a tank, an empty delivery, an air call none");
    {
        SupportLoadout p{};
        check(SupportCallPreset(21,&p) && p.count==4 && p.soldier[0]==SupportWeapon::sniper && p.soldier[1]==SupportWeapon::shotgun,
              "a squad's preset: the ini's leader and members");
        check(SupportCallPreset(30,&p) && p.count==12 && p.soldier[4]==SupportWeapon::sniper && p.soldier[5]==SupportWeapon::rocket,
              "a platoon's preset: three squads, the ini's platoon weapons");
        check(!SupportCallPreset(0,&p) && p.count==0,"an air call has no load");
    }
    {
        SupportLoadout l{};l.count=8;
        for(int i=0;i<4;++i){l.soldier[i]=SupportWeapon::wingLance;l.soldier[4+i]=SupportWeapon::fencerCannon;}
        ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;routeResult=npc::navigation::Result::moving;peersNew=true;
        check(SupportCallComposedAt(21,target,&l,note,128),"a composed squad call queued");SupportDispatchTick();
        check(made==8 && resourceCount==8 && lastResources[0]==SupportSoldierResource(SupportWeapon::wingLance,true) &&
              lastResources[3]==SupportSoldierResource(SupportWeapon::wingLance,false) &&
              lastResources[4]==SupportSoldierResource(SupportWeapon::fencerCannon,true) && Validate(deployments[0].plan),
              "the composed squads walk in: a Wing Diver squad and a Fencer squad, each with its own leader; a peer accepts it");
        ResetSupportDispatch();made=deleted=seatNow=activated=delivered=followed=resourceCount=0;fixtureMs+=40000;
        SupportLoadout one{};one.count=4;for(int i=0;i<4;++i)one.soldier[i]=SupportWeapon::wingThunderBow;
        check(SupportCallComposedAt(29,target,&one,note,128),"a composed helicopter assault queued");SupportDispatchTick();
        check(made==6 && deployments[0].plan.count==6 && resourceCount==5 && lastResources[1]==SupportSoldierResource(SupportWeapon::wingThunderBow,true) &&
              Validate(deployments[0].plan),"one squad aboard the transport: hull, pilot and the four composed soldiers");
        ResetSupportDispatch();made=0;fixtureMs+=40000;
        SupportLoadout many{};many.count=12;
        check(!SupportCallComposedAt(GroundStart()+2,target,&many,note,128) && !offlinePending && !made &&
              std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportLoadoutTooMany))!=nullptr,
              "twelve soldiers for an APC's four seats: refused with the reason, nothing planned");
        // An older peer (no kCapLoadout): the call's own load, and the HUD says the chosen load was not sent.
        peersLoadout=false;ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;routeResult=npc::navigation::Result::moving;
        SupportCallComposedAt(21,target,&l,note,128);SupportDispatchTick();SupportCallStatus(note,128);
        check(made==4 && lastResources[0]==SupportSoldierResource(SupportWeapon::sniper,true) &&
              std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportLoadoutLegacyPeers))!=nullptr,
              "older peer: the squad's own four (the ini's), and the HUD says why");
        peersLoadout=true;ResetSupportDispatch();
        // Online (not the host): the load goes to the host in the request.
        testOnline=true;submitReady=true;lastNetLoadout=0;fixtureMs+=40000;
        check(SupportCallComposedAt(21,target,&l,note,128) && lastNetLoadout==PackSupportLoadout(l),"a guest's request carries its load");
        testOnline=false;ResetSupportDispatch();
    }
    {
        // A crewed APC with a composed load: its driver, then one squad of passengers (a leader first); a peer accepts it.
        SupportPlan apc{};apc.catalogId=static_cast<unsigned>(GroundStart()+2);apc.count=6;std::memcpy(apc.target,target,12);
        apc.units[0].resourceId=kVehicle+1;
        apc.units[1].resourceId=kSoldier;apc.units[1].role=1;
        for(unsigned i=2;i<6;++i){apc.units[i].resourceId=SupportSoldierResource(SupportWeapon::fencerShotgun,i==2);apc.units[i].role=1;}
        for(auto& u:apc.units)u.matrix[0]=u.matrix[5]=u.matrix[10]=u.matrix[15]=1;
        check(Validate(apc),"a crewed APC: driver and a composed squad of passengers");
        apc.units[1].resourceId=kLeader;check(!Validate(apc),"its driver is a member, never a leader");
        apc.units[1].resourceId=kSoldier;apc.count=7;apc.units[6]=apc.units[5];check(!Validate(apc),"six aboard a five-seat APC is refused");
    }
    {
        // Out-of-game loadouts (support_loadout.h, docs/feature-2026-10-09-loadout-editor.md), from a real ini.
        wchar_t path[MAX_PATH];GetTempPathW(MAX_PATH,path);wcscat_s(path,L"edf6_support_loadout_test.ini");
        FILE* out=nullptr;_wfopen_s(&out,path,L"wb");check(out!=nullptr,"loadout ini written");
        const char loadText[]="[VehicleCrew]\r\nSupportPreset_SQUAD=lance@1E3A8A*2,cannon,rifle@X:FFFFFF\r\n"
                              "SupportPreset_TANK_CREWED=rifle\r\nSupportTankRounds=AP:1,HE:1\r\n";
        std::fwrite(loadText,1,sizeof(loadText)-1,out);std::fclose(out);
        LoadSupportConfig(path);DeleteFileW(path);
        const auto& c=SupportCfg();
        check(c.preset[21].count==4 && c.preset[21].kind[0]==SupportWeapon::wingLance && c.preset[21].look[0].primary==0x1E3A8A &&
              c.preset[21].kind[2]==SupportWeapon::fencerCannon && !c.preset[21].look[2].On() && c.preset[21].look[3].secondary==0xFFFFFF,
              "SupportPreset_SQUAD read with its kinds and colours");
        check(c.preset[GroundStart()].count==0 && std::wcsstr(c.problems,L"SupportPreset_TANK_CREWED")!=nullptr,
              "a preset on a tank (no seats) is refused and named");
        check(c.tankRounds.ratio && c.tankRounds.count==2,"SupportTankRounds read as a ratio");
        looksQueued=0;PreloadSupportLooks();
        check(looksQueued==3,"mission start queues the three coloured soldiers' templates");
        lookFiles=false;looksQueued=0;PreloadSupportLooks();
        check(looksQueued==0,"a coloured soldier whose file is missing is not queued (logged, stock)");
        lookFiles=true;
        SupportLoadout p{};
        check(SupportCallPreset(21,&p) && p.count==4 && p.soldier[1]==SupportWeapon::wingLance && p.soldier[3]==SupportWeapon::rifle,
              "the preset is the composition panel's start");
        // A call as it is (a radio weapon) brings the preset, offline with the looks.
        lookReady=true;peersNew=true;ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
        routeResult=npc::navigation::Result::moving;
        check(SupportCallAt(21,target,note,128),"a preset squad call queued");SupportDispatchTick();
        const auto look=[](std::uint32_t r) noexcept {return r|(1u<<kSupportLookShift);};
        check(made==4 && lastResources[0]==look(SupportSoldierResource(SupportWeapon::wingLance,true)) &&
              lastResources[1]==look(SupportSoldierResource(SupportWeapon::wingLance,false)) &&
              lastResources[2]==SupportSoldierResource(SupportWeapon::fencerCannon,false) &&
              lastResources[3]==look(SupportSoldierResource(SupportWeapon::rifle,false)) && Validate(deployments[0].plan),
              "offline: the preset's soldiers, the coloured ones with their look, the uncoloured stock");
        // The panel changed seat 2: that seat comes stock, the others keep their colours.
        ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
        SupportLoadout changed=PresetLoadout(c.preset[21]);changed.soldier[1]=SupportWeapon::sniper;
        check(SupportCallComposedAt(21,target,&changed,note,128),"a changed preset queued");SupportDispatchTick();
        check(lastResources[0]==look(SupportSoldierResource(SupportWeapon::wingLance,true)) &&
              lastResources[1]==SupportSoldierResource(SupportWeapon::sniper,false),"a seat changed on the panel comes stock");
        // Not generated / not preloaded: stock look, same soldiers.
        lookReady=false;ResetSupportDispatch();made=0;resourceCount=0;fixtureMs+=40000;
        SupportCallAt(21,target,note,128);SupportDispatchTick();
        check(made==4 && lastResources[0]==SupportSoldierResource(SupportWeapon::wingLance,true),"no generated look: the stock template");
        // Another machine's plan never carries a look (no capability bit to agree on it).
        SupportPlan coloured=deployments[0].plan;coloured.units[0].resourceId=look(coloured.units[0].resourceId);
        check(Validate(coloured),"this machine's own plan may carry a look");
        testOnline=true;soloHost=false;check(!Validate(coloured),"a plan with a look is refused in a room with peers");
        testOnline=false;ResetSupportDispatch();
        // The tanks' rounds: AP:1,HE:1 alternates over the tanks deployed this mission, AP first.
        const auto tank=static_cast<std::uint32_t>(GroundStart());
        apReady=true;SupportPlan plan{};routeResult=npc::navigation::Result::moving;
        check(Plan(tank,target,0,&plan)==support_net::PlanResult::ready && plan.units[0].resourceId==kVehicle+(1u<<8) &&
              Validate(plan) && HullRound(plan.units[0].resourceId,SupportVehicleKind::tank)==TankRound::ap,"the first tank: AP");
        tanksCalled=1;plan={};
        check(Plan(tank,target,0,&plan)==support_net::PlanResult::ready && plan.units[0].resourceId==kVehicle,"the second tank: HE (stock)");
        tanksCalled=2;plan={};
        check(Plan(tank+1,target,0,&plan)==support_net::PlanResult::ready && plan.units[0].resourceId==kVehicle+(1u<<8),
              "the third (an empty delivery tank counts too): AP");
        apReady=false;tanksCalled=0;plan={};
        check(Plan(tank,target,0,&plan)==support_net::PlanResult::ready && plan.units[0].resourceId==kVehicle,
              "AP file not installed: stock HE (logged)");
        apReady=true;testOnline=true;soloHost=true;plan={};
        check(Plan(tank,target,0,&plan)==support_net::PlanResult::ready && plan.units[0].resourceId==kVehicle+(1u<<8),
              "the host of a one-player world: AP");
        SupportPlan ap=plan;soloHost=false;
        check(!Validate(ap) && HullRound(ap.units[0].resourceId,SupportVehicleKind::tank)==TankRound::he,
              "a peer in the room: an AP hull id is refused (stock is what it would make)");
        soloHost=true;
        check(!GroundHullOk(kVehicle+1u+(1u<<8),SupportVehicleKind::transport),"a round on an APC is refused");
        testOnline=false;soloHost=false;apReady=false;ResetSupportDispatch();
        check(tanksCalled==0,"a new mission counts its tanks from the first");
    }
    std::printf("support_dispatch_test: %d checks passed\n",checks);
}
#endif
