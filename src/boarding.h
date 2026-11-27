// boarding.cpp: boarding the plugin's aircraft.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// boarding.cpp: the player into it (the boarding gun's path). False, logged: not taken on (the boarding hook not in, no
// player, or refused: not a board-able friend, the player in a vehicle, no seats); nothing will press for it.
bool BoardingRequest(unsigned char* vehicle) noexcept;
bool BoardingReady() noexcept;                           // boarding.cpp: its hook is in (BoardingRequest can take one on)
bool BoardingUnderway() noexcept;                        // boarding.cpp: a boarding is still pressing the button
void BoardingTick() noexcept;
bool InstallBoarding() noexcept;   // after CheckHeliProfile (the board button)
void ResetBoarding() noexcept;
// While the boarding gun presses the board button for the player: the one vehicle a seat may be found in (crew.cpp
// FindSeatHook gives none in any other), else null.
const void* BoardingOnly() noexcept;
}  // namespace crew
