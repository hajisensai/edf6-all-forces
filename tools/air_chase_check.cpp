// When a charge drone goes off at its target (src/air_chase.h), offline: its run at a target (it goes off on it, held off
// it within its charge's reach, and keeps after one it does not close on farther out); and the 2026-10-10 log's two cases
// flown with a stand-in doll drone: a flyer climbing away above it (caught now it climbs as fast as it flies at a flyer,
// jet.cpp Rotor), and a target it is held 16 m off.
//   cmake --build build --target air_chase_check && buildir_chase_check.exe      (exit code 1 on a failure)
#include "../src/air_chase.h"
#include <cmath>
#include <cstdio>

using namespace crew::airchase;

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}

int a=0,b=0;   // stand-in target objects
void Run() {
    const float trigger=6.0f,held=24.0f;
    Closing c{};
    Check(Step(c,&a,200.0f,trigger,held,false,0)==Verdict::chase,"far: chases");
    Check(Step(c,&a,5.0f,trigger,held,false,100)==Verdict::detonate,"within the trigger: goes off");
    // Closing steadily from 200 m at 1 m a frame-step of 100 ms: never held.
    c=Closing{};
    bool ok=true;
    for(int i=0;i<190;++i)ok=ok && Step(c,&a,200.0f-static_cast<float>(i),trigger,held,false,static_cast<std::uint64_t>(i)*100)==Verdict::chase;
    Check(ok,"closing in: chases on");
    // Held 16 m off (the log's 50EF5B70: 13:28:28-13:29:37): goes off within kHeldMs.
    c=Closing{};
    std::uint64_t ms=0;
    Verdict v=Verdict::chase;
    while(ms<60000 && (v=Step(c,&a,16.0f+0.3f*std::sin(static_cast<float>(ms)*0.01f),trigger,held,false,ms))==Verdict::chase)ms+=16;
    Check(v==Verdict::detonate && ms>=kHeldMs && ms<=kHeldMs+100,"held 16 m off: goes off",static_cast<double>(ms));
    // walled (Sense) within held: at once.
    c=Closing{};
    Check(Step(c,&a,20.0f,trigger,held,true,0)==Verdict::detonate,"walled within its reach: goes off");
    // Outrun 28-110 m off (the log's 27D50C9EF10): it keeps after it, never giving it up (the user 2026-10-10: the drones
    // are to attack the flyers).
    c=Closing{};ms=0;v=Verdict::chase;
    while(ms<60000 && (v=Step(c,&a,28.0f+40.0f*(1.0f+std::sin(static_cast<float>(ms)*0.0005f)),trigger,held,false,ms))==Verdict::chase)
        ms+=16;
    Check(v==Verdict::chase && ms>=60000,"outrun: keeps after it",static_cast<double>(ms));
    // A new target starts a new run.
    Check(Step(c,&b,300.0f,trigger,held,false,ms+16)==Verdict::chase && c.target==&b && c.best==300.0f,"a new target: a new run");
}

// A stand-in doll drone (Hover's wantV[1]: half the gap to its goal a second, at most its climb) after a flyer that climbs
// 13 m/s from 30 m over it, the 2026-10-10 log's chase. Its goal the target's lock point (before the fix) at cruise/2 =
// 12.5 m/s: it never closes in, the two climb together to the 1200 m ceiling; at its cruise it still stays 2 x 13 = 26 m
// under (outside the held reach). Led by 2 s (jet.cpp Rotor kChargeLead) at its cruise (25 m/s, at a flyer) it catches it
// well under the ceiling and goes off on it (Step).
float Chase(float climb,float lead,float* top) {
    const float trigger=6.0f,held=24.0f;
    float drone=150.0f,target=180.0f;
    Closing c{};
    for(int f=0;f<60*90;++f) {
        target+=13.0f/60.0f;
        if(target>1200.0f)break;
        const float vy=std::fmax(-climb,std::fmin(climb,(target+13.0f*lead-drone)*0.5f));
        drone+=vy/60.0f;
        if(Step(c,&a,std::fabs(target-drone),trigger,held,false,static_cast<std::uint64_t>(f)*16)==Verdict::detonate){*top=drone;return drone;}
    }
    *top=drone;
    return -1.0f;
}
void Climb() {
    float top=0.0f;
    Check(Chase(12.5f,0.0f,&top)<0.0f,"at half its cruise, unled: never catches the climbing flyer",top);
    Check(Chase(25.0f,0.0f,&top)<0.0f,"at its cruise, unled: stays 26 m under it",top);
    const float caught=Chase(25.0f,2.0f,&top);
    Check(caught>0.0f && caught<400.0f,"at its cruise, led 2 s: catches it and goes off",caught);
}
}  // namespace

int main() {
    Run();
    Climb();
    std::printf("%d/%d cases passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
