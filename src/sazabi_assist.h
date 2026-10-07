// The Sazabi's aim assist (sazabi_camera.inc Assist; the user, 2026-10-08: 「给高达加上辅助瞄准准星内一定范围自动瞄准」,
// 「类似手柄自动锁定的感觉」). Pure (no game state): tools/sazabi_assist_check.cpp runs it offline.
//  - Pick: of the enemies' lock points, the one nearest the screen's centre within a cone round its ray (`cone` rad from
//    the eye, within `range` m), seen from the eye (the caller drops those behind a wall). The one held last frame keeps
//    it while within `keep` rad (wider than the cone): the assist does not flick between two enemies side by side.
//  - Magnetism: the aim point (every weapon fires at it) is the picked point, not what the centre's ray meets.
//  - Pull: the heading and the aim's pitch eased toward where they would put the picked point at the screen's centre,
//    a share `gain` a second of what is left (exponential), at most `most` rad/s: a pad's lock-on feel; the player's own
//    turn still wins (at the cone's edge the pull is a few deg/s, the stick's full turn 110 deg/s).
//  - The lock-on (the user: 「只狼什么的也会拉镜头吧，按下锁定以后」): its key held enemy picked as Pick does, pulled onto
//    hard (the same Pull, a stronger gain) on the mouse as on a pad; a flick of the stick or the mouse switches (Switch).
#pragma once
#include <cmath>

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

// The lock-on's switch (the right stick / the mouse flicked while locked, as Sekiro's): of the other enemies' points
// within `range` and `most` rad of the view's heading `yaw`, the next one round from the locked point `from` the way
// the flick turned (`turn` > 0: the heading's way up, its left; < 0 its right: the yaw of a point is atan2(x, z) from
// the eye, as the heading's). The point picked, or -1 (none that way).
inline int Switch(const float* eye,float yaw,const float* from,const Point* pts,int n,const void* held,float turn,float range,
                  float most) noexcept {
    const float base=std::atan2(from[0]-eye[0],from[2]-eye[2]);
    int best=-1;
    float bestStep=1e9f;
    for(int i=0;i<n;++i) {
        if(pts[i].obj==held)continue;
        const float d[3]={pts[i].at[0]-eye[0],pts[i].at[1]-eye[1],pts[i].at[2]-eye[2]};
        if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>range*range)continue;
        const float y=std::atan2(d[0],d[2]);
        if(std::fabs(WrapPi(y-yaw))>most)continue;
        const float step=WrapPi(y-base)*(turn>0.0f ? 1.0f : -1.0f);
        if(step>1e-3f && step<bestStep){best=i;bestStep=step;}
    }
    return best;
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
