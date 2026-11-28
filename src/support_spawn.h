// Ground support's audited stock resources. Positions are supplied by the entry planner: this API
// never invents an entry, teleports towards the destination, or substitutes another vehicle.
#pragma once
#include "support_level.h"
#include "zw_vehicle_catalog.h"
#include <cstdint>

namespace crew {
enum class SupportVehicleKind : std::uint8_t { tank, transport, civilianTruck, count };
// unmanned means EMPTY DELIVERY: a real driver brings it, stops and leaves by the native exit path.
// It never means an autonomous stock hull or an invisible dummy driver.
enum class SupportCrewMode : std::uint8_t { soldiers, unmanned };
enum class SupportEnvironment : std::uint8_t { surface, wasteland, underground };
struct SupportVehicleSpec {
    const wchar_t* name;
    const wchar_t* sgo;
    unsigned seats;       // actual stock vehicle_riding_position rows; includes the driver
    float halfWidth,halfLength,height; // conservative envelope about the native origin, metres
    unsigned vtable;
    bool wasteland;
    // Its weapons' damage goes by the support level on this stock request curve (support_firepower.h); unarmed: none.
    bool armed;
    supportlevel::Curve curve;
};
constexpr int kSupportVehicleCount=static_cast<int>(SupportVehicleKind::count);
// Bounds read from each stock MDB in Root.cpk; rounded OUT, including the tank's barrel.
inline constexpr SupportVehicleSpec kSupportVehicles[]={
    {L"布莱克战车",L"app:/Object/V505_TANK_MISSION.SGO",1,1.61f,5.60f,2.63f,0x17DADB0,false,true,supportlevel::Curve::blacker},
    {L"救援装甲运兵车",L"app:/Object/V507_RESCUETANK_AI.SGO",5,3.19f,5.95f,6.68f,0x17DB590,false,true,supportlevel::Curve::grape},
    {L"民用轻卡",L"app:/Object/V512_KEITRUCK_BGP.SGO",5,1.04f,2.18f,2.65f,0x17E01B0,true,false,supportlevel::Curve::eros},
};
constexpr const SupportVehicleSpec* SupportVehicleInfo(SupportVehicleKind kind) noexcept {
    const auto i=static_cast<unsigned>(kind);
    return i<static_cast<unsigned>(kSupportVehicleCount) ? &kSupportVehicles[i] : nullptr;
}
// Both modes require a real driver during transit. The delivery coordinator owns the arrival/exit
// transaction for unmanned; this hull factory does not report that transaction as completed.
constexpr bool SupportVehicleModeAvailable(SupportCrewMode mode) noexcept {
    return mode==SupportCrewMode::soldiers || mode==SupportCrewMode::unmanned;
}
// Underground none of these hulls (support_policy.h Allowed: only soldiers come in there, by the drill transport below).
constexpr bool SupportVehicleEnvironmentAllowed(SupportVehicleKind kind,SupportEnvironment env) noexcept {
    const auto row=SupportVehicleInfo(kind);
    return row && (env==SupportEnvironment::surface || (env==SupportEnvironment::wasteland && row->wasteland));
}
// The drill transport (docs/insertion-re.md §5; the user: 「地底可以用钻地坦克」): underground the infantry calls come in a
// drill tank (support_dispatch.cpp), the Air Raider's drill tank with passenger seats (tools/make_drill.py
// TRANSPORT_SGO_FILE: Vehicle505_Tank, the driver's seat and TRANSPORT_PASSENGERS more on its locators). Not a
// SupportVehicleKind: a kind more would add two ground calls and an airdrop to the bar and move every later entry (the
// transports, the airdrops, the strikes, the rescue). Its own plan resource (a hull of the ground range, past the kinds').
// Unarmed for the support's firepower: its one weapon is the drill's bit (fires nothing; drill.cpp works the drill).
// Bounds from pylib/drill_model.py, rounded out: the hull 4.8 m wide, 5.74 m tall, its back 3.6 m behind the origin, the
// drill's tip 9.9 m ahead (DRILL_BASE z 3.35 + DRILL_LENGTH 6.55).
inline constexpr SupportVehicleSpec kSupportDrillTransport=
    {L"钻地运兵车",L"app:/Object/EDF6VC_DRILL_TRANSPORT.SGO",13,2.4f,9.9f,5.8f,0x17DADB0,false,false,supportlevel::Curve::blacker};
inline constexpr const wchar_t kSupportDrillTransportFile[]=L"EDF6VC_DRILL_TRANSPORT.SGO";
inline constexpr std::uint32_t kSupportDrillTransportResource=0x20100u;   // kSupportVehicleResource + 0x100
// Called on the mission's preload thread; clears last mission's readiness before native preloads.
void PreloadSupportVehicles() noexcept;
// The drill transport preloaded this mission (its SGO the installer's: Mods/OBJECT) and not broken.
bool SupportDrillTransportReady() noexcept;
// An empty drill transport at the verified entry, as SpawnSupportVehicle makes a stock hull (the same class and seats
// required of what the game made); rolled back by DeleteSupportVehicle.
unsigned char* SpawnSupportDrillTransport(const float* entry,const float* heading,const void* owner) noexcept;
// The Vehicler's vehicles (zw_vehicle_catalog.h; docs/newclass-re/vehicler.md §2.2): the installer's files (Mods/OBJECT,
// tools/make_zw_vehicles.py), preloaded when this mission's set has their item (preload_set_logic.h Item::zwVehicle0 + kind),
// made as the stock hulls are (MakeHull: the class and seats of the generated table required of what the game made, the
// guns' firepower marked on the vehicle's curve, its mission setup, friend team, level 1). Not wasteland-capable, armed.
constexpr SupportVehicleSpec ZwHullSpec(const ZwVehicleSpec& v) noexcept {
    return SupportVehicleSpec{v.row,v.sgo,v.seats,v.halfWidth,v.halfLength,v.height,v.vtable,false,true,v.curve};
}
// An empty hull of kind `kind` at the verified entry (zw_vehicle_dispatch.cpp plans it); rolled back by DeleteSupportVehicle.
// nullptr: not ready (ZwVehicleReady), a bad entry / heading, or the game's object not the table's.
unsigned char* SpawnZwVehicle(int kind,const float* entry,const float* heading,const void* owner) noexcept;
bool SupportVehicleReady(SupportVehicleKind kind,SupportCrewMode mode) noexcept;
// Creates and initializes an EMPTY hull at the already verified entry, sets friend team and records
// its copy owner (owner is the parent; otherwise inherits SetSpawnOwner, as jet::Launch). Caller then
// registers its native network identity, boards real humans and starts its validated route; non-null is not a
// crew/delivery success. No RideAi / DummyVehicleRider participates in initialization.
// `variant`: a loaded copy of the hull (support_loadout.h VehicleVariantFile, app:/object/ path) preloaded this mission
// (support_variants.h); nullptr: the stock hull. The same class and seats are required of it.
unsigned char* SpawnSupportVehicle(SupportVehicleKind kind,SupportCrewMode mode,const float* entry,
                                   const float* heading,const void* owner,const wchar_t* variant=nullptr) noexcept;
// Rollback only a hull returned above, while the caller still owns its live reference, before delivery.
bool DeleteSupportVehicle(unsigned char* vehicle) noexcept;
// The independent setup step above, alone: reads the just-created vehicle's mission_setup (0x62D6E0), applies it
// (slot 46) and destroys the temporary setup by its variant (0x1765220). False when either step failed. The caller has
// checked those natives (debug_spawn.cpp) and that `vehicle` is a VehicleBase; it deletes the vehicle on false.
bool ApplyMissionSetup(unsigned char* vehicle) noexcept;
}  // namespace crew
