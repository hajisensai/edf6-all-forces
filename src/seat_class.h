// Every class in every seat (ini AllClassesRide; allclass.cpp, docs/all-classes-ride-re.md): the pure part, header only
// (tests/seat_class_test.cpp runs it without the game).
//
// The stock seat test (CanRideSeat 0x6346FC, CanRide 0x62DD24): human+0x31C & seat+0x34 & seat+0x30 non-zero, where
//   - human+0x31C is the soldier's SGO game_object_vehicle_mask (read at 0x545EE4, stored at 0x5462D5): Ranger 1,
//     Wing Diver 2, Fencer 4, Air Raider 8, in every player and NPC soldier SGO of Root.cpk;
//   - seat+0x30 is the seat's class mask, vehicle_riding_position[i][4] (0x62B79C into the seat being built, copied
//     into the vehicle's seat vector by 0x628E20). The stock values: 15, 9 (R + AR) and 143 (bit 7: a human of
//     another team may take it, 0x6346EC);
//   - seat+0x34 is all ones from the seat's constructor (0x6293A4) and only ever copied.
// A seat opened to every class is the seat's own mask with the four class bits added, as if its SGO said so. A seat
// whose mask holds no class bit at all admits no soldier and stays as it is: 0 is how the Proteus rework closes a
// seat (proteus.cpp, written at run time over the widened mask and put back afterwards); the other bits (bit 7) are
// kept.
#pragma once
#include <cstdint>

namespace seatclass {
constexpr std::uint32_t kClassBits=0x0F;   // R 1 | WD 2 | F 4 | AR 8
constexpr std::uint32_t Widen(std::uint32_t mask) noexcept { return (mask&kClassBits) ? (mask|kClassBits) : mask; }
// The stock seat test, as the game makes it.
constexpr bool Admits(std::uint32_t human,std::uint32_t seatOn,std::uint32_t seatMask) noexcept { return (human&seatOn&seatMask)!=0; }

// The ride state's entry (HumanBase 0x57ADF0): a seat shown with its rider (the seat locator's MAB DispCharacter,
// seat+0x254) plays the locator's Animation (seat+0x10) on the rider's CAS by name (0x116C2D0 at 0x57AE8E); a name
// the rider's CAS lacks is refused (0x11664F0 gives -1) and the rider keeps the state it had on foot. The stock
// shown seats' Animations: vehicle503_bike_driver / vehicle613_bike_driver (the Freed bikes: in the Ranger's and the
// Air Raider's CAS only) and vehicle_striker_rear (the trucks' and the rescue tank's benches: in all four). Every
// other stock seat hides its rider (DispCharacter 0). A rider whose class lacks the seat's state sits as on a bench:
// kFallbackRide, the one riding state all four classes have.
constexpr wchar_t kFallbackRide[]=L"vehicle_striker_rear";
}  // namespace seatclass
