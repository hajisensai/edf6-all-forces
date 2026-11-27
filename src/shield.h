// shield.cpp: the vehicles' shields.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
void ResetShields() noexcept;     // shield.cpp
bool InstallShields() noexcept;                    // shield.cpp: the Shield Bearer's shield lets slow things through
// shield.cpp: whether a round (its candidate collector) passes a shield layer's body (slow: true, so it is left out)
bool ShieldLetsThrough(void* collector,std::uint32_t body) noexcept;
// shield.cpp: a fast vehicle's velocity (m/s) kept from crossing a hostile shield's face; the speed it lost
float ShieldBlock(const unsigned char* vehicle,float* vel) noexcept;
void ShieldVehicle(unsigned char* vehicle) noexcept;   // shield.cpp: the same for a vehicle with no plugin body
}  // namespace crew
