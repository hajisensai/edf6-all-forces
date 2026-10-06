// The rotor craft's acceleration budget (src/hover_lift.h, jet_flight.cpp Hover) flown offline, Hover's own velocity
// integration (vel += acc dt, 60 frames a second) with the carrier's numbers (jet_internal.h kCarrierLean: respond
// 2.5 s, jerk 1.2 m/s^3; its Kind: thrust 4 m/s^2; playerjet_board.inc HoverClimb: 24 m/s, the ascend key's climb):
//  - the NPCs' one budget (lift 0) is bit for bit the code Hover had (OldAccel, copied from it) on random inputs;
//  - the case from the user's log (2026-10-06 14:57:59): flying 45 m/s forward and sinking 8.3 m/s, the player brakes
//    (want 0 forward) and holds ascend: the climb rate before (one budget) and after (PlayerLift of the carrier's
//    120 t) over 4 s. After, it climbs within a second and reaches kWantClimb by 3 s; before, it still sinks at 1 s;
//  - the lift: the carrier's ~10.6 m/s^2, the heli's own at the heli's mass, capped there for lighter craft, 0 off.
// Exit code 1 when one fails. Built on request only:
// cmake --build build --target hover_lift_check && build\hover_lift_check.exe
#include "../src/hover_lift.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

namespace {
using namespace crew;
int failures=0;

void Expect(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%.4f, %.4f)\n",what,a,b);
}

float Len(const float* a) { return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }
// jet_flight.cpp Hover's acceleration before hover_lift.h, verbatim.
void OldAccel(const float* wantV,const float* vel,float respond,float thrust,float jerk,const float* prev,float dt,float* acc) {
    for(int i=0;i<3;++i)acc[i]=(wantV[i]-vel[i])/respond;
    const float a=Len(acc),most=thrust;
    if(a>most)for(int i=0;i<3;++i)acc[i]*=most/a;
    if(jerk>0.0f) {
        float change[3]={acc[0]-prev[0],acc[1]-prev[1],acc[2]-prev[2]};
        const float c=Len(change),step=jerk*dt;
        if(c>step)for(int i=0;i<3;++i)change[i]*=step/c;
        for(int i=0;i<3;++i)acc[i]=prev[i]+change[i];
    }
}

constexpr float kDt=1.0f/60.0f,kRespond=2.5f,kJerk=1.2f,kThrust=4.0f,kClimbKey=24.0f,kCarrierMass=120000.0f;
constexpr float kWantClimb=10.0f;   // m/s: the climb the player should have 3 s into braking with ascend held

// The climb rate (m/s) at each whole second of `seconds`, braking from 45 m/s forward and 8.3 m/s down, ascend held.
void Fly(float lift,int seconds,float* climb) {
    float vel[3]={0.0f,-8.3f,45.0f},acc[3]={0.0f,0.0f,0.0f};
    const float want[3]={0.0f,kClimbKey,0.0f};
    for(int f=1;f<=seconds*60;++f) {
        float next[3];
        hover::Accel(want,vel,kRespond,hover::Budget{kThrust,lift,kJerk},acc,kDt,next);
        std::memcpy(acc,next,12);
        for(int i=0;i<3;++i)vel[i]+=acc[i]*kDt;
        if(f%60==0)climb[f/60-1]=vel[1];
    }
}
}  // namespace

int main() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-60.0f,60.0f),r(0.05f,3.0f),t(0.5f,40.0f),j(0.0f,5.0f);
    int same=0;
    for(int n=0;n<200000;++n) {
        const float want[3]={u(rng),u(rng),u(rng)},vel[3]={u(rng),u(rng),u(rng)},prev[3]={u(rng)*0.1f,u(rng)*0.1f,u(rng)*0.1f};
        const float respond=r(rng),thrust=t(rng),jerk=n%3 ? j(rng) : 0.0f;
        float a[3],b[3];
        OldAccel(want,vel,respond,thrust,jerk,prev,kDt,a);
        hover::Accel(want,vel,respond,hover::Budget{thrust,0.0f,jerk},prev,kDt,b);
        same+=std::memcmp(a,b,12)==0;
    }
    std::printf("the NPCs' one budget: %d / 200000 frames bit for bit the old Hover\n",same);
    Expect(same==200000,"lift 0 is the old Hover",same,200000);

    const float lift=hover::PlayerLift(kCarrierMass,1.0f);
    std::printf("lift: carrier %.2f m/s^2, heli %.2f, drone (2.2 t) %.2f, carrier x0 %.2f\n",lift,hover::PlayerLift(hover::kHeliMass,1.0f),
                hover::PlayerLift(2200.0f,1.0f),hover::PlayerLift(kCarrierMass,0.0f));
    Expect(std::fabs(lift-24.2f*std::cbrt(10000.0f/120000.0f))<1e-3f,"the carrier's lift",lift);
    Expect(std::fabs(hover::PlayerLift(2200.0f,1.0f)-hover::kHeliLift)<1e-4f,"a light craft has the heli's lift, no more");
    Expect(hover::PlayerLift(kCarrierMass,0.0f)==0.0f,"PlayerRotorLift 0: the one budget");

    float before[4],after[4];
    Fly(0.0f,4,before);
    Fly(lift,4,after);
    std::printf("braking from 45 m/s, sinking 8.3 m/s, ascend held: climb rate (m/s) at 1 / 2 / 3 / 4 s\n");
    std::printf("  before (one 4 m/s^2 budget): %6.2f %6.2f %6.2f %6.2f\n",before[0],before[1],before[2],before[3]);
    std::printf("  after  (lift %.1f m/s^2):     %6.2f %6.2f %6.2f %6.2f\n",lift,after[0],after[1],after[2],after[3]);
    Expect(before[0]<0.0f,"before: still sinking 1 s in",before[0]);
    Expect(after[0]>0.0f,"after: climbing 1 s in",after[0]);
    Expect(after[2]>=kWantClimb,"after: a real climb 3 s in",after[2],kWantClimb);
    Expect(after[3]<=kClimbKey+1e-3f,"never past the climb asked",after[3],kClimbKey);
    std::printf(failures ? "%d failed\n" : "hover_lift_check: all passed\n",failures);
    return failures ? 1 : 0;
}
