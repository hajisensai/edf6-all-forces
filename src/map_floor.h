// The floor along a map ray, for caves and indoor maps (src/heli.cpp MapFloorRay drives it with the game's map ray;
// tools/map_cmd_check.cpp checks it offline on a made-up cave). No game, no Windows.
// The user, 2026-10-06: "3d地图要对洞穴图适配一下" -- in a cave the map's camera is over the cave's roof. The game draws
// the roof's triangles one-sided (from inside the cave), so from above the map shows the floor, but the collision is
// hit from both sides: the map ray under the pointer stopped on the roof's back, a guard point was set up there, the
// formation's slots were put on the roof by a ray from the sky, and the map's focus sat on the roof's height.
//  - A hit on a triangle's back is seen from where the game draws nothing: the ray goes on past it (Floor). Which side
//    is the back needs the triangle's own normal; whether the hit's normal (collector +0x40, docs/raycast-re.md §2.2)
//    is the triangle's own or one turned to face the ray is learned once from a floor under the player (Learn): seen
//    from above and from just below, its own normal is up both times, a facing one flips. Until it is known (or when it
//    faces the ray) no hit is a back: the old first hit.
//  - The ground under a point (Near): of the floors (front faces) on a vertical ray, the one nearest the height asked
//    about, so a unit, the focus or a slot on a lower level of a cave is not put on the level above.
#pragma once
#include <cmath>
#include <cstdint>

namespace mapfloor {
enum class Normals : std::uint8_t { unknown, own, facing };
constexpr float kBackDot=0.05f;   // a hit is on the back when the ray runs along its own normal by more than this
constexpr float kStep=0.05f;      // m past a skipped hit the next ray starts
constexpr int kMaxHits=8;         // hits a pick looks at
constexpr int kMaxHitsDown=24;    // hits a vertical ray from high over a point looks at (each cave level over it
                                  // costs two: its roof's back and its floor)

// A floor under the player seen from above (`fromAbove`: the hit's normal) and from just below it (`fromBelow`).
// `own` is for good (a facing normal never looks like the triangle's own from both sides); `facing` may be a thin
// slab's underside or a second body just under the floor seen from below, so it is only kept until a floor shows
// `own` (heli.cpp LearnMapNormals keeps asking now and then).
inline Normals Learn(const float* fromAbove,const float* fromBelow) noexcept {
    if(!(fromAbove[1]>0.5f))return Normals::unknown;   // not a floor: nothing learned
    if(fromBelow[1]>0.5f)return Normals::own;
    if(fromBelow[1]<-0.5f)return Normals::facing;
    return Normals::unknown;
}

// A hit seen from its back: along unit `dir`, normal `n`.
inline bool Back(Normals k,const float* dir,const float* n) noexcept {
    return k==Normals::own && dir[0]*n[0]+dir[1]*n[1]+dir[2]*n[2]>kBackDot;
}

// Walks the segment a->b hit by hit (`ray(from, to, hit, normal)`: metres to the nearest hit, or < 0 with none), each
// front hit handed to `take(hit, metres from a)` until it returns true. False when none was taken.
template<class Ray,class Take>
bool Walk(const float* a,const float* b,Normals k,Ray ray,Take take,int most=kMaxHits) noexcept {
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(!(len>1e-3f) || !std::isfinite(len))return false;
    const float dir[3]={d[0]/len,d[1]/len,d[2]/len};
    float from[3]={a[0],a[1],a[2]},gone=0.0f;
    for(int i=0;i<most;++i) {
        float hit[3],n[3];
        const float m=ray(from,b,hit,n);
        if(!(m>=0.0f) || !std::isfinite(m))return false;
        gone+=m;
        if(!Back(k,dir,n) && take(hit,gone))return true;
        gone+=kStep;
        if(gone>=len)return false;
        for(int j=0;j<3;++j)from[j]=hit[j]+dir[j]*kStep;
    }
    return false;
}

// The first front hit along a->b: metres from a (`hit` the point), or -1.
template<class Ray>
float Floor(const float* a,const float* b,Normals k,Ray ray,float* hit) noexcept {
    float at=-1.0f;
    Walk(a,b,k,ray,[&](const float* h,float m){ hit[0]=h[0];hit[1]=h[1];hit[2]=h[2];at=m;return true; });
    return at;
}

// Of the front hits on the vertical ray from `top` down to `bottom` (x, z the same) that `ok(point)` takes, the height
// nearest `y`: true with `h`.
template<class Ray,class Ok>
bool Near(const float* top,const float* bottom,float y,Normals k,Ray ray,Ok ok,float* h) noexcept {
    bool any=false;
    Walk(top,bottom,k,ray,[&](const float* p,float){
        const bool accepted=ok(p);
        if(accepted && (!any || std::fabs(p[1]-y)<std::fabs(*h-y))){*h=p[1];any=true;}
        // Only an accepted floor below y bounds the remaining candidates. A rejected floor can hide a lower,
        // standable level closer to y than the best floor above it.
        return accepted && p[1]<y;
    },kMaxHitsDown);
    return any;
}
template<class Ray>
bool Near(const float* top,const float* bottom,float y,Normals k,Ray ray,float* h) noexcept {
    return Near(top,bottom,y,k,ray,[](const float*){ return true; },h);
}
}  // namespace mapfloor
