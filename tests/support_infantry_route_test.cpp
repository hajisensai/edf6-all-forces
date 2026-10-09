// Real support dispatcher, NPC Think/Drive/MoveTo, and GroundNavigate. Inert scene/actors and
// a deterministic movement consumer replace only the game, not the route or NPC movement adapters.
#define SUPPORT_INFANTRY_NATIVE_TEST
#define Plan NpcCombatPlan
#define now npcFixtureMs
#define kLeader kNpcLeaderOffset
#define vehicle npcFixtureVehicle
#include "../tools/npc_core_check.cpp"
#undef vehicle
#undef kLeader
#undef now
#undef Plan
#include "../src/support_dispatch.cpp"
#include "../src/support_config.cpp"
#include <memory>
namespace crew {
namespace {bool supportSessionActive=true;}
support::Policy SupportMissionPolicy() noexcept {return {support::Environment::normal,false};}
int SupportAirCallCount() noexcept{return 21;}
const wchar_t* SupportAirCallName(int) noexcept{return L"air";}
const wchar_t* SupportAirCallKey(int) noexcept{return L"AIR";}
bool SupportSoloHostWorld() noexcept{return false;}
bool SupportAircraftSpec(int,SupportAircraft*) noexcept{return false;}
support::Refusal PlanAirSupport(const SupportAircraft&,const float*,const float*,support::Route*) noexcept{return support::Refusal::unsupported;}
unsigned char* PrepareSupportAircraft(const SupportAircraft&,const float*) noexcept{return nullptr;}
bool ActivateSupportAircraft(unsigned char*,const SupportAircraft&,const float*,bool) noexcept{return false;}
bool DeleteSupportAircraft(const ObjRef&) noexcept{return false;}
bool SupportSoldiersReady() noexcept{return true;}
const wchar_t* SupportSoldierFailureText() noexcept{return L"";}
bool ApplySupportSoldierResource(const float*,std::uint32_t,const unsigned char*,bool,ObjRef*) noexcept{return false;}
bool CreateSupportSoldierUnregistered(const float*,std::uint32_t,const unsigned char*,bool,ObjRef*) noexcept{return false;}
bool SupportPeersAcceptAirborne() noexcept{return true;}
bool SupportPeersAcceptRescue() noexcept{return true;}
void RescueHeliDeployed(unsigned char*,const float*,bool) noexcept{}
void RescueRequestFailed(const wchar_t*) noexcept{}
bool DeriveSupportSoldierNetId(const void*,unsigned,unsigned char*) noexcept{return false;}
bool RegisterSupportObject(const void*,const unsigned char*) noexcept{return false;}
bool FollowSupportSoldier(const ObjRef& who,const ObjRef& leader) noexcept {
    Follow(static_cast<unsigned char*>(const_cast<void*>(who.obj)),static_cast<unsigned char*>(const_cast<void*>(leader.obj)));return true;
}
bool DeleteSupportSoldier(const ObjRef&) noexcept{return false;}
bool HoldSupportSoldier(const ObjRef&,bool) noexcept{return true;}
bool SupportVehicleReady(SupportVehicleKind,SupportCrewMode) noexcept{return false;}
unsigned char* SpawnSupportVehicle(SupportVehicleKind,SupportCrewMode,const float*,const float*,const void*) noexcept{return nullptr;}
bool DeleteSupportVehicle(unsigned char*) noexcept{return false;}
void ConfigureSupportNet(const support_net::Hooks&) noexcept{}
bool SubmitSupportRequest(int,const float*,wchar_t*,std::size_t) noexcept{return false;}
void SupportNetTick() noexcept{}
void ResetSupportNet() noexcept{}
void ReportSupportFailure(std::uint64_t) noexcept{}
bool SupportTransactionActive(std::uint64_t) noexcept{return supportSessionActive;}
bool SupportPeersAcceptVariants() noexcept{return true;}
bool ValidateMissionCrewPlan(const SupportPlan&) noexcept{return false;}
bool ApplyMissionCrewPlan(std::uint64_t,const SupportPlan&,bool) noexcept{return false;}
void DestroyMissionCrewPlan(std::uint64_t) noexcept{}
void InstallMissionCrewSupport() noexcept{}
void ResetMissionCrewSupport() noexcept{}
bool InstallMissionParticipantGate(MissionParticipantAdmission,MissionPlayerCreated) noexcept{return true;}
bool MissionParticipantGateReady() noexcept{return true;}
bool SupportMissionPlayerAllowed(int) noexcept{return true;}
void NoteSupportMissionPlayerCreated(int,const ObjRef&) noexcept{}
bool MissionParticipantCreationsMatch(const ObjRef*,unsigned) noexcept{return true;}
bool ReadMissionParticipants(void**,unsigned,unsigned*,unsigned*) noexcept{return true;}
namespace support_net {bool ValidPlan(const Plan& p,bool) noexcept{return p.count>0&&p.count<=kMaxUnits;}}
bool NpcPrepareVehicleRoutePost(unsigned char*,const float*,float) noexcept{return false;}
bool HeliCommand(const void*,const Command&) noexcept{return false;}
bool JetCommand(const void*,const Command&) noexcept{return false;}
PlayArea MapPlayArea() noexcept{return {{-930,-930},{930,930},true,0,true};}
bool MapGroundNear(float,float,float,float* y,bool) noexcept{*y=0;return true;}
}
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
alignas(16) unsigned char units[4][0x2200]{},identities[4][0x20]{},weapon[0xC00]{};
unsigned char* weaponList[]={weapon};
void Unit(unsigned i,float x) {
    using namespace crew;
    auto* h=units[i];std::memset(h,0,sizeof(units[i]));std::memset(identities[i],0,sizeof(identities[i]));
    Put<const void*>(h,0,image+kSoldiers[0].vtable);Put<void*>(h,kSelfCtrl,identities[i]);
    Put<long>(identities[i],8,1);Put<long>(identities[i],12,1);Put<int>(h,kTeam,kTeamFriend);
    Put<float>(h,kPosition,x);h[kListFlags]|=kInAiList;Put<unsigned>(h,kControlMask,kMaskMove);
    Put<float>(h,kHumanHp,100);Put<float>(h,kHumanHpMax,100);
    Put<void*>(h,kWeapons,weaponList);Put<std::uint64_t>(h,kWeaponCount,1);
}
void Tick(bool dispatch=true,bool move=true) {
    using namespace crew;
    npcFixtureMs+=16;++frame;world.frame=frame;
    if(dispatch)SupportDispatchTick();
    Think(units[0],0);
    for(unsigned i=1;i<4;++i) {
        // The original native follow intent is already present when the plugin's Think wrapper runs.
        Put<float>(units[i],kMoveX,0.25f);Put<float>(units[i],kMoveZ,0);
        Think(units[i],0);
    }
    if(move) {
        auto* h=units[0];const float x=At<float>(h,kPosition)+At<float>(h,kMoveX)*0.16f;
        const float z=At<float>(h,kPosition+8)+At<float>(h,kMoveZ)*0.16f;
        Put<float>(h,kPosition,x);Put<float>(h,kPosition+8,z);
        // Scene fixture advances existing followers near their real leader, without changing links.
        for(unsigned i=1;i<4;++i){Put<float>(units[i],kPosition,x-static_cast<float>(i));Put<float>(units[i],kPosition+8,z);}
    }
}
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"inert EDF image fixture allocated");
    Jump(kSetFollow,reinterpret_cast<const void*>(&FollowRec));Reset();ResetSupportDispatch();
    config.npcEvade=false;config.npcFireLane=false;config.npcGuardFormation=10;sessionOn=true;host=true;
    Put<float>(weapon,kArmReach,100);Put<float>(weapon,kArmDamage,10);Put<int>(weapon,kWeaponAmmo,100);
    for(unsigned i=0;i<4;++i)Unit(i,-900-static_cast<float>(i));
    auto& d=deployments[0];d.used=true;d.assigned=true;d.networked=true;d.id=7001;d.born=npcFixtureMs;
    d.plan.catalogId=21;d.plan.count=4;
    for(unsigned i=0;i<4;++i){d.objects[i]=ObjRef::Of(units[i]);d.plan.units[i].resourceId=i ? kSoldier : kLeader;
        if(i)FollowSupportSoldier(d.objects[i],d.objects[0]);}
    SupportDispatchTick();
    Check(d.used && !d.started && !FindSquad(units[0]),"first activation waits for initial Think registration without rolling back new actors");
    Tick();Tick();Check(d.started && FindSquad(units[0])->routeActive,"online authority starts route once native squad is known");
    Check(follows==3 && At<void*>(units[1],kNpcLeaderOffset)==units[0],"support preserves real native follower links");
    auto* fine=Entry(units[0],npcFixtureMs);bool ordinaryLongFailed=false;
    for(int i=0;i<2200 && !ordinaryLongFailed;++i) {
        npcFixtureMs+=16;++frame;world.frame=frame;
        MoveTo(units[0],Pos(units[0]),d.plan.target,kInfantryRouteStop);
        ordinaryLongFailed=fine->navigation.failed;
    }
    Check(ordinaryLongFailed && fine->navigation.profile.cell==2 && At<float>(units[0],kMoveX)==0,
        "negative control: ordinary MoveTo cannot navigate the same direct 900m order within its unchanged bounds");
    fine->navigation={};
    Think(units[0],0); // the isolated negative-control loop did not run the normal Think registration
    const auto originalNodeLimit=npc::navigation::kNodes;
    int frames=0;bool progressed=false;
    for(;frames<2500 && !progressed;++frames){Tick();progressed=At<float>(units[0],kPosition)>-890;}
    Check(progressed && d.infantry[0].navigation.profile.cell==4 && soldiers[0].navigation.profile.cell==2,
        "dispatcher coarse route feeds ordinary cell2 MoveTo and actually advances a 900m entrance");
    Check(originalNodeLimit==1536,"long entrance does not expand global search capacity");
    auto* q=FindSquad(units[0]);Soldier* soldier=Entry(units[0],npcFixtureMs);
    const auto* oldLeader=At<void*>(units[1],kNpcLeaderOffset);const int oldFollows=follows;
    float same[3];std::memcpy(same,q->routePoint,12);
    soldier->target=ObjRef::Of(other);soldier->spotSet=true;soldier->spot[0]=123;
    NpcPrepareSquadRoute(units[0],same,1);
    Check(soldier->target.obj==other && soldier->spotSet && soldier->spot[0]==123 && follows==oldFollows,
        "waypoint refresh does not clear combat state or reparent followers");
    // The real combat branch remains before quiet route movement, even while ingress continues.
    Put<void*>(other,kSelfCtrl,ctrl);Put<long>(ctrl,8,1);Put<long>(ctrl,12,1);other[kDead]=0;
    const float x=At<float>(units[0],kPosition);
    world.enemies=1;world.enemy[0]=Enemy{other,{x+20,0,0},1};soldier->spotSet=true;
    soldier->spot[0]=x;soldier->spot[1]=0;soldier->spot[2]=5;soldier->spotAt=npcFixtureMs;
    Tick(true,false);
    Check(soldier->target.obj==other && q->routeActive,"nearby combat retains its target during route updates");
    const auto* battlePos=Pos(units[0]);const float battleEye[3]={battlePos[0],battlePos[1]+kEye,battlePos[2]};
    const auto battle=Drive(*soldier,units[0],kSoldiers[0],ArmsOf(units[0]),nullptr,battleEye,battlePos,q,npcFixtureMs);
    Check(std::strcmp(battle.move,"combat spot")==0,"actual Drive selects combat movement ahead of the support route");
    world.enemies=0;wall=true;
    for(int i=0;i<12;++i)Tick(true,false);
    Check(At<float>(units[0],kMoveX)==0 && At<float>(units[0],kMoveZ)==0,"blocked coarse or fine route stops leader instead of walking through wall");
    wall=false;
    Check(At<void*>(units[1],kNpcLeaderOffset)==oldLeader && At<float>(units[1],kMoveX)==0.25f,
        "quiet followers keep their native follow input while leader waits");
    // Remote copies cannot acquire/update the route or have their intent rewritten here.
    Put<unsigned char>(units[0],edf::kRiderNet+edf::kNetFlags,1);Put<float>(units[0],kMoveX,0.73f);
    Check(!NpcPrepareSquadRoute(units[0],same,1),"remote leader cannot accept a local route");
    Think(units[0],0);Check(At<float>(units[0],kMoveX)==0.73f,"remote Think leaves replicated intent untouched");
    Put<unsigned char>(units[0],edf::kRiderNet+edf::kNetFlags,0);
    for(;frames<18000 && !d.delivered;++frames)Tick();
    Check(d.delivered && std::fabs(At<float>(units[0],kPosition))<=kInfantryArrival+0.1f,
        "ordinary NPC Think inputs carry leader all the way from 900m entrance to final destination");
    Check(q->cmd.order==Order::guard && !q->routeActive && q->guardShape==npc::formation::Shape::perimeter,
        "arrival restores final guard and configured formation once");
    Check(follows==3 && At<void*>(units[1],kNpcLeaderOffset)==units[0],"entire ingress never recreates follower relationships");
    q->cmd={};q->routeActive=true;sessionOn=false;
    const float takeover[3]={At<float>(units[0],kPosition),0,0};
    Check(SquadCommand(units[0],Command{Order::guard,{takeover[0],takeover[1],takeover[2]}}) &&
          !NpcPrepareSquadRoute(units[0],takeover,1) && q->routeCancelled,
          "explicit player squad order permanently takes ownership from this ingress route");
    ResetSupportDispatch();ResetNpcAi();VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("support_infantry_route_test: %d checks passed after %d frames\n",checks,frames);
}
