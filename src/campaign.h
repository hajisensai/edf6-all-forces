// The EDF5 campaign, the mission packs and the EDF5 online missions (edf5campaign.cpp, pack_switch.cpp,
// edf5online.cpp, edf5online_room.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
bool InstallEdf5Campaign() noexcept;
// The offline HQ's Q / E (LB / RB): the previous / next mission pack without the title on the way (pack_switch.cpp).
bool InstallPackSwitch() noexcept;
// Whether the mission-pack dialog would let the player pick content `id` (mgr: the GameDataMgr; edf5campaign.cpp).
bool MissionPackSelectable(std::uintptr_t mgr,int id) noexcept;
bool InstallPortedMissions() noexcept;   // PortedMissionsUnlocked: the packs' mission select (edf5campaign.cpp)
void SetMissionPackSaveHook(bool ready) noexcept;   // the save-load hook is in (airstrike.cpp): packs with Legacy rows owned
void MigrateMissionPackSave(unsigned char* gs) noexcept;   // a pack's loaded save moved to the rows now, once (edf5campaign.cpp)
bool InstallEdf41MenuBg() noexcept;      // EDF4.1's HQ background in the 4.1 packs (edf5campaign.cpp)
// The current mode's content id (0..2 EDF6's, then the installed packs': Cfg().edf5CampaignContent, edf41Content ...), -1
// when there is none to read (edf5campaign.cpp, as the mission select reads it). Any thread: only reads.
int GameContentId() noexcept;
// EDF5's compiled mission scripts online: the BVM executor's PreloadPlayerResource and CreatePlayer given their online
// half (edf5online.cpp). Edf5BvmOnlinePlayers: the patched creation loop's count.
bool InstallEdf5Online() noexcept;
bool Edf5OnlineReady() noexcept;
std::uint32_t Edf5BvmOnlinePlayers() noexcept;
bool Edf5BvmOnlinePlayerArgs(int index,int* pad,int* split) noexcept;
void Edf5BvmOnlinePlayerMade(int index,const void* made) noexcept;
void ResetEdf5Online() noexcept;
// More than the BVM loop's four online (edf5online_room.cpp, edf5online_room.h): EDF6Coop's BvmPlayerTableHooks in
// place here; every one of the mission's `count` players' machines said it creates them too (EDF6Coop member attribute
// EDF6X_BVMPLAYERS; false with the reason in `why`); this machine's own word told and the others' watched (the room's
// frame, room_ui.cpp).
bool Edf5CoopBvmTableHooked() noexcept;
bool Edf5RoomAllowsBvmPlayers(std::uint32_t count,char* why,std::size_t size) noexcept;
void Edf5OnlineRoomTick() noexcept;
}  // namespace crew
