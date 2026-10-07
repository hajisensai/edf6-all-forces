// The Sazabi's flight control (src/sazabi_flight.h) flown offline over flat ground (y = 0 the soles' floor): each
// scenario scripts the stick, the ascend trigger and the boost, steps 60 times a second and checks what a Gundam action
// game's flight does (the walk and run speeds, the jump and climb, the fall, the boost dash on the ground and in the air
// holding its height, the burst turned by a second press, the gauge burned and refilled, the overheat that grounds it
// until it lands and cools). Exit 1 on a failure; --trace prints every scenario's path.
// Built by the offline checks: cmake --build build --target sazabi_flight_check && build\sazabi_flight_check.exe
#include "../src/sazabi_flight.h"
#include "../src/sazabi_flames.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace {
using namespace sazabi::flight;
constexpr float kDt=1.0f/60.0f;
int failures=0;
bool trace=false;

void Fail(const char* scenario,const char* what,float got) {
    std::printf("FAIL %s: %s (%.3f)\n",scenario,what,got);
    ++failures;
}

struct Run {
    State st;
    float pos[3]{};
    Params p;
    float t=0.0f;
    Events all{};   // any of this run's events
    // `secs` of `in` (heading 0: forward +z)
    void Fly(const char* name,const Input& in,float secs) {
        for(float e=0.0f;e<secs;e+=kDt) {
            const Events ev=Step(st,in,0.0f,pos[1],kDt,p);
            all.jumped=all.jumped || ev.jumped;all.burst=all.burst || ev.burst;
            all.landed=all.landed || ev.landed;all.overheated=all.overheated || ev.overheated;
            for(int i=0;i<3;++i)pos[i]+=st.vel[i]*kDt;
            if(!st.air && pos[1]<kFloat)pos[1]=kFloat;   // the ground under it (the tracking holds it there)
            t+=kDt;
            if(trace && std::fmod(t,0.25f)<kDt)
                std::printf("  %-10s t=%5.2f pos=(%6.1f,%6.1f,%6.1f) vel=(%5.1f,%5.1f,%5.1f) gauge %.2f%s%s%s\n",name,t,pos[0],pos[1],pos[2],
                            st.vel[0],st.vel[1],st.vel[2],st.gauge,st.air ? " AIR" : "",st.boosting ? " BOOST" : "",st.overheat ? " OVERHEAT" : "");
        }
    }
    float Ground() const { return std::sqrt(st.vel[0]*st.vel[0]+st.vel[2]*st.vel[2]); }
};
Input Stick(float f,float r) { Input i;i.forward=f;i.right=r;return i; }

void Walk() {
    Run r;
    r.Fly("walk",Stick(kWalkShare,0.0f),3.0f);
    if(std::fabs(r.Ground()-r.p.walk)>0.5f)Fail("walk","the stick at the walk's share walks at SazabiWalk",r.Ground());
    r.Fly("run",Stick(1.0f,0.0f),3.0f);
    if(std::fabs(r.Ground()-r.p.run)>0.5f)Fail("run","the full stick runs at SazabiRun",r.Ground());
    if(r.st.air)Fail("run","it stays on its feet",1.0f);
    r.Fly("stop",Input{},3.0f);
    if(r.Ground()>0.1f)Fail("stop","let go it stops",r.Ground());
}

void Strafe() {   // the stick's right is the mech's right: heading 0 faces +z, its right is -x
    Run r;
    r.Fly("strafe",Stick(0.0f,1.0f),2.0f);
    if(!(r.st.vel[0]<-r.p.run*0.9f) || std::fabs(r.st.vel[2])>0.5f)Fail("strafe","the stick right moves it to its right (-x)",r.st.vel[0]);
}

void JumpClimbFall() {
    Run r;
    Input up;up.ascend=1.0f;
    r.Fly("climb",up,2.0f);
    if(!r.all.jumped)Fail("climb","the ascend trigger jumps",0.0f);
    if(r.pos[1]<30.0f)Fail("climb","two seconds of climb lift it 30 m or more",r.pos[1]);
    if(std::fabs(r.st.vel[1]-r.p.climb)>2.0f)Fail("climb","it climbs at SazabiClimb",r.st.vel[1]);
    const float burned=1.0f-r.st.gauge;
    if(burned<2.0f/r.p.thrusterSec*0.8f || burned>2.0f/r.p.thrusterSec+0.2f)Fail("climb","two seconds' climb burn about 2/SazabiThrusterSec",burned);
    const float top=r.pos[1];
    for(int i=0;i<600 && !r.all.landed;++i)r.Fly("fall",Input{},kDt);   // until it touches down (10 s at most)
    if(!r.all.landed)Fail("fall","let go it falls and lands",top);
    if(r.st.air)Fail("fall","it is on its feet",r.pos[1]);
    const float g=r.st.gauge;
    r.Fly("regen",Input{},kCoolSec+kLandRegenSec*0.5f);
    if(!(r.st.gauge>g+0.15f))Fail("regen","on its feet the gauge comes back quickly after landing",r.st.gauge-g);
}

void GroundBoost() {
    Run r;
    Input b=Stick(1.0f,0.0f);b.boost=true;
    r.Fly("boost",b,0.05f);
    if(!r.all.burst || r.Ground()<r.p.boost*kBurst-1.0f)Fail("boost","the press bursts to most of SazabiDash at once",r.Ground());
    r.Fly("boost",b,1.0f);
    if(std::fabs(r.Ground()-r.p.boost)>1.0f)Fail("boost","held, it skates at SazabiDash",r.Ground());
    if(r.st.air)Fail("boost","a boost on its feet stays on the ground",r.pos[1]);
    if(!(r.st.gauge<1.0f-1.0f/r.p.thrusterSec*0.8f))Fail("boost","a second's boost burns the gauge",r.st.gauge);
    r.Fly("coast",Stick(1.0f,0.0f),2.0f);
    if(std::fabs(r.Ground()-r.p.run)>1.0f)Fail("coast","let go of the boost it runs again",r.Ground());
}

void AirBoost() {
    Run r;
    Input up;up.ascend=1.0f;
    r.Fly("up",up,1.5f);
    const float h=r.pos[1];
    Input b=Stick(1.0f,0.0f);b.boost=true;
    r.Fly("airdash",b,2.0f);
    if(std::fabs(r.Ground()-r.p.boost)>1.5f)Fail("airdash","a boost in the air dashes at SazabiDash",r.Ground());
    if(r.pos[1]<h-6.0f)Fail("airdash","a boost in the air holds its height (sinking 6 m at most)",h-r.pos[1]);
    if(!r.st.air)Fail("airdash","it stays in the air",r.pos[1]);
    // a second press with the stick left turns the dash at once
    Input l=Stick(0.0f,-1.0f);
    r.Fly("turn",l,0.05f);   // the boost let go
    l.boost=true;
    r.Fly("turn",l,0.1f);
    if(!(r.st.vel[0]>r.p.boost*kBurst-2.0f))Fail("turn","a fresh press turns the dash to the stick (+x: its left)",r.st.vel[0]);
}

void Overheat() {
    Run r;
    Input up;up.ascend=1.0f;
    r.Fly("burn",up,r.p.thrusterSec+1.5f);
    if(!r.all.overheated || !r.st.overheat)Fail("overheat","climbing the gauge out overheats it",r.st.gauge);
    if(r.st.climbing || r.st.vel[1]>0.0f)Fail("overheat","overheated it no longer climbs",r.st.vel[1]);
    Input b=Stick(1.0f,0.0f);b.boost=true;b.ascend=1.0f;
    r.Fly("burn",b,0.5f);
    if(r.st.boosting)Fail("overheat","overheated it cannot boost",r.Ground());
    r.Fly("down",Input{},30.0f);
    if(r.st.air)Fail("overheat","it falls and lands",r.pos[1]);
    if(r.st.overheat)Fail("overheat","landed and cooled, the overheat is over",r.st.cool);
    if(!(r.st.gauge>0.5f))Fail("overheat","the gauge comes back on its feet",r.st.gauge);
}

void LedgeAndNoGround() {
    State st;
    Params p;
    Step(st,Input{},0.0f,10.0f,kDt,p);
    if(!st.air)Fail("ledge","its soles 10 m over the ground: it is in the air (walked off)",0.0f);
    State s2;
    Step(s2,Input{},0.0f,kNoGround,kDt,p);
    if(!s2.air)Fail("ledge","nothing under it: in the air",0.0f);
}
// The thrusters' flames (src/sazabi_flames.h): out on its feet, a hover's quarter in the air, a cruise's level boosting;
// a burst full for kBurstHold then back to cruise over kBurstFall; the flicker within its bounds and smooth (no level
// jumping more than kMostStep a frame but at a burst's start); the burst nozzles out when not boosting; the waist's
// attitude jet on the side away from a fast turn.
void Flames() {
    namespace fs=sazabi::flames;
    fs::State s;
    if(fs::Level(s,0,false,0)!=0.0f)Fail("flames","on its feet, idle: out",fs::Level(s,0,false,0));
    s.air=true;
    float lo=9.0f,hi=-9.0f,prev=-1.0f,step=0.0f;
    for(int i=0;i<600;++i) {   // ten seconds hovering
        s.t=static_cast<float>(i)*kDt;
        const float l=fs::Level(s,2,false,0);
        lo=std::fmin(lo,l);hi=std::fmax(hi,l);
        if(prev>=0.0f)step=std::fmax(step,std::fabs(l-prev));
        prev=l;
    }
    if(lo<fs::kHover*(1.0f-fs::kFlicker-fs::kSwell)-1e-3f || hi>fs::kHover*(1.0f+fs::kFlicker+fs::kSwell)+1e-3f)Fail("flames","the hover's flicker within its bounds",hi-lo);
    if(hi-lo<fs::kHover*fs::kFlicker)Fail("flames","the hover flickers at all",hi-lo);
    if(step>0.05f)Fail("flames","the flicker smooth frame to frame",step);
    if(fs::Level(s,4,true,0)!=0.0f)Fail("flames","a burst nozzle out while hovering",fs::Level(s,4,true,0));
    s.boosting=true;s.sinceBurst=0.0f;
    if(fs::Base(s)!=1.0f)Fail("flames","a burst's start full",fs::Base(s));
    s.sinceBurst=fs::kBurstHold+fs::kBurstFall*0.5f;
    if(!(fs::Base(s)<1.0f && fs::Base(s)>fs::kCruise))Fail("flames","half way down from the burst",fs::Base(s));
    s.sinceBurst=fs::kBurstHold+fs::kBurstFall+0.01f;
    if(std::fabs(fs::Base(s)-fs::kCruise)>1e-4f)Fail("flames","after the burst, cruise",fs::Base(s));
    if(fs::Level(s,4,true,0)<=0.0f)Fail("flames","a burst nozzle burns boosting",fs::Level(s,4,true,0));
    s.boosting=false;s.air=false;s.sinceBurst=1e3f;s.yawRate=1.0f;   // turning left on its feet
    if(fs::Level(s,9,true,-1)<=0.0f || fs::Level(s,8,true,1)!=0.0f)Fail("flames","turning left: the right waist's attitude jet, not the left's",fs::Level(s,8,true,1));
}
}  // namespace

int main(int argc,char** argv) {
    for(int a=1;a<argc;++a)if(std::strcmp(argv[a],"--trace")==0)trace=true;
    Walk();
    Strafe();
    JumpClimbFall();
    GroundBoost();
    AirBoost();
    Overheat();
    LedgeAndNoGround();
    Flames();
    std::printf(failures ? "sazabi_flight_check: %d failures\n" : "sazabi_flight_check: all scenarios pass\n",failures);
    return failures ? 1 : 0;
}
