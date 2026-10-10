// What the charge drones and the gunship go for, and when a charge drone gives up (src/air_chase.h), offline: the
// weapons' limits, the drone's goal under its ceiling, its run at a target (it goes off on it, held off it, and gives up
// one it does not close on), the shun list; and the 2026-10-10 log's two cases flown with a stand-in doll drone (climb
// 12.5 m/s at the most, as jet.cpp Rotor): a flyer climbing away above it, and a target it is held 16 m off.
//   cmake --build build --target air_chase_check && build\air_chase_check.exe      (exit code 1 on a failure)
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

void Limit() {
    const Limits any{false,0.0f},gunship{true,0.0f},charge{false,kChargeCeiling};
    Check(Allowed(any,true,900.0f,true,0.0f),"guns: a flyer 900 m up");
    Check(!Allowed(gunship,true,60.0f,true,0.0f),"gunship: never a flyer");
    Check(Allowed(gunship,false,4.0f,true,0.0f),"gunship: a target on the ground");
    Check(Allowed(charge,true,150.0f,true,0.0f),"charge: a flyer 150 m over the ground");
    Check(Allowed(charge,true,kChargeCeiling,true,0.0f),"charge: at its ceiling");
    Check(!Allowed(charge,true,483.0f,true,0.0f),"charge: the log's flyer at 483 m");
    Check(Allowed(charge,true,350.0f,true,200.0f),"charge: 150 m over a 200 m hill");
    Check(Allowed(charge,true,900.0f,false,0.0f),"charge: no ground known, no ceiling told");
    Check(UnderCeiling(985.0f,0.0f,kChargeCeiling)==kChargeCeiling,"goal held at the ceiling",UnderCeiling(985.0f,0.0f,kChargeCeiling));
    Check(UnderCeiling(80.0f,0.0f,kChargeCeiling)==80.0f,"goal under it kept");
    Check(UnderCeiling(300.0f,150.0f,kChargeCeiling)==300.0f,"goal over a hill kept");
}

int a=0,b=0;   // stand-in target objects
void Run() {
    const float trigger=6.0f,held=24.0f;
    Closing c{};
    Check(Step(c,&a,200.0f,trigger,held,false,0)==Verdict::chase,"far: chases");
    Check(Step(c,&a,5.0f,trigger,held,false,100)==Verdict::detonate,"within the trigger: goes off");
    // Closing steadily from 200 m at 1 m a frame-step of 100 ms: never held, never given up.
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
    // Outrun 28-110 m off (the log's 27D50C9EF10): gives up after kGiveUpMs without a new nearest.
    c=Closing{};ms=0;v=Verdict::chase;
    while(ms<60000 && (v=Step(c,&a,28.0f+40.0f*(1.0f+std::sin(static_cast<float>(ms)*0.0005f)),trigger,held,false,ms))==Verdict::chase)
        ms+=16;
    // Its nearest (28 m) at 9.4 s, then off again: given up kGiveUpMs after that.
    Check(v==Verdict::giveUp && ms-c.closerAt>=kGiveUpMs && ms-c.closerAt<kGiveUpMs+100 && c.best<29.0f,"outrun: gives up",
          static_cast<double>(ms),static_cast<double>(c.closerAt));
    // A new target starts a new run.
    Check(Step(c,&b,300.0f,trigger,held,false,ms+16)==Verdict::chase && c.target==&b && c.best==300.0f,"a new target: a new run");
}

void Shuns() {
    ShunList s{};
    Check(!Shunned(s,&a,0),"none shunned");
    Shun(s,&a,1000);
    Check(Shunned(s,&a,999) && !Shunned(s,&a,1000) && !Shunned(s,&b,0),"shunned until then");
    int objects[kShuns+1]{};
    for(int i=0;i<kShuns;++i)Shun(s,&objects[i],2000+static_cast<std::uint64_t>(i));
    Check(!Shunned(s,&a,500),"the full list drops the one ending first");
    Shun(s,&objects[kShuns],5000);
    Check(Shunned(s,&objects[kShuns],100) && !Shunned(s,&objects[0],100) && Shunned(s,&objects[1],100),"then the next ending first");
    Shun(s,&objects[2],9000);
    Check(Shunned(s,&objects[2],8000) && Shunned(s,&objects[1],100),"shunned again: its own entry");
}

// A stand-in doll drone (jet.cpp Rotor: its goal the target's lock point, climb at most cruise/2 = 12.5 m/s, as Hover's
// wantV[1]) after a flyer that climbs 13 m/s, always some 30 m over the drone: before the fix it followed it to the 1200 m
// ceiling. With its limits it lets the target go once it is over kChargeCeiling, and its goal never passes it.
void Climb() {
    const Limits doll{false,kChargeCeiling};
    float drone=150.0f,target=180.0f,highest=0.0f;
    bool taken=true;
    for(int f=0;f<60*90 && taken;++f) {
        target+=13.0f/60.0f;
        taken=Allowed(doll,true,target,true,0.0f);
        if(!taken)break;
        const float goal=UnderCeiling(target,0.0f,kChargeCeiling);
        const float vy=std::fmax(-12.5f,std::fmin(12.5f,(goal-drone)*0.5f));
        drone+=vy/60.0f;
        if(drone>highest)highest=drone;
    }
    Check(!taken,"the climbing flyer let go");
    Check(highest<=kChargeCeiling+1.0f,"the drone stays under its ceiling",highest);
}
}  // namespace

int main() {
    Limit();
    Run();
    Shuns();
    Climb();
    std::printf("%d/%d cases passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
