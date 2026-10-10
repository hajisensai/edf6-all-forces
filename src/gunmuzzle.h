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

// The length of segment `p`-`q` (in the box's own axes) inside box `a` grown by `grow` m each way (the slab test): 0 when
// the segment misses it.
inline float SegmentInside(const Airframe& a,const float* p,const float* q,float grow) noexcept {
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

// The length of the segment `from`-`to` (world) inside box `a` grown by `grow` m each way, on vehicle matrix `m` (the
// slab test): 0 when the segment misses it. The check's measure of a round crossing its own airframe, the airframe held still.
inline float InsideLength(const float* m,const Airframe& a,const float* from,const float* to,float grow) noexcept {
    float p[3],q[3];
    ToLocal(m,from,p);ToLocal(m,to,q);
    return SegmentInside(a,p,q,grow);
}

// The round's path against the airframe as both move (the user, 2026-10-10: 「炮舰机的机炮有可能会打在自己身上，导致没打出
// 去」). The muzzle above holds a straight line from a still airframe clear of it, but the gunship flies on while its round
// flies out: the round does not take the gunship's velocity (it leaves the IFC at its own speed along the line it is
// aimed), so in the airframe's axes its path leans back by the gunship's speed over the round's (up to 145 / 480 m/s for
// the shell: 17 degrees); the round may leave a frame after it is made (its object's first step: not verified in the
// game), the airframe a frame on by then; a round that falls bends off the line. Each of these brings a start on the
// box's edge back in. Launch picks the start for the path as it is relative to the airframe, Clears follows it frame by
// frame (the airframe moved and turned on as it flies) and holds any round whose hit sphere would touch the airframe.
//
// A round (docs/carrier-laser-re.md §3: #5 speed, #6 AmmoGravityFactor; docs/hud-re.md: a frame v += g, p += v): its speed
// (m a frame), how far it falls (m a frame per frame: the gravity factor x the world's gravity / 3600) and its hit radius.
struct Round { float speed,fall,hit; };
// The gunship's three: make_jets.py CANNON_SPEED and GATLING_SPEED, no fall; the stock shell DEMOGUNSHIPFIREE25 #5 8 m a
// frame, #6 0 (a RocketBullet01 with no fall). The shell is fired "ballistic" (IFC +0x2F8 = 1), but the IFC's solve
// 0x2312A0 aims the round up by half its fall over its flight, the fall scaled by the round's gravity factor (param +0xB0):
// 0 here, the same straight line.
constexpr Round kCannon{16.0f,0.0f,kCannonHit},kGatling{17.0f,0.0f,kGatlingHit},kShell{8.0f,0.0f,kShellHit};
// The vehicle's motion, world: velocity (m a frame), spin (rad a frame).
struct Motion { float vel[3],spin[3]; };
constexpr int kLeaveLag=1;      // frames a round may leave after it is made: 0 and this both held clear
constexpr int kTrackFrames=40;  // frames of a round's path followed (past the airframe at any of the three speeds)
constexpr int kSubSteps=4;      // steps a frame

inline float Len(const float* v) noexcept { return std::sqrt(Dot(v,v)); }

// The round's velocity as it leaves `from` for `aim` (m a frame): along the line at its speed, raised by half its fall
// over its flight to the aim (0x2312A0).
inline void LaunchVelocity(const float* from,const float* aim,const Round& r,float* v) noexcept {
    const float d[3]={aim[0]-from[0],aim[1]-from[1],aim[2]-from[2]};
    const float len=Len(d);
    for(int c=0;c<3;++c)v[c]=len>0.0f ? d[c]/len*r.speed : 0.0f;
    if(r.speed>0.0f)v[1]+=0.5f*r.fall*(len/r.speed);
}

// `v` turned by `spin` (axis x angle, rad), as `out` (Rodrigues).
inline void Turn(const float* spin,const float* v,float* out) noexcept {
    const float a=Len(spin);
    if(!(a>1e-9f)){for(int c=0;c<3;++c)out[c]=v[c];return;}
    const float k[3]={spin[0]/a,spin[1]/a,spin[2]/a};
    const float cs=std::cos(a),sn=std::sin(a),kv=Dot(k,v);
    const float x[3]={k[1]*v[2]-k[2]*v[1],k[2]*v[0]-k[0]*v[2],k[0]*v[1]-k[1]*v[0]};
    for(int c=0;c<3;++c)out[c]=v[c]*cs+x[c]*sn+k[c]*kv*(1.0f-cs);
}

// The vehicle's matrix `t` frames on from `m` at motion `mo`, as `out`.
inline void PoseAt(const float* m,const Motion& mo,float t,float* out) noexcept {
    const float turn[3]={mo.spin[0]*t,mo.spin[1]*t,mo.spin[2]*t};
    for(int r=0;r<3;++r){Turn(turn,m+4*r,out+4*r);out[4*r+3]=0.0f;}
    for(int c=0;c<3;++c)out[12+c]=m[12+c]+mo.vel[c]*t;
    out[15]=1.0f;
}

// How much of the path of round `r` from `start` at `aim`, leaving `lag` frames after it was made on the vehicle at `m`
// moving at `mo`, runs inside the airframe grown by `grow`, in the airframe's own moving axes (0: none), up to the aim or
// kTrackFrames.
inline float PathInside(const float* m,const Motion& mo,const Airframe& a,const Round& r,const float* start,const float* aim,int lag,
                        float grow) noexcept {
    float v[3];
    LaunchVelocity(start,aim,r,v);
    const float d[3]={aim[0]-start[0],aim[1]-start[1],aim[2]-start[2]};
    const float flight=r.speed>0.0f ? Len(d)/r.speed : 0.0f;
    const float until=flight<static_cast<float>(kTrackFrames) ? flight : static_cast<float>(kTrackFrames);
    float pose[16],prev[3],now[3];
    PoseAt(m,mo,static_cast<float>(lag),pose);
    ToLocal(pose,start,prev);
    float inside=0.0f;
    for(int i=1;;++i) {
        float t=static_cast<float>(i)/static_cast<float>(kSubSteps);
        if(t>until)t=until;
        const float at[3]={start[0]+v[0]*t,start[1]+v[1]*t-0.5f*r.fall*t*t,start[2]+v[2]*t};
        PoseAt(m,mo,static_cast<float>(lag)+t,pose);
        ToLocal(pose,at,now);
        inside+=SegmentInside(a,prev,now,grow);
        for(int c=0;c<3;++c)prev[c]=now[c];
        if(!(t<until))break;
    }
    return inside;
}

// Whether round `r` from `start` at `aim` stays clear of the airframe of the vehicle at `m` moving at `mo`: its hit sphere
// off the airframe all along its path, whether it leaves at once or kLeaveLag frames later.
inline bool Clears(const float* m,const Motion& mo,const Airframe& a,const Round& r,const float* start,const float* aim) noexcept {
    for(int lag=0;lag<=kLeaveLag;++lag)if(PathInside(m,mo,a,r,start,aim,lag,r.hit)>0.0f)return false;
    return true;
}

// The muzzle for round `r` at `aim` from the vehicle at `m` moving at `mo`, as `out` (world): where the round's path relative
// to the airframe, from the box's centre, leaves the box grown by `clear` and by the vehicle's travel over kLeaveLag frames
// (a late round leaves from a point the airframe has moved on from). The plain Muzzle when the vehicle is still. False when
// the aim is inside that box (no muzzle; `out` is the box's centre). Clears says whether the path really stays clear.
inline bool Launch(const float* m,const Motion& mo,const Airframe& a,const Round& r,const float* aim,float clear,float* out) noexcept {
    const float grow=clear+Len(mo.vel)*static_cast<float>(kLeaveLag);
    if(!Muzzle(m,a,aim,grow,out))return false;
    for(int pass=0;pass<2;++pass) {   // the line from the start moves a little with the start: twice is a fixed point
        float v[3];
        LaunchVelocity(out,aim,r,v);
        const float rel[3]={v[0]-mo.vel[0],v[1]-mo.vel[1],v[2]-mo.vel[2]};
        float dir[3]={Dot(rel,m),Dot(rel,m+4),Dot(rel,m+8)};
        const float l=Len(dir);
        if(!(l>1e-6f))break;
        for(int i=0;i<3;++i)dir[i]/=l;
        const float t=ExitDistance(a,dir,grow);
        if(!std::isfinite(t))break;
        const float at[3]={a.centre[0]+dir[0]*t,a.centre[1]+dir[1]*t,a.centre[2]+dir[2]*t};
        ToWorld(m,at,out);
    }
    return true;
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
