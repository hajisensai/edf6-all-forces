// The gun stabilizer's math (stab.cpp; README 炮管稳定器; docs/camera-re.md §7), kept apart from the game so that
// tools/stab_check.cpp can check it off it (no EDF.dll in here).
//  - A frame: the world rows of a gun's mount (the hull's rows at veh+0x60: row 0 the local x, row 1 up, row 2 the
//    nose), the aim's axes in it as the stock code has them (docs/nix-re.md §3, 0x5FACD0): yaw `a` turns the nose toward
//    local +x, the pitch axis is negative up; the gun's local line (sin a cos e, sin e, cos a cos e), e = -pitch.
//  - The stabilizer (Hold / Step) runs after the stock axis step: it keeps a reference line in the WORLD that the
//    gun's own command (the step's turn, whoever gave the input) moves, and turns the axes toward that line as seen
//    from the hull where the drawn gun will be, closing `1 - 1/(1 + lag x 60)` of the error a frame (a real stabilizer's
//    residual: the error grows with the hull's turn rate, `lag` s of it), never turning the axis faster in total than
//    the turret's own top rate (the stock params' top, rad/frame) and never past its stops. Past `slip` rad off (the hull
//    turns faster than the drive), the reference is dragged along: the gun lags, then catches up once the hull calms.
//  - Which hull the drawn gun is seen in is measured, not assumed (Probe): the muzzle bones of the last pose against
//    the axes as they were then, under each hypothesis (the gun's mount: the hull, or the hull turned by seat 0's yaw
//    axis, a gun on the main turret; the pose's hull: the aim step's own, or the next step's, the hull having moved on
//    between the step and the pose); the right one keeps the gun's offset in its axes' frame (o) constant. A hypothesis
//    none fits means the axes do not drive the gun the way the frame says: that seat is left stock.
#pragma once
#include "turretcam.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace stab {
using tcam::kPi;
using tcam::Wrap;

struct Frame { float r[9]; };   // rows: local x, up, nose (world, unit, orthogonal)

inline float Dot3(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline bool Unit3(float* a) noexcept {
    const float l=std::sqrt(Dot3(a,a));
    if(!std::isfinite(l) || l<1e-6f)return false;
    a[0]/=l;a[1]/=l;a[2]/=l;
    return true;
}
// Three rows made orthonormal (Gram-Schmidt from the nose, then up, then x): false when they are degenerate.
inline bool Orthonormal(const float* x,const float* y,const float* z,Frame* f) noexcept {
    float r0[3],r1[3],r2[3];
    std::memcpy(r2,z,12);
    if(!Unit3(r2))return false;
    const float yz=Dot3(y,r2);
    for(int i=0;i<3;++i)r1[i]=y[i]-r2[i]*yz;
    if(!Unit3(r1))return false;
    const float xz=Dot3(x,r2),xy=Dot3(x,r1);
    for(int i=0;i<3;++i)r0[i]=x[i]-r2[i]*xz-r1[i]*xy;
    if(!Unit3(r0))return false;
    std::memcpy(f->r,r0,12);std::memcpy(f->r+3,r1,12);std::memcpy(f->r+6,r2,12);
    return true;
}
// A game matrix (rows of 4 floats: x, up, nose, position) as a frame.
inline bool FromMatrix(const float* m,Frame* f) noexcept { return Orthonormal(m,m+4,m+8,f); }
inline void Local(const Frame& f,const float* d,float* l) noexcept { for(int i=0;i<3;++i)l[i]=Dot3(f.r+3*i,d); }
inline void World(const Frame& f,const float* l,float* d) noexcept {
    for(int i=0;i<3;++i)d[i]=f.r[i]*l[0]+f.r[3+i]*l[1]+f.r[6+i]*l[2];
}
// The aim's line for axes `a` (yaw, pitch: the aim's senses) in `f`, and back.
inline void Dir(const Frame& f,const float* a,float* d) noexcept {
    const float e=-a[1];
    const float l[3]={std::sin(a[0])*std::cos(e),std::sin(e),std::cos(a[0])*std::cos(e)};
    World(f,l,d);
}
inline void Angles(const Frame& f,const float* d,float* a) noexcept {
    float l[3];
    Local(f,d,l);
    a[0]=std::atan2(l[0],l[2]);
    a[1]=-std::atan2(l[1],std::sqrt(l[0]*l[0]+l[2]*l[2]));
}
// `f` turned about its own up by the yaw `a` (aim senses): the frame of a gun on a turret seat 0's yaw axis turns.
inline Frame Turned(const Frame& f,float a) noexcept {
    const float c=std::cos(a),s=std::sin(a);
    Frame t=f;
    for(int i=0;i<3;++i){t.r[i]=c*f.r[i]-s*f.r[6+i];t.r[6+i]=s*f.r[i]+c*f.r[6+i];}
    return t;
}
// The frame one step on at the rate `prev` -> `now` turned (M now x M prev^T x M now, rows as the matrix).
inline Frame Ahead(const Frame& now,const Frame& prev) noexcept {
    float a[9],out[9];
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[3*i+j]=Dot3(now.r+3*i,prev.r+3*j);
    for(int i=0;i<3;++i)for(int k=0;k<3;++k)out[3*i+k]=a[3*i]*now.r[k]+a[3*i+1]*now.r[3+k]+a[3*i+2]*now.r[6+k];
    Frame f{};
    return Orthonormal(out,out+3,out+6,&f) ? f : now;
}
// The angle (rad) one frame is turned from another.
inline float Turn(const Frame& a,const Frame& b) noexcept {
    const float tr=Dot3(a.r,b.r)+Dot3(a.r+3,b.r+3)+Dot3(a.r+6,b.r+6);
    const float c=0.5f*(tr-1.0f);
    return std::acos(c>1.0f ? 1.0f : c<-1.0f ? -1.0f : c);
}
// `v` (in the axes' frame at `a`) back to the gun's own: the inverse of yaw-after-pitch (Ry(-yaw), then Rx(-e)).
inline void Unturn(const float* a,const float* v,float* o) noexcept {
    const float cy=std::cos(a[0]),sy=std::sin(a[0]),e=-a[1],ce=std::cos(e),se=std::sin(e);
    const float x=v[0]*cy-v[2]*sy,z=v[0]*sy+v[2]*cy,y=v[1];
    o[0]=x;o[1]=y*ce-z*se;o[2]=y*se+z*ce;
}

// --- the stabilizer ---

// A class's stabilizer (stab.cpp's table): `lag` s of the hull's turn rate left as error, `slip` rad the most the gun
// lets the reference get ahead of it before dragging it.
struct Perf { float lag,slip; };
inline float Gain(const Perf& p) noexcept { return 1.0f/(1.0f+(p.lag>0.0f ? p.lag : 0.0f)*60.0f); }

// An aim axis' stops (`full`: a whole circle, wrapping).
struct Stops { float lo,hi; bool full; };
inline Stops StopsOf(float lo,float hi) noexcept { return Stops{lo,hi,hi-lo>=2.0f*kPi-1e-3f}; }
inline float Diff(const Stops& s,float to,float from) noexcept { return s.full ? Wrap(to-from) : to-from; }
inline float Keep(const Stops& s,float a) noexcept { return s.full ? Wrap(a) : a<s.lo ? s.lo : a>s.hi ? s.hi : a; }

struct Hold {
    bool live;
    float ref[3];     // the line held in the world
    Frame last;       // the mount's frame at the last step (the command turns the reference about it)
    Frame seen;       // the frame the last step aimed the gun for (where the drawn gun was seen)
    float shift[2];   // what the last step added to the axes past the stock step (rad, the aim's senses)
    float error;      // rad the gun is off the reference after the last step (both axes' larger)
    bool slipping;    // the reference was dragged (the drive could not keep up)
};

// A native velocity-joint readback replaces its previous target with the measured angle before the
// next input step. Reconcile that actuator tracking difference in the PREVIOUS pose basis, rather
// than treating it as another hull correction. Do not update last/seen: the following Step must still
// compensate this frame's parent motion and apply the player's/NPC's new command independently.
inline void Readback(Hold& h,const Stops& stops,int axis,float commanded,float measured) noexcept {
    if(!h.live || axis<0 || axis>1 || !std::isfinite(commanded) || !std::isfinite(measured))return;
    const float delta=Diff(stops,measured,commanded);
    if(delta==0.0f)return;
    float ref[2];Angles(h.seen,h.ref,ref);ref[axis]+=delta;Dir(h.seen,ref,h.ref);
}

// One step after the stock one. `stops` the axes' ends, `before` / `after` the angles around the stock step (its turn
// is the command), `top` the turret's top rate (rad/frame), `f` the mount's frame now (the command's), `seen` the one
// the drawn gun will be seen in (f, or f a frame ahead). `out` the angles to set.
inline void Step(Hold& h,const Stops* stops,const float* before,const float* after,float top,const Frame& f,const Frame& seen,
                 const Perf& p,float* out) noexcept {
    out[0]=after[0];out[1]=after[1];
    h.shift[0]=h.shift[1]=0.0f;h.error=0.0f;h.slipping=false;
    if(!h.live){Dir(seen,after,h.ref);h.last=f;h.seen=seen;h.live=true;return;}
    // The command (the stock step's own turn) moves the reference, about the frame it was held in.
    float ra[2];
    Angles(h.last,h.ref,ra);
    for(int i=0;i<2;++i)ra[i]+=Diff(stops[i],after[i],before[i]);
    Dir(h.last,ra,h.ref);
    float t[2];
    Angles(seen,h.ref,t);
    const float k=Gain(p);
    bool moved=false;
    for(int i=0;i<2;++i) {
        const float want=Keep(stops[i],t[i]);
        moved=moved || want!=t[i];
        t[i]=want;
        const float own=Diff(stops[i],after[i],before[i]);
        float c=Diff(stops[i],t[i],after[i])*k;
        const float total=own+c,most=top>0.0f ? top : 0.0f;
        if(total>most)c=most-own;
        else if(total<-most)c=-most-own;
        out[i]=Keep(stops[i],after[i]+c);
        h.shift[i]=Diff(stops[i],out[i],after[i]);
        float left=Diff(stops[i],t[i],out[i]);
        if(std::fabs(left)>p.slip){left=left>0.0f ? p.slip : -p.slip;t[i]=Keep(stops[i],out[i]+left);h.slipping=moved=true;}
        if(std::fabs(left)>h.error)h.error=std::fabs(left);
    }
    if(moved)Dir(seen,t,h.ref);   // held at a stop, or dragged: the reference is where the gun can be
    h.last=f;h.seen=seen;
}

// What a controller of a held gun works against, before this frame's step: `held` the axes the gun goes to with no
// command (the reference seen from `seen`, this frame's, within the stops), `hull` how far the hull's turn since the
// last step moved that (the part of a want's change, and of the axes' own motion, that is the stabilizer's, not the
// command's). False (both the axes' own and 0) while it holds nothing.
inline bool Held(const Hold& h,const Stops* stops,const Frame& seen,const float* axes,float* held,float* hull) noexcept {
    held[0]=axes[0];held[1]=axes[1];hull[0]=hull[1]=0.0f;
    if(!h.live)return false;
    float now[2],was[2];
    Angles(seen,h.ref,now);Angles(h.seen,h.ref,was);
    for(int i=0;i<2;++i){held[i]=Keep(stops[i],now[i]);hull[i]=Diff(stops[i],now[i],was[i]);}
    return true;
}

// Held, and the frame the controller must see its wants in (`frame`): the one `held` is seen in, `seen` (where the
// drawn gun will be after the step: the hull a step ahead when the pose takes the next step's hull, Probe); `now` (the
// hull at this step) while nothing is held. A want seen in `now` against a `held` seen in `seen` is off by the hull's
// turn in a frame: the controller keeps the gun that far off its point (0.4 deg at 25 deg/s), the offset swinging from
// side to side with every steering correction.
inline bool HeldIn(const Hold& h,const Stops* stops,const Frame& seen,const Frame& now,const float* axes,float* held,float* hull,
                   Frame* frame) noexcept {
    *frame=now;
    if(!Held(h,stops,seen,axes,held,hull))return false;
    *frame=seen;
    return true;
}

// 0x5FC280(axis, true) maps TWO bone samples: angle and angle-rate. After stabilization `rate` is still the stock
// command's velocity, while the pose must interpolate the whole turn from `before` to the corrected angle. Supply
// that displacement only while mapping; keeping it in the controller would feed the stabilizer into its motor.
template<class Apply> inline void Remap(float* axis,float before,float corrected,Apply apply) noexcept {
    const float commandRate=axis[3];
    axis[2]=corrected;
    axis[3]=Diff(StopsOf(axis[0],axis[1]),corrected,before);
    apply(axis);
    axis[3]=commandRate;
}

// --- which frame the drawn gun is in (see the top) ---

constexpr int kMounts=2,kTimings=2,kHypotheses=kMounts*kTimings;   // h = mount x 2 + timing (0 same, 1 next)
constexpr float kDecay=0.995f;           // a step's share of the running sums (~3 s at 60 frames)
constexpr float kDecideMotion=0.05f;     // rad of motion summed before a choice is made
constexpr float kClear=0.5f;             // the other hypothesis' error at least this many times the winner's
constexpr float kFitMost=0.3f;           // the winner's error per rad of motion: more, and none fits
constexpr float kAlignedMount=0.05f;     // rad: seat 0's yaw this near zero, a gun on its turret is on the hull's frame

struct Probe {
    bool has;                // o and the frames below are last step's
    float o[kHypotheses][3]; // the gun's offset in its axes' frame, per hypothesis
    float err[kHypotheses];  // the running change of o, per hypothesis
    float hull,mount,motion; // the running motion that tells the timings apart / the mounts / any
};
struct Choice { bool known; bool fits; int mount; bool next; };

// `hullNow` / `hullPrev` the hull's frames at this step and the last, `posed` the gun's axes as the last pose built it
// (the angles before this step's stock step), `seat0` seat 0's yaw as that pose built it, `muzzle` the drawn gun's line
// (world unit). `turret`: the mount hypothesis is open (a gunner seat; seat 0's own gun is on the hull's frame).
inline void Feed(Probe& p,const Frame& hullNow,const Frame& hullPrev,const float* posed,float seat0,float seat0Was,const float* posedWas,
                 const float* muzzle,bool turret) noexcept {
    const Frame pose[kHypotheses]={hullPrev,hullNow,Turned(hullPrev,seat0),Turned(hullNow,seat0)};
    float o[kHypotheses][3];
    for(int h=0;h<kHypotheses;++h){float l[3];Local(pose[h],muzzle,l);Unturn(posed,l,o[h]);}
    if(p.has) {
        const float hull=Turn(hullNow,hullPrev),gun=std::fabs(Wrap(posed[0]-posedWas[0]))+std::fabs(posed[1]-posedWas[1]);
        const float mount=turret ? std::fabs(Wrap(seat0-seat0Was))+std::fabs(posed[1]-posedWas[1])*std::fabs(std::sin(seat0)) : 0.0f;
        p.hull=p.hull*kDecay+hull;p.mount=p.mount*kDecay+mount;p.motion=p.motion*kDecay+hull+gun+std::fabs(Wrap(seat0-seat0Was));
        for(int h=0;h<kHypotheses;++h) {
            const float d[3]={o[h][0]-p.o[h][0],o[h][1]-p.o[h][1],o[h][2]-p.o[h][2]};
            p.err[h]=p.err[h]*kDecay+std::sqrt(Dot3(d,d));
        }
    }
    std::memcpy(p.o,o,sizeof(o));
    p.has=true;
}

// The probe's answer. Seat 0 (`turret` false) is known from the start on its hull, at the aim step's own frame, until
// the measure says otherwise; a gunner seat is known once its mount is told apart (or seat 0's yaw is near zero, where
// the two mounts are one).
inline Choice Decide(const Probe& p,bool turret,float seat0) noexcept {
    Choice c{!turret,true,0,false};
    const int mounts=turret ? kMounts : 1;
    int best=0;
    for(int h=1;h<mounts*kTimings;++h)if(p.err[h]<p.err[best])best=h;
    if(p.hull>=kDecideMotion) {   // the timing: the winner's against the other timing on the same mount
        const int m=best/kTimings;
        c.next=p.err[m*kTimings+1]<p.err[m*kTimings]*kClear;
    }
    const int timing=c.next ? 1 : 0;
    if(turret) {
        const float e0=p.err[timing],e1=p.err[kTimings+timing];
        if(p.mount>=kDecideMotion && (e0<e1*kClear || e1<e0*kClear)){c.known=true;c.mount=e1<e0 ? 1 : 0;}
        else if(std::fabs(Wrap(seat0))<kAlignedMount){c.known=true;c.mount=0;}
    }
    if(p.motion>=kDecideMotion && p.err[c.mount*kTimings+timing]>kFitMost*p.motion)c.fits=false;
    return c;
}
}  // namespace stab
}  // namespace crew
