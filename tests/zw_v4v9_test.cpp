// The Vehicler's V4..V9 (docs/newclass-re/vehicler-v4v9-plan.md): the unlock stages and capture (src/zw_unlock_logic.h),
// V7's heat and V8's zone (src/zw_heat_logic.h), the table's new columns (src/zw_vehicles.inc).
#include "zw_heat_logic.h"
#include "zw_unlock_logic.h"
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
int KindOf(const wchar_t* row) { return crew::ZwVehicleKindOfRow(std::wstring_view(row)); }
}  // namespace

int main() {
    using namespace crew;
    // the table: nine vehicles, V1..V3 at the start, the rest by the plan
    Check(kZwVehicleCount==9 && kZwVehicleCount<=kZwVehicleMost,"nine vehicles, within the reserved items");
    const int v4=KindOf(L"ZW_V4_OBJ279"),v5=KindOf(L"ZW_V5_OBJ760"),v6=KindOf(L"ZW_V6_AH56"),v7=KindOf(L"ZW_V7_1K17"),
              v8=KindOf(L"ZW_V8_TV8"),v9=KindOf(L"ZW_V9_VZ9");
    Check(v4==3 && v5==4 && v6==5 && v7==6 && v8==7 && v9==8,"V4..V9 after V1..V3, in order");
    for(int k=0;k<3;++k)Check(kZwVehicles[k].unlock==ZwUnlock::start && !kZwVehicles[k].capture,"V1..V3 at the start");
    Check(kZwVehicles[v4].unlock==ZwUnlock::mid && kZwVehicles[v5].unlock==ZwUnlock::mid && kZwVehicles[v6].unlock==ZwUnlock::mid,
          "V4 / V5 / V6 mid");
    Check(kZwVehicles[v7].unlock==ZwUnlock::late && kZwVehicles[v8].unlock==ZwUnlock::late,"V7 / V8 late");
    Check(kZwVehicles[v9].unlock==ZwUnlock::final,"V9 final");
    Check(kZwVehicles[v4].capture,"V4 can be captured");
    Check(kZwVehicles[v7].behavior==ZwBehavior::heat && kZwVehicles[v8].behavior==ZwBehavior::fallout &&
          ZwVehicleKindOfBehavior(ZwBehavior::heat)==v7 && ZwVehicleKindOfBehavior(ZwBehavior::fallout)==v8,"behaviours");
    Check(kZwVehicles[v6].vtable==0x17DB238u && kZwVehicles[v9].vtable==0x17DB238u && kZwVehicles[v7].vtable==0x17DB9D8u,
          "V6 / V9 helicopters, V7 the Maser hull");

    // progress
    using namespace zwunlock;
    const int rows=kStoryMissions;
    Check(ProgressKinds(0,rows,false)==0,"nothing at the start");
    Check(ProgressKinds(48,rows,false)==0,"48 of 147: not a third");
    const std::uint32_t mid=(1u<<v4)|(1u<<v5)|(1u<<v6),late=(1u<<v7)|(1u<<v8);
    Check(ProgressKinds(49,rows,false)==mid,"49 of 147: the mid ones");
    Check(ProgressKinds(97,rows,false)==mid,"97: still mid");
    Check(ProgressKinds(98,rows,false)==(mid|late),"98: the late ones too");
    Check(ProgressKinds(146,rows,false)==(mid|late),"all but the last: no V9");
    Check(ProgressKinds(147,rows,true)==(mid|late|(1u<<v9)),"the last cleared: V9");
    Check(ProgressKinds(1,rows,true)==(1u<<v9),"the last alone (a save that skipped): V9 alone");
    Check(ProgressKinds(100,0,false)==0,"no rows: no stage");

    // the clear count: any class, any difficulty bit
    static std::uint8_t clear[3][16]{};
    clear[0][0]=1;clear[1][0]=4;clear[2][3]=2;clear[1][15]=1;
    Check(Cleared(&clear[0][0],3,16,16)==3,"rows cleared by any class counted once");
    Check(Cleared(&clear[0][0],3,16,4)==2,"only the first rows");
    Check(Cleared(nullptr,3,16,16)==0,"no save");

    // capture
    CaptureFacts f;f.kind=v4;f.vehicler=true;f.alive=true;f.aboard=true;
    Check(Captures(f),"a Vehicler boarding an intact 279 it does not own");
    f.aboard=false;f.distance=kCaptureReach;Check(Captures(f),"or standing beside it");
    f.distance=kCaptureReach+0.5f;Check(!Captures(f),"not from afar");
    f.aboard=true;f.owned=1u<<v4;Check(!Captures(f),"owned already");
    f.owned=0;f.alive=false;Check(!Captures(f),"a wreck is no capture");
    f.alive=true;f.vehicler=false;Check(!Captures(f),"only the Vehicler");
    f.vehicler=true;f.kind=v5;Check(!Captures(f),"V5 is not a capture kind");
    f.kind=-1;Check(!Captures(f),"not a Vehicler's vehicle");

    // heat: firing from cold overheats in kFireFrames, then the forced cool-down, then it fires again
    using namespace zwheat;
    Heat h;
    Check(Damage(h)==1.0f,"cold: x1");
    int frames=0;
    while(Step(h,true) && frames<10000)++frames;
    Check(frames>=kFireFrames-2 && frames<=kFireFrames+1 && h.overheated && h.heat==1.0f,"overheats after ~kFireFrames of firing");
    Check(Damage(h)==1.0f+kGain,"hot: x3");
    int cool=0;
    while(!Step(h,false) && cool<10000)++cool;
    Check(!h.overheated && h.heat==kResume && cool>=295 && cool<=301,"the forced cool-down takes ~5 s");
    Heat w;
    for(int i=0;i<120;++i)Step(w,true);
    const float warm=w.heat;
    for(int i=0;i<60;++i)Step(w,false);
    Check(w.heat<warm && w.heat>0.0f && !w.overheated,"idling cools, not overheated");
    for(int i=0;i<1000;++i)Step(w,false);
    Check(w.heat==0.0f,"cools to cold, not below");

    // the zone: pulses every kZonePeriod frames for kZoneFrames
    const float at[3]={1.0f,2.0f,3.0f};
    Zone z=StartZone(at);
    int pulses=0,ticks=0;
    while(z.left>0){if(Tick(z))++pulses;++ticks;}
    Check(ticks==kZoneFrames && pulses==kZoneFrames/kZonePeriod,"a pulse a period over the zone's life");
    Check(!Tick(z),"over: no more pulses");
    Check(z.pos[0]==1.0f && z.pos[2]==3.0f,"the zone stays where the tank died");

    std::printf("zw_v4v9: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
