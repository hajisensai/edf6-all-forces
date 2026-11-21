// A winged craft's velocity steered as a wing steers it: its speed changes along its path (thrust / brake) and its path
// turns (lift across it), nothing pushes it sideways. Used where the plugin itself sets a wing's velocity outside its own
// flight model: a Dreadnought's hangar bringing a fighter home from afar (dreadnought_hangar.inc FlyTo). Until
// 2026-11-21 that closed the velocity on the one wanted as a whole at 22 m/s^2, in any direction: a fighter called home
// at 160 m/s braked and slid sideways onto its new line while its nose swung round after it (test hub #42:
// 「飞机现在降落会平移过来...凭空出现速度的平移」). Pure math (no EDF.dll): tests/wing_turn_test.cpp.
#pragma once
#include <cmath>

namespace crew {
namespace wing {
inline float Len3(const float* a) noexcept { return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }

// One step of `dt` s: `vel` (m/s) toward `want`, its speed by at most `accel` m/s^2 along the path, its path turned toward
// want's direction at most `turn` m/s^2 across it (a rate of turn / speed rad/s). `lateral` (may be null) gets the
// acceleration across the path this step took (the lift a wing would bank into). From (near) rest it starts along `want`.
inline void Step(float* vel,const float* want,float accel,float turn,float dt,float* lateral) noexcept {
    if(lateral)lateral[0]=lateral[1]=lateral[2]=0.0f;
    const float speed=Len3(vel),wantSpeed=Len3(want);
    float next=wantSpeed-speed;
    const float most=accel*dt;
    next=speed+(next>most ? most : next< -most ? -most : next);
    if(next<0.0f)next=0.0f;
    if(speed<1e-3f) {   // no path yet: it sets off along `want`
        for(int i=0;i<3;++i)vel[i]=wantSpeed>1e-3f ? want[i]/wantSpeed*next : 0.0f;
        return;
    }
    float dir[3]={vel[0]/speed,vel[1]/speed,vel[2]/speed};
    if(wantSpeed>1e-3f) {
        const float to[3]={want[0]/wantSpeed,want[1]/wantSpeed,want[2]/wantSpeed};
        float c=dir[0]*to[0]+dir[1]*to[1]+dir[2]*to[2];
        c=c>1.0f ? 1.0f : c< -1.0f ? -1.0f : c;
        const float angle=std::acos(c),step=turn*dt/speed;
        if(angle<=step)for(int i=0;i<3;++i)dir[i]=to[i];
        else {
            // The unit across the path toward `to` (straight back: turned round over the world's up, else across x).
            float across[3]={to[0]-dir[0]*c,to[1]-dir[1]*c,to[2]-dir[2]*c};
            float l=Len3(across);
            if(l<1e-4f){across[0]=-dir[2];across[1]=0.0f;across[2]=dir[0];l=Len3(across);}
            if(l<1e-4f){across[0]=0.0f;across[1]=-dir[2];across[2]=dir[1];l=Len3(across);}
            for(float& a:across)a/=l;
            const float co=std::cos(step),si=std::sin(step);
            for(int i=0;i<3;++i)dir[i]=dir[i]*co+across[i]*si;
        }
    }
    for(int i=0;i<3;++i) {
        const float was=vel[i];
        vel[i]=dir[i]*next;
        if(lateral)lateral[i]=(vel[i]-was)/dt;
    }
    if(lateral) {   // only the part across the new path: the change of speed along it is the engine's
        const float along=lateral[0]*dir[0]+lateral[1]*dir[1]+lateral[2]*dir[2];
        for(int i=0;i<3;++i)lateral[i]-=dir[i]*along;
    }
}
}  // namespace wing
}  // namespace crew
