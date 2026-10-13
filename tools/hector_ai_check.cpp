// EDF4.1 Hector's arm gate and back-off cycle (src/hector_ai.h) checked offline: which arms start an attack by range
// (none / one / both, and the 4.1 random choice when the two arms differ), the cycle's counter (counts only inside
// `near`, twice as fast inside the stop radius), the back-off's length and what follows it, and how the walk is steered.
//   cmake --build build --target hector_ai_check && build\hector_ai_check.exe      (exit code 1 on a failure)
#include "../src/hector_ai.h"
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
hector::Config Cannon301Launcher() {
    hector::Config c;
    c.range[0]=2000.0f; c.range[1]=150.0f;      // 301 CL: cannon on arms_l, launcher on arms_r
    return c;
}
}  // namespace

int main() {
    using namespace hector;
    Rng rng;
    rng.Seed(12345);
    // --- the pick
    {
        Config c=Cannon301Launcher();
        Check(PickArms(c,rng,2500.0f)==0u,"beyond every range: nothing fires");
        Check(PickArms(c,rng,2000.0f)==0u,"range is exclusive (4.1: range > distance)");
        for(int i=0;i<200;++i)Check(PickArms(c,rng,1000.0f)==kArmLeft,"only the cannon reaches 1000");
        int both=0,left=0,right=0;
        for(int i=0;i<4000;++i) {
            const unsigned m=PickArms(c,rng,100.0f);
            both+=m==3u; left+=m==kArmLeft; right+=m==kArmRight;
        }
        Check(both+left+right==4000 && both>1700 && both<2300,"different arms in range: about half both",both);
        Check(left>800 && left<1200 && right>800 && right<1200,"...a quarter each alone",left,right);
        c.sameWeapon=true;
        for(int i=0;i<200;++i)Check(PickArms(c,rng,100.0f)==3u,"the same weapon on both arms: both always");
        Config s;
        s.range[0]=0.0f; s.range[1]=2000.0f;    // a shield (range 0) never fires
        for(int i=0;i<200;++i)Check(PickArms(s,rng,500.0f)==kArmRight,"a shield arm is never picked");
    }
    // --- the cycle
    {
        Config c;
        Cycle y=Begin(c,rng);
        Check(y.counter>=720.0f && y.counter<2160.0f,"first counter 720..2160",y.counter);
        Check(y.stopRadius>=20.0f && y.stopRadius<=80.0f,"stop radius 20..80",y.stopRadius);
        const float before=y.counter;
        Check(Step(y,c,rng,400.0f)==Event::None && y.counter==before,"outside near: the counter stands still");
        const float r=y.stopRadius;
        Step(y,c,rng,r+10.0f > c.closeIn-1.0f ? c.closeIn-1.0f : r+10.0f);
        Check(y.counter==before-1.0f,"inside near, outside the stop radius: 1 a frame",y.counter,before);
        Step(y,c,rng,r-1.0f);
        Check(y.counter==before-3.0f,"inside the stop radius: 2 a frame",y.counter,before);
        // run it down
        y.counter=3.0f;
        Check(Step(y,c,rng,r-1.0f)==Event::None && y.counter==1.0f,"3 -> 1");
        Check(Step(y,c,rng,r-1.0f)==Event::BackoffStart && y.phase==Phase::Retreat,"1 -> back-off");
        Check(y.retreatLeft>=540 && y.retreatLeft<1260,"back-off 540..1260 frames",y.retreatLeft);
        const int len=y.retreatLeft;
        int frames=1;
        Event ev=Event::None;
        while(true) {
            ev=Step(y,c,rng,500.0f);       // distance does not matter while backing off
            if(ev!=Event::None)break;
            ++frames;
        }
        Check(ev==Event::BackoffEnd && frames==len,"the back-off lasts its rolled length",frames,len);
        Check(y.phase==Phase::Approach && y.counter>=720.0f,"then a new cycle",y.counter);
    }
    // --- the walk
    {
        Config c;
        Cycle y;
        y.stopRadius=40.0f;
        Walk w=Steer(y,c,300.0f);
        Check(!w.takeover,"far: the game's own walk");
        w=Steer(y,c,100.0f);
        Check(w.takeover && !w.retreat && w.go,"inside near, outside the stop radius: straight at the target");
        w=Steer(y,c,39.0f);
        Check(w.takeover && !w.retreat && !w.go,"inside the stop radius: stand");
        y.phase=Phase::Retreat;
        w=Steer(y,c,10.0f);
        Check(w.takeover && w.retreat && w.go,"back-off: away from the target");
        w=Steer(y,c,900.0f);
        Check(w.takeover && w.retreat && w.go,"back-off at any distance");
    }
    std::printf("hector_ai_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
