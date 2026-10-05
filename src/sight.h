// The gun sight's and the fighter HUD's math (playerjet.cpp Sight, hud.cpp FighterHud), kept apart from the game so
// it can be checked off it (no EDF.dll in here).
#pragma once
#include "vecmath.h"

namespace crew {
namespace sight {
// The time a round at `speed` m/s from the origin meets a target `d` away moving at `vel` m/s: the least t > 0 with
// |d + vel t| = speed t; -1 never (the target outruns the round).
inline float Intercept(const float* d,const float* vel,float speed) noexcept {
    const float a=vec::Dot(vel,vel)-speed*speed,b=2.0f*vec::Dot(d,vel),c=vec::Dot(d,d);
    if(std::fabs(a)<1e-3f)return b<0.0f ? -c/b : -1.0f;   // as fast as the round: one root
    const float disc=b*b-4.0f*a*c;
    if(disc<0.0f)return -1.0f;
    const float r=std::sqrt(disc),t1=(-b-r)/(2.0f*a),t2=(-b+r)/(2.0f*a);
    const float lo=t1<t2 ? t1 : t2,hi=t1<t2 ? t2 : t1;
    return lo>0.0f ? lo : hi>0.0f ? hi : -1.0f;
}

// Where a round fired now from `pos` along the unit `nose` at `speed` m/s, falling at `drop` m/s^2, is `t` s on.
inline void RoundAt(const float* pos,const float* nose,float speed,float drop,float t,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=pos[i]+nose[i]*speed*t;
    out[1]-=0.5f*drop*t*t;
}

// How many screens off its centre a point may still be drawn (lines run on to it, the screen's edge cuts them).
constexpr float kOffScreen=4.0f;
// The screen point (viewport pixels, y down) of the point `p` (w = 1) or of the direction `p` (w = 0: where it
// vanishes, the horizon's and the pitch ladder's: the same wherever the camera stands) through the row-vector
// view-projection `vp` (docs/hud-re.md §0). False behind the eye or more than kOffScreen screens out. No depth test:
// a direction has none, and a far point is the sight's whatever the far plane.
inline bool ToScreen(const float* vp,const float* p,float w,float width,float height,float* sx,float* sy) noexcept {
    float c[4];
    for(int k=0;k<4;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+w*vp[12+k];
    if(!(c[3]>1e-6f))return false;
    const float x=c[0]/c[3],y=c[1]/c[3];
    if(!(std::fabs(x)<=kOffScreen && std::fabs(y)<=kOffScreen))return false;
    *sx=width*0.5f*(1.0f+x);*sy=height*0.5f*(1.0f-y);
    return true;
}

// The way (a unit vector in pixels, y down) from the screen's centre toward the point `p`, in front of the eye or
// behind it: the clip x and y before the divide by w (the view's right and up, scaled by the projection), so a point
// behind the eye and to its left points left. Straight behind (or ahead): down.
inline void Toward(const float* vp,const float* p,float width,float height,float* dx,float* dy) noexcept {
    float c[2];
    for(int k=0;k<2;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+vp[12+k];
    float x=c[0]*width*0.5f,y=-c[1]*height*0.5f;
    const float l=std::sqrt(x*x+y*y);
    if(!std::isfinite(l) || l<1e-6f){x=0.0f;y=1.0f;}
    else{x/=l;y/=l;}
    *dx=x;*dy=y;
}

// The heading (degrees, 0..360) of the direction `d`, a compass's: 0 along the world's +Z, growing as it turns right
// (the right of +Z is -X: docs/player-jet-re.md §2), so 90 along -X. -1 with no horizontal part.
inline float HeadingOf(const float* d) noexcept {
    if(d[0]*d[0]+d[2]*d[2]<1e-8f)return -1.0f;
    float h=std::atan2(-d[0],d[2])*57.2957795f;
    if(h<0.0f)h+=360.0f;
    return h>=360.0f ? 0.0f : h;
}
}  // namespace sight
}  // namespace crew
