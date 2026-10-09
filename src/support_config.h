// Out-of-mission support configuration (EDF6VehicleCrew.ini, section VehicleCrew; the installer's menu 7 edits the
// same keys): which catalog entries may be called and what their real crews carry. Read at plugin start and on every
// ini save (plugin.cpp LoadConfig); the dispatcher reads the published copy at each request.
//   SupportDisabled=KEY,KEY...          catalog keys (SupportCallKey) that cannot be called; empty: all callable
//   SupportSquadWeapon=rifle            members of the 4-person squad
//   SupportSquadLeaderWeapon=rifle      every infantry squad's leader (squad and platoon)
//   SupportPlatoonWeapons=rifle,rocket,sniper   members of the platoon's three squads, one weapon a squad
//   SupportVehicleCrewWeapon=rifle      the real crews of tanks / transports / trucks (what they carry on foot)
//   SupportAircraftCrewWeapon=rifle     the real crews of the called aircraft
//   SupportAircraftCount_<KEY>=n        aircraft one call brings (0: the call's own number)
//   SupportPreset_<KEY>=rifle@1E3A8A*2,rocket   a seated entry's soldiers, colours (support_loadout.h)
//   SupportTankRounds=AP:1,HE:1         the support tanks' main gun, per tank (support_loadout.h)
// Weapons: rifle / flame / rocket / shotgun / sniper (or 步枪 / 火焰 / 火箭 / 霰弹 / 狙击): the stock Ranger templates'
// own AI weapons (support_call.h SupportWeapon). An invalid value keeps the default and is reported (Problems).
#pragma once
#include "support_call.h"
#include "support_loadout.h"
#include <cstddef>
#include <cstdint>

namespace crew {
inline constexpr int kSupportConfigUnits=64;   // catalog entries a bitmask can hold (SupportCallCount is 33: the transports since 2026-10-09)
inline constexpr int kSupportAircraftMost=8;
struct SupportConfig {
    std::uint64_t disabled=0;                  // bit = catalog index
    SupportWeapon squad=SupportWeapon::rifle,leader=SupportWeapon::rifle;
    SupportWeapon platoon[3]={SupportWeapon::rifle,SupportWeapon::rocket,SupportWeapon::sniper};
    SupportWeapon vehicleCrew=SupportWeapon::rifle,aircraftCrew=SupportWeapon::rifle;
    std::uint8_t aircraft[kSupportConfigUnits]{};   // 0: the call's own count
    SupportPreset preset[kSupportConfigUnits]{};    // SupportPreset_<key>; count 0: the entry's own load
    RoundMix tankRounds{};                          // SupportTankRounds; count 0: every tank HE (stock)
    wchar_t problems[256]{};                   // the invalid settings, for the HUD and the log; empty: none
    bool Enabled(int catalog) const noexcept {
        return catalog<0 || catalog>=kSupportConfigUnits || !((disabled>>catalog)&1u);
    }
};
// Reads one value of the ini (empty when absent); returns its length.
using SupportIniRead=std::size_t(*)(void* context,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept;
// The configuration key of catalog entry `index` (SupportCallKey), nullptr past the end.
using SupportKeyOf=const wchar_t*(*)(int index) noexcept;
// The seats a player may fill on catalog entry `index` (SupportCallSeats), for SupportPreset_<key>.
using SupportSeatsOf=int(*)(int index) noexcept;
bool ParseSupportWeapon(const wchar_t* text,SupportWeapon* out) noexcept;
const wchar_t* SupportWeaponName(SupportWeapon weapon) noexcept;     // the ini spelling
const wchar_t* SupportWeaponLabel(SupportWeapon weapon) noexcept;    // the HUD's
// Pure parse and validation (tests/support_config_test.cpp). `units`: catalog entries with keys (SupportCallCount);
// `seatsOf` nullptr: every entry takes a preset of up to 12.
SupportConfig ParseSupportConfig(SupportIniRead read,void* context,SupportKeyOf keyOf,int units,SupportSeatsOf seatsOf=nullptr) noexcept;
// Production: parse `iniPath` and publish it (plugin.cpp LoadConfig); the published copy (never freed, as Cfg).
void LoadSupportConfig(const wchar_t* iniPath) noexcept;
const SupportConfig& SupportCfg() noexcept;
}
