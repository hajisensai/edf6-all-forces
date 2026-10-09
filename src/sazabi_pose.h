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
// A funnel's nose (its glowing muzzle's end) at the bind, unit, the model's frame: every funnel lies along it in its pack
// (the model folder's sz_funnel_* pieces' long axis, toward their glowing material: 0.035..0.061, 0.76, 0.645).
inline constexpr float kFunnelNose[3]={0.048f,0.763f,0.645f};
// The thrusters' nozzles, the model's own (the user, 2026-10-07: 「尾焰也没有根据实际喷气孔来」): each bell's exit
// (its rim's centre) from its bone's joint and the way it opens, in the bone's own frame (the bind's: the model's axes),
// found in the model folder's OBJ (bells: pieces shaped as surfaces of revolution flaring along their axis, and the
// stacks of rings the small ones are made of; checked on renders of the model, the preview folder's
// 20261007-nozzles); its flame's length and width (m: about nine exit radii long, the exit wide); `burst`: it burns
// only while the mech boosts or climbs (the main ones, the legs' big bells and the backpack's, burn with any thrust).
struct Nozzle { int bone; float at[3]; float dir[3]; float flame[2]; bool burst; };
inline constexpr Nozzle kNozzles[]={
    {kShinL,{3.256f,-1.821f,-4.619f},{0.641f,-0.261f,-0.722f},{12.4f,2.76f},false},   // the left leg main thruster (the big bell)
    {kShinR,{-3.256f,-1.821f,-4.619f},{-0.641f,-0.261f,-0.722f},{12.4f,2.76f},false},   // the right leg main thruster
    {kBackpack,{-0.500f,1.479f,-2.352f},{0.000f,-0.200f,-0.980f},{8.1f,1.80f},false},   // the backpack's upper cluster (three bells)
    {kBackpack,{-0.400f,0.329f,-2.152f},{0.000f,-0.600f,-0.800f},{5.4f,1.20f},false},   // the backpack's lower bell
    {kShinL,{-1.514f,-3.361f,-5.389f},{0.160f,0.130f,-0.979f},{4.6f,1.02f},true},   // the left calf's rear bell
    {kShinR,{1.514f,-3.361f,-5.389f},{-0.160f,0.130f,-0.979f},{4.6f,1.02f},true},   // the right calf's rear bell
    {kBackpack,{3.080f,-5.271f,-5.552f},{0.750f,-0.030f,-0.660f},{4.4f,0.98f},true},   // the left rear binder's bell
    {kBackpack,{-3.080f,-5.271f,-5.552f},{-0.769f,-0.030f,-0.639f},{4.4f,0.98f},true},   // the right rear binder's bell
    {kPelvis,{5.200f,1.192f,0.020f},{0.670f,-0.220f,-0.710f},{3.6f,0.80f},true},   // the left waist's bells
    {kPelvis,{-4.830f,0.942f,-0.681f},{-0.632f,-0.733f,-0.251f},{3.6f,0.80f},true},   // the right waist's bells
};
inline constexpr int kNozzleCount=static_cast<int>(sizeof(kNozzles)/sizeof(kNozzles[0]));

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
// What the mech is doing (sazabi.cpp Animate / ArmsPose; a remote copy from sazabi_net.inc UnpackPose). The fields the
// network carries are its own (sazabi_net.h Pose); the rest every machine works out for itself (sazabi.cpp Pose: from the
// velocity, the turn and the arms' state it holds anyway), so sazabi_anim.h's animator runs the same on every machine.
struct PoseInput {
    float t=0.0f;           // s, the mech's own clock (idle breathing)
    float gait=0.0f;        // rad, the flight's walk phase (sent; sazabi_anim.h steps its own, Anim::phase)
    float stride=0.0f;      // 0 standing .. 1 a full run (sent; the animator reads move instead)
    float air=0.0f;         // 0 on its feet .. 1 flying (legs trail, feet point down)
    float lean=0.0f;        // rad, the whole mech's lean forward (+) / back (-) from its flight
    float bank=0.0f;        // rad, its roll into a turn (+ to its left)
    float crouch=0.0f;      // 0 .. 1 knees bent: a landing, a jump's wind-up, a brace for the cannon
    float aimPitch=0.0f;    // rad, + up: where the rifle points, against the mech's facing
    float aimYaw=0.0f;      // rad, + left: ditto (the waist and chest turn part of it)
    float aim=1.0f;         // 0 .. 1 driven: the rifle held ready (0: parked, the arms hang)
    float guard=0.0f;       // 0 .. 1 the shield held up across the chest
    float swing=-1.0f;      // the tomahawk: < 0 not swinging; 0 .. 1 one swing (wind-up, strike, recover)
    int combo=0;            // ...which of the combo's swings: 0 the diagonal cut, 1 the slash across, 2 the overhead chop
    float boost=0.0f;       // 0 .. 1 the boost dash's pose (leaning into it, legs trailing, the free arm back)
    float recoil=0.0f;      // 0 .. 1 the rifle's kick (a shot: up at once, eased off)
    float cannon=0.0f;      // 0 .. 1 bracing for the chest cannon
    bool funnelOut[6]{};    // a funnel launched: drawn where it flies (funnelAt), not in its pack
    float funnelAt[6][3]{}; // ...its centre, in sz_root's frame (sazabi.cpp flies it in the world)
    float funnelDir[6][3]{};// ...where its nose points (unit, sz_root's frame)
    // not sent: each machine's own
    float move[2]{};        // m/s over the ground in sz_root's frame (x left, z ahead): where and how fast it walks
    float yawRate=0.0f;     // rad/s its facing turns (+ to its left): the feet step round a turn on the spot
    bool fire=false;        // the rifle's trigger held (the tomahawk put away at once for it; the arm up for it)
    bool present=false;     // the shield's missiles asked for: the shield turned onto the aim
    int special=0;          // the special weapon chosen (sazabi_arms.inc Special): a glance at it when it changes
};
struct Pose {
    M3 rot[kBoneCount];       // local rotations
    float pos[kBoneCount][3]; // local translations (the bind's by default)
    float scale[kBoneCount];  // 1 drawn, 0 shrunk to its joint
    M3 modelRot[kBoneCount];  // after Finish: each bone's rotation and joint in sz_root's frame
    float modelPos[kBoneCount][3];
    // the gait's plan (sazabi_anim.h Legs), per leg [0] left [1] right: the ankle and the foot's rotation standing (sz_root's
    // frame, before the torso leans, turned with the hips), the knee's stance bend (rad), the step's offset from the stance
    // (m, sz_root's x and z), lift (m) and toe (deg), how far ahead the leg is (-1 .. 1)
    float ankleStand[2][3];
    M3 footStand[2];
    float kneeStand[2];
    float footDx[2],footDz[2],footLift[2],footToe[2],legFore[2];
    float gaitW;              // 0 .. 1 how much of the gait is on (stepping; 0 standing or in the air)
    float sway;               // m the pelvis moves toward the left foot (+) / the right (-)
    bool rifleInHand;         // the rifle in the right hand (else racked on the hip: sazabi_anim.h Arms)
    bool axeInHand;           // the tomahawk in the right hand, its blade lit (else on the shield, dark)
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

// ------------------------------------------------------------------------------------------ the legs' geometry
// (sazabi_anim.h walks them.)
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
// The boost dash in the air: the legs trailing.
constexpr float kBoostThigh=15.0f,kBoostKnee=25.0f;
// The tomahawk's combo (the games' three swings): each swing winds up, strikes and recovers in the seconds given
// (sazabi_arms.inc Tomahawk times it and strikes mid-strike; sazabi_anim.h draws it). 0 the diagonal cut (its wind-up
// also draws the tomahawk off the shield when it was put away), 1 the slash across, 2 the overhead chop.
struct SwingTime { float secs[3]; };
inline constexpr SwingTime kCombo[3]={{{0.30f,0.12f,0.22f}},{{0.16f,0.10f,0.22f}},{{0.22f,0.12f,0.40f}}};
// swing `c`'s length (s), and where its wind-up ends and its strike ends (u).
inline float SwingSec(int c) { const SwingTime& w=kCombo[c<0 ? 0 : c>2 ? 2 : c]; return w.secs[0]+w.secs[1]+w.secs[2]; }
inline float WindEnd(int c) { const SwingTime& w=kCombo[c<0 ? 0 : c>2 ? 2 : c]; return w.secs[0]/SwingSec(c); }
inline float StrikeEnd(int c) { const SwingTime& w=kCombo[c<0 ? 0 : c>2 ? 2 : c]; return (w.secs[0]+w.secs[1])/SwingSec(c); }

inline float Smooth(float x) { x=x<0?0:x>1?1:x; return x*x*(3-2*x); }
// The rotation nearest `a` keeping its forward (+z row) and then its up (Gram-Schmidt).
inline M3 Orthonormal(const M3& a) {
    float z[3]={a.m[6],a.m[7],a.m[8]},y[3]={a.m[3],a.m[4],a.m[5]};
    float l=std::sqrt(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]);
    for(float& c:z)c/=l;
    const float d=y[0]*z[0]+y[1]*z[1]+y[2]*z[2];
    for(int k=0;k<3;++k)y[k]-=d*z[k];
    l=std::sqrt(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]);
    for(float& c:y)c/=l;
    const float x[3]={y[1]*z[2]-y[2]*z[1],y[2]*z[0]-y[0]*z[2],y[0]*z[1]-y[1]*z[0]};
    return {{x[0],x[1],x[2], y[0],y[1],y[2], z[0],z[1],z[2]}};
}
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

// One foot's step at phase `phase` (rad; the right foot half a cycle behind the left) of a cycle whose planted foot
// travels `travel` m, on the ground `duty` of it, lifted `lift` m while carried: its offset ahead of where it stands
// (m, + along the walk), its lift (m), its toe's turn up (deg), whether it is on the ground and its place in that part
// (0 .. 1). Planted it goes back under the body exactly as fast as the body goes on (it does not slide).
struct FootPlan { float dz,lift,toe,s; bool stance; };
constexpr float kSwingToe=14.0f;
inline FootPlan Plan(float phase,float travel,float duty,float lift,int side) {
    float u=phase/(2.0f*kPi)+(side==0 ? 0.0f : 0.5f);
    u-=std::floor(u);
    FootPlan f{};
    if(u<duty) {   // planted: from travel/2 ahead to travel/2 behind
        f.stance=true;f.s=u/duty;
        f.dz=travel*(0.5f-f.s);
        return f;
    }
    f.s=(u-duty)/(1.0f-duty);   // swung: back to front (eased: it leaves and lands slow), lifted, its toe up
    f.dz=travel*(-0.5f+Smooth(f.s));
    f.lift=lift*std::sin(kPi*f.s);
    f.toe=kSwingToe*std::sin(kPi*f.s);
    return f;
}

// The leg `side`'s ankle (sz_root's frame) with its thigh turned `th` more about the pelvis's x (forward / back) and
// `ab` about its z (out / in) and its knee bent `kn` more, the rest of the pose as Finish left it.
inline M3 ThighTurn(float th,float ab) { return Mul(RotX(th),RotZ(ab)); }
inline void AnkleWith(const Pose& p,int side,float th,float kn,float ab,float* out) {
    const int t=side==0 ? kThighL : kThighR,par=kBones[t].parent;
    const M3 thigh=Mul(Mul(p.rot[t],ThighTurn(th,ab)),p.modelRot[par]);
    const M3 shin=Mul(Mul(RotX(kn),p.rot[t+1]),thigh);
    float a[3],b[3];
    Apply(p.pos[t+1],thigh,a);
    Apply(p.pos[t+2],shin,b);
    for(int k=0;k<3;++k)out[k]=p.modelPos[t][k]+a[k]+b[k];
}

// On its feet, the feet planted: the pelvis up or down by what puts the lowest of the legs' undersides on the floor
// (LowestSole), so whichever foot carries it stays on the floor through the stride, a crouch and the lean (the bob is the
// legs' own geometry); off its feet that hold fades out (air). After the legs and the torso's lean, before the arms.
inline void Plant(float air,Pose* p) {
    Finish(p);
    p->pos[kPelvis][1]-=LowestSole(*p)*(1.0f-air);
}

// The funnels flying (funnelOut): each at its funnelAt, its nose along funnelDir (the bind lies every funnel along
// kFunnelNose with no turn of its own, so its model rotation is the turn taking that onto the direction). Docked: as bound.
inline void Funnels(const PoseInput& in,Pose* p) {
    Finish(p);
    float nose[3]={kFunnelNose[0],kFunnelNose[1],kFunnelNose[2]};
    const float l=std::sqrt(nose[0]*nose[0]+nose[1]*nose[1]+nose[2]*nose[2]);
    for(float& c:nose)c/=l;
    for(int k=0;k<6;++k) {
        if(!in.funnelOut[k])continue;
        float dir[3]={in.funnelDir[k][0],in.funnelDir[k][1],in.funnelDir[k][2]};
        const float d=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
        if(!(d>1e-4f)){dir[0]=nose[0];dir[1]=nose[1];dir[2]=nose[2];}else for(float& c:dir)c/=d;
        PlaceAt(p,kFunnels[k],Align(nose,dir),in.funnelAt[k]);
    }
}
}  // namespace sazabi
