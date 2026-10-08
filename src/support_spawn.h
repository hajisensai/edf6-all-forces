// Ground support's audited stock resources. Positions are supplied by the entry planner: this API
// never invents an entry, teleports towards the destination, or substitutes another vehicle.
#pragma once
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
};
constexpr int kSupportVehicleCount=static_cast<int>(SupportVehicleKind::count);
// Bounds read from each stock MDB in Root.cpk; rounded OUT, including the tank's barrel.
inline constexpr SupportVehicleSpec kSupportVehicles[]={
    {L"布莱克战车",L"app:/Object/V505_TANK_MISSION.SGO",1,1.61f,5.60f,2.63f,0x17DADB0,false},
    {L"救援装甲运兵车",L"app:/Object/V507_RESCUETANK_AI.SGO",5,3.19f,5.95f,6.68f,0x17DB590,false},
    {L"民用轻卡",L"app:/Object/V512_KEITRUCK_BGP.SGO",5,1.04f,2.18f,2.65f,0x17E01B0,true},
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
constexpr bool SupportVehicleEnvironmentAllowed(SupportVehicleKind kind,SupportEnvironment env) noexcept {
    const auto row=SupportVehicleInfo(kind);
    return row && (env==SupportEnvironment::surface || env==SupportEnvironment::underground ||
                   (env==SupportEnvironment::wasteland && row->wasteland));
}
// Called on the mission's preload thread; clears last mission's readiness before native preloads.
void PreloadSupportVehicles() noexcept;
bool SupportVehicleReady(SupportVehicleKind kind,SupportCrewMode mode) noexcept;
// Creates and initializes an EMPTY hull at the already verified entry, sets friend team and records
// its copy owner (owner is the parent; otherwise inherits SetSpawnOwner, as jet::Launch). Caller then
// registers its native network identity, boards real humans and starts its validated route; non-null is not a
// crew/delivery success. No RideAi / DummyVehicleRider participates in initialization.
unsigned char* SpawnSupportVehicle(SupportVehicleKind kind,SupportCrewMode mode,const float* entry,
                                   const float* heading,const void* owner) noexcept;
// Rollback only a hull returned above, while the caller still owns its live reference, before delivery.
bool DeleteSupportVehicle(unsigned char* vehicle) noexcept;
}  // namespace crew
