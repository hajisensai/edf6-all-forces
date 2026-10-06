// The split missiles' (MissileBullet02, e.g. Blood Storm) split distance measured to the target's surface: the pure
// part of splitmissile.cpp, no game memory (tests/split_fuse_test.cpp runs it against a simulated flight).
// The stock split test (0x26CF00, docs/split-missile-re.md) splits when the distance from the round to its lock point
// is under Ammo_CustomParameter[12][1] and the angle to it under [12][2]; the lock point is the target's one centre
// bone, so a target bigger than that distance (the DLC egg) is hit on its surface before the round ever splits.
#pragma once
#include <cmath>

namespace split {
// A ray from `a` to `b` that sees `target` alone (every other unit and the map let through): metres from `a` to its
// nearest surface, or < 0 with none. `ctx`: the caster's own.
using Cast=float (*)(void* ctx,const float* a,const float* b,const void* target);

inline float Distance(const float* a,const float* b) noexcept {
    const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}

// Whether a ray's hit on `object` counts: only the lock target's own bodies (a hit with no object never).
inline bool Counts(const void* object,const void* target) noexcept { return object && object==target; }

// Metres from `pos` along the line of sight to `aim` to the first surface of `target`, or -1: none before the lock
// point (a ray with no hit, a lock point outside its target).
inline float SurfaceAlong(const float* pos,const float* aim,const void* target,Cast cast,void* ctx) noexcept {
    const float total=Distance(pos,aim);
    if(!target || !cast || !(total>0.0f))return -1.0f;
    const float d=cast(ctx,pos,aim,target);
    return d>=0.0f && d<total ? d : -1.0f;
}

// Where the stock split test is to see the round: on its line of sight, `surface` m short of the lock point (so its
// distance is the one to the target's surface and its angle, the line of sight unchanged, the same). False, `out`
// untouched: no surface nearer than the lock point.
inline bool Proxy(const float* pos,const float* aim,float surface,float* out) noexcept {
    const float d=Distance(pos,aim);
    if(!(surface>=0.0f) || !(surface<d))return false;
    const float k=surface/d;
    for(int i=0;i<3;++i)out[i]=aim[i]-(aim[i]-pos[i])*k;
    return true;
}
}  // namespace split
