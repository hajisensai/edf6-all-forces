// The gunship's muzzle, the pure part (no game, no Windows: tools/gunship_muzzle_check.cpp includes it alone; jet_bay.cpp
// fires the gunship's shells and cannon from it). README 炮舰机的机炮; the user, 2026-10-06: 「炮舰机的机炮会打到自己身上」.
//
// The rounds used to leave from the vehicle's origin (veh+0x90). On the bomber401 model the gunship flies, that origin is
// the belly's floor (the model's lowest vertex is 0.13 m over it, its wing 1.3-2.8 m over it, 52 m across: pylib/
// jet_models.py model_box('bomber401')). The gunship circles its target banked into the turn (the NPC: about 60 degrees at
// its 2 g; the player: up to playerjet.cpp kTurnBank, 69 degrees) and looks down at the target only 22-30 degrees (350 m
// over a 600-850 m circle): in the airframe's own axes the line from the belly to the target climbs (the bank less the
// look-down) across the fuselage and the inside wing, so the rounds flew out through the plane.
//
// The muzzle now: where the line from the airframe box's centre to the aim leaves that box grown by `clear` each way (the
// whole model's box: wings, nose and tail, in the vehicle's axes). The grown box is convex, so the rest of the line to the
// aim stays outside it, whatever the attitude: no round crosses its own airframe. `clear` is the round's hit radius
// (AmmoSize x AmmoHitSizeAdjust, docs/carrier-laser-re.md §3) and kMargin: its hit sphere stays clear of the airframe all
// the way, without leaning on the bullets' owner test (docs/bullet-pass-re.md §3.2, which spares the shooter itself).
#pragma once
#include <cmath>

namespace gunmuzzle {
// A box in the vehicle's axes (x aside, y up, z forward: the rows of veh+0x60), around the vehicle's origin.
struct Airframe { float centre[3],half[3]; };
// The gunship's: the stock bomber401 model's whole box (tools/make_jets.py GUNSHIP_AIRFRAME, held to the model by its
// build and to this by tools/selftest.py gunship_muzzle_wired).
constexpr Airframe kGunship{{0.0f,2.136f,0.0f},{25.938f,2.009f,8.078f}};
constexpr float kMargin=0.5f;      // m past a round's hit sphere
constexpr float kCannonHit=1.6f;   // the cannon round's hit radius: make_jets.py CANNON_SIZE x CANNON_HIT
constexpr float kGatlingHit=0.8f;  // the gatling round's: make_jets.py GATLING_SIZE x GATLING_HIT
constexpr float kShellHit=10.0f;   // the shell's: DEMOGUNSHIPFIREE25 #7 x #8 (make_jets.py SHELL_HIT, checked on the file)

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// A world point `p` in the axes of matrix `m` (rows right, up, forward, then the position at m[12..14]), as `out`.
inline void ToLocal(const float* m,const float* p,float* out) noexcept {
    const float d[3]={p[0]-m[12],p[1]-m[13],p[2]-m[14]};
    for(int i=0;i<3;++i)out[i]=Dot(d,m+4*i);
}
// A point `p` in the axes of `m` in the world, as `out`.
inline void ToWorld(const float* m,const float* p,float* out) noexcept {
    for(int c=0;c<3;++c)out[c]=m[12+c]+p[0]*m[c]+p[1]*m[4+c]+p[2]*m[8+c];
}

// How far from the box's centre the unit direction `dir` (in the box's axes) leaves it grown by `grow` each way.
inline float ExitDistance(const Airframe& a,const float* dir,float grow) noexcept {
    float t=INFINITY;
    for(int i=0;i<3;++i) {
        const float d=std::fabs(dir[i]);
        if(d>1e-6f && (a.half[i]+grow)/d<t)t=(a.half[i]+grow)/d;
    }
    return t;
}

// The muzzle of a vehicle (matrix `m`, veh+0x60) with airframe `a` for a round at `aim` (world): where the line to the aim
// leaves the box grown by `clear`, as `out` (world). False when the aim is inside that (then `out` is the box's centre).
inline bool Muzzle(const float* m,const Airframe& a,const float* aim,float clear,float* out) noexcept {
    float centre[3];
    ToWorld(m,a.centre,centre);
    float local[3];
    ToLocal(m,aim,local);
    const float d[3]={local[0]-a.centre[0],local[1]-a.centre[1],local[2]-a.centre[2]};
    const float len=std::sqrt(Dot(d,d));
    const float dir[3]={len>0.0f ? d[0]/len : 0.0f,len>0.0f ? d[1]/len : 0.0f,len>0.0f ? d[2]/len : 0.0f};
    const float t=ExitDistance(a,dir,clear);
    if(!(len>t) || !std::isfinite(t)) {
        for(int c=0;c<3;++c)out[c]=centre[c];
        return false;
    }
    const float at[3]={a.centre[0]+dir[0]*t,a.centre[1]+dir[1]*t,a.centre[2]+dir[2]*t};
    ToWorld(m,at,out);
    return true;
}

// The length of the segment `from`-`to` (world) inside box `a` grown by `grow` m each way, on vehicle matrix `m` (the
// slab test): 0 when the segment misses it. The check's measure of a round crossing its own airframe.
inline float InsideLength(const float* m,const Airframe& a,const float* from,const float* to,float grow) noexcept {
    float p[3],q[3];
    ToLocal(m,from,p);ToLocal(m,to,q);
    float t0=0.0f,t1=1.0f;
    for(int i=0;i<3;++i) {
        const float lo=a.centre[i]-a.half[i]-grow,hi=a.centre[i]+a.half[i]+grow,d=q[i]-p[i];
        if(std::fabs(d)<1e-9f) {
            if(p[i]<lo || p[i]>hi)return 0.0f;
            continue;
        }
        float u=(lo-p[i])/d,v=(hi-p[i])/d;
        if(u>v){const float s=u;u=v;v=s;}
        if(u>t0)t0=u;
        if(v<t1)t1=v;
        if(t0>=t1)return 0.0f;
    }
    const float d[3]={q[0]-p[0],q[1]-p[1],q[2]-p[2]};
    return (t1-t0)*std::sqrt(Dot(d,d));
}

// Where a round with a spread of `spread` rad lands about the aim `at`, fired from `from`, as `out` (world): the `n`th
// point of a golden-angle (Vogel) disc of kScatterRing points across the cone's width at the aim's distance, in the plane
// square to the line. A burst covers the disc evenly with no random state (the same burst lands the same: replays and the
// check see it), its mean on the aim. `spread` 0 or no line: `at` itself.
constexpr int kScatterRing=16;                 // points in the pattern before it repeats
constexpr float kGoldenAngle=2.39996323f;      // rad: pi (3 - sqrt 5), the turn between two points
inline void Scatter(const float* from,const float* at,float spread,int n,float* out) noexcept {
    for(int c=0;c<3;++c)out[c]=at[c];
    const float d[3]={at[0]-from[0],at[1]-from[1],at[2]-from[2]};
    const float len=std::sqrt(Dot(d,d));
    if(!(spread>0.0f) || !(len>0.0f) || !std::isfinite(len))return;
    const float dir[3]={d[0]/len,d[1]/len,d[2]/len};
    const float side[3]={std::fabs(dir[1])<0.99f ? 0.0f : 1.0f,std::fabs(dir[1])<0.99f ? 1.0f : 0.0f,0.0f};   // up, or x near vertical
    float u[3]={dir[1]*side[2]-dir[2]*side[1],dir[2]*side[0]-dir[0]*side[2],dir[0]*side[1]-dir[1]*side[0]};
    const float ul=std::sqrt(Dot(u,u));
    for(int c=0;c<3;++c)u[c]/=ul;
    const float w[3]={dir[1]*u[2]-dir[2]*u[1],dir[2]*u[0]-dir[0]*u[2],dir[0]*u[1]-dir[1]*u[0]};
    const unsigned k=static_cast<unsigned>(n)%static_cast<unsigned>(kScatterRing);
    const float r=len*spread*0.5f*std::sqrt((static_cast<float>(k)+0.5f)/static_cast<float>(kScatterRing));
    const float a=kGoldenAngle*static_cast<float>(k);
    const float cs=std::cos(a)*r,sn=std::sin(a)*r;
    for(int c=0;c<3;++c)out[c]=at[c]+u[c]*cs+w[c]*sn;
}
}  // namespace gunmuzzle
