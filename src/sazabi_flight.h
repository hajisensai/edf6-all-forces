// The Sazabi's flight control (sazabi.cpp runs it each frame; tools/sazabi_flight_check.cpp checks it offline): a mobile
// suit as the Gundam action games fly one (the user, 2026-10-07: 「飞控也应该给高达写一个专门的」, picked 高达动作游戏式):
//  - on its feet the stick walks (to kWalkShare of its travel) and runs (past it), relative to the mech's heading,
//    which is the camera's (it always faces the aim);
//  - the ascend trigger jumps (pressed on its feet) and climbs while held; let go in the air it falls (the descend key
//    twice as fast);
//  - the boost (dash key / pad A): pressed, a burst at Params::boost along the stick (ahead with none); held, the burst
//    goes on as a boost dash, on its feet skating along the ground, in the air holding its height; in the air it can be
//    pressed again to turn the dash (a step);
//  - the stick in the air pushes it about with inertia (Params::fly at most);
//  - the thrusters' gauge: a boost or a climb burns it (a full gauge = Params::thrusterSec of either), a jump and each
//    burst take a bite; spent in the air it overheats: no boost, no climb, it falls, and only after it lands and
//    kCoolSec have passed does the gauge come back (Params::regen a second on its feet, kLandRegen times that
//    for kLandRegenSec after a landing, so it is ready again soon after touching down, as in those games).
// Velocities are world m/s; `heading` is the mech's (forward = (sin h, 0, cos h)); `feet` the soles' height over what
// is under them (kNoGround: nothing).
#pragma once
#include <cmath>

namespace sazabi::flight {

constexpr float kNoGround=-1.0e9f;
constexpr float kFloat=0.15f,kTrack=8.0f,kMostRise=25.0f,kMostDrop=40.0f,kOffGround=3.0f;
constexpr float kGroundAccel=30.0f,kAirAccel=22.0f;   // m/s^2 toward the stick's speed
constexpr float kBoostAccel=140.0f;                   // m/s^2 toward the boost's speed (after its burst)
constexpr float kBurst=0.85f;                         // of Params::boost, the burst's speed at once
constexpr float kWalkShare=0.6f;
constexpr float kJump=0.6f;                           // of Params::climb, up the moment it leaves the ground
constexpr float kClimbTake=4.0f;                      // 1/s: how fast the climb comes on
constexpr float kHoldTake=6.0f;                       // 1/s: a boost in the air takes its climb or fall to 0 this fast
constexpr float kLiftOff=0.3f;                        // the ascend trigger past this
constexpr float kJumpCost=0.06f,kBurstCost=0.06f,kLeast=0.04f;   // a jump's and a burst's bite, the gauge they need
constexpr float kCoolSec=0.6f,kLandRegen=3.0f,kLandRegenSec=1.0f;
constexpr float kMostFall=60.0f;

struct Params {
    float walk=12.0f,run=26.0f,fly=48.0f,boost=60.0f,climb=24.0f,gravity=20.0f,thrusterSec=8.0f,regen=0.4f;
};
struct Input {
    float forward=0.0f,right=0.0f;   // the stick, -1..1, relative to the heading
    float ascend=0.0f;               // 0..1
    bool boost=false,descend=false;
};
struct State {
    float vel[3]{};
    bool air=false;
    float gauge=1.0f;                // 0..1
    bool overheat=false;
    bool boosting=false;             // a boost (dash) burning now
    bool climbing=false;             // the climb burning now
    bool boostHeld=false,ascendHeld=false;
    float boostDir[3]{0.0f,0.0f,1.0f};
    float cool=0.0f;                 // s on its feet since it landed (the overheat's cooling, the quick regen)
};
// What happened this step (sounds, the pose).
struct Events { bool jumped,burst,landed,overheated; float landSpeed; };

inline float Clampf(float x,float lo,float hi) { return x<lo ? lo : x>hi ? hi : x; }

// The stick as a world direction (unit, y 0) and its travel (0..1).
inline float StickDir(const Input& in,float heading,float* dir) {
    const float s=std::sin(heading),c=std::cos(heading);
    float mag=std::sqrt(in.forward*in.forward+in.right*in.right);
    dir[0]=0.0f;dir[1]=0.0f;dir[2]=0.0f;
    if(mag<1e-3f)return 0.0f;
    // forward (s, 0, c); right (-c, 0, s)
    dir[0]=(s*in.forward-c*in.right)/mag;dir[2]=(c*in.forward+s*in.right)/mag;
    return mag>1.0f ? 1.0f : mag;
}

inline void Burn(State& st,float amount) {
    st.gauge-=amount;
    if(st.gauge<=0.0f){st.gauge=0.0f;}
}

// The horizontal velocity toward `want` (m/s) at `accel` m/s^2.
inline void Toward(State& st,const float* want,float accel,float dt) {
    for(int i=0;i<3;i+=2) {
        const float d=want[i]-st.vel[i];
        const float most=accel*dt;
        st.vel[i]+=Clampf(d,-most,most);
    }
}

// One step of `dt` s.
inline Events Step(State& st,const Input& in,float heading,float feet,float dt,const Params& p) {
    Events ev{};
    float dir[3];
    const float mag=StickDir(in,heading,dir);
    const float fwd[3]={std::sin(heading),0.0f,std::cos(heading)};
    const bool boostPress=in.boost && !st.boostHeld,ascendPress=in.ascend>kLiftOff && !st.ascendHeld;
    st.boostHeld=in.boost;st.ascendHeld=in.ascend>kLiftOff;
    const bool canBurn=!st.overheat && st.gauge>kLeast;
    // a burst: pressed with gauge enough, along the stick (ahead with none)
    if(boostPress && canBurn) {
        for(int i=0;i<3;++i)st.boostDir[i]=mag>0.1f ? dir[i] : fwd[i];
        const float b=p.boost*kBurst;
        const float along=st.vel[0]*st.boostDir[0]+st.vel[2]*st.boostDir[2];
        if(along<b){st.vel[0]=st.boostDir[0]*b;st.vel[2]=st.boostDir[2]*b;}
        Burn(st,kBurstCost);
        ev.burst=true;
    }
    st.boosting=in.boost && !st.overheat && st.gauge>0.0f;
    if(st.boosting && mag>0.1f)for(int i=0;i<3;++i)st.boostDir[i]=dir[i];   // a held boost follows the stick
    if(!st.air) {
        if(ascendPress && canBurn) {   // a jump
            st.air=true;
            st.vel[1]=p.climb*kJump;
            Burn(st,kJumpCost);
            ev.jumped=true;
        } else if(feet==kNoGround || feet>kOffGround) {
            st.air=true;   // walked off a ledge
        } else {
            st.vel[1]=Clampf((kFloat-feet)*kTrack,-kMostDrop,kMostRise);
            st.cool+=dt;
            if(st.cool>=kCoolSec)st.overheat=false;
            if(!st.boosting && st.cool>=kCoolSec)
                st.gauge=std::fmin(1.0f,st.gauge+p.regen*(st.cool<kCoolSec+kLandRegenSec ? kLandRegen : 1.0f)*dt);
        }
    }
    if(st.air) {
        st.cool=0.0f;
        st.climbing=in.ascend>0.0f && !st.overheat && st.gauge>0.0f;
        if(st.climbing) {
            const float want=p.climb*in.ascend;
            st.vel[1]+=(want-st.vel[1])*std::fmin(1.0f,kClimbTake*dt);
            Burn(st,in.ascend*dt/p.thrusterSec);
        } else if(st.boosting) {   // a boost in the air holds its height
            st.vel[1]+=(0.0f-st.vel[1])*std::fmin(1.0f,kHoldTake*dt);
        } else {
            st.vel[1]-=p.gravity*dt*(in.descend ? 2.0f : 1.0f);
        }
        st.vel[1]=std::fmax(st.vel[1],-kMostFall);
    } else st.climbing=false;
    // the horizontal: a boost along its direction, else the stick's walk / run / flight
    if(st.boosting) {
        const float want[3]={st.boostDir[0]*p.boost,0.0f,st.boostDir[2]*p.boost};
        Toward(st,want,kBoostAccel,dt);
        Burn(st,dt/p.thrusterSec);
    } else {
        const float top=st.air ? p.fly : mag<=kWalkShare ? p.walk*mag/kWalkShare :
                        p.walk+(mag-kWalkShare)/(1.0f-kWalkShare)*(p.run-p.walk);
        const float want[3]={dir[0]*top,0.0f,dir[2]*top};
        Toward(st,want,st.air ? kAirAccel : kGroundAccel,dt);
    }
    // spent in the air: overheated until it lands and cools
    if(st.air && st.gauge<=0.0f && !st.overheat){st.overheat=true;ev.overheated=true;}
    if(st.overheat){st.boosting=false;st.climbing=false;}
    // down: the knees take it
    if(st.air && feet!=kNoGround && feet<=kFloat+0.2f && st.vel[1]<=0.0f) {
        ev.landed=true;ev.landSpeed=-st.vel[1];
        st.air=false;st.vel[1]=0.0f;st.cool=0.0f;
    }
    return ev;
}
}  // namespace sazabi::flight
