// The host's settings and the room's builds online (host_settings_logic.h: why, and the word every member publishes;
// coop_attr.h: EDF6Coop's member attributes). Offline, or as the host, every value is this machine's ini.
#pragma once
#include "host_settings_logic.h"
#include "room_sync.h"
#include "scope_spread_logic.h"

namespace crew {
// The room's / HQ's frame (room_ui.cpp, UI thread), at most every 0.5 s: this machine's word told when it changed, the
// host's values and the others' builds read.
void HostSettingsRoomTick() noexcept;
// MissionSync_Begin (mission.cpp, the script's thread), before the load: the same read once more, and what every member
// must have for this mission decided (the 408's gunner seat), kept for the whole mission.
void HostSettingsMissionSync() noexcept;
// An offline mission's start (mission.cpp PreloadHook, the offline preload; online missions never reach it): what a
// mission sync decided for the room is dropped, this machine's values played by (the 408's gunner seat open).
void HostSettingsOfflineMission() noexcept;
// The values to play by (any thread; cheap): the host's as a guest whose host published them, else this machine's ini.
bool VanillaRecruitInEffect() noexcept;
bool NpcFreeSquadCoverInEffect() noexcept;
// The scoped weapons' spread (scope_spread.cpp, every player round): the host's ScopeSpread* as a guest whose host
// published them, else this machine's ini.
scopespread::Params ScopeSpreadInEffect() noexcept;
// The support rules this machine's 整备 support page shows (armory.cpp FillRules, UI thread; any thread): offline and as
// the host this machine's SupportDisabled / SupportPtScale; as a guest the host's from its word (known false until it
// arrived). host_settings_logic.h DecideSupportRules.
hostcfg::SupportRules SupportRulesInEffect() noexcept;
// The 408's sidecar gunner seat (edf41bike.cpp) this mission: offline always; online only when every member of the room
// said its build has it (host_settings_logic.h kFeatBikeGunner), as edf5online_room.h does for the player count.
bool Edf41BikeGunnerAllowed() noexcept;
// The commander mode this mission (commander.cpp, docs/feature-2026-11-21-commander-mode.md): offline this machine's
// ini CommanderMode at the mission's start; online decided at the mission's sync by the host's word, and only when every
// member's build has it (host_settings_logic.h CommanderAtSync). Any thread.
bool CommanderModeInEffect() noexcept;
// The room menu's status lines (UI thread): the host's settings when they are not this machine's, the builds that
// differ. Returns how many (at most `capacity`).
int HostSettingsLines(roomsync::SyncLine* out,int capacity) noexcept;
}  // namespace crew
