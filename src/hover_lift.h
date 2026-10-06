// A rotor craft's acceleration budget (jet_flight.cpp Hover; docs/player-jet-re.md §15). Pure math, no EDF.dll:
// tools/hover_lift_check.cpp flies it offline.
//
// Hover closes the craft's velocity on the one it wants in `respond` s at most its kind's thrust (jet_internal.h Kind,
// the carrier's 4 m/s^2), changing that acceleration by at most its lean's jerk a second. One budget for all three
// axes: braking from 40 m/s takes nearly the whole of it, so the ascend key held while braking barely climbed (the user,
// 2026-10-06: 「空母...完全没办法正常开」; the log at 14:57:59-14:58:03: ascend held, -8.3 -> -5.8 m/s). The NPCs fly
// on that one budget as they always have (Budget::lift 0). Under the player a rotor craft has a lift of its own: the
// vertical acceleration it may use, apart from the horizontal (PlayerLift), and the jerk applies to the horizontal
// alone (the lean and the nacelles follow it; the vertical eases in over `respond` already).
//
// The lift: the stock heli's (V506: docs/aircraft-re.md, heli_movement [0][1] = 34, the lift of a full rotor in m/s^2,
// less gravity 9.8: 24.2 m/s^2 over a hover), for a craft heavier than the heli its mass^(-1/3) share of it (the
// square-cube law: lift goes with the rotor area, size^2; mass with size^3; so the excess acceleration with
// 1/size = mass^(-1/3)), never more than the heli's; times ini PlayerRotorLift (1 the default, 0 the NPCs' one budget).
// Never less than the kind's thrust either (Accel): a light craft (the blast drones: 30 m/s^2) climbed on all of it.
// kHeliMass: the V506 is a UH-60 class utility helicopter, ~10 t at its takeoff weight (an assumption: the game has no
// mass for it).
#pragma once
#include <cmath>

namespace crew {
namespace hover {
constexpr float kHeliLift=34.0f-9.8f,kHeliMass=10000.0f;

struct Budget { float thrust,lift,jerk; };   // m/s^2, m/s^2 (0: one budget for all three axes), m/s^3 (0: none)

inline float Len3(const float* a) noexcept { return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }

// The player's lift for a craft of `mass` kg (see the top), times `scale` (ini PlayerRotorLift).
inline float PlayerLift(float mass,float scale) noexcept {
    if(!(mass>0.0f) || !(scale>0.0f))return 0.0f;
    const float share=std::cbrt(kHeliMass/mass);
    return kHeliLift*(share<1.0f ? share : 1.0f)*scale;
}

// This frame's acceleration (`acc`, m/s^2) toward the velocity `want` from `vel`, `prev` the last frame's: Hover's.
inline void Accel(const float* want,const float* vel,float respond,const Budget& b,const float* prev,float dt,float* acc) noexcept {
    for(int i=0;i<3;++i)acc[i]=(want[i]-vel[i])/respond;
    if(b.lift<=0.0f) {   // one budget (the NPCs'): exactly as Hover always did
        const float a=Len3(acc),most=b.thrust;
        if(a>most)for(int i=0;i<3;++i)acc[i]*=most/a;
        if(b.jerk>0.0f) {
            float change[3]={acc[0]-prev[0],acc[1]-prev[1],acc[2]-prev[2]};
            const float c=Len3(change),step=b.jerk*dt;
            if(c>step)for(int i=0;i<3;++i)change[i]*=step/c;
            for(int i=0;i<3;++i)acc[i]=prev[i]+change[i];
        }
        return;
    }
    const float flat=std::sqrt(acc[0]*acc[0]+acc[2]*acc[2]);
    if(flat>b.thrust){acc[0]*=b.thrust/flat;acc[2]*=b.thrust/flat;}
    const float lift=b.lift>b.thrust ? b.lift : b.thrust;   // never less than the one budget gave it standing still
    acc[1]=acc[1]>lift ? lift : acc[1]<-lift ? -lift : acc[1];
    if(b.jerk>0.0f) {
        float change[2]={acc[0]-prev[0],acc[2]-prev[2]};
        const float c=std::sqrt(change[0]*change[0]+change[1]*change[1]),step=b.jerk*dt;
        if(c>step){change[0]*=step/c;change[1]*=step/c;}
        acc[0]=prev[0]+change[0];acc[2]=prev[2]+change[1];
    }
}
}  // namespace hover
}  // namespace crew
