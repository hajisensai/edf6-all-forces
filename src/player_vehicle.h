// The jets a player summons with a vehicle request, loaded as SupportVehicle_OWN_* says (player_vehicle_logic.h: the
// design, the decision and the wire; docs/feature-2026-10-25-room-menu-review.md §4). Game thread only.
#pragma once
#include "plugin_net_logic.h"

namespace crew {
// At load: the request transporter's container configure call (0x5E5687) watched. False (logged): the requests make the
// stock twins as always, and this machine tells no peer it has a loaded jet (so no requester picks one).
bool InstallPlayerVehicles() noexcept;
// A mission's start (mission.cpp, after PreloadSupportVariants): this machine's own loaded jets and every other room
// member's (room_sync.h ForEachRoomPlayerVehicle) whose files are in Mods/OBJECT preloaded; the decisions forgotten.
void PreloadPlayerVehicles() noexcept;
// The container's vehicle step (airdrop.cpp VehicleStep, before its CreateObject): the path it makes `sgo` from, swapped
// for the loaded jet when this request was decided so; `sgo` itself otherwise.
const wchar_t* PlayerVehicleStepPath(const wchar_t* sgo) noexcept;
// The PVEH channel (support_net.cpp registers it).
plugin_net::Channel PlayerVehicleNetChannel() noexcept;
}  // namespace crew
