// Real Crew payload group resolver and its exported, sized C ABI; no game process.
#include <Windows.h>
#define now aimApiNow
#define main ExistingPayloadSightMain
#include "payload_sight_test.cpp"
#undef main
#undef now
#include "../src/turretaim.cpp"
namespace crew {
bool TurretCamTurret(const void*,unsigned) noexcept{return false;}
bool FirstPersonVehicleView(const void*,unsigned) noexcept{return false;}
bool CameraRay(float*,float*) noexcept{return false;}
bool CameraRayOf(const void*,float*,float*) noexcept{return false;}
bool MapOwnsView() noexcept{return false;}
float MapRay(const float*,const float*,float*) noexcept{return -1;}
bool SightZoomCanMount(const void*,unsigned) noexcept{return false;}
bool SightZoomMounted(const void*) noexcept{return false;}
bool HighCamOn(const void*) noexcept{return false;}
bool TurretCamHighTransition(const void*) noexcept{return false;}
}
namespace {
const unsigned char* Ask(VehicleFixture& c,const void* source,bool* handled=nullptr) {
    edf::aimlink::PayloadAimV1 reply{sizeof(edf::aimlink::PayloadAimV1),nullptr};
    const bool yes=EDF6VehicleCrew_PayloadAimV1(c.vehicle,0,source,&reply);
    if(handled)*handled=yes;
    return static_cast<const unsigned char*>(reply.weapon);
}
void Pair(VehicleFixture& c) {
    Flak(c);c.seatHolders[0][3]=c.holders[2];c.weapon[6][0x101]=6;c.weapon[2][0x100]=2;c.weapon[2][0x101]=7;
}
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    SetUp();auto& c=cars[0];Tick(c);sightinput::keys['R']=true;Tick(c);
    bool handled=false;
    Check(Ask(c,c.weapon[0],&handled)==c.weapon[0] && handled,"independent turret control does not follow the chosen hull-gun payload");
    Check(Ask(c,c.weapon[3])==c.weapon[6],"native secondary source resolves its selected real store");
    edf::aimlink::PayloadAimV1 bad{0,c.weapon[0]};
    Check(!EDF6VehicleCrew_PayloadAimV1(c.vehicle,0,c.weapon[0],&bad) && bad.weapon==c.weapon[0],"wrong ABI size is rejected without touching an unknown layout");
    Check(!EDF6VehicleCrew_PayloadAimV1(c.vehicle,0,c.weapon[0],nullptr),"null ABI output is rejected");
    Ask(c,c.weapon[2],&handled);Check(!handled,"a weapon outside the requested seat never grants payload ownership");

    SetUp();Pair(c);Tick(c);sightinput::keys['R']=true;Tick(c);
    Check(Ask(c,c.weapon[0],&handled)==c.weapon[6] && handled,"AA source resolves the selected unmarked linked loadout");
    Check(Ask(c,c.weapon[6])==c.weapon[6],"a live selected store passed by V4 resolves to the same firing group");
    Put<int>(c.weapon[6],kWeaponAmmo,0);
    Check(Ask(c,c.weapon[0])==c.weapon[2],"spent group leader resolves the still-loaded right barrel without another frame");
    Put<int>(c.weaponCtrl[2],8,0);
    Check(!Ask(c,c.weapon[0],&handled) && handled,"expired linked partner is authoritative empty, never native fallback");
    Put<int>(c.weaponCtrl[2],8,1);Put<int>(c.weapon[2],kWeaponAmmo,0);
    Check(!Ask(c,c.weapon[0],&handled) && handled,"all linked barrels spent remains an authoritative empty group");
    cfg.stockStores=false;
    Ask(c,c.weapon[0],&handled);Check(!handled,"disabling optional stores releases management to native source firing");
    cfg.stockStores=true;aimApiNow+=201;
    Ask(c,c.weapon[0],&handled);Check(!handled,"expired firing redirect releases management in the same bound as PullHook");

    SetUp();Pair(c);Tick(c);sightinput::keys['R']=true;Tick(c);
    Put<void*>(c.vehicle,kSelfCtrl,cars[1].vehicleCtrl);
    Ask(c,c.weapon[0],&handled);Check(!handled,"reused vehicle weak identity cannot inherit the previous object's redirect");
    Put<int>(cars[1].vehicleCtrl,8,0);Ask(c,c.weapon[0],&handled);
    Check(!handled,"dead vehicle control cannot answer an actual-weapon query");
    SetUp();Pair(c);Tick(c);sightinput::keys['R']=true;Tick(c);Leave(c,0);localHuman=people[1];
    Ask(c,c.weapon[0],&handled);Check(!handled,"an empty or non-authoritative seat does not claim the optional contract");

    SetUp();Flak(c);c.weapon[0][0x100]=c.weapon[1][0x100]=2;c.weapon[0][0x101]=8;c.weapon[1][0x101]=9;
    Put<std::uint64_t>(SeatAt(c.vehicle,0),kSeatWeaponCount,2);Tick(c);cfg.stockStores=false;
    Check(Ask(c,c.weapon[0],&handled)==c.weapon[0] && handled,"M163 native ammunition remains managed when optional stores are disabled");
    Check(NativePayloadControl(c.vehicle,0),"the real NPC caller gate discovers nativeControl from live StoreSpec, independent of optional stores");
    sightinput::keys['R']=true;Tick(c);
    Check(Ask(c,c.weapon[0],&handled)==c.weapon[1] && handled,"M163 disabled-optional-store query follows the mutually exclusive AP belt");

    // Auto's input can run before this frame's Crew selection. Use a still-live preceding update, not GameFrame equality.
    SetUp();Pair(c);people[0][edf::kHumanPlayer]=0;localHuman=nullptr;aimApiNpc=true;
    PayloadReadout r{};unsigned char* ws[kMostPayload]{};r.count=ReadSeat(c.vehicle,0,ClassOf(c.vehicle),ws,r,false);
    auto* pick=PickFor(c.vehicle,0,aimApiNow);pick->npc=true;pick->weapon=c.weapon[6];pick->seen=aimApiNow;
    const int sources[]={0,1};RedirectPayload(c.vehicle,0,*pick,ws,r,sources,2,2);
    aimApiNow+=16;
    Check(Ask(c,c.weapon[0],&handled)==c.weapon[6] && handled,"Auto before this frame's NPC planning uses the valid preceding selection");
    Put<int>(c.weapon[6],kWeaponAmmo,0);
    Check(Ask(c,c.weapon[0])==c.weapon[2],"NPC adapter also resolves the surviving right barrel");
    pick->weapon=nullptr;pick->redirects=0;pick->seen=aimApiNow;
    Check(!Ask(c,c.weapon[0],&handled) && handled,"fresh NPC target loss cannot resurrect native aiming");
    aimApiNpc=false;Ask(c,c.weapon[0],&handled);Check(!handled,"NPC authority loss does not claim the optional query");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("payload aim API: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
