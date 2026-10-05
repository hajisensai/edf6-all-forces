// An unguided round's arc against a moving target, as the game flies it (no EDF.dll in here: tools/heli_fire_check.cpp
// runs it offline). The bullet core (0x235D50 -> BulletControl 0x233CB0: NapalmBullet01, GrenadeBullet01 and the guns'
// SolidBullet01 alike, autoturret/docs/re-notes.md "Rounds in flight", H) spawns a round at its muzzle with velocity
// direction x AmmoSpeed (m/frame) + the shooter's velocity x AmmoOwnerMove / 60, and steps it once a frame: v += drop,
// p += v (drop = AmmoGravityFactor x the world gravity / 3600, m/frame^2), with no drag, for AmmoAlive frames. So after
// n frames it is at pos + n vel + drop n(n+1)/2 (sight.h RoundAfter's closed form).
//  - Pass: where that arc comes nearest a target moving at a steady velocity (frame by frame, the segment between two
//    frames' relative positions searched for its nearest point), and how near.
//  - Solve: the direction to fire in so the arc meets the target where it will be (the lead and the drop both): the aim
//    point is moved back by the arc's miss and the arc flown again, until the miss is gone (or stops shrinking: out
//    of reach). Exact for the per-frame model, not the parabola's approximation.
#pragma once
#include "vecmath.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace roundaim {
// A round as its weapon fires it: AmmoSpeed (m/frame), the frame's fall (m/frame^2, a world vector: AmmoGravityFactor x
// gravity / 3600), AmmoOwnerMove (the share of the shooter's velocity it keeps) and AmmoAlive (frames).
struct Round { float speed,drop[3],ownerMove; int alive; };

// The round's velocity (m/frame) leaving along the unit `dir` from a shooter moving at `shooter` m/s.
inline void LaunchVel(const Round& r,const float* dir,const float* shooter,float* vel) noexcept {
    for(int i=0;i<3;++i)vel[i]=dir[i]*r.speed+shooter[i]*r.ownerMove/60.0f;
}

// Where the round fired from `pos` with velocity `vel` (m/frame) is `n` frames on.
inline void At(const Round& r,const float* pos,const float* vel,float n,float* out) noexcept {
    const float fall=0.5f*n*(n+1.0f);
    for(int i=0;i<3;++i)out[i]=pos[i]+n*vel[i]+r.drop[i]*fall;
}

// The arc's nearest pass by the target (`target` now, moving `tvel` m/s): `miss` m apart at frame `frames` (fractional),
// the round then at `round` and the target at `where`. ok false: no arc (no speed, no life).
struct Pass { float miss,frames,round[3],where[3]; bool ok; };

inline Pass Nearest(const Round& r,const float* pos,const float* vel,const float* target,const float* tvel) noexcept {
    Pass p{1e30f,0.0f,{pos[0],pos[1],pos[2]},{target[0],target[1],target[2]},false};
    if(!(r.alive>0) || !std::isfinite(vel[0]+vel[1]+vel[2]))return p;
    float prev[3]={pos[0]-target[0],pos[1]-target[1],pos[2]-target[2]};
    for(int n=1;n<=r.alive;++n) {
        float at[3];At(r,pos,vel,static_cast<float>(n),at);
        const float fn=static_cast<float>(n)/60.0f;
        const float cur[3]={at[0]-target[0]-tvel[0]*fn,at[1]-target[1]-tvel[1]*fn,at[2]-target[2]-tvel[2]*fn};
        // The nearest point to the origin on the segment prev -> cur (the relative motion is straight within a frame).
        const float seg[3]={cur[0]-prev[0],cur[1]-prev[1],cur[2]-prev[2]};
        const float len2=vec::Dot(seg,seg);
        const float u=len2>1e-12f ? vec::Clamp(-vec::Dot(prev,seg)/len2,0.0f,1.0f) : 1.0f;
        const float closest[3]={prev[0]+seg[0]*u,prev[1]+seg[1]*u,prev[2]+seg[2]*u};
        const float d=vec::Len(closest);
        if(d<p.miss) {
            p.miss=d;p.frames=static_cast<float>(n-1)+u;p.ok=true;
            const float t=p.frames/60.0f;
            for(int i=0;i<3;++i){p.where[i]=target[i]+tvel[i]*t;p.round[i]=p.where[i]+closest[i];}
        }
        std::memcpy(prev,cur,12);
    }
    return p;
}

// The nearest pass of a round fired now along the unit `dir` from `pos`, by a shooter moving at `shooter` m/s.
inline Pass Fire(const Round& r,const float* pos,const float* dir,const float* shooter,const float* target,const float* tvel) noexcept {
    float vel[3];LaunchVel(r,dir,shooter,vel);
    return Nearest(r,pos,vel,target,tvel);
}

// The unit direction to fire along from `pos` (shooter moving `shooter` m/s) so the round meets the target (`target`,
// moving `tvel` m/s): `dir`, and its nearest pass with that direction in `pass`. False when no direction gets within
// `close` m (out of reach in its life, or no speed): `dir` is then the best one found.
inline bool Solve(const Round& r,const float* pos,const float* shooter,const float* target,const float* tvel,float close,float* dir,
                  Pass* pass) noexcept {
    float aim[3]={target[0],target[1],target[2]};
    float best[3]={target[0]-pos[0],target[1]-pos[1],target[2]-pos[2]};
    if(!vec::Normalize(best)){best[0]=0.0f;best[1]=-1.0f;best[2]=0.0f;}
    Pass bestPass{};bestPass.ok=false;bestPass.miss=1e30f;
    for(int step=0;step<12;++step) {
        float d[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
        if(!vec::Normalize(d))break;
        const Pass p=Fire(r,pos,d,shooter,target,tvel);
        if(!p.ok)break;
        if(p.miss<bestPass.miss){bestPass=p;std::memcpy(best,d,12);}
        if(p.miss<0.05f)break;
        // Move the aim point back by the miss (where the round was minus where the target was at the pass).
        for(int i=0;i<3;++i)aim[i]-=p.round[i]-p.where[i];
    }
    std::memcpy(dir,best,12);
    if(pass)*pass=bestPass;
    return bestPass.ok && bestPass.miss<close;
}

// The miss of a cone's centre that the cone's own scatter already makes (m at `range`): the game draws a shot's
// angle off the centre uniformly in [0, cone] (fire 0x691B02 -> 0x4E820, common/edf/weapon.h), half of it the median.
inline float Scatter(float cone,float range) noexcept { return 0.5f*cone*range; }

// The fire gate for an unguided round: a pass `range` m from the muzzle is worth a shot when its miss is within what
// the cone scatters anyway there (at least `hit` m: a hit radius or the round's blast) and that scatter itself is at
// most `most` m (farther, the shot is the scatter's, not the aim's). `tol` gets the miss allowed.
inline bool Worth(const Pass& p,float range,float cone,float hit,float most,float* tol) noexcept {
    const float spread=Scatter(cone,range);
    *tol=spread>hit ? spread : hit;
    return p.ok && spread<=most && p.miss<=*tol;
}
}  // namespace roundaim
}  // namespace crew
