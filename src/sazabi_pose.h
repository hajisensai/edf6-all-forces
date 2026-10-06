// The Sazabi's moving parts (sazabi.cpp Pose; tools/sazabi_pose_check.cpp runs the same code offline and
// tools/sazabi_pose_view.py renders what it writes onto the real model). Every sz_ bone of pylib/sazabi_model.py's
// skeleton is bound level (identity rotation at its joint), so the local matrix the plugin writes (bone record +0x70)
// is a rotation's three rows over the bind translation, or a translation of its own (Pose::pos). The engine composes
// world = local x parent's world (docs/sazabi-re.md §2).
//
// Conventions (row vectors, v' = v M, the rows of M the images of x, y, z; the model's axes x left, y up, z forward):
//   a child's model rotation = its local rotation x its parent's model rotation (applied first, then the parent's);
//   RotX(a) with a > 0 tips +z down and swings a hanging limb back: a torso leans forward, a knee bends;
//   RotX(-a) swings a hanging limb forward (a thigh striding, an arm raised);
//   RotY(a) with a > 0 turns +z toward +x: the mech turns to its left;
//   RotZ(a) with a > 0 turns +x toward +y: a left arm lifts sideways (a right one drops).
// Everything runs in sz_root's frame (sz_root itself is never written: its parent `body` is where the V506 CAS
// puts it). Plain math, no game memory: header only, so the offline check compiles it unchanged.
#pragma once
#include <cmath>
#include <cstring>

namespace sazabi {
constexpr float kPi=3.14159265f,kDeg=kPi/180.0f;

// The bones the plugin writes, in pylib/sazabi_model.py SKELETON order (preorder: a parent before its children).
// tools/selftest.py holds this table to that list.
enum Bone {
    kRoot,kPelvis,kWaist,kChest,kHead,kCannon,kBackpack,
    kFunnelPackL,kFunnelL1,kFunnelL2,kFunnelL3,kTubeL,kFunnelPackR,kFunnelR1,kFunnelR2,kFunnelR3,kTubeR,
    kShoulderL,kUpperArmL,kForearmL,kHandL,kShield,kMissile,
    kShoulderR,kUpperArmR,kForearmR,kHandR,kRifle,kMuzzle,
    kThighL,kShinL,kFootL,kThighR,kShinR,kFootR,
    kAxe,kAxeBlade,
    kBoneCount
};
struct BoneDef { const wchar_t* name; int parent; };
inline constexpr BoneDef kBones[kBoneCount]={
    {L"sz_root",-1},{L"sz_pelvis",kRoot},{L"sz_waist",kPelvis},{L"sz_chest",kWaist},{L"sz_head",kChest},
    {L"sz_cannon",kChest},{L"sz_backpack",kChest},
    {L"sz_funnelpack_l",kBackpack},{L"sz_funnel_l1",kFunnelPackL},{L"sz_funnel_l2",kFunnelPackL},
    {L"sz_funnel_l3",kFunnelPackL},{L"sz_tube_l",kBackpack},
    {L"sz_funnelpack_r",kBackpack},{L"sz_funnel_r1",kFunnelPackR},{L"sz_funnel_r2",kFunnelPackR},
    {L"sz_funnel_r3",kFunnelPackR},{L"sz_tube_r",kBackpack},
    {L"sz_shoulder_l",kChest},{L"sz_upperarm_l",kChest},{L"sz_forearm_l",kUpperArmL},{L"sz_hand_l",kForearmL},
    {L"sz_shield",kForearmL},{L"sz_missile",kShield},
    {L"sz_shoulder_r",kChest},{L"sz_upperarm_r",kChest},{L"sz_forearm_r",kUpperArmR},{L"sz_hand_r",kForearmR},
    {L"sz_rifle",kHandR},{L"sz_muzzle",kRifle},
    {L"sz_thigh_l",kPelvis},{L"sz_shin_l",kThighL},{L"sz_foot_l",kShinL},
    {L"sz_thigh_r",kPelvis},{L"sz_shin_r",kThighR},{L"sz_foot_r",kShinR},
    {L"sz_axe",kRoot},{L"sz_axe_blade",kAxe},
};
constexpr bool ParentsFirst() {
    for(int i=1;i<kBoneCount;++i)if(kBones[i].parent<0 || kBones[i].parent>=i)return false;
    return kBones[0].parent==-1;
}
static_assert(ParentsFirst(),"kBones: every parent before its children");
inline constexpr int kFunnels[6]={kFunnelL1,kFunnelL2,kFunnelL3,kFunnelR1,kFunnelR2,kFunnelR3};

// ------------------------------------------------------------------------------------------ 3 x 3 rotations
struct M3 { float m[9]; };
inline M3 Ident() { return {{1,0,0, 0,1,0, 0,0,1}}; }
inline M3 RotX(float a) { const float c=std::cos(a),s=std::sin(a); return {{1,0,0, 0,c,s, 0,-s,c}}; }
inline M3 RotY(float a) { const float c=std::cos(a),s=std::sin(a); return {{c,0,-s, 0,1,0, s,0,c}}; }
inline M3 RotZ(float a) { const float c=std::cos(a),s=std::sin(a); return {{c,s,0, -s,c,0, 0,0,1}}; }
// a then b (row vectors: v a b)
inline M3 Mul(const M3& a,const M3& b) {
    M3 r{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i*3+j]=a.m[i*3]*b.m[j]+a.m[i*3+1]*b.m[3+j]+a.m[i*3+2]*b.m[6+j];
    return r;
}
inline M3 T(const M3& a) { return {{a.m[0],a.m[3],a.m[6], a.m[1],a.m[4],a.m[7], a.m[2],a.m[5],a.m[8]}}; }
inline void Apply(const float* v,const M3& a,float* out) {   // out = v a
    const float x=v[0],y=v[1],z=v[2];
    for(int j=0;j<3;++j)out[j]=x*a.m[j]+y*a.m[3+j]+z*a.m[6+j];
}
// The rotation whose +z is `dir` (unit) and whose +y is as near world up as that allows: x = up x f, y = f x x.
inline M3 Facing(const float* dir) {
    float f[3]={dir[0],dir[1],dir[2]};
    float l=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if(!(l>1e-5f)){f[0]=0;f[1]=0;f[2]=1;l=1;}
    for(float& c:f)c/=l;
    float x[3]={f[2],0.0f,-f[0]};                    // (0,1,0) x f
    l=std::sqrt(x[0]*x[0]+x[2]*x[2]);
    if(l<1e-5f){x[0]=1;x[2]=0;l=1;}
    x[0]/=l;x[2]/=l;
    const float y[3]={f[1]*x[2]-f[2]*x[1],f[2]*x[0]-f[0]*x[2],f[0]*x[1]-f[1]*x[0]};
    return {{x[0],x[1],x[2], y[0],y[1],y[2], f[0],f[1],f[2]}};
}
// The shortest rotation taking unit `a` onto unit `b` (Rodrigues; row-vector form: a R = b).
inline M3 Align(const float* a,const float* b) {
    const float v[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    const float c=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    if(c<-0.9999f)return RotY(kPi);
    const float k=1.0f/(1.0f+c);
    // column-vector R = I + [v]x + [v]x^2 k; the row-vector form is its transpose
    const M3 col={{v[0]*v[0]*k+c, v[0]*v[1]*k-v[2], v[0]*v[2]*k+v[1],
                   v[1]*v[0]*k+v[2], v[1]*v[1]*k+c, v[1]*v[2]*k-v[0],
                   v[2]*v[0]*k-v[1], v[2]*v[1]*k+v[0], v[2]*v[2]*k+c}};
    return T(col);
}

// ------------------------------------------------------------------------------------------ input, output
struct PoseInput {
    float t=0.0f;           // s, the mech's own clock (idle breathing)
    float gait=0.0f;        // rad, the walk cycle's phase (sazabi.cpp advances it by the ground covered: GaitStep)
    float stride=0.0f;      // 0 standing .. 1 a full run's stride (the gait's amplitude)
    float air=0.0f;         // 0 on its feet .. 1 flying (legs trail, feet point down)
    float lean=0.0f;        // rad, the whole mech's lean forward (+) / back (-) from its acceleration and speed
    float bank=0.0f;        // rad, its roll into a turn (+ to its left)
    float crouch=0.0f;      // 0 .. 1 knees bent: a landing, a jump's wind-up, a brace for the cannon
    float aimPitch=0.0f;    // rad, + up: where the rifle points, against the mech's facing
    float aimYaw=0.0f;      // rad, + left: ditto (the waist and chest turn part of it)
    float aim=1.0f;         // 0 .. 1 the rifle raised to aim (1 while it is drawn and not swinging)
    float guard=0.0f;       // 0 .. 1 the shield held up across the chest
    float swing=-1.0f;      // the tomahawk: < 0 stowed; 0 .. 1 one swing (wind-up, strike, recover)
    float cannon=0.0f;      // 0 .. 1 bracing for the chest cannon
    bool funnelOut[6]{};    // a funnel launched: hidden in its pack (it flies as a drone of its own)
};
struct Pose {
    M3 rot[kBoneCount];       // local rotations
    float pos[kBoneCount][3]; // local translations (the bind's by default)
    float scale[kBoneCount];  // 1 drawn, 0 shrunk to its joint
    M3 modelRot[kBoneCount];  // after Finish: each bone's rotation and joint in sz_root's frame
    float modelPos[kBoneCount][3];
};
// Each bone's joint in sz_root's frame at the bind (the bone records' bind world, sazabi.cpp Joints).
struct Rig { float joint[kBoneCount][3]; };

inline void Reset(const Rig& rig,Pose* p) {
    for(int i=0;i<kBoneCount;++i) {
        p->rot[i]=Ident();
        p->scale[i]=1.0f;
        const int par=kBones[i].parent;
        for(int k=0;k<3;++k)p->pos[i][k]=rig.joint[i][k]-(par>=0 ? rig.joint[par][k] : 0.0f);
    }
}
// Forward kinematics into modelRot / modelPos.
inline void Finish(Pose* p) {
    for(int i=0;i<kBoneCount;++i) {
        const int par=kBones[i].parent;
        if(par<0){p->modelRot[i]=p->rot[i];std::memcpy(p->modelPos[i],p->pos[i],sizeof p->pos[i]);continue;}
        p->modelRot[i]=Mul(p->rot[i],p->modelRot[par]);
        float off[3];
        Apply(p->pos[i],p->modelRot[par],off);
        for(int k=0;k<3;++k)p->modelPos[i][k]=p->modelPos[par][k]+off[k];
    }
}
// Bone `i`'s local made so that its model rotation and joint are `rot` and `at` (sz_root's frame), its parent's as
// Finish left it (the parent must be finished first).
inline void PlaceAt(Pose* p,int i,const M3& rot,const float* at) {
    const int par=kBones[i].parent;
    const M3 inv=T(p->modelRot[par]);
    p->rot[i]=Mul(rot,inv);
    const float d[3]={at[0]-p->modelPos[par][0],at[1]-p->modelPos[par][1],at[2]-p->modelPos[par][2]};
    Apply(d,inv,p->pos[i]);
}
// The 4x4 local matrix (row-major, rows right, up, forward, position) of bone `i`.
inline void LocalMatrix(const Pose& p,int i,float* out) {
    for(int r=0;r<3;++r){for(int c=0;c<3;++c)out[r*4+c]=p.rot[i].m[r*3+c]*p.scale[i];out[r*4+3]=0.0f;}
    for(int c=0;c<3;++c)out[12+c]=p.pos[i][c];
    out[15]=1.0f;
}

// ------------------------------------------------------------------------------------------ the poses
// Gait (degrees): a giant's stride (a leg ~12 m from hip to sole; one cycle kStride m of ground), the thigh's swing,
// the knee's bend while the leg swings forward, the foot kept flat, the pelvis's bob and the waist's counter-twist.
constexpr float kStride=16.0f;
constexpr float kThighSwing=26.0f,kKneeSwing=42.0f,kKneeStance=8.0f,kFootFlat=1.0f;
constexpr float kWaistTwist=7.0f,kArmSwing=14.0f,kRunLean=8.0f;
// The stance (tools/sazabi_stance.py works it out from the model): the source stands as it hovers (legs spread wide,
// shins raked back, toes pointing down), so on its feet each leg is turned to stand: the thigh Rx(thighX) Rz(thighZ),
// the knee Rx(knee), the foot Rx(footX) Rz(footZ) (degrees), [0] the left leg, [1] the right; the pelvis drops
// kStanceDrop m to put the soles on the floor. Off its feet (air 1) the stance goes and the legs are the source's own:
// its hovering pose is the flight's.
struct Stance { float thighX,thighZ,knee,footX,footZ; };
inline constexpr Stance kStance[2]={{-10.0f,-20.25f,5.0f,-42.0f,18.0f},{-10.0f,20.25f,5.0f,-42.0f,-18.0f}};
constexpr float kStanceDrop=1.53f;
// The left leg's underside (tools/sazabi_stance.py: its shin's and foot's points furthest along each of 64 directions,
// directions in the stance; kSole), each in its bone's own frame from its joint; the right leg's are these mirrored (x -> -x,
// the right bone: the right leg is the left mirrored). The feet are planted on the lowest of them (Legs).
struct SolePoint { int bone; float at[3]; };
inline constexpr SolePoint kSole[]={
    {kShinL,{3.479f,-5.307f,1.373f}},
    {kShinL,{-0.146f,-6.541f,-0.670f}},
    {kShinL,{2.510f,-4.764f,-3.277f}},
    {kShinL,{3.588f,-5.248f,1.393f}},
    {kShinL,{-0.338f,-6.502f,-0.886f}},
    {kShinL,{4.408f,-4.401f,-1.927f}},
    {kShinL,{2.611f,-5.772f,1.785f}},
    {kShinL,{-1.719f,-5.444f,-3.847f}},
    {kShinL,{-1.824f,-5.627f,-3.502f}},
    {kShinL,{0.451f,-3.600f,-5.933f}},
    {kShinL,{-2.064f,-5.834f,-1.736f}},
    {kShinL,{4.065f,-2.463f,-3.666f}},
    {kShinL,{2.877f,-5.155f,2.303f}},
    {kShinL,{-1.844f,-5.444f,-3.830f}},
    {kShinL,{4.377f,-4.534f,-1.627f}},
    {kShinL,{-1.377f,-5.936f,-0.275f}},
    {kShinL,{1.049f,-2.385f,-6.519f}},
    {kShinL,{3.213f,-4.905f,2.237f}},
    {kShinL,{4.330f,-1.440f,-3.791f}},
    {kShinL,{2.674f,-5.596f,1.940f}},
    {kShinL,{0.475f,-3.498f,-6.004f}},
    {kShinL,{3.408f,-5.006f,1.905f}},
    {kShinL,{-2.314f,-5.608f,-1.359f}},
    {kShinL,{2.358f,-1.080f,-5.910f}},
    {kShinL,{2.971f,-5.049f,2.330f}},
    {kShinL,{-2.590f,-3.752f,-4.757f}},
    {kShinL,{4.338f,-1.416f,-3.750f}},
    {kShinL,{-2.795f,-5.202f,-2.493f}},
    {kShinL,{3.955f,-0.627f,-4.382f}},
    {kShinL,{-2.016f,-2.639f,-5.750f}},
    {kShinL,{4.424f,-1.940f,-2.773f}},
    {kShinL,{-2.193f,-5.666f,-1.170f}},
    {kShinL,{3.088f,-4.959f,2.287f}},
    {kShinL,{4.150f,-0.807f,-4.119f}},
    {kShinL,{-0.990f,-2.088f,-6.172f}},
    {kShinL,{3.307f,-4.905f,2.137f}},
    {kShinL,{3.447f,-0.463f,-4.871f}},
    {kShinL,{-2.228f,-2.854f,-5.566f}},
    {kShinL,{4.135f,-0.791f,-3.240f}},
    {kShinL,{3.713f,-0.463f,-4.609f}},
    {kShinL,{0.928f,-2.690f,1.834f}},
    {kFootL,{3.717f,-4.010f,1.485f}},
    {kFootL,{-1.005f,-2.379f,0.047f}},
    {kFootL,{-2.618f,2.144f,-4.508f}},
    {kFootL,{-3.669f,1.593f,-3.918f}},
    {kFootL,{2.116f,-0.747f,-1.711f}},
    {kFootL,{2.991f,-4.387f,1.891f}},
    {kFootL,{-2.915f,-0.647f,-1.696f}},
    {kFootL,{0.233f,1.000f,-3.469f}},
    {kFootL,{1.132f,-3.596f,1.201f}},
    {kFootL,{0.616f,0.757f,-3.239f}},
    {kFootL,{-2.689f,-0.971f,-1.375f}},
    {kFootL,{2.092f,-0.417f,-1.688f}},
    {kFootL,{3.186f,-2.521f,0.043f}},
    {kFootL,{-0.072f,1.875f,-2.950f}},
    {kFootL,{-3.599f,1.878f,-3.715f}},
    {kFootL,{-1.279f,-2.073f,0.093f}},
    {kFootL,{-2.681f,2.359f,-4.231f}},
    {kFootL,{1.561f,1.339f,0.660f}}
};
// Landing / crouch: knees bend, the pelvis drops.
constexpr float kCrouchThigh=-24.0f,kCrouchKnee=48.0f,kCrouchDrop=2.2f;
// The rifle arm raised: the upper arm forward and out, the forearm bent a little; the wrist then turns the rifle
// onto the aim exactly (AimRifle).
constexpr float kAimRaise=78.0f,kAimOut=12.0f,kAimElbow=-22.0f;
constexpr float kChestShare=0.45f;   // of the aim's yaw the chest takes (the rest the arm)
constexpr float kMostAimYaw=60.0f,kMostAimPitch=55.0f;
// The shield up: the left arm across the chest.
constexpr float kGuardRaise=62.0f,kGuardIn=-38.0f,kGuardElbow=-70.0f;
// The tomahawk's swing: wind-up (raised back over the right shoulder) to kWindUp, the strike (down and across) to
// kStrike, then back. Its grip kGripAhead m along the hand's forward.
constexpr float kWindUp=0.35f,kStrike=0.6f,kGripAhead=1.1f;

inline float Smooth(float x) { x=x<0?0:x>1?1:x; return x*x*(3-2*x); }
inline float Clamp(float x,float lo,float hi) { return x<lo?lo:x>hi?hi:x; }

// The lowest of the legs' undersides (kSole, both legs) as posed, in sz_root's frame (the floor y = 0).
inline float LowestSole(const Pose& p) {
    float lowest=1e9f;
    for(int side=0;side<2;++side)
        for(const SolePoint& sp:kSole) {
            const int bone=side==0 ? sp.bone : sp.bone+(kThighR-kThighL);
            const float local[3]={side==0 ? sp.at[0] : -sp.at[0],sp.at[1],sp.at[2]};
            float w[3];
            Apply(local,p.modelRot[bone],w);
            const float y=p.modelPos[bone][1]+w[1];
            lowest=y<lowest ? y : lowest;
        }
    return lowest;
}

// The legs: the stance, the gait, the crouch (Plant puts them on the floor).
inline void Legs(const PoseInput& in,Pose* p) {
    const float s=std::sin(in.gait),c=std::cos(in.gait),st=in.stride,air=in.air,cr=in.crouch,feet=1.0f-air;
    for(int side=0;side<2;++side) {
        const Stance& k=kStance[side];
        const float sw=side==0 ? s : -s,fwd=side==0 ? c : -c;   // the right leg half a cycle behind
        float thigh=-kThighSwing*sw*st;                          // - forward
        float knee=(kKneeSwing*(fwd>0 ? fwd : 0)+kKneeStance)*st;
        thigh=thigh*feet+kCrouchThigh*cr;
        knee=knee*feet+kCrouchKnee*cr;
        const float foot=-kFootFlat*(thigh+knee)*feet;
        const int t=side==0 ? kThighL : kThighR;
        // the stance first (in the pelvis's frame), the gait's swing about the pelvis's x after it
        p->rot[t]=Mul(Mul(RotX(k.thighX*feet*kDeg),RotZ(k.thighZ*feet*kDeg)),RotX(thigh*kDeg));
        p->rot[t+1]=RotX((k.knee*feet+knee)*kDeg);
        p->rot[t+2]=Mul(RotX(foot*kDeg),Mul(RotX(k.footX*feet*kDeg),RotZ(k.footZ*feet*kDeg)));
    }
    p->pos[kPelvis][1]+=-kCrouchDrop*cr-kStanceDrop*feet;
}

// On its feet, the feet planted: the pelvis up or down by what puts the lowest of the legs' undersides on the floor
// (LowestSole), so whichever foot carries it stays on the floor through the stride, a crouch and the lean (the bob is the
// legs' own geometry); off its feet that hold fades out (air). After the legs and the torso's lean, before the arms.
inline void Plant(const PoseInput& in,Pose* p) {
    Finish(p);
    p->pos[kPelvis][1]-=LowestSole(*p)*(1.0f-in.air);
}

inline void Torso(const PoseInput& in,Pose* p) {
    const float twist=kWaistTwist*std::sin(in.gait)*in.stride*(1-in.air)*kDeg;
    const float yaw=Clamp(in.aimYaw,-kMostAimYaw*kDeg,kMostAimYaw*kDeg)*kChestShare*in.aim;
    const float breathe=0.6f*std::sin(in.t*1.3f)*kDeg;
    p->rot[kPelvis]=Mul(RotZ(-in.bank),RotX(in.lean+kRunLean*kDeg*in.stride*(1-in.air)));
    p->rot[kWaist]=RotY(twist+yaw*0.4f);
    p->rot[kChest]=Mul(RotX(breathe-6.0f*kDeg*in.cannon),RotY(yaw*0.6f));
    p->rot[kHead]=RotX(-Clamp(in.aimPitch,-30*kDeg,30*kDeg)*0.4f);
    // the backpack's tubes flare up in flight, the funnel packs open a little
    const float flare=12.0f*in.air*kDeg;
    p->rot[kTubeL]=RotX(-flare);p->rot[kTubeR]=RotX(-flare);
}

// The left arm: swinging with the gait, or the shield up across the chest.
inline void LeftArm(const PoseInput& in,Pose* p) {
    const float sw=kArmSwing*std::sin(in.gait)*in.stride*(1-in.air)*(1-in.guard);
    const float g=Smooth(in.guard);
    p->rot[kUpperArmL]=Mul(RotX(-(sw+kGuardRaise*g)*kDeg),RotY(kGuardIn*g*kDeg));
    p->rot[kForearmL]=RotY(kGuardElbow*g*kDeg);
}

// The right arm raised so that the rifle (+z of sz_rifle, which follows the hand rigidly) points along the aim.
inline void AimRifle(const PoseInput& in,Pose* p) {
    const float a=Smooth(in.aim);
    const float sw=kArmSwing*std::sin(in.gait+kPi)*in.stride*(1-in.air)*(1-a);
    p->rot[kUpperArmR]=Mul(RotX(-(kAimRaise*a+sw)*kDeg),RotZ(-kAimOut*a*kDeg));
    p->rot[kForearmR]=RotX(kAimElbow*a*kDeg);
    Finish(p);
    const float yaw=Clamp(in.aimYaw,-kMostAimYaw*kDeg,kMostAimYaw*kDeg);
    const float pitch=Clamp(in.aimPitch,-kMostAimPitch*kDeg,kMostAimPitch*kDeg);
    const float dir[3]={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    // the hand's model rotation wanted: the aim's (rifle along it), blended from the bind's (hanging, rifle level)
    const M3 want=Facing(dir);
    const M3 hand=Mul(p->rot[kHandR],p->modelRot[kForearmR]);
    M3 blend{};
    for(int k=0;k<9;++k)blend.m[k]=hand.m[k]*(1-a)+want.m[k]*a;
    // re-orthonormalise (Facing of the blend's forward)
    const M3 target=Facing(&blend.m[6]);
    p->rot[kHandR]=Mul(target,T(p->modelRot[kForearmR]));
}

// The tomahawk: stowed (rigid with the shield, as at the bind), or drawn in the right hand through a swing; its
// blade lit only while drawn. `axeAxis`: the handle's direction at the bind, grip to blade (sazabi.cpp from the
// joints of sz_axe and sz_axe_blade).
inline void Tomahawk(const PoseInput& in,const Rig& rig,Pose* p) {
    Finish(p);
    if(in.swing<0.0f) {   // stowed: its bind offset from the shield, carried with the shield
        const int s=kShield;
        float off[3]={rig.joint[kAxe][0]-rig.joint[s][0],rig.joint[kAxe][1]-rig.joint[s][1],rig.joint[kAxe][2]-rig.joint[s][2]};
        float at[3];
        Apply(off,p->modelRot[s],at);
        for(int k=0;k<3;++k)at[k]+=p->modelPos[s][k];
        PlaceAt(p,kAxe,p->modelRot[s],at);
        p->scale[kAxeBlade]=0.0f;
        return;
    }
    // drawn: the swing's arm (overrides the aim), then the axe in the hand with its blade along the hand's forward
    const float u=in.swing;
    float raise,across;
    if(u<kWindUp){const float k=Smooth(u/kWindUp);raise=150.0f*k;across=-20.0f*k;}
    else if(u<kStrike){const float k=Smooth((u-kWindUp)/(kStrike-kWindUp));raise=150.0f-170.0f*k;across=-20.0f+70.0f*k;}
    else {const float k=Smooth((u-kStrike)/(1.0f-kStrike));raise=-20.0f+20.0f*k;across=50.0f-50.0f*k;}
    p->rot[kUpperArmR]=Mul(RotX(-raise*kDeg),RotY(across*kDeg));
    p->rot[kForearmR]=RotX(-25.0f*kDeg);
    p->rot[kHandR]=RotX(-40.0f*kDeg);
    p->rot[kChest]=Mul(p->rot[kChest],RotY(across*0.3f*kDeg));
    p->scale[kRifle]=0.0f;   // the rifle away while the tomahawk is out
    Finish(p);
    const float axis[3]={rig.joint[kAxeBlade][0]-rig.joint[kAxe][0],rig.joint[kAxeBlade][1]-rig.joint[kAxe][1],
                         rig.joint[kAxeBlade][2]-rig.joint[kAxe][2]};
    const float len=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
    const float unit[3]={axis[0]/len,axis[1]/len,axis[2]/len},fwd[3]={0,0,1};
    const M3 rot=Mul(Align(unit,fwd),p->modelRot[kHandR]);   // handle to blade along the hand's +z
    float ahead[3]={0,0,kGripAhead},at[3];
    Apply(ahead,p->modelRot[kHandR],at);
    for(int k=0;k<3;++k)at[k]+=p->modelPos[kHandR][k];
    PlaceAt(p,kAxe,rot,at);
    p->scale[kAxeBlade]=1.0f;
}

// The whole pose for `in` on `rig`.
inline void Animate(const PoseInput& in,const Rig& rig,Pose* p) {
    Reset(rig,p);
    Legs(in,p);
    Torso(in,p);
    Plant(in,p);
    LeftArm(in,p);
    AimRifle(in,p);
    Tomahawk(in,rig,p);
    for(int k=0;k<6;++k)p->scale[kFunnels[k]]=in.funnelOut[k] ? 0.0f : 1.0f;
    Finish(p);
}

// The gait's phase after `dt` s covering `ground` m/s.
inline float GaitStep(float phase,float ground,float dt) {
    phase+=2.0f*kPi*ground/kStride*dt;
    return phase>200.0f*kPi ? phase-200.0f*kPi : phase;
}
}  // namespace sazabi
