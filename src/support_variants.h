// The generated variant files this machine has (support_loadout.h: coloured soldiers EDF6VC_NPC_*.SGO, loaded vehicles
// EDF6VC_LO_*.SGO, written by tools/support_loadout.py into Mods/OBJECT). Preloaded once at a mission's start (mission.cpp,
// after the stock soldiers and hulls): a plan only uses a variant that is ready here, and a host only one every peer's
// hello says it has too (support_protocol.h kExtVariants and its filter).
//
// What is preloaded is exactly what a plan of this mission can ask for (support_variants_logic.h MissionVariants), never
// a scan of the directory (generated files pile up there: a room's pre-sync only ever adds them):
//   this machine's own configuration (ForEachConfiguredVariant)
//   + the host's list the last room validated (room_sync.h ForEachRoomHostVariant; a client's)
//   + the last kPendingPreloadMost names on the pending list (a host's plan asked for them before; now generated)
// each one whose file is there. Each part is bounded, so the whole always fits kSupportVariantsMost.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {
// ForEachConfiguredVariant gives at most a look per seat (kSupportLoadoutMost 12), a crew and a loadout per catalog entry
// (kSupportConfigUnits 64), and the mission crews' two: 64 x 14 + 2 (support_variants.cpp checks the constants).
inline constexpr int kConfiguredVariantsMost=64*(12+2)+2;
inline constexpr int kHostVariantsMost=512;      // a host's whole list (room_sync_logic.h kSetMost, checked there)
inline constexpr int kPendingPreloadMost=128;    // the pending list's newest names
inline constexpr int kSupportVariantsMost=kConfiguredVariantsMost+kHostVariantsMost+kPendingPreloadMost;
// Mission start: the files MissionVariants names that are in Mods/OBJECT preloaded (the stock preload, as the soldiers'
// and hulls' own) and remembered for SupportVariantReady and the hello's filter.
void PreloadSupportVariants() noexcept;
// Whether `file` (an upper-case name, support_loadout.h SupportLookFile / VehicleVariantFile) was preloaded this mission.
bool SupportVariantReady(const wchar_t* file) noexcept;
// The hello's extension (support_protocol.h kExtVariants) and the Bloom filter (32 bytes: the wire's fixed 256 bits) of
// the files ready here that the host can ask for: with a host's list from the room, those of its files ready here only
// (fewer bits set: fewer false positives); without one, every file ready here (support_variants_logic.h HelloBloom).
void SupportVariantHello(std::uint32_t* ext,unsigned char* bloom32) noexcept;
// A variant this machine was asked for (its own preset, or a host's plan) whose file it does not have: its name is
// added once to Mods/Plugins/EDF6VehicleCrew.variants_pending.txt; the installer (menu 7 or 1, the game closed) makes
// every file listed there from its name (tools/support_loadout.py) and clears the list. Logged.
void NoteMissingVariant(const wchar_t* file,const char* why) noexcept;
// --- Crew colours (support_dispatch.cpp; docs/feature-2026-10-12-room-loadout-menu.md 阶段 5) ---
// The variant (support_loadout.h LookVariant, kVariantApplied as the host decides it for a squad's soldier) of a real crew
// soldier of `catalog` (a SupportCallKey entry, or support_net.h kMissionCrewCatalog) made from soldier `resource`: the
// colours SupportCrewColour_<KEY> / SupportCrewColour give it (SupportConfig::CrewLook). 0: stock, nothing wanted.
// Each crew seat carries its own (support_net.h Unit::variant), exactly as a squad's soldier does.
std::uint64_t SupportCrewVariant(std::uint32_t catalog,std::uint32_t resource) noexcept;
// The CreateObject path (app:/object/...) soldier `resource` carrying `variant` is made from: its look's file when the
// variant is applied and the file is ready here; else nullptr (stock), and a wanted file missing here goes on the pending
// list (`remote`: the host's plan). A soldier's colours never refuse anything.
const wchar_t* SupportSoldierLookPath(std::uint32_t resource,std::uint64_t variant,bool remote,wchar_t* path,
                                      std::size_t capacity) noexcept;
// Every variant file the current configuration (SupportCfg) can make a plan ask for, whatever is called: each catalog
// entry's SupportPreset_<KEY> colours (a leader's template every four seats, as SupportLoadoutResource) and its
// SupportVehicle_<KEY> pylons (the hull its key names, as the plan's HullVariant). Upper-case names (SupportLookFile /
// VehicleVariantFile), and every crew's colours (SupportCrewColour_*, the mission crews' too: SupportCrewVariant's
// files). A name possibly more than once. The room pre-sync's host list (room_sync.cpp). Returns how many were given.
int ForEachConfiguredVariant(void (*each)(void* context,const wchar_t* file),void* context) noexcept;
}  // namespace crew
