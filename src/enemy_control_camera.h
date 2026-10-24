// The controlled enemy's camera conventions, shared by the sampler, the native movement
// converter and the plugin-owned 4.1 controllers. EDF's world is +Y up with the heading
// `yaw` looking along (sin yaw, 0, cos yaw); the screen's right of that heading is
// (-cos yaw, 0, sin yaw): +X is the LEFT of +Z (map_cam.h Forward/Right, which the M map
// pans with). Turning right lowers the heading; a positive pitch looks up.
// Native pad record axes (docs/enemy-control-native-re.md, docs/heli-input-re.md):
// +0x0C right positive, +0x24 forward NEGATIVE (0x56D920 writes -1 for the forward key),
// +0x3C right positive, +0x54 down positive; the mouse +0x66C/+0x684 right/down positive.
#pragma once
#include <cmath>

namespace crew::enemy_control {
inline void CameraForward(float yaw,float out[3]) noexcept { out[0]=std::sin(yaw);out[1]=0.0f;out[2]=std::cos(yaw); }
inline void CameraRight(float yaw,float out[3]) noexcept { out[0]=-std::cos(yaw);out[1]=0.0f;out[2]=std::sin(yaw); }
// Semantic move (right, up, forward) relative to the camera heading -> a world vector.
inline void CameraMoveToWorld(float yaw,const float move[3],float out[3]) noexcept {
    float right[3],forward[3];CameraRight(yaw,right);CameraForward(yaw,forward);
    for(int i=0;i<3;++i)out[i]=right[i]*move[0]+forward[i]*move[2];
    out[1]+=move[1];
}
// Native pad/mouse look axes (right/down positive) turn the camera: right lowers the
// heading, down lowers the pitch (the pitch is elevation).
constexpr float kLookYawRate=0.04f,kLookPitchRate=0.025f;
inline float TurnYaw(float yaw,float lookRight) noexcept {
    return std::remainder(yaw-lookRight*kLookYawRate,6.28318530717958647692f);
}
inline float TurnPitch(float pitch,float lookDown) noexcept {
    const float next=pitch-lookDown*kLookPitchRate;
    return next<-1.2f ? -1.2f : next>1.2f ? 1.2f : next;
}
// The pad's left stick as semantic movement: forward is -[+0x24].
inline float StickForward(float raw24) noexcept { return -raw24; }
}
