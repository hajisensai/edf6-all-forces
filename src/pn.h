// Proportional navigation, the one guidance law every homing round flies by: the plugin's own missiles (missile.cpp,
// their g limit and navigation constant from their store) and the stock homing rounds of every weapon, the player's,
// the NPCs' and the enemies' (guidance.cpp, their limit from their own stock numbers). Units: m and frames.
// Pure math (no game): tests/pn_test.cpp flies it offline.
#pragma once
#include "vecmath.h"

namespace crew {
namespace pn {
// The acceleration across its path (m a frame^2) that steers a round at `pos` flying `vel` onto a target at `aim`
// moving `tv` (m a frame): `nav` times the line of sight's turn rate crossed with its velocity, its part along the
// velocity taken out, at most `most` long. Zero when the target is within a metre (no line of sight to speak of).
inline void Lateral(const float* pos,const float* vel,const float* aim,const float* tv,float nav,float most,float* a) noexcept {
    using vec::Cross;using vec::Dot;using vec::Len;
    a[0]=a[1]=a[2]=0.0f;
    const float r[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
    const float rv[3]={tv[0]-vel[0],tv[1]-vel[1],tv[2]-vel[2]};
    const float r2=Dot(r,r);
    if(!(r2>1.0f))return;
    float w[3];Cross(r,rv,w);
    for(auto& x:w)x/=r2;   // the line of sight's turn, rad a frame
    Cross(w,vel,a);
    for(int i=0;i<3;++i)a[i]*=nav;
    float dir[3]={vel[0],vel[1],vel[2]};
    if(!vec::Normalize(dir)){a[0]=a[1]=a[2]=0.0f;return;}
    const float along=Dot(a,dir);
    for(int i=0;i<3;++i)a[i]-=dir[i]*along;
    const float len=Len(a);
    if(!std::isfinite(len)){a[0]=a[1]=a[2]=0.0f;return;}
    if(len>most && len>0.0f)for(int i=0;i<3;++i)a[i]*=most/len;
}

// A velocity-steered round (the stock type 1): `vel` turned by `a` (across it) and set to `speed`. False: no
// direction to speak of (left as it was).
inline bool Turn(float* vel,const float* a,float speed) noexcept {
    float dir[3]={vel[0]+a[0],vel[1]+a[1],vel[2]+a[2]};
    if(!vec::Normalize(dir))return false;
    for(int i=0;i<3;++i)vel[i]=dir[i]*speed;
    return true;
}

// `from` (unit) turned toward `to` (unit) by at most `most` rad, into `out` (unit).
inline void Toward(const float* from,const float* to,float most,float* out) noexcept {
    using vec::Dot;
    const float c=vec::Clamp(Dot(from,to),-1.0f,1.0f),angle=std::acos(c);
    if(!(angle>most)){std::memcpy(out,to,12);return;}
    // The part of `to` across `from`: the plane of the turn (opposite: any plane across).
    float across[3]={to[0]-from[0]*c,to[1]-from[1]*c,to[2]-from[2]*c};
    if(!vec::Normalize(across)) {
        const float up[3]={0.0f,1.0f,0.0f},side[3]={1.0f,0.0f,0.0f};
        vec::Cross(from,std::fabs(from[1])<0.9f ? up : side,across);
        if(!vec::Normalize(across)){std::memcpy(out,from,12);return;}
    }
    const float s=std::sin(most),k=std::cos(most);
    for(int i=0;i<3;++i)out[i]=from[i]*k+across[i]*s;
    vec::Normalize(out);
}

// A thrust-steered round (the stock type 2: it pushes `thrust` a frame along its nose and turns its nose at most
// `turn` rad a frame): the nose (unit, into `out`) whose push gives the lateral acceleration `lat` (at most `thrust`
// long) and the rest of the push along its flight `vel`; from the nose it has, `nose`, at most `turn` away.
inline void Thrust(const float* vel,const float* nose,const float* lat,float thrust,float turn,float* out) noexcept {
    using vec::Dot;
    float v[3]={vel[0],vel[1],vel[2]};
    if(!vec::Normalize(v))std::memcpy(v,nose,12);
    float want[3];
    if(thrust>0.0f) {
        const float l2=Dot(lat,lat),t2=thrust*thrust;
        const float along=l2<t2 ? std::sqrt(t2-l2) : 0.0f;
        for(int i=0;i<3;++i)want[i]=(lat[i]+v[i]*along)/thrust;
        if(!vec::Normalize(want))std::memcpy(want,v,12);
    } else std::memcpy(want,v,12);
    Toward(nose,want,turn,out);
}
}  // namespace pn
}  // namespace crew
