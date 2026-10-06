// Who sits in a vehicle seat: the one test both plugins make (common/seat.cpp).
#pragma once
#include "edf/layout.h"

namespace edf {
// none: the seat's rider weak_ptr is expired or empty. dummy: the DummyVehicleRider an NPC-crewed vehicle
// is driven through. player: a human driven by a pad on this machine (IsPlayer). other: anything else (an NPC
// soldier, another machine's player, a rider that cannot be read).
enum class Rider { none, dummy, player, other };
Rider SeatRider(const unsigned char* image,const unsigned char* seat) noexcept;
/// A human driven by a pad on this machine (pad set and the player-controlled flag on, and not another machine's
// player copied here: RemoteRider).
bool IsPlayer(const unsigned char* human) noexcept;
// A player of any machine: this machine's (IsPlayer), or another's copied here with its player flag on. What a test
// that tells NPC soldiers from players asks (an NPC's squad, a soldier to seat), not who this machine's keys drive.
bool IsAnyPlayer(const unsigned char* human) noexcept;
// Whether another machine runs this rider (online): its network object's flag bit 0.
bool RemoteRider(const unsigned char* rider) noexcept;
// The vehicle's seat count (0 when the seat array cannot be read, or holds more than kMaxSeats) and its
// seat `index`.
constexpr unsigned kMaxSeats=16;
unsigned SeatCount(const unsigned char* vehicle) noexcept;
inline unsigned char* SeatAt(unsigned char* vehicle,unsigned index) noexcept {
    return At<unsigned char*>(vehicle,kSeats)+index*kSeatStride;
}
}  // namespace edf
