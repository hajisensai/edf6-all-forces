// The Primer creatures' moving parts (primer.cpp PoseDragonfly / PoseCentipede; tools/primer_pose_sim.cpp runs the
// same code offline and tools/primer_pose_view.py renders what it writes). Both models are the plugin's own
// (pylib/dragonfly_model.py, pylib/centipede_model.py): their moving parts are bones under `body`, bound level,
// which the V506 animation leaves alone, so the plugin writes their local matrices (bone record +0x70, as the
// jets' elevons: docs/mdb-format.md §3); the engine makes world = local x parent's world each frame.
// Dragonfly:
//   wings wing_fl / wing_fr / wing_bl / wing_br: about local z (their roots), + lifts a right wing's tip, so a left
//     one takes the opposite sign; the fore and hind pairs beat in opposition, as a dragonfly's do;
//   abdomen abd1 -> abd4 (a chain): about local x, - bends the tail down; armed it curls down under the body (each
//     segment kCurl), a stinger brought to bear: the warning before it fires.
// Centipede (one segment a creature, a chain of them one long centipede):
//   legs leg_l / leg_r: about local y (fore and aft), the two in opposition, a linked one a step behind the one
//     ahead (the wave down the chain); dead, also about local z: folded in under it, kicking ever weaker;
//   head / tail: shrunk to their joints (kHidden) when it is linked into a longer creature, so that one shows a
//     single head at its front and a single tail at its end. Split, they grow back (GrowScale: over kRegrowSec for
//     the head of the part behind, half that for the tail of the part ahead, ending with a small overshoot), and
//     while the head grows the headless one writhes (`writhe`: its legs faster and wider, stump and tail jerking);
//     dead, the head droops and the tail curls up;
//   gun / sting: aimed mounts (AimLocal) for its barbs and its stinger.
// Plain math, no game memory: header only, so the simulator compiles it unchanged.
#pragma once
#include <cmath>

namespace primer {
enum Axis { kAxisX, kAxisY, kAxisZ };
struct PoseBone { const wchar_t* name; Axis axis,axis2; };   // turned about axis, then axis2
constexpr float kPi=3.14159265f,kDeg=kPi/180.0f;

// local = R(axis, angle) x bind (row vectors, 4x4 row-major: rows right, up, forward, position), its three rows
// times `scale` (1: as is; kHidden: shrunk to its joint).
inline void TurnLocal(const float* bind,Axis axis,float angle,float* out,float scale=1.0f) {
    const float c=std::cos(angle),s=std::sin(angle);
    float r[9];
    if(axis==kAxisX){const float m[9]={1,0,0, 0,c,s, 0,-s,c};for(int i=0;i<9;++i)r[i]=m[i];}
    else if(axis==kAxisY){const float m[9]={c,0,-s, 0,1,0, s,0,c};for(int i=0;i<9;++i)r[i]=m[i];}
    else {const float m[9]={c,s,0, -s,c,0, 0,0,1};for(int i=0;i<9;++i)r[i]=m[i];}
    for(int row=0;row<3;++row)
        for(int col=0;col<4;++col)
            out[row*4+col]=(r[row*3]*bind[col]+r[row*3+1]*bind[4+col]+r[row*3+2]*bind[8+col])*scale;
    for(int col=0;col<4;++col)out[12+col]=bind[12+col];
}

// R(axis2, angle2) x R(axis, angle) x bind: turned about `axis`, then about `axis2` (its own, as turned), scaled.
inline void TurnLocal2(const float* bind,Axis axis,float angle,Axis axis2,float angle2,float* out,float scale=1.0f) {
    float once[16];
    TurnLocal(bind,axis,angle,once);
    TurnLocal(once,axis2,angle2,out,scale);
}

// --- The dragonfly ---
inline constexpr PoseBone kDragonflyBones[]={
    {L"wing_fl",kAxisZ,kAxisZ},{L"wing_fr",kAxisZ,kAxisZ},{L"wing_bl",kAxisZ,kAxisZ},{L"wing_br",kAxisZ,kAxisZ},
    {L"abd1",kAxisX,kAxisX},{L"abd2",kAxisX,kAxisX},{L"abd3",kAxisX,kAxisX},{L"abd4",kAxisX,kAxisX},
};
constexpr int kDragonflyBoneCount=static_cast<int>(sizeof(kDragonflyBones)/sizeof(kDragonflyBones[0]));
enum DragonflyBone { kWingFL, kWingFR, kWingBL, kWingBR, kAbd1 };
// Wings: kBeatHz a second (a real one's 30 would alias at 60 frames), kBeatAmp deg each way about kWingRest up;
// armed (hovering at its target) kArmHz, kArmAmp.
constexpr float kWingRest=4.0f,kBeatHz=6.0f,kBeatAmp=30.0f,kArmHz=9.0f,kArmAmp=22.0f;
// Abdomen: a sway of kSway deg per segment at kSwayHz (a wave down the tail), plus the curl (0..1) kCurl deg per
// segment. The curl moves at kCurlRate a second; the guns fire only curled kCurlFire or more: the warning.
constexpr float kSway=3.0f,kSwayHz=0.7f,kCurl=-18.0f,kCurlRate=1.6f,kCurlFire=0.85f;

struct DragonflyInput { float t; bool arm; };   // seconds of game time; whether it wants its sting out

inline float CurlStep(float curl,const DragonflyInput& in,float dt) {
    const float want=in.arm ? 1.0f : 0.0f,step=kCurlRate*dt;
    return curl+(want-curl>step ? step : want-curl<-step ? -step : want-curl);
}

// Each part's angle (rad, kDragonflyBones order) with the abdomen curled `curl`.
inline void DragonflyAngles(const DragonflyInput& in,float curl,float* out) {
    const float two=2.0f*kPi;
    const float hz=in.arm ? kArmHz : kBeatHz,amp=in.arm ? kArmAmp : kBeatAmp;
    const float fore=kWingRest+amp*std::sin(two*hz*in.t),hind=kWingRest+amp*std::sin(two*hz*in.t+kPi);
    out[kWingFR]=fore*kDeg;out[kWingFL]=-fore*kDeg;
    out[kWingBR]=hind*kDeg;out[kWingBL]=-hind*kDeg;
    for(int s=0;s<4;++s)out[kAbd1+s]=(kSway*std::sin(two*kSwayHz*in.t-0.6f*static_cast<float>(s))+kCurl*curl)*kDeg;
}

// --- The centipede ---
// One segment a creature (pylib/centipede_model.py): its pair of legs, its head and tail (shown only at the chain's
// ends), the dorsal barbs' mount `gun` and the tail's stinger `sting` (aimed: AimLocal, primer.cpp).
inline constexpr PoseBone kCentipedeBones[]={
    {L"leg_l",kAxisY,kAxisZ},{L"leg_r",kAxisY,kAxisZ},{L"head",kAxisX,kAxisX},{L"tail",kAxisX,kAxisX},
};
constexpr int kCentipedeBoneCount=static_cast<int>(sizeof(kCentipedeBones)/sizeof(kCentipedeBones[0]));
enum CentipedeBone { kLegL, kLegR, kHead, kTail };
// Legs: kStride m a step (its frequency is speed / kStride, kStepMin..kStepMax Hz), kLegSwing deg each way on the
// ground, kLegAir in the air at kAirStepHz. A linked one steps kChainLag of a step behind the one ahead of it
// (primer.cpp): the wave that runs down a centipede's legs runs down the chain.
constexpr float kStride=2.2f,kStepMin=0.8f,kStepMax=4.0f,kLegSwing=24.0f,kLegAir=12.0f,kAirStepHz=1.3f,kChainLag=0.18f;
constexpr float kHidden=0.02f;   // a hidden head / tail's scale
// s a split's new front takes to grow its head back (primer.cpp holds it out of the fight meanwhile); its new
// tail's at the other side, kTailRegrowShare of that. Writhing, the legs' swing is (1 + kWritheLegs x writhe) times
// as wide and kWritheStep times as quick at most, the head stump and the tail jerk kWritheJerk deg.
constexpr float kRegrowSec=2.5f,kTailRegrowShare=0.5f,kWritheLegs=1.5f,kWritheStep=3.0f,kWritheJerk=18.0f;
// Dead (`dead` 0..1 over kDeathCurlSec): the legs fold in under it kDeathLegs deg and kick, ever weaker
// (kDeathKick deg at kDeathKickHz, gone by kDeathKickSec); the head droops kDeathHead deg, the tail curls up
// kDeathTail deg: a dead insect's curl, as the wreck falls.
constexpr float kDeathCurlSec=0.6f,kDeathLegs=70.0f,kDeathKick=25.0f,kDeathKickHz=5.0f,kDeathKickSec=3.0f,
                kDeathHead=30.0f,kDeathTail=50.0f;

// A head's / tail's scale grown `grown` (0: hidden, 1: whole) of the way back: an ease-out that overshoots ~10%
// before it settles (the part pops out of the joint).
inline float GrowScale(float grown) {
    const float g=grown<0.0f ? 0.0f : grown>1.0f ? 1.0f : grown,u=g-1.0f;
    const float e=1.0f+2.70158f*u*u*u+1.70158f*u*u;   // back-out easing
    return kHidden+(1.0f-kHidden)*e;
}

// Seconds of game time; m/s it moves; in the air; how much of its head and of its tail shows (0: hidden behind
// another / before one, 1: whole; between: growing back, GrowScale); how hard it writhes (0..1: a split's headless
// part while its head grows); seconds since it died (<0: alive).
struct CentipedeInput { float t,speed; bool flying; float head,tail,writhe,dead; };

// Each part's angle (rad: about its axis, then its axis2) and scale, kCentipedeBones order; `phase`: the legs' step
// phase (rad), stepped by CentipedeStep so its frequency can change without a jump.
inline void CentipedeAngles(const CentipedeInput& in,float phase,float* angle,float* angle2,float* scale) {
    const float two=2.0f*kPi;
    for(int i=0;i<kCentipedeBoneCount;++i){angle[i]=0.0f;angle2[i]=0.0f;scale[i]=1.0f;}
    scale[kHead]=in.head>=1.0f ? 1.0f : GrowScale(in.head);
    scale[kTail]=in.tail>=1.0f ? 1.0f : GrowScale(in.tail);
    if(in.dead>=0.0f) {
        const float curl=in.dead>=kDeathCurlSec ? 1.0f : in.dead/kDeathCurlSec;
        const float fade=in.dead>=kDeathKickSec ? 0.0f : 1.0f-in.dead/kDeathKickSec;
        const float kick=kDeathKick*fade*std::sin(two*kDeathKickHz*in.t);
        angle[kLegL]=kick*kDeg;angle[kLegR]=-kick*kDeg;
        angle2[kLegL]=kDeathLegs*curl*kDeg;angle2[kLegR]=-kDeathLegs*curl*kDeg;   // tips down and in, under it
        angle[kHead]=kDeathHead*curl*kDeg;
        angle[kTail]=kDeathTail*curl*kDeg;
        return;
    }
    const float swing=(in.flying ? kLegAir : kLegSwing)*(1.0f+kWritheLegs*in.writhe);
    angle[kLegL]=swing*std::sin(phase)*kDeg;
    angle[kLegR]=swing*std::sin(phase+kPi)*kDeg;   // the other half of the step
    if(in.writhe>0.0f) {
        const float jerk=kWritheJerk*in.writhe;
        angle[kHead]=jerk*std::sin(two*3.1f*in.t)*kDeg;
        angle[kTail]=jerk*std::sin(two*2.3f*in.t+1.0f)*kDeg;
    }
}

// The legs' phase after `dt` s at `in`'s speed.
inline float CentipedeStep(float phase,const CentipedeInput& in,float dt) {
    float hz=in.flying ? kAirStepHz : in.speed/kStride;
    hz=hz<kStepMin ? kStepMin : hz>kStepMax ? kStepMax : hz;
    hz*=1.0f+(kWritheStep-1.0f)*in.writhe;
    phase+=2.0f*kPi*hz*dt;
    return phase>200.0f*kPi ? phase-200.0f*kPi : phase;
}

// `bind` turned to point its forward (+z) along `dir` (its parent's frame), its up as near the parent's up as that
// allows; its position kept. An aimed mount: the gun's muzzles hang on it, so the barrel goes where it points.
inline void AimLocal(const float* bind,const float* dir,float* out) {
    float f[3]={dir[0],dir[1],dir[2]};
    float l=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if(!(l>1e-4f)){f[0]=0.0f;f[1]=0.0f;f[2]=1.0f;l=1.0f;}
    for(float& c:f)c/=l;
    float r[3]={f[2],0.0f,-f[0]};   // up x forward with up = +y
    l=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(l<1e-4f){r[0]=1.0f;r[2]=0.0f;l=1.0f;}   // straight up or down: any right
    r[0]/=l;r[2]/=l;
    const float u[3]={f[1]*r[2]-f[2]*r[1],f[2]*r[0]-f[0]*r[2],f[0]*r[1]-f[1]*r[0]};
    const float rows[3][3]={{r[0],r[1],r[2]},{u[0],u[1],u[2]},{f[0],f[1],f[2]}};
    for(int row=0;row<3;++row){for(int c=0;c<3;++c)out[row*4+c]=rows[row][c];out[row*4+3]=0.0f;}
    for(int c=0;c<4;++c)out[12+c]=bind[12+c];
}

// The elevation (rad) that lobs a round of `speed` m/s falling at `g` m/s^2 onto a point `across` m away and `up` m
// higher: the high arc (a mortar's: over cover), at most `most`; false when out of its reach.
inline bool LobElevation(float across,float up,float speed,float g,float most,float* out) {
    const float v2=speed*speed,disc=v2*v2-g*(g*across*across+2.0f*up*v2);
    if(disc<0.0f || across<1.0f)return false;
    const float a=std::atan2(v2+std::sqrt(disc),g*across);
    *out=a>most ? most : a;
    return true;
}
}  // namespace primer
