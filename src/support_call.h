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
// What the map's support bar shows of the catalog (hud.cpp MapSupportBar): an entry's icon, and whether a call can be
// asked for now (one dispatcher serves every entry: a request still being planned, its cooldown after a delivery).
enum class SupportIcon : std::uint8_t { jet, heli, carrier, gunship, sub, squad, platoon, tank, apc, truck };
SupportIcon SupportCallIcon(int index) noexcept;
// Which of its kind's variants an entry is (the bar's chip icon): an aircraft guarding the mark or following the player,
// a ground vehicle crewed or delivered empty; none for the ones with no variants (the infantry, the submarine carrier).
enum class SupportVariant : std::uint8_t { none, guard, follow, crewed, empty };
SupportVariant SupportCallVariant(int index) noexcept;
enum class SupportReady : std::uint8_t { ready, planning, cooldown, off };
struct SupportReadiness { SupportReady state; int seconds; };   // seconds: the cooldown left
SupportReadiness SupportCallReadiness() noexcept;
}
