// The Vehicler's start seat (docs/newclass-re/vehicler-plan.md §2, §5 M3; the pure part vehicler_start_logic.h): a local
// Vehicler (soldier::ZwType 4) asks once a mission for the vehicle its slot 0 holds (zw_vehicle_dispatch.h RequestZwStart:
// the host plans it, every machine makes the same hull), and once that is ready is seated in its driver seat by the stock
// RideVehicle 0x5765E0, the ride announced to the room by 0x5763E0 (every peer's copy replays it: 0x577661). Refused,
// underground, not installed: one log line, the player walks in. The NPCs' seat pick leaves the owner's driver seat
// (npcai.cpp SeatTakes, vehicler_start_logic.h DriverSeatBarred).
#pragma once

namespace crew {
bool InstallVehiclerStart() noexcept;   // module "vehicler start" (src/module_table.h): the ride's natives checked
void VehiclerStartTick() noexcept;      // every frame, after the support dispatch's (its made hulls activated first)
void ResetVehiclerStart() noexcept;     // a new mission: the requests' records forgotten
}  // namespace crew
