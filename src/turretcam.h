// The turret camera's math (turretcam.cpp, docs/camera-re.md §5), kept apart from the game so that
// tools/turret_cam_check.cpp can check it off it (no EDF.dll in here).
//  - Directions: a world heading `yaw` (rad, growing as it turns right: the right of +Z is -X, docs/player-jet-re.md §2;
//    a seat aim's yaw axis is atan2(local x, local z), local x the hull's row 0, its left: it grows turning left, and
//    turretcam.cpp goes between the two through the hull's matrix) and an elevation `pitch` (rad, up positive; the aim's
//    pitch axis is negative-up).
//  - A rig: the camera orbits a point O at a radius, looking through it; its eye is never lower than `rise` over O (the
//    height the rig's own design puts it at), so looking up tilts the view instead of sinking the eye into the hull.
//  - The turret command: an input of -1..1 to the stock axis step (0x5FBC00, NextRate / AxisStep below), the same range
//    a stick gives it, so the turret never turns faster or eases quicker than its own params allow; picked a frame at a
//    time as the one after which letting go would bring the axis to rest nearest its want (AxisCommand).
#pragma once
#include "vecmath.h"
#include <cmath>

namespace crew {
namespace tcam {
constexpr float kPi=3.14159265f;

inline float Wrap(float a) noexcept {
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}
// The unit direction of heading `yaw`, elevation `pitch`.
inline void Dir(float yaw,float pitch,float* d) noexcept {
    const float c=std::cos(pitch);
    d[0]=-std::sin(yaw)*c;d[1]=std::sin(pitch);d[2]=std::cos(yaw)*c;
}
inline float YawOf(const float* d) noexcept { return std::atan2(-d[0],d[2]); }
inline float PitchOf(const float* d) noexcept {
    const float h=std::sqrt(d[0]*d[0]+d[2]*d[2]);
    return std::atan2(d[1],h);
}
// The horizontal forward and right of heading `yaw` (right = (-fz, 0, fx), the game's).
inline void Flat(float yaw,float* fwd,float* right) noexcept {
    fwd[0]=-std::sin(yaw);fwd[1]=0.0f;fwd[2]=std::cos(yaw);
    right[0]=-fwd[2];right[1]=0.0f;right[2]=fwd[0];
}

// A camera rig: the orbit point `up` m over the hull's origin and `ahead` / `side` m along the camera's own heading
// (O turns with the camera, round the hull's origin: authored points in the vehicle's frame swing with the view, as a
// third-person camera round its vehicle does), plus `fixed` (a world offset from the hull's origin kept as is: a turret's
// yaw axis); the radius, and the eye's least height over O.
struct Rig { float up,ahead,side,radius,rise; float fixed[3]; };

// The rig's eye and look-at for the view `d` (unit) with the hull's origin at `origin`: the eye `radius` behind O along
// `d`, raised (with the look-at, so the view keeps its direction) to `rise` over O when it would be lower.
inline void Place(const Rig& r,const float* origin,float yaw,const float* d,float* eye,float* look) noexcept {
    float fwd[3],right[3];
    Flat(yaw,fwd,right);
    float o[3];
    for(int i=0;i<3;++i)o[i]=origin[i]+r.fixed[i]+fwd[i]*r.ahead+right[i]*r.side;
    o[1]+=r.up;
    for(int i=0;i<3;++i){eye[i]=o[i]-d[i]*r.radius;look[i]=o[i];}
    const float low=o[1]+r.rise-eye[1];
    if(low>0.0f){eye[1]+=low;look[1]+=low;}
}

// The rig an authored pair makes (`look`, `eye` in the vehicle's frame: x right, y up, z ahead, as
// game_object_camera_setting holds them): O at the look-at, the eye where it is at the pair's own pitch. False when the
// pair is no camera behind its point (an eye not behind, or one on the point).
inline bool Authored(const float* look,const float* eye,Rig* out) noexcept {
    const float dz=look[2]-eye[2],dy=look[1]-eye[1],dx=look[0]-eye[0];
    const float r=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(!(dz>0.5f) || !(r>1.0f) || !std::isfinite(r))return false;
    *out=Rig{look[1],look[2],look[0],r,eye[1]-look[1],{0.0f,0.0f,0.0f}};
    return true;
}

// The high view (README 高视角): from `height` m over the origin and `back` m behind it the view looks `pitchDeg` down
// ahead; as a rig: O at height - back tan(pitch) over the origin, radius back / cos(pitch), the eye kept at least as
// high as that view's.
inline Rig High(float height,float back,float pitchDeg) noexcept {
    const float p=pitchDeg*kPi/180.0f,c=std::cos(p),t=std::tan(p);
    const float r=back>0.5f ? back/c : height*0.5f;
    const float up=back>0.5f ? height-back*t : height*0.5f;
    return Rig{up,0.0f,0.0f,r,height-up,{0.0f,0.0f,0.0f}};
}

// Who turns the player's turret this frame (common/edf/aimlink.h V2): `steers` EDF6AutoTurret's answer (1: it wrote the
// seat's input this frame, 0: it did not, -1: no answer, an older peer or none), `in` the aim's input, `stick` the
// rider's as the aim gets it. Another hand's: the camera leaves the turret to it for the frame. With no answer, V1's
// guess: an input more than `tol` off the stick.
inline bool Foreign(int steers,const float* in,const float* stick,float tol) noexcept {
    if(steers>=0)return steers!=0;
    return std::fabs(in[0]-stick[0])>tol || std::fabs(in[1]-stick[1])>tol;
}
// Whether the gun's round (its low arc) goes through the view point, else the gun's bore line: only for a real hit
// (ground, a building, a target under the screen's centre) outside the lead-circle mode. The lead circle already solved
// the arc (the bore line must pass through it: raising the gun again for the drop drops nothing twice), and a view
// that hits nothing gives a made-up point kAimFar out whose range means nothing.
inline bool BallisticAim(bool viewHit,bool leadCircle) noexcept { return viewHit && !leadCircle; }

// Each rig parameter `a` eased toward `b` by `k` (0..1).
inline void Ease(Rig& a,const Rig& b,float k) noexcept {
    a.up+=(b.up-a.up)*k;a.ahead+=(b.ahead-a.ahead)*k;a.side+=(b.side-a.side)*k;
    a.radius+=(b.radius-a.radius)*k;a.rise+=(b.rise-a.rise)*k;
    for(int i=0;i<3;++i)a.fixed[i]+=(b.fixed[i]-a.fixed[i])*k;
}

// The stock axis step (0x5FBC00) of one aim axis: params {brake, accel, top} (the aim's +0x90); the rate, normalized to
// the top rate, eases to the input by accel (same sense, or from under `brake` with input pushed) or by brake (stopping:
// zero under brake x 0.4 with no input); the angle moves by rate x top and stops at the axis' ends (a full circle wraps).
inline float NextRate(float v,float in,const float* p) noexcept {
    const bool pushed=std::fabs(in)>1.1920929e-7f;
    if((p[0]>=std::fabs(v) && pushed) || v*in>0.0f)return v+(in-v)*p[1];
    v+=(in-v)*p[0];
    return p[0]*0.4f>=std::fabs(v) && !pushed ? 0.0f : v;
}
struct Axis { float lo,hi,angle,rate; };
inline void AxisStep(Axis& x,float in,const float* p) noexcept {
    const float v=p[2]!=0.0f ? x.rate/p[2] : 0.0f;
    x.rate=NextRate(v,in,p)*p[2];
    x.angle+=x.rate;
    if(x.hi-x.lo>=2.0f*kPi-1e-3f){x.angle=Wrap(x.angle);return;}
    if(x.angle<x.lo){x.angle=x.lo;x.rate=x.rate>0.0f ? x.rate : 0.0f;}
    if(x.angle>x.hi){x.angle=x.hi;x.rate=x.rate<0.0f ? x.rate : 0.0f;}
}

// How far (rad) an axis at normalized rate `v` still turns past a want moving `drift` rad a frame when its input goes
// to `hold` (the input that keeps pace with the want: no input for a still one), until its rate is the want's.
constexpr int kRestFrames=240;
inline float Rest(float v,float hold,float drift,const float* p) noexcept {
    float d=0.0f;
    const float pace=drift/p[2];
    for(int n=0;n<kRestFrames && std::fabs(v-pace)>1e-5f;++n){v=NextRate(v,hold,p);d+=v*p[2]-drift;}
    return d;
}

// The input (-1..1, what a stick could give: never past the stock top rate or its own easing) that brings an axis
// `error` rad off its want, turning at `rate` rad a frame, onto a want moving `drift` rad a frame. Planned in the want's
// own motion: the input after which going back to the input that keeps pace with it (`hold`; none for a still want)
// would leave the axis on it. The step brakes on no input and eases on any (NextRate), so where it ends up is monotonic
// only within each sign of the input: the best of no input, of `hold`, of the positive inputs and of the negative ones
// (each found by bisection) is taken, an overshoot counted double. Far off: the full input (the stock top rate); near:
// it settles onto the want, no overshoot to fight back.
constexpr int kBisect=20;
inline float Miss(float error,float drift,float hold,float v,float in,const float* p) noexcept {
    const float v1=NextRate(v,in,p);
    return error-(v1*p[2]-drift)-Rest(v1,hold,drift,p);   // > 0: short of the want
}
inline float Score(float miss,float sense) noexcept { return miss*sense>=0.0f ? std::fabs(miss) : 2.0f*std::fabs(miss); }
// The input in [lo, hi] (one sign) with the least miss: the miss falls as the input grows.
inline float Solve(float error,float drift,float hold,float v,float lo,float hi,const float* p) noexcept {
    if(Miss(error,drift,hold,v,hi,p)>=0.0f)return hi;
    if(Miss(error,drift,hold,v,lo,p)<=0.0f)return lo;
    for(int i=0;i<kBisect;++i) {
        const float mid=0.5f*(lo+hi);
        if(Miss(error,drift,hold,v,mid,p)>0.0f)lo=mid;
        else hi=mid;
    }
    return 0.5f*(lo+hi);
}
inline float AxisCommand(float error,float rate,float drift,const float* p) noexcept {
    if(!(p[2]>1e-6f) || !std::isfinite(error) || !std::isfinite(rate) || !std::isfinite(drift))return 0.0f;
    const float v=rate/p[2],sense=error>=0.0f ? 1.0f : -1.0f,hold=vec::Clamp(drift/p[2],-1.0f,1.0f);
    constexpr float kTiny=1e-6f;
    const float pick[4]={0.0f,hold,Solve(error,drift,hold,v,kTiny,1.0f,p),Solve(error,drift,hold,v,-1.0f,-kTiny,p)};
    float best=0.0f,bestScore=3.4e38f;
    for(float in:pick) {
        const float score=Score(Miss(error,drift,hold,v,in,p),sense);
        if(score<bestScore){bestScore=score;best=in;}
    }
    return best;
}
}  // namespace tcam
}  // namespace crew
