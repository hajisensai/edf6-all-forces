#include "sight_contacts.h"
#include <cstdio>
#include <limits>

int main() {
    using namespace crew::vehiclecontacts;
    int failed=0,checks=0;
    const auto check=[&](bool value,const char* what){++checks;if(!value){++failed;std::printf("FAIL: %s\n",what);}};
    check(OpticalClass("505_Tank") && OpticalClass("601_Tank") && OpticalClass("603_Flak") && OpticalClass("Helicopter409"),
          "tank, AA and attack helicopter optics are offered");
    check(!OpticalClass(nullptr) && !OpticalClass("503_Bike") && !OpticalClass("507_Rescuetank"),
          "unarmed transport and bikes do not acquire invented sensors");
    check(ThermalClass("505_Tank") && ThermalClass("603_Flak") && ThermalClass("Helicopter409") &&
          !ThermalClass("Car") && !ThermalClass("Helicopter410") && !ThermalClass("HelicopterBase"),
          "thermal capability is narrower than merely having optical sights");
    Scan scan{};scan.look[2]=1.0f;
    int objects[kCandidates+8]{};
    const float behind[3]={0,0,-100},side[3]={100,0,0},faraway[3]={0,0,kRange+1};
    scan.Add(&objects[0],behind);scan.Add(&objects[1],side);scan.Add(&objects[2],faraway);
    const float nan[3]={0,std::numeric_limits<float>::quiet_NaN(),100};scan.Add(&objects[3],nan);
    check(scan.count==0,"behind, off-cone, out-of-range and nonfinite lock points are discarded");
    const float offset[3]={20,0,100},centre[3]={0,0,100};
    scan.Add(&objects[0],offset);scan.Add(&objects[0],centre);scan.Add(&objects[0],offset);
    check(scan.count==1 && scan.point[0].at[0]==0,"one object with multiple lock points keeps its most central point");
    for(int i=1;i<kCandidates+8;++i) {
        const float point[3]={float(i),0,100};scan.Add(&objects[i],point);
    }
    check(scan.count==kCandidates,"candidate storage and the native LOS budget remain bounded");
    check(scan.point[0].key==&objects[0] && scan.point[kCandidates-1].key==&objects[kCandidates-1],
          "view-centre contacts are retained when registry exceeds the budget");
    check(Visible(100,-1) && Visible(100,100) && !Visible(100,99) && !Visible(100,0),
          "map geometry blocks optical recognition, including a wall immediately before the lock point");
    check(!Visible(100,std::numeric_limits<float>::quiet_NaN()),"unreadable/nonfinite visibility does not create a contact");
    std::printf("vehicle sensor: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
