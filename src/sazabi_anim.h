// The Sazabi's animation: a stateful animator over sazabi_pose.h's skeleton (sazabi.cpp Pose runs it every frame on every
// machine, the remote copies too: everything it keeps it works out from PoseInput; tools/sazabi_pose_check.cpp runs it
// offline and tools/sazabi_pose_view.py renders what it writes onto the real model).
//
// What it does, in the order Animate does it:
//   the legs: a stance (the source hovers: kStance stands it), steps planned along the way it walks (any way: ahead,
//     aside, back), the hips turned into that way and the chest turned back onto the aim, steps round a turn on the spot,
//     settling steps when it stops, a lean into the run and into a speed change, the crouch on a spring (a landing);
//   the arms: placed by where the wrists go (analytic two-bone IK: SolveArm) and which way the hands and the forearms face,
//     not by angles off the bind (the source's arms hang unevenly: the left forearm points ahead, the right hangs; turning
//     them by angles left the shield lying flat and the tomahawk sideways). Each arm's goal (ArmGoal) is a blend of poses:
//     the rifle aimed / carried / hanging, the shield at the side / up across the chest / turned onto the aim for its
//     missiles, the tomahawk's swings (keys: Melee), the draw (the rifle racked on the hip, the tomahawk pulled off the
//     shield) and the put-away (Stow), the cannon's brace, the boost's swept-back arm;
//   what follows: the shoulder armour with the upper arms, the head onto the aim, the backpack's tanks and the funnel
//     packs swinging on springs, a glance at the special weapon chosen.
// Plain math, no game memory: header only, so the offline check compiles it unchanged.
#pragma once
#include "sazabi_pose.h"

namespace sazabi {
// ------------------------------------------------------------------------------------------ vectors
struct V3 { float x,y,z; };
inline V3 Of(const float* p) { return {p[0],p[1],p[2]}; }
inline void Store(V3 v,float* p) { p[0]=v.x;p[1]=v.y;p[2]=v.z; }
inline V3 operator+(V3 a,V3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline V3 operator-(V3 a,V3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline V3 operator-(V3 a) { return {-a.x,-a.y,-a.z}; }
inline V3 operator*(V3 a,float k) { return {a.x*k,a.y*k,a.z*k}; }
inline float VDot(V3 a,V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline V3 VCross(V3 a,V3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float VLen(V3 a) { return std::sqrt(VDot(a,a)); }
inline V3 VUnit(V3 a,V3 otherwise={0.0f,0.0f,1.0f}) { const float l=VLen(a); return l>1e-6f ? a*(1.0f/l) : otherwise; }
inline V3 VPerp(V3 v,V3 unitAxis) { return v-unitAxis*VDot(v,unitAxis); }
inline V3 VLerp(V3 a,V3 b,float t) { return a+(b-a)*t; }
inline V3 Times(V3 v,const M3& m) {   // v m (row vector)
    return {v.x*m.m[0]+v.y*m.m[3]+v.z*m.m[6],v.x*m.m[1]+v.y*m.m[4]+v.z*m.m[7],v.x*m.m[2]+v.y*m.m[5]+v.z*m.m[8]};
}
inline V3 AnyAcross(V3 a) { return VUnit(std::fabs(a.y)<0.9f ? VCross(a,{0.0f,1.0f,0.0f}) : VCross(a,{1.0f,0.0f,0.0f})); }
inline V3 Row(const M3& m,int r) { return {m.m[r*3],m.m[r*3+1],m.m[r*3+2]}; }

// ------------------------------------------------------------------------------------------ rotations
// The frame whose rows are `ref` made square to `axis`, `axis`, and their cross: what a limb's segment is turned by is
// the turn from its frame at the bind to the one wanted (Turn), its axis along the segment and `ref` the side it bends to.
inline M3 Basis(V3 axis,V3 ref) {
    const V3 a=VUnit(axis);
    V3 r=VPerp(ref,a);
    r=VLen(r)>1e-4f ? VUnit(r) : AnyAcross(a);
    const V3 c=VCross(r,a);
    return {{r.x,r.y,r.z, a.x,a.y,a.z, c.x,c.y,c.z}};
}
inline M3 Turn(V3 axis0,V3 ref0,V3 axis1,V3 ref1) { return Mul(T(Basis(axis0,ref0)),Basis(axis1,ref1)); }
// The rotation whose +z is `fwd` and whose +y is as near `up` as that allows (the hand's: +z the rifle's barrel and the
// tomahawk's edge, +y the rifle's top and the tomahawk's handle toward its blade).
inline M3 Orient(V3 fwd,V3 up) {
    const V3 z=VUnit(fwd);
    V3 y=VPerp(up,z);
    y=VLen(y)>1e-4f ? VUnit(y) : AnyAcross(z);
    const V3 x=VCross(y,z);
    return {{x.x,x.y,x.z, y.x,y.y,y.z, z.x,z.y,z.z}};
}
// The turn `ang` about unit `k` (right-handed), row-vector form.
inline M3 AxisAngle(V3 k,float ang) {
    const float c=std::cos(ang),s=std::sin(ang),t=1.0f-c;
    return T(M3{{c+t*k.x*k.x,t*k.x*k.y-s*k.z,t*k.x*k.z+s*k.y,
                 t*k.y*k.x+s*k.z,c+t*k.y*k.y,t*k.y*k.z-s*k.x,
                 t*k.z*k.x-s*k.y,t*k.z*k.y+s*k.x,c+t*k.z*k.z}});
}
// `t` of the way from rotation `a` to `b` along the shortest turn (slerp).
inline M3 RotLerp(const M3& a,const M3& b,float t) {
    if(t<=0.0f)return a;
    if(t>=1.0f)return b;
    const M3 d=Mul(T(a),b);   // a then d is b
    const float cosA=Clamp((d.m[0]+d.m[4]+d.m[8]-1.0f)*0.5f,-1.0f,1.0f),ang=std::acos(cosA);
    const V3 axis={d.m[5]-d.m[7],d.m[6]-d.m[2],d.m[1]-d.m[3]};
    const float l=VLen(axis);
    if(ang<1e-4f || l<1e-5f) {   // the same (the blend made square), or half a turn apart (no axis: one or the other)
        if(ang>0.5f*kPi)return t<0.5f ? a : b;
        M3 m{};
        for(int k=0;k<9;++k)m.m[k]=a.m[k]*(1.0f-t)+b.m[k]*t;
        return Orthonormal(m);
    }
    return Mul(a,AxisAngle(axis*(1.0f/l),ang*t));
}

// ------------------------------------------------------------------------------------------ springs
// Critically damped: x follows `to` at rate `w` (1/s), no overshoot (exact for any dt).
inline void Follow(float& x,float& v,float to,float w,float dt) {
    const float y=x-to,e=std::exp(-w*dt),k=(v+w*y)*dt;
    x=to+(y+k)*e;
    v=(v-w*k)*e;
}
// A sprung, damped swing (overshoots and settles: armour and tanks on their mounts), stepped in slices of at most 1/120 s.
inline void Sway(float& x,float& v,float to,float stiff,float damp,float dt) {
    for(float left=dt;left>1e-6f;) {
        const float h=left<1.0f/120.0f ? left : 1.0f/120.0f;
        v+=(stiff*(to-x)-damp*v)*h;
        x+=v*h;
        left-=h;
    }
}

// ------------------------------------------------------------------------------------------ the arms' solver
// What an arm is asked for, in sz_root's frame: where its wrist goes (the hand bone's joint), the way its elbow points,
// how its hand is turned (the right one: absolute; the left one follows its forearm), and where a reference fixed in its
// forearm faces (`roll`: the left forearm's the shield's face, ShieldNormal; the right forearm's the hand's top).
struct ArmGoal { V3 wrist; V3 pole; M3 hand; V3 roll; };
inline ArmGoal Blend(const ArmGoal& a,const ArmGoal& b,float t) {
    if(t<=0.0f)return a;
    if(t>=1.0f)return b;
    return {VLerp(a.wrist,b.wrist,t),VUnit(VLerp(a.pole,b.pole,t),b.pole),RotLerp(a.hand,b.hand,t),
            VUnit(VLerp(a.roll,b.roll,t),b.roll)};
}
inline int UpperArm(int side) { return side==0 ? kUpperArmL : kUpperArmR; }   // then its forearm, then its hand
// The shield's face (out of the left forearm, pylib/sazabi_arms.py shield_frame) at the bind: +x made square to the
// forearm.
inline V3 ShieldNormal(const Rig& rig) {
    const V3 f=VUnit(Of(rig.joint[kHandL])-Of(rig.joint[kForearmL]));
    return VUnit(VPerp({1.0f,0.0f,0.0f},f));
}
// Arm `side` (0 left, 1 right) onto `g`: the elbow where the two segments of their bind lengths meet, bent toward the
// pole's side; each segment turned from its bind frame (axis along it, its bending side the model's front: the elbows
// bend forward) onto its posed one. Its chest must be finished (Finish) first.
inline void SolveArm(const Rig& rig,int side,const ArmGoal& g,Pose* p) {
    const int up=UpperArm(side),fo=up+1,ha=up+2;
    Finish(p);
    const V3 s0=Of(rig.joint[up]),e0=Of(rig.joint[fo]),w0=Of(rig.joint[ha]);
    const float l1=VLen(e0-s0),l2=VLen(w0-e0);
    const V3 s=Of(p->modelPos[up]),d=g.wrist-s;
    const V3 dir=VUnit(d,{0.0f,-1.0f,0.0f});
    const float dist=Clamp(VLen(d),std::fabs(l1-l2)+0.01f,(l1+l2)*0.999f);
    const float a=(l1*l1-l2*l2+dist*dist)/(2.0f*dist),h=std::sqrt(std::fmax(0.0f,l1*l1-a*a));
    const V3 pole=VUnit(VPerp(g.pole,dir),AnyAcross(dir));
    const V3 e=s+dir*a+pole*h,w=s+dir*dist;
    const V3 u=VUnit(e-s),f=VUnit(w-e),u0=VUnit(e0-s0),f0=VUnit(w0-e0),front={0.0f,0.0f,1.0f};
    // the bend's side: where the forearm goes off the upper arm (straight: away from the pole, its limit)
    const V3 bend=VUnit(VPerp(f,u)+(-pole)*0.05f,-pole);
    p->rot[up]=Mul(Turn(u0,front,u,bend),T(p->modelRot[kBones[up].parent]));
    Finish(p);
    const V3 rollBind=side==0 ? ShieldNormal(rig) : V3{0.0f,1.0f,0.0f};
    p->rot[fo]=Mul(Turn(f0,rollBind,f,g.roll),T(p->modelRot[up]));
    Finish(p);
    p->rot[ha]=side==0 ? Ident() : Mul(g.hand,T(p->modelRot[fo]));
}

// ------------------------------------------------------------------------------------------ tuning
// The gait. A cycle (two steps) covers kStrideWalk .. kStrideRun m of ground at full amplitude (a leg ~12.9 m from hip
// to sole); each foot is on the ground kDutyWalk .. kDutyRun of it. Slower than kStepFull m/s the steps shorten (and quicken:
// the planted foot keeps pace with the ground) down to kStepLeast of their length. Settling (stopped with a foot out) the
// steps go on at kSettleRate cycles a second while they die away; turning on the spot the hips lag the facing (the feet
// stay put) up to kTurnLag, and past kTurnStep it steps round in kTurnCycle s.
constexpr float kStrideWalk=14.0f,kStrideRun=21.0f,kDutyWalk=0.6f,kDutyRun=0.42f,kLiftWalk=1.0f,kLiftRun=1.7f;
constexpr float kStepFull=6.0f,kStepLeast=0.35f,kSettleRate=1.1f,kTurnLag=40.0f,kTurnStep=18.0f,kTurnCycle=0.85f;
constexpr int kIkIterations=20;
constexpr float kIkStep=12.0f*kDeg,kReach=0.97f,kSway=0.45f,kFootFlat=1.0f,kAbduct=35.0f;
// The hips turn kHipShare of the way into the walk's direction (at most kHipMost; walking back, into its reverse); the
// waist and chest turn back so the chest faces the aim.
constexpr float kHipShare=0.7f,kHipMost=40.0f,kBackOn=110.0f,kBackOff=70.0f;
// Leaning (deg): into the run, standing, per m/s/s of a speed change (at most kAccelLeanMost).
constexpr float kRunLean=10.0f,kWalkLean=3.0f,kStandLean=4.0f,kAccelLean=0.5f,kAccelLeanMost=9.0f;
// The standing crouch, the shield's, the boost's lean, the cannon's brace (each foot kCannonStep m ahead / behind, the
// chest back kCannonBack deg), the rifle's kick (muzzle kRecoilMuzzle deg up, the wrist kRecoilBack m back).
constexpr float kStandCrouch=0.1f,kGuardCrouch=0.4f,kBoostLean=35.0f,kCannonStep=2.5f,kCannonBack=8.0f;
constexpr float kRecoilMuzzle=5.0f,kRecoilBack=0.9f;
constexpr float kChestShare=0.5f;   // of the aim's yaw the chest takes (the rest the arm)
constexpr float kMostAimYaw=60.0f,kMostAimPitch=55.0f;
constexpr float kGuardTurn=-14.0f;  // deg the chest turns with the shield up (to its right: the shield's side ahead)
constexpr float kArmSwing=1.7f;     // m the free wrist swings ahead / back with the opposite leg
// The tomahawk: held ready kAxeHold s after a swing, then put away in kStowSec (kStowQuick for the rifle's trigger);
// the hand holds it kAxeGrip m from its butt. Its draw takes the first kDrawShare of the first swing's wind-up: the hand
// racks the rifle on the hip (kDrawRack of the draw) and takes the tomahawk off the shield.
constexpr float kAxeHold=1.6f,kStowSec=0.6f,kStowQuick=0.32f,kAxeGrip=1.6f,kDrawShare=0.6f,kDrawRack=0.45f;
// The rifle's put-away: the tomahawk back on the shield at kStowShield of it, the rifle in hand at kStowRifle.
constexpr float kStowShield=0.4f,kStowRifle=0.75f;
// The fist's centre from the right wrist, in the hand's own frame (the model's hand: its pieces' centre).
inline constexpr float kFist[3]={-0.26f,-0.74f,-0.19f};
// Where the rifle is racked, in the pelvis's frame from its joint (the right rear skirt's outside), and which way its
// barrel and its top face there.
inline constexpr float kRack[3]={-5.4f,-0.6f,-2.6f},kRackBarrel[3]={0.08f,-0.92f,-0.38f},kRackTop[3]={-1.0f,0.0f,0.0f};

// Arm poses: wrists from the shoulder (the upper arm's joint) in the chest's frame (x left, y up, z ahead), m; directions
// in the chest's frame.
struct HandPose { float wrist[3]; float fwd[3]; float up[3]; float pole[3]; };
inline constexpr HandPose kRifleCarry={{-1.0f,-4.3f,2.6f},{-0.12f,-0.45f,0.88f},{0.0f,1.0f,0.3f},{-1.0f,-0.3f,-1.0f}};
inline constexpr HandPose kRifleHang={{-0.9f,-5.0f,1.3f},{-0.1f,-0.8f,0.6f},{0.0f,0.6f,0.8f},{-0.5f,0.0f,-1.0f}};
inline constexpr HandPose kRifleBrace={{-3.0f,-3.2f,0.6f},{-0.3f,-0.6f,0.75f},{0.0f,1.0f,0.0f},{-0.2f,-1.0f,-1.0f}};
constexpr float kAimReach=4.7f;     // m the right wrist goes out along the aim from the shoulder
inline constexpr float kAimOff[3]={-0.6f,-1.1f,0.0f},kAimPole[3]={-1.0f,-0.8f,-0.4f};
// The left arm: its wrist, the way the shield faces (`fwd`), the elbow's way.
inline constexpr HandPose kShieldSide={{1.3f,-3.9f,2.0f},{1.0f,0.1f,-0.2f},{0,0,0},{1.0f,-0.3f,-1.0f}};
// The shield held up in front (ShieldFront): its face can only look square to the forearm, so the forearm lies across the
// chest, rising toward the right: the wrist kFrontAhead m ahead of the shoulder along the face, kFrontAcross m across to
// the right of it and kFrontUp m up (less `drop`), the elbow out and down. Up (guard) its face a little to the right of
// the chest's ahead; for its missiles (present) onto the aim; for the draw and the put-away low, before the belly, its
// inner face (where the tomahawk is stowed) to the right hand.
constexpr float kFrontAhead=2.1f,kFrontAcross=2.0f,kFrontUp=0.3f,kDrawDrop=1.8f;
inline constexpr float kGuardFace[3]={-0.1f,0.05f,1.0f},kDrawFace[3]={0.0f,-0.35f,1.0f};
inline constexpr HandPose kShieldBoost={{1.2f,-3.7f,-1.6f},{1.0f,0.0f,0.0f},{0,0,0},{0.6f,0.0f,-1.0f}};
inline constexpr HandPose kShieldBrace={{3.0f,-3.0f,0.6f},{1.0f,0.0f,0.0f},{0,0,0},{0.0f,-1.0f,-1.0f}};
constexpr float kMeleeGuard=0.35f;  // of the guard the shield arm holds while the tomahawk is out
// A hit the shield stops (PoseInput::blocks): its kick's impulse (1/s into the sprung jolt, ~0.5 at its height) and what
// the jolt does at 1: the shield's wrist kBlockBack m back toward the chest, the chest kBlockLean deg back, the knees in.
constexpr float kBlockKick=14.0f,kBlockBack=1.6f,kBlockLean=5.0f,kBlockCrouch=0.25f;
// The shield raised for its missiles (ShieldRaise): out at the left front, the forearm upright, its face onto the aim;
// apart from the guard's (across the chest, crouched: the user, 2026-10-09 「举盾和右键攻击重叠了」): the wrist kRaiseAhead
// m ahead of the shoulder, kRaiseOut out to the left, kRaiseUp up.
// (the forearm upright: the elbow ahead of the shoulder and a little under it, the wrist its forearm's length over it)
constexpr float kRaiseAhead=2.0f,kRaiseOut=1.2f,kRaiseUp=2.5f;

// The tomahawk's swings: keys over a swing's u (0 .. 1; WindEnd and StrikeEnd are the wind-up's and the strike's ends):
// the right wrist (as HandPose), the hand's +z (the edge, leading the cut) and +y (the handle toward the blade); the chest's
// turn (deg, + left), the lean (deg ahead), the step (0 .. 1 of kLunge: the left foot ahead, the pelvis after it), the crouch.
struct MeleeKey { float wrist[3]; float edge[3]; float up[3]; float twist,lean,lunge,crouch; };
constexpr float kLunge=3.0f;
// held ready between swings: up before it, edge ahead
inline constexpr MeleeKey kAxeReady={{-1.2f,-2.5f,3.0f},{0.0f,-0.52f,0.85f},{0.05f,0.85f,0.52f},0.0f,2.0f,0.0f,0.1f};
// each swing: wind-up's end, mid-strike, strike's end (then back to kAxeReady)
inline constexpr MeleeKey kSwingKeys[3][3]={
    {   // the diagonal cut: from over the right shoulder down across to the left hip
        {{-2.0f,2.2f,-0.4f},{0.05f,0.85f,0.52f},{-0.3f,0.5f,-0.81f},-35.0f,-4.0f,0.0f,0.0f},
        {{-0.5f,0.6f,4.6f},{0.2f,-0.88f,0.42f},{0.12f,0.45f,0.88f},5.0f,8.0f,0.6f,0.2f},
        {{1.8f,-2.6f,3.4f},{0.45f,-0.45f,-0.7f},{0.5f,-0.55f,0.67f},30.0f,12.0f,1.0f,0.4f},
    },
    {   // the slash across: right to left at the chest's height
        {{-3.4f,0.2f,0.4f},{-0.48f,0.0f,0.88f},{-0.85f,0.2f,-0.48f},-45.0f,0.0f,0.0f,0.1f},
        {{-0.2f,-0.4f,4.9f},{1.0f,0.0f,0.0f},{0.0f,0.15f,0.99f},0.0f,6.0f,0.7f,0.25f},
        {{3.0f,-0.7f,2.4f},{0.52f,0.0f,-0.85f},{0.85f,0.05f,0.52f},40.0f,8.0f,1.0f,0.3f},
    },
    {   // the overhead chop: from behind the head straight down ahead
        {{-0.5f,3.4f,-0.6f},{0.0f,0.95f,0.3f},{0.0f,0.3f,-0.95f},-10.0f,-10.0f,0.0f,0.0f},
        {{-0.3f,0.9f,4.6f},{0.0f,-0.95f,0.3f},{0.0f,0.3f,0.95f},0.0f,12.0f,0.8f,0.4f},
        {{0.2f,-3.2f,3.6f},{0.0f,-0.71f,-0.7f},{0.0f,-0.7f,0.71f},5.0f,20.0f,1.0f,0.8f},
    },
};

// ------------------------------------------------------------------------------------------ the animator's state
struct Anim {
    bool started=false;
    // the gait
    float phase=0.0f;            // rad: the steps' cycle
    float amp=0.0f,ampV=0.0f;    // 0 .. 1 the steps' amplitude
    float run=0.0f,runV=0.0f;    // 0 walk .. 1 run
    float dir[2]={0.0f,1.0f};    // unit, sz_root's x and z: the way the steps go
    float hip=0.0f,hipV=0.0f;    // rad: the hips' turn into the walk's direction
    float lag=0.0f;              // rad: the hips' lag behind a turn on the spot
    float turning=0.0f;          // s left of stepping round
    float settle=0.0f;           // cycles a second the steps go on at while they die away
    float speed=0.0f,accel=0.0f; // m/s along the walk, its change (m/s/s, eased)
    float leanF=0.0f,leanFV=0.0f,leanS=0.0f,leanSV=0.0f;   // rad ahead and to the left
    float crouch=0.0f,crouchV=0.0f;
    float still=0.0f;            // s standing still (the idle's weight shifts)
    bool planted[2]={true,true};
    bool backward=false;         // walking back (the hips turned into the reverse of the way), held past kBackOn/kBackOff
    // the arms
    float raise=0.0f,raiseV=0.0f;     // the rifle up onto the aim
    float ready=0.0f,readyV=0.0f;     // 0 .. 1 driven (the arms held ready, not hanging)
    float guard=0.0f,guardV=0.0f;
    float present=0.0f,presentV=0.0f; // the shield turned onto the aim (its missiles)
    float boost=0.0f,boostV=0.0f;
    float brace=0.0f,braceV=0.0f;
    bool axeOut=false;           // the tomahawk off the shield (in the hand, or about to be put back: stow)
    bool drawing=false;          // this swing's wind-up draws it (the rifle racked first)
    float drawFrom=0.0f;         // ...from this far into the draw (kDrawRack: the rifle already racked, a put-away cut short)
    float held=0.0f;             // s since its last swing
    float stow=-1.0f;            // < 0 not putting it away; 0 .. 1 putting it back and taking the rifle
    float swingWas=-1.0f;
    int comboWas=0;
    float melee=0.0f,meleeV=0.0f;// 0 .. 1 the body in the tomahawk's swings (eased in and out)
    float twist=0.0f,lean=0.0f,lunge=0.0f,cut=0.0f;   // the swing's body keys as they are now
    int stage=0;                 // the right arm's goal's source (Arms: aim, draw, swing, ready, put-away)
    float fade=1.0f;             // 0 .. 1 the right arm's crossfade from where it was when its source changed (kFade s)
    ArmGoal fadeFrom{},right{};  // ...from that goal; the goal it was last given
    bool rightSet=false;
    // what follows
    float headYaw=0.0f,headYawV=0.0f,headPitch=0.0f,headPitchV=0.0f;
    float tank=0.0f,tankV=0.0f,pack=0.0f,packV=0.0f;
    float glance=0.0f;           // s left of the glance at the special weapon just chosen
    int special=-1;
    float pulse=0.0f;            // 0 .. 1 the funnel packs thrown open (a funnel launched)
    float kick=0.0f,kickV=0.0f;  // the shield knocked back by a hit it stopped (a sprung jolt: kBlockKick)
    int blocksSeen=0;
    bool funnelWas[6]{};
    float clock=0.0f;
};

// What the animator lets the weapons do (sazabi_arms.inc): the rifle in hand and up (its rounds leave along the aim),
// the shield turned onto the aim (its missiles leave it ahead).
inline bool RifleReady(const Anim& a) { return !a.axeOut && a.stow<0.0f && a.raise>0.85f && a.brace<0.15f && a.fade>=1.0f; }
inline bool ShieldReady(const Anim& a) { return !a.axeOut && a.present>0.8f; }

// A swing's keys: kAxeReady (or the draw's grab, at `fromU`), its wind-up's end, mid-strike, its strike's end, kAxeReady
// again; which of the four parts `u` is in and how far through it (eased: the wind-up smooth, the strike accelerating
// through, the recovery easing out).
inline void SwingPart(int combo,float u,float fromU,int* part,float* t,float at[5]) {
    at[0]=fromU;at[1]=WindEnd(combo);at[2]=0.5f*(WindEnd(combo)+StrikeEnd(combo));at[3]=StrikeEnd(combo);at[4]=1.0f;
    int i=0;
    while(i<3 && u>=at[i+1])++i;
    const float span=at[i+1]-at[i];
    float x=span>1e-5f ? Clamp((u-at[i])/span,0.0f,1.0f) : 1.0f;
    x=i==0 ? Smooth(x) : i==1 ? x*x : i==2 ? 1.0f-(1.0f-x)*(1.0f-x) : Smooth(x);
    *part=i;*t=x;
}
inline const MeleeKey& SwingKey(int combo,int k) { return k==0 || k==4 ? kAxeReady : kSwingKeys[combo][k-1]; }
inline int ComboOf(int c) { return c<0 ? 0 : c>2 ? 2 : c; }
inline float DrawEnd() { return kDrawShare*WindEnd(0); }

// ------------------------------------------------------------------------------------------ stepping the state
inline float Wrap(float a) { while(a>kPi)a-=2.0f*kPi; while(a<-kPi)a+=2.0f*kPi; return a; }
inline float StrideLength(float run) { return kStrideWalk+(kStrideRun-kStrideWalk)*Clamp(run,0.0f,1.0f); }
inline float Duty(float run) { return kDutyWalk+(kDutyRun-kDutyWalk)*Clamp(run,0.0f,1.0f); }
inline float Lift(float run) { return kLiftWalk+(kLiftRun-kLiftWalk)*Clamp(run,0.0f,1.0f); }
// The cycle's length (m) at the steps' amplitude now: shorter strides when slow, and stepping aside (the hips only
// spread so far: long side steps sank the pelvis 2.3 m to reach them) kSideStep of them.
constexpr float kSideStep=0.55f;
inline float Cycle(const Anim& a) {
    return StrideLength(a.run)*(kStepLeast+(1.0f-kStepLeast)*a.amp)*(1.0f-(1.0f-kSideStep)*std::fabs(a.dir[0]));
}
inline FootPlan StepOf(const Anim& a,int side) { return Plan(a.phase,Cycle(a)*Duty(a.run)*a.amp,Duty(a.run),Lift(a.run)*a.amp,side); }

// The gait's share of a frame: its direction, speed, hips, the turn on the spot, the phase.
inline void StepGait(const PoseInput& in,float runSpeed,float dt,Anim& a) {
    const float feet=1.0f-in.air;
    const float vx=in.move[0],vz=in.move[1],speed=std::sqrt(vx*vx+vz*vz)*feet;
    const float was=a.speed;
    a.speed=speed;
    a.accel+=((dt>0.0f ? (speed-was)/dt : 0.0f)-a.accel)*std::fmin(1.0f,6.0f*dt);
    if(speed>0.5f){a.dir[0]=vx/(speed/feet);a.dir[1]=vz/(speed/feet);}
    Follow(a.amp,a.ampV,Clamp(speed/kStepFull,0.0f,1.0f),6.0f,dt);
    Follow(a.run,a.runV,Clamp((speed-0.5f*runSpeed*0.46f)/(runSpeed*0.77f),0.0f,1.0f),4.0f,dt);
    // the hips into the walk (walking back: into its reverse), held level standing
    const float way=std::atan2(a.dir[0],a.dir[1]);
    if(speed>1.0f)a.backward=std::fabs(way)>(a.backward ? kBackOff : kBackOn)*kDeg;   // held: no flapping across the side
    const float back=a.backward ? Wrap(way-kPi) : way;
    const float hipWant=speed>1.0f ? Clamp(back*kHipShare,-kHipMost*kDeg,kHipMost*kDeg) : 0.0f;
    // the turn on the spot: the feet stay where they are while the body turns (the hips lag), until it steps round
    if(feet>0.5f && speed<1.0f) {
        a.lag=Clamp(a.lag-in.yawRate*dt,-kTurnLag*kDeg,kTurnLag*kDeg);
        if(a.turning<=0.0f && std::fabs(a.lag)>kTurnStep*kDeg)a.turning=kTurnCycle;
    }
    if(a.turning>0.0f || speed>=1.0f || feet<=0.5f)a.lag*=std::exp(-(a.turning>0.0f ? 4.5f : 6.0f)*dt);
    a.turning=std::fmax(0.0f,a.turning-dt);
    Follow(a.hip,a.hipV,hipWant,6.0f,dt);
    // the phase: the planted foot keeps pace with the ground; stopped, the steps go on while they die away; turning, a
    // cycle of steps in place
    // (the planted foot goes back Cycle x amp a cycle: the cycles a second that make that the ground's speed)
    const float pace=speed/(Cycle(a)*std::fmax(a.amp,0.2f));
    if(speed>1.0f)a.settle=std::fmax(kSettleRate,pace);
    else a.settle*=std::exp(-2.0f*dt);
    float rate=speed>1.0f ? pace : (a.amp>0.05f ? a.settle : 0.0f);
    if(a.turning>0.0f)rate=std::fmax(rate,1.0f/kTurnCycle);
    a.phase+=2.0f*kPi*rate*dt;
    if(a.phase>200.0f*kPi)a.phase-=200.0f*kPi;
    if(a.turning>0.0f && speed<1.0f)a.amp=std::fmax(a.amp,0.25f);   // lifted to step round
    for(int side=0;side<2;++side)a.planted[side]=StepOf(a,side).stance || feet<0.5f;
    a.still=speed<0.5f && a.turning<=0.0f ? a.still+dt : 0.0f;
}

// The tomahawk drawn, swung, held ready and put away; the rifle racked and taken back (sazabi_arms.inc times the swings).
inline void StepWeapons(const PoseInput& in,float dt,Anim& a) {
    const bool swinging=in.swing>=0.0f;
    if(swinging && a.swingWas<0.0f) {   // a combo begins: drawn unless the tomahawk is still in hand
        const bool inHand=a.axeOut && (a.stow<0.0f || a.stow<kStowShield);
        a.drawing=!inHand;
        a.drawFrom=a.axeOut && a.stow>=kStowShield && a.stow<kStowRifle ? kDrawRack : 0.0f;   // the rifle still racked
        a.axeOut=true;a.stow=-1.0f;
    }
    // the next swing of the combo: drawn already (a combo's first swing is 0, whatever the last combo ended on)
    if(swinging && a.swingWas>=0.0f && in.combo!=a.comboWas)a.drawing=false;
    // a shot (the trigger here, its kick on a remote copy: the trigger is not sent) puts the tomahawk away at once
    const bool shooting=in.fire || in.recoil>0.0f;
    if(swinging){a.held=0.0f;}
    else if(a.axeOut) {
        a.held+=dt;
        if(a.stow<0.0f && (a.held>kAxeHold || shooting || in.present || in.aim<0.5f))a.stow=0.0f;
        if(a.stow>=0.0f) {
            a.stow+=dt/(shooting ? kStowQuick : kStowSec);
            if(a.stow>=1.0f){a.stow=-1.0f;a.axeOut=false;a.drawing=false;}
        }
    }
    a.swingWas=in.swing;a.comboWas=in.combo;
    Follow(a.melee,a.meleeV,swinging || (a.axeOut && a.stow<0.0f) ? 1.0f : 0.0f,10.0f,dt);
    // the body's keys (the arm's are Arms'): none while the draw reaches for the tomahawk, the ready stance's held
    a.twist=a.lean=a.lunge=a.cut=0.0f;
    const bool draw=a.drawing && in.combo==0;
    if(swinging && !(draw && in.swing<DrawEnd())) {
        const int c=ComboOf(in.combo);
        int i=0;
        float t=0.0f,at[5];
        SwingPart(c,in.swing,draw ? DrawEnd() : 0.0f,&i,&t,at);
        const MeleeKey& k0=SwingKey(c,i);
        const MeleeKey& k1=SwingKey(c,i+1);
        const float from=draw && i==0 ? 0.0f : 1.0f;   // from the draw's grab: no stance of its own yet
        a.twist=k0.twist*from+(k1.twist-k0.twist*from)*t;a.lean=k0.lean*from+(k1.lean-k0.lean*from)*t;
        a.lunge=k0.lunge+(k1.lunge-k0.lunge)*t;a.cut=k0.crouch+(k1.crouch-k0.crouch)*t;
    } else if(!swinging && a.axeOut && a.stow<0.0f) {
        a.twist=kAxeReady.twist;a.lean=kAxeReady.lean;a.cut=kAxeReady.crouch;
    }
}

inline void Step(const PoseInput& in,float runSpeed,float dt,Anim& a) {
    if(!a.started){a=Anim{};a.started=true;a.ready=in.aim;}
    dt=Clamp(dt,0.0f,0.1f);
    a.clock+=dt;
    StepGait(in,runSpeed,dt,a);
    StepWeapons(in,dt,a);
    const float feet=1.0f-in.air;
    a.ready=Clamp(in.aim,0.0f,1.0f);   // eased already (sazabi.cpp Animate)
    // the rifle up on the aim whenever it is in hand and driven (a dash too: lowered, the dash's lean pointed it at the
    // ground); carried low only while it comes off or goes back on the rack
    const bool inHand=!a.axeOut;
    const float up=inHand ? 1.0f : 0.0f;
    Follow(a.raise,a.raiseV,up*a.ready,in.fire ? 18.0f : 9.0f,dt);
    Follow(a.guard,a.guardV,in.guard,in.guard>a.guard ? 14.0f : 8.0f,dt);
    Follow(a.present,a.presentV,in.present && inHand ? 1.0f : 0.0f,16.0f,dt);
    Follow(a.boost,a.boostV,in.boost,8.0f,dt);
    Follow(a.brace,a.braceV,in.cannon,9.0f,dt);
    const float crouch=Clamp(in.crouch+(kStandCrouch+kGuardCrouch*a.guard+0.6f*a.cut*a.melee)*feet,0.0f,1.0f);
    Follow(a.crouch,a.crouchV,crouch,in.crouch>a.crouch ? 22.0f : 7.0f,dt);
    // the lean: into the run and into a speed change, along the walk's way
    const float lean=(kStandLean+kWalkLean*a.amp+(kRunLean-kWalkLean)*a.run*a.amp+
                      Clamp(a.accel*kAccelLean,-kAccelLeanMost,kAccelLeanMost))*kDeg*feet;
    Follow(a.leanF,a.leanFV,lean*(a.speed>1.0f ? a.dir[1] : 1.0f),7.0f,dt);
    Follow(a.leanS,a.leanSV,lean*(a.speed>1.0f ? a.dir[0] : 0.0f)*0.6f,7.0f,dt);
    // the glance at a special weapon chosen, the funnel packs thrown open by a launch
    if(in.special!=a.special){if(a.special>=0)a.glance=0.7f;a.special=in.special;}
    a.glance=std::fmax(0.0f,a.glance-dt);
    bool launched=false;
    for(int k=0;k<6;++k){launched|=in.funnelOut[k] && !a.funnelWas[k];a.funnelWas[k]=in.funnelOut[k];}
    if(launched)a.pulse=1.0f;
    a.pulse=std::fmax(0.0f,a.pulse-1.6f*dt);
    // a hit stopped on the shield: a jolt back through the arm and the body, sprung back
    if(in.blocks!=a.blocksSeen){a.kickV+=kBlockKick;a.blocksSeen=in.blocks;}
    Sway(a.kick,a.kickV,0.0f,140.0f,16.0f,dt);
    // the tanks and the packs swing with the speed's change and the climb
    Sway(a.tank,a.tankV,Clamp(-a.accel*0.012f,-0.35f,0.35f)+0.2f*in.air,60.0f,7.0f,dt);
    Sway(a.pack,a.packV,Clamp(-a.accel*0.006f,-0.2f,0.2f),90.0f,9.0f,dt);
}

// ------------------------------------------------------------------------------------------ the pose: legs and body
struct ChestFrame { V3 at; M3 rot; };
inline V3 Place(const ChestFrame& c,const float* off) { return c.at+Times(Of(off),c.rot); }
inline V3 Dir(const ChestFrame& c,const float* d) { return VUnit(Times(Of(d),c.rot)); }
inline ChestFrame Shoulder(const Pose& p,int side) { return {Of(p.modelPos[UpperArm(side)]),p.modelRot[kChest]}; }

// The stance and the crouch, the steps planned: each ankle and foot as it stands (turned with the hips), each step's
// offset along the walk's way, the pelvis's sway. Gait walks them.
inline void Legs(const PoseInput& in,const Anim& a,Pose* p) {
    const float feet=1.0f-in.air,cr=Clamp(a.crouch+kBlockCrouch*std::fmax(a.kick,0.0f)*feet,0.0f,1.0f);
    for(int side=0;side<2;++side) {
        const Stance& k=kStance[side];
        const float thigh=kCrouchThigh*cr,knee=kCrouchKnee*cr,foot=-kFootFlat*(thigh+knee);
        const int t=side==0 ? kThighL : kThighR;
        p->rot[t]=Mul(Mul(RotX(k.thighX*feet*kDeg),RotZ(k.thighZ*feet*kDeg)),RotX((thigh+kBoostThigh*a.boost*in.air)*kDeg));
        p->kneeStand[side]=(k.knee*feet+knee+kBoostKnee*a.boost*in.air)*kDeg;
        p->rot[t+1]=RotX(p->kneeStand[side]);
        p->rot[t+2]=Mul(RotX(foot*kDeg),Mul(RotX(k.footX*feet*kDeg),RotZ(k.footZ*feet*kDeg)));
    }
    p->pos[kPelvis][1]+=-kCrouchDrop*cr-kStanceDrop*feet;
    Finish(p);
    const float hip=a.hip+a.lag;
    const M3 turn=RotY(hip);
    const V3 pelvis=Of(p->modelPos[kPelvis]);
    p->gaitW=a.amp*feet;
    // the swing's step only standing: walking, the steps carry it on (on top of a step out ahead it is out of the leg's reach)
    const float lunge=a.lunge*kLunge*feet*(1.0f-a.amp);
    for(int side=0;side<2;++side) {
        const int f=side==0 ? kFootL : kFootR;
        Store(pelvis+Times(Of(p->modelPos[f])-pelvis,turn),p->ankleStand[side]);
        p->footStand[side]=Mul(p->modelRot[f],turn);
        const FootPlan plan=StepOf(a,side);
        const float half=0.5f*Cycle(a)*Duty(a.run)*a.amp;
        const float dz=plan.dz*feet;
        p->footDx[side]=a.dir[0]*dz;
        p->footDz[side]=a.dir[1]*dz+(side==0 ? kCannonStep : -kCannonStep)*a.brace*feet+(side==0 ? lunge : -0.3f*lunge);
        p->footLift[side]=plan.lift*feet;
        p->footToe[side]=plan.toe*a.amp*feet;
        p->legFore[side]=half>0.01f ? plan.dz/half*p->gaitW : 0.0f;
    }
    // over the left foot at the middle of its stance (+x: the left), over the right half a cycle on; standing a while,
    // the weight shifts from foot to foot now and then
    float u=a.phase/(2.0f*kPi);
    u-=std::floor(u);
    const float idle=Smooth((a.still-1.5f)/2.0f)*0.35f*std::sin(a.clock*0.45f);
    p->sway=(kSway*p->gaitW*std::cos(2.0f*kPi*(u-0.5f*Duty(a.run)))+idle)*feet;
}

// The steps onto the legs (after the torso's lean): the pelvis over the carrying foot and low enough for the longest step,
// then each leg's thigh and knee turned (least squares, Gauss-Newton) so its ankle is where its step puts it, and its
// foot flat on the ground (its standing rotation) but for the swung toe.
inline void Gait(const PoseInput& in,Pose* p) {
    const float feet=1.0f-in.air;
    if(!(feet>0.0f))return;
    float target[2][3];
    for(int side=0;side<2;++side) {
        target[side][0]=p->ankleStand[side][0]+p->footDx[side];
        target[side][1]=p->ankleStand[side][1]+p->footLift[side];
        target[side][2]=p->ankleStand[side][2]+p->footDz[side];
    }
    p->pos[kPelvis][0]+=p->sway;
    Finish(p);
    float down=0.0f;
    for(int side=0;side<2;++side) {
        const int t=side==0 ? kThighL : kThighR;
        float straight[3];
        AnkleWith(*p,side,0.0f,-p->kneeStand[side],0.0f,straight);
        const float e[3]={straight[0]-p->modelPos[t][0],straight[1]-p->modelPos[t][1],straight[2]-p->modelPos[t][2]};
        const float reach=kReach*std::sqrt(e[0]*e[0]+e[1]*e[1]+e[2]*e[2]);
        const float d[3]={target[side][0]-p->modelPos[t][0],target[side][1]-p->modelPos[t][1],target[side][2]-p->modelPos[t][2]};
        const float level=reach*reach-d[0]*d[0]-d[2]*d[2];
        const float need=-d[1]-std::sqrt(level>0.0f ? level : 0.0f);
        down=need>down ? need : down;
    }
    p->pos[kPelvis][1]-=down;
    Finish(p);
    for(int side=0;side<2;++side) {
        // q = (thigh forward/back, knee, thigh out/in); damped Gauss-Newton on the ankle's miss, each step at most kIkStep,
        // from the knee the hip-to-ankle distance asks for (law of cosines) and the thigh forward half of it: started
        // straight, a foot lifted high under the hip (a run's swing) went to the wrong fold, thigh kicked out and knee
        // jammed, the ankle metres behind
        const float lo[3]={-80.0f*kDeg,-p->kneeStand[side],-kAbduct*kDeg},hi[3]={60.0f*kDeg,130.0f*kDeg-p->kneeStand[side],kAbduct*kDeg};
        float q[3]={0.0f,0.0f,0.0f};
        {
            const int t=side==0 ? kThighL : kThighR;
            const float l1=std::sqrt(p->pos[t+1][0]*p->pos[t+1][0]+p->pos[t+1][1]*p->pos[t+1][1]+p->pos[t+1][2]*p->pos[t+1][2]);
            const float l2=std::sqrt(p->pos[t+2][0]*p->pos[t+2][0]+p->pos[t+2][1]*p->pos[t+2][1]+p->pos[t+2][2]*p->pos[t+2][2]);
            float now[3];
            AnkleWith(*p,side,0.0f,0.0f,0.0f,now);
            const V3 hip=Of(p->modelPos[t]);
            const auto bend=[&](float d){return kPi-std::acos(Clamp((l1*l1+l2*l2-d*d)/(2.0f*l1*l2),-1.0f,1.0f));};
            const float more=bend(VLen(Of(target[side])-hip))-bend(VLen(Of(now)-hip));
            q[1]=Clamp(more,lo[1],hi[1]);
            q[0]=Clamp(-0.5f*q[1],lo[0],hi[0]);
        }
        for(int it=0;it<kIkIterations;++it) {
            float at[3],j[3][3],r[3];
            AnkleWith(*p,side,q[0],q[1],q[2],at);
            for(int c=0;c<3;++c) {
                constexpr float h=1e-3f;
                float d[3]={q[0],q[1],q[2]},b[3];
                d[c]+=h;
                AnkleWith(*p,side,d[0],d[1],d[2],b);
                for(int k=0;k<3;++k)j[c][k]=(b[k]-at[k])/h;
            }
            for(int k=0;k<3;++k)r[k]=target[side][k]-at[k];
            float m[3][3],g[3];   // (J^T J + damping) step = J^T r
            for(int x=0;x<3;++x) {
                g[x]=j[x][0]*r[0]+j[x][1]*r[1]+j[x][2]*r[2];
                for(int y=0;y<3;++y)m[x][y]=j[x][0]*j[y][0]+j[x][1]*j[y][1]+j[x][2]*j[y][2]+(x==y ? 1e-3f : 0.0f);
            }
            const float det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+
                            m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
            if(!(std::fabs(det)>1e-12f))break;
            for(int c=0;c<3;++c) {   // Cramer: column c replaced by g
                float n[3][3];
                for(int x=0;x<3;++x)for(int y=0;y<3;++y)n[x][y]=y==c ? g[x] : m[x][y];
                const float dc=n[0][0]*(n[1][1]*n[2][2]-n[1][2]*n[2][1])-n[0][1]*(n[1][0]*n[2][2]-n[1][2]*n[2][0])+
                               n[0][2]*(n[1][0]*n[2][1]-n[1][1]*n[2][0]);
                q[c]=Clamp(q[c]+Clamp(dc/det,-kIkStep,kIkStep),lo[c],hi[c]);
            }
        }
        const int t=side==0 ? kThighL : kThighR;
        p->rot[t]=Mul(p->rot[t],ThighTurn(q[0],q[2]));
        p->rot[t+1]=Mul(RotX(q[1]),p->rot[t+1]);
    }
    Finish(p);
    for(int side=0;side<2;++side) {   // the foot flat as it stands (the ground's frame), its toe up while swung
        const int f=side==0 ? kFootL : kFootR;
        const M3 want=Mul(RotX(-p->footToe[side]*kDeg),p->footStand[side]);
        p->rot[f]=Mul(RotLerp(p->modelRot[f],want,feet),T(p->modelRot[kBones[f].parent]));
    }
}

// The aim's direction in sz_root's frame (its yaw and pitch held to what the arm reaches), the kick's lift added.
inline V3 AimDir(const PoseInput& in,float kickDeg) {
    const float yaw=Clamp(in.aimYaw,-kMostAimYaw*kDeg,kMostAimYaw*kDeg);
    const float pitch=Clamp(in.aimPitch,-kMostAimPitch*kDeg,kMostAimPitch*kDeg)+kickDeg*kDeg;
    return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
}

// The hips, waist, chest and head: the hips into the walk, the lean along it, the waist and chest turned back onto the
// aim (and into a swing, away from the shield), the head after the aim (and a glance at a weapon chosen).
inline void Torso(const PoseInput& in,float dt,Anim& a,Pose* p) {
    const float hip=a.hip+a.lag;
    const float twist=7.0f*kDeg*0.5f*(p->legFore[0]-p->legFore[1]);   // the shoulders against the hips
    const float aimYaw=Clamp(in.aimYaw,-kMostAimYaw*kDeg,kMostAimYaw*kDeg)*kChestShare*a.ready*(1.0f-a.melee);
    const float breathe=0.6f*std::sin(a.clock*1.3f)*kDeg;
    const float b=a.boost;
    const float leanF=(a.leanF+in.lean+a.lean*kDeg*a.melee)*(1.0f-b)+kBoostLean*kDeg*b;
    p->rot[kPelvis]=Mul(RotY(hip),Mul(RotZ(-in.bank-a.leanS),RotX(leanF)));
    const float chestYaw=aimYaw+(kGuardTurn*a.guard+a.twist*a.melee)*kDeg;   // where the chest faces, from the facing
    p->rot[kWaist]=RotY(-hip*0.6f+twist+chestYaw*0.4f);
    p->rot[kChest]=Mul(RotX(breathe-kCannonBack*kDeg*a.brace-kBlockLean*kDeg*a.kick),RotY(-hip*0.4f+chestYaw*0.6f));
    // the head: onto the aim's remainder, a glance at the weapon chosen (0 the shield's missiles: the left; 1 the
    // funnels: up; 2 the chest cannon: down)
    float yaw=Clamp(in.aimYaw*a.ready-chestYaw,-45.0f*kDeg,45.0f*kDeg),pitch=Clamp(in.aimPitch*a.ready,-30.0f*kDeg,30.0f*kDeg)*0.5f;
    if(a.glance>0.0f) {
        const float g=Smooth(a.glance/0.2f)*Smooth((0.7f-a.glance)/0.15f);
        const float gy[3]={40.0f,0.0f,0.0f},gp[3]={-15.0f,25.0f,-30.0f};
        const int s=a.special<0 ? 0 : a.special>2 ? 2 : a.special;
        yaw+=gy[s]*kDeg*g;pitch+=gp[s]*kDeg*g;
    }
    Follow(a.headYaw,a.headYawV,yaw,10.0f,dt);
    Follow(a.headPitch,a.headPitchV,pitch,10.0f,dt);
    p->rot[kHead]=Mul(RotX(-a.headPitch),RotY(a.headYaw));
    // the backpack's tanks flare up in flight and swing with a speed's change; the funnel packs open for a launch
    const float flare=12.0f*in.air*kDeg+a.tank;
    p->rot[kTubeL]=RotX(-flare);p->rot[kTubeR]=RotX(-flare);
    const float open=(25.0f*a.pulse+(a.glance>0.0f && a.special==1 ? 12.0f*Smooth(a.glance/0.2f) : 0.0f))*kDeg;
    p->rot[kFunnelPackL]=Mul(RotX(-open+a.pack),RotZ(0.4f*open));
    p->rot[kFunnelPackR]=Mul(RotX(-open+a.pack),RotZ(-0.4f*open));
}

// ------------------------------------------------------------------------------------------ the pose: arms and weapons
// The left arm's goal from a pose (its shield's face along `fwd` in the chest's frame, or `face` when given).
inline ArmGoal ShieldGoal(const ChestFrame& c,const HandPose& h) { return {Place(c,h.wrist),Dir(c,h.pole),Ident(),Dir(c,h.fwd)}; }
// The left arm's goal with the shield up in front, its face along `face` (sz_root's frame, unit).
inline ArmGoal ShieldFront(const ChestFrame& c,V3 face,float drop) {
    const V3 up=Row(c.rot,1),across=VUnit(VCross(face,up),-Row(c.rot,0));   // to the right of the face
    const V3 flat=VUnit(VPerp(face,up),Row(c.rot,2));
    return {c.at+flat*kFrontAhead+across*kFrontAcross+up*(kFrontUp-drop),VUnit(-across+up*-1.2f),Ident(),face};
}
// The left arm's goal with the shield raised out at the left front for its missiles, its face along `face`.
inline ArmGoal ShieldRaise(const ChestFrame& c,V3 face) {
    const V3 up=Row(c.rot,1),left=Row(c.rot,0),flat=VUnit(VPerp(face,up),Row(c.rot,2));
    return {c.at+flat*kRaiseAhead+left*kRaiseOut+up*kRaiseUp,VUnit(flat+left*0.3f+up*-1.0f),Ident(),face};
}
// The right arm's goal holding the rifle (or the tomahawk) `hand` turned as given, the wrist at `wrist`.
inline ArmGoal HandGoal(V3 wrist,V3 pole,const M3& hand) { return {wrist,pole,hand,Row(hand,1)}; }
inline ArmGoal RifleGoal(const ChestFrame& c,const HandPose& h) {
    return HandGoal(Place(c,h.wrist),Dir(c,h.pole),Orient(Dir(c,h.fwd),Dir(c,h.up)));
}
inline ArmGoal AimGoal(const PoseInput& in,const ChestFrame& c,float kick) {
    const V3 dir=AimDir(in,kRecoilMuzzle*kick),flat=AimDir(in,0.0f);
    const V3 wrist=c.at+flat*kAimReach+Times(Of(kAimOff),c.rot)-flat*(kRecoilBack*kick);
    return HandGoal(wrist,Dir(c,kAimPole),Orient(dir,Row(c.rot,1)));
}
inline ArmGoal MeleeGoal(const ChestFrame& c,const MeleeKey& k) {
    return HandGoal(Place(c,k.wrist),Dir(c,kAimPole),Orient(Dir(c,k.edge),Dir(c,k.up)));
}
// The tomahawk's place: where its butt is and how it is turned, on the shield (as at the bind, carried with the shield)
// or in a hand turned `hand` with its wrist at `wrist` (the handle through the fist, kAxeGrip from the butt; the edge
// the hand's +z, the handle toward the blade its +y).
struct AxeAt { V3 butt; M3 rot; };
inline V3 AxeUp(const Rig& rig) { return VUnit(Of(rig.joint[kAxeBlade])-Of(rig.joint[kAxe])); }
inline V3 AxeEdge(const Rig& rig) {   // the blade stands out across the shield (pylib/sazabi_arms.py axe: its w)
    const V3 u=VUnit(Of(rig.joint[kHandL])-Of(rig.joint[kForearmL]));
    return VCross(ShieldNormal(rig),u);
}
inline AxeAt AxeOnShield(const Rig& rig,const Pose& p) {
    const V3 off=Of(rig.joint[kAxe])-Of(rig.joint[kShield]);
    return {Of(p.modelPos[kShield])+Times(off,p.modelRot[kShield]),p.modelRot[kShield]};
}
inline AxeAt AxeInHand(const Rig& rig,V3 wrist,const M3& hand) {
    const M3 rot=Mul(Turn(AxeUp(rig),AxeEdge(rig),{0.0f,1.0f,0.0f},{0.0f,0.0f,1.0f}),hand);
    const V3 fist=wrist+Times(Of(kFist),hand);
    return {fist-Row(hand,1)*kAxeGrip,rot};
}
// The hand that takes the tomahawk where it is (its wrist and turn: the inverse of AxeInHand).
inline ArmGoal GrabGoal(const Rig& rig,const AxeAt& axe,V3 pole) {
    const M3 hand=Mul(T(Turn(AxeUp(rig),AxeEdge(rig),{0.0f,1.0f,0.0f},{0.0f,0.0f,1.0f})),axe.rot);
    const V3 fist=axe.butt+Row(hand,1)*kAxeGrip;
    return HandGoal(fist-Times(Of(kFist),hand),pole,hand);
}
// The rifle racked on the right rear skirt (its joint, the grip, at kRack in the pelvis's frame).
inline void RackAt(const Pose& p,V3* at,M3* rot) {
    const M3& pel=p.modelRot[kPelvis];
    *at=Of(p.modelPos[kPelvis])+Times(Of(kRack),pel);
    *rot=Mul(Orient(Of(kRackBarrel),Of(kRackTop)),pel);
}

// The right arm through a swing at `u`: from kAxeReady (or `from`: the draw's grab) through its wind-up, strike and
// recovery back to kAxeReady (SwingPart); the wrist along a Catmull-Rom curve through them, the hand turned between them.
inline ArmGoal SwingAt(const ChestFrame& c,int combo,float u,const ArmGoal* from,float fromU) {
    const int s=ComboOf(combo);
    ArmGoal g[5];
    for(int k=0;k<5;++k)g[k]=MeleeGoal(c,SwingKey(s,k));
    if(from)g[0]=*from;
    int i=0;
    float t=0.0f,at[5];
    SwingPart(s,u,from ? fromU : 0.0f,&i,&t,at);
    const V3 p0=g[i>0 ? i-1 : 0].wrist,p1=g[i].wrist,p2=g[i+1].wrist,p3=g[i<3 ? i+2 : 4].wrist;
    const float t2=t*t,t3=t2*t;
    ArmGoal out=Blend(g[i],g[i+1],t);
    out.wrist=(p1*2.0f+(p2-p0)*t+(p0*2.0f-p1*5.0f+p2*4.0f-p3)*t2+(p1*3.0f-p0-p2*3.0f+p3)*t3)*0.5f;
    return out;
}

// Which hand holds what (Arms): the rifle in the right hand or racked, the tomahawk in it or on the shield.
struct Holding { bool rifleInHand,axeInHand; };

// The arms and the weapons, after the legs and the torso.
constexpr float kFade=0.12f;
inline void Arms(const PoseInput& in,const Rig& rig,float dt,Anim& a,Pose* p) {
    Finish(p);
    const ChestFrame cl=Shoulder(*p,0),cr=Shoulder(*p,1);
    // the left arm: at its side swinging with the right leg, up across the chest (guard), turned onto the aim (its
    // missiles), held out of the right hand's way for the draw and the put-away, swept back in a boost, out for the cannon
    ArmGoal left=ShieldGoal(cl,kShieldSide);
    const float sw=kArmSwing*p->legFore[1];
    left.wrist=left.wrist+Times(V3{0.0f,0.3f*std::fabs(sw),sw},cl.rot);
    left=Blend(left,ShieldGoal(cl,kShieldBoost),a.boost*(1.0f-a.guard));
    const float guard=std::fmax(a.guard,kMeleeGuard*a.melee);
    left=Blend(left,ShieldFront(cl,Dir(cl,kGuardFace),0.0f),guard);
    const V3 aim=AimDir(in,0.0f);
    left=Blend(left,ShieldRaise(cl,aim),a.present);
    left=Blend(left,ShieldGoal(cl,kShieldBrace),a.brace);
    // the draw and the put-away: the shield brought in front of the belly, its inner face (the tomahawk's) to the right hand
    float drawU=-1.0f;
    const float drawEnd=DrawEnd();
    if(in.swing>=0.0f && a.drawing && in.combo==0)drawU=std::fmax(in.swing/drawEnd,a.drawFrom);
    float support=0.0f;
    if(drawU>=0.0f)support=Smooth(drawU*3.0f)*(1.0f-Smooth((drawU-1.0f)*4.0f));
    if(a.stow>=0.0f)support=Smooth(a.stow*5.0f)*(1.0f-Smooth((a.stow-kStowShield)*6.0f));
    left=Blend(left,ShieldFront(cl,Dir(cl,kDrawFace),kDrawDrop),support);
    left=Blend(ShieldGoal(cl,kShieldSide),left,Clamp(a.ready+a.guard,0.0f,1.0f));
    left.wrist=left.wrist-Row(cl.rot,2)*(kBlockBack*a.kick);   // a stopped hit's jolt
    SolveArm(rig,0,left,p);
    Finish(p);
    const AxeAt stowed=AxeOnShield(rig,*p);
    const ArmGoal grab=GrabGoal(rig,stowed,Dir(cr,kAimPole));
    V3 rackAt;
    M3 rackRot;
    RackAt(*p,&rackAt,&rackRot);
    const ArmGoal rack=HandGoal(rackAt,Dir(cr,kAimPole),rackRot);
    // the right arm with the rifle: hanging (parked), carried low, up on the aim (the kick in it), braced out for the cannon
    ArmGoal rifle=Blend(RifleGoal(cr,kRifleHang),Blend(RifleGoal(cr,kRifleCarry),AimGoal(in,cr,in.recoil),a.raise),a.ready);
    rifle=Blend(rifle,RifleGoal(cr,kRifleBrace),a.brace);
    ArmGoal right=rifle;
    Holding hold{true,false};
    int stage=0;
    if(in.swing>=0.0f) {
        hold={false,true};
        stage=drawU>=0.0f && drawU<1.0f ? 1 : 2;
        if(drawU>=0.0f && drawU<1.0f) {   // the draw: the rifle to the rack, the hand to the tomahawk
            const float r=kDrawRack;
            hold.rifleInHand=drawU<r;
            hold.axeInHand=false;
            right=drawU<r ? Blend(rifle,rack,Smooth(drawU/r)) : Blend(rack,grab,Smooth((drawU-r)/(1.0f-r)));
        } else {
            right=SwingAt(cr,in.combo,in.swing,drawU>=0.0f ? &grab : nullptr,drawEnd);
        }
    } else if(a.axeOut) {   // held ready, then put away: back on the shield, the rifle off the rack, up again
        hold={false,true};
        stage=a.stow>=0.0f ? 4 : 3;
        const ArmGoal ready=MeleeGoal(cr,kAxeReady);
        const float s=a.stow;
        right=ready;
        if(s>=0.0f) {
            if(s<kStowShield)right=Blend(ready,grab,Smooth(s/kStowShield));
            else if(s<kStowRifle){right=Blend(grab,rack,Smooth((s-kStowShield)/(kStowRifle-kStowShield)));hold.axeInHand=false;}
            else{right=Blend(rack,rifle,Smooth((s-kStowRifle)/(1.0f-kStowRifle)));hold={true,false};}
        }
    }
    // a change of source the state machine did not lead into (a combo begun in a put-away, a draw cut short) would jump
    // the hand: from where it was, crossfaded in over kFade
    if(stage!=a.stage && a.rightSet){a.fadeFrom=a.right;a.fade=0.0f;}
    a.stage=stage;
    a.fade=std::fmin(1.0f,a.fade+dt/kFade);
    a.right=right;a.rightSet=true;
    if(a.fade<1.0f)right=Blend(a.fadeFrom,right,Smooth(a.fade));
    SolveArm(rig,1,right,p);
    Finish(p);
    // the shoulder armour lifts with its upper arm (a share of its turn)
    p->rot[kShoulderL]=RotLerp(Ident(),p->rot[kUpperArmL],0.35f);
    p->rot[kShoulderR]=RotLerp(Ident(),p->rot[kUpperArmR],0.35f);
    Finish(p);
    // the rifle in the hand (rigid, as bound) or on the rack
    if(hold.rifleInHand){p->rot[kRifle]=Ident();for(int k=0;k<3;++k)p->pos[kRifle][k]=rig.joint[kRifle][k]-rig.joint[kHandR][k];}
    else{float at[3];Store(rackAt,at);PlaceAt(p,kRifle,rackRot,at);}
    // the tomahawk in the hand (lit) or on the shield (dark)
    const AxeAt axe=hold.axeInHand ? AxeInHand(rig,Of(p->modelPos[kHandR]),p->modelRot[kHandR]) : AxeOnShield(rig,*p);
    float at[3];
    Store(axe.butt,at);
    PlaceAt(p,kAxe,axe.rot,at);
    p->scale[kAxeBlade]=hold.axeInHand ? 1.0f : 0.0f;
    p->rifleInHand=hold.rifleInHand;p->axeInHand=hold.axeInHand;
}

// ------------------------------------------------------------------------------------------ the whole pose
// The pose for `in` on `rig` after `dt` s, the animator `a` stepped (runSpeed: the run's m/s, sazabi.cpp's Cfg).
inline void Animate(const PoseInput& in,const Rig& rig,float runSpeed,float dt,Anim& a,Pose* p) {
    Step(in,runSpeed,dt,a);
    Reset(rig,p);
    Legs(in,a,p);
    Torso(in,Clamp(dt,0.0f,0.1f),a,p);
    Gait(in,p);
    Plant(in.air,p);
    Arms(in,rig,Clamp(dt,0.0f,0.1f),a,p);
    Funnels(in,p);
    Finish(p);
}
}  // namespace sazabi
