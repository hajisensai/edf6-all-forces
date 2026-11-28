// The Vehicler's V4..V9 at run time (docs/newclass-re/vehicler-v4v9-plan.md; the pure parts zw_unlock_logic.h and
// zw_heat_logic.h): the later vehicles' unlocks, V7's beam heat and V8's radiation zone. Module "vehicler gear"
// (module_table.h).
//
// Unlocks: each one is this machine's own save's (a weapon record's owned bit, as every weapon the player has); a request
// carries the requester's set (zw_vehicle_catalog.h ZwUnlockedKinds), so the room needs no message of its own for them.
//  - progress: GrantZwProgress at both 0xDC550 entries (airstrike.cpp Grant: the save load and a new game) and at each
//    mission's snapshot (zw_vehicle_catalog.cpp), when the current mode is the EDF6 story (content 0);
//  - capture: every frame each local Vehicler near or aboard an intact hull of a capture kind (V4) it does not own: the row
//    owned now and the mission's set updated (NoteZwUnlocked). The hull is a Vehicler's (zw_vehicle_dispatch.h records
//    every one on every machine, so a guest sees the host's and the other way round).
// V7 heat (the driver's machine: its weapon is the driver's): every frame the player rides a heat hull, its beam's rounds
// going down is firing; the heat scales the holder's damage multiplier (support_firepower.h: +0x40 of each holder) and,
// overheated, the weapon's cooldown (+0xE0C) is held so the stock update does not let it fire.
// V8 fallout (the host, offline this machine: the zone's blasts are the room's damage): a heat-less hull of the fallout kind
// that dies leaves a zone (zw_heat_logic.h Zone) pulsing edf41c::Blast while its wreck is in the scene.
#pragma once

namespace crew {
bool InstallZwVehicleGear() noexcept;
void ZwVehicleGearTick() noexcept;    // every frame, the game thread, after the support dispatch's (its hulls activated)
void ResetZwVehicleGear() noexcept;   // a new mission: heat, zones and capture notes forgotten
// Own the vehicles the EDF6 story's progress unlocks in `gs` (the game status; nullptr: the live one). Returns how many
// records it owned now (0 none new, -1 not the story / no table / no save to read). Logs what it granted.
int GrantZwProgress(unsigned char* gs) noexcept;
}  // namespace crew
