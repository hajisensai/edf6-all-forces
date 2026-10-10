// A stock vehicle's seat events (EDF.dll, read 2026-10-09 for the user's "上车声是原版的上载具声音": a support whose crew
// is created aboard played the stock boarding sound as it came in). No game, no Windows: tools/seat_events_check.cpp.
//  - Vehicle +0x628 (16 bits): bit i = seat i is taken, as the vehicle last saw it; bit i << [+0x620] = seat i changed
//    this frame. [+0x620] is the seat count (+0x618, written at 0x629C8F in the vehicle's set-up 0x629450).
//  - The seat scan 0x62EEC0 (its loop 0x62F160..0x62F1BD) compares each seat's rider (seat +0x268, alive) with its bit;
//    on a difference it flips the bit and sets the seat's change bit. A seat emptied by 0x634940 clears it and sets it.
//  - The car / helicopter / walker updates (0x674B1A, 0x652713, 0x6437D0) call 0x632A60(vehicle, boarding preset,
//    leaving preset) (the car: car_base_se_table [8] 搭乘 / [9] 降車): each seat with its change bit plays the boarding
//    preset at the seat when a rider is in it, else the leaving one (0x7B4510); the base update 0x630250 then clears
//    the change bits (0x630596: and [+0x628], [+0x624]).
// A soldier created aboard (support_dispatch.cpp BoardAirborne: NpcSeatCrewNow) is no boarding: the vehicle should see
// that seat as taken from the start. Quiet() is the mask the vehicle would hold had the rider always been there.
#pragma once
#include <cstddef>
#include <cstdint>

namespace seatevt {
inline constexpr std::size_t kMask=0x628,kShift=0x620;
// The native arithmetic exactly: the seat's bit is `1 << seat` in 8 bits (`shl al, cl` at 0x632ACC / 0x62F165: none
// for seat 8 and up, whose seats the vehicle never tracks), its change bit that one `<< shift` in 16 bits (`shl ax, cl`
// at 0x62F1B7: none when it falls past bit 15).
constexpr std::uint16_t SeatBit(unsigned seat) noexcept { return seat<8 ? static_cast<std::uint16_t>(1u<<seat) : 0; }
constexpr std::uint16_t ChangeBit(unsigned shift,unsigned seat) noexcept {
    return shift<16 ? static_cast<std::uint16_t>((static_cast<unsigned>(SeatBit(seat))<<shift)&0xFFFFu) : 0;
}
constexpr std::uint16_t Quiet(std::uint16_t mask,unsigned shift,unsigned seat) noexcept {
    return static_cast<std::uint16_t>((mask|SeatBit(seat))&~ChangeBit(shift,seat));
}
// Whether 0x632A60 would play a seat sound for `seat` now.
constexpr bool Sounds(std::uint16_t mask,unsigned shift,unsigned seat) noexcept {
    const std::uint16_t change=ChangeBit(shift,seat);
    return change && (mask&change);
}
// Whether the seat scan would mark `seat` changed: the rider's presence differs from its bit.
constexpr bool ScanChanges(std::uint16_t mask,unsigned seat,bool rider) noexcept {
    return SeatBit(seat) && ((mask&SeatBit(seat))!=0)!=rider;
}
}  // namespace seatevt
