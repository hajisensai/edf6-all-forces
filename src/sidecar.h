// sidecar.cpp: the sidecar bike and the vehicles' passengers.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// sidecar.cpp: the sidecar motorcycle (EDF6VC_SIDECAR.SGO, docs/sidecar-re.md): a Freed bike whose second rider stands
// in the sidecar on foot (their own weapons), held there by the plugin. SidecarFrame after the stock input (the held
// gunner put back, an NPC gunner taken in, the bike driven for the player in the sidecar); SidecarBoard from the board
// button (crew.cpp FindSeatHook, before the stock seat search): true when it took the player into the sidecar (no seat
// then); SidecarHoldsPlayer: the player stands in its sidecar (crew.cpp gives it no NPC driver: the plugin drives);
// SidecarLevel: from the car step's setAngVel (physics.cpp), the roll part of a sidecar bike's angular velocity
// replaced by its way back to level.
bool InstallSidecar() noexcept;
bool IsSidecar(const void* vehicle) noexcept;
void SidecarFrame(unsigned char* vehicle) noexcept;
bool SidecarBoard(unsigned char* vehicle,unsigned char* human) noexcept;
bool SidecarHoldsPlayer(const void* vehicle) noexcept;
// Projectile candidates and explosion targets: only this passenger's current bike and its native driver.
bool SidecarBulletPass(const void* owner,const void* target,const void* ownerCtrl) noexcept;
// The game's warp of a human's character controller to `pos` (sidecar.cpp, as the ride exit does); false: not known.
bool WarpHuman(unsigned char* human,const float* pos) noexcept;
int SidecarPassengers() noexcept;     // passengers riding now (the bullets' hook's quick "nothing to pass" test)
bool SidecarBulletHooked() noexcept;  // jet_hooks.cpp: the bullets' candidate hook is in (InstallBulletPass)
// jet_hooks.cpp: Dreadnought `v`'s hull bodies in slot `slot` (0..3, dreadnought.cpp's ships; nullptr: none), its own and its
// riders' rounds pass through them.
void HullBodies(int slot,const unsigned char* v) noexcept;
void SidecarLevel(const void* body,float* w) noexcept;
void ResetSidecars() noexcept;
// physics.cpp: the car step's final setAngVel (0x6746C6) goes through the plugin (SidecarLevel), redirected at load.
bool SidecarLevelHooked() noexcept;
}  // namespace crew
