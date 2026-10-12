// The in-game loadout menu's saving (docs/feature-2026-10-12-room-loadout-menu.md, phase 2): what the menu calls.
//   SaveSupportLoadout     one SupportPreset_<KEY> / SupportVehicle_<KEY> / SupportCrewColour[_<KEY>] of EDF6VehicleCrew.ini
//                          set (empty: removed,
//                          the stock load), checked with the plugin's own parsers, written atomically, the config reloaded
//                          at once (loadout_ini.h, loadout_file.h)
//   StartVariantGeneration the installer's `loadout-generate` started in the background (Mods/Plugins/
//                          EDF6VehicleCrew.generator.exe, written by install): it makes the coloured soldiers / loaded
//                          vehicles' files the ini and the pending list name that are not there yet, new names only
//   VariantGenerationStatus what it is doing / did (Mods/Plugins/EDF6VehicleCrew.generate_status.txt)
// Game thread or any one thread; each call is short (no waiting on the generator).
#pragma once
#include "loadout_file.h"
#include "loadout_ini.h"

namespace crew {
bool SaveSupportLoadout(LoadoutField field,const wchar_t* unit,const wchar_t* value,wchar_t* why,std::size_t capacity) noexcept;
// A whole menu save: every item checked first (all or none written: loadout_ini.h EditLoadoutIniAll), then the ini read
// once, written once (atomic) and the config reloaded once. False: nothing written; `*failed` the item refused (-1: none
// in particular, e.g. the file unreadable).
struct LoadoutSave { LoadoutField field; const wchar_t* unit; const wchar_t* value; };
inline constexpr int kLoadoutSaveMost=128;   // room_menu.h SaveSet: 2 * kMostUnits lines
bool SaveSupportLoadouts(const LoadoutSave* items,int count,int* failed,wchar_t* why,std::size_t capacity) noexcept;
bool StartVariantGeneration(wchar_t* why,std::size_t capacity) noexcept;
GenerateStatus VariantGenerationStatus() noexcept;
}  // namespace crew
