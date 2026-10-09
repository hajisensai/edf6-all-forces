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

constexpr float kPitchMost=80.0f*kPi/180.0f,kViewMost=85.0f*kPi/180.0f;

// Observe the actual projectile endpoint, keeping its exact position even at point-blank range, beyond the old
// ground cursor's range limit, or above/below the hull. The eye orbits the endpoint, so distant shots do not lift the
// camera hundreds of metres above the vehicle. Free look only orbits this point; it never commands the gun.
inline void ImpactPlace(const float* origin,const float* point,float fallbackYaw,float height,float back,
                        float orbitYaw,float orbitPitch,float* eye,float* look) noexcept {
    const float delta[3]={point[0]-origin[0],0.0f,point[2]-origin[2]};
    const float yaw=(vec::Dot(delta,delta)>1e-6f ? YawOf(delta) : fallbackYaw)+orbitYaw;
    const float radius=std::fmax(1.0f,std::sqrt(height*height+back*back));
    const float pitch=vec::Clamp(std::atan2(height,back)+orbitPitch,15.0f*kPi/180.0f,kViewMost);
    float d[3];Dir(yaw,-pitch,d);
    for(int i=0;i<3;++i){look[i]=point[i];eye[i]=point[i]-d[i]*radius;}
}

// The point the high view observes (the user, 2026-10-09: "这个俯瞰视角有问题，计算明显不对，稍微远就跳到了指数级别的
// 距离"): the shot's real landing `at` (`landed`) while it is within `most` m of the muzzle across the ground; otherwise
// (the round's life ends in the air, it passes the search's reach, or it lands farther) the ground point under where the
// shot ends, `most` m at the farthest, along the shot's heading (the bore's when it goes straight up or down), at the
// ground's height there `ground`. Before, an unlanded shot's end was observed where it was: a flat gun raised a few
// degrees put the view on a point hundreds of metres up and up to its whole life's flight away (the log: 122 m at 0.4 deg
// down, 2409 m in the air at 2 deg up). Now the observed distance across the ground is never over the round's own reach
// or `most`, the point is never in the air, and over flat ground the distance is continuous in the gun's elevation
// (where a shot stops landing it lands at its life's end: the same point) and grows with it up to the arc's farthest
// angle (tools/turret_cam_check.cpp sweeps it). The landing's distance still grows as height / tan(depression) for a flat
// gun near the level: that is where its rounds go.
inline float AcrossDistance(const float* a,const float* b) noexcept {
    const float dx=b[0]-a[0],dz=b[2]-a[2];
    return std::sqrt(dx*dx+dz*dz);
}
inline void HighFocus(const float* muzzle,const float* dir,bool landed,const float* at,float most,float ground,float* out) noexcept {
    const float d=AcrossDistance(muzzle,at);
    if(landed && d<=most){for(int i=0;i<3;++i)out[i]=at[i];return;}
    float h[2]={at[0]-muzzle[0],at[2]-muzzle[2]};
    float len=d;
    if(!(len>1e-3f)){h[0]=dir[0];h[1]=dir[2];len=std::sqrt(h[0]*h[0]+h[1]*h[1]);}
    if(!(len>1e-6f)){h[0]=0.0f;h[1]=1.0f;len=1.0f;}
    const float r=std::fmin(d,most);
    out[0]=muzzle[0]+h[0]/len*r;out[1]=ground;out[2]=muzzle[2]+h[1]/len*r;
}

// The view (yaw, pitch) of rig `r` whose screen's centre goes through world point `p` (the high view handing back: the
// normal view comes back on the point the high one was on). The centre's line goes through O, raised with the eye when
// the eye would be under O + rise (Place); found by going round a few times (O turns with the heading, the raise
// follows the pitch).
constexpr int kAimAtRounds=64;
inline void AimAt(const Rig& r,const float* origin,const float* p,float* yaw,float* pitch) noexcept {
    float d[3]={p[0]-origin[0],0.0f,p[2]-origin[2]};
    float y=d[0]*d[0]+d[2]*d[2]>1e-6f ? YawOf(d) : *yaw,pt=0.0f;
    for(int i=0;i<kAimAtRounds;++i) {   // the raise's pull on the pitch is slow to settle looking up at a hill
        float fwd[3],right[3];
        Flat(y,fwd,right);
        float o[3];
        for(int k=0;k<3;++k)o[k]=origin[k]+r.fixed[k]+fwd[k]*r.ahead+right[k]*r.side;
        o[1]+=r.up+std::fmax(0.0f,r.rise+std::sin(pt)*r.radius);
        for(int k=0;k<3;++k)d[k]=p[k]-o[k];
        if(!vec::Normalize(d))break;
        const float ny=YawOf(d),np=PitchOf(d);
        const bool settled=std::fabs(Wrap(ny-y))<1e-7f && std::fabs(np-pt)<1e-7f;
        y=ny;pt=np;
        if(settled)break;
    }
    *yaw=y;*pitch=vec::Clamp(pt,-kPitchMost,kPitchMost);
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

// Where the gun's wants are measured from: the point on its bore line (through `muzzle` along the unit `dir`) nearest
// `pivot`, the centre the turret turns about (turretcam.cpp TurretPivot); the muzzle itself with no pivot. Not the
// muzzle: it sits R m out from the pivot and swings with the gun, so a want measured from it moves as the gun turns
// toward it, D/(D-R) of the turn ahead of it for a point D m from the pivot. The step after overshoots by that, the next
// one back: near points (the ground just ahead of a short, low rig: the Grape's) the barrel jerks left and right every
// frame, and under D = 2R it never settles. From the pivot the want does not move with the gun's own turn; the bore line
// through the point is the same, so far points are aimed as before.
inline void AimOrigin(const float* muzzle,const float* dir,const float* pivot,float* origin) noexcept {
    for(int i=0;i<3;++i)origin[i]=muzzle[i];
    if(!pivot)return;
    const float back[3]={pivot[0]-muzzle[0],pivot[1]-muzzle[1],pivot[2]-muzzle[2]};
    const float t=vec::Dot(back,dir);
    if(!std::isfinite(t))return;
    for(int i=0;i<3;++i)origin[i]=muzzle[i]+dir[i]*t;
}

// The point `to` (world) from `from` in the frame `rows` (world rows: the local x, up, nose): `l` local, `across` its
// horizontal distance. The axes' wants for the bore line through it are atan2(l[0], l[2]) and -atan2(l[1], across).
inline void LocalTo(const float* rows,const float* from,const float* to,float* l,float* across) noexcept {
    const float d[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    for(int i=0;i<3;++i)l[i]=vec::Dot(rows+3*i,d);
    *across=std::sqrt(l[0]*l[0]+l[2]*l[2]);
}

// One frame's turret command (turretcam.cpp Steer): the input that turns each axis onto `want` (the aim's senses), each
// wanted angle's drift a frame fed forward; true when both are on within `onTarget` rad. `held` / `hull`: what the
// stabilizer holds the gun at with no command and the hull's part of that since the last step (stab.h Held; the axes as
// they are and 0 with none): steered from `held`, the hull's turn out of the drift (the stabilizer turns the gun by it
// after the step: fed forward here too it would send the gun past the point a second time). `want` must be seen in the
// frame `held` is (stab.h HeldIn).
struct SteerState { float lastWant[2],drift[2]; bool hasWant; };
inline bool SteerAxes(SteerState& g,const float* want,const float* held,const float* hull,const Axis* axes,const float* params,
                      float onTarget,float* in) noexcept {
    bool on=true;
    for(int i=0;i<2;++i) {
        const Axis& x=axes[i];
        const bool full=x.hi-x.lo>=2.0f*kPi-0.01f;
        const float target=full ? want[i] : vec::Clamp(want[i],x.lo,x.hi);
        const float error=full ? Wrap(target-held[i]) : target-held[i];
        if(g.hasWant) {
            const float moved=(full ? Wrap(target-g.lastWant[i]) : target-g.lastWant[i])-hull[i];
            g.drift[i]+=(vec::Clamp(moved,-0.2f,0.2f)-g.drift[i])*0.5f;
        }
        g.lastWant[i]=target;
        in[i]=AxisCommand(error,x.rate,g.drift[i],params);
        on=on && std::fabs(error)<onTarget;
    }
    g.hasWant=true;
    return on;
}
}  // namespace tcam
}  // namespace crew
