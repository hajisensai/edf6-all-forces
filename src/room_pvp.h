// The room's PvP setup (room_pvp_logic.h): this member's pick, the host's settings and lock on EDF6Coop's member
// attributes, the host's start held until every member acknowledged the lock, and the 对战 pages' view.
#pragma once
#include "room_pvp_dialog.h"

namespace crew {
// Plugin initialization: the start gate (0x73E3E9: the host's "everyone ready, publish the start" check). False (logged)
// when the code differs: the room then starts as it always did, and the mission's M map panel is the only way to set
// teams.
bool InstallRoomPvp() noexcept;
// The room screen's frame (UI thread): publish, read, acknowledge, keep the lock current while the start is held.
void RoomPvpTick() noexcept;
// The room was left (a mission starting keeps the last lock for it).
void RoomPvpLeave() noexcept;
// The 对战 pages' view now, and a change a page asked for (UI thread).
roompvp::View RoomPvpView() noexcept;
void RoomPvpChange(const roompvp::Action& action) noexcept;
// The units page's line: this member's faction and role ("" when the room has no PvP: offline).
void RoomPvpSummary(wchar_t* out,std::size_t n) noexcept;
// Any thread: the host's lock this machine started from (the host: the one every member acknowledged when the start was
// let through; a member: the one it acknowledged), and its serial (false: none, or PvP off). The host's pvp.cpp applies it
// through the protocol once per mission world (each retry too); every machine preloads its units (debug_spawn.cpp).
bool RoomPvpMatch(pvp_protocol::State* out,std::uint32_t* serial) noexcept;
}  // namespace crew
