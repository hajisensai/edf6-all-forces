// turretaim.cpp: the turrets' aim from EDF6AutoTurret for the HUD (hud.cpp TurretAimMarks; common/edf/aimlink.h).
#pragma once
#include "edf/aimlink.h"

namespace crew {
// The local player's turret readout (EDF6AutoTurret's: its mode, its lock, the lead circle) as of its last frame, game
// thread; false with that plugin absent, the player at none of its turrets, or ini TurretAimHud off.
bool PlayerTurretAim(edf::aimlink::TurretReadoutV1* out) noexcept;
}  // namespace crew
