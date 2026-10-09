#pragma once
#include <cstddef>
#include <cstdint>
namespace crew {
// Real support soldiers are the stock Ranger templates N601_COMMON_RANGER_<variant>[_LEADER] of Root.cpk: the same
// AssultSoldier class, model and CAS, each loading its own stock AI weapon (tests/support_soldier_native_audit.py).
// A soldier resource id is (weapon << 8) | role, role 1 = member, 2 = squad leader. The rifle (AF) keeps ids 1 / 2,
// so protocol v2 plans from an older host remain the same soldiers.
enum class SupportWeapon : std::uint8_t { rifle, flame, rocket, shotgun, sniper, count };
inline constexpr int kSupportWeaponCount=static_cast<int>(SupportWeapon::count);
inline constexpr std::uint32_t kSupportRangerResource=1,kSupportLeaderResource=2,
    kSupportAircraftResource=0x10000,kSupportVehicleResource=0x20000;
constexpr std::uint32_t SupportSoldierResource(SupportWeapon weapon,bool leader) noexcept {
    return (static_cast<std::uint32_t>(weapon)<<8) | (leader ? kSupportLeaderResource : kSupportRangerResource);
}
constexpr bool IsSupportSoldierResource(std::uint32_t id) noexcept {
    return ((id&0xFFu)==kSupportRangerResource || (id&0xFFu)==kSupportLeaderResource) &&
           (id>>8)<static_cast<std::uint32_t>(kSupportWeaponCount);
}
constexpr bool IsSupportLeaderResource(std::uint32_t id) noexcept {
    return IsSupportSoldierResource(id) && (id&0xFFu)==kSupportLeaderResource;
}
constexpr SupportWeapon SupportSoldierWeapon(std::uint32_t id) noexcept {
    return IsSupportSoldierResource(id) ? static_cast<SupportWeapon>(id>>8) : SupportWeapon::rifle;
}
// An aircraft created in the air at the plan's matrix, its real crew created inside it and seated at once (2026-10-09,
// the user: "空中支援不是场外飞进来吗，不需要真起飞吧"). kSupportAircraftResource + catalog is the older plan of a host
// before this: the hull on a runway, its crew walking aboard (still applied as it was, for such a host).
inline constexpr std::uint32_t kSupportAirborneOffset=0x8000;
constexpr bool IsSupportAirborneAircraft(std::uint32_t id) noexcept {
    return id>=kSupportAircraftResource+kSupportAirborneOffset && id<kSupportVehicleResource;
}
constexpr bool IsSupportAircraft(std::uint32_t id) noexcept { return id>=kSupportAircraftResource && id<kSupportVehicleResource; }
static_assert(SupportSoldierResource(SupportWeapon::rifle,false)==kSupportRangerResource &&
              SupportSoldierResource(SupportWeapon::rifle,true)==kSupportLeaderResource,"protocol v2 rifle ids unchanged");
static_assert(SupportSoldierResource(SupportWeapon::sniper,true)<kSupportAircraftResource,"soldiers stay below hulls");
// Same catalog as native radio weapons. Online requests go through the host (support_net); a world whose only actual
// participant is this host is deployed here directly (no peer exists to replicate to).
int SupportCallCount() noexcept;
const wchar_t* SupportCallName(int index) noexcept;
// The catalog entry's stable configuration key (EDF6VehicleCrew.ini SupportDisabled / SupportAircraftCount_<key>).
const wchar_t* SupportCallKey(int index) noexcept;
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept;
void SupportDispatchTick() noexcept;
void ResetSupportDispatch() noexcept;
void SupportCallStatus(wchar_t* out,std::size_t capacity) noexcept;
}
