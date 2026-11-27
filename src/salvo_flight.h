// The salvo store's rounds flown without the game (tools/sortie_check.cpp RainClearance; docs/missile-re.md
// §MissileBullet02, docs/split-missile-re.md, docs/guidance-re.md): what the native MissileBullet02 carrier and its
// MissileBullet01 children do with the SGO's numbers (pylib/vcobjects.py SALVO_*, through py_consts.inc) under the
// plugin's PN (src/guidance.cpp, src/pn.h), frame by frame, in m and frames:
//   cold drop  kSalvoDropFrames: its own velocity the rail's kSalvoEject in a kSalvoFan cone (polar angle uniform,
//              0x4E820) round the launcher's nose, not pushed; the inherited (the launcher's) x kSalvoDropKeep a frame
//              plus kSalvoDropGravity's gravity (0x26EDF0)
//   motor      the inherited x0.9 a frame (0x26EC10); its own speed + kSalvoAccel to kSalvoTop; from kSalvoHomingDelay
//              frames on PN (type 1) turns its own velocity, the lateral at most |own| x kSalvoTurn, the line of sight's
//              rate taken against own + inherited (guidance.cpp Steer)
//   split      kSalvoSplitAfter frames after ignition (CP[12] mode 1): kSalvoChildren children from where it is, each in
//              a kSalvoSplitCone cone round its nose (CP[13] mode 0: the carrier's nose, 0x26E765), at
//              kSalvoChildSpeed, on the carrier's lock (0x26C82E), PN after kSalvoChildHomingDelay
// The ground is not in it: a round goes on along what PN asks, and PN knows nothing of the terrain under the line of
// sight. A round fanned off the line of sight by e comes back onto it by a bow below (or beside) it of about
// 0.19 x range x e (N = 3), which is what decides how high the launcher must be (sortie_plan.h kRainAlt).
// Not modelled (each a few metres): the rails' offsets on the airframe, the target's motion, AmmoSize's hit radius
// beyond kHitRadius, the children's inherited velocity (none taken).
#pragma once
#include "pn.h"
#include "py_consts.inc"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace salvo_flight {
constexpr float kWorldGravity=14.7f;   // m/s^2 at gravity factor 1 (jet_internal.h kGravity, measured)
constexpr float kInheritDecay=0.9f;    // the inherited velocity a frame once lit (0x17A1D80)
constexpr float kHitRadius=3.0f;       // m from the lock point: a hit
constexpr float kWasted=20.0f;         // m (level) from the target: a burst on the ground farther off is wasted
constexpr int kLife=1200;              // frames a round is followed (its AmmoAlive is longer; none lasts this long)

struct Rng {
    std::uint32_t s;
    float Unit() noexcept {   // [0, 1)
        s=s*1664525u+1013904223u;
        return static_cast<float>(s>>8)*(1.0f/16777216.0f);
    }
};

// A unit direction at a polar angle uniform in [0, half] round the unit `d`, its azimuth uniform (0x4E820).
inline void Cone(const float* d,float half,Rng& rng,float* out) noexcept {
    const float th=half*rng.Unit(),ph=6.2831853f*rng.Unit();
    const float up[3]={0.0f,1.0f,0.0f},side[3]={1.0f,0.0f,0.0f};
    float u[3];vec::Cross(d,std::fabs(d[1])<0.9f ? up : side,u);
    vec::Normalize(u);
    float w[3];vec::Cross(d,u,w);
    for(int i=0;i<3;++i)out[i]=d[i]*std::cos(th)+(u[i]*std::cos(ph)+w[i]*std::sin(ph))*std::sin(th);
    vec::Normalize(out);
}

// Where the rounds ended: on the lock point, on the ground within kWasted of the target, on it farther off, or still
// in the air after kLife.
struct Tally {
    int hit=0,close=0,wasted=0,air=0;
    float shortest=1e9f;   // the nearest a wasted round came down to the launcher (level, m)
    int Rounds() const noexcept { return hit+close+wasted+air; }
};
using GroundFn=float(*)(float x,float z);   // the ground's height under (x, z)

struct Round {
    float pos[3],own[3],inherited[3];
    float accel,turn,top;
    int delay;
};

// Flies `r` from `t` frames after its ignition (the children's at once). True: it split (at r's place, along its own
// velocity); false: it ended, counted into `tally` against the target `aim` (the launcher at `from`).
inline bool Fly(Round& r,int t,bool splits,const float* aim,const float* from,float nav,GroundFn ground,Tally& tally) noexcept {
    for(;t<kLife;++t) {
        for(float& v:r.inherited)v*=kInheritDecay;
        const float own0=vec::Len(r.own);
        float speed=own0+r.accel;
        if(speed>r.top)speed=r.top;
        if(t>=r.delay) {
            const float vel[3]={r.own[0]+r.inherited[0],r.own[1]+r.inherited[1],r.own[2]+r.inherited[2]};
            const float still[3]={0.0f,0.0f,0.0f};
            float a[3];pn::Lateral(r.pos,vel,aim,still,nav,own0*r.turn,a);
            pn::Turn(r.own,a,speed);
        } else {
            float d[3]={r.own[0],r.own[1],r.own[2]};
            if(vec::Normalize(d))for(int i=0;i<3;++i)r.own[i]=d[i]*speed;
        }
        if(splits && t>=pyconst::kSalvoSplitAfter)return true;
        for(int i=0;i<3;++i)r.pos[i]+=r.own[i]+r.inherited[i];
        if(vec::Dist(r.pos,aim)<=kHitRadius){++tally.hit;return false;}
        if(r.pos[1]<=ground(r.pos[0],r.pos[2])) {
            if(vec::Flat(r.pos,aim)<=kWasted)++tally.close;
            else {
                ++tally.wasted;
                const float back=vec::Flat(r.pos,from);
                if(back<tally.shortest)tally.shortest=back;
            }
            return false;
        }
    }
    ++tally.air;
    return false;
}

// One carrier let go at `pos` by a launcher whose nose is `nose` (unit) flying `vel` (m a frame), on the lock point
// `aim`: it and its children counted into `tally` (a carrier down before its split counts once).
inline void Carrier(const float* pos,const float* nose,const float* vel,const float* aim,float nav,GroundFn ground,Rng& rng,
                    Tally& tally) noexcept {
    Round c{};
    std::memcpy(c.pos,pos,12);
    float d[3];Cone(nose,pyconst::kSalvoFan,rng,d);
    for(int i=0;i<3;++i){c.own[i]=d[i]*pyconst::kSalvoEject;c.inherited[i]=vel[i];}
    c.accel=pyconst::kSalvoAccel;c.turn=pyconst::kSalvoTurn;c.top=pyconst::kSalvoTop;c.delay=pyconst::kSalvoHomingDelay;
    const float g=pyconst::kSalvoDropGravity*kWorldGravity/3600.0f;
    for(int f=0;f<pyconst::kSalvoDropFrames;++f) {
        for(float& v:c.inherited)v*=pyconst::kSalvoDropKeep;
        c.inherited[1]-=g;
        for(int i=0;i<3;++i)c.pos[i]+=c.own[i]+c.inherited[i];
        if(c.pos[1]<=ground(c.pos[0],c.pos[2])){++tally.wasted;return;}
    }
    if(!Fly(c,0,true,aim,pos,nav,ground,tally))return;
    float along[3]={c.own[0],c.own[1],c.own[2]};
    if(!vec::Normalize(along))std::memcpy(along,nose,12);
    for(int k=0;k<pyconst::kSalvoChildren;++k) {
        Round child{};
        std::memcpy(child.pos,c.pos,12);
        float cd[3];Cone(along,pyconst::kSalvoSplitCone,rng,cd);
        for(int i=0;i<3;++i)child.own[i]=cd[i]*(pyconst::kSalvoChildSpeed/60.0f);
        child.accel=pyconst::kSalvoChildAccel;child.turn=pyconst::kSalvoChildTurn;child.delay=pyconst::kSalvoChildHomingDelay;
        child.top=pyconst::kSalvoChildTop*(pyconst::kSalvoChildTopLow+(1.0f-pyconst::kSalvoChildTopLow)*rng.Unit());
        Fly(child,0,false,aim,pos,nav,ground,tally);
    }
}
}  // namespace salvo_flight
}  // namespace crew
