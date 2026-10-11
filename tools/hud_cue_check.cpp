// The HUD's switch cues (src/hud_cue.h) checked offline: a switch of the picked store or of EDF6AutoTurret's aim mode
// shows its banner for the given time and then not, the first value seen (boarding) shows none, a value back again
// shows anew; every plugin store and every stock round class gets the picture of its kind.
//   cmake --build build --target hud_cue_check && build\hud_cue_check.exe      (exit code 1 on a failure)
#include "../src/hud_cue.h"
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}
}  // namespace

int main() {
    using namespace crew::hudcue;
    constexpr unsigned long long kMs=1500;
    Change c{};
    Check(!Changed(c,0,1000,kMs),"the first value seen (boarding): no banner");
    Check(!Changed(c,0,1100,kMs),"no switch: no banner");
    Check(Changed(c,1,2000,kMs),"a switch: the banner");
    Check(Changed(c,1,3499,kMs),"the banner kMs long");
    Check(!Changed(c,1,3500,kMs),"then gone");
    Check(Changed(c,0,4000,kMs),"switched back: the banner again");
    Check(Changed(c,1,4100,kMs) && Changed(c,1,5599,kMs) && !Changed(c,1,5600,kMs),"a switch during a banner starts it anew");
    Change unseen{};
    unseen.seen=false;
    Check(!Changed(unseen,3,10000,kMs),"forgotten (out of the aircraft) and seen again: no banner");

    // The plugin's stores (stores.inc names) by name; a name not known by its role.
    Check(StoreIconOf("AIM-9X",0)==StoreIcon::aamShort,"AIM-9X");
    Check(StoreIconOf("AIM-120",0)==StoreIcon::aam,"AIM-120");
    Check(StoreIconOf("AIM-54",0)==StoreIcon::aamLong,"AIM-54");
    Check(StoreIconOf("AGM-65",1)==StoreIcon::agm,"AGM-65");
    Check(StoreIconOf("AGM-114",1)==StoreIcon::agmLight,"AGM-114");
    Check(StoreIconOf("Mk 82",2)==StoreIcon::bomb,"Mk 82");
    Check(StoreIconOf("Hydra 70",3)==StoreIcon::rocket,"Hydra 70");
    Check(StoreIconOf("CANNON",2)==StoreIcon::gun && StoreIconOf("GATLING",2)==StoreIcon::gun,"the gunship's side guns: rounds");
    Check(StoreIconOf("NEW",1)==StoreIcon::agm && StoreIconOf("NEW",2)==StoreIcon::bomb && StoreIconOf("NEW",3)==StoreIcon::rocket &&
          StoreIconOf(nullptr,0)==StoreIcon::aam,"by role");
    Check(StoreIconOf("DRONES",5)==StoreIcon::drone,"drone deployment gets aircraft icon, not bomb");
    Check(StoreIconOf("SHELLS",4)==StoreIcon::gun && StoreIconOf("CHARGE",6)==StoreIcon::charge,"special gun and detonation roles retain their semantics");
    Check(ArmIconOf(1,false,1)==StoreIcon::energy && ArmIconOf(1,false,2)==StoreIcon::energy && ArmIconOf(1,false,3)==StoreIcon::energy,
          "laser, beam and maser are not drawn as machine-gun ammunition");
    // The stock rounds (vhud.h RoundKind: none 0, arc 1, rocket 2, homing 3).
    Check(ArmIconOf(1,false)==StoreIcon::gun && ArmIconOf(0,false)==StoreIcon::gun,"a gun's rounds");
    Check(ArmIconOf(2,false)==StoreIcon::rocket,"rockets");
    Check(ArmIconOf(3,false)==StoreIcon::aam,"a homing missile");
    Check(ArmIconOf(1,true)==StoreIcon::bomb,"a lobbed round");

    // The turret aim overlay follows the fire-control pick when the turret is laid by it (vhud.h sight / turret).
    Check(TurretOverlayOwned(0,0),"the stock main gun picked: the overlay");
    Check(TurretOverlayOwned(2,2),"a store on the main gun's holder picked (APFSDS / HE): the overlay, not only arm 0");
    Check(!TurretOverlayOwned(3,0),"a pick the turret does not turn with (the Titan's hull gatling): none");
    Check(!TurretOverlayOwned(-1,-1) && !TurretOverlayOwned(0,-1),"no pick, or no turret gun: none");

    std::printf("hud_cue_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
