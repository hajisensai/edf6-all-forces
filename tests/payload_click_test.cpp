// Actual payload.cpp queue consumption, fire-control and holder redirects; no renderer or game process.
#define main ExistingPayloadSightMain
#include "payload_sight_test.cpp"
#undef main
#include <thread>
namespace {
PayloadReadout Read(VehicleFixture& c) { Tick(c);PayloadReadout r{};Check(PlayerPayload(&r),"published payload snapshot is readable");return r; }
bool Queue(const PayloadReadout& r,int entry) { return RequestPayloadSelection(r.selectionToken,r.seat,entry); }
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    SetUp();auto& c=cars[0];auto r=Read(c);
    Check(r.selectionToken!=0 && !r.entry[0].selectable && r.entry[1].selectable && r.entry[2].selectable,"Titan independent main is read-only; native secondary and real store are clickable");
    Check(!Queue(r,0) && !Queue(r,-1) && !Queue(r,3) && !RequestPayloadSelection(r.selectionToken,r.seat+1,2),"read-only, missing and wrong-seat clicks are rejected");
    const auto token=r.selectionToken;const auto picked=PayloadPicked(c.vehicle);
    bool queued=false;std::thread draw([&]{queued=Queue(r,2);});draw.join();
    Check(queued && PayloadPicked(c.vehicle)==picked && NoShots(c),"draw thread only queues numbers; selection and fire remain untouched");
    mapHeld=true;Tick(c);PayloadReadout after{};PlayerPayload(&after);
    Check(after.selectionToken==token && after.picked==2 && PayloadPicked(c.vehicle)==c.weapon[6] && Sight(c)==c.weapon[6],"map-held frame consumes click and previews selected real store");
    Check(NoShots(c) && At<float>(SeatAt(c.vehicle,0),0x2E4)==0 && At<float>(SeatAt(c.vehicle,0),0x2E0)==0,"click never creates weapon or seat fire intent");
    Check(Queue(after,1),"native secondary can be selected back by click");Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[3] && Sight(c)==c.weapon[3],"click switches back to native secondary during map hold");
    // A concurrent physical switch edge is drained while map UI owns the pointer.
    sightinput::keys['R']=true;Tick(c);mapHeld=false;Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[3],"map physical key hold does not replay a switch when closed");
    sightinput::keys['R']=false;Tick(c);sightinput::keys['R']=true;Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[6],"R cycle still works after UI choice");
    PullHook(c.holders[3]);Check(c.weapon[6][kWeaponTrigger]==1 && !c.weapon[3][kWeaponTrigger],"actual native secondary trigger follows the clicked/cycled mount");
    c.weapon[6][kWeaponTrigger]=0;sightinput::keys['R']=false;Triggers(c,0,1,0);Tick(c);
    Check(Sight(c)==c.weapon[0] && PayloadPicked(c.vehicle)==c.weapon[6],"RT still selects main sight without changing secondary mount");

    SetUp();r=Read(c);Queue(r,2);Board(c,0,1);Tick(c);PlayerPayload(&after);
    Check(after.selectionToken!=r.selectionToken && PayloadPicked(c.vehicle)==c.weapon[3],"queued click cannot cross player replacement");
    Check(!Queue(r,2),"old drawn token is rejected after replacement publication");
    SetUp();r=Read(c);Queue(r,2);Leave(c,0);Board(c,1,0);Tick(c);
    Check(!PayloadPicked(c.vehicle) && Sight(c,1)==c.weapon[1],"queued driver index cannot become a gunner-seat selection");
    SetUp();r=Read(c);Queue(r,2);Leave(c,0);Board(cars[1],0,0);Tick(cars[1]);
    Check(PayloadPicked(cars[1].vehicle)==cars[1].weapon[3],"queued click cannot cross vehicles");
    SetUp();r=Read(c);Queue(r,2);Put<void*>(c.vehicle,kSelfCtrl,cars[1].vehicleCtrl);Tick(c);PlayerPayload(&after);
    Check(after.selectionToken!=r.selectionToken && PayloadPicked(c.vehicle)==c.weapon[3],"same-address vehicle weak identity invalidates request");
    SetUp();r=Read(c);Queue(r,2);Put<void*>(people[0],kSelfCtrl,personCtrl[2]);Tick(c);PlayerPayload(&after);
    Check(after.selectionToken!=r.selectionToken && PayloadPicked(c.vehicle)==c.weapon[3],"same-address human weak identity invalidates request");

    SetUp();r=Read(c);Queue(r,2);std::swap(c.seatHolders[0][1],c.seatHolders[0][2]);Tick(c);PlayerPayload(&after);
    Check(after.selectionToken!=r.selectionToken && PayloadPicked(c.vehicle)==c.weapon[3],"topology reorder rejects old index instead of selecting different weapon");
    SetUp();r=Read(c);Queue(r,2);Put<void*>(c.holders[6],kHolderCtrl,c.weaponCtrl[7]);Tick(c);PlayerPayload(&after);
    Check(after.selectionToken!=r.selectionToken && PayloadPicked(c.vehicle)==c.weapon[3],"same weapon with replacement holder control rejects stale request");
    SetUp();r=Read(c);Queue(r,2);Put<int>(c.weaponCtrl[6],8,0);Tick(c);PlayerPayload(&after);
    Check(after.count==2 && after.selectionToken==0 && !PayloadPicked(c.vehicle),"expired mount has no clickable choice and queued click does nothing");
    SetUp();r=Read(c);Queue(r,2);Put<int>(c.weapon[6],kWeaponAmmo,0);Tick(c);PlayerPayload(&after);
    Check(!after.entry[2].selectable && PayloadPicked(c.vehicle)==c.weapon[3],"permanently spent mount is rejected during main-thread revalidation");
    SetUp();Put<int>(c.weapon[6],kWeaponAmmo,0);Put<int>(c.weapon[6],kWeaponReloadTime,120);r=Read(c);
    Check(r.entry[2].selectable && Queue(r,2),"reloadable empty mount remains a legitimate selectable choice");Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[6] && Sight(c)==c.weapon[6] && NoShots(c),"reloading click preserves selected weapon without firing");
    SetUp();r=Read(c);Queue(r,2);cfg.stockStores=false;Tick(c);PlayerPayload(&after);
    Check(after.selectionToken==0 && !PayloadPicked(c.vehicle) && !Queue(r,2),"turning feature off rejects pending and later stale clicks");
    SetUp();r=Read(c);Queue(r,2);ResetPayload();Tick(c);PlayerPayload(&after);
    Check(after.selectionToken!=r.selectionToken && !Queue(r,2) && PayloadPicked(c.vehicle)==c.weapon[3],"mission reset never reuses old clickable token");
    SetUp();r=Read(c);Queue(r,2);c.vehicle[kDead]=1;Tick(c);
    Check(!Queue(r,2) && NoShots(c),"vehicle death clears pending click publication");

    SetUp();r=Read(c);Queue(r,2);Queue(r,1);Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[3],"last valid queued click wins without accumulating input actions");
    SetUp();r=Read(c);Queue(r,2);Check(!Queue(r,0),"invalid main-gun click rejected while a valid click is queued");Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[6],"invalid request cannot overwrite valid queued choice");
    SetUp();r=Read(c);Queue(r,2);
    // Age only the transport in the fixture: no blocking sleep or alteration of game timing.
    choiceRequest.posted=GetTickCount64()-kFreshMs-1;Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[3],"delayed click expires rather than applying long after UI dismissal");
    // A single-control tank can click back to its native primary, unlike Titan's independent primary.
    SetUp();Put<void*>(c.vehicle,0,image+kVt403);c.seatHolders[0][1]=c.holders[6];Put<std::uint64_t>(SeatAt(c.vehicle,0),kSeatWeaponCount,2);
    r=Read(c);Check(r.entry[0].selectable && r.entry[1].selectable,"single-control vehicle exposes native primary and actual added store");
    Queue(r,1);Tick(c);PlayerPayload(&after);Queue(after,0);Tick(c);
    Check(PayloadPicked(c.vehicle)==c.weapon[0] && Sight(c)==c.weapon[0] && NoShots(c),"single-control vehicle can click back to real primary without a shot");
    // Explicit M tick processes UI even when no vehicle input/flight frame runs.
    SetUp();mapHeld=true;Put<int>(c.vehicleCtrl,8,1);Put<void*>(people[0],kHumanVehicleCtrl,c.vehicleCtrl);
    Put<void*>(people[0],kHumanVehicleCtrl-8,c.vehicle);PumpPayloadUi(people[0]);PlayerSelectablePayload(&r);
    Check(r.selectionToken && Queue(r,2),"map-only pump publishes a clickable stock snapshot");
    PumpPayloadUi(people[1]);Check(PlayerSelectablePayload(&after) && after.selectionToken==r.selectionToken,"unrelated human map tick cannot invalidate current player's queued click");
    PumpPayloadUi(people[0]);Check(PayloadPicked(c.vehicle)==c.weapon[6] && NoShots(c),"map-only pump consumes click without a native vehicle frame or shot");
    Put<void*>(people[0],kHumanVehicleCtrl,nullptr);PumpPayloadUi(people[0]);
    Check(!PlayerSelectablePayload(&r),"map-only dismount clears the selectable publication");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("payload click: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
