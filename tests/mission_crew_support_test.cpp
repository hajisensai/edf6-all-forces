#include "../src/mission_crew_support.cpp"
#include "../src/npc_tier.h"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;Config config{};
const Config& Cfg() noexcept {return config;}
NpcMissionCrewFn silentRegistered=nullptr;
void SetNpcMissionCrew(NpcMissionCrewFn f) noexcept {silentRegistered=f;}
namespace {
unsigned char vehicle[0x1000]{},seats[2*edf::kSeatStride]{},vctrl[16]{},people[15][0x1800]{},pctrl[15][16]{};
bool sessionFlag=false,ready=true,active=true;int unholds=0;int spawned=0,deleted=0,boards=0,releases=0,retries=0,failAt=0;
std::uint64_t submitted=0;
}
bool InstallMissionCrewTeam(MissionCrewTeamFn) noexcept {return true;}
void SetObjectTeam(unsigned char* p,std::int32_t team) noexcept {Put<int>(p,kTeam,team);}
bool InSession() noexcept {return sessionFlag;}
bool hostHere=true;bool OnlineHostOnly() noexcept {return hostHere;}
bool SupportTransactionActive(std::uint64_t) noexcept {return active;}
bool HoldSupportSoldier(const ObjRef&,bool held) noexcept {if(!held)++unholds;return true;}
bool NpcRestoreMissionSeat(unsigned char*,unsigned char*) noexcept {return true;}
bool SupportSoldiersReady() noexcept {return ready;}
bool ReadNativeObjectId(const unsigned char* p,unsigned char* id) noexcept {std::memset(id,0,32);id[0]=p==vehicle ? 5 : 6;return true;}
unsigned char* MissionVehicleById(const unsigned char* id) noexcept {return id && id[0]==5 && !vehicle[kDead] ? vehicle : nullptr;}
unsigned char* PlayerHuman() noexcept {return vehicle;}
bool SeatPoint(const unsigned char*,unsigned seat,float* at,float* reach) noexcept {at[0]=static_cast<float>(seat*2);at[1]=at[2]=0;*reach=3;return true;}
// The crew colours (support_dispatch.cpp, stage 5): the host's variant for each crew seat, and the file a peer makes it from.
std::uint32_t crewCatalogAsked=0;std::uint64_t crewVariant=0;bool lookHere=true;
const wchar_t* looks[15]{};std::uint32_t resources[15]{};const wchar_t* kLookPath=L"app:/object/edf6vc_npc_rifle_1e3a8a_x.sgo";
std::uint64_t SupportCrewVariant(std::uint32_t catalog,std::uint32_t resource) noexcept {
    crewCatalogAsked=catalog;return IsSupportSoldierResource(resource) ? crewVariant : 0;
}
const wchar_t* SupportSoldierLookPath(std::uint32_t,std::uint64_t variant,bool,wchar_t*,std::size_t) noexcept {
    return variant && (variant&kVariantApplied) && lookHere ? kLookPath : nullptr;
}
// npc_arms.h / support_soldier.h: the building machine's player's weapons (localArms), their loads (armsLoad), the
// weapon hooks (armsReady); the weapon each soldier was made with (weapons[]).
bool armsReady=true;support_net::RequesterArms localArms{};support_net::PlanResult armsLoad=support_net::PlanResult::ready;
int armsAsked=0;std::uint64_t weapons[15]{};
bool SupportSoldierArmsReady() noexcept {return armsReady;}
void LocalRequesterArms(support_net::RequesterArms* out) noexcept {*out=localArms;}
support_net::PlanResult NpcArmsLoaded(const support_net::Plan&) noexcept {++armsAsked;return armsLoad;}
bool ApplySupportSoldierResource(const float*,std::uint32_t resource,const unsigned char*,bool local,ObjRef* out,const wchar_t* look,
                                 std::uint64_t weapon) noexcept {
    if(failAt && spawned+1==failAt)return false;
    if(local)return false;   // the mission crew is never a local-only soldier (as ApplySupportSoldierSpawn was)
    looks[spawned]=look;resources[spawned]=resource;weapons[spawned]=weapon;
    Put<void*>(people[spawned],kSelfCtrl,pctrl[spawned]);Put<int>(pctrl[spawned],8,1);
    *out=ObjRef::Of(people[spawned++]);return true;
}
bool DeleteSupportSoldier(const ObjRef&) noexcept {++deleted;return true;}
int NpcBoardCrew(unsigned char*,unsigned char* const*,int count) noexcept {boards+=count;return count;}
void ReleaseLegacyMissionRiders(unsigned char*) noexcept {++releases;}
void RetryMissionCrew(unsigned char*) noexcept {++retries;}
void ConfigureMissionCrew(MissionCrewFactoryFn,MissionVehicleIdFn,MissionCrewTickFn) noexcept {}
std::uint64_t SubmitPreparedSupportPlan(const SupportPlan&) noexcept {return ++submitted;}
}
namespace {
int checks=0;
void Check(bool b,const char* what){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
void Reset(){using namespace crew;ResetMissionCrewSupport();teamReady=true;spawned=deleted=boards=releases=retries=failAt=0;sessionFlag=false;ready=true;active=true;unholds=0;
 armsReady=true;localArms={};armsLoad=support_net::PlanResult::ready;armsAsked=0;std::memset(weapons,0,sizeof(weapons));
 std::memset(vehicle,0,sizeof(vehicle));std::memset(seats,0,sizeof(seats));std::memset(people,0,sizeof(people));
 Put<void*>(vehicle,kSelfCtrl,vctrl);Put<int>(vctrl,8,1);Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);
 for(unsigned i=0;i<2;++i){Put<unsigned>(seats+i*edf::kSeatStride,0x30,1);Put<unsigned>(seats+i*edf::kSeatStride,0x34,1);}
}
}
int main(){using namespace crew;image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 Check(image!=nullptr,"private image");Reset();SupportPlan p;
 Check(Build(vehicle,&p) && p.count==3,"one existing vehicle plus two real actor specs");
 Check(Request(vehicle,false) && spawned==2 && boards==2 && releases==1,"offline script crew commits real actors then releases legacy riders");
 Check(Request(vehicle,true) && spawned==2,"snapshot replay reuses committed real actors");
 Reset();failAt=2;Check(!Request(vehicle,false) && deleted==1 && releases==0,"partial actor creation rolls back before touching legacy crew");
 Reset();ready=false;Check(!Request(vehicle,false) && spawned==0,"unloaded resources queue without fabricating crew");
 Reset();sessionFlag=true;Build(vehicle,&p);
 Check(ValidateMissionCrewPlan(p),"known existing vehicle plan accepted");
 p.units[1].resourceId=77;Check(!ValidateMissionCrewPlan(p),"non-catalog actor rejected");p.units[1].resourceId=2;
 p.units[1].role=0;Check(!ValidateMissionCrewPlan(p),"unbound mission actor rejected");p.units[1].role=1;
 Check(ApplyMissionCrewPlan(7,p,true) && spawned==2 && boards==0 && releases==0,"remote creates real actors but waits for native host seat messages");
 Check(ApplyMissionCrewPlan(7,p,true) && spawned==2,"replayed commit never creates duplicate soldiers");
 DestroyMissionCrewPlan(7);Check(deleted==2 && retries==1,"rollback deletes only owned soldiers and retains existing vehicle");
 Reset();sessionFlag=true;Check(Request(vehicle,false) && submitted==1 && spawned==0,"host submits through shared support transaction");
 Check(Request(vehicle,false) && submitted==1,"pending network deployment is deduplicated");
 Reset();Request(vehicle,false);auto& d=deployments[0];
 Put<void*>(people[0],0x1548,vehicle);Put<void*>(people[1],0x1548,vehicle);BoardPending(d);
 const int before=boards;Put<void*>(people[0],0x1548,nullptr);BoardPending(d);
 Check(boards==before,"dismounted real soldier is not forced back into vehicle every frame");
 TeamChanged(vehicle,1);
 Check(At<int>(people[1],kTeam)==1 && At<int>(people[0],kTeam)!=1,"script team follows current crew but not dismounted soldiers");
 Reset();sessionFlag=true;Build(vehicle,&p);active=false;
 Check(ApplyMissionCrewPlan(9,p,false) && boards==0 && unholds==0,"host keeps actors held before all-peer activation");
 active=true;Tick(vehicle);
 Check(boards==2 && unholds==2,"all-peer active barrier releases crew before native boarding");
 Reset();sessionFlag=true;Build(vehicle,&p);active=false;ApplyMissionCrewPlan(10,p,true);
 Check(unholds==0,"remote actor remains held before reliable activation");active=true;Tick(vehicle);
 Check(unholds==2 && boards==0,"remote activation releases hold without generating seat messages");
 // Crew colours (SupportCrewColour_MISSION): each seat carries the host's look variant; a peer makes it from the file.
 Reset();sessionFlag=true;crewVariant=LookVariant({0x1E3A8A,kStockColour})|kVariantApplied;Build(vehicle,&p);
 Check(crewCatalogAsked==support_net::kMissionCrewCatalog && p.units[1].variant && p.units[2].variant &&
       LookOfVariant(p.units[1].variant,nullptr),"each mission crew seat carries its colours' variant");
 Check(ValidateMissionCrewPlan(p),"a coloured mission crew plan is accepted");
 Reset();crewVariant=LookVariant({0x1E3A8A,kStockColour})|kVariantApplied;
 Check(Request(vehicle,false) && spawned==2 && looks[0]==kLookPath && looks[1]==kLookPath && resources[0]==2 && resources[1]==1,
       "offline: the leader and the member made from their coloured files, as the rifle templates' resources");
 Reset();crewVariant=LookVariant({0x1E3A8A,kStockColour})|kVariantApplied;lookHere=false;
 Check(Request(vehicle,false) && spawned==2 && !looks[0] && !looks[1],"a colour file missing here: the crew comes stock, never refused");
 lookHere=true;
 Reset();sessionFlag=true;crewVariant=LookVariant({0x1E3A8A,kStockColour});Build(vehicle,&p);
 Check(ApplyMissionCrewPlan(11,p,true) && !looks[0],"a look the host did not apply: stock");
 Reset();sessionFlag=true;crewVariant=0;Build(vehicle,&p);
 Check(!p.units[1].variant && ApplyMissionCrewPlan(12,p,true) && !looks[0],"no crew colour: stock, no variant (an older host's plan)");
 p.units[1].variant=kVariantApplied|kVariantVehicle|1;Check(!ValidateMissionCrewPlan(p),"pylons on a mission crew soldier are refused");
 crewVariant=0;
 // 测试站 #41 (mission_crew_quiet.h): a mission crew's own talk is off while it sits in its vehicle and back once off it.
 {
  constexpr std::uint32_t kTalk=crewquiet::kRandomTalk,kOther=0x4004420;   // the stock default flags hold the bit
  auto flags=[](int i){return At<std::uint32_t>(people[i],kObjectFlags);};
  auto aboard=[](int i,bool on){Put<void*>(people[i],0x1548,on ? static_cast<void*>(vehicle) : nullptr);};
  // The pure step on its own: owned clear, owned restore, nothing owed for a bit someone else had cleared, gone forgotten.
  Check(crewquiet::Next(true,true,kOther|kTalk,false).write && crewquiet::Next(true,true,kOther|kTalk,false).flags==kOther &&
        crewquiet::Next(true,true,kOther|kTalk,false).quieted,"aboard: the talk bit cleared and owed back");
  Check(!crewquiet::Next(true,true,kOther,false).write && !crewquiet::Next(true,true,kOther,false).quieted,
        "aboard with the bit already off: nothing written, nothing owed");
  Check(crewquiet::Next(true,false,kOther,true).flags==(kOther|kTalk) && crewquiet::Next(true,false,kOther,true).write &&
        !crewquiet::Next(true,false,kOther,true).quieted,"off the vehicle: the owed bit given back");
  Check(!crewquiet::Next(true,false,kOther,false).write,"never quieted: never written");
  Check(!crewquiet::Next(false,false,0,true).write && !crewquiet::Next(false,false,0,true).quieted,"a soldier gone: forgotten, no write");
  Check(crewquiet::Next(true,true,kOther|kTalk,true).flags==kOther && crewquiet::Next(true,true,kOther|kTalk,true).quieted,
        "set again while aboard: cleared again, still owed once");

  Reset();InstallMissionCrewSupport();
  Check(silentRegistered==&MissionCrewRiding,"the silent-boarding judge is the mission crew registry");
  Request(vehicle,false);
  for(int i=0;i<2;++i)Put<std::uint32_t>(people[i],kObjectFlags,kOther|kTalk);
  MissionCrewQuietTick();
  Check(flags(0)==(kOther|kTalk) && flags(1)==(kOther|kTalk),"on foot by its vehicle: talk untouched");
  Check(MissionCrewRiding(vehicle,people[0]) && MissionCrewRiding(vehicle,people[1]),"its own crew in its own vehicle is a silent boarding");
  unsigned char stranger[0x1800]{},otherCar[0x1000]{},sctrl[16]{},octrl[16]{};
  Put<void*>(stranger,kSelfCtrl,sctrl);Put<void*>(otherCar,kSelfCtrl,octrl);
  Check(!MissionCrewRiding(vehicle,stranger),"a soldier of no mission deployment boarding it (the player's, a commanded one) sounds");
  Check(!MissionCrewRiding(otherCar,people[0]),"the mission crew boarding another vehicle sounds");
  aboard(0,true);aboard(1,true);MissionCrewQuietTick();
  Check(flags(0)==kOther && flags(1)==kOther,"seated in its vehicle: each soldier's talk bit cleared, nothing else");
  MissionCrewQuietTick();Check(flags(0)==kOther && deployments[0].quiet[0],"steady while aboard");
  aboard(0,false);MissionCrewQuietTick();
  Check(flags(0)==(kOther|kTalk) && !deployments[0].quiet[0] && flags(1)==kOther,"dismounted: its talk back, the one still aboard stays quiet");
  aboard(0,true);MissionCrewQuietTick();Check(flags(0)==kOther,"back aboard: quiet again");
  people[1][kDead]=1;MissionCrewQuietTick();
  Check(flags(1)==(kOther|kTalk) && !deployments[0].quiet[1],"killed aboard: the bit given back (no longer a seated crew)");
  people[1][kDead]=0;MissionCrewQuietTick();Check(flags(1)==kOther,"(alive again in the fixture) quiet again");
  vehicle[kDead]=1;MissionCrewQuietTick();
  Check(flags(0)==(kOther|kTalk) && flags(1)==(kOther|kTalk),"its vehicle destroyed: every soldier's talk back");
  vehicle[kDead]=0;MissionCrewQuietTick();Check(flags(0)==kOther,"the vehicle back (fixture): quiet again");
  config.enabled=false;MissionCrewQuietTick();
  Check(flags(0)==(kOther|kTalk) && flags(1)==(kOther|kTalk),"the plugin turned off: every soldier's talk back");
  config.enabled=true;MissionCrewQuietTick();Check(flags(0)==kOther && flags(1)==kOther,"turned on again: quiet again");
  // A bit some other code had cleared before the soldier sat down is not the plugin's to give back.
  aboard(0,false);MissionCrewQuietTick();Put<std::uint32_t>(people[0],kObjectFlags,kOther);aboard(0,true);MissionCrewQuietTick();
  aboard(0,false);MissionCrewQuietTick();Check(flags(0)==kOther,"a bit already off when it sat down stays off when it leaves");
  // Rollback: the bit given back before the soldier is deleted (symmetric), then the slot forgets it.
  aboard(0,true);Put<std::uint32_t>(people[0],kObjectFlags,kOther|kTalk);MissionCrewQuietTick();
  const auto tx=deployments[0].transaction;Check(flags(0)==kOther && flags(1)==kOther,"both quiet before the rollback");
  DestroyMissionCrewPlan(tx);
  Check(flags(0)==(kOther|kTalk) && flags(1)==(kOther|kTalk) && deleted==2,"rollback: every quieted soldier's talk back, then deleted");
  Check(!MissionCrewRiding(vehicle,people[0]),"a rolled-back crew is no longer a mission crew");
  // Mission end: the scene is the teardown's, nothing written through the old pointers.
  Reset();Request(vehicle,false);for(int i=0;i<2;++i){Put<std::uint32_t>(people[i],kObjectFlags,kOther|kTalk);aboard(i,true);}
  MissionCrewQuietTick();Check(flags(0)==kOther,"quiet before the mission ends");
  ResetMissionCrewSupport();MissionCrewQuietTick();
  Check(flags(0)==kOther && !MissionCrewRiding(vehicle,people[0]),"mission reset: forgotten without a write");
  // A peer's copy (remote deployment): its own copies are quieted the same way (the talk event is gated per machine).
  Reset();sessionFlag=true;Build(vehicle,&p);ApplyMissionCrewPlan(21,p,true);
  for(int i=0;i<2;++i){Put<std::uint32_t>(people[i],kObjectFlags,kOther|kTalk);aboard(i,true);}
  MissionCrewQuietTick();
  Check(flags(0)==kOther && flags(1)==kOther && MissionCrewRiding(vehicle,people[1]),"a peer's copies quiet in their vehicle too");
  aboard(1,false);MissionCrewQuietTick();Check(flags(1)==(kOther|kTalk),"a peer's copy dismounted: talk back");
  // Not yet committed (a host's network transaction still pending): nothing is a mission crew yet.
  Reset();sessionFlag=true;Request(vehicle,false);
  Check(!MissionCrewRiding(vehicle,people[0]),"an uncommitted deployment quiets no one");
 }
 // The crew's player weapons (2026-11-10, the maintainer: 任务乘员 "改成拿玩家武器"; docs/feature-npc-weapon-tier.md 2.4):
 // the building machine's player's rifle-kind pick (offline this one, online the host), carried by the plan.
 {
  npctier::Arms pick;pick.on=true;pick.id=9;for(auto& st:pick.stars)st=3;   // a rifle row (Minion Buster Burst)
  const std::uint64_t rifle=npctier::PackArms(pick);
  Check(npctier::UnpackArms(rifle,SupportWeapon::rifle,nullptr),"the fixture's pick is a rifle-kind weapon");
  Reset();localArms.kind[static_cast<int>(SupportWeapon::rifle)]=rifle;
  Check(Build(vehicle,&p) && p.units[1].arms==rifle && p.units[2].arms==rifle && !p.units[0].arms,
        "each crew seat (leader and member: the rifle templates) carries the builder's rifle pick; the vehicle none");
  Check(Request(vehicle,false) && spawned==2 && weapons[0]==rifle && weapons[1]==rifle && armsAsked==1,
        "offline: the crew made with that weapon once it is loaded");
  Reset();localArms.kind[static_cast<int>(SupportWeapon::rifle)]=rifle;armsLoad=support_net::PlanResult::pending;
  Check(!Request(vehicle,false) && spawned==0 && releases==0,"its weapon still loading: nothing made, the old riders kept (asked again)");
  armsLoad=support_net::PlanResult::refused;
  Check(!Request(vehicle,false) && spawned==0,"its weapon cannot be loaded: nothing made, never the stock crew instead");
  armsLoad=support_net::PlanResult::ready;
  Check(Request(vehicle,false) && spawned==2 && weapons[0]==rifle,"loaded: made with it");
  Reset();sessionFlag=true;localArms.kind[static_cast<int>(SupportWeapon::rifle)]=rifle;armsLoad=support_net::PlanResult::pending;
  const auto before2=submitted;
  Check(!Request(vehicle,false) && submitted==before2,"host: no transaction before its own weapon is loaded");
  armsLoad=support_net::PlanResult::ready;
  Check(Request(vehicle,false) && submitted==before2+1,"host: submitted once loaded");
  Reset();sessionFlag=true;hostHere=false;localArms.kind[static_cast<int>(SupportWeapon::rifle)]=rifle;
  const auto before3=submitted;
  Check(!Request(vehicle,false) && submitted==before3 && armsAsked==0 && spawned==0,
        "a client builds no crew plan: picks and loads no weapon of its own player");
  hostHere=true;
  Reset();sessionFlag=true;localArms.kind[static_cast<int>(SupportWeapon::rifle)]=rifle;Build(vehicle,&p);
  Check(ValidateMissionCrewPlan(p),"a peer with the weapon hooks accepts an armed crew plan");
  Check(ApplyMissionCrewPlan(31,p,true) && weapons[0]==rifle && weapons[1]==rifle,"a peer makes the host's weapons, not its own");
  armsReady=false;
  Check(!ValidateMissionCrewPlan(p),"a peer without the weapon hooks refuses an armed crew plan (never stock)");
  armsReady=true;
  Reset();Check(Build(vehicle,&p) && !p.units[1].arms && !p.units[2].arms,"no pick (weapon hooks not in on the builder): stock crew");
  Check(Request(vehicle,false) && spawned==2 && !weapons[0] && !weapons[1],"...made as the stock templates");
 }
 VirtualFree(image,0,MEM_RELEASE);std::printf("mission_crew_support: %d checks passed\n",checks);
}

