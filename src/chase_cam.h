// The chase follower of the plugin's vehicle cameras (turretcam.cpp LookHook / Camera, firstperson.cpp CameraAfterNative):
// pure math, no game memory, so tests/chase_cam_test.cpp runs the same code offline.
// Why (2026-11-21, 「载具/玩家之类的视角优化一下」; the Ace Combat community on a jet mod's camera: "stiff, like a picture
// stuck on the screen"): every camera the plugin places was put on its vehicle's origin exactly each tick (turretcam.cpp
// Place also overwrites the native eased points, so the stock per-tick lerp cam+0x650 / +0x654 does nothing), and the
// player's jets keep the stock Type-1 locators welded to the root bone `mdl`: the camera swung round the fuselage with
// every roll while the native LookTo (0x4E220, a direction only) kept the horizon level. Nothing had inertia, nothing
// rolled with the craft.
//  - Spring: a critically damped spring (omega rad/s) for a point, solved exactly over each step (the target taken to move
//    in a straight line through the step: exact for any step length, so 30, 60 or 240 steps a second give one result at
//    the shared sample times). `feed` is the share of the target's own velocity the spring follows without lag: 0 trails a
//    steady motion by 2 v / omega, 1 only lags accelerations (a / omega^2: the bumps, the turns, the braking), and up to
//    0.5 a jump of the target is reached without overshoot (the step response is 1 - e^-wt (1 + (1 - 2 feed) w t)).
//  - The craft's chase frame (FollowCraft, the jets): the stock eye and look-at held in the craft's frame, the frame
//    eased toward the craft's attitude (a shortest-arc slerp, dirTau), its bank partly taken out first (rollShare of it
//    kept, eased over rollTau): a banked turn rolls the picture by rollShare of the bank and the camera no longer swings
//    the whole bank round the fuselage. Past 90 deg of bank the share folds back (Fold) so the inverted craft is followed
//    upright and a roll through 180 deg has no jump; climbing or diving steeply the share fades out (the bank of a nose
//    pointed at the sky is no angle at all). The look-at leads by the path's angle off the nose (lookAhead s of speed, at
//    most lookAheadMax m): straight and level it is the stock look-at.
//  - Anchor (FollowAnchor, cameras whose direction is the player's input: the turret view, the Sazabi, the gunner seats,
//    the mouse-flown heli): only the vehicle's origin is sprung, the eye and look-at moved by the same offset, so the view's
//    direction stays exactly the input's (no mouse smoothing). A heli also rolls by rollShare of its bank as seen along
//    the view.
//  - Roll (UpRows): the native step builds the camera's rows from a direction (no roll); after it the rows are turned
//    about their forward row onto the follower's up, still orthonormal, the forward row (the screen's centre) untouched.
// Rules: dt <= 0 (the pause menu's camera steps, a second fetch in one tick) integrates nothing and returns the last
// result; a non-finite input is refused and leaves the state as it was; a gap over kMostDt, a jump over snapDist or an
// attitude change over snapAngle starts over on the target (a teleport, a new map).
#pragma once
#include <cmath>

namespace chase {
constexpr float kPi=3.14159265358979f;
constexpr float kMostDt=0.25f;   // s: a longer step is a gap (a load, a seat change unseen), not motion

struct Tuning {
    float omega;          // rad/s: the position spring (critically damped)
    float feed;           // 0..1: share of the target's velocity followed without lag (see the top)
    float dirTau;         // s: the chase frame's ease onto the craft's attitude (FollowCraft)
    float rollShare;      // share of the bank the picture rolls with
    float rollTau;        // s: that roll's ease
    float lookAhead;      // s of speed the look-at leads by along the path's angle off the nose (FollowCraft)
    float lookAheadMax;   // m: at most
    float snapDist;       // m: the target this far from the follower is a teleport
    float snapAngle;      // rad: the attitude this far from the chase frame is a teleport (FollowCraft)
};
// The tunings (to be judged in game; the user's feedback names no numbers). Ground: the turret view, the Sazabi, a gunner
// seat, the Dreadnought: 1.5 m behind at 15 m/s, a jolt of the hull smoothed over ~0.1 s, never an overshoot.
constexpr Tuning kGround{10.0f,0.5f,0.0f,0.0f,0.0f,0.0f,0.0f,25.0f,0.0f};
// The mouse-flown heli: as the ground, a sixth of its bank in the picture.
constexpr Tuning kHeli{10.0f,0.5f,0.0f,0.15f,0.3f,0.0f,0.0f,40.0f,0.0f};
// Aircraft whose view the player turns (the gunship's gun seat): no lag at speed (feed 1), the turns' and the bumps' only.
constexpr Tuning kAir{6.0f,1.0f,0.0f,0.0f,0.0f,0.0f,0.0f,150.0f,0.0f};
// The player's jets (the stock locators' chase): attitude eased over 0.15 s, 40% of the bank rolled, 0.3 s lead.
constexpr Tuning kJet{6.0f,1.0f,0.15f,0.4f,0.3f,0.3f,30.0f,150.0f,1.6f};
// The plugin's rotor craft flown on the same locators: slower, the heli's share of the bank.
constexpr Tuning kRotor{6.0f,1.0f,0.2f,0.15f,0.3f,0.3f,15.0f,150.0f,1.6f};

namespace detail {
inline bool Finite3(const float* a) noexcept { return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]); }
inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline void Cross(const float* a,const float* b,float* o) noexcept {
    const float x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0];
    o[0]=x;o[1]=y;o[2]=z;
}
inline bool Unit(float* a) noexcept {
    const float l=std::sqrt(Dot(a,a));
    if(!(l>1e-6f) || !std::isfinite(l))return false;
    a[0]/=l;a[1]/=l;a[2]/=l;
    return true;
}
}  // namespace detail

// The share of the way an exponential ease of time constant `tau` goes in `dt` (tau <= 0: all of it).
inline float Share(float dt,float tau) noexcept { return tau>0.0f ? 1.0f-std::exp(-dt/tau) : 1.0f; }

// --- the position spring ---
struct Spring {
    bool valid=false;
    float pos[3]{},vel[3]{};
    float target[3]{};       // the target at the last step
    float targetVel[3]{};    // its velocity over the last step (m/s; the look-ahead's)
};
enum class Step { refused,held,moved,snapped };

inline void Reset(Spring& s,const float* at) noexcept {
    s=Spring{};s.valid=true;
    for(int i=0;i<3;++i){s.pos[i]=at[i];s.target[i]=at[i];}
}

// One step of `dt` s onto `target`; `out` the follower's point (the target on a start, the last point when held).
inline Step Follow(Spring& s,const Tuning& t,const float* target,float dt,float* out) noexcept {
    if(!detail::Finite3(target) || !std::isfinite(dt)) {
        if(s.valid)for(int i=0;i<3;++i)out[i]=s.pos[i];
        return Step::refused;
    }
    float away[3]={target[0]-s.pos[0],target[1]-s.pos[1],target[2]-s.pos[2]};
    if(!s.valid || dt>kMostDt || std::sqrt(detail::Dot(away,away))>t.snapDist) {
        Reset(s,target);
        for(int i=0;i<3;++i)out[i]=target[i];
        return Step::snapped;
    }
    if(dt<=0.0f){for(int i=0;i<3;++i)out[i]=s.pos[i];return Step::held;}
    // The target from s.target to `target` at v through the step. Relative to it the spring is critically damped about
    // its offset p = -2 (1 - feed) v / omega: q = rel - p, q(t) = (q0 + (q0' + w q0) t) e^-wt.
    const float w=t.omega,e=std::exp(-w*dt);
    float next[3],nextVel[3];
    for(int i=0;i<3;++i) {
        const float v=(target[i]-s.target[i])/dt;
        const float p=-2.0f*(1.0f-t.feed)*v/w;
        const float q0=s.pos[i]-s.target[i]-p,qd0=s.vel[i]-v;
        const float c=qd0+w*q0;
        const float q1=(q0+c*dt)*e,qd1=(qd0-w*c*dt)*e;
        next[i]=target[i]+q1+p;nextVel[i]=qd1+v;
        s.targetVel[i]=v;
    }
    if(!detail::Finite3(next) || !detail::Finite3(nextVel)){Reset(s,target);for(int i=0;i<3;++i)out[i]=target[i];return Step::snapped;}
    for(int i=0;i<3;++i){s.pos[i]=next[i];s.vel[i]=nextVel[i];s.target[i]=target[i];out[i]=next[i];}
    return Step::moved;
}

// --- directions and frames (rows: left, up, forward; the game's: left = up x forward) ---

// `dir` (unit) turned toward `target` (unit) by `share` of the angle between them, along the shortest arc.
inline bool EaseDir(float* dir,const float* target,float share) noexcept {
    using namespace detail;
    if(!Finite3(dir) || !Finite3(target) || !std::isfinite(share))return false;
    const float c=Dot(dir,target);
    const float angle=std::acos(c>1.0f ? 1.0f : c<-1.0f ? -1.0f : c);
    if(angle<1e-6f){for(int i=0;i<3;++i)dir[i]=target[i];return true;}
    float axis[3];   // in the plane of the two, perpendicular to dir, toward target
    for(int i=0;i<3;++i)axis[i]=target[i]-dir[i]*c;
    if(!Unit(axis)) {   // opposite: any perpendicular
        const float side[3]={0.0f,1.0f,0.0f};
        Cross(side,dir,axis);
        if(!Unit(axis)){const float x[3]={1.0f,0.0f,0.0f};Cross(x,dir,axis);if(!Unit(axis))return false;}
    }
    const float a=angle*share,ca=std::cos(a),sa=std::sin(a);
    for(int i=0;i<3;++i)dir[i]=dir[i]*ca+axis[i]*sa;
    return Unit(dir);
}

// The level frame of a forward direction (as the native LookTo builds it: no roll); false: vertical.
inline bool LevelFrame(const float* forward,float* left,float* up) noexcept {
    using namespace detail;
    const float y[3]={0.0f,1.0f,0.0f};
    Cross(y,forward,left);
    if(!Unit(left))return false;
    Cross(forward,left,up);
    return Unit(up);
}

// A bank (rad) folded so that past 90 deg it comes back to 0 at 180: continuous through 180 (the inverted craft is
// followed whole, upright in the picture), equal to the bank up to 90.
inline float Fold(float bank) noexcept {
    const float b=std::atan2(std::sin(bank),std::cos(bank));
    const float h=0.5f*kPi;
    return b>h ? kPi-b : b<-h ? -kPi-b : b;
}

// The bank of rows (left, up, forward) in the world, cockpit_instruments.h BankOf's sense (right wing down positive), in
// rad; `weight` how far that is an angle at all: the forward row's level share (0 pointing at the sky).
inline float WorldBank(const float* left,const float* up,const float* forward,float* weight) noexcept {
    const float level=1.0f-forward[1]*forward[1];
    *weight=level>0.0f ? std::sqrt(level) : 0.0f;
    return left[1]!=0.0f || up[1]!=0.0f ? std::atan2(left[1],up[1]) : 0.0f;
}

// The bank of a vehicle whose up is `vehicleUp` as the picture along `forward` shows it (the same sense), rad;
// `weight` the share of its up in the picture's plane.
inline float SeenBank(const float* forward,const float* vehicleUp,float* weight) noexcept {
    float left[3],up[3];
    *weight=0.0f;
    if(!LevelFrame(forward,left,up))return 0.0f;
    const float l=detail::Dot(vehicleUp,left),u=detail::Dot(vehicleUp,up);
    *weight=std::sqrt(l*l+u*u);
    return *weight>1e-6f ? std::atan2(-l,u) : 0.0f;
}

// The camera rows (16 floats, rows left / up / forward / eye) turned about their forward row so their up is `up`'s
// part across it: orthonormal, right-handed as before, the forward row and the eye untouched. False: `up` along the
// forward row or a broken frame (nothing written).
inline bool UpRows(float* rows,const float* up) noexcept {
    using namespace detail;
    float f[3]={rows[8],rows[9],rows[10]};
    if(!Finite3(up) || !Unit(f))return false;
    const float along=Dot(up,f);
    float u[3]={up[0]-f[0]*along,up[1]-f[1]*along,up[2]-f[2]*along};
    if(!Unit(u))return false;
    float l[3];Cross(u,f,l);
    if(!Unit(l))return false;
    for(int i=0;i<3;++i){rows[i]=l[i];rows[4+i]=u[i];rows[8+i]=f[i];}
    return true;
}

// The up of a level frame along `forward` rolled by `roll` rad (BankOf's sense); false: vertical.
inline bool RolledUp(const float* forward,float roll,float* up) noexcept {
    float left[3],level[3];
    if(!LevelFrame(forward,left,level))return false;
    const float c=std::cos(roll),s=std::sin(roll);
    for(int i=0;i<3;++i)up[i]=level[i]*c-left[i]*s;
    return true;
}

// --- attitudes (unit quaternions of the rotation whose columns are the rows left, up, forward) ---
struct Quat { float w=1.0f,x=0.0f,y=0.0f,z=0.0f; };

inline Quat FromRows(const float* l,const float* u,const float* f) noexcept {
    // R's columns are l, u, f: R[i][0] = l[i], R[i][1] = u[i], R[i][2] = f[i].
    const float m00=l[0],m10=l[1],m20=l[2],m01=u[0],m11=u[1],m21=u[2],m02=f[0],m12=f[1],m22=f[2];
    const float trace=m00+m11+m22;
    Quat q;
    if(trace>0.0f) {
        const float s=std::sqrt(trace+1.0f)*2.0f;
        q.w=0.25f*s;q.x=(m21-m12)/s;q.y=(m02-m20)/s;q.z=(m10-m01)/s;
    } else if(m00>m11 && m00>m22) {
        const float s=std::sqrt(1.0f+m00-m11-m22)*2.0f;
        q.w=(m21-m12)/s;q.x=0.25f*s;q.y=(m01+m10)/s;q.z=(m02+m20)/s;
    } else if(m11>m22) {
        const float s=std::sqrt(1.0f+m11-m00-m22)*2.0f;
        q.w=(m02-m20)/s;q.x=(m01+m10)/s;q.y=0.25f*s;q.z=(m12+m21)/s;
    } else {
        const float s=std::sqrt(1.0f+m22-m00-m11)*2.0f;
        q.w=(m10-m01)/s;q.x=(m02+m20)/s;q.y=(m12+m21)/s;q.z=0.25f*s;
    }
    const float n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    q.w/=n;q.x/=n;q.y/=n;q.z/=n;
    return q;
}

inline void Rotate(const Quat& q,const float* v,float* out) noexcept {
    // v + 2 w (r x v) + 2 r x (r x v)
    const float r[3]={q.x,q.y,q.z};
    float t[3];detail::Cross(r,v,t);
    for(int i=0;i<3;++i)t[i]*=2.0f;
    float rt[3];detail::Cross(r,t,rt);
    for(int i=0;i<3;++i)out[i]=v[i]+q.w*t[i]+rt[i];
}

inline void ToRows(const Quat& q,float* l,float* u,float* f) noexcept {
    const float x[3]={1.0f,0.0f,0.0f},y[3]={0.0f,1.0f,0.0f},z[3]={0.0f,0.0f,1.0f};
    Rotate(q,x,l);Rotate(q,y,u);Rotate(q,z,f);
}

// The angle (rad) of the rotation between two attitudes.
inline float Angle(const Quat& a,const Quat& b) noexcept {
    const float d=std::fabs(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z);
    return 2.0f*std::acos(d>1.0f ? 1.0f : d);
}

// `a` turned toward `b` by `share` of the angle between them, the shorter way round.
inline Quat Slerp(const Quat& a,Quat b,float share) noexcept {
    float d=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;
    if(d<0.0f){d=-d;b.w=-b.w;b.x=-b.x;b.y=-b.y;b.z=-b.z;}
    float ka,kb;
    if(d>0.9995f){ka=1.0f-share;kb=share;}
    else {
        const float th=std::acos(d),s=std::sin(th);
        ka=std::sin((1.0f-share)*th)/s;kb=std::sin(share*th)/s;
    }
    Quat q{ka*a.w+kb*b.w,ka*a.x+kb*b.x,ka*a.y+kb*b.y,ka*a.z+kb*b.z};
    const float n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    q.w/=n;q.x/=n;q.y/=n;q.z/=n;
    return q;
}

// A vehicle matrix's rows (16 floats: left, up, forward, position) made orthonormal (forward kept, then up).
inline bool Rows(const float* m,float* l,float* u,float* f) noexcept {
    using namespace detail;
    for(int i=0;i<3;++i){f[i]=m[8+i];u[i]=m[4+i];}
    if(!Finite3(f) || !Finite3(u) || !Unit(f))return false;
    Cross(u,f,l);
    if(!Unit(l))return false;
    Cross(f,l,u);
    return Unit(u);
}

// --- the followers ---
struct Chase {
    Spring anchor;           // the vehicle's origin
    bool oriented=false;     // `orient` holds the chase frame (FollowCraft)
    Quat orient;
    bool rolled=false;       // `roll` holds the eased roll / bank taken out
    float roll=0.0f;
};
struct Placement { float eye[3],look[3],up[3]; bool hasUp; };

inline void Forget(Chase& c) noexcept { c=Chase{}; }

// Anchor follower: `eye` / `look` placed off the vehicle (matrix `m`, its origin m+12) moved by the sprung origin's
// offset; with t.rollShare a roll by that share of the bank seen along the view. False: refused (nothing written).
inline bool FollowAnchor(Chase& c,const Tuning& t,const float* m,const float* eye,const float* look,float dt,Placement* out) noexcept {
    using namespace detail;
    if(!Finite3(eye) || !Finite3(look) || !Finite3(m+12) || !std::isfinite(dt))return false;
    float at[3];
    const Step step=Follow(c.anchor,t,m+12,dt,at);
    if(step==Step::refused)return false;
    if(step==Step::snapped)c.rolled=false;
    for(int i=0;i<3;++i){const float d=at[i]-m[12+i];out->eye[i]=eye[i]+d;out->look[i]=look[i]+d;}
    out->hasUp=false;
    if(!(t.rollShare>0.0f))return true;
    float f[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]},vu[3]={m[4],m[5],m[6]};
    if(!Unit(f) || !Unit(vu))return true;
    float weight;
    const float bank=SeenBank(f,vu,&weight);
    const float want=t.rollShare*Fold(bank)*(weight>1.0f ? 1.0f : weight);
    if(!c.rolled){c.roll=want;c.rolled=true;}
    else if(dt>0.0f)c.roll+=(want-c.roll)*Share(dt,t.rollTau);
    out->hasUp=RolledUp(f,c.roll,out->up);
    return true;
}

// The craft's chase (see the top): `eye` / `look` the stock locators' points on the craft (matrix `m`). False: refused.
inline bool FollowCraft(Chase& c,const Tuning& t,const float* m,const float* eye,const float* look,float dt,Placement* out) noexcept {
    using namespace detail;
    float l[3],u[3],f[3];
    if(!Finite3(eye) || !Finite3(look) || !Finite3(m+12) || !std::isfinite(dt) || !Rows(m,l,u,f))return false;
    const bool fresh=!c.anchor.valid || dt>kMostDt;
    float at[3];
    const Step step=Follow(c.anchor,t,m+12,dt,at);
    if(step==Step::refused)return false;
    const bool start=fresh || step==Step::snapped;
    // The eye and the look-at in the craft's frame.
    float oe[3],ol[3];
    {
        const float de[3]={eye[0]-m[12],eye[1]-m[13],eye[2]-m[14]},dl[3]={look[0]-m[12],look[1]-m[13],look[2]-m[14]};
        oe[0]=Dot(de,l);oe[1]=Dot(de,u);oe[2]=Dot(de,f);
        ol[0]=Dot(dl,l);ol[1]=Dot(dl,u);ol[2]=Dot(dl,f);
    }
    // The bank taken out: all but rollShare of it (folded, faded out steep), eased.
    float weight;
    const float bank=WorldBank(l,u,f,&weight);
    const float want=(1.0f-t.rollShare)*Fold(bank)*weight;
    if(start || !c.rolled){c.roll=want;c.rolled=true;}
    else if(dt>0.0f)c.roll+=(want-c.roll)*Share(dt,t.rollTau);
    const float cr=std::cos(-c.roll),sr=std::sin(-c.roll);
    float tl[3],tu[3];
    for(int i=0;i<3;++i){tl[i]=l[i]*cr+u[i]*sr;tu[i]=u[i]*cr-l[i]*sr;}
    const Quat goal=FromRows(tl,tu,f);
    if(start || !c.oriented || Angle(c.orient,goal)>t.snapAngle){c.orient=goal;c.oriented=true;}
    else if(dt>0.0f)c.orient=Slerp(c.orient,goal,Share(dt,t.dirTau));
    float ce[3],cl[3];
    Rotate(c.orient,oe,ce);Rotate(c.orient,ol,cl);
    for(int i=0;i<3;++i){out->eye[i]=at[i]+ce[i];out->look[i]=at[i]+cl[i];}
    // The lead: the path's angle off the nose, lookAhead s of speed out (at most lookAheadMax).
    const float* v=c.anchor.targetVel;
    const float speed=std::sqrt(Dot(v,v));
    if(!start && speed>1.0f && t.lookAhead>0.0f) {
        const float reach=std::fmin(speed*t.lookAhead,t.lookAheadMax);
        for(int i=0;i<3;++i)out->look[i]+=reach*(v[i]/speed-f[i]);
    }
    const float y[3]={0.0f,1.0f,0.0f};
    Rotate(c.orient,y,out->up);
    out->hasUp=true;
    return Finite3(out->eye) && Finite3(out->look) && Finite3(out->up);
}
}  // namespace chase
