// The Depth Crawler's driving on whatever surface it clings to (src/ground.cpp drives it; tools/crawl_check.cpp checks
// it offline). No game, no Windows.
// The crawler climbs walls and ceilings (docs/camera-re.md: 会爬墙). Its move stick works along its own rows (x along row
// 0 "right", z along row 2 "forward") and its turn input spins it about its own row 1 "up", whatever way those point in
// the world. The user, 2026-10-06: "蜘蛛车爬墙上指挥不动了" -- the driver used to measure the goal and its heading on
// the level plane alone: on a wall the level goal direction has (almost) nothing along the crawler's rows, so the
// stick was zero, and the heading was "undefined", so it never turned. Everything here is in the crawler's own frame:
//  - the way to the goal is the 3D offset projected onto the surface (the plane of rows 0 and 2);
//  - on a floor, arrived means that in-surface distance within the stop (the level distance, as before: a goal far over
//    a flat floor -- the player in a heli -- does not make it circle under them);
//  - on a wall or a ceiling (up less than kFloorUp), arrived means the 3D distance within the stop; a goal that lies off
//    the surface (straight out from the wall, kOffSurface) is reached by going down the wall first, onto the floor, or,
//    lying behind the wall (past a building), up and over it; on a ceiling right over it, it stays;
//  - a turn is measured about the crawler's up: the error to a wanted direction and how far it turned between frames,
//    the same sign convention as atan2(x, z) on a level floor (a positive angle from +z towards +x).
#pragma once
#include <cmath>

namespace crawl {
constexpr float kFloorUp=0.7f;      // up.y at least this: a floor (about 45 deg); less: a wall, a ceiling
constexpr float kOffSurface=0.35f;  // the in-surface share of the 3D offset below which the goal is off this surface

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
// `v` with its part along unit `up` taken out.
inline void Tangent(const float* v,const float* up,float* out) noexcept {
    const float k=Dot(v,up);
    for(int i=0;i<3;++i)out[i]=v[i]-up[i]*k;
}

// The crawler's frame: rows of its world matrix (right, up, forward), up made unit.
struct Frame { float right[3],up[3],fwd[3]; };
inline bool MakeFrame(const float* m,Frame* f) noexcept {
    for(int i=0;i<3;++i){f->right[i]=m[i];f->up[i]=m[4+i];f->fwd[i]=m[8+i];}
    const float l=Len(f->up);
    if(!(l>1e-3f) || !std::isfinite(l))return false;
    for(float& c:f->up)c/=l;
    return true;
}
inline bool OnFloor(const Frame& f) noexcept { return f.up[1]>=kFloorUp; }

// The move towards `goal` from `pos`: the stick (x right, z forward, length at most 1) and whether it moves.
// `stop`: how near it stops; `moving`: it moved last frame (a stopped crawler starts again only `hysteresis` past the
// stop); the stick ramps up over `ramp` m.
inline bool Move(const Frame& f,const float* pos,const float* goal,float stop,bool moving,float hysteresis,float ramp,
                 float* stick) noexcept {
    stick[0]=stick[1]=0.0f;
    const float d[3]={goal[0]-pos[0],goal[1]-pos[1],goal[2]-pos[2]};
    float t[3];Tangent(d,f.up,t);
    const float dist=Len(d),plane=Len(t);
    const bool floor=OnFloor(f);
    const float gap=floor ? plane : dist;
    const float start=stop+(moving ? 0.0f : hysteresis);
    if(!std::isfinite(gap) || gap<start || gap<0.5f)return false;
    float way[3]={t[0],t[1],t[2]};
    if(!floor && plane<kOffSurface*dist) {
        // Off this surface. In front of the wall (out where its face looks): down the wall onto the floor. Behind it
        // (a point past a building, the wall in between): up and over it -- going down there only drives it back into
        // the same wall. On a ceiling neither way lies on it: a point under it is as near as it gets (no stick: the
        // in-surface remainder is noise, normalized it would dart about over the point).
        const float vertical[3]={0.0f,Dot(d,f.up)>=0.0f ? -1.0f : 1.0f,0.0f};
        float g[3];Tangent(vertical,f.up,g);
        if(!(Len(g)>0.2f))return false;
        for(int i=0;i<3;++i)way[i]=g[i];
    }
    const float w=Len(way);
    if(!(w>1e-3f))return false;
    const float mag=std::fmin(std::fmax((gap-stop)/ramp,0.0f),1.0f)/w;
    float x=Dot(way,f.right)*mag,z=Dot(way,f.fwd)*mag;
    const float len=std::sqrt(x*x+z*z);
    if(len>1.0f){x/=len;z/=len;}
    stick[0]=x;stick[1]=z;
    return true;
}

// The signed angle (rad) from `a` to `b` about unit `up`, both taken onto the plane across `up`: positive turns +z
// towards +x on a level floor (atan2(x, z) rising). 0 when either lies (almost) along `up`: no direction on the plane.
inline float AngleAbout(const float* a,const float* b,const float* up) noexcept {
    float ta[3],tb[3];Tangent(a,up,ta);Tangent(b,up,tb);
    if(!(Len(ta)>0.05f*Len(a)) || !(Len(tb)>0.05f*Len(b)) || !(Len(ta)>1e-6f) || !(Len(tb)>1e-6f))return 0.0f;
    // cross(ta, tb) . up, with the game's handedness: on a level floor (0,0,1) -> (1,0,0) is +pi/2.
    const float c[3]={ta[1]*tb[2]-ta[2]*tb[1],ta[2]*tb[0]-ta[0]*tb[2],ta[0]*tb[1]-ta[1]*tb[0]};
    return std::atan2(Dot(c,up),Dot(ta,tb));
}
}  // namespace crawl
