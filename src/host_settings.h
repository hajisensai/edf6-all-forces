// The host's settings and the room's builds online (host_settings_logic.h: why, and the word every member publishes;
// coop_attr.h: EDF6Coop's member attributes). Offline, or as the host, every value is this machine's ini.
#pragma once
#include "room_sync.h"

namespace crew {
// The room's / HQ's frame (room_ui.cpp, UI thread), at most every 0.5 s: this machine's word told when it changed, the
// host's values and the others' builds read.
void HostSettingsRoomTick() noexcept;
// MissionSync_Begin (mission.cpp, the script's thread), before the load: the same read once more, and what every member
// must have for this mission decided (the 408's gunner seat), kept for the whole mission.
void HostSettingsMissionSync() noexcept;
// The values to play by (any thread; cheap): the host's as a guest whose host published them, else this machine's ini.
bool VanillaRecruitInEffect() noexcept;
bool NpcFreeSquadCoverInEffect() noexcept;
// The 408's sidecar gunner seat (edf41bike.cpp) this mission: offline always; online only when every member of the room
// said its build has it (host_settings_logic.h kFeatBikeGunner), as edf5online_room.h does for the player count.
bool Edf41BikeGunnerAllowed() noexcept;
// The room menu's status lines (UI thread): the host's settings when they are not this machine's, the builds that
// differ. Returns how many (at most `capacity`).
int HostSettingsLines(roomsync::SyncLine* out,int capacity) noexcept;
}  // namespace crew
