// Model-space helpers for the Proteus deployment and visible directional shield.
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
constexpr int kPanels=36;
// Every mesh is a ten-degree panel centred on +Z. Its own rigid skin bone
// gives each panel the angle needed by the configured shield arc.
inline void Panel(int index,float arcDeg,float yaw,bool visible,float* out) noexcept {
    Identity(out);
    if(!std::isfinite(arcDeg) || !std::isfinite(yaw))visible=false;
    const float arc=std::fmin(360.0f,std::fmax(10.0f,arcDeg));
    const int count=static_cast<int>(std::ceil(arc/10.0f));
    if(!visible || index>=count){out[0]=out[5]=out[10]=0.0f;return;}
    constexpr float rad=0.0174532925199433f;
    const float width=arc/static_cast<float>(count);
    const float angle=yaw+(-arc*0.5f+(static_cast<float>(index)+0.5f)*width)*rad;
    const float scale=std::tan(width*0.5f*rad)/std::tan(5.0f*rad);
    out[0]=scale*std::cos(angle);out[2]=-scale*std::sin(angle);
    out[8]=std::sin(angle);out[10]=std::cos(angle);
}
} // namespace proteus::pose
