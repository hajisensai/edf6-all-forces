// Who sits in a vehicle seat (docs/re-notes.md "seat rider", autoturret/docs/re-notes.md "player rider").
#include "edf/seat.h"
#include "edf/memory.h"
#include "edf/step_objects.h"
#include <cmath>

namespace edf {
// Online, another machine's player is copied here with its player flag and may carry a pad object too: it is that
// machine's player, never this one's (its keys, its camera, its riding are run there).
// A scene object of this frame (stepobj) is read without asking Readable; its members under __try (it is allocated, not
// necessarily a human as long as kHumanPlayer).
bool IsPlayer(const unsigned char* human) noexcept {
    if(!stepobj::Reads(human,kHumanPlayer+1))return false;
    __try {
        return human[kHumanPlayer] && At<const void*>(human,kHumanPad) && (human[kRiderNet+kNetFlags]&1)==0;   // !RemoteRider
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool IsAnyPlayer(const unsigned char* human) noexcept {
    if(!stepobj::Reads(human,kHumanPlayer+1))return false;
    __try {
        return human[kHumanPlayer] && (At<const void*>(human,kHumanPad) || (human[kRiderNet+kNetFlags]&1)!=0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool IsAnyPlayerSelf(const unsigned char* self) noexcept {
    return self[kHumanPlayer] && (At<const void*>(self,kHumanPad) || (self[kRiderNet+kNetFlags]&1)!=0);   // RemoteRider's bit
}

bool RemoteRider(const unsigned char* rider) noexcept {
    if(!stepobj::Reads(rider,kRiderNet+kNetFlags+1))return false;
    __try {
        return (rider[kRiderNet+kNetFlags]&1)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The seat's weak reference: its control block the one the System holds the rider by (stepobj) is readable and the rider
// allocated without asking Readable; anything else is asked as before.
Rider SeatRider(const unsigned char* image,const unsigned char* seat) noexcept {
    const auto ctrl=At<const unsigned char*>(seat,kSeatRiderCtrl);
    if(!ctrl)return Rider::none;
    const auto rider=At<const unsigned char*>(seat,kSeatRider);
    const bool trusted=rider && stepobj::ControlOf(rider)==ctrl;
    if(!trusted && !Readable(ctrl,0x10))return Rider::none;
    __try {
        if(At<std::int32_t>(ctrl,kCtrlUses)==0)return Rider::none;
        if(!trusted && !Readable(rider,kHumanPlayer+1))return Rider::other;
        if(At<const unsigned char*>(rider,0)==image+kDummyRiderVtable)return Rider::dummy;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return Rider::other; }
    return IsPlayer(rider) ? Rider::player : Rider::other;
}

bool LivingSoldierInSeat(const unsigned char* image,const unsigned char* seat) noexcept {
    const Rider who=SeatRider(image,seat);
    if(who==Rider::none || who==Rider::dummy)return false;
    const auto rider=At<const unsigned char*>(seat,kSeatRider);
    if(!stepobj::Reads(rider,kHumanPlayer+1))return false;
    __try {
        if(rider[kDead])return false;
        const auto vt=At<const unsigned char*>(rider,0);
        constexpr unsigned soldiers[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};
        bool soldier=false;
        for(const auto rva:soldiers)if(vt==image+rva)soldier=true;
        const float hp=At<float>(rider,0x2F8);
        return soldier && std::isfinite(hp) && hp>0.0f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool AnyPlayerIn(const unsigned char* image,const unsigned char* seat) noexcept {
    const Rider r=SeatRider(image,seat);
    return r==Rider::player || (r==Rider::other && IsAnyPlayer(At<const unsigned char*>(seat,kSeatRider)));
}

unsigned SeatCount(const unsigned char* vehicle) noexcept {
    const auto n=At<std::uint64_t>(vehicle,kSeatCount);
    const auto seats=At<const unsigned char*>(vehicle,kSeats);
    // A vehicle of this frame's scene (stepobj) keeps its seat array (made with it, never regrown) until it is released.
    return n>0 && n<=kMaxSeats && seats && (stepobj::Trusted(vehicle) || Readable(seats,n*kSeatStride)) ? static_cast<unsigned>(n) : 0;
}
}  // namespace edf
