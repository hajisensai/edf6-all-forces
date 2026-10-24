// The room / HQ support loadout menu's screen (docs/feature-2026-10-12-room-loadout-menu.md stage 4; room_ui.cpp): the
// online room's (HUiRoom) and the offline HQ's (HUiHQMain) OnUpdate chained, the menu (room_menu.h) stepped from the
// keyboard and pad or its native menu Button Click, the screen's own input taken while it is open, and drawn into the elements tools/room_layout.py adds
// to their layouts (room_ui_logic.h decides what; the game's own TextField / ColorChipBox / texture skins draw it).
#pragma once
namespace crew {
// At load: checks the code it calls and chains both OnUpdates (reading each slot's current value: EDF6Coop chains the
// room's too). False, logged REFUSED, when anything differs: both screens stay as they are.
bool InstallRoomUi() noexcept;
// mission.cpp MissionStart: the room or HQ screen is gone (its sync stops publishing, the menu closes, nothing is kept).
void ResetRoomUi() noexcept;
}  // namespace crew
