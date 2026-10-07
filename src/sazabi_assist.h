// The Sazabi's aim assist (sazabi_camera.inc Assist; the user, 2026-10-08: 「给高达加上辅助瞄准准星内一定范围自动瞄准」,
// 「类似手柄自动锁定的感觉」). Pure (no game state): tools/sazabi_assist_check.cpp runs it offline.
//  - Pick: of the enemies' lock points, the one nearest the screen's centre within a cone round its ray (`cone` rad from
//    the eye, within `range` m), seen from the eye (the caller drops those behind a wall). The one held last frame keeps
//    it while within `keep` rad (wider than the cone): the assist does not flick between two enemies side by side.
//  - Magnetism: the aim point (every weapon fires at it) is the picked point, not what the centre's ray meets.
//  - Pull: the heading and the aim's pitch eased toward where they would put the picked point at the screen's centre,
//    a share `gain` a second of what is left (exponential), at most `most` rad/s: a pad's lock-on feel; the player's own
//    turn still wins (at the cone's edge the pull is a few deg/s, the stick's full turn 110 deg/s).
#pragma once
#include <cmath>
#include <cstddef>

namespace sazabi::assist {

struct Point { const void* obj; float at[3]; };   // an enemy's lock point (heli.h VisitEnemies)

inline float Angle(const float* dir,const float* eye,const float* at,float* dist) noexcept {
    const float d[3]={at[0]-eye[0],at[1]-eye[1],at[2]-eye[2]};
    const float l=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    *dist=l;
    if(!(l>1e-3f))return 3.2f;
    float c=(d[0]*dir[0]+d[1]*dir[1]+d[2]*dir[2])/l;
    c=c>1.0f ? 1.0f : c<-1.0f ? -1.0f : c;
    return std::acos(c);
}

// The point picked (its index in `pts`), or -1. `held` the object picked last frame (nullptr: none). Each object may have
// several lock points (a giant's weak points): of the held one's, the nearest the centre is kept.
inline int Pick(const float* eye,const float* dir,const Point* pts,int n,float cone,float keep,float range,const void* held) noexcept {
    int best=-1,kept=-1;
    float bestA=cone,keptA=keep;
    for(int i=0;i<n;++i) {
        float dist;
        const float a=Angle(dir,eye,pts[i].at,&dist);
        if(dist>range)continue;
        if(held && pts[i].obj==held && a<=keptA){kept=i;keptA=a;}
        if(a<=bestA){best=i;bestA=a;}
    }
    return kept>=0 ? kept : best;
}

// The registry's order is unrelated to the reticle. Keep the best candidates, including the held object's points,
// even when they are visited after the fixed-size buffer fills. Ranking matches Pick: held first, then angle.
template<std::size_t Capacity>
struct Candidates {
    Point pts[Capacity]{};
    float angles[Capacity]{};
    int n=0;
    void Add(const Point& point,const float* eye,const float* dir,float cone,float keep,float range,const void* held) noexcept {
        float dist;
        const float angle=Angle(dir,eye,point.at,&dist);
        const bool holding=held && point.obj==held;
        if(!std::isfinite(angle) || !std::isfinite(dist) || angle>(holding ? keep : cone) || dist>range)return;
        int at=0;
        while(at<n) {
            const bool otherHeld=held && pts[at].obj==held;
            if((holding && !otherHeld) || (holding==otherHeld && angle<angles[at]))break;
            ++at;
        }
        if(at==static_cast<int>(Capacity))return;
        if(n<static_cast<int>(Capacity))++n;
        for(int i=n-1;i>at;--i){pts[i]=pts[i-1];angles[i]=angles[i-1];}
        pts[at]=point;angles[at]=angle;
    }
};

// MapRay excludes units: a hit just in front of the target is still a wall, never the enemy's hull.
inline bool Visible(float distance,float mapHit) noexcept {
    return std::isfinite(distance) && std::isfinite(mapHit) && (mapHit<0.0f || mapHit>=distance-0.02f);
}

// The heading and pitch (rad; forward = (sin yaw, 0, cos yaw), pitch up +) that put `at` on the centre's ray of a camera
// whose eye is `eye(yaw, pitch)`: the eye moves as the view turns (it orbits behind the mech), so a few rounds of
// "aim from where the eye would be".
template<class Eye>
void Solve(const float* at,float yaw,float pitch,Eye eye,float* outYaw,float* outPitch) noexcept {
    for(int round=0;round<4;++round) {
        float e[3];
        eye(yaw,pitch,e);
        const float d[3]={at[0]-e[0],at[1]-e[1],at[2]-e[2]};
        const float flat=std::sqrt(d[0]*d[0]+d[2]*d[2]);
        if(!(flat>1e-3f))break;
        yaw=std::atan2(d[0],d[2]);
        pitch=std::atan2(d[1],flat);
    }
    *outYaw=yaw;*outPitch=pitch;
}

inline float WrapPi(float a) noexcept {
    constexpr float kPi=3.14159265f;
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}

// One frame's pull of `now` toward `want` (rad): `gain` 1/s of what is left, at most `most` rad/s. The change.
inline float Pull(float now,float want,float gain,float most,float dt) noexcept {
    const float off=WrapPi(want-now);
    const float share=1.0f-std::exp(-gain*dt);
    float step=off*share;
    const float cap=most*dt;
    step=step>cap ? cap : step<-cap ? -cap : step;
    return step;
}

}  // namespace sazabi::assist
