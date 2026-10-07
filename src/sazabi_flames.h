// How strongly each of the Sazabi's nozzles burns this frame (sazabi_arms.inc Flames feeds it to the Booster flames;
// tools/sazabi_flight_check.cpp checks it). Pure. After the mobile suits of the games and the film (the research,
// 2026-10-07: idle about a quarter of a boost's flame, a burst at the start of a dash that holds about 0.12 s and falls
// back over 0.3 s, a soft flicker of about +-12% with a slower swell of about 7 Hz, attitude jets fired against a turn):
//  - on its feet, not boosting: out;
//  - in the air on no thrust: kHover (the thrusters holding it about);
//  - boosting or climbing: kCruise; for kBurstHold s after a burst (a dash or a jump begun) full, back to kCruise over
//    kBurstFall s;
//  - the burst nozzles (sazabi_pose.h Nozzle::burst: calves, binders, waist) only while boosting or climbing, except the
//    waist's, which also fire as attitude jets, the side away from a turn faster than kTurnJet, at kAttitude;
//  - every nozzle flickers by its own phase (no two in step).
#pragma once
#include <cmath>

namespace sazabi::flames {

constexpr float kHover=0.25f,kCruise=0.62f,kBurstHold=0.12f,kBurstFall=0.3f;
constexpr float kFlicker=0.12f,kSwell=0.04f,kSwellHz=7.0f,kFlickerHz=30.0f;
constexpr float kTurnJet=0.6f,kAttitude=0.5f;   // rad/s, level

struct State {
    bool air=false,boosting=false;   // in the air; boosting or climbing
    float sinceBurst=1e3f;           // s since the last burst began
    float yawRate=0.0f;              // rad/s, + turning left
    float t=0.0f;                    // s, a running clock (the flicker)
};

// A smooth noise -1..1 at `x` (cosine between deterministic values at the integers).
inline float Noise(float x,int seed) {
    const float i=std::floor(x),f=x-i;
    auto h=[seed](float k) {
        unsigned u=static_cast<unsigned>(static_cast<int>(k))*2654435761u^static_cast<unsigned>(seed+1)*2246822519u;
        u^=u>>13;u*=3266489917u;u^=u>>16;
        return static_cast<float>(u&0xFFFF)/32767.5f-1.0f;
    };
    const float w=(1.0f-std::cos(f*3.14159265f))*0.5f;
    return h(i)*(1.0f-w)+h(i+1.0f)*w;
}

// The base level (before the flicker) of the main thrusters.
inline float Base(const State& s) {
    float base=s.boosting ? kCruise : s.air ? kHover : 0.0f;
    if(s.sinceBurst<kBurstHold)base=1.0f;
    else if(s.sinceBurst<kBurstHold+kBurstFall) {
        const float k=(s.sinceBurst-kBurstHold)/kBurstFall;
        base=std::fmax(base,1.0f+(kCruise-1.0f)*k);
    }
    return base;
}

// The level of nozzle `i` (`burst`: one of the burst nozzles; `waistSide`: +1 the left waist's, -1 the right's, 0 none).
inline float Level(const State& s,int i,bool burst,int waistSide) {
    float base=Base(s);
    if(burst && !s.boosting && s.sinceBurst>=kBurstHold+kBurstFall)base=0.0f;
    // the attitude jets: turning left (+), the right waist's fire (it pushes the tail round), and the other way
    if(waistSide!=0 && std::fabs(s.yawRate)>kTurnJet && (s.yawRate>0.0f ? waistSide<0 : waistSide>0))base=std::fmax(base,kAttitude);
    if(base<=0.0f)return 0.0f;
    const float f=1.0f+kFlicker*Noise(s.t*kFlickerHz,i*7+3)+kSwell*std::sin(6.2831853f*kSwellHz*s.t+static_cast<float>(i)*1.7f);
    const float v=base*f;
    return v<0.0f ? 0.0f : v>1.0f ? 1.0f : v;
}
}  // namespace sazabi::flames
