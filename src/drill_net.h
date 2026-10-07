// The drill's state travels through its own NetworkObject, so the game's descriptor routes it to the same tank.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew {
namespace drill_net {
constexpr std::int8_t kTag=15; // coop reserves 13 (hits) and 14 (RNG)
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
};
static_assert(sizeof(State)==96,"fixed little-endian x64 wire layout");
inline bool Valid(const State& s) noexcept {
    if(s.magic!=kMagic || s.version!=kVersion || !s.sender || !s.sequence || s.controller< -1 ||
       s.phase>Phase::back || s.overheated>1)return false;
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
// Kept inside the tank's ObjRef-bound Drill, never keyed by a raw address alone. Each sender's watermark survives
// a driver change; a delayed packet from the previous driver cannot undo the new driver's launch or catch.
struct Gate {
    struct Seen { std::uint64_t sender=0;std::uint32_t sequence=0; };
    static constexpr std::size_t kMaxSenders=1024; // the coop room's supported peer capacity
    Seen seen[kMaxSenders]{};
    bool Admit(const State& s,bool session,bool authority,std::int32_t controller) noexcept {
        if(!session || authority || s.controller!=controller || !Valid(s))return false;
        Seen* empty=nullptr;
        for(auto& x:seen) {
            if(!x.sender && !empty)empty=&x;
            if(x.sender!=s.sender)continue;
            // Serial-number arithmetic also accepts the sender's uint32 wrap, while rejecting older messages.
            const std::uint32_t delta=s.sequence-x.sequence;
            if(!delta || delta>=0x80000000u)return false;
            x.sequence=s.sequence;return true;
        }
        if(!empty)return false; // bounded, do not evict a watermark and re-admit an old event
        *empty=Seen{s.sender,s.sequence};return true;
    }
};
constexpr bool Replicated(bool session,std::uint16_t flags) noexcept { return session && (flags&3)!=0; }
} // namespace drill_net
bool InstallDrillNet() noexcept;
bool DrillNetSend(unsigned char* vehicle,drill_net::State state) noexcept;
std::int32_t DrillNetController(unsigned char* vehicle) noexcept;
void DrillNetReceived(unsigned char* vehicle,const drill_net::State& state) noexcept;
} // namespace crew
