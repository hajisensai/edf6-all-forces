// drill.cpp: the drill tank.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// drill.cpp: the drill tank (EDF6VC_DRILL.SGO, docs/drill-re.md). DrillInput before the stock input (the player's
// trigger taken for the drill), DrillFrame after it (spin, pose, bites).
bool InstallDrill() noexcept;
bool IsDrillTank(const void* vehicle) noexcept;
void DrillInput(unsigned char* vehicle) noexcept;
void DrillFrame(unsigned char* vehicle) noexcept;
void ResetDrills() noexcept;
// The local player's drill (hud.cpp): its RPM, the top RPM, whether it touches something now, whether it is launched
// (flying) and on its way back (returning). False with none.
struct DrillCue { float rpm,maxRpm,heat; bool touching,overheated,flying=false,returning=false,keys=true; };
bool PlayerDrillCue(DrillCue* out) noexcept;
}  // namespace crew
