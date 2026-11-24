// This mission's preload of the plugin's own content (the set: preload_set_logic.h). mission.cpp MissionStart's Preload*
// functions ask Want item by item; host_settings.cpp says the room's set online (its word's n= / p=) and decides it at
// the mission's sync, the offline preload (HostSettingsOfflineMission) decides it from this machine alone.
#pragma once
#include "preload_set_logic.h"
#include "support_aircraft.h"
#include <atomic>

namespace crew {
struct SupportConfig;

namespace preload {
// What this machine's own play reaches (PreloadMode applied): its support catalog (SupportDisabled), its equipped call
// weapons, what every mission takes (Always); everything in the test range, with the debug spawn tool or in a PvP match
// (they make any body). Any thread (the room's tick, the mission's sync).
Set LocalNeeds() noexcept;
// The mission's set, decided before its preload: offline from LocalNeeds, online at the sync (host_settings.cpp).
void DecideOffline() noexcept;
void DecideOnline(const Decision& d) noexcept;
// MissionStart, before the first Preload*: the decided set taken for this mission (logged). A mission start no decision
// came for (none known: every start path decides) takes LocalNeeds, said.
void BeginMission() noexcept;
// This mission's set (BeginMission writes it, the Preload* functions after it on the same thread read it). Header-only so
// the Preload* functions' own fixtures need nothing of the decision; before any mission (and in those fixtures) every item.
namespace detail {
inline std::atomic<std::uint64_t> missionSet[2]={~std::uint64_t{0},~std::uint64_t{0}};
}
// Whether this mission preloads `i` (the Preload* functions; the file must still be installed).
inline bool Want(Item i) noexcept {
    const int b=static_cast<int>(i);
    return b>=0 && b<128 && ((detail::missionSet[b>>6].load(std::memory_order_relaxed)>>(b&63))&1u);
}
}  // namespace preload

// support_dispatch.cpp: what catalog entry `index` brings (false: no such entry). preload_set.cpp turns it into items
// (SupportPreloadCatalog over the entries the catalog enables).
struct SupportCatalogReach {
    bool aircraft=false;    // `spec`: the entry's aircraft (a flown call's, a transport's, an airdrop's plane, the rescue)
    SupportAircraft spec{};
    bool sub=false,rope=false,chute=false,airdrop=false,hulls=false,drill=false,ultimate=false;
};
bool SupportCatalogReachOf(int index,SupportCatalogReach& out) noexcept;
// preload_set.cpp: what the catalog entries `cfg` enables reach.
void SupportPreloadCatalog(const SupportConfig& cfg,preload::Set& out) noexcept;
// airstrike.cpp: what the call weapons this player has equipped (every class's slots) reach (preload::AddCallItems).
void AddEquippedCallItems(preload::Set& out) noexcept;
// jet_spawn.cpp: what a support aircraft reaches: its body, and a carrier's drones (its own blast / doll and the gun drone).
void AddSupportAircraftItems(const SupportAircraft& spec,preload::Set& out) noexcept;
}  // namespace crew
