// crew.cpp's own steps of the module registry's passes (modules.cpp): what crew.cpp keeps for itself (its per-vehicle
// states, the underground watch, the player's exit watch), stepped from the passes like any module's.
#pragma once

namespace crew::frame {
void CrewVehicle(unsigned char* v,int cls) noexcept;   // the crews of `v` (class index `cls`)
void AimLinesVehicle(unsigned char* v) noexcept;       // the stock aim lines hidden or given back (with the plugin off too)
void JetReapVehicle(unsigned char* v) noexcept;
void HeliReapVehicle(unsigned char* v) noexcept;
void HeliVehicle(unsigned char* v) noexcept;           // a heli's NPC pilot (heli.cpp HeliFrame)
void SubVehicle(unsigned char* v) noexcept;            // the submarine carrier, whoever sits in it (subcarrier.cpp)
void GroundVehicle(unsigned char* v) noexcept;         // a ground robo's NPC driver (ground.cpp)
void UnderPlayerTick() noexcept;                       // the underground watch: the player
void UnderVehicleStep(unsigned char* v) noexcept;      // ...and each vehicle
void UnderReset() noexcept;                            // ...its watched objects dropped (the mission's start)
void ExitGroundWatchTick() noexcept;                   // the player off a vehicle put on the floor (exit_ground.h)
}  // namespace crew::frame
