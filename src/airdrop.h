// airdrop.cpp: a support vehicle delivered by the plugin's transport helicopter in the game's own container (the stock Air
// Raider request's: docs/airdrop-vehicle-re.md). The user, 2026-10-09: "运输机还要能空投载具". One helicopter, one
// container, one vehicle: it flies to the point and hovers over it (heli.cpp HeliFerry), lets the container go and
// leaves; the container falls, settles and makes the vehicle, empty (team 5: anyone may board it), with its own mission
// setup applied.
// Offline only (refused in any session, a one-player room too): the stock container registers the vehicle on the room's network when in
// a session, from an identity the plugin's helicopter does not have (docs/airdrop-vehicle-re.md section 4).
#pragma once
#include "support_spawn.h"

namespace crew {
// mission.cpp: the container's SGO preloaded with the mission's own resources (the vehicles': PreloadSupportVehicles).
void PreloadAirdrop() noexcept;
// Whether a container drop of `kind` can be made now (the profile, the container and the vehicle preloaded).
bool AirdropReady(SupportVehicleKind kind) noexcept;
// support_dispatch.cpp: `carrier` (the transport helicopter, just in) flies to hover over `target` carrying a container
// for `kind`, made under it at once. False: nothing made, the helicopter's ferry let go (the caller sends it off).
bool AirdropBegin(const void* carrier,SupportVehicleKind kind,const float* target) noexcept;
// Once a frame (map.cpp, with TransportTick): the carried containers held under their carriers, let go over their points.
void AirdropTick() noexcept;
void ResetAirdrops() noexcept;   // mission.cpp: a new mission (the last one's containers went with its objects)
bool InstallAirdrop() noexcept;  // plugin.cpp: the profile checked, the container's vehicle step watched
}  // namespace crew
