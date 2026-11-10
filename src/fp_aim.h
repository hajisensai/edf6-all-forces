// The first person's aim and stock-action arithmetic (firstperson.cpp), no game state: tools/fp_aim_check.cpp runs it
// offline. docs/fp-soldier-re.md §2.5 / §8 (2026-11-10, test site #32: "looking up or walking, the shots hit the
// ground / a wall / the sky"; "doing an action in the first person the weapon does not vanish and the hands jitter").
//
// Converge: a shot leaves from the real muzzle (the third person's gun, 0x696FD0 reads the muzzle entry's position
// row +0x80) along the entry's forward row (+0x70, read with +0x50/+0x60 by 0x691560). The third person's camera
// looks along the soldier's aim frame from 2.5 m behind and 0.6 m to the left (game_object_camera_setting: the eye
// and the look-at differ in z only), so its centre line runs beside the bullets. The first person moves the eye to the
// head and keeps that direction: the bullets still leave from the gun at the chest, 0.2-0.5 m below and to the side
// of the eye, in parallel. Whatever the eye sees closer than that offset is missed: the ground just ahead on a down
// slope, a wall's edge or the lip of cover the eye clears and the gun does not. A weapon whose muzzle is laid by its
// animation (fire_model_direction > 0, mode 1: the entry's own bone) adds the walk's and the turn's swing of the gun.
// Converge turns the shot's rows (minimal rotation: the roll kept) so the round leaves the real muzzle toward the point
// the eye's centre line meets (the map's first hit, else `reach` along it): what the crosshair is on is what is hit.
#pragma once
#include <cmath>

namespace crew {
namespace fpaim {

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline void Cross(const float* a,const float* b,float* out) noexcept {
    out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];
}
inline bool Finite3(const float* a) noexcept { return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]); }
inline bool Unit3(float* a) noexcept {
    const float n=std::sqrt(Dot(a,a));
    if(!(n>1e-6f) || !std::isfinite(n))return false;
    a[0]/=n;a[1]/=n;a[2]/=n;
    return true;
}

// The limits: the converged point at least kMinAhead m past the muzzle along the view (a wall at the eye's nose would
// otherwise turn the shot sideways or back), a shot turned at most kMostTurn rad (anything more is not this offset:
// a stale view, a gun laid elsewhere by its own animation on purpose; it is left native), the point without a hit kFar
// m along the view (about parallel there: 0.3 m in 1.5 km), the map searched as far.
constexpr float kMinAhead=1.0f,kMostTurn=0.7854f,kFar=1500.0f;

enum class Result { turned,unchanged,refused };

// The point the eye's centre line meets: `hit` m along `dir` (unit) from `eye`, or `reach` without one (hit < 0), and
// never closer than kMinAhead past `muzzle` along the line.
inline void AimPoint(const float* eye,const float* dir,const float* muzzle,float hit,float reach,float* out) noexcept {
    float t=hit>=0.0f ? hit : reach;
    const float m[3]={muzzle[0]-eye[0],muzzle[1]-eye[1],muzzle[2]-eye[2]};
    const float least=Dot(m,dir)+kMinAhead;
    if(t<least)t=least;
    for(int i=0;i<3;++i)out[i]=eye[i]+dir[i]*t;
}

// v turned about the unit axis k by the angle whose cosine is c and sine s (Rodrigues).
inline void Turn(float* v,const float* k,float c,float s) noexcept {
    float kv[3];Cross(k,v,kv);
    const float along=Dot(k,v)*(1.0f-c);
    float out[3];
    for(int i=0;i<3;++i)out[i]=v[i]*c+kv[i]*s+k[i]*along;
    for(int i=0;i<3;++i)v[i]=out[i];
}

// `rows`: the shot's right, up and forward rows (x, y, z, w each: 12 floats, the muzzle entry's +0x50..+0x7F),
// turned in place so the forward row points from `muzzle` to AimPoint. The w column and each row's length are kept.
// refused: not finite, no direction, or a turn past kMostTurn (rows untouched).
inline Result Converge(float* rows,const float* muzzle,const float* eye,const float* dir,float hit,float reach=kFar) noexcept {
    for(int r=0;r<3;++r)if(!Finite3(rows+r*4))return Result::refused;
    if(!Finite3(muzzle) || !Finite3(eye) || !Finite3(dir) || !std::isfinite(hit) || !(reach>0.0f))return Result::refused;
    float d[3]={dir[0],dir[1],dir[2]};
    if(!Unit3(d))return Result::refused;
    float p[3];AimPoint(eye,d,muzzle,hit,reach,p);
    float want[3]={p[0]-muzzle[0],p[1]-muzzle[1],p[2]-muzzle[2]};
    float now[3]={rows[8],rows[9],rows[10]};
    if(!Unit3(want) || !Unit3(now))return Result::refused;
    const float c=Dot(now,want);
    if(c<std::cos(kMostTurn))return Result::refused;
    float k[3];Cross(now,want,k);
    const float s=std::sqrt(Dot(k,k));
    if(!(s>1e-7f))return Result::unchanged;
    for(float& v:k)v/=s;
    for(int r=0;r<3;++r)Turn(rows+r*4,k,c,s);
    return Result::turned;
}

// The stock action's arms (an appeal: 0x59A880 hides every weapon set's weapon through 0x696150, weapon +0x78C = 1,
// and plays the body's animation): the first person shows the arms as that animation poses them and no viewmodel.
// `sets` hand sets, each with a weapon (`armed`) the third person hides (`hidden`): native arms when at least one set is
// armed and every armed set's weapon is hidden.
inline bool NativeArms(int sets,const bool* armed,const bool* hidden) noexcept {
    bool any=false;
    for(int i=0;i<sets;++i) {
        if(!armed[i])continue;
        if(!hidden[i])return false;
        any=true;
    }
    return any;
}

}  // namespace fpaim
}  // namespace crew
