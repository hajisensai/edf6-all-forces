// Production dispatcher submarine branches; native creation/transport are the existing inert fixtures.
#define main LegacySupportDispatchFixtureMain
#include "support_dispatch_test.cpp"
#undef main

namespace {
int checks=0;
void Check(bool value,const char* why) {
    ++checks;if(!value){std::fprintf(stderr,"FAIL sub dispatch: %s\n",why);std::exit(1);}
}
void ResetCase() {
    using namespace crew;
    ResetSupportDispatch();config=Config{};LoadSupportConfig(nullptr);
    testOnline=true;soloHost=false;fixtureMs+=1000;made=deleted=registered=seatNow=0;
    subLaunches=subActivated=subDeleted=dronePrepared=droneActivated=droneDeleted=0;
    terrainSea=Sea::water;allReady=true;nativeFail=false;subBindOk=subActivateOk=subDeleteOk=true;
    registerSupportOk=true;seatFail=false;pvpAttachOk=true;transactionActive=false;
    pvpAttachCalls=pvpForgetCalls=pvpLifecycle=pvpAttachOrder=pvpSeatOrder=pvpActivateOrder=0;
    pvpSupportMode=1;preparedSubPlan={};preparedSubToken=0;
}
crew::SupportPlan Planned() {
    using namespace crew;
    SupportPlan plan{};const float target[3]={40,0,80},from[3]={-30,0,-90};
    Check(PlanSubmarine(16,target,from,0,&plan)==support_net::PlanResult::ready,"exact host plan available");
    plan.requester[0]=0x55;plan.team=1;
    for(unsigned i=0;i<plan.count;++i){plan.units[i].netId[0]=0x60;plan.units[i].netId[4]=static_cast<unsigned char>(i+1);plan.units[i].netId[12]=5;}
    return plan;
}
}
int main() {
    using namespace crew;
    ResetCase();auto plan=Planned();
    Check(plan.count==2 && plan.units[1].resourceId==kSupportRangerResource && plan.units[1].role==1,"plan carries one actual Ranger pilot");
    Check(!std::memcmp(plan.units[0].matrix,plan.units[1].matrix,64) && plan.units[0].matrix[13]==-40 && plan.target[1]==10,
          "pilot and hull retain host underwater transform and separate afloat height");
    Check(SpawnDeployment(5001,plan,false,true),"network commit constructs hull and pilot");
    Check(made==2 && registered==1 && seatNow==0 && subActivated==0 && pvpAttachCalls==1,
          "commit registers hull, binds the complete batch and leaves pilot seating/ship activation behind ACK barrier");
    Check(lastId==plan.units[1].netId || (lastId && !std::memcmp(lastId,plan.units[1].netId,32)),"pilot receives the canonical supplied native ID");
    ++fixtureMs;SupportDispatchTick();Check(!subActivated && !seatNow,"pending all-peer transaction cannot surface or fight");
    transactionActive=true;++fixtureMs;SupportDispatchTick();
    Check(subActivated==1 && seatNow==1 && deployments[0].assigned && deployments[0].delivered,"all-peer activation seats real pilot and starts ship once");
    ++fixtureMs;SupportDispatchTick();Check(subActivated==1 && seatNow==1,"submarine never enters ordinary air/ground routing");
    const ObjRef parent=deployments[0].objects[0];
    const float from[3]={3,250,4},nose[3]={0,0,1};
    Check(SubmitSubmarineDrone(parent,from,nose,from,plan.requester),"authority submits one authenticated child plan");
    Check(preparedSubPlan.count==1 && preparedSubPlan.units[0].resourceId==kSupportSubDroneResource &&
          preparedSubPlan.units[0].variant==0xAABBCCDDu && preparedSubPlan.team==1 &&
          !std::memcmp(preparedSubPlan.requester,plan.requester,32),"child plan retains native parent key and canonical current owner/team");
    Check(SpawnDeployment(5002,preparedSubPlan,false,true) && dronePrepared==1 && !droneActivated,"child commit also remains inert before ACK");
    transactionActive=false;++fixtureMs;SupportDispatchTick();Check(!droneActivated,"unconfirmed child never fires");
    transactionActive=true;++fixtureMs;SupportDispatchTick();Check(droneActivated==1,"all-peer child ACK opens its activation gate");
    pvpSupportMode=-1;const auto prior=preparedSubToken;
    Check(!SubmitSubmarineDrone(parent,from,nose,from,plan.requester) && preparedSubToken==prior,"PVP mode/team transition cannot inherit a guessed side");
    pvpSupportMode=2;Check(SubmitSubmarineDrone(parent,from,nose,from,plan.requester) && preparedSubPlan.team==2,"later children refresh the owner's newly committed side");
    Destroy(5002);Check(droneDeleted==1,"child rollback uses only its real prepared-drone delete path");
    Destroy(5001);Check(subDeleted==1 && deleted==1,"hull rollback also deletes its own real crew");

    ResetCase();plan=Planned();pvpAttachOk=false;
    Check(!SpawnDeployment(5003,plan,false,true) && subDeleted==1 && deleted==1 && !subActivated && !deployments[0].used,
          "owner attachment failure rolls back hull and real pilot before activation");
    ResetCase();plan=Planned();registerSupportOk=false;
    Check(!SpawnDeployment(5004,plan,false,true) && made==1 && subDeleted==1 && !subActivated && !deployments[0].used,
          "native hull registration failure deletes partial creation without making or activating a pilot");
    ResetCase();plan=Planned();subBindOk=false;
    Check(!SpawnDeployment(5005,plan,false,true) && subDeleted==1 && deleted==1 && !subActivated,"parent owner binding failure rolls back complete batch");
    ResetCase();plan=Planned();Check(SpawnDeployment(5006,plan,false,true),"seat-failure fixture creates the complete inert batch");
    seatFail=true;transactionActive=true;++fixtureMs;SupportDispatchTick();
    Check(!subActivated && subDeleted==1 && deleted==1 && !deployments[0].used,"real pilot seat failure reports transaction failure and destroys the batch");
    ResetCase();plan=Planned();Check(SpawnDeployment(5007,plan,true,true),"remote peer creates same hull and pilot plan");
    transactionActive=true;++fixtureMs;SupportDispatchTick();
    Check(subActivated==1 && !seatNow && deployments[0].delivered,"remote activation waits for host native ride event rather than seating an independent pilot");
    auto bad=plan;bad.units[1].matrix[13]+=1;Check(!Validate(bad),"a peer cannot relocate its pilot away from host plan");
    bad=plan;bad.units[0].variant=4;Check(!Validate(bad),"hull cannot smuggle an unsupported payload variant");
    std::printf("sub support dispatcher: %d checks passed\n",checks);return 0;
}
