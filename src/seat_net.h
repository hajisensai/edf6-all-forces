// A seat's weapon pick (payload.cpp), one kind of the plugin's object messages (object_net.h: the head, the gates, the
// tag 15 layer that carries it): which of the seat's weapons its fire lands on (a stock vehicle's switchable stores, its
// native ammunition alternatives). The machine whose choice it is sends it (the seat's player on theirs; for an NPC
// gunner, the machine that runs it: IsOnlineAuthority); every other machine lands that seat's pulls on the same weapon
// (PullHook). `holder` is the weapon's holder in the vehicle's holder array (veh +0x638, NativeHolderIndex), -1 none
// picked; `seat` the seat; `flags` kNpc when an NPC gunner chose it. Pure: tests/seat_net_test.cpp.
#pragma once
#include "object_net.h"

namespace crew::seat_net {
constexpr std::uint32_t kPickMagic=0x4B435053,kPickVersion=1;   // 'SPCK'
constexpr std::uint32_t kNpc=1,kPickFlags=1;
constexpr std::uint32_t kMostSeats=16,kMostHolders=64;
struct Pick {
    object_net::Head head{kPickMagic,kPickVersion,0,0,-1};
    std::uint32_t seat=0;
    std::int32_t holder=-1;
    std::uint32_t flags=0;
    std::uint32_t reserved=0;   // explicit: no uninitialized padding goes on the wire
};
static_assert(sizeof(Pick)==40 && offsetof(Pick,reserved)==36,"pointer-free, explicit wire layout");
constexpr bool Valid(const Pick& p) noexcept {
    return object_net::HeadValid(p.head,kPickMagic,kPickVersion) && p.seat<kMostSeats && p.holder>=-1 &&
        p.holder<static_cast<std::int32_t>(kMostHolders) && !(p.flags&~kPickFlags) && p.reserved==0;
}
inline bool Decode(const void* bytes,std::size_t size,Pick& into) noexcept {
    if(!bytes || size!=sizeof(Pick))return false;
    Pick p;std::memcpy(&p,bytes,sizeof(p));
    if(!Valid(p))return false;
    into=p;return true;
}
// When a machine sends its seat's pick: at once when it changes, else every kPickRefreshMs (a lost message, a copy that
// joined late, a receiver whose record expired). A receiver forgets a pick not refreshed for kPickStaleMs.
constexpr unsigned long long kPickRefreshMs=500,kPickStaleMs=2000;
constexpr bool PickDue(bool changed,unsigned long long now,unsigned long long sentAt) noexcept {
    return changed || sentAt==0 || now-sentAt>=kPickRefreshMs;
}
}  // namespace crew::seat_net
