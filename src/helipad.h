// Where a helicopter can take off this mission (2026-10-10, the user: 「能从机场起飞就从机场起飞吧」 / 「可以吧」): pure, so
// tests/rescue_logic_test.cpp runs it. The sea rescue's takeoff candidates are the carrier's deck and these.
//
// What the game's data has (checked 2026-10-10 against the player's own Root.cpk / Chunk01 / Chunk02): none of the 50
// shipped map archives (.MAC) places an airfield, runway, apron, hangar or helipad piece (no def name of the 1201 says
// so: the only "landdeck" pieces are NW_RINKAI's wharf decks), and of the EDF6 missions only M017 / M031D / M031E create
// helicopters, all of them event helicopters flying routes (CreateNeutral ev_v506 / ev_v602 + SetAiRoute). So there is
// no airfield to read; what there is, is ground a helicopter really stood on: a vehicle request's Brute or Eros set
// down by the Air Raider, a heli a player or the plugin's pilot landed, a mission's heli parked. A helicopter that rests
// on solid ground (within kGrounded of it, not over water) for kStillMs without moving more than kStill is a pad; pads
// kSame apart, at most kMost a mission. A pad a helicopter is still standing on is occupied (Occupied): nothing is made
// on top of it.
#pragma once
#include <cmath>

namespace crew::helipad {
constexpr int kMost=16,kWatch=32;
constexpr float kSame=25.0f,kStill=0.5f,kGrounded=4.0f,kOccupied=15.0f;
constexpr unsigned long long kStillMs=3000,kSampleMs=500;
struct Watch { const void* v=nullptr; float at[3]{}; unsigned long long since=0,last=0; };
struct Pads { float at[kMost][3]{}; int count=0; Watch watch[kWatch]{}; };

inline float Dist(const float* a,const float* b) noexcept {
    const float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return std::sqrt(x*x+y*y+z*z);
}
// Whether helicopter `v` is due a sample (one every kSampleMs: a ground ray each).
inline bool Due(const Pads& p,const void* v,unsigned long long ms) noexcept {
    for(const auto& w:p.watch)if(w.v==v)return ms-w.last>=kSampleMs;
    return true;
}
// A sample of `v` at `pos`; `grounded`: resting on solid ground there. True when it made a new pad.
inline bool Observe(Pads& p,const void* v,const float* pos,bool grounded,unsigned long long ms) noexcept {
    if(!v || !std::isfinite(pos[0]+pos[1]+pos[2]))return false;
    Watch* w=nullptr;Watch* oldest=&p.watch[0];
    for(auto& row:p.watch) {
        if(row.v==v){w=&row;break;}
        if(row.last<oldest->last)oldest=&row;
    }
    if(!w){w=oldest;*w=Watch{};w->v=v;for(int i=0;i<3;++i)w->at[i]=pos[i];}
    w->last=ms;
    if(!grounded || Dist(w->at,pos)>kStill) {
        for(int i=0;i<3;++i)w->at[i]=pos[i];
        w->since=grounded ? ms : 0;
        return false;
    }
    if(!w->since){w->since=ms;return false;}
    if(ms-w->since<kStillMs || p.count>=kMost)return false;
    for(int i=0;i<p.count;++i)if(Dist(p.at[i],w->at)<kSame)return false;
    for(int i=0;i<3;++i)p.at[p.count][i]=w->at[i];
    ++p.count;return true;
}
// Whether a helicopter sampled just now stands within kOccupied of pad `i`.
inline bool Occupied(const Pads& p,int i,unsigned long long ms) noexcept {
    for(const auto& w:p.watch)if(w.v && ms-w.last<=3*kSampleMs && Dist(w.at,p.at[i])<kOccupied)return true;
    return false;
}
}  // namespace crew::helipad
