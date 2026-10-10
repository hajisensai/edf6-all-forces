// The generated variant files this machine has (support_loadout.h: coloured soldiers EDF6VC_NPC_*.SGO, loaded vehicles
// EDF6VC_LO_*.SGO, written by tools/support_loadout.py into Mods/OBJECT). Found and preloaded once at a mission's start
// (mission.cpp, after the stock soldiers and hulls): a plan only uses a variant that is ready here, and a host only one
// every peer's hello says it has too (support_protocol.h kExtVariants and its filter).
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {
inline constexpr int kSupportVariantsMost=64;   // files preloaded a mission (more are logged and left stock)
// Mission start: enumerate Mods/OBJECT/EDF6VC_NPC_*.SGO and EDF6VC_LO_*.SGO, preload each (the stock preload, as the
// soldiers' and hulls' own), and remember them for SupportVariantReady and the hello's filter.
void PreloadSupportVariants() noexcept;
// Whether `file` (an upper-case name, support_loadout.h SupportLookFile / VehicleVariantFile) was preloaded this mission.
bool SupportVariantReady(const wchar_t* file) noexcept;
// The hello's extension (support_protocol.h kExtVariants) and the Bloom filter of the files ready here (32 bytes).
void SupportVariantHello(std::uint32_t* ext,unsigned char* bloom32) noexcept;
// A variant this machine was asked for (its own preset, or a host's plan) whose file it does not have: its name is
// added once to Mods/Plugins/EDF6VehicleCrew.variants_pending.txt; the installer (menu 7 or 1, the game closed) makes
// every file listed there from its name (tools/support_loadout.py) and clears the list. Logged.
void NoteMissingVariant(const wchar_t* file,const char* why) noexcept;
}  // namespace crew
