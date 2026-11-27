// vehicleram.cpp's hooks (the logic is vehicleram.h).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// vehicleram.cpp: the ground vehicles' ram (README 载具撞击伤害). Install at load (the CarBase mass read's check, the
// Barga's own update hook: the one class crew.cpp does not chain); the frame from every vehicle's input (crew.cpp, the
// Proteus's among them) and that hook.
bool InstallVehicleRam() noexcept;
void VehicleRamFrame(unsigned char* vehicle) noexcept;
void ResetVehicleRams() noexcept;
}  // namespace crew
