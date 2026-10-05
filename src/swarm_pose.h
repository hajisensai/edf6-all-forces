// The Primer swarm drone's moving parts (jet_swarm.cpp SwarmPose; tools/swarm_pose_sim.cpp runs the same code
// offline and tools/swarm_pose_view.py renders what it writes). The drone is the plugin's own dragonfly model
// (pylib/dragonfly_model.py): its wings and abdomen segments are bones under `body`, bound level, which the V506
// animation leaves alone, so the plugin writes their local matrices (bone record +0x70, as the jets' elevons:
// docs/mdb-format.md §3); the engine makes world = local x parent's world each frame.
//   wings wing_fl / wing_fr / wing_bl / wing_br: about local z (their roots), + lifts a right wing's tip, so a left
//     one takes the opposite sign; the fore and hind pairs beat in opposition, as a dragonfly's do;
//   abdomen abd1 -> abd4 (a chain): about local x, - bends the tail down; armed it curls down and forward under
//     the body (each segment kCurl), a stinger brought to bear: the warning before it fires.
// Plain math, no game memory: header only, so the simulator compiles it unchanged.
#pragma once
#include <cmath>

namespace swarm {
enum Axis { kAxisX, kAxisY, kAxisZ };
struct PoseBone { const wchar_t* name; Axis axis; };
inline constexpr PoseBone kDroneBones[]={
    {L"wing_fl",kAxisZ},{L"wing_fr",kAxisZ},{L"wing_bl",kAxisZ},{L"wing_br",kAxisZ},
    {L"abd1",kAxisX},{L"abd2",kAxisX},{L"abd3",kAxisX},{L"abd4",kAxisX},
};
constexpr int kDroneBoneCount=static_cast<int>(sizeof(kDroneBones)/sizeof(kDroneBones[0]));
enum DroneBone { kWingFL, kWingFR, kWingBL, kWingBR, kAbd1, kAbd2, kAbd3, kAbd4 };

constexpr float kPi=3.14159265f,kDeg=kPi/180.0f;
// Wings: kBeatHz a second (a real one's 30 would alias at 60 frames), kBeatAmp deg each way about kWingRest up;
// armed (hovering at its target) kArmHz, kArmAmp; a wreck's hang at kWingWreck.
constexpr float kWingRest=4.0f,kBeatHz=6.0f,kBeatAmp=30.0f,kArmHz=9.0f,kArmAmp=22.0f,kWingWreck=-25.0f;
// Abdomen: a sway of kSway deg per segment at kSwayHz (a wave down the tail), plus the curl (0..1) kCurl deg per
// segment; a wreck's hangs half curled.
constexpr float kSway=3.0f,kSwayHz=0.7f,kCurl=-18.0f,kWreckCurl=0.5f;
// The curl moves at kCurlRate (of 0..1) a second; the guns fire only curled this far: the warning.
constexpr float kCurlRate=1.6f,kCurlFire=0.85f;

// What the parts follow: seconds of game time, whether it wants its sting out, a wreck.
struct DroneInput { float t; bool arm,wreck; };

// The curl's next value (0..1) after `dt` s.
inline float CurlStep(float curl,const DroneInput& in,float dt) {
    const float want=in.wreck ? kWreckCurl : in.arm ? 1.0f : 0.0f;
    const float step=kCurlRate*dt;
    return curl+(want-curl>step ? step : want-curl<-step ? -step : want-curl);
}

// Each part's angle (rad, kDroneBones order) at `in` with the abdomen curled `curl`.
inline void DroneAngles(const DroneInput& in,float curl,float* out) {
    const float two=2.0f*kPi;
    const float hz=in.arm ? kArmHz : kBeatHz,amp=in.arm ? kArmAmp : kBeatAmp;
    const float fore=in.wreck ? kWingWreck : kWingRest+amp*std::sin(two*hz*in.t);
    const float hind=in.wreck ? kWingWreck : kWingRest+amp*std::sin(two*hz*in.t+kPi);
    out[kWingFR]=fore*kDeg;out[kWingFL]=-fore*kDeg;
    out[kWingBR]=hind*kDeg;out[kWingBL]=-hind*kDeg;
    for(int s=0;s<4;++s) {
        const float sway=in.wreck ? 0.0f : kSway*std::sin(two*kSwayHz*in.t-0.6f*static_cast<float>(s));
        out[kAbd1+s]=(sway+kCurl*curl)*kDeg;
    }
}

// local = R(axis, angle) x bind (row vectors, 4x4 row-major: rows right, up, forward, position).
inline void TurnLocal(const float* bind,Axis axis,float angle,float* out) {
    const float c=std::cos(angle),s=std::sin(angle);
    float r[9];
    if(axis==kAxisX){const float m[9]={1,0,0, 0,c,s, 0,-s,c};for(int i=0;i<9;++i)r[i]=m[i];}
    else if(axis==kAxisY){const float m[9]={c,0,-s, 0,1,0, s,0,c};for(int i=0;i<9;++i)r[i]=m[i];}
    else {const float m[9]={c,s,0, -s,c,0, 0,0,1};for(int i=0;i<9;++i)r[i]=m[i];}
    for(int row=0;row<3;++row)
        for(int col=0;col<4;++col)
            out[row*4+col]=r[row*3]*bind[col]+r[row*3+1]*bind[4+col]+r[row*3+2]*bind[8+col];
    for(int col=0;col<4;++col)out[12+col]=bind[12+col];
}
}  // namespace swarm
