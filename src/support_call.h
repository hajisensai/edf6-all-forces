#pragma once
#include <cstddef>
#include <cstdint>
namespace crew {
// Real support soldiers are the stock Ranger templates N601_COMMON_RANGER_<variant>[_LEADER] of Root.cpk: the same
// AssultSoldier class, model and CAS, each loading its own stock AI weapon (tests/support_soldier_native_audit.py).
// A soldier resource id is (weapon << 8) | role, role 1 = member, 2 = squad leader. The rifle (AF) keeps ids 1 / 2,
// so protocol v2 plans from an older host remain the same soldiers.
// After the five Rangers, the other stock AI templates of Root.cpk a player composes a transport's load from (the user,
// 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人"): the Wing Divers N606_AIPALEWING_<LANCE|LR|MS|IZN|TB>[_LEADER]
// (class PaleWing, EDF6_wingDiver.cas) and the Fencers N607_AIHEAVYARMOR_<SC|SMC|SP|SSG>[_LEADER] (HeavyArmor,
// EDF6HeavyArmor.cas), each with its own stock AI weapon; Root.cpk has no Air Raider AI template. Every transport's
// passenger seat takes all four classes (the APC's mask 143, the truck's 15, the transports' 15). Only the Rangers are
// ini weapons (kSupportRangerWeaponCount, support_config.cpp); the others come only in a composed load, which a host
// plans only when every peer announced support_protocol.h kCapLoadout.
enum class SupportWeapon : std::uint8_t { rifle, flame, rocket, shotgun, sniper,
                                          wingLance, wingLaser, wingMonster, wingIzuna, wingThunderBow,
                                          fencerCannon, fencerMiddleCannon, fencerPileBanker, fencerShotgun, count };
inline constexpr int kSupportWeaponCount=static_cast<int>(SupportWeapon::count);
inline constexpr int kSupportRangerWeaponCount=5;
// A soldier kind's class: 0 Ranger, 1 Wing Diver, 2 Fencer (the bar's composition panel rows).
constexpr int SupportSoldierClass(SupportWeapon w) noexcept {
    return static_cast<int>(w)<kSupportRangerWeaponCount ? 0 : w<SupportWeapon::fencerCannon ? 1 : 2;
}
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
// --- A composed load (the user, 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人并且可以点多次，直到座位满") ---
// The soldiers a player puts aboard a call, in seat order: soldier i is the leader of its squad when i % 4 == 0 (the
// same rule as every infantry plan). On the wire (a request's `challenge`, support_protocol.cpp: unused by a request and
// ignored there by an older host) as 4 bits of count then 4 bits a soldier; 0 = no composition (the call's own load).
inline constexpr int kSupportLoadoutMost=12;
struct SupportLoadout { int count=0; SupportWeapon soldier[kSupportLoadoutMost]{}; };
static_assert(kSupportWeaponCount<=16 && kSupportLoadoutMost<=15,"a soldier fits 4 bits, the count 4 bits");
constexpr std::uint64_t PackSupportLoadout(const SupportLoadout& l) noexcept {
    if(l.count<=0 || l.count>kSupportLoadoutMost)return 0;
    std::uint64_t bits=static_cast<std::uint64_t>(l.count);
    for(int i=0;i<l.count;++i)bits|=static_cast<std::uint64_t>(static_cast<std::uint8_t>(l.soldier[i])&0xFu)<<(4+4*i);
    return bits;
}
// False (and *out empty) for 0 or anything malformed: a count past the most, a kind past the table, bits past the count.
constexpr bool UnpackSupportLoadout(std::uint64_t bits,SupportLoadout* out) noexcept {
    SupportLoadout l{};
    const int count=static_cast<int>(bits&0xFu);
    if(count<=0 || count>kSupportLoadoutMost || (bits>>(4+4*count))!=0){if(out)*out=l;return false;}
    l.count=count;
    for(int i=0;i<count;++i) {
        const auto kind=static_cast<int>((bits>>(4+4*i))&0xFu);
        if(kind>=kSupportWeaponCount){if(out)*out=SupportLoadout{};return false;}
        l.soldier[i]=static_cast<SupportWeapon>(kind);
    }
    if(out)*out=l;
    return true;
}
constexpr std::uint32_t SupportLoadoutResource(const SupportLoadout& l,int i) noexcept {
    return SupportSoldierResource(i>=0 && i<l.count ? l.soldier[i] : SupportWeapon::rifle,i%4==0);
}

int SupportCallCount() noexcept;
const wchar_t* SupportCallName(int index) noexcept;
// How many soldiers a player may put aboard the entry (the bar opens its composition panel), 0 for one called as it
// is: the infantry 12 (one plan, three squads), a transport helicopter / plane its 12 riders, a crewed APC / truck its
// passenger seats (the stock rows but the driver's: 4); a tank, an empty delivery, an air strike 0.
int SupportCallSeats(int index) noexcept;
// The load the entry brings uncomposed (the ini's weapons, support_config.h), the panel's start; false for none.
bool SupportCallPreset(int index,SupportLoadout* out) noexcept;
// SupportCallAt with a composed load (nullptr or empty: the call's own). A load past the entry's seats is refused.
bool SupportCallComposedAt(int index,const float* target,const SupportLoadout* load,wchar_t* note,std::size_t capacity) noexcept;
// The catalog entry's stable configuration key (EDF6VehicleCrew.ini SupportDisabled / SupportAircraftCount_<key>).
const wchar_t* SupportCallKey(int index) noexcept;
// A map / radio call of entry `index`; the sea rescue entry is refused here (only the rescue's own trigger asks for it).
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept;
// The entries the map's support bar offers and cycles (mapcmd.cpp): every one but the sea rescue, the catalog's last.
int SupportMenuCount() noexcept;
// The sea rescue (2026-10-09, the user: 「给救援加一个支援目录项，走正规的呼叫支援流程」): the catalog's last entry,
// after the ground ones, so every older index keeps its meaning on the wire. heli.cpp's trigger (a local player on
// foot in the sea, a submarine carrier out) asks for it at the swimmer through the same request path as a map call:
// offline / a one-player world's host plans it here, any other online machine through the host (support_net,
// kExtSeaRescue on every peer). One 410 made in the air at the edge, its real pilot made inside it and seated at once;
// its door seats are left for the swimmer. Same return and note as SupportCallAt.
int SupportRescueCatalog() noexcept;
bool SupportRescueAt(const float* target,wchar_t* note,std::size_t capacity) noexcept;
// heli.cpp's half of the rescue (the dispatcher calls these, game thread):
//  - a rescue deployment was made on this machine with every crew seated (`target`: its plan's, the request's point,
//    identical on every machine). `flown`: this machine runs its flight (offline, the host), else it is a peer's copy.
//    `requester`: where it is flown, the player who asked (support_net.h SupportTransactionRequester; empty: unknown,
//    the heli leaves at once).
//  - this machine's rescue request ended without a heli (refused, timed out, cancelled, interrupted, not made): `why`.
struct ObjRef;
void RescueHeliDeployed(unsigned char* vehicle,const float* target,bool flown,const ObjRef& requester) noexcept;
void RescueRequestFailed(const wchar_t* why) noexcept;
//  - the ground helicopters stood on this mission and none stands on now (helipad.h; heli.cpp): takeoff points.
int RescueTakeoffPads(float (*out)[3],int most) noexcept;
void SupportDispatchTick() noexcept;
void ResetSupportDispatch() noexcept;
void SupportCallStatus(wchar_t* out,std::size_t capacity) noexcept;
// What the map's support bar shows of the catalog (hud.cpp MapSupportBar): an entry's icon, and whether a call can be
// asked for now (one dispatcher serves every entry: a request still being planned, its cooldown after a delivery).
enum class SupportIcon : std::uint8_t { jet, heli, carrier, gunship, sub, squad, platoon, tank, apc, truck };
SupportIcon SupportCallIcon(int index) noexcept;
// Which of its kind's variants an entry is (the bar's chip icon): an aircraft guarding the mark or following the player,
// a ground vehicle crewed or delivered empty; none for the ones with no variants (the infantry, the submarine carrier).
// The transports' (support_dispatch.cpp TransportCatalog): a squad or a platoon flown in (the row: a helicopter assault, a
// paratroop drop). The container airdrops' (AirdropCatalog): the vehicle the plane drops (the row: the plane).
enum class SupportVariant : std::uint8_t { none, guard, follow, crewed, empty, squad, platoon, tank, apc, truck };
SupportVariant SupportCallVariant(int index) noexcept;
enum class SupportReady : std::uint8_t { ready, planning, cooldown, off };
struct SupportReadiness { SupportReady state; int seconds; };   // seconds: the cooldown left
SupportReadiness SupportCallReadiness() noexcept;
}
