// The first person's procedural motion (firstperson.cpp; the user, 2026-10-12: the viewmodel's animation made in
// code), no game state: tools/fp_view_check.cpp runs it offline.
// One step a game frame takes what the soldier did this frame (Cue) and moves a set of damped springs; their state is
// the viewmodel's offset in the camera's own frame (x right, y up, z ahead, m) and its turn (pitch up, yaw right, roll
// right, rad) about a pivot near the grip. Every channel rests at 0, so a soldier standing still with nothing happening
// is shown exactly as the view transform puts it.
//  - sway: turning the view leaves the weapon behind a little (the look's change this frame, opposite, eased back);
//  - bob: walking and running swing it along a figure eight with the distance covered (on the ground only);
//  - land: a landing dips it as hard as the fall was fast;
//  - recoil: each shot kicks it back and up by the weapon's own recoil;
//  - reload: turned on its side, a little up and in, while the magazine is changed (the grip sits below the view's
//    edge; lowering it put the whole weapon out of view, measured);
//  - switch: lowered out of view and raised again when the weapon changes;
//  - move: the Ranger's sprint lowers and turns it in (Call of Duty's sprint: a run held, the gun down); a burst of
//    thrust (the Fencer's dash and boost, the Wing Diver's flight) is Titanfall 2's / DOOM Eternal's dash: the weapon
//    stays aimed, only dragged back a little by the push (the user, 2026-10-14: the Fencer's dash took the sprint's
//    turn and swung its two weapons across the view, again at every dash); a roll or an evade drops it.
#pragma once
#include <cmath>

namespace crew {
namespace fpmotion {
enum class Move { none,sprint,dash,roll };

// What the soldier did this frame.
struct Cue {
    float lookYaw,lookPitch;    // the view's turn this frame, rad (yaw right, pitch up)
    float speed;                // horizontal speed, m/frame
    bool grounded;
    float fallSpeed;            // m/frame downward at a landing this frame, else 0
    int shots;                  // rounds fired this frame
    float recoil;               // the weapon's recoil strength (0..1 after scaling)
    bool reloading;
    bool switched;              // the weapon in hand changed this frame
    Move move;
};

// The viewmodel's offset and turn now.
struct Pose { float x,y,z,pitch,yaw,roll; };

struct Spring { float at,vel; };

// One damped spring step toward `want` (stiffness k, damping d a frame; semi-implicit Euler, stable for k < 1).
inline void Step(Spring& s,float want,float k,float d) noexcept {
    s.vel+=(want-s.at)*k-s.vel*d;
    s.at+=s.vel;
}

struct State {
    Spring x,y,z,pitch,yaw,roll;
    float phase;                // the bob's phase, rad
    int switchFrames;           // frames left of the lower-and-raise
};

constexpr float kSwayGain=0.6f,kSwayMost=0.08f;     // rad of turn per rad looked, at most
constexpr float kBobStride=1.4f;                     // m a full figure eight
constexpr float kBobSide=0.012f,kBobUp=0.008f,kBobRun=0.15f;   // m at walking pace; the pace the bob is full at, m/frame
constexpr float kLandGain=0.25f,kLandMost=0.06f;    // m of dip per m/frame of fall, at most
constexpr float kRecoilBack=0.035f,kRecoilUp=0.05f; // m back and rad up a shot at full recoil
constexpr int kSwitchFrames=24;                      // the lower-and-raise, frames (half down, half up)
constexpr float kK=0.18f,kD=0.45f;                   // the springs (about a fifth of a second to settle)

inline float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }

inline Pose Advance(State& s,const Cue& c) noexcept {
    // Wanted rest of each channel this frame.
    float wx=0.0f,wy=0.0f,wz=0.0f,wp=0.0f,wyaw=0.0f,wr=0.0f;
    // Sway: a kick against the look, the spring brings it back.
    s.yaw.vel+=Clamp(-c.lookYaw*kSwayGain,-kSwayMost,kSwayMost);
    s.pitch.vel+=Clamp(-c.lookPitch*kSwayGain,-kSwayMost,kSwayMost);
    // Bob: along the distance walked.
    const float pace=c.grounded ? Clamp(c.speed/kBobRun,0.0f,1.0f) : 0.0f;
    s.phase=std::fmod(s.phase+c.speed/kBobStride*6.2831853f,6.2831853f);
    wx+=std::sin(s.phase)*kBobSide*pace;
    wy+=-std::fabs(std::cos(s.phase))*kBobUp*pace;
    // Land: a push down at once.
    if(c.fallSpeed>0.0f)s.y.vel-=Clamp(c.fallSpeed*kLandGain,0.0f,kLandMost);
    // Recoil: each shot a push back and up.
    if(c.shots>0) {
        const float r=Clamp(c.recoil,0.0f,1.0f)*static_cast<float>(c.shots<3 ? c.shots : 3);
        s.z.vel-=kRecoilBack*r;
        s.pitch.vel+=kRecoilUp*r;
    }
    // Reload: on its side, a little up and toward the middle, while it lasts.
    if(c.reloading){wx-=0.03f;wy+=0.02f;wp+=0.10f;wr+=0.45f;}
    // Switch: down out of view, then up.
    if(c.switched)s.switchFrames=kSwitchFrames;
    if(s.switchFrames>0) {
        if(s.switchFrames>kSwitchFrames/2){wy-=0.25f;wp-=0.6f;}
        --s.switchFrames;
    }
    // The stock moves.
    if(c.move==Move::sprint){wx+=0.05f;wy-=0.06f;wp-=0.35f;wyaw-=0.45f;wr+=0.2f;}
    if(c.move==Move::dash){wy-=0.015f;wz-=0.04f;}
    if(c.move==Move::roll){wy-=0.15f;wp-=0.5f;}
    Step(s.x,wx,kK,kD);Step(s.y,wy,kK,kD);Step(s.z,wz,kK,kD);
    Step(s.pitch,wp,kK,kD);Step(s.yaw,wyaw,kK,kD);Step(s.roll,wr,kK,kD);
    return Pose{s.x.at,s.y.at,s.z.at,s.pitch.at,s.yaw.at,s.roll.at};
}
}  // namespace fpmotion
}  // namespace crew
