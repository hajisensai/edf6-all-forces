// The drill's state travels through its own NetworkObject, so the game's descriptor routes it to the same tank.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "seat_net.h"

namespace crew {
namespace drill_net {
constexpr std::int8_t kTag=seat_net::kTag;   // the vehicles' one tag (seat_net.h): this kind is its kMagic
constexpr std::uint32_t kMagic=0x4C495244,kVersion=1;
enum class Phase : std::uint32_t { home,out,back };
struct State {
    std::uint32_t magic=kMagic,version=kVersion;
    std::uint64_t sender=0;
    std::uint32_t sequence=0;
    std::int32_t controller=-1;
    Phase phase=Phase::home;
    std::uint32_t overheated=0;
    float pos[3]{},dir[3]{},axis[3]{},speed=0,rpm=0,heat=0,angle=0;
    float flown=0;
    std::uint32_t backAgeMs=0;
    std::uint32_t reserved=0; // explicit last four bytes: no uninitialized struct padding goes on the wire
};
static_assert(sizeof(State)==96,"fixed little-endian x64 wire layout");
static_assert(offsetof(State,reserved)==92,"explicit zero reserved bytes occupy the tail");
inline bool Valid(const State& s) noexcept {
    if(s.magic!=kMagic || s.version!=kVersion || !s.sender || !s.sequence || s.controller< -1 ||
       s.phase>Phase::back || s.overheated>1 || s.reserved)return false;
    for(int i=0;i<3;++i)if(!std::isfinite(s.pos[i]) || std::fabs(s.pos[i])>1.0e7f ||
        !std::isfinite(s.dir[i]) || !std::isfinite(s.axis[i]))return false;
    if(s.phase!=Phase::home) {
        const float a=s.axis[0]*s.axis[0]+s.axis[1]*s.axis[1]+s.axis[2]*s.axis[2];
        const float d=s.dir[0]*s.dir[0]+s.dir[1]*s.dir[1]+s.dir[2]*s.dir[2];
        if(!(a>0.9f && a<1.1f && d>0.9f && d<1.1f))return false;
    }
    return std::isfinite(s.speed) && s.speed>=0 && s.speed<=1.0e6f && std::isfinite(s.rpm) &&
        s.rpm>=0 && s.rpm<=1.0e6f && std::isfinite(s.heat) && s.heat>=0 && s.heat<=1 &&
        std::isfinite(s.angle) && std::fabs(s.angle)<=7 && std::isfinite(s.flown) && s.flown>=0 &&
        s.flown<=1.0e7f && s.backAgeMs<=12050;
}
inline bool Decode(const void* data,std::size_t size,State& out) noexcept {
    if(!data || size!=sizeof(State))return false;
    State s;std::memcpy(&s,data,sizeof(s));
    if(!Valid(s))return false;
    out=s;return true;
}
// Kept inside the tank's ObjRef-bound Drill, never keyed by a raw address alone (seat_net.h Gate: each sender's
// watermark survives a driver change; a delayed packet from the previous driver cannot undo the new driver's launch or
// catch).
struct Gate : seat_net::Gate {
    bool Admit(const State& s,bool session,bool authority,std::int32_t controller) noexcept {
        if(!Valid(s))return false;
        seat_net::Head h;std::memcpy(&h,&s,sizeof(h));
        return seat_net::Gate::Admit(h,session,authority,controller);
    }
};
using seat_net::Replicated;
} // namespace drill_net
bool InstallDrillNet() noexcept;
bool DrillNetSend(unsigned char* vehicle,drill_net::State state) noexcept;
std::int32_t DrillNetController(unsigned char* vehicle) noexcept;
void DrillNetReceived(unsigned char* vehicle,const drill_net::State& state) noexcept;
} // namespace crew
