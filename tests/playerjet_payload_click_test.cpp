// Execute playerjet.cpp's real Stores and UI-only pump; token validation is exercised separately in aircraft_payload_click_test.
#define main ExistingRecoveryMain
#include "playerjet_recovery_test.cpp"
#undef main
int main() {
    using namespace crew;int bridgeChecks=0,failed=0;
    auto check=[&](bool yes,const char* msg){++bridgeChecks;if(!yes){++failed;std::printf("FAIL %s\n",msg);}};
    payloadScenario=true;recoveryConfig.enabled=recoveryConfig.playerJet=true;
    unsigned char vehicle[0x3000]{},weapons[2][0x1800]{},seats[edf::kSeatStride*2]{},human[0x400]{},ctrl[16]{};
    Put<void*>(vehicle,kSelfCtrl,ctrl);Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);
    Put<void*>(SeatAt(vehicle,0),kSeatRider,human);Put<void*>(SeatAt(vehicle,0),kSeatRiderCtrl,ctrl);Put<int>(ctrl,8,1);
    human[edf::kHumanPlayer]=1;Put<void*>(human,edf::kHumanPad,human);payloadHuman=human;
    const StoreSpec aa{L"AAM","AA",StoreRole::air,1,0},ag{L"AGM","AG",StoreRole::ground,1,0};
    payloadStores[0]={weapons[0],&aa,4,0,1000};payloadStores[1]={weapons[1],&ag,8,0,1000};payloadCount=2;
    auto& j=jets[0];j=PJet{};j.ref=ObjRef::Of(vehicle);j.vehicle=vehicle;j.kind=&kKinds[0];j.driven=true;
    Stick stick{};float pos[3]{};
    payloadChoice=1;vehicle[kFireStore]=1;Stores(j,vehicle,stick,pos);
    check(j.store==1 && !vehicle[kFireStore] && payloadTriggers==0,"accepted UI choice updates actual j.store without launching even if an old fire byte remains");
    payloadChoice=-1;vehicle[kFireStore]=1;Stores(j,vehicle,stick,pos);
    check(payloadTriggers==1 && !vehicle[kFireStore],"later normal secondary fire still works once on current real store");
    payloadMap=true;vehicle[kFireStore]=1;Stores(j,vehicle,stick,pos);
    check(payloadTriggers==1 && !vehicle[kFireStore],"map-held frame clears secondary fire instead of treating UI click as launch");
    payloadChoice=0;flyOk=true;const int before=payloadTriggers;vehicle[kFireStore]=1;
    PumpAircraftPayloadUi(vehicle);
    check(j.store==0 && j.stores==2 && j.storeRounds[0]==4,"UI-only aircraft pump updates persistent pilot selection and snapshot");
    check(payloadTriggers==before && !vehicle[kFireStore],"UI-only pump clears stale map-held fire without executing a flight/fire consumer");
    Put<unsigned char>(SeatAt(vehicle,0),kSeatPad,1);Put<std::uint16_t>(SeatAt(vehicle,0),kSeatButtons,kButtonLB);
    payloadChoice=-1;PumpAircraftPayloadUi(vehicle);payloadMap=false;stick.switchStore=true;
    const int kept=j.store;Stores(j,vehicle,stick,pos);
    check(j.store==kept,"LB held through map-only pump does not cycle on map close");
    j.driven=false;PumpAircraftPayloadUi(vehicle);check(payloadForgets>0,"no active pilot invalidates aircraft UI publication");
    j.driven=true;Put<void*>(SeatAt(vehicle,0),kSeatRiderCtrl,nullptr);const int forgotten=payloadForgets;
    PumpAircraftPayloadUi(vehicle);check(payloadForgets==forgotten+1,"empty pilot seat rejects map-only selection");
    Put<void*>(SeatAt(vehicle,0),kSeatRiderCtrl,ctrl);payloadChoice=-1;payloadCount=0;Stores(j,vehicle,stick,pos);
    check(payloadForgets==forgotten+2,"aircraft with no actual stores clears selectable panel");
    std::printf("playerjet payload bridge: %d checks, %d failures\n",bridgeChecks,failed);return failed?1:0;
}
