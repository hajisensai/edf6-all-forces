// Small vector helpers shared by the 506 bodies' owners (subcarrier.cpp, carrierlaser.cpp, playerjet.cpp,
// body506.cpp). In their own namespace so that a file with private helpers of the same names (jet.cpp, heli.cpp)
// can include this without an ambiguity: a user pulls in the ones it uses with `using vec::Dot;`.
#pragma once
#include <cmath>
#include <cstring>

namespace crew {
namespace vec {
inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
inline float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
inline void Cross(const float* a,const float* b,float* out) noexcept {
    const float c[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    std::memcpy(out,c,12);
}
// `a` made unit length; false (left as it was) when it has none to speak of.
inline bool Normalize(float* a) noexcept {
    const float l=Len(a);
    if(!std::isfinite(l) || l<1e-4f)return false;
    a[0]/=l;a[1]/=l;a[2]/=l;
    return true;
}
// `cur` moved toward `want` by at most `step`.
inline float Approach(float cur,float want,float step) noexcept { return cur+Clamp(want-cur,-step,step); }
inline float Dist(const float* a,const float* b) noexcept {
    const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    return Len(d);
}
inline float Flat(const float* a,const float* b) noexcept {
    const float dx=a[0]-b[0],dz=a[2]-b[2];
    return std::sqrt(dx*dx+dz*dz);
}
// Body frame (m: rows right, up, nose, position; veh+0x60) <-> world.
inline void ToLocal(const float* m,const float* p,float* out) noexcept {
    const float rel[3]={p[0]-m[12],p[1]-m[13],p[2]-m[14]};
    out[0]=Dot(rel,m);out[1]=Dot(rel,m+4);out[2]=Dot(rel,m+8);
}
inline void ToWorld(const float* m,const float* l,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=m[12+i]+m[i]*l[0]+m[4+i]*l[1]+m[8+i]*l[2];
}
}  // namespace vec
}  // namespace crew
