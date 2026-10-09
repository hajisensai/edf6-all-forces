// Model-space helpers for the Proteus deployment (its support piles, proteus_visual.inc).
#pragma once
#include "proteus_logic.h"
#include "exhaust_pose.h"
#include <cmath>
#include <cstring>

namespace proteus::pose {
inline float Deployed(const State& s,const Tunables& k) noexcept {
    if(s.mode==Mode::walk)return 0.0f;
    if(s.mode==Mode::deployed)return 1.0f;
    const float t=StaggerShare(s,k);
    return s.mode==Mode::deploying ? t : 1.0f-t;
}
inline void Identity(float* out) noexcept {
    for(int i=0;i<16;++i)out[i]=i%5==0 ? 1.0f : 0.0f;
}
// A translation in world coordinates converted into the bone parent's coordinates.
// This preserves the mesh's skinning axes: the two models' pile axes differ.
inline bool MoveWorld(const float* local,const float* world,const float* delta,float* out) noexcept {
    float invLocal[16],parent[16],invParent[16];
    if(!crew::exhaust::Inverse(local,invLocal))return false;
    crew::exhaust::Mul(invLocal,world,parent);
    if(!crew::exhaust::Inverse(parent,invParent))return false;
    std::memcpy(out,local,64);
    for(int i=0;i<3;++i)out[12+i]+=delta[0]*invParent[i]+delta[1]*invParent[4+i]+delta[2]*invParent[8+i];
    return true;
}
inline void Point(const float* p,const float* matrix,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=p[0]*matrix[i]+p[1]*matrix[4+i]+p[2]*matrix[8+i]+matrix[12+i];
}
} // namespace proteus::pose
