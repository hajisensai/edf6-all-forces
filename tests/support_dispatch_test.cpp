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
unsigned char objects[32][0x3000]{},controls[32][32]{},seats[32][edf::kSeatStride*3]{};
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
unsigned char* PrepareSupportAircraft(const SupportAircraft&,const float* matrix) noexcept {
    auto ref=Make(true);std::memcpy(static_cast<unsigned char*>(const_cast<void*>(ref.obj))+kPosition,matrix+12,12);
    return static_cast<unsigned char*>(const_cast<void*>(ref.obj));
}
bool lastAirborne=false;
bool ActivateSupportAircraft(unsigned char*,const SupportAircraft&,const float*,bool airborne) noexcept {++activated;lastAirborne=airborne;return true;}
bool peersAirborne=true;
bool SupportPeersAcceptAirborne() noexcept {return peersAirborne;}
bool DeleteSupportAircraft(const ObjRef& ref) noexcept {if(ref)++deleted;return true;}
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
    for(int i=0;i<count && i<3;++i)Put<const void*>(SeatAt(v,static_cast<unsigned>(i)),kSeatRider,crew[i]);
    return count;
}
bool DeriveSupportSoldierNetId(const void*,unsigned,unsigned char*) noexcept {return true;}
bool FollowSupportSoldier(const ObjRef&,const ObjRef&) noexcept {++followed;return true;}
bool DeleteSupportSoldier(const ObjRef& ref) noexcept {if(ref)++deleted;return true;}
int registered=0;
bool RegisterSupportObject(const void*,const unsigned char*) noexcept {++registered;if(!seatNow)++registeredBeforeSeat;return true;}
bool SupportVehicleReady(SupportVehicleKind,SupportCrewMode) noexcept {return true;}
unsigned char* SpawnSupportVehicle(SupportVehicleKind,SupportCrewMode,const float*,const float*,const void*) noexcept {return nullptr;}
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
bool SubmitSupportRequest(int,const float*,wchar_t* note,std::size_t size) noexcept {
    ++netRequests;
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
bool HeliCommand(const void*,const Command&) noexcept {return true;}
bool JetCommand(const void*,const Command&) noexcept {return true;}
PlayArea MapPlayArea() noexcept {return {{-1500,-1500},{1500,1500},true,0,true};}
bool MapGroundNear(float,float,float,float* y,bool) noexcept {*y=0;return terrain;}
#ifndef SUPPORT_ROUTE_NATIVE_TEST
float MapRay(const float*,const float*,float*) noexcept {return -1;}
#endif
#ifndef SUPPORT_ROUTE_NATIVE_TEST
npc::navigation::Result GroundNavigate(npc::navigation::State&,const float* from,const float*,float,std::uint64_t,float* waypoint,npc::navigation::Profile) noexcept {
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
    auto invalid=deployments[0].plan;invalid.units[1].role=0;
    check(!Validate(invalid),"a manifest cannot detach the specified crew from its aircraft");
    invalid=deployments[0].plan;invalid.count=1;
    check(!Validate(invalid),"a manned aircraft manifest cannot omit its real pilot");
    check(!SupportCallAt(0,target,note,128),"successful deployment consumes cooldown");
    ResetSupportDispatch();made=deleted=boardRequests=activated=0;terrain=false;fixtureMs+=40000;
    check(SupportCallAt(0,target,note,128),"invalid terrain request can be queued for explicit validation");
    SupportDispatchTick();check(!made && !offlinePending,"no safe entry refuses without spawning at target");
    ResetSupportDispatch();terrain=true;nativeFail=true;
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
    check(Plan(0,target,&networkPlan)==support_net::PlanResult::ready && Spawn(10,networkPlan,false),"committed online plan creates registered stand-ins");
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
    check(Plan(0,target,&networkPlan)==support_net::PlanResult::refused && !made,"older peer: air support refused before anything exists");
    SupportCallStatus(note,128);
    check(std::wcsstr(note,hudtext::Tr(hudtext::Tx::supportAirNeedsUpdate))!=nullptr,"older peer: the HUD says why");
    peersAirborne=true;
    // An older host's runway plan (kAircraft + catalog) is applied as that host planned it: hull, crew walking aboard.
    {SupportPlan legacy;check(Plan(0,target,&legacy)==support_net::PlanResult::ready,"plan for the legacy fixture");
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
    std::printf("support_dispatch_test: %d checks passed\n",checks);
}
#endif
