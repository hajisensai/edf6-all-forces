// The Andryusha's fuse online (docs/andryusha-re.md §3.1; room protocol 10): the fuse is the weapon's own copy of the
// mark (andryusha.h kMarkImpactBits / kMarkAirburstBits at weapon +0x8C4), and every machine runs its own copy of the
// vehicle and of its weapon, so a switch is sent to the vehicle's copies over the vehicles' tag 15 transport (seat_net.h)
// as an 'AFUZ' block: the weapon's holder in the vehicle's holder array (the same on every copy) and the fuse. A
// receiver writes that fuse into its copy's weapon, so every machine's carriers burst and split alike.
//  - Who sends: the machine whose player sits on the launcher's seat (seat 0); with no player there, the machine that
//    runs the vehicle (IsOnlineAuthority), carrying the fuse it holds (the last switch it received). One sender at a
//    time, the control epoch the seat's rider (seat_net::SeatController: -1 none), as the seats' picks (seat_net.h Pick).
//  - When: at once on a switch, else every kRefreshMs: a member who joined late, a lost message, a copy re-made all get
//    the fuse within a refresh. A vehicle re-made (destroyed, called again) starts from its SGO's impact fuse on every
//    machine alike.
// Pure: tools/andryusha_check.cpp.
#pragma once
#include "andryusha.h"
#include "seat_net.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::andryusha_net {
constexpr std::uint32_t kMagic=0x5A554641u;   // 'AFUZ'
constexpr std::uint32_t kVersion=1;
constexpr unsigned long long kRefreshMs=1000;
struct State {
    seat_net::Head head{kMagic,kVersion,0,0,-1};
    std::int32_t holder=-1;                   // the launcher's holder in the vehicle's holder array
    std::uint8_t fuse=0;                      // andryusha::Fuse: 1 impact, 2 airburst
    std::uint8_t pad[3]{};                    // written 0
};
static_assert(sizeof(State)==32 && offsetof(State,holder)==24 && offsetof(State,fuse)==28,"the wire block");

constexpr bool Valid(const State& s) noexcept {
    return seat_net::HeadValid(s.head,kMagic,kVersion) && s.holder>=0 &&
        s.holder<static_cast<std::int32_t>(seat_net::kMostHolders) &&
        (s.fuse==static_cast<std::uint8_t>(andryusha::Fuse::impact) || s.fuse==static_cast<std::uint8_t>(andryusha::Fuse::airburst)) &&
        !s.pad[0] && !s.pad[1] && !s.pad[2];
}
inline bool Decode(const void* bytes,std::size_t size,State& out) noexcept {
    if(!bytes || size!=sizeof(State))return false;
    State s;
    std::memcpy(&s,bytes,sizeof(State));
    if(!Valid(s))return false;
    out=s;
    return true;
}
// Whether this machine speaks for the vehicle's fuse: its player rides the launcher's seat, or no player does and it
// runs the vehicle. Every other machine admits the sender's blocks (and never its own: one speaker at a time).
constexpr bool Speaks(bool localPlayerRides,bool playerRides,bool authority) noexcept {
    return localPlayerRides || (!playerRides && authority);
}
// When the speaker sends: a fuse it has not sent yet (a switch, a new speaker), or the refresh due.
constexpr bool Due(std::uint8_t fuse,std::uint8_t sent,unsigned long long now,unsigned long long sentAt) noexcept {
    return fuse!=sent || sentAt==0 || now-sentAt>=kRefreshMs;
}
}  // namespace crew::andryusha_net
