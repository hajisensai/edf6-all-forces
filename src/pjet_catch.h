// Catch guidance is expressed at the native boarding point, not the model/rigid-body origin.
#pragma once
#include "vecmath.h"
#include <cmath>

namespace crew { namespace pjet {
// The body target one physics step ahead and its velocity feed-forward. The door moves with both translation and
// rotation; predict the *relative* motion. Leading by player velocity alone while also feeding that velocity forward
// leaves a permanent lead of a whole second (6 m for a parachute), outside the native boarding radius.
inline void CatchDoor(const float* playerPosition,const float* playerVelocity,const float* body,const float* door,
                      const float* bodyVelocity,const float* angularVelocity,float dt,float* target,float* drift) noexcept {
    const float offset[3]={door[0]-body[0],door[1]-body[1],door[2]-body[2]};
    float spin[3];vec::Cross(angularVelocity,offset,spin);
    for(int i=0;i<3;++i) {
        const float relative=playerVelocity[i]-bodyVelocity[i]-spin[i];
        target[i]=body[i]+playerPosition[i]-door[i]+relative*dt;
        drift[i]=playerVelocity[i]-spin[i];
    }
}

// Formation velocity: close the door's remaining error while following the player's drift. No fixed-wing stall floor
// applies here. With no error it still follows the drift, rather than returning with last frame's stale velocity.
inline float CatchVelocity(const float* body,const float* target,const float* drift,float speed,float gain,float brake,
                           float* velocity) noexcept {
    float error[3]={target[0]-body[0],target[1]-body[1],target[2]-body[2]};
    const float distance=vec::Len(error);
    const float closing=std::fmin(std::fmin(distance*gain,speed),std::sqrt(2.0f*brake*distance));
    for(int i=0;i<3;++i)velocity[i]=drift[i]+(distance>1e-6f ? error[i]*closing/distance : 0.0f);
    return distance;
}
} }  // namespace crew::pjet
