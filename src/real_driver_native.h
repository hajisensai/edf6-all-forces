#pragma once

namespace crew {
// True only for a live real NPC seat whose input the local vehicle controller owns.
using NpcSeatInputOwnedFn=bool(*)(unsigned char* seat) noexcept;
bool InstallRealDriverNative(NpcSeatInputOwnedFn ownsSeatInput) noexcept;
bool RealDriverNativeReady() noexcept;
// The non-spawning portion of RideAi: optionally apply mission_setup, set NPC
// weapon flags and enable its existing path component. Does not create a rider,
// reparent a soldier, or set the snapshot mode that recreates a Dummy rider.
bool PrepareNpcVehicle(unsigned char* vehicle,bool spawned) noexcept;
}
