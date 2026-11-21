// Catch guidance is expressed at the native boarding point, not the model/rigid-body origin.
// The catch jet is a wing all the way in (playerjet.cpp AutoFly: the player jet's own flight, Air): nothing here sets its
// velocity. Until 2026-11-21 its last 250 m were a "formation" that wrote the velocity whole (the parachute's drift plus a
// closing term), so a jet at 100+ m/s took a new speed and direction in one frame and slid sideways and down under the
// player with its nose held still (test hub #42: 「凭空出现速度的平移」). Now it flies a pass at the door's point and the
// player is captured into its seat within kCaptureRange of it (CaptureDoor; playerjet.cpp Catch: the boarding gun's way in).
#pragma once
#include "vecmath.h"
#include <cmath>

namespace crew { namespace pjet {
// m from the door within which a passing catch jet takes the parachuting player aboard. A wing at 70-100 m/s passes a
// point in a few frames, and the native seat reach (~3 m) is narrower than its pass can be aimed: the capture is the
// catch's own mechanism (the player is moved into the seat), never the jet's velocity being set to meet them.
constexpr float kCaptureRange=20.0f;

// The body target one physics step ahead: where the body must be for its door to be on the player. The door moves with
// both translation and rotation; predict the *relative* motion. Leading by player velocity alone leaves a permanent lead
// of a whole second (6 m for a parachute).
inline void CatchDoor(const float* playerPosition,const float* playerVelocity,const float* body,const float* door,
                      const float* bodyVelocity,const float* angularVelocity,float dt,float* target) noexcept {
    const float offset[3]={door[0]-body[0],door[1]-body[1],door[2]-body[2]};
    float spin[3];vec::Cross(angularVelocity,offset,spin);
    for(int i=0;i<3;++i) {
        const float relative=playerVelocity[i]-bodyVelocity[i]-spin[i];
        target[i]=body[i]+playerPosition[i]-door[i]+relative*dt;
    }
}

// The way a catch jet flying at `vel` steers for a point `to` from it (unit, into `aim`), planned on the level (the
// parachute comes down at 6 m/s: the pass is a level one), its tightest turn `radius` m (review 2026-11-21: pursued
// straight at, a player 250 m abeam, inside the jet's turn, was circled 2.8 times in 45 s; one behind it was looped over,
// 700 m up):
//  - inside the circle of its tightest turn (closer than 2 r sin(its bearing) on the level) it cannot turn in on them:
//    it goes straight on (`*extending`) until they are kCatchRoom times that far, then turns in (a pass round, not a
//    circle about them: between the two it keeps to what it was doing, or it would ride the circle's edge);
//  - more than kCatchTurnIn off its heading it turns level at that (a banked turn at its full lift, never a loop);
//  - its climb or dive toward their height no steeper than kCatchSlope.
// True: it is sent straight at them (else turning or going on to turn: AutoFly slows it to its corner speed, its
// tightest turn).
constexpr float kCatchRoom=1.6f;     // x its tightest turn's circle: room enough to turn in after going straight on
constexpr float kCatchTurnIn=1.0f;   // rad (57 degrees) off its heading at most it is sent toward
constexpr float kCatchSlope=0.5f;    // its climb or dive, rise / run, at most
inline bool CatchAim(const float* vel,const float* to,float radius,bool* extending,float* aim) noexcept {
    const float v=std::hypot(vel[0],vel[2]),d=std::hypot(to[0],to[2]);
    float h[2]={0.0f,1.0f};   // its heading on the level (none: north)
    if(v>1e-3f){h[0]=vel[0]/v;h[1]=vel[2]/v;}
    float dir[2]={h[0],h[1]};
    bool onIt=d<=1e-3f;
    if(d>1e-3f) {
        const float t[2]={to[0]/d,to[2]/d};
        const float sine=h[0]*t[1]-h[1]*t[0],cosine=h[0]*t[0]+h[1]*t[1];
        const float circle=2.0f*radius*std::fabs(sine);
        if(d<circle)*extending=true;
        else if(d>=circle*kCatchRoom)*extending=false;
        if(*extending){}   // straight on
        else if(std::atan2(std::fabs(sine),cosine)>kCatchTurnIn) {
            const float a=sine>0.0f ? kCatchTurnIn : -kCatchTurnIn,c=std::cos(a),s=std::sin(a);
            dir[0]=h[0]*c-h[1]*s;dir[1]=h[0]*s+h[1]*c;
        } else {dir[0]=t[0];dir[1]=t[1];onIt=true;}
    }
    const float slope=d>1e-3f ? to[1]/d : (to[1]>0.0f ? kCatchSlope : -kCatchSlope);
    aim[0]=dir[0];aim[1]=std::fmax(-kCatchSlope,std::fmin(kCatchSlope,slope));aim[2]=dir[1];
    vec::Normalize(aim);
    return onIt;
}

// Whether the door at `gap` m from the player (its native reach `reach`) captures them this frame.
constexpr bool CaptureDoor(float gap,float reach) noexcept { return gap<(reach>kCaptureRange ? reach : kCaptureRange); }
} }  // namespace crew::pjet
