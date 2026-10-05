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
// Centipede:
//   legs leg_<segment>_l / _r: about local y (fore and aft), the two of a segment in opposition and each segment a
//     step behind the one before (the wave that runs down a centipede), faster the faster it goes;
//   body segF1 / segF2 / segB1 / segB2: about local y, an S-wave along it (smaller in the air); linked, the front
//     half also bends toward the one ahead and the rear half toward the one behind (LinkBend), so a long one
//     curves as one body instead of a row of straight ones with gaps on the outside of its turns;
//   head / tail: shrunk to their joints (kHidden) when it is linked into a longer creature, so that one shows a
//     single head at its front and a single tail at its end. Split, they grow back (GrowScale: over kRegrowSec for
//     the head of the part behind, half that for the tail of the part ahead, ending with a small overshoot), and
//     while the head grows the headless part writhes (`writhe`: its legs and its S-wave faster and wider).
// Plain math, no game memory: header only, so the simulator compiles it unchanged.
#pragma once
#include <cmath>

namespace primer {
enum Axis { kAxisX, kAxisY, kAxisZ };
struct PoseBone { const wchar_t* name; Axis axis; };
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

// --- The dragonfly ---
inline constexpr PoseBone kDragonflyBones[]={
    {L"wing_fl",kAxisZ},{L"wing_fr",kAxisZ},{L"wing_bl",kAxisZ},{L"wing_br",kAxisZ},
    {L"abd1",kAxisX},{L"abd2",kAxisX},{L"abd3",kAxisX},{L"abd4",kAxisX},
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
inline constexpr PoseBone kCentipedeBones[]={
    {L"segF1",kAxisY},{L"segF2",kAxisY},{L"segB1",kAxisY},{L"segB2",kAxisY},{L"head",kAxisY},{L"tail",kAxisY},
    {L"leg_segF2_l",kAxisY},{L"leg_segF2_r",kAxisY},{L"leg_segF1_l",kAxisY},{L"leg_segF1_r",kAxisY},
    {L"leg_body_l",kAxisY},{L"leg_body_r",kAxisY},{L"leg_segB1_l",kAxisY},{L"leg_segB1_r",kAxisY},
    {L"leg_segB2_l",kAxisY},{L"leg_segB2_r",kAxisY},
};
constexpr int kCentipedeBoneCount=static_cast<int>(sizeof(kCentipedeBones)/sizeof(kCentipedeBones[0]));
enum CentipedeBone { kSegF1, kSegF2, kSegB1, kSegB2, kHead, kTail, kLeg0 };   // legs: front segment first, l then r
constexpr int kLegSegments=5;
// Legs: kStride m a step (its frequency is speed / kStride, kStepMin..kStepMax Hz), kLegSwing deg each way on the
// ground, kLegAir in the air at kAirStepHz; each segment kLegLag of a step behind the one before.
constexpr float kStride=2.2f,kStepMin=0.8f,kStepMax=4.0f,kLegSwing=24.0f,kLegAir=12.0f,kAirStepHz=1.3f,kLegLag=0.18f;
// Body: kWave deg (kWaveAir in the air) at kWaveHz, each joint kWaveLag rad behind (front +, rear the S's other way).
constexpr float kWave=7.0f,kWaveAir=4.0f,kWaveHz=0.9f,kWaveLag=0.9f;
constexpr float kHidden=0.02f;   // a hidden head / tail's scale
// s a split's new front takes to grow its head back (primer.cpp holds it out of the fight meanwhile); its new
// tail's at the other side, kTailRegrowShare of that. Writhing, the S-wave and the legs' swing are (1 + kWritheWave /
// kWritheLegs x writhe) times as wide and the legs kWritheStep times as quick at most.
constexpr float kRegrowSec=2.5f,kTailRegrowShare=0.5f,kWritheWave=2.0f,kWritheLegs=1.0f,kWritheStep=3.0f;

// A head's / tail's scale grown `grown` (0: hidden, 1: whole) of the way back: an ease-out that overshoots ~10%
// before it settles (the part pops out of the joint).
inline float GrowScale(float grown) {
    const float g=grown<0.0f ? 0.0f : grown>1.0f ? 1.0f : grown,u=g-1.0f;
    const float e=1.0f+2.70158f*u*u*u+1.70158f*u*u;   // back-out easing
    return kHidden+(1.0f-kHidden)*e;
}
constexpr float kMaxBend=0.7f;   // rad a half of it bends toward a neighbour at most

// The bend (rad about y, + toward its right) that turns a half pointing `along` its body (+: the way it points)
// toward a neighbour `right` m to its right and `along` m along: the front half takes it for the one ahead
// (along its forward), the rear half the negative of it for the one behind (along its back).
inline float LinkBend(float right,float along) {
    const float a=std::atan2(right,along>0.1f ? along : 0.1f);
    return a>kMaxBend ? kMaxBend : a<-kMaxBend ? -kMaxBend : a;
}

// Seconds of game time; m/s it moves; in the air; how much of its head and of its tail shows (0: hidden behind
// another / before one, 1: whole; between: growing back, GrowScale); its halves' bends toward its neighbours
// (LinkBend; 0: none); how hard it writhes (0..1: a split's headless part while its head grows).
struct CentipedeInput { float t,speed; bool flying; float head,tail,bendFront,bendRear,writhe; };

// Each part's angle (rad) and scale, kCentipedeBones order; `phase`: the legs' step phase (rad), stepped by
// CentipedeStep so its frequency can change without a jump.
inline void CentipedeAngles(const CentipedeInput& in,float phase,float* angle,float* scale) {
    const float two=2.0f*kPi;
    for(int i=0;i<kCentipedeBoneCount;++i){angle[i]=0.0f;scale[i]=1.0f;}
    const float wave=(in.flying ? kWaveAir : kWave)*(1.0f+kWritheWave*in.writhe);
    const float w=two*kWaveHz*in.t;
    angle[kSegF1]=wave*std::sin(w)*kDeg+in.bendFront*0.5f;
    angle[kSegF2]=wave*std::sin(w-kWaveLag)*kDeg+in.bendFront*0.5f;
    angle[kSegB1]=-wave*std::sin(w+kWaveLag)*kDeg+in.bendRear*0.5f;
    angle[kSegB2]=-wave*std::sin(w+2.0f*kWaveLag)*kDeg+in.bendRear*0.5f;
    const float swing=(in.flying ? kLegAir : kLegSwing)*(1.0f+kWritheLegs*in.writhe);
    for(int s=0;s<kLegSegments;++s) {
        const float p=phase-two*kLegLag*static_cast<float>(s);
        angle[kLeg0+2*s]=swing*std::sin(p)*kDeg;            // left
        angle[kLeg0+2*s+1]=swing*std::sin(p+kPi)*kDeg;      // right: the other half of the step
    }
    scale[kHead]=in.head>=1.0f ? 1.0f : GrowScale(in.head);
    scale[kTail]=in.tail>=1.0f ? 1.0f : GrowScale(in.tail);
}

// The legs' phase after `dt` s at `in`'s speed.
inline float CentipedeStep(float phase,const CentipedeInput& in,float dt) {
    float hz=in.flying ? kAirStepHz : in.speed/kStride;
    hz=hz<kStepMin ? kStepMin : hz>kStepMax ? kStepMax : hz;
    hz*=1.0f+(kWritheStep-1.0f)*in.writhe;
    phase+=2.0f*kPi*hz*dt;
    return phase>200.0f*kPi ? phase-200.0f*kPi : phase;
}
}  // namespace primer
