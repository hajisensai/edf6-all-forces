// The NPC helis' napalm, checked without the game (the user, 2026-10-06: "npc直升机的烧夷弹好像射的非常不准"). The
// store on a strafing 506's missile byte (src/heli.cpp kStoreHolder) against targets at several ranges, heights and
// speeds, from a hovering heli and from one running in, as the bullet core flies its rounds (src/roundaim.h):
//  - before: what heli.cpp did until 2026-10-06: the nose on the gun's lead (the gatling's 240 m/s straight line, no
//    drop: LeadPoint with gunGravity 0) and the store fired as if it were the homing missile, whenever the nose was
//    within kMissileCone (10 deg) of that point and the target 50..HeliRange (350) m off;
//  - after: the nose on the store's solved arc (roundaim::Solve, StoreSense), fired only while roundaim::Worth passes.
// For each it prints the miss of the cone's centre (m, at the nearest pass), whether the gate fires, and the share of a
// burst's rounds that land within kBurn m of the target with the weapon's own scatter (FireAccuracy: the polar angle
// uniform in [0, cone], the azimuth uniform), Monte Carlo. Then the gate against an attitude off the solution, and the
// Eros No. 6's drop pod released on a run. Exit code 1 when an "after" shot that fires misses by more than it allows,
// a muzzle off the solution by its tolerance's angle still fires, or a drop pod's release misses by more than the
// gate allows. Built on request only: cmake --build build --target heli_fire_check && build\heli_fire_check.exe
#include "../src/roundaim.h"
#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {
using namespace crew;
constexpr float kGravity=14.7f,kPi=3.14159265f;
constexpr float kGunSpeed=240.0f;            // the 506 gatling: 4 m/frame
constexpr float kMissileCone=10.0f,kMissileMin=50.0f,kHeliRange=350.0f;   // the old gate (heli.cpp, ini HeliRange)
constexpr float kHitRadius=3.0f,kStoreSpread=12.0f;                        // heli.cpp's
constexpr float kBurn=8.0f;                  // m: a round this near the target counts (a napalm patch's reach, a guess)
constexpr int kSamples=4000;

roundaim::Round Napalm() noexcept {   // V_506HELI_NAPALM01
    roundaim::Round r{3.0f,{0.0f,-kGravity*1.0f/3600.0f,0.0f},0.0f,360};
    return r;
}
roundaim::Round Pod() noexcept {      // V_506HELI_UNDER_NAPALM01
    roundaim::Round r{0.1f,{0.0f,-kGravity*2.0f/3600.0f,0.0f},1.0f,360};
    return r;
}
constexpr float kNapalmCone=0.2f;

// heli.cpp LeadPoint (copied): `time(d)` s of flight to d m, falling gravity x kGravity, two passes.
template<class F> void LeadPoint(const float* pos,const float* aim,const float* tv,float gravity,F time,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=aim[i];
    for(int pass=0;pass<2;++pass) {
        const float d[3]={out[0]-pos[0],out[1]-pos[1],out[2]-pos[2]};
        const float t=time(std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]));
        for(int i=0;i<3;++i)out[i]=aim[i]+tv[i]*t;
        out[1]+=0.5f*gravity*kGravity*t*t;
    }
}

// A deterministic uniform in [0, 1).
std::uint32_t seed=12345u;
float Uniform() noexcept { seed=seed*1664525u+1013904223u; return static_cast<float>(seed>>8)/16777216.0f; }

// `dir` turned off by `polar` rad toward the azimuth `az`.
void Tilt(const float* dir,float polar,float az,float* out) noexcept {
    float u[3];
    const float up[3]={0.0f,1.0f,0.0f},side[3]={1.0f,0.0f,0.0f};
    vec::Cross(dir,std::fabs(dir[1])<0.9f ? up : side,u);vec::Normalize(u);
    float w[3];vec::Cross(dir,u,w);
    const float s=std::sin(polar),c=std::cos(polar);
    for(int i=0;i<3;++i)out[i]=dir[i]*c+(u[i]*std::cos(az)+w[i]*std::sin(az))*s;
}

// The share of rounds fired along `dir` (scattered by `cone`) that pass within kBurn of the target.
float HitShare(const roundaim::Round& r,const float* from,const float* dir,const float* hv,const float* target,const float* tv,
               float cone) noexcept {
    int hits=0;
    for(int k=0;k<kSamples;++k) {
        float d[3];Tilt(dir,Uniform()*cone,Uniform()*2.0f*kPi,d);
        if(roundaim::Fire(r,from,d,hv,target,tv).miss<=kBurn)++hits;
    }
    return static_cast<float>(hits)/static_cast<float>(kSamples);
}

struct Case { const char* name; float range,height,heliSpeed,targetSpeed; bool crossing; };

int failures=0;

void Row(const Case& c) noexcept {
    // The heli at the origin, the target `range` ahead (+z) and `height` below; the heli flying +z at heliSpeed, the
    // target moving across (+x) or toward the heli (-z).
    const float pos[3]={0.0f,0.0f,0.0f},target[3]={0.0f,-c.height,c.range};
    const float hv[3]={0.0f,0.0f,c.heliSpeed};
    const float tv[3]={c.crossing ? c.targetSpeed : 0.0f,0.0f,c.crossing ? 0.0f : -c.targetSpeed};
    const roundaim::Round r=Napalm();
    const float dist=vec::Dist(pos,target);
    // Before: the nose (the muzzle) on the gun's lead.
    float lead[3];LeadPoint(pos,target,tv,0.0f,[](float d) noexcept { return d/kGunSpeed; },lead);
    float before[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};vec::Normalize(before);
    const roundaim::Pass b=roundaim::Fire(r,pos,before,hv,target,tv);
    const bool bFires=dist>kMissileMin && dist<kHeliRange;   // the nose on the lead: within kMissileCone, so it fired
    // ...and the worst the old gate let through: the nose 10 deg off it (low: the napalm falls further short).
    float worst[3];Tilt(before,kMissileCone*kPi/180.0f,kPi*0.5f,worst);
    if(worst[1]>before[1]){Tilt(before,kMissileCone*kPi/180.0f,-kPi*0.5f,worst);}
    const roundaim::Pass bw=roundaim::Fire(r,pos,worst,hv,target,tv);
    const float bShare=bFires ? HitShare(r,pos,before,hv,target,tv,kNapalmCone) : 0.0f;
    // After: the nose on the solved arc, fired when the gate passes.
    float after[3];roundaim::Pass a{};
    roundaim::Solve(r,pos,hv,target,tv,kHitRadius,after,&a);
    float tol=0.0f;
    const float range=vec::Dist(pos,a.round);
    const bool aFires=roundaim::Worth(a,range,kNapalmCone,kHitRadius,kStoreSpread,&tol);
    const float aShare=aFires ? HitShare(r,pos,after,hv,target,tv,kNapalmCone) : 0.0f;
    std::printf("%-34s before: miss %6.1f m (10deg off: %6.1f) fires=%d hit%%=%5.1f | after: miss %5.2f m fires=%d allowed %5.1f hit%%=%5.1f\n",
                c.name,b.miss,bw.miss,bFires,bShare*100.0f,a.miss,aFires,tol,aShare*100.0f);
    if(aFires && a.miss>tol){std::printf("  FAIL: fired with a miss over what it allows\n");++failures;}
    // The attitude: the muzzle off the solution by the angle of its tolerance and a half: the gate must hold.
    if(aFires) {
        const float off=std::atan(tol*1.5f/range);
        float tilted[3];Tilt(after,off,0.3f,tilted);
        const roundaim::Pass t=roundaim::Fire(r,pos,tilted,hv,target,tv);
        float tt=0.0f;
        const bool tFires=roundaim::Worth(t,vec::Dist(pos,t.round),kNapalmCone,kHitRadius,kStoreSpread,&tt);
        if(tFires){std::printf("  FAIL: the muzzle %.1f deg off the solution (miss %.1f m) still fires\n",off*180.0f/kPi,t.miss);++failures;}
    }
}

// The Eros No. 6's pod on a run: the heli `height` over the target, flying at it at `speed` m/s from 200 m out, the
// pod's FireVector straight down (a level heli). Before: released at the first frame the old gate opened (the nose on
// the target within 350 m: at once). After: the first frame roundaim::Worth passes.
void PodRun(float height,float speed,float tspeed) noexcept {
    const roundaim::Round r=Pod();
    const float down[3]={0.0f,-1.0f,0.0f},hv[3]={0.0f,0.0f,speed},tv[3]={0.0f,0.0f,-tspeed};
    float beforeMiss=-1.0f,afterMiss=-1.0f,afterAt=0.0f,afterTol=0.0f;
    for(int f=0;f<60*30;++f) {
        const float t=static_cast<float>(f)/60.0f;
        const float pos[3]={0.0f,height,-200.0f+speed*t},target[3]={0.0f,0.0f,-tspeed*t};
        const roundaim::Pass p=roundaim::Fire(r,pos,down,hv,target,tv);
        const float dist=vec::Dist(pos,target);
        if(beforeMiss<0.0f && dist>kMissileMin && dist<kHeliRange)beforeMiss=p.miss;
        float tol=0.0f;
        if(roundaim::Worth(p,vec::Dist(pos,p.round),kNapalmCone,kHitRadius,kStoreSpread,&tol)){afterMiss=p.miss;afterAt=-pos[2];afterTol=tol;break;}
    }
    std::printf("pod: %2.0f m over, run %4.1f m/s, target %4.1f m/s closer   before: miss %6.1f m | after: miss %4.2f m (allowed %.1f), released %.0f m short\n",
                height,speed,tspeed,beforeMiss,afterMiss,afterTol,afterAt);
    if(afterMiss<0.0f || afterMiss>afterTol){std::printf("  FAIL: no release within what it allows\n");++failures;}
}
}  // namespace

int main() {
    std::printf("Twin napalm gun (V_506HELI_NAPALM01: 3 m/frame, gravity x1, cone 0.2 rad); hit%% = rounds within %.0f m\n",kBurn);
    const Case cases[]={
        {"hover, 40 m, 25 below, still",40,25,0,0,false},
        {"hover, 80 m, 25 below, still",80,25,0,0,false},
        {"hover, 120 m, 25 below, still",120,25,0,0,false},
        {"hover, 160 m, 25 below, still",160,25,0,0,false},
        {"hover, 250 m, 25 below, still",250,25,0,0,false},
        {"hover, 350 m, 25 below, still",340,25,0,0,false},
        {"hover, 100 m, 60 below, still",100,60,0,0,false},
        {"hover, 100 m, level, still",100,0,0,0,false},
        {"hover, 100 m, 25 below, 10 m/s across",100,25,0,10,true},
        {"hover, 100 m, 25 below, 10 m/s closer",100,25,0,10,false},
        {"run 17 m/s, 60 m, 25 below, still",60,25,17,0,false},
        {"run 17 m/s, 100 m, 25 below, still",100,25,17,0,false},
        {"run 17 m/s, 100 m, 25 below, 8 across",100,25,17,8,true},
        {"run 25 m/s, 160 m, 25 below, 8 across",160,25,25,8,true},
    };
    for(const Case& c:cases)Row(c);
    std::printf("\nNapalm drop pod (V_506HELI_UNDER_NAPALM01: 0.1 m/frame down, gravity x2, keeps the heli's velocity)\n");
    PodRun(25.0f,17.0f,0.0f);
    PodRun(25.0f,25.0f,0.0f);
    PodRun(40.0f,17.0f,5.0f);
    std::printf("\n%s (%d failure(s))\n",failures ? "FAILED" : "ok",failures);
    return failures ? 1 : 0;
}
