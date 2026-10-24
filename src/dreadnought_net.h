// The Dreadnought's turrets over the vehicles' tag 15 transport (seat_net.h; dreadnought.cpp sends and receives): the turrets
// a machine's own player mans, their aim, and the rounds they fired since its last block. Every machine sends only its own
// gunner's turret (`manned`); a receiver turns that turret of its copy and fires a round of no damage from its muzzle at the
// same point (the damage is the gunner's machine's, once: jet_bay.cpp TurretRound). Pure: tests/dreadnought_net_test.cpp.
#pragma once
#include "seat_net.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew::dreadnought_net {
constexpr std::uint32_t kMagic=0x52544E44u;   // 'DNTR'
constexpr std::uint32_t kVersion=1;
constexpr int kTurrets=8;                     // dreadnought_turrets.inc kTurrets
struct State {
    seat_net::Head head{kMagic,kVersion,0,0,-1};
    std::uint8_t manned=0;                    // bit n: turret n is this sender's player's
    std::uint8_t fired=0;                     // bit n: turret n fired at aim[n] since the sender's last block
    std::uint16_t pad=0;
    float yaw[kTurrets]{},pitch[kTurrets]{};  // rad, in the ship's frame (0, 0: ahead, level)
    float aim[kTurrets][3]{};                 // the world point a round flew at
    std::uint32_t tail=0;                     // the head's 8-byte alignment, written 0
};
static_assert(sizeof(State)==24+4+64+96+4,"the wire block");

constexpr float kMostAngle=3.2f;              // |rad| a turret's angle can be
inline bool Valid(const State& s) noexcept {
    if(!seat_net::HeadValid(s.head,kMagic,kVersion) || (s.fired & ~s.manned) || s.pad || s.tail)return false;
    for(int n=0;n<kTurrets;++n) {
        if(!std::isfinite(s.yaw[n]) || !std::isfinite(s.pitch[n]) || std::fabs(s.yaw[n])>kMostAngle || std::fabs(s.pitch[n])>kMostAngle)
            return false;
        for(float a:s.aim[n])if(!std::isfinite(a))return false;
    }
    return true;
}
inline bool Decode(const void* bytes,std::size_t size,State& out) noexcept {
    if(!bytes || size!=sizeof(State))return false;
    std::memcpy(&out,bytes,sizeof(State));
    return Valid(out);
}
}  // namespace crew::dreadnought_net
