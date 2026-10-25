// A vehicle gun's fire-control system (src/firecontrol.h) checked offline: each plugin gun's file maps to the system the
// real weapon had, stock weapons to the future war's computer (radar on an anti-aircraft vehicle), and each system's kit
// grows from the one under it (a computer has what a gyro sight has, a radar what a computer has).
//   cmake --build build --target firecontrol_check && build\firecontrol_check.exe      (exit code 1 on a failure)
#include "../src/firecontrol.h"
#include <cstdio>
#include <cwchar>

namespace {
using namespace crew::fcs;
int failures=0,cases=0;
void Check(bool ok,const char* what) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}
System Of(const wchar_t* name,bool antiAir=false) { return crew::fcs::Of(name,std::wcslen(name),antiAir); }
bool Includes(const Kit& more,const Kit& less) {
    return (more.rangefinder || !less.rangefinder) && (more.lead || !less.lead) && (more.solution || !less.solution) &&
           (more.leadLine || !less.leadLine) && more.pickCone>=less.pickCone;
}
}  // namespace

int main() {
    Check(Of(L"EDF6VC_FLAK88_L.SGO")==System::optical && Of(L"EDF6VC_STURMTIGER_R.SGO")==System::optical,"WWII guns: a telescope");
    Check(Of(L"EDF6VC_FLAK88_L.SGO",true)==System::optical,"the 88's direct-fire telescope even on an anti-aircraft class");
    Check(Of(L"EDF6VC_VULCAN_HE_2000.SGO",true)==System::gyro && Of(L"EDF6VC_QVULCAN_AP_R_1000.SGO",true)==System::gyro &&
          Of(L"EDF6VC_QUADVULCAN_L.SGO",true)==System::gyro,"the M163 and its quad upgrade: the gyro lead-computing sight");
    Check(Of(L"EDF6VC_TUNGUSKA_L.SGO")==System::radar && Of(L"EDF6VC_GDF005_R.SGO")==System::radar &&
          Of(L"EDF6VC_CIWS_PHALANX_L.SGO")==System::radar,"radar fire control: the Tunguska, the Skyguard, the Phalanx");
    Check(Of(L"EDF6VC_BMPT_AP_150.SGO")==System::computer && Of(L"edf6vc_bmpt_l.sgo")==System::computer,
          "the BMPT's computer, the file's case aside");
    Check(Of(L"V505_TANK_CANNON01.SGO")==System::computer && Of(L"V603_FLAK_GUNH01_DLC_L.SGO",true)==System::radar,
          "stock guns: the future war's computer, radar on the flak");
    Check(crew::fcs::Of(nullptr,0,false)==System::computer,"no file read: the computer");
    Check(crew::fcs::Of(L"EDF6VC_FLAK8",12,false)==System::computer,"a name shorter than a prefix matches nothing");
    const Kit o=KitOf(System::optical),g=KitOf(System::gyro),c=KitOf(System::computer),r=KitOf(System::radar);
    Check(!o.rangefinder && !o.lead && !o.solution && !o.leadLine,"a telescope: no rangefinder figure, no computed lead");
    Check(g.rangefinder && g.lead && !g.solution,"a gyro sight: range and lead, no solution data");
    Check(c.solution && !c.leadLine && r.leadLine && r.pickCone>c.pickCone,"a radar: the lead line and a wider acquisition");
    Check(Includes(g,o) && Includes(c,g) && Includes(r,c),"each system has all of the one under it");
    std::printf(failures ? "firecontrol_check: %d of %d FAILED\n" : "firecontrol_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
