// Who sits in a vehicle seat (docs/re-notes.md "seat rider", autoturret/docs/re-notes.md "player rider").
#include "edf/seat.h"
#include "edf/memory.h"

namespace edf {
// Online, another machine's player is copied here with its player flag and may carry a pad object too: it is that
// machine's player, never this one's (its keys, its camera, its riding are run there).
bool IsPlayer(const unsigned char* human) noexcept {
    return Readable(human,kHumanPlayer+1) && human[kHumanPlayer] && At<const void*>(human,kHumanPad) && !RemoteRider(human);
}

bool IsAnyPlayer(const unsigned char* human) noexcept {
    return Readable(human,kHumanPlayer+1) && human[kHumanPlayer] && (At<const void*>(human,kHumanPad) || RemoteRider(human));
}

bool RemoteRider(const unsigned char* rider) noexcept {
    return Readable(rider,kRiderNet+kNetFlags+1) && (rider[kRiderNet+kNetFlags]&1)!=0;
}

Rider SeatRider(const unsigned char* image,const unsigned char* seat) noexcept {
    const auto ctrl=At<const unsigned char*>(seat,kSeatRiderCtrl);
    if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,kCtrlUses)==0)return Rider::none;
    const auto rider=At<const unsigned char*>(seat,kSeatRider);
    if(!Readable(rider,kHumanPlayer+1))return Rider::other;
    if(At<const unsigned char*>(rider,0)==image+kDummyRiderVtable)return Rider::dummy;
    return IsPlayer(rider) ? Rider::player : Rider::other;
}

unsigned SeatCount(const unsigned char* vehicle) noexcept {
    const auto n=At<std::uint64_t>(vehicle,kSeatCount);
    const auto seats=At<const unsigned char*>(vehicle,kSeats);
    return n>0 && n<=kMaxSeats && Readable(seats,n*kSeatStride) ? static_cast<unsigned>(n) : 0;
}
}  // namespace edf
