// turretaim.cpp: the turrets' aim from EDF6AutoTurret for the HUD (hud.cpp TurretAimMarks; common/edf/aimlink.h).
#pragma once
#include "edf/aimlink.h"

namespace crew {
// The local player's turret readout (EDF6AutoTurret's: its mode, its lock, the lead circle) as of its last frame, game
// thread; false with that plugin absent, the player at none of its turrets, or ini TurretAimHud off.
bool PlayerTurretAim(edf::aimlink::TurretReadoutV1* out) noexcept;
// The same readout whatever TurretAimHud says (the turret camera asks the mode: turretcam.cpp).
bool AutoTurretReadout(edf::aimlink::TurretReadoutV1* out) noexcept;
// EDF6AutoTurret's answer whether it turned `vehicle`'s seat `seat` this game frame (aimlink.h Steers): 1 yes, 0 no,
// -1 no answer (the plugin absent or older than the V2 link).
int AutoTurretSteers(const void* vehicle,unsigned seat) noexcept;
// Whether EDF6AutoTurret takes the gun stabilizer's turn out of what it learns (aimlink.h V3 StabilizerAware): 1 yes,
// 0 it is loaded without that (older), -1 it is not loaded.
int AutoTurretStabAware() noexcept;
}  // namespace crew
