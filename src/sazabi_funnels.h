// The Sazabi's funnels' flight (sazabi_arms.inc runs it each frame for the mech; tools/sazabi_funnel_check.cpp flies it
// offline and draws it). Pure: no game, world metres and seconds, y up.
// After the official games' and the film's funnels (the user, 2026-10-07: 「复刻一下人家的浮游炮运动」; the research,
// from the arcade EXVS2 XB's published weapon data and the setting: the Sazabi's six funnels, three in each pack, launch
// one after another, curve round to their own side of the target, only turn their muzzles on it once there, fire,
// and come home after about three seconds on station):
//  - launch: kLaunchGap s apart, packs alternating (L1 R1 L2 R2 L3 R3), each thrown up and back out of its pack at
//    kLaunchSpeed with a spread, for kLaunchSec;
//  - transit: a cubic Bezier at kCruise m/s to its station, bowing kBow m to one side (no two the same way), the nose
//    along its flight;
//  - station: a point kStationLo..Hi m from its target, spread round it at the golden angle in the order they launched
//    (so six funnels close round it from every side, above it as much as level with it), around the side away from the
//    mech too; there it turns on the target for kAimSec and fires, kShots times kShotGap s apart, hopping kHopLo..Hi m
//    aside between shots in kHopSec (the film's dodging fight);
//  - home: after its shots, or kStationMost s on station, or with no target left, back to its pack at kCruise x
//    kHomeFactor, slowing over its last kDockSlow m, docked within kDockAt m;
//  - no target at all: it hangs round the mech kGuardRadius m off, kGuardHigh m over it, until kSortieMost s are up.
#pragma once
#include <cmath>

namespace sazabi::funnels {

constexpr int kCount=6;
constexpr float kPiF=3.14159265f;
constexpr float kLaunchGap=0.12f,kLaunchSpeed=25.0f,kLaunchSec=0.3f,kLaunchSpread=25.0f*kPiF/180.0f;
constexpr float kCruise=150.0f,kBow=30.0f,kMinTransit=0.45f;
constexpr float kLead=25.0f;   // m at most a course carries on the way it was going before it turns for its end
constexpr float kStationLo=60.0f,kStationHi=90.0f,kElevLo=-20.0f*kPiF/180.0f,kElevHi=40.0f*kPiF/180.0f;
constexpr float kGolden=137.508f*kPiF/180.0f;
constexpr float kAimSec=0.25f,kShotGap=0.5f,kHopLo=15.0f,kHopHi=25.0f,kHopSec=0.35f;
constexpr int kShots=3;
constexpr float kStationMost=3.0f,kSortieMost=8.0f,kHomeFactor=1.2f,kDockSlow=10.0f,kDockAt=1.5f;
constexpr float kGuardRadius=14.0f,kGuardHigh=26.0f,kMostTargets=8;
constexpr float kRecharge=3.5f;   // s a funnel waits in its pack after docking (the gauge the HUD shows)

enum class Phase : int { docked, launching, transit, station, hop, home };
struct Target { float at[3]; };
struct Funnel {
    Phase phase=Phase::docked;
    float at[3]{},vel[3]{},dir[3]{0.0f,0.0f,1.0f};
    float p0[3]{},p1[3]{},p2[3]{},p3[3]{};   // the Bezier it flies (transit, hop, home)
    float u=0.0f,dur=1.0f;                  // its place along it, its time
    float wait=0.0f;                        // launching: till it leaves; station: till its next shot / its aim
    float onStation=0.0f,sortie=0.0f,recharge=0.0f;
    int target=-1,shots=0;
    float offset[3]{};                      // its station from its target
    int order=0;                            // its place in the launch (the golden angle's index)
};
struct Shot { int funnel; float from[3]; float at[3]; };
struct Events { int shots; Shot shot[kCount]; int launched,docked; };

inline float Len3(const float* v) { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
inline void Bezier(const Funnel& f,float u,float* p,float* d) {
    const float a=1.0f-u;
    for(int k=0;k<3;++k) {
        p[k]=a*a*a*f.p0[k]+3.0f*a*a*u*f.p1[k]+3.0f*a*u*u*f.p2[k]+u*u*u*f.p3[k];
        d[k]=3.0f*a*a*(f.p1[k]-f.p0[k])+6.0f*a*u*(f.p2[k]-f.p1[k])+3.0f*u*u*(f.p3[k]-f.p2[k]);
    }
}
// A deterministic 0..1 from two ints (the spreads: no randomness, the same flight each time it is given the same scene).
inline float Hash(int a,int b) {
    unsigned h=static_cast<unsigned>(a)*2654435761u^static_cast<unsigned>(b+1)*2246822519u;
    h^=h>>13;h*=3266489917u;h^=h>>16;
    return static_cast<float>(h&0xFFFFFF)/static_cast<float>(0xFFFFFF);
}

// A Bezier for `f` from where it is to `to`, leaving along its velocity, bowing `bow` m to its `side` (+1 / -1).
inline void Course(Funnel& f,const float* to,float speed,float bow,float side,float least) {
    float d[3]={to[0]-f.at[0],to[1]-f.at[1],to[2]-f.at[2]};
    const float len=Len3(d);
    const float up[3]={0.0f,1.0f,0.0f};
    float s[3]={d[1]*up[2]-d[2]*up[1],d[2]*up[0]-d[0]*up[2],d[0]*up[1]-d[1]*up[0]};   // d x up: across the course
    const float sl=Len3(s);
    if(sl>1e-3f)for(float& c:s)c/=sl;
    const float v=Len3(f.vel);
    for(int k=0;k<3;++k) {
        f.p0[k]=f.at[k];
        f.p1[k]=f.at[k]+(v>1.0f ? f.vel[k]/v : d[k]/(len+1e-3f))*std::fmin(len*0.33f,kLead)+s[k]*bow*side;
        f.p2[k]=to[k]-d[k]*0.33f+s[k]*bow*side;
        f.p3[k]=to[k];
    }
    f.u=0.0f;
    f.dur=std::fmax(least,len*1.25f/speed);   // the curve is about a quarter longer than the line
}

// Its station round target `t`: the golden angle by its launch order, about the line from the mech to the target (its
// far side as much as its near one), at an elevation and a distance spread by the order too.
inline void StationOf(Funnel& f,const float* mech,const Target& t) {
    float base=std::atan2(mech[0]-t.at[0],mech[2]-t.at[2]);   // from the target toward the mech
    const float az=base+kGolden*static_cast<float>(f.order+1);
    const float el=kElevLo+(kElevHi-kElevLo)*Hash(f.order,7);
    const float r=kStationLo+(kStationHi-kStationLo)*Hash(f.order,11);
    f.offset[0]=std::sin(az)*std::cos(el)*r;f.offset[1]=std::sin(el)*r;f.offset[2]=std::cos(az)*std::cos(el)*r;
}

inline void Look(Funnel& f,const float* d) {
    const float l=Len3(d);
    if(l>1e-3f)for(int k=0;k<3;++k)f.dir[k]=d[k]/l;
}

// The flight of every funnel for `dt` s. `docks`: each pack slot's point now (world); `mech`: the mech's body centre;
// `targets` (`n`, nearest first: a funnel keeps its own while it lasts); `launch`: the order to go (every docked and
// charged funnel leaves). The shots fired this step are in the events (the caller makes the beams).
inline Events Step(Funnel* fs,const float (*docks)[3],const float* mech,const float* mechFwd,const Target* targets,int n,
                   bool launch,float dt) {
    Events ev{};
    if(launch) {   // the launch order: packs alternating, kLaunchGap apart
        static constexpr int kSeq[kCount]={0,3,1,4,2,5};
        int next=0;
        for(int i=0;i<kCount;++i) {
            Funnel& f=fs[kSeq[i]];
            if(f.phase!=Phase::docked || f.recharge>0.0f)continue;
            f=Funnel{};
            f.phase=Phase::launching;f.order=i;f.wait=kLaunchGap*static_cast<float>(next++);
            for(int k=0;k<3;++k)f.at[k]=docks[kSeq[i]][k];
        }
    }
    for(int i=0;i<kCount;++i) {
        Funnel& f=fs[i];
        if(f.phase==Phase::docked) {
            for(int k=0;k<3;++k){f.at[k]=docks[i][k];f.vel[k]=0.0f;}
            f.recharge=std::fmax(0.0f,f.recharge-dt);
            continue;
        }
        f.sortie+=dt;
        const bool hasTarget=f.target>=0 && f.target<n;
        if(f.phase==Phase::launching) {
            for(int k=0;k<3;++k)f.at[k]=docks[i][k];
            if((f.wait-=dt)>0.0f)continue;
            // out of the pack: up and back (against the mech's facing) and out to its pack's side, spread
            const float side=i<3 ? 1.0f : -1.0f;
            const float yaw=(Hash(i,3)-0.5f)*2.0f*kLaunchSpread,pitch=(Hash(i,5)-0.5f)*kLaunchSpread;
            float out[3]={-mechFwd[0]*0.6f+mechFwd[2]*side*0.6f,0.8f+pitch,-mechFwd[2]*0.6f-mechFwd[0]*side*0.6f};
            const float c=std::cos(yaw),s=std::sin(yaw);
            const float ox=out[0]*c+out[2]*s,oz=-out[0]*s+out[2]*c;out[0]=ox;out[2]=oz;
            const float l=Len3(out);
            for(int k=0;k<3;++k)f.vel[k]=out[k]/l*kLaunchSpeed;
            f.phase=Phase::transit;f.u=1.0f;f.dur=kLaunchSec;   // flies straight out for kLaunchSec first (u: done)
            f.wait=kLaunchSec;
            ++ev.launched;
            continue;
        }
        if(f.phase==Phase::transit && f.wait>0.0f) {   // the throw out of the pack
            f.wait-=dt;
            for(int k=0;k<3;++k)f.at[k]+=f.vel[k]*dt;
            Look(f,f.vel);
            if(f.wait>0.0f)continue;
            // its target: its order's share of those there (round-robin), its station round it, its course there
            if(n>0){f.target=f.order%n;StationOf(f,mech,targets[f.target]);}
            float to[3];
            if(f.target>=0 && f.target<n)for(int k=0;k<3;++k)to[k]=targets[f.target].at[k]+f.offset[k];
            else {
                const float a=kGolden*static_cast<float>(f.order);
                to[0]=mech[0]+std::sin(a)*kGuardRadius;to[1]=mech[1]+kGuardHigh;to[2]=mech[2]+std::cos(a)*kGuardRadius;
            }
            Course(f,to,kCruise,kBow,(f.order&1) ? 1.0f : -1.0f,kMinTransit);
            continue;
        }
        if(f.phase==Phase::transit || f.phase==Phase::hop || f.phase==Phase::home) {
            // a moving target: the course's end follows it (the station is held round where it is now)
            if(f.phase!=Phase::home && hasTarget)for(int k=0;k<3;++k)f.p3[k]=targets[f.target].at[k]+f.offset[k];
            if(f.phase==Phase::home)for(int k=0;k<3;++k)f.p3[k]=docks[i][k];
            float u=f.u+dt/f.dur;
            if(f.phase==Phase::home) {   // slowing over the last kDockSlow m
                const float d[3]={docks[i][0]-f.at[0],docks[i][1]-f.at[1],docks[i][2]-f.at[2]};
                const float left=Len3(d);
                if(left<kDockSlow)u=f.u+dt/f.dur*std::fmax(0.25f,left/kDockSlow);
            }
            f.u=std::fmin(u,1.0f);
            float p[3],d[3];
            Bezier(f,f.u,p,d);
            for(int k=0;k<3;++k){f.vel[k]=(p[k]-f.at[k])/dt;f.at[k]=p[k];}
            if(f.phase==Phase::hop && hasTarget) {   // a hop keeps its muzzle on the target
                const float* t=targets[f.target].at;
                const float a[3]={t[0]-f.at[0],t[1]-f.at[1],t[2]-f.at[2]};
                Look(f,a);
            } else Look(f,d);
            if(f.phase==Phase::home) {
                const float r[3]={docks[i][0]-f.at[0],docks[i][1]-f.at[1],docks[i][2]-f.at[2]};
                if(f.u>=1.0f || Len3(r)<kDockAt){f=Funnel{};f.recharge=kRecharge;for(int k=0;k<3;++k)f.at[k]=docks[i][k];++ev.docked;}
                continue;
            }
            if(f.u<1.0f && f.sortie<kSortieMost)continue;
            if(!hasTarget || f.sortie>=kSortieMost) {   // nothing to fight (or out of time): it hangs about, then home
                if(f.sortie<kSortieMost && n>0){f.target=f.order%n;StationOf(f,mech,targets[f.target]);float to[3];
                    for(int k=0;k<3;++k)to[k]=targets[f.target].at[k]+f.offset[k];
                    Course(f,to,kCruise,kBow,(f.order&1) ? 1.0f : -1.0f,kMinTransit);continue;}
                if(f.sortie<kSortieMost){for(float& c:f.vel)c=0.0f;f.phase=Phase::station;continue;}   // guard the mech
                f.phase=Phase::home;Course(f,docks[i],kCruise*kHomeFactor,kBow*0.5f,(f.order&1) ? -1.0f : 1.0f,kMinTransit);
                continue;
            }
            f.phase=Phase::station;
            if(f.shots==0)f.wait=kAimSec;   // arrived: turn on it first
            for(float& c:f.vel)c=0.0f;
            continue;
        }
        if(f.phase==Phase::station) {
            f.onStation+=dt;
            if(hasTarget) {   // held round a moving target, its muzzle on it
                for(int k=0;k<3;++k){const float to=targets[f.target].at[k]+f.offset[k];f.vel[k]=(to-f.at[k])/dt;f.at[k]=to;}
                const float* t=targets[f.target].at;
                const float a[3]={t[0]-f.at[0],t[1]-f.at[1],t[2]-f.at[2]};
                Look(f,a);
            } else if(n>0) {   // its target gone: the next one there
                f.target=f.order%n;StationOf(f,mech,targets[f.target]);float to[3];
                for(int k=0;k<3;++k)to[k]=targets[f.target].at[k]+f.offset[k];
                f.phase=Phase::transit;f.wait=0.0f;Course(f,to,kCruise,kBow,1.0f,kMinTransit);continue;
            } else {   // no enemy at all: it guards the mech, round and over it, facing its way, till the sortie is up
                const float a=kGolden*static_cast<float>(f.order);
                const float to[3]={mech[0]+std::sin(a)*kGuardRadius,mech[1]+kGuardHigh,mech[2]+std::cos(a)*kGuardRadius};
                for(int k=0;k<3;++k){f.vel[k]=(to[k]-f.at[k])/dt;f.at[k]=to[k];}
                Look(f,mechFwd);
                if(f.sortie>=kSortieMost) {
                    f.phase=Phase::home;
                    Course(f,docks[i],kCruise*kHomeFactor,kBow*0.5f,(f.order&1) ? -1.0f : 1.0f,kMinTransit);
                }
                continue;
            }
            const bool done=f.shots>=kShots || f.onStation>=kStationMost || f.sortie>=kSortieMost || !hasTarget;
            if(done && (f.wait-=dt)<=0.0f) {
                f.phase=Phase::home;
                Course(f,docks[i],kCruise*kHomeFactor,kBow*0.5f,(f.order&1) ? -1.0f : 1.0f,kMinTransit);
                continue;
            }
            if(done)continue;
            if((f.wait-=dt)>0.0f)continue;
            // fire, then hop aside for the next one
            Shot& s=ev.shot[ev.shots++];
            s.funnel=i;
            for(int k=0;k<3;++k){s.from[k]=f.at[k];s.at[k]=targets[f.target].at[k];}
            ++f.shots;
            if(f.shots>=kShots){f.wait=kAimSec;continue;}   // the last: a moment, then home
            const float* t=targets[f.target].at;
            float to[3]={f.at[0]-t[0],f.at[1]-t[1],f.at[2]-t[2]};
            const float up[3]={0.0f,1.0f,0.0f};
            float side[3]={to[1]*up[2]-to[2]*up[1],to[2]*up[0]-to[0]*up[2],to[0]*up[1]-to[1]*up[0]};
            const float sl=Len3(side);
            const float hop=kHopLo+(kHopHi-kHopLo)*Hash(i,f.shots+20);
            const float dirSign=Hash(i,f.shots+40)<0.5f ? -1.0f : 1.0f;
            const float rise=(Hash(i,f.shots+60)-0.5f)*hop*0.6f;
            for(int k=0;k<3;++k)f.offset[k]+=(sl>1e-3f ? side[k]/sl : 0.0f)*hop*dirSign+(k==1 ? rise : 0.0f);
            float dest[3];
            for(int k=0;k<3;++k)dest[k]=t[k]+f.offset[k];
            f.phase=Phase::hop;
            for(float& c:f.vel)c=0.0f;
            Course(f,dest,hop/kHopSec,hop*0.3f,dirSign,kHopSec);
            f.dur=kHopSec;
            f.wait=kShotGap-kHopSec;   // after the hop, the rest of the shot's gap
            continue;
        }
    }
    return ev;
}
}  // namespace sazabi::funnels
