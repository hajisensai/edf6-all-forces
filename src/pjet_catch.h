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

// Whether the door at `gap` m from the player (its native reach `reach`) captures them this frame.
constexpr bool CaptureDoor(float gap,float reach) noexcept { return gap<(reach>kCaptureRange ? reach : kCaptureRange); }
} }  // namespace crew::pjet
