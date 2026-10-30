// The vehicles' own turrets (seat_turret.inc, StoreSpec::turret) over the vehicles' tag 15 transport (seat_net.h): the
// turrets a machine trains (its player's gunner seat, its NPC gunner, the unmanned auto turrets of a vehicle it runs) and
// where they point. A receiver turns that turret of its copy (fresh within kRemoteMs, else back to its rest). The rounds
// are the gun's own: the trainer's machine pulls the trigger of the holder on that seat, as every seat's own fire.
// Pure: tests/seat_turret_test.cpp.
#pragma once
#include "seat_net.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew::seat_turret_net {
constexpr std::uint32_t kMagic=0x52555453u;   // 'STUR'
constexpr std::uint32_t kVersion=1;
constexpr int kMostTurrets=8;                 // seat_turret.inc kMostTurrets
constexpr unsigned long long kSendMs=50,kRemoteMs=600;
struct State {
    seat_net::Head head{kMagic,kVersion,0,0,-1};
    std::uint8_t trained=0;                   // bit n: entry n is a turret this sender trains
    std::uint8_t pad[3]{};
    std::int16_t holder[kMostTurrets]{};      // its weapon's holder in the vehicle's holder array (the same on every copy)
    float yaw[kMostTurrets]{},pitch[kMostTurrets]{};   // rad, from its bind pose (0, 0: ahead, level)
    std::uint32_t tail=0;                     // the head's 8-byte alignment, written 0
};
static_assert(sizeof(State)==24+4+16+64+4,"the wire block");

constexpr float kMostAngle=3.2f;              // |rad| a turret's angle can be
inline bool Valid(const State& s) noexcept {
    if(!seat_net::HeadValid(s.head,kMagic,kVersion) || !s.trained || s.pad[0] || s.pad[1] || s.pad[2] || s.tail)return false;
    for(int n=0;n<kMostTurrets;++n) {
        if(!(s.trained&(1u<<n))) {
            if(s.holder[n] || s.yaw[n]!=0.0f || s.pitch[n]!=0.0f)return false;   // an unused entry is all zero
            continue;
        }
        if(s.holder[n]<0 || s.holder[n]>=static_cast<std::int16_t>(seat_net::kMostHolders))return false;
        if(!std::isfinite(s.yaw[n]) || !std::isfinite(s.pitch[n]) || std::fabs(s.yaw[n])>kMostAngle ||
           std::fabs(s.pitch[n])>kMostAngle)return false;
    }
    return true;
}
inline bool Decode(const void* bytes,std::size_t size,State& out) noexcept {
    if(!bytes || size!=sizeof(State))return false;
    State s;
    std::memcpy(&s,bytes,sizeof(State));
    if(!Valid(s))return false;
    out=s;
    return true;
}
}  // namespace crew::seat_turret_net
