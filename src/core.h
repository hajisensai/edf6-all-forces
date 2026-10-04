// The core's entry points (crew.cpp, mission.cpp, loadout.cpp, overlay.cpp), which plugin.cpp installs in
// order. Included by crew.h.
#pragma once
#include <cstdint>

namespace crew {
// crew.cpp: the seat hooks (FindSeat, the on-foot prompt). False when no class's slot 49 could be hooked.
bool InstallCrew() noexcept;
// crew.cpp: chains every crewed class's per-frame input (the whole per-frame layer). Once, from whichever
// comes first after every plugin has loaded: the mission's start, the on-foot prompt, the board button.
void EnsureInputs() noexcept;
// mission.cpp: hooks the mission's player preload (offline and session), where MissionStart runs.
bool InstallMission() noexcept;
// The preload's call (0x59DE50's signature): mission.cpp's hook hands it to loadout.cpp, which wraps it in
// the forced test-range loadout when that is on, else just makes it.
using PlayerPreloadFn=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
std::uintptr_t LoadoutPreload(PlayerPreloadFn call,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d);
// loadout.cpp: forced test-range loadout (off unless EDF6TestRange.loadout.ini says Enabled=1).
bool InstallLoadout(const wchar_t* pluginIni) noexcept;
// overlay.cpp: the call-pick keys and banner (one thread for the plugin's life; the keys come from Cfg()).
void StartCallPicker() noexcept;
}  // namespace crew
