// crew.cpp's seat and vehicle primitives the other modules use.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// Never from inside a team walk's visitor (the board prompt, FindSeat): crew.cpp WithTeamField.
void SetObjectTeam(unsigned char* object,std::int32_t team) noexcept;
// The NPC in seat `from` moved to seat `to` (seat, then clear, as the bump moves one); false: not moved, the NPC where it
// was (crew.cpp).
bool MoveRider(unsigned char* vehicle,unsigned from,unsigned to) noexcept;
// crew.cpp (game thread): the vehicle has been started this mission: someone other than this machine's player (an NPC,
// another machine's player) has been aboard, or PowerVehicle said this machine's player started it (a panoramic
// cockpit's boot, cockpit_instruments.h kBootSteps). A vehicle the crew table has no room for reads as never started.
bool VehiclePowered(const void* vehicle) noexcept;
void PowerVehicle(const void* vehicle) noexcept;
// The vehicle class (crew.cpp kClasses) of an object by its vtable, -1 for anything else (a board-able vehicle or not).
int VehicleClassOf(const void* object) noexcept;
// The seat buttons `vehicle`'s stock player input tests in its seat `seat` (seat_switch_logic.h CarPadReads: the Car's
// path, a Grape's driver A / LB / L3 horn), 0 for the classes that read none (or are not reversed).
unsigned StockPadReads(const void* vehicle,unsigned seat) noexcept;
// crew.cpp: the seat's weapons whose stock aim line AimLines has hidden now (the walk it hides them by), at most
// `most`; how many.
int HiddenAimGuns(const unsigned char* seat,const unsigned char** out,int most) noexcept;
}  // namespace crew
