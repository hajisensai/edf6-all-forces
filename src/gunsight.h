// A vehicle gun's sight (the user, 2026-10-08: "vehicles and weapons that have a sight should get a sight HUD, tanks
// above all, and the gunship"): the range ladder of a direct-fire gun, as a tank gunner's sight carries it. Each tick
// is where a round fired now along the bore is when it has gone that far over the ground (level distance from the
// muzzle), so the gunner holds the tick for a target's range on it. The round is flown as the game flies it
// (roundaim.h: the per-frame step, the shooter's share of velocity, AmmoAlive), the same model the pipper is worked out
// by: tick and pipper agree. Pure arithmetic (no EDF.dll): vhud.cpp works the ladder out on the game thread, hud.cpp
// GunReticle draws it, tools/gunsight_check.cpp checks it offline.
#pragma once
#include "roundaim.h"
#include "vecmath.h"
#include <cmath>

namespace crew {
namespace gunsight {
constexpr int kMostTicks=6;
// The ladder's steps, m: the smallest of them that fits the round's reach in kMostTicks ticks.
constexpr float kSteps[]={50.0f,100.0f,200.0f,250.0f,500.0f,1000.0f};
constexpr float kMostReach=3000.0f;   // m: no tick past this (vhud.cpp kReach, the near camera's far clip)

// The level distance from `from` to `p`.
inline float Level(const float* from,const float* p) noexcept {
    const float dx=p[0]-from[0],dz=p[2]-from[2];
    return std::sqrt(dx*dx+dz*dz);
}

// The step for a round that goes `reach` m over the ground in its life (0: none, no ladder).
inline float StepFor(float reach) noexcept {
    if(!(reach>0.0f))return 0.0f;
    const float r=reach<kMostReach ? reach : kMostReach;
    for(float s:kSteps)
        if(r/s<=static_cast<float>(kMostTicks))return s;
    return kSteps[sizeof(kSteps)/sizeof(kSteps[0])-1];
}

struct Ladder {
    int ticks;
    float step;                     // m between ticks
    float at[kMostTicks][3];        // the round when it has gone (k + 1) x step over the ground
    float range[kMostTicks];        // (k + 1) x step
};

// The ladder of a round `r` fired now from the muzzle `pos` along the unit `dir` by a shooter moving `shooter` m/s.
// No ticks for a round that cannot leave (no speed, no life) or that never gets one step away.
inline Ladder Of(const roundaim::Round& r,const float* pos,const float* dir,const float* shooter) noexcept {
    Ladder l{};
    float vel[3];
    roundaim::LaunchVel(r,dir,shooter,vel);
    if(!(r.alive>0) || !std::isfinite(vel[0]+vel[1]+vel[2]))return l;
    // Its reach: the farthest it gets over the ground in its life (a round fired up comes back nearer; the arc's
    // farthest frame is found by flying it).
    float reach=0.0f;
    for(int n=1;n<=r.alive;++n) {
        float p[3];roundaim::At(r,pos,vel,static_cast<float>(n),p);
        const float d=Level(pos,p);
        if(d>reach)reach=d;
    }
    l.step=StepFor(reach);
    if(!(l.step>0.0f))return l;
    float prev[3]={pos[0],pos[1],pos[2]};
    float prevD=0.0f,next=l.step;
    for(int n=1;n<=r.alive && l.ticks<kMostTicks && next<=kMostReach;++n) {
        float p[3];roundaim::At(r,pos,vel,static_cast<float>(n),p);
        const float d=Level(pos,p);
        while(d>=next && d>prevD && l.ticks<kMostTicks && next<=kMostReach) {
            const float u=(next-prevD)/(d-prevD);   // within the frame the round goes straight
            for(int i=0;i<3;++i)l.at[l.ticks][i]=prev[i]+(p[i]-prev[i])*u;
            l.range[l.ticks]=next;
            ++l.ticks;
            next+=l.step;
        }
        if(d<prevD)break;   // past its farthest: coming back
        prev[0]=p[0];prev[1]=p[1];prev[2]=p[2];prevD=d;
    }
    return l;
}
}  // namespace gunsight
}  // namespace crew
