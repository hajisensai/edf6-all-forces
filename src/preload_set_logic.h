// What a mission preloads of the plugin's own content (hitch-4 / mem-1 / mem-5, docs/reviews/2026-11-24-optimization-audit.md),
// pure (no Windows, no game: tests/preload_set_test.cpp). src/preload_set.cpp decides the set at each mission's start;
// the Preload* functions (mission.cpp MissionStart) ask it item by item before they queue a file (0x7A3780).
//
// Before 2026-11-24 every mission preloaded every call's resources the install had (38 jet bodies and their weapons, the
// Dreadnought's 111 rounds, the submarine carrier, the laser, every support soldier kind...), whatever the room could call.
// Now the set is what this mission can reach:
//   - the map's support bar: the catalog entries enabled by the host's SupportDisabled (offline: this machine's; online
//     the host's catalog is what every member's request is planned against, support_dispatch.cpp PlanCfg), and the
//     support soldiers' kinds of every member's support loadout (member_config.h: each member's calls carry its own);
//   - the call weapons (calls.inc kCallRows) every member has equipped: the vehicle requests' jets, the thrown drones,
//     the Dreadnought with its hangar and rounds.
// Online every machine must preload the same set (the objects are made on every machine): each member says what its
// own loadout reaches (Needs, in its host-settings word: host_settings_logic.h), the host joins them with its catalog and
// says the set, and every machine takes the host's set at the mission's sync (MissionSync_Begin), before the load.
// Offline: the same algorithm over this machine alone.
//
// PreloadMode (ini, a developer's switch for measurements): 0 the set above, 1 everything installed (the old way),
// 2 nothing of the plugin's (A/B of the plugin's cost; the calls then do not come). Online the host's set decides, so
// a member's PreloadMode only changes what it contributes as the host.
#pragma once
#include "zw_crew_vehicle_logic.h"
#include "zw_vehicle_catalog.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace crew::preload {

// One bit per preloadable thing. Bodies are jet_internal.h kBodies' indexes (static_assert in preload_set.cpp).
enum class Item : std::uint8_t {
    body0=0,                 // .. body0+39: kBodies[k] (with its weapons, stores.inc kJetWeapons), k<kLowBodies
    catch0=40,               // .. catch0+6: playerjet_kinds.h kCatchFiles[i] (the catch after an ejection)
    chute=48,                // the parachute's canopy (playerjet.cpp ChuteMake)
    rope=49,                 // a helicopter's rope (transport.cpp's rope descent)
    sub=50,                  // the submarine carrier (subcarrier.cpp)
    laser=51,                // the portal laser and its sight (carrierlaser.cpp)
    airdrop=52,              // the stock container a transport plane drops one in (airdrop.cpp)
    groundHulls=53,          // the stock ground support hulls (support_spawn.cpp: tank, transport, truck)
    drillTransport=54,       // the infantry calls' drill transport underground (support_spawn.h)
    ultimate=55,             // the ultimate strike's magic circles (ultimate.cpp)
    dnRounds=56,             // the Dreadnought's turret rounds (jet_bay.cpp kDnRounds) and, with them, the gunship's shells
    vehicleRounds=57,        // the impact charges, the drill charge, the EMC / Sazabi / Proteus / Andryusha rounds
    bodyMore=58,             // .. bodyMore+5: kBodies[kLowBodies+k] (the bodies past body0's room: every bit below keeps
                             // what it says, the host-settings word's p= / n= unchanged for the items already there)
    soldier0=64,             // .. soldier0+16: support_call.h SupportWeapon k's two bodies (support_soldier.cpp)
    zwVehicle0=96,           // .. zwVehicle0+9: zw_vehicle_catalog.h kZwVehicles[k], the Vehicler's vehicle (support_spawn.cpp)
    end=128,
};
inline constexpr int kBodyItems=41,kCatchItems=7,kSoldierItems=17,kLowBodies=40;
// jet_internal.h Body's indexes this file names (static_assert against Body in preload_set.cpp).
namespace body {
enum : int { strike=0, fighter=1, bomber401=2, bomber501_2=3, interceptor=4, multirole=5, carrier=6, drone=7, blast=8,
             doll=9, heli410=10, heli506=11, gunship=12, blastCarrier=13, dollCarrier=14, enemyFighter=15, primerFighter=16,
             centipede=17, dragonfly=18, heliMedic=19, heliTransport=20, transportPlane=21, dnFighter=22, dnDefender=23,
             dnBomber=24, dnInterceptor=25, xmFighter=26, xmAttack=27, xmHeavyAttack=28, xmBomber=29 };
}
constexpr Item Body(int k) noexcept {
    return static_cast<Item>(k<kLowBodies ? static_cast<int>(Item::body0)+k : static_cast<int>(Item::bodyMore)+k-kLowBodies);
}
constexpr Item Catch(int i) noexcept { return static_cast<Item>(static_cast<int>(Item::catch0)+i); }
constexpr Item Soldier(int k) noexcept { return static_cast<Item>(static_cast<int>(Item::soldier0)+k); }
constexpr Item ZwVehicle(int k) noexcept { return static_cast<Item>(static_cast<int>(Item::zwVehicle0)+k); }
static_assert(static_cast<int>(Item::zwVehicle0)+kZwVehicleMost<=static_cast<int>(Item::end),"the Vehicler's vehicles fit");
static_assert(static_cast<int>(Item::body0)+kLowBodies<=static_cast<int>(Item::catch0),"bodies before the catch files");
static_assert(static_cast<int>(Item::bodyMore)+kBodyItems-kLowBodies<=static_cast<int>(Item::soldier0),
              "the further bodies before the soldiers");
static_assert(static_cast<int>(Item::catch0)+kCatchItems<=static_cast<int>(Item::chute),"catch files before the chute");
static_assert(static_cast<int>(Item::soldier0)+kSoldierItems<=static_cast<int>(Item::zwVehicle0),"soldiers before the Vehicler's vehicles");

struct Set {
    std::uint64_t w[2]{};
    constexpr bool Has(Item i) const noexcept {
        const int b=static_cast<int>(i);
        return b>=0 && b<128 && ((w[b>>6]>>(b&63))&1u);
    }
    constexpr void Add(Item i) noexcept {
        const int b=static_cast<int>(i);
        if(b>=0 && b<128)w[b>>6]|=std::uint64_t{1}<<(b&63);
    }
    constexpr void Join(const Set& o) noexcept { w[0]|=o.w[0];w[1]|=o.w[1]; }
    constexpr bool Empty() const noexcept { return !w[0] && !w[1]; }
    constexpr int Count() const noexcept {
        int n=0;
        for(int b=0;b<128;++b)n+=static_cast<int>((w[b>>6]>>(b&63))&1u);
        return n;
    }
    friend constexpr bool operator==(const Set& a,const Set& b) noexcept { return a.w[0]==b.w[0] && a.w[1]==b.w[1]; }
    friend constexpr bool operator!=(const Set& a,const Set& b) noexcept { return !(a==b); }
};
// What a call weapon (calls.inc kCallRows, by its row id) reaches of the plugin's own, into `out`. The vehicle request
// rows bring their object through the weapon's own resource (the game preloads an equipped weapon's); what the plugin adds
// is what flies with it: a requested jet's twin fires the NPC body's weapons (stores.inc kJetWeapons, preloaded by the
// body's mark), a carrier launches its drones, the thrown drones are the plugin's bodies, the Dreadnought's hangar
// launches its four fighters and its turrets fire their rounds. Every other row (the ground vehicles, the guns, the
// player jets PJET_*: their own SGO) reaches nothing of the plugin's preload.
inline constexpr std::wstring_view kCallPrefix=L"EDF6VC_CALL_";
// The Vehicler's vehicle rows (zw_vehicle_catalog.h, its slot 0) bring their vehicle: the plugin makes it at the start
// (zw_vehicle_dispatch.cpp), the game preloads nothing of it (the row is an inert accessory).
inline void AddCallItems(std::wstring_view row,Set& out) noexcept {
    if(const int kind=ZwVehicleKindOfRow(row);kind>=0){out.Add(ZwVehicle(kind));return;}
    if(row.substr(0,kCallPrefix.size())!=kCallPrefix)return;
    row.remove_prefix(kCallPrefix.size());
    struct Reach { std::wstring_view key; int bodies[4]; bool dnRounds; };
    static constexpr Reach kReach[]={
        {L"FLY_INTERCEPTOR",{body::interceptor,-1,-1,-1},false},
        {L"FLY_FIGHTER",{body::fighter,-1,-1,-1},false},
        {L"FLY_MULTIROLE",{body::multirole,-1,-1,-1},false},
        {L"FLY_GUNSHIP",{body::gunship,-1,-1,-1},false},
        {L"FLY_DRONE",{body::drone,-1,-1,-1},false},
        {L"FLY_CARRIER",{body::carrier,body::drone,-1,-1},false},
        {L"FLY_BLAST_CARRIER",{body::blastCarrier,body::blast,body::drone,-1},false},
        {L"FLY_DOLL_CARRIER",{body::dollCarrier,body::doll,body::drone,-1},false},
        {L"THROW_BLAST",{body::blast,-1,-1,-1},false},
        {L"THROW_DOLL",{body::doll,-1,-1,-1},false},
        {L"THROW_DRONE",{body::drone,-1,-1,-1},false},
        {L"DREADNOUGHT",{body::dnFighter,body::dnDefender,body::dnBomber,body::dnInterceptor},true},
        {L"XM_MEDIUM_FIGHTER_AA",{body::xmFighter,-1,-1,-1},false},
        {L"XM_MEDIUM_GUNSHIP_AA",{body::xmAttack,-1,-1,-1},false},
        {L"XM_LARGE_GUNSHIP_AA",{body::xmHeavyAttack,-1,-1,-1},false},
        {L"XM_LARGE_BOMBER_CORE",{body::xmBomber,-1,-1,-1},false},
    };
    for(const auto& r:kReach) {
        if(r.key!=row)continue;
        for(const int b:r.bodies)if(b>=0)out.Add(Body(b));
        if(r.dnRounds)out.Add(Item::dnRounds);
        return;
    }
}
// What every mission takes whatever the room can call: the rounds a vehicle of the player's may fire any time (a Proteus
// may be called any time, ProteusRework switched on mid-mission), the parachute's canopy (an ejection out of any jet the
// player flies), and every support soldier kind (the soldiers also crew the mission's own vehicles and the composition
// panel picks any installed kind mid-mission: support_soldier.cpp keeps one flag for them all, so they are not narrowed),
// with the vehicle a support Vehicler soldier gets (zw_crew_vehicle_logic.h kCrewKind: such a soldier may come any time).
constexpr Set Always() noexcept {
    Set s;
    s.Add(Item::vehicleRounds);
    s.Add(Item::chute);
    for(int k=0;k<kSoldierItems;++k)s.Add(Soldier(k));
    s.Add(ZwVehicle(zwcrew::kCrewKind));
    return s;
}

// Every item there is (PreloadMode=1: the old way, each still only when its file is installed).
constexpr Set All() noexcept {
    Set s;
    for(int k=0;k<kBodyItems;++k)s.Add(Body(k));
    for(int i=0;i<kCatchItems;++i)s.Add(Catch(i));
    for(int b=static_cast<int>(Item::chute);b<=static_cast<int>(Item::vehicleRounds);++b)s.Add(static_cast<Item>(b));
    for(int k=0;k<kZwVehicleCount;++k)s.Add(ZwVehicle(k));
    for(int k=0;k<kSoldierItems;++k)s.Add(Soldier(k));
    return s;
}
enum class Mode : std::uint8_t { needed=0, all=1, none=2 };
constexpr Mode ModeOf(int ini) noexcept { return ini==1 ? Mode::all : ini==2 ? Mode::none : Mode::needed; }
// What a machine says it needs (its word's n=, mode applied): PreloadMode=1 everything, 2 nothing, else `needed`.
constexpr Set ByMode(Mode m,const Set& needed) noexcept { return m==Mode::all ? All() : m==Mode::none ? Set{} : needed; }
// The host's set for the room (its word's p=): its own needs and every member's said needs. A member that said none
// (its word not out yet) adds nothing: the protocol keeps older builds out (mod_room.h, word v4's bump), so a member without a
// word is one whose word comes within a beat.
constexpr Set RoomSet(const Set& hostSet,const Set* members,int count) noexcept {
    Set s=hostSet;
    for(int i=0;i<count;++i)s.Join(members[i]);
    return s;
}

// The set as text: 32 hex digits, the high word first (the host-settings word's p= / n= fields).
inline constexpr std::size_t kTextSize=33;
inline bool Format(const Set& s,char* out,std::size_t size) noexcept {
    const int n=std::snprintf(out,size,"%016llX%016llX",static_cast<unsigned long long>(s.w[1]),
                              static_cast<unsigned long long>(s.w[0]));
    return n==32 && static_cast<std::size_t>(n)<size;
}
// Strict: exactly 32 upper-case hex digits.
inline bool Parse(std::string_view text,Set& out) noexcept {
    if(text.size()!=32)return false;
    std::uint64_t w[2]{};
    for(std::size_t i=0;i<32;++i) {
        const char c=text[i];
        const int d=c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : -1;
        if(d<0)return false;
        std::uint64_t& word=w[i<16 ? 1 : 0];
        word=(word<<4)|static_cast<std::uint64_t>(d);
    }
    out.w[0]=w[0];out.w[1]=w[1];
    return true;
}

// Online, at the mission's sync: what this machine preloads. The host's set when the host said one (every member then
// preloads the same); offline / as the host, this machine's own decision. A guest whose host said none is no room this
// build plays in (the room's protocol keeps older builds out, mod_room.h): it preloads nothing of the plugin's, and
// its calls do not come, rather than objects one machine has and another lacks.
enum class Source : std::uint8_t { local, host, hostSilent };
struct Decision { Set set; Source source=Source::local; };
constexpr Decision AtSync(bool inRoom,bool isHost,bool hostSaid,const Set& hostSet,const Set& local) noexcept {
    if(!inRoom || isHost)return Decision{local,Source::local};
    if(!hostSaid)return Decision{Set{},Source::hostSilent};
    return Decision{hostSet,Source::host};
}
}  // namespace crew::preload
