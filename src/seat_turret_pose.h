// A vehicle's own turret (seat_turret.inc, StoreSpec::turret): where it wants to point and how its two bones are posed.
// Its bones are two leaves of one parent (the hull's `body`: tools/prep_xmorph.py), their binds level (a translation
// each): the yaw bone turns about its upright, the pitch bone (its guns, its muzzles' holder) turns with it and lifts
// about its own across. Angles rad from the bind (0, 0: ahead +z, level); yaw toward +x, pitch up. Matrices are the
// engine's: row vectors, rows the axes, the translation the fourth row (as dreadnought_turrets.inc Pose). Pure:
// tests/seat_turret_test.cpp.
#pragma once
#include <cmath>
#include <cstring>

namespace crew::seat_turret {
constexpr float kPi=3.14159265358979323846f;

inline float Wrap(float a) noexcept {
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}
inline float Clamp(float x,float lo,float hi) noexcept { return x<lo ? lo : x>hi ? hi : x; }

struct Limits { float low,high,arc,yawRate,pitchRate; };   // StoreTurret's
struct Aim { float yaw=0.0f,pitch=0.0f; };

// The angles of a direction `d` in the parent's frame (unnormalised).
inline Aim AnglesOf(const float* d) noexcept {
    return {std::atan2(d[0],d[2]),std::atan2(d[1],std::sqrt(d[0]*d[0]+d[2]*d[2]))};
}
// `want` within the turret's limits: its traverse (arc either side of ahead) and its elevations.
inline Aim Limit(Aim want,const Limits& l) noexcept {
    return {Clamp(Wrap(want.yaw),-l.arc,l.arc),Clamp(want.pitch,l.low,l.high)};
}
// One step of `now` toward `want` (limited) at the turret's rates over `dt` s; the short way round with an all-round
// traverse, else within its arc. True when it is on it (within `on` rad on both axes).
inline bool Swing(Aim& now,Aim want,const Limits& l,float dt,float on) noexcept {
    want=Limit(want,l);
    const bool round=l.arc>=kPi-1e-4f;
    const float dy=round ? Wrap(want.yaw-now.yaw) : want.yaw-now.yaw,dp=want.pitch-now.pitch;
    const float sy=l.yawRate*dt,sp=l.pitchRate*dt;
    now.yaw=Clamp(dy,-sy,sy)+now.yaw;
    now.yaw=round ? Wrap(now.yaw) : Clamp(now.yaw,-l.arc,l.arc);
    now.pitch=Clamp(now.pitch+Clamp(dp,-sp,sp),l.low,l.high);
    const float ey=round ? Wrap(want.yaw-now.yaw) : want.yaw-now.yaw;
    return std::fabs(ey)<on && std::fabs(want.pitch-now.pitch)<on;
}

// The two bones' locals at `a`: `bindYaw` / `bindGun` their bind locals (translations under one parent), `yaw` / `gun`
// out. The yaw bone turns about its origin; the gun bone lifts about its own origin and turns with the yaw bone about
// its origin: its rotation pitch then yaw, its origin the bind's offset from the yaw origin turned.
inline void Pose(const float* bindYaw,const float* bindGun,Aim a,float* yaw,float* gun) noexcept {
    const float cy=std::cos(a.yaw),sy=std::sin(a.yaw),cp=std::cos(a.pitch),sp=std::sin(a.pitch);
    const float Y[9]={cy,0.0f,-sy, 0.0f,1.0f,0.0f, sy,0.0f,cy};
    const float P[9]={1.0f,0.0f,0.0f, 0.0f,cp,-sp, 0.0f,sp,cp};
    std::memcpy(yaw,bindYaw,64);
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)yaw[r*4+c]=Y[r*3+c];
    std::memcpy(gun,bindGun,64);
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)gun[r*4+c]=P[r*3]*Y[c]+P[r*3+1]*Y[3+c]+P[r*3+2]*Y[6+c];
    const float d[3]={bindGun[12]-bindYaw[12],bindGun[13]-bindYaw[13],bindGun[14]-bindYaw[14]};
    for(int c=0;c<3;++c)gun[12+c]=bindYaw[12+c]+d[0]*Y[c]+d[1]*Y[3+c]+d[2]*Y[6+c];
}
}  // namespace crew::seat_turret
