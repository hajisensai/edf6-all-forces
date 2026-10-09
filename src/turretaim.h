// turretaim.cpp: the turrets' aim from EDF6AutoTurret for the HUD (hud.cpp TurretAimMarks; common/edf/aimlink.h).
#pragma once
#include "edf/aimlink.h"

namespace crew {
// The local player's turret readout (EDF6AutoTurret's: its mode, its lock, the lead circle) as of its last frame, game
// thread; false with that plugin absent, the player at none of its turrets, or ini TurretAimHud off.
bool PlayerTurretAim(edf::aimlink::TurretReadoutV1* out) noexcept;
bool PlayerTurretBinding(edf::aimlink::ModeBindingV1* out) noexcept;
// The same readout whatever TurretAimHud says (the turret camera asks the mode: turretcam.cpp).
bool AutoTurretReadout(edf::aimlink::TurretReadoutV1* out) noexcept;
// EDF6AutoTurret's answer whether it turned `vehicle`'s seat `seat` this game frame (aimlink.h Steers): 1 yes, 0 no,
// -1 no answer (the plugin absent or older than the V2 link).
int AutoTurretSteers(const void* vehicle,unsigned seat) noexcept;
// Whether EDF6AutoTurret takes the gun stabilizer's turn out of what it learns (aimlink.h V3 StabilizerAware): 1 yes,
// 0 it is loaded without that (older), -1 it is not loaded.
int AutoTurretStabAware() noexcept;
// The one player turret aim (aimlink.h V4): EDF6AutoTurret's bindings, lock and lead for the seat `seat` of `vehicle`
// whose turret the camera serves, asked once a game frame (turretcam.cpp TurretCamFrame) with `gun` the gun the turret
// turns (turretcam.cpp TurretGun). True: auto-aim holds a lock, `point` is where `gun`'s round meets it (the turret
// camera steers onto it in place of the screen's centre); false: the view steers (no peer, the lead circle, no lock).
bool PlayerTurretLead(const void* vehicle,unsigned seat,const void* gun,float* point) noexcept;
// The stock spot (原版 Q 标记) cast by a local player: not cast at all with VanillaSpot=0 (the default; the custom Q of
// qmark.cpp replaces it), else from a vehicle along the camera actually drawn, not the stock riding camera's locators
// (src/spot_ray.h): at load, the call 0x59B75C redirected (checked; else left stock).
bool InstallSpotRay() noexcept;
}  // namespace crew
