// Execute the production dispatcher and authenticated carrier-request adapter with inert native actors.
#define main LegacyCarrierSupportDispatchFixtureMain
#include "support_dispatch_test.cpp"
#undef main

namespace {
int checks=0;
void Check(bool value,const char* why) {
    ++checks;if(!value){std::fprintf(stderr,"FAIL carrier dispatch: %s\n",why);std::exit(1);}
}
void ResetCase() {
    using namespace crew;
    ResetSupportDispatch();config=Config{};LoadSupportConfig(nullptr);
    testOnline=true;testHost=true;soloHost=false;fixtureMs+=1000;made=deleted=registered=seatNow=0;
    subLaunches=subActivated=subDeleted=dronePrepared=droneActivated=droneDeleted=0;
    allReady=true;nativeFail=false;registerSupportOk=true;pvpAttachOk=true;transactionActive=false;
    carrierReady=carrierSlotReady=carrierReserveOk=carrierNetworkAccepted=true;
    carrierPrepared=carrierConfirmed=carrierCancelled=carrierReserved=carrierRecalls=carrierTransactionsCancelled=0;
    carrierNetworkKind=carrierNetworkSlot=99;std::memset(carrierNetworkParent,0,32);std::memset(carrierNetworkAnchor,0,12);
    pvpAttachCalls=pvpForgetCalls=pvpLifecycle=pvpAttachOrder=pvpSeatOrder=pvpActivateOrder=0;
    pvpSupportMode=1;preparedSubPlan={};preparedSubToken=0;
    submarineParent=Make(true);carrierDriver=Make();hostRequester=carrierDriver;
    auto* raw=static_cast<unsigned char*>(const_cast<void*>(submarineParent.obj));
    const float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,90,300,-70,1};
    std::memcpy(raw+kMatrix,matrix,64);
}
void AssignId(crew::SupportPlan& plan) {
    plan.units[0].netId[0]=0x70;plan.units[0].netId[4]=1;plan.units[0].netId[12]=5;
}
}
int main() {
    using namespace crew;
    const float from[3]={90,275,-70},heading[3]={0,0,1},target[3]={300,20,-100};
    for(unsigned kind=0;kind<3;++kind) {
        ResetCase();const auto token=SubmitCarrierSupportDrone(submarineParent,from,heading,target,static_cast<SupportDroneKind>(kind),kind+1);
        Check(token==1 && carrierReserved==1 && made==2,"host reserves one child transaction without locally spawning it");
        Check(preparedSubPlan.count==1 && preparedSubPlan.catalogId==16 && preparedSubPlan.units[0].resourceId==CarrierDroneResource(kind,kind+1,true),
              "gun blast and doll retain their exact kind slot and player-order intent");
        Check(preparedSubPlan.units[0].variant==0xAABBCCDDu && preparedSubPlan.requester[0]==0x55 && preparedSubPlan.team==1 &&
              !std::memcmp(preparedSubPlan.target,target,12),"child plan retains canonical parent key owner side and target");
        auto plan=preparedSubPlan;AssignId(plan);
        Check(SpawnDeployment(6000+kind,plan,false,true) && carrierPrepared==1 && !carrierConfirmed && !droneActivated && pvpAttachCalls==1,
              "native commit prepares one registered owned child behind all-peer activation");
        ++fixtureMs;SupportDispatchTick();Check(!carrierConfirmed && !droneActivated,"unconfirmed child cannot deduct sortie or fight");
        transactionActive=true;++fixtureMs;SupportDispatchTick();
        Check(carrierConfirmed==1 && droneActivated==1,"all-peer ACK activates and confirms the matching reserved slot once");
        ++fixtureMs;SupportDispatchTick();Check(carrierConfirmed==1 && droneActivated==1,"normal route ticks cannot reactivate a carrier child");
        Destroy(6000+kind);Check(carrierCancelled==1 && droneDeleted==1 && pvpForgetCalls==1,"rollback cancels exact slot and deletes the registered owned child");
    }
    ResetCase();carrierDriver={};
    Check(SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::gun,0)==1 && !CarrierDroneOrdered(preparedSubPlan.units[0].resourceId),
          "NPC carrier child retains orbit behavior instead of acquiring a player target order");
    ResetCase();carrierSlotReady=false;
    Check(!SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::gun,0) && !preparedSubToken && !carrierReserved,
          "host rejects unavailable live slot before transaction allocation");
    ResetCase();pvpSupportMode=-1;
    Check(!SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::gun,0) && !preparedSubToken,"uncommitted owner side cannot be guessed");
    ResetCase();carrierReserveOk=false;
    Check(!SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::gun,0) && preparedSubToken==1 && !carrierPrepared,
          "reservation race retires submitted transaction without making a child");
    ResetCase();Check(SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::doll,5)==1,"rollback fixture plans a doll");
    auto failedPlan=preparedSubPlan;AssignId(failedPlan);pvpAttachOk=false;
    Check(!SpawnDeployment(6100,failedPlan,false,true) && carrierCancelled==1 && droneDeleted==1 && !carrierConfirmed && !droneActivated,
          "owner failure cancels prepared slot and child before doll activation");
    ResetCase();Check(SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::gun,0)==1,"shot-down fixture submits child");
    auto deadPlan=preparedSubPlan;AssignId(deadPlan);Check(SpawnDeployment(6150,deadPlan,false,true),"shot-down fixture prepares child");
    transactionActive=true;++fixtureMs;SupportDispatchTick();
    Put<unsigned char>(const_cast<void*>(deployments[0].objects[0].obj),kDead,1);++fixtureMs;SupportDispatchTick();
    Check(!deployments[0].used && carrierCancelled==1 && droneDeleted==1,"shot-down child retires Session slot and weak ledger instead of silently renewing deployment");
    ResetCase();Check(SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::gun,0)==1,"remote death fixture submits child");
    deadPlan=preparedSubPlan;AssignId(deadPlan);Check(SpawnDeployment(6151,deadPlan,true,true),"remote death fixture prepares child");
    transactionActive=true;++fixtureMs;SupportDispatchTick();
    Put<unsigned char>(const_cast<void*>(deployments[0].objects[0].obj),kDead,1);++fixtureMs;SupportDispatchTick();
    Check(deployments[0].used && !carrierCancelled && !droneDeleted,"remote dead child retains token and ledger until host cancels transaction");
    Destroy(6151);Check(!deployments[0].used && carrierCancelled==1 && droneDeleted==1,"host cancel destroys remote death metadata exactly once");
    ResetCase();Check(SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::gun,0)==1,"prepare rejection fixture reserves before any deployment exists");
    Destroy(preparedSubToken);
    Check(carrierTransactionsCancelled==1 && !carrierPrepared && !droneDeleted,"prepare rejection drains reserved token even when there is no child or deployment row");
    auto bad=failedPlan;bad.units[0].resourceId=kSupportCarrierDroneBase+8;
    Check(!Validate(bad),"resource bit outside kind slot ordered layout is rejected");
    bad=failedPlan;bad.units[0].role=1;Check(!Validate(bad),"carrier child cannot smuggle a crew role");

    for(unsigned kind=0;kind<3;++kind) {
        ResetCase();testHost=false;carrierSlotReady=false;
        const auto token=SubmitCarrierSupportDrone(submarineParent,from,heading,target,static_cast<SupportDroneKind>(kind),5);
        Check(token==kCarrierDroneRequestQueued && !preparedSubToken && !carrierReserved && !carrierPrepared && made==2,
              "guest queues to host without local sortie token reservation or creation despite stale local availability");
        Check(carrierNetworkKind==kind && carrierNetworkSlot==5 && carrierNetworkParent[0]==0x66 && carrierNetworkParent[12]==5 &&
              !std::memcmp(carrierNetworkAnchor,target,12),"guest request carries full canonical source ID kind slot and anchor");
    }
    ResetCase();testHost=false;carrierNetworkAccepted=false;
    Check(!SubmitCarrierSupportDrone(submarineParent,from,heading,target,SupportDroneKind::gun,0) && !preparedSubToken,"failed broker send has no local fallback");
    ResetCase();unsigned char id[32]{};ReadNativeObjectId(static_cast<const unsigned char*>(submarineParent.obj),id);
    Check(HandleCarrierDroneNetworkRequest(7,"client",id,2,4,target) && preparedSubToken==1 && carrierReserved==1,"authenticated actual driver can request exact doll slot");
    Check(preparedSubPlan.units[0].matrix[12]==90 && preparedSubPlan.units[0].matrix[13]==275 && preparedSubPlan.units[0].matrix[14]==-70 &&
          CarrierDroneOrdered(preparedSubPlan.units[0].resourceId),"host derives spawn transform from actual native source and marks only accepted player target");
    const auto accepted=preparedSubToken;
    Check(!HandleCarrierDroneNetworkRequest(7,"forged",id,0,0,target) && preparedSubToken==accepted,"transport PUID must match current native driver identity");
    const auto originalRequester=hostRequester;hostRequester=Make();
    Check(!HandleCarrierDroneNetworkRequest(7,"client",id,0,0,target) && preparedSubToken==accepted,"another same-team player cannot launch this driver's child");
    hostRequester=originalRequester;id[31]=1;
    Check(!HandleCarrierDroneNetworkRequest(7,"client",id,0,0,target) && preparedSubToken==accepted,"source validation compares all 32 bytes instead of a partial ID");
    id[31]=0;carrierSlotReady=false;
    Check(!HandleCarrierDroneNetworkRequest(7,"client",id,0,0,target) && preparedSubToken==accepted,"host independently checks live slot availability for guest requests");
    carrierSlotReady=true;testHost=false;
    Check(!HandleCarrierDroneNetworkRequest(7,"client",id,0,0,target),"guest cannot act as a request authority");
    testHost=true;
    Check(!HandleCarrierDroneNetworkRequest(7,"client",id,3,1,target),"malformed recall cannot disguise a spawn or alter a slot");
    const float zero[3]{};
    Check(HandleCarrierDroneNetworkRequest(7,"client",id,carrier_drone_net::kRecall,0,zero) && carrierRecalls==1,"authenticated recall enters real host recovery once");
    Check(SubmitCarrierSupportRecall(submarineParent) && carrierRecalls==2,"host local recall enters same native recovery");
    testHost=false;
    Check(SubmitCarrierSupportRecall(submarineParent) && carrierNetworkKind==carrier_drone_net::kRecall && carrierNetworkSlot==0 &&
          carrierNetworkAnchor[0]==0 && carrierNetworkAnchor[1]==0 && carrierNetworkAnchor[2]==0 && carrierRecalls==2,
          "guest recall uses authenticated broker with canonical empty payload and no local recovery");
    std::printf("carrier support dispatcher: %d checks passed\n",checks);return 0;
}
