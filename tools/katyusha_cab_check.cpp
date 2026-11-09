// The Katyusha's firing floor over its cab (common/edf/katyusha_cab.h, test site #36) offline:
//  - CabFloor is the table at every entry's azimuth, linear between two entries (never under the lower nor over the
//    higher of them), the same a whole turn either way and at negative azimuths, and the table's highest for an azimuth
//    that is no number;
//  - the table is the model's (tools/gen_katyusha_cab.py): something is in the way over the cab, nothing behind;
//  - StepElevation (src/katyusha.cpp Launcher's frame) never leaves the launcher under the floor: a stock pose at 0 over
//    the cab is lifted onto it at once, a pose over it is eased at the step, and the turntable turning from the rear over
//    the cab at the step's elevation keeps every frame on or over the floor at that frame's azimuth.
// Exit code 1 when one fails. cmake --build build --target katyusha_cab_check && build\katyusha_cab_check.exe
#include "edf/katyusha_cab.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace edf::katyusha;

namespace {
constexpr float kRad=3.14159265f/180.0f;
int failures=0,checks=0;

void Expect(bool ok,const char* what,float a=0.0f,float b=0.0f) {
    ++checks;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}

bool Near(float a,float b) { return std::fabs(a-b)<=1e-5f; }
}  // namespace

int main() {
    Expect(kCabSteps*kCabStepDeg==360.0f,"the table goes round once",static_cast<float>(kCabSteps),kCabStepDeg);
    float top=0.0f;
    for(int k=0;k<kCabSteps;++k) {
        const float az=static_cast<float>(k)*kCabStepDeg*kRad;
        Expect(Near(CabFloor(az),kCabFloorDeg[k]*kRad),"an entry's azimuth gives the entry",static_cast<float>(k),CabFloor(az)/kRad);
        Expect(Near(CabFloor(az+2.0f*3.14159265f),CabFloor(az)) && Near(CabFloor(az-2.0f*3.14159265f),CabFloor(az)),
               "a whole turn either way is the same",static_cast<float>(k));
        Expect(kCabFloorDeg[k]>=0.0f && kCabFloorDeg[k]<80.0f,"an entry within the hinge's stops",static_cast<float>(k),kCabFloorDeg[k]);
        top=kCabFloorDeg[k]>top ? kCabFloorDeg[k] : top;
        const float lo=kCabFloorDeg[k],hi=kCabFloorDeg[(k+1)%kCabSteps];
        for(int j=1;j<10;++j) {
            const float f=CabFloor(az+static_cast<float>(j)*0.1f*kCabStepDeg*kRad)/kRad;
            Expect(f>=std::fmin(lo,hi)-1e-4f && f<=std::fmax(lo,hi)+1e-4f,"between two entries, between their floors",static_cast<float>(k),f);
        }
    }
    Expect(Near(CabFloor(std::numeric_limits<float>::quiet_NaN()),top*kRad),"no azimuth: the highest",CabFloor(std::nanf(""))/kRad,top);
    Expect(Near(CabFloor(-0.0f),kCabFloorDeg[0]*kRad) && Near(CabFloor(-1e-9f),CabFloor(1e-9f)),"just under 0 wraps to 360");
    Expect(Near(CabFloor(-10.0f*kRad),CabFloor(350.0f*kRad)),"negative azimuths wrap",CabFloor(-10.0f*kRad)/kRad,CabFloor(350.0f*kRad)/kRad);
    // The model's: the cab is over the rails ahead; nothing of the truck is behind them.
    Expect(CabFloor(0.0f)>5.0f*kRad,"over the cab the floor is up",CabFloor(0.0f)/kRad);
    Expect(CabFloor(180.0f*kRad)==0.0f,"behind, no floor",CabFloor(180.0f*kRad)/kRad);

    // The pose's frame.
    const float step=1.1f/60.0f,cab=CabFloor(0.0f);
    Expect(StepElevation(0.0f,0.0f,step,cab)==cab,"a stock pose at 0 over the cab is lifted onto the floor at once",StepElevation(0.0f,0.0f,step,cab),cab);
    Expect(Near(StepElevation(0.8f,1.2f,step,cab),0.8f+step),"over the floor it eases up at the step");
    Expect(Near(StepElevation(0.8f,0.0f,step,cab),0.8f-step),"and down at the step");
    Expect(StepElevation(cab+0.5f*step,0.0f,step,cab)==cab,"eased down onto the floor, no lower");
    Expect(StepElevation(0.1f,0.0f,step,0.0f)==0.1f-step && StepElevation(0.0f,0.0f,step,0.0f)==0.0f,"no floor: the stock pose's own");
    // The turntable swung from behind over the cab and on round, a degree a frame, the stock pose at 0 all the while.
    float elev=0.0f;
    for(int d=0;d<=540;++d) {
        const float az=(180.0f+static_cast<float>(d))*kRad,f=CabFloor(az);
        elev=StepElevation(elev,0.0f,step,f);
        Expect(elev>=f,"turning over the cab, never under the floor",static_cast<float>(d),elev/kRad);
    }
    if(failures){std::printf("katyusha_cab_check: %d of %d checks failed\n",failures,checks);return 1;}
    std::printf("katyusha_cab_check: ok (%d checks: the table round the turntable, its line between entries, the pose's frame)\n",checks);
    return 0;
}
