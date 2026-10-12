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

}  // namespace crew::roomsync
