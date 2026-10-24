// The EDF4.1 outfit (docs/edf41-outfits.md; outfit41.cpp): a fifth outfit, "The Shadow of New Despair" (绝望阴影再袭),
// picked in the HQ's outfit popup by either local player, stored by the plugin (EDF6VehicleCrew.outfit41.ini), the
// default in the EDF4.1 mission packs, and told to the online room (EDF6Coop member attributes) so every member sees it.
#pragma once
#include "core.h"
#include <cstdint>

namespace crew {
// At load: checks the code it rests on (src/outfit41_signatures.inc), then redirects the model path calls, the popup's
// show call, chains the five stock items and the 3D view's OnMessage. False, logged REFUSED, when the code differs:
// nothing patched.
bool InstallOutfit41() noexcept;
// mission.cpp's online session preload hook hands the call here: the member it preloads (this machine's or another's,
// its mission index) decides the model path call inside (0x59DD27), then the call is made.
std::uintptr_t Outfit41SessionPreload(PlayerPreloadFn call,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d);
// The room's / HQ's frame (room_ui.cpp, the UI thread, whatever the room menu's switches; the 3D view's messages do the same
// inside outfit41.cpp): this machine's value told to the room when it changed, the others' watched. At most every 0.5 s.
void Outfit41RoomTick() noexcept;
// mission.cpp's MissionSync_Begin hook, right before the game packs this machine's member record from GS (0x78E9A0,
// what the others build this machine's player from): the EDF4.1 colours put into the block the record reads, so the
// others never wait for a frame of the room to see them (whatever thread the script runs on: the swap is locked).
void Outfit41MissionSync() noexcept;
}  // namespace crew
