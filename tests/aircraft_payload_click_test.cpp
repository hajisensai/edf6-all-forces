#define main ExistingPayloadSightMain
#include "payload_sight_test.cpp"
#undef main
#include <utility>
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    SetUp();auto& c=cars[0];sightBody=PluginBody::playerJet;
    const StoreSpec spec{L"AAM", "AA MISSILE",StoreRole::air,1,0},special{L"", "BOMB BAY",StoreRole::bomb,0,0};
    sightStoreSpec=&spec;c.weapon[3][0x100]=2;
    Store stores[]={{c.weapon[3],&spec,10,0,1000},{c.weapon[6],&spec,10,0,1000},{nullptr,&special,99,0,0}};
    Check(AircraftPayloadChoice(c.vehicle,stores,3,0)==-1,"publishing aircraft stores does not synthesize a request");
    PayloadReadout r{};Check(PlayerSelectablePayload(&r) && r.count==3 && r.picked==0,"unified panel reads current pilot store ordering");
    Check(r.selectionToken && r.entry[0].selectable && r.entry[1].selectable && !r.entry[2].selectable,"real aircraft weapons selectable; null-weapon special action is read-only");
    Check(!RequestPayloadSelection(r.selectionToken,0,2),"special bay/drone/gunner action cannot masquerade as weapon choice");
    Check(RequestPayloadSelection(r.selectionToken,0,1),"pilot click is queued");mapHeld=true;
    const int choice=AircraftPayloadChoice(c.vehicle,stores,3,0);
    Check(choice==1 && NoShots(c),"map-owned input consumes real store choice without any firing latch");
    PlayerSelectablePayload(&r);Check(r.picked==1 && r.entry[1].picked,"unified snapshot confirms applied index");
    Tick(c);Check(PlayerSelectablePayload(&r) && r.picked==1,"stock PayloadFrame does not erase plugin-aircraft publication");
    RequestPayloadSelection(r.selectionToken,0,0);std::swap(stores[0],stores[1]);
    Check(AircraftPayloadChoice(c.vehicle,stores,3,1)==-1,"aircraft topology reorder rejects queued old index");
    PlayerSelectablePayload(&r);RequestPayloadSelection(r.selectionToken,0,0);Put<int>(c.weapon[6],kWeaponAmmo,0);
    Check(AircraftPayloadChoice(c.vehicle,stores,3,1)==-1,"native ammo revalidation rejects stale Store ammo snapshot");
    PlayerSelectablePayload(&r);Check(!r.entry[0].selectable && r.entry[0].rounds==0,"empty real store publishes non-clickable actual ammo");
    Put<int>(c.weapon[6],kWeaponAmmo,10);AircraftPayloadChoice(c.vehicle,stores,3,1);PlayerSelectablePayload(&r);
    RequestPayloadSelection(r.selectionToken,0,0);Put<int>(c.weaponCtrl[6],8,0);
    Check(AircraftPayloadChoice(c.vehicle,stores,3,1)==-1,"expired aircraft holder rejects pending selection");
    Put<int>(c.weaponCtrl[6],8,1);AircraftPayloadChoice(c.vehicle,stores,3,1);PlayerSelectablePayload(&r);
    RequestPayloadSelection(r.selectionToken,0,0);stores[0].spec=&special;
    Check(AircraftPayloadChoice(c.vehicle,stores,3,1)==-1,"changed store spec identity invalidates queued request");
    stores[0].spec=&spec;AircraftPayloadChoice(c.vehicle,stores,3,1);PlayerSelectablePayload(&r);
    RequestPayloadSelection(r.selectionToken,0,0);Board(c,0,1);
    Check(AircraftPayloadChoice(c.vehicle,stores,3,1)==-1,"pilot replacement invalidates old aircraft click");
    Leave(c,0);Board(c,1,1);
    Check(AircraftPayloadChoice(c.vehicle,stores,3,1)==-1 && !PlayerSelectablePayload(&r),"moving to gunner seat removes pilot-store UI instead of selecting gunner specials");
    Leave(c,1);Board(c,0,1);AircraftPayloadChoice(c.vehicle,stores,3,0);PlayerSelectablePayload(&r);
    RequestPayloadSelection(r.selectionToken,0,1);cfg.playerJet=false;
    Check(AircraftPayloadChoice(c.vehicle,stores,3,0)==-1 && !PlayerSelectablePayload(&r),"disabled player aircraft invalidates publication and pending request");
    cfg.playerJet=true;AircraftPayloadChoice(c.vehicle,stores,3,0);PlayerSelectablePayload(&r);const auto token=r.selectionToken;
    ForgetAircraftPayload(c.vehicle);
    Check(!PlayerSelectablePayload(&r) && !RequestPayloadSelection(token,0,1),"forgetting aircraft clears unified publication and request token");
    Check(NoShots(c),"all aircraft clicks and rejected actions remain non-firing");
    VirtualFree(image,0,MEM_RELEASE);std::printf("aircraft payload click: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
