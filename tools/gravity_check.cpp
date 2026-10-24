// The gravity operations' pure part (src/gravity_logic.h) checked without the game, and the damage of representative throws
// printed (the user, 2026-10-20: 「根据速度和质量造成伤害」):
//  - the carry: its speed cap falls with the mass and is 0 past kMostLiftKg; a thing at its goal moves with the goal, one far
//    from it at the cap, toward it;
//  - the throw: lighter faster, heavier slower, its energy growing with the mass (a car hits harder than an ant);
//  - the impact: a free fall at the world's gravity is no impact, a stop from speed is one of that speed, a character's
//    landing is one of the fall it landed with, two blows inside kImpactGapS count once, a thing at rest ends its flight;
//  - the estimates: an insect's sphere of r 1.5 m comes to about 2 t.
// Exit code 1 when one fails. Built on request only:
// cmake --build build --target gravity_check && build\gravity_check.exe
#include "../src/gravity_logic.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {
int failures=0;
void Expect(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%.4f, %.4f)\n",what,a,b);
}
bool Near(double a,double b,double tol) { return std::fabs(a-b)<=tol; }

void CarryChecks() {
    using namespace gravmath;
    Expect(HoldCap(500.0f)==kHoldMostSpeed && HoldCap(kHoldRefKg)==kHoldMostSpeed,"light things carried at full speed");
    float last=HoldCap(kHoldRefKg);
    for(float kg=3000.0f;kg<=kMostLiftKg;kg*=1.5f) {
        const float cap=HoldCap(kg);
        Expect(cap<=last && cap>=kHoldLeastSpeed,"the carry cap falls with the mass, never under its floor",kg,cap);
        last=cap;
    }
    Expect(HoldCap(kMostLiftKg*1.01f)==0.0f && HoldCap(0.0f)==0.0f,"too heavy (or massless): not carried");
    const float at[3]={0,0,0},goalVel[3]={3,0,0};
    float v[3];
    Expect(HoldVelocity(at,at,goalVel,1000.0f,v) && Near(v[0],3.0,1e-4) && Near(v[1],0.0,1e-4),"at its goal it keeps pace with the goal");
    const float far[3]={0,0,100};
    Expect(HoldVelocity(at,far,nullptr,1000.0f,v) && Near(Len(v),kHoldMostSpeed,1e-3) && v[2]>0.0f,"far from it: at the cap, toward it",Len(v));
    const float close[3]={0,0,1};
    Expect(HoldVelocity(at,close,nullptr,1000.0f,v) && Near(v[2],1.0/kHoldTau,1e-3),"near it: the spring's speed",v[2]);
    Expect(!HoldVelocity(at,far,nullptr,1.0e6f,v) && v[0]==0.0f && v[1]==0.0f && v[2]==0.0f,"a kaiju is not carried");
}

void ThrowChecks() {
    using namespace gravmath;
    const float base=40.0f;
    Expect(Near(ThrowSpeed(kThrowRefKg,base),base,1e-3),"the reference mass leaves at the base speed");
    float lastV=1e9f,lastE=0.0f;
    for(float kg=300.0f;kg<=kMostLiftKg;kg*=2.0f) {
        const float v=ThrowSpeed(kg,base);
        const float e=0.5f*kg*v*v;
        Expect(v<=lastV,"heavier flies slower",kg,v);
        Expect(e>lastE,"heavier carries more energy",kg,e);
        Expect(v>=kThrowLeast && v<=kThrowMost && v<=2.0f*base,"the throw's speed within its bounds",kg,v);
        lastV=v;lastE=e;
    }
    Expect(ThrowSpeed(0.0f,base)==0.0f && ThrowSpeed(1000.0f,0.0f)==0.0f,"no mass or no base: no throw");
}

void FlightChecks() {
    using namespace gravmath;
    const float dt=1.0f/60.0f;
    bool done=false;
    {   // a free fall at the world's gravity (and the insects' doubled one) is never an impact
        for(float g:{kWorldGravity,2.0f*kWorldGravity}) {
            Flight f;
            float v[3]={10,20,0};
            float worst=0.0f;
            for(int i=0;i<200;++i){worst=std::fmax(worst,f.Step(v,false,dt,&done));v[1]-=g*dt;}
            Expect(worst==0.0f,"a free fall is no impact",g,worst);
        }
    }
    {   // a stop from 30 m/s into a wall: an impact of about 30
        Flight f;
        const float fly[3]={30,0,0},stop[3]={0,0,0};
        f.Step(fly,false,dt,&done);
        const float dv=f.Step(stop,false,dt,&done);
        Expect(Near(dv,30.0-2.0*kWorldGravity*dt,1e-3),"a stop is an impact of its speed",dv);
        const float again=f.Step(fly,false,dt,&done);
        const float twice=f.Step(stop,false,dt,&done);
        Expect(again==0.0f && twice==0.0f,"a second blow inside the gap counts once",again,twice);
    }
    {   // a character landing at -25 m/s whose controller reports a small speed already: the fall it landed with
        Flight f;
        const float fall[3]={0,-25,0},landed[3]={0,-24.6f,0};
        f.Step(fall,false,dt,&done);
        const float dv=f.Step(landed,true,dt,&done);
        Expect(Near(dv,25.0,1e-3),"a landing is an impact of its fall",dv);
    }
    {   // at rest: the flight ends; a long step starts the readings over (no false impact across a pause)
        Flight f;
        const float fly[3]={30,0,0},rest[3]={0.5f,0,0};
        f.Step(fly,false,dt,&done);
        const float paused=f.Step(rest,false,0.5f,&done);
        Expect(paused==0.0f && !done,"a pause is no impact",paused);
        int frames=0;
        do { f.Step(rest,false,dt,&done); ++frames; } while(!done && frames<1000);
        Expect(done && Near(frames*dt,kRestS,2*dt),"at rest the flight ends",frames*dt);
    }
    Expect(Near(ClingKg(1.5f),2120.6,1.0),"a giant ant's sphere weighs about 2 t",ClingKg(1.5f));
}

void PrintCases() {
    using namespace gravmath;
    struct Case { const char* what; float kg; float dv; };
    const float base=40.0f,scale=20.0f;
    const Case cases[]={
        {"giant ant (r 1.5 m) thrown, into the ground",ClingKg(1.5f),ThrowSpeed(ClingKg(1.5f),base)},
        {"colonist (1.5 t) thrown, into a wall",FamilyKg(Family::humanoid,0.0f),ThrowSpeed(1500.0f,base)},
        {"EDF soldier (100 kg) thrown",FamilyKg(Family::human,0.0f),ThrowSpeed(100.0f,base)},
        {"car (1.5 t) thrown",1500.0f,ThrowSpeed(1500.0f,base)},
        {"Grape (17 t) thrown",17000.0f,ThrowSpeed(17000.0f,base)},
        {"Titan (227.5 t): past the lift",227500.0f,ThrowSpeed(227500.0f,base)},
        {"giant ant dropped from 30 m",ClingKg(1.5f),std::sqrt(2.0f*kWorldGravity*30.0f)},
    };
    std::printf("\nthrows at GravityThrowSpeed %.0f m/s, GravityDamage %.0f (damage = 1/2 m dv^2 / 287 kJ x %.0f):\n",base,scale,scale);
    for(const Case& c:cases) {
        const bool lifted=HoldCap(c.kg)>0.0f;
        std::printf("  %-44s %8.0f kg  %5.1f m/s  %8.1f damage%s\n",c.what,c.kg,c.dv,lifted ? ImpactDamage(c.kg,c.dv,scale) : 0.0f,
                    lifted ? "" : "  (not lifted)");
    }
}
}  // namespace

int main() {
    CarryChecks();
    ThrowChecks();
    FlightChecks();
    PrintCases();
    if(failures)std::printf("\n%d check(s) failed\n",failures);
    else std::printf("\nall gravity checks passed\n");
    return failures ? 1 : 0;
}
