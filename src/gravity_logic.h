// The gravity operations, the pure part (no game, no Windows: tools/gravity_check.cpp includes it alone). gravity.cpp moves
// the game's objects with it; the gravity gun (gravitygun.cpp) and anything else that grabs, holds or throws goes through
// gravity.h (docs/gravity-re.md). World metres, seconds, kg, y up.
//  - Hold: a velocity-level spring onto a goal point (the goal's own motion fed forward), its speed capped by the mass: a
//    light thing snaps to the goal, a heavy one is dragged there, past kMostLiftKg not at all (HoldVelocity).
//  - Throw: the speed falls with the mass's fourth root (ThrowSpeed): a heavier throw flies slower but carries more energy,
//    so what it deals grows with its mass and its speed together (the user, 2026-10-20: 「根据速度和质量造成伤害」).
//  - Impact: what a thrown thing loses in a frame beyond its gravity, or the speed it lands with (Flight::Step); its damage is
//    the rams' formula (vehicleram.h ram::Damage: 1/2 m dv^2 at 287 kJ a point) times the ini's GravityDamage.
//  - Mass: the physics' own for a rigid body or a ragdoll; estimates for the characters the game moves without one
//    (docs/gravity-re.md §4: an insect's collision sphere, the families' fixed weights), CharacterKg.
#pragma once
#include "vehicleram.h"
#include <cmath>

namespace gravmath {
constexpr float kWorldGravity=14.7f;    // m/s^2: EDF.dll's hknp world (1.5 g, docs/gravity-re.md §2)
// Hold (HoldVelocity):
constexpr float kHoldTau=0.12f;         // s: the spring's time to close the gap by e (a critically damped feel at 60 fps)
constexpr float kHoldMostSpeed=60.0f;   // m/s: the fastest a light thing is carried
constexpr float kHoldRefKg=2000.0f;     // a thing up to this heavy is carried at full speed
constexpr float kMostLiftKg=60000.0f;   // a heavier thing is not moved at all (a tank of 40 t still is, slowly)
constexpr float kHoldLeastSpeed=4.0f;   // m/s: the cap's floor for anything liftable
// Throw (ThrowSpeed):
constexpr float kThrowRefKg=2000.0f;    // a thing this heavy leaves at the base speed
constexpr float kThrowLeast=12.0f,kThrowMost=90.0f;   // m/s
// Impact (Flight):
constexpr float kImpactLeastDv=6.0f;    // m/s lost in a frame beyond gravity: less is a scrape, not a blow
constexpr float kImpactGapS=0.3f;       // a thing's next impact counts this long after its last at the soonest
constexpr float kFlightMostS=8.0f;      // a throw is followed this long at most
constexpr float kRestSpeed=2.0f,kRestS=0.6f;   // slower than this for this long: it has come to rest, no longer followed
constexpr float kMostStepS=0.1f;        // a longer step (a pause, a load) starts the flight's readings over

inline float Len(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
inline float Clamp(float x,float lo,float hi) noexcept { return x<lo ? lo : x>hi ? hi : x; }

// The fastest a thing of `kg` is carried (m/s): full speed up to kHoldRefKg, then falling with the square root of the mass;
// 0 past kMostLiftKg (or without a mass).
inline float HoldCap(float kg) noexcept {
    if(!(kg>0.0f) || kg>kMostLiftKg)return 0.0f;
    if(kg<=kHoldRefKg)return kHoldMostSpeed;
    const float cap=kHoldMostSpeed*std::sqrt(kHoldRefKg/kg);
    return cap<kHoldLeastSpeed ? kHoldLeastSpeed : cap;
}

// One frame of the hold: the velocity that brings a thing at `at` toward `goal` (moving at `goalVel`) in kHoldTau, capped at
// HoldCap(kg) (the goal's own motion inside the cap: a carried thing keeps pace with its carrier). `out` is the velocity to
// set; false (and `out` zero) when nothing that heavy can be carried.
inline bool HoldVelocity(const float* at,const float* goal,const float* goalVel,float kg,float* out) noexcept {
    const float cap=HoldCap(kg);
    out[0]=out[1]=out[2]=0.0f;
    if(!(cap>0.0f))return false;
    float v[3];
    for(int k=0;k<3;++k)v[k]=(goal[k]-at[k])/kHoldTau+(goalVel ? goalVel[k] : 0.0f);
    const float s=Len(v);
    const float f=s>cap ? cap/s : 1.0f;
    for(int k=0;k<3;++k)out[k]=v[k]*f;
    return std::isfinite(out[0]+out[1]+out[2]);
}

// The speed a throw of `base` m/s gives a thing of `kg` (m/s): base x (kThrowRefKg / kg)^(1/4), within kThrowLeast ..
// kThrowMost and never above `base` x 2. Its energy 1/2 m v^2 then grows with the root of the mass: a car thrown hits harder
// than an ant though it flies slower.
inline float ThrowSpeed(float kg,float base) noexcept {
    if(!(kg>0.0f) || !(base>0.0f))return 0.0f;
    const float v=base*std::pow(kThrowRefKg/kg,0.25f);
    const float hi=base*2.0f<kThrowMost ? base*2.0f : kThrowMost;
    return Clamp(v,kThrowLeast<hi ? kThrowLeast : hi,hi);
}

// The damage of an impact (or a contact) of a thing of `kg` at `dv` m/s, the ini's scale applied (vehicleram.h).
inline float ImpactDamage(float kg,float dv,float scale) noexcept { return ram::Damage(kg,dv,1.0f,scale); }

// A thrown thing followed frame by frame for its impacts.
struct Flight {
    float prev[3]{};
    bool have=false;      // prev is a reading of the last step
    bool airborne=false;  // the last step's landed flag was off
    float age=0.0f,still=0.0f,sinceHit=kImpactGapS;
    // One step of `dt` s with the thing's velocity `vel` and its landed flag (characters: on the ground now; a rigid body:
    // false). Returns the speed lost in an impact this step (0: none): the drop of velocity beyond what gravity explains
    // (|prev - vel| less g dt twice over, the gravity of the families that fall faster), or the speed it was falling with
    // when it lands. `done` is set when the flight is over (at rest, or followed kFlightMostS).
    float Step(const float* vel,bool landed,float dt,bool* done) noexcept {
        *done=false;
        if(!(dt>0.0f) || dt>kMostStepS || !std::isfinite(vel[0]+vel[1]+vel[2])) {
            have=false;
            return 0.0f;
        }
        age+=dt;sinceHit+=dt;
        float dv=0.0f;
        if(have) {
            const float d[3]={prev[0]-vel[0],prev[1]-vel[1],prev[2]-vel[2]};
            dv=Len(d)-2.0f*kWorldGravity*dt;
            if(landed && airborne) {   // a character's landing: its fall (and its drive) stopped at once
                const float fell=Len(prev);
                if(fell>dv)dv=fell;
            }
        }
        for(int k=0;k<3;++k)prev[k]=vel[k];
        have=true;
        airborne=!landed;
        still=Len(vel)<kRestSpeed ? still+dt : 0.0f;
        if(still>=kRestS || age>=kFlightMostS)*done=true;
        if(dv<kImpactLeastDv || sinceHit<kImpactGapS)return 0.0f;
        sinceHit=0.0f;
        return dv;
    }
};

// The characters' weights where the game keeps none (docs/gravity-re.md §4). An insect / berserker: its collision sphere's
// radius (its cling controller's, from its SGO) at kClingDensity (a sphere round a leggy body is mostly air: a giant ant of
// r 1.5 m comes to about 2 t). The walkers' are fixed per family (the game's own capsule mass is 100 for every humanoid
// and monster alike, which tells nothing): a colonist (EDF5's Primer infantry, three times a man's height) 1.5 t, a
// kaiju (Monster501/504) past kMostLiftKg, a human the SGO's or 100 kg.
constexpr float kClingDensity=150.0f;   // kg/m^3 of the bounding sphere (L: a feel, docs/gravity-re.md §4)
constexpr float kPi=3.14159265f;
inline float ClingKg(float radius) noexcept {
    if(!(radius>0.05f) || !std::isfinite(radius))return 1000.0f;
    const float r=radius>20.0f ? 20.0f : radius;
    return kClingDensity*4.0f/3.0f*kPi*r*r*r;
}
enum class Family : int { insect, humanoid, humanoidBoss, monster, human, berserker, walker };
inline float FamilyKg(Family f,float clingRadius) noexcept {
    switch(f) {
    case Family::insect:
    case Family::berserker:return ClingKg(clingRadius);
    case Family::humanoid:return 1500.0f;
    case Family::humanoidBoss:return 3000.0f;
    case Family::monster:return 2.0e6f;      // a kaiju: past kMostLiftKg, never lifted
    case Family::human:return 100.0f;
    case Family::walker:return 2000.0f;      // the other walking characters (mermen, flying dinos, squids)
    }
    return 1000.0f;
}
}  // namespace gravmath
