// Room-stage pre-sync of generated variant files over EDF6Coop's member attributes (EDF6CoopGetMemberAttributeApi v1,
// edf-coop-stable docs/member-attribute-api.md): the host announces the variant files the coming mission's support
// plans need; every member generates the ones it lacks before the mission starts.
// Contract between the menu screen (src/room_ui.cpp, stage 4) and the sync (src/room_sync.cpp, stage 6).
// Every call comes from the game's UI thread (the HUiRoom OnUpdate wrapper; offline HQ never calls it).
#pragma once
#include <cstddef>

namespace crew::roomsync {

enum class SyncState { unavailable, alone, waiting, ready };
// One line the menu shows (UTF-16, already localised), e.g. "全员已就绪" / "某某缺 2 个文件，生成中".
struct SyncLine { wchar_t text[128]; bool warn; };

// Once per room frame.
void TickRoomSync() noexcept;
// The room is gone (left / mission started): stop publishing.
void LeaveRoomSync() noexcept;
SyncState RoomSyncState() noexcept;
// Up to `capacity` lines for the menu's status area; returns how many.
int RoomSyncLines(SyncLine* out,int capacity) noexcept;
// The pre-sync turned off (RoomPreSync / RoomLoadout / Enabled): the host's list kept for the next mission dropped too.
void ForgetRoomHostList() noexcept;
// Any thread (a mission's start): each file of the host's list the last room validated (a client's; none on the host, or
// when there was no valid list), the names support_loadout.h writes. Kept after the room is left (a mission starting
// leaves it): what that mission's plans can ask of this machine (support_variants.h PreloadSupportVariants).
int ForEachRoomHostVariant(void (*each)(void* context,const wchar_t* file),void* context) noexcept;

}  // namespace crew::roomsync
