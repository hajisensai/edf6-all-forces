#include "../src/mission_crew_support.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;
namespace {
unsigned char vehicle[0x1000]{},seats[2*edf::kSeatStride]{},vctrl[16]{},people[15][0x1800]{},pctrl[15][16]{};
bool sessionFlag=false,ready=true,active=true;int unholds=0;int spawned=0,deleted=0,boards=0,releases=0,retries=0,failAt=0;
std::uint64_t submitted=0;
}
bool InstallMissionCrewTeam(MissionCrewTeamFn) noexcept {return true;}
void SetObjectTeam(unsigned char* p,std::int32_t team) noexcept {Put<int>(p,kTeam,team);}
bool InSession() noexcept {return sessionFlag;}
bool OnlineHostOnly() noexcept {return true;}
bool SupportTransactionActive(std::uint64_t) noexcept {return active;}
bool HoldSupportSoldier(const ObjRef&,bool held) noexcept {if(!held)++unholds;return true;}
bool NpcRestoreMissionSeat(unsigned char*,unsigned char*) noexcept {return true;}
bool SupportSoldiersReady() noexcept {return ready;}
bool ReadNativeObjectId(const unsigned char* p,unsigned char* id) noexcept {std::memset(id,0,32);id[0]=p==vehicle ? 5 : 6;return true;}
unsigned char* MissionVehicleById(const unsigned char* id) noexcept {return id && id[0]==5 && !vehicle[kDead] ? vehicle : nullptr;}
unsigned char* PlayerHuman() noexcept {return vehicle;}
bool SeatPoint(const unsigned char*,unsigned seat,float* at,float* reach) noexcept {at[0]=static_cast<float>(seat*2);at[1]=at[2]=0;*reach=3;return true;}
bool ApplySupportSoldierSpawn(const float*,bool,const unsigned char*,ObjRef* out) noexcept {
    if(failAt && spawned+1==failAt)return false;
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
 VirtualFree(image,0,MEM_RELEASE);std::printf("mission_crew_support: %d checks passed\n",checks);
}

