// The player's turret aim math (designate.cpp), kept apart from the game so it can be checked off it
// (tools/turret_lead_check.cpp): the angle a target sits off the view, the order a lock press picks in, and the lead
// solve behind the lead circle. No EDF.dll in here; edf::BallisticArc is common/weapon.cpp's pure solve.
#pragma once
#include <cmath>
#include "edf/weapon.h"

namespace autoturret {
namespace aim {
inline float Dot3(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// The angle (rad) between the unit view `dir` from `eye` and the point `p`, and its distance; pi when p is at the eye.
inline float OffView(const float* eye,const float* dir,const float* p,float* distance) noexcept {
    const float d[3]={p[0]-eye[0],p[1]-eye[1],p[2]-eye[2]};
    const float l=std::sqrt(Dot3(d,d));
    *distance=l;
    if(!(l>1e-3f))return 3.14159265f;
    const float c=Dot3(d,dir)/l;
    return std::acos(c<-1.0f ? -1.0f : c>1.0f ? 1.0f : c);
}

// A lock press over the targets in sight, sorted nearest the view first: the nearest, or with one already locked (at
// `current` in that order) the next one out, round to the nearest after the last. -1 with none in sight.
inline int NextPick(int count,int current) noexcept {
    if(count<=0)return -1;
    return current<0 ? 0 : (current+1)%count;
}

// The lead solution from a gun's `muzzle` (world) for a round leaving at `speed` m/frame and falling `drop` m/frame^2
// along the vehicle's down (its rows `m`: right m[0..2], up m[4..6], forward m[8..10]), on the low arc, against a target
// at `target` moving `vel` m/frame: `aim` the point the round meets it (the target that many frames on), `dir` the unit
// world direction the gun's line must take for that (the arc's elevation over the line to `aim`), `frames` the flight.
// The flight time is taken to the lead point itself, pass after pass, until the point moves under kLeadSettle (each pass
// shrinks the error by about the target's speed over the round's: a 200 m/s jet against the flak's 480 m/s, 0.4).
// False out of reach.
constexpr int kLeadPasses=8;
constexpr float kLeadSettle=0.01f;   // m
inline bool LeadSolve(const float* m,const float* muzzle,const float* target,const float* vel,float speed,float drop,float* aim,
                      float* dir,float* frames) noexcept {
    float a[3]={target[0],target[1],target[2]};
    float elevation=0.0f,n=0.0f,local[3]={};
    for(int pass=0;;++pass) {
        const float d[3]={a[0]-muzzle[0],a[1]-muzzle[1],a[2]-muzzle[2]};
        local[0]=Dot3(d,m);local[1]=Dot3(d,m+4);local[2]=Dot3(d,m+8);
        if(!edf::BallisticArc(std::sqrt(local[0]*local[0]+local[2]*local[2]),local[1],speed,drop,false,elevation,n))return false;
        if(pass==kLeadPasses)break;
        float moved=0.0f;
        for(int i=0;i<3;++i){const float was=a[i];a[i]=target[i]+vel[i]*n;moved+=(a[i]-was)*(a[i]-was);}
        if(moved<kLeadSettle*kLeadSettle)break;
    }
    const float yaw=std::atan2(local[0],local[2]),ce=std::cos(elevation);
    const float l[3]={std::sin(yaw)*ce,std::sin(elevation),std::cos(yaw)*ce};
    for(int i=0;i<3;++i)dir[i]=m[i]*l[0]+m[4+i]*l[1]+m[8+i]*l[2];
    const float length=std::sqrt(Dot3(dir,dir));
    if(!(length>0.5f))return false;
    for(int i=0;i<3;++i){dir[i]/=length;aim[i]=a[i];}
    *frames=n;
    return std::isfinite(n);
}
}  // namespace aim
}  // namespace autoturret
