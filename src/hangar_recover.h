// A Dreadnought's hangar bringing a fighter home (dreadnought_hangar.inc BayFrame, BayJob::recover; FlyTo): where it
// makes for, how fast, whether its bay holds it, and its velocity and attitude this frame. Pure math (no EDF.dll):
// tests/hangar_recover_test.cpp runs this very code, and tools/playerjet_recovery_check.py's negative controls put the
// replaced laws back to see that test fail.
//
// Test hub #42 (「飞机现在降落会平移过来...凭空出现速度的平移...除非某些有引力捕获机制控制速度这种合理行为」): until
// 2026-11-21 the hangar moved a fighter in any direction from the moment it took it (its whole velocity closed on the
// wanted one at kRecoverAccel, the nose turned after it at 1.2/s): one called home at 160 m/s braked and slid sideways onto
// its new line. Now:
//  - its capture is the bay's own: a tube kCaptureRange round its final line (from its mouth out to its approach point,
//    kApproachOut outboard and kApproachUp over it), measured from the fighter to that line, and only of a fighter no
//    faster than kCaptureSpeed over the ship (one through it faster flies on as a wing and comes round). Held, the bay
//    turns it nose outboard and backs it in at kFinalSpeed, its velocity changed in any direction at kRecoverAccel;
//  - outside it the fighter flies there as the wing it is (wing_turn.h): its speed along its path, its path turned at
//    kRecoverTurn, its nose on its path, banked into its turn. It makes for the nearest point of that final line at what
//    stops it there (sqrt(kRecoverAccel d)): it is slow enough for its bay (kCaptureSpeed, 15 m/s) about 10 m off it.
//    That is the most it ever moves across its nose, held.
// (A first try, 16afc19a, measured its capture to the mouth, at kApproachOut x 1.5 = 225 m: its approach point is 151 m
// out of the mouth, so the whole of its last stretch was inside it, and the slide there was the old one.)
#pragma once
#include "wing_turn.h"
#include <cmath>

namespace crew {
namespace recover {
// The ship (dreadnought.cpp kLength, kBeam, kKeel: static_assert there) and the distances round its hull as its
// fighters' size goes (set on a 200 m ship, its fighters 7-12 m: kSize 1).
constexpr float kShipLength=500.0f,kShipBeam=0.2f*kShipLength,kShipKeel=55.25f;
constexpr float kSize=kShipLength/200.0f;
constexpr float kApproachOut=60.0f*kSize,kApproachUp=8.0f*kSize;   // m outboard of its mouth and over it: its final line's end
constexpr float kApproachSpeed=70.0f,kFinalSpeed=12.0f;  // m/s over the ship's: closing, backing in on its final line
constexpr float kFarSpeed=160.0f,kFarOff=500.0f;   // ...and from farther out than kFarOff (escorts 1.5-2 km out took 30 s)
constexpr float kRecoverAccel=22.0f;         // m/s^2 its speed changes at most (along its path; in the bay's hold, any way)
constexpr float kRecoverTurn=30.0f;          // m/s^2 (~3 g) across its path flying home
constexpr float kRecoverRate=1.2f;           // rad/s its path turns at most (slow: under its nose's 1.5 rad/s, kHoldMostJet)
constexpr float kGravity=9.8f;               // m/s^2: the bank of that turn (its lift against gravity)
constexpr float kCaptureRange=6.0f*kSize;    // m from its final line within which its bay holds it (15 m)
constexpr float kClearTop=kShipKeel+15.0f;   // m over the ship's centre a fighter crosses its hull at
constexpr float kInboard=10.0f;              // m outboard of its mouth, less: over the hull (or the far side)
// The fastest over the ship its bay takes it at, and so the most it ever moves across its nose in the hold: a little
// over its backing in. Flying for its final line at sqrt(kRecoverAccel d), it slows to that 10 m off it, inside the tube.
constexpr float kCaptureSpeed=15.0f;
static_assert(kCaptureSpeed*kCaptureSpeed/kRecoverAccel<kCaptureRange,"a wing closing on its final line is slow enough inside the tube");
static_assert(kFinalSpeed<kCaptureSpeed,"backing in is slower than entering");

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// The velocity closes on `want` at `accel` m/s^2 at most, as a whole (it turns and slows together): the bay's hold.
inline void Close(float* vel,const float* want,float accel,float dt) noexcept {
    const float d[3]={want[0]-vel[0],want[1]-vel[1],want[2]-vel[2]};
    const float len=wing::Len3(d),most=accel*dt;
    const float k=len>most && len>0.0f ? most/len : 1.0f;
    for(int i=0;i<3;++i)vel[i]+=d[i]*k;
}

// The point of segment a..b nearest `p` into `at`; its distance.
inline float Nearest(const float* p,const float* a,const float* b,float* at) noexcept {
    const float ab[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]},ap[3]={p[0]-a[0],p[1]-a[1],p[2]-a[2]};
    const float l2=Dot(ab,ab);
    float t=l2>0.0f ? Dot(ap,ab)/l2 : 0.0f;
    t=t<0.0f ? 0.0f : t>1.0f ? 1.0f : t;
    for(int i=0;i<3;++i)at[i]=a[i]+ab[i]*t;
    const float d[3]={p[0]-at[0],p[1]-at[1],p[2]-at[2]};
    return wing::Len3(d);
}

// Where a fighter at `p` moving at `vel` makes for: its bay's `mouth`, `outboard` (unit), the ship's `up` (unit), its
// centre `centre`, its velocity `ship`.
struct Plan {
    float aim[3];    // the point it makes for
    float most;      // m/s over the ship's, at most, it closes on it at
    bool capture;    // in its bay's hold (backing in, nose outboard); else flying as a wing
};
inline Plan Planned(const float* p,const float* vel,const float* centre,const float* ship,const float* mouth,const float* outboard,
                    const float* up) noexcept {
    Plan plan{};
    float end[3];
    for(int i=0;i<3;++i)end[i]=mouth[i]+outboard[i]*kApproachOut+up[i]*kApproachUp;
    float at[3];
    const float off=Nearest(p,mouth,end,at);
    const float to[3]={mouth[0]-p[0],mouth[1]-p[1],mouth[2]-p[2]};
    const float rel[3]={vel[0]-ship[0],vel[1]-ship[1],vel[2]-ship[2]};
    if(off<kCaptureRange && Dot(rel,rel)<=kCaptureSpeed*kCaptureSpeed) {   // in its bay's hold: backing straight in
        for(int i=0;i<3;++i)plan.aim[i]=mouth[i];
        plan.most=kFinalSpeed;plan.capture=true;
        return plan;
    }
    const float rise=Dot(to,up);
    const float flat[3]={to[0]-up[0]*rise,to[1]-up[1]*rise,to[2]-up[2]*rise};   // across the ship's up
    const float out=-Dot(flat,outboard);   // how far outboard of its mouth
    if(out<kInboard) {   // inboard of its mouth (over the hull, or the far side): up, then over to its side
        const float high=(p[0]-centre[0])*up[0]+(p[1]-centre[1])*up[1]+(p[2]-centre[2])*up[2];
        if(high<kClearTop-3.0f)for(int i=0;i<3;++i)plan.aim[i]=p[i]+up[i]*(kClearTop-high);
        else for(int i=0;i<3;++i)plan.aim[i]=centre[i]+up[i]*kClearTop+outboard[i]*(kShipBeam+kApproachOut);
    } else for(int i=0;i<3;++i)plan.aim[i]=at[i];   // outboard: onto its final line, the nearest of it
    plan.most=wing::Len3(to)>kFarOff ? kFarSpeed : kApproachSpeed;
    return plan;
}

// Its frame: from `vel` (world) at `p`, the ship moving at `ship`. `nose`: its body's nose now (kept by a wing at rest).
// Out: its velocity; flying as a wing, the nose (unit, along its path) and up (unit: its lift, the turn's pull plus
// gravity held, across the nose) it is turned to (in the bay's hold the caller turns it nose outboard); its distance to
// its aim.
struct Flight { float vel[3],nose[3],up[3]; float dist; };
inline Flight Fly(const float* p,const float* vel,const float* ship,const Plan& plan,const float* nose,float dt) noexcept {
    Flight out{};
    const float d[3]={plan.aim[0]-p[0],plan.aim[1]-p[1],plan.aim[2]-p[2]};
    out.dist=wing::Len3(d);
    const float closing=std::fmin(plan.most,std::sqrt(kRecoverAccel*out.dist));
    float want[3];
    for(int i=0;i<3;++i)want[i]=out.dist>1e-3f ? d[i]/out.dist*closing : 0.0f;   // over the ship's
    float rel[3]={vel[0]-ship[0],vel[1]-ship[1],vel[2]-ship[2]};
    float lateral[3]={0.0f,0.0f,0.0f};
    if(plan.capture)Close(rel,want,kRecoverAccel,dt);
    else wing::Step(rel,want,kRecoverAccel,kRecoverTurn,dt,lateral,kRecoverRate);
    for(int i=0;i<3;++i)out.vel[i]=ship[i]+rel[i];
    float n[3]={out.vel[0],out.vel[1],out.vel[2]};
    float l=wing::Len3(n);
    if(l<1e-3f){n[0]=nose[0];n[1]=nose[1];n[2]=nose[2];l=wing::Len3(n);}
    if(l<1e-6f){n[0]=0.0f;n[1]=0.0f;n[2]=1.0f;l=1.0f;}
    for(float& c:n)c/=l;
    float u[3]={lateral[0],lateral[1]+kGravity,lateral[2]};
    const float along=Dot(u,n);
    for(int i=0;i<3;++i)u[i]-=n[i]*along;
    l=wing::Len3(u);
    if(l<1e-4f){u[0]=0.0f;u[1]=1.0f;u[2]=0.0f;l=1.0f;}
    for(int i=0;i<3;++i){out.nose[i]=n[i];out.up[i]=u[i]/l;}
    return out;
}
}  // namespace recover
}  // namespace crew
