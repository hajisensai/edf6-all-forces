// A vehicle weapon's round in flight as the game steps it (docs/hud-re.md §7), kept apart from the game so it can be
// checked off it (tools/rounds_check.cpp builds this header alone; no EDF.dll in here).
//  - Arc: every round class whose update leaves its velocity to the bullet core's BulletControl (SolidBullet01 and
//    Rail, RocketBullet01, GrenadeBullet01, LaserBullet01, FlameBullet02, AcidBullet01...): a frame, v += drop, then
//    p += v (m/frame, m/frame^2; 0x233DC4 / 0x234AA7: semi-implicit Euler).
//  - Motor: MissileBullet01 flying straight (Ammo_CustomParameter[0] 0, the rockets: the 409's, the Depth Crawler's,
//    the Begaruta's). Its update (slot 5, 0x26A880) keeps two velocities of its own, `own` (+0x13D0, along the rail: the
//    round's AmmoSpeed at spawn) and `inh` (+0x13E0, the shooter's velocity it took on), and its age (+0x1400, from 0):
//      before ignition (age < CP[7][0]): inh = inh * CP[7][1]; own = own * CP[7][2] (1 if not given);
//                                        inh += the frame's gravity (+0x13F0 = AmmoGravityFactor g / 3600)
//      from ignition on: inh *= 0.9 (0x17A1D80); own's length += CP[4] (never below 0, 0x4D940), then at most
//                        CP[6] (0x23F4E0); its direction never turns (type 0)
//    then hands the core own + inh (x 60, m/s: 0x235540), and the core's BulletControl adds its gravity once more
//    before moving it: a frame moves the round by own + inh + drop. So the drop does not build up after ignition (inh
//    is rewritten from its own state each frame): a powered rocket sinks a fixed drop a frame; before ignition it
//    falls with inh, its fall damped by CP[7][1] towards drop / (1 - CP[7][1]).
// The impact: the first ground along the path, a map ray a segment of frames (FirstHit, with the caller's ray).
#pragma once
#include <cmath>
#include <cstring>

namespace crew {
namespace rounds {
struct Arc { float vel[3],drop[3]; };
inline void Step(Arc& a,float* p) noexcept {
    for(int i=0;i<3;++i){a.vel[i]+=a.drop[i];p[i]+=a.vel[i];}
}

constexpr float kInhDecay=0.9f;   // 0x26AA26: the inherited velocity a powered frame keeps
struct Motor {
    float own[3],inh[3],drop[3];  // m/frame; drop m/frame^2 (the core's gravity a frame)
    float accel,top;              // CP[4] m/frame^2 along own; CP[6] own's top speed (m/frame)
    float keepInh,keepOwn;        // CP[7][1], CP[7][2]: what a frame before ignition keeps of each
    int ignite,age;               // CP[7][0] frames before the motor burns; the round's age (frames)
};
// 0x4D940: `v` given the length `len` (none below 0); a zero vector stays as it is.
inline void SetLength(float* v,float len) noexcept {
    const float l2=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
    if(l2==0.0f)return;
    const float k=(len>0.0f ? len : 0.0f)/std::sqrt(l2);
    for(int i=0;i<3;++i)v[i]*=k;
}
// 0x23F4E0: `v` no longer than `most`.
inline void Limit(float* v,float most) noexcept {
    const float l2=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
    if(most*most>=l2)return;
    const float k=most/std::sqrt(l2);
    for(int i=0;i<3;++i)v[i]*=k;
}
inline void Step(Motor& m,float* p) noexcept {
    if(m.age<m.ignite) {
        for(int i=0;i<3;++i){m.inh[i]*=m.keepInh;m.own[i]*=m.keepOwn;m.inh[i]+=m.drop[i];}
    } else {
        for(float& x:m.inh)x*=kInhDecay;
        SetLength(m.own,std::sqrt(m.own[0]*m.own[0]+m.own[1]*m.own[1]+m.own[2]*m.own[2])+m.accel);
        Limit(m.own,m.top);
    }
    for(int i=0;i<3;++i)p[i]+=m.own[i]+m.inh[i]+m.drop[i];
    ++m.age;
}

// The first ground along a round's path from `from`: stepped `segment` frames at a time, the ray `ray(a, b, hit)`
// (metres from a to the hit, < 0 none) along each stretch; at most `frames` frames and no farther than `reach` m from
// `from` (the view's reach: a round past it lands nowhere the player can see). `hit` and the frames it took (a share of
// the stretch's frames by the ray's distance). False: none; `end` gets where the round stopped (its life's end or the
// reach) and `took` its frames.
template<class Flight,class Ray>
bool FirstHit(Flight f,const float* from,int frames,int segment,float reach,Ray ray,float* hit,float* end,float* took) noexcept {
    float p[3]={from[0],from[1],from[2]};
    for(int n=0;n<frames;n+=segment) {
        const float a[3]={p[0],p[1],p[2]};
        const int steps=frames-n<segment ? frames-n : segment;
        for(int k=0;k<steps;++k)Step(f,p);
        const float d[3]={p[0]-a[0],p[1]-a[1],p[2]-a[2]};
        const float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        const float at=ray(a,p,hit);
        if(at>=0.0f) {
            *took=static_cast<float>(n)+(length>1e-3f ? at/length : 0.0f)*static_cast<float>(steps);
            return true;
        }
        const float o[3]={p[0]-from[0],p[1]-from[1],p[2]-from[2]};
        if(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]>reach*reach){std::memcpy(end,p,12);*took=static_cast<float>(n+steps);return false;}
    }
    std::memcpy(end,p,12);
    *took=static_cast<float>(frames);
    return false;
}
}  // namespace rounds
}  // namespace crew
