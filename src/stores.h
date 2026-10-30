// The jets' stores (src/stores.cpp, docs/stores-re.md): the missiles and bombs a jet carries besides its guns, each in
// a holder of its own (pylib/vcobjects.py JETS: guns L / R, the stores, the fuel tank). The plugin tells them apart by
// their weapon's SGO, picks and fires them itself (the 506 fires only holders 0-2 from its two fire bytes), and
// weighs them: a jet with stores aboard is heavier and draggier.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {
// Preserve native store role ids 0..3; special actions have their own semantics,
// rather than borrowing bomb merely to get an aiming-point cross.
// coax: a tank's / an IFV's coaxial machine gun, fired by its own (secondary) trigger, never one of the switch's choices
// (payload.cpp).
enum class StoreRole { air, ground, bomb, rocket, gun, drone, charge, coax };
// The round a gun store's sight names when its numbers alone would name it wrong (tools/gen_stores.py SHELLS): `native`
// the sight's own reading (reticle.h ShellOf), `heat` a shaped charge (it bursts, but is no HE), `canister` a cloud of
// balls (vhud.cpp Arm).
enum class StoreShell : std::uint8_t { native, heat, canister };
// The vehicles' own fire keys (StoreSpec::fireKey; pylib/vcobjects.py FIRE_KEYS): the second trigger, then the extra keys.
constexpr unsigned char kSecondTriggerKey=2,kFireLastKey=6;
struct StoreSpec {
    const wchar_t* prefix;   // its weapon files' names: prefix + rounds + ".SGO" (vcobjects.store_file)
    const char* name;        // shown in the cockpit
    StoreRole role;
    float mass;              // kg a round
    float drag;              // a round's share of the clean jet's parasitic drag
    unsigned group=0;        // nonzero: one selectable loadout spanning its native barrel holders
    unsigned barrel=0;       // original native holder index for that linked barrel
    bool nativeControl=false; // a native trigger source, independently selectable (alternate ammunition)
    StoreShell shell=StoreShell::native;
    // Its own fire key (pylib/vcobjects.py Store.key, docs/multi-weapon-keys.md): 0 none (the primary's cycle, or native);
    // kSecondTriggerKey the seat's otherwise unread second trigger (the BMPT's missiles: their own switch, T / the d-pad
    // right, payload.cpp Switch, cycles the weapons on it, never R / LB); 3..kFireLastKey VehicleFire<n>Key, one weapon
    // a key. Each fires at once with the primary and the other keys.
    unsigned char fireKey=0;
    bool autoTurret=false;   // fires itself with the vehicle's auto turrets on (VehicleAutoTurretKey; payload.cpp ControlOf, FireSeat)
};
// A jet kind's mass without stores (kg) and the durability its SGO gives it (the HP before the game's tier scales it).
struct JetMass { float mark,mass,durability; };
// A barrel that fires in turn with the other of its pair (pylib/vcobjects.py GroundVehicle.alternate, twin_barrels;
// src/twin_cadence.h): its weapon file (exact, any case) and its pair (from 1: one per belt of a twin gun).
struct TwinBarrel { const wchar_t* file; unsigned pair; };

// One holder of seat 0 that holds a store, as its weapon is now.
struct Store {
    unsigned char* weapon;
    const StoreSpec* spec;
    std::int32_t ammo;
    std::int32_t locked;     // targets in its lock list (homing ones)
    float lockRange;         // m (homing ones; its SGO's LockonRange)
};
constexpr int kMostStores=7;   // the gunship: its four pylons and its shells, cannon and gatling (playerjet_board.inc SpecialRoom)

// The stores aboard `vehicle` (seat 0's holders, in their order: the cockpit's cycle). Their count.
int ReadStores(unsigned char* vehicle,Store* out,int most) noexcept;
// Fires the store's weapon this frame (its trigger latch, weapon +0x139; held, again each frame).
void TriggerStore(const Store& s) noexcept;
// What `count` stores add to the jet of mark `mark`: its mass over its clean mass (>= 1) and its parasitic drag's
// share over clean (>= 0). A mark without a mass: none.
struct Burden { float mass,drag; };
Burden BurdenOf(float mark,const Store* stores,int count) noexcept;
// The kind of mark `mark` (pylib/vcobjects.py JET_MASSES: every jet's), or nullptr.
const JetMass* JetMassOf(float mark) noexcept;
// The `i`th of the NPC jets' weapon SGOs (stores.inc kJetWeapons): its jet's mark and its path (app:/weapon/...); nullptr
// past the last.
struct JetWeaponRow { float mark; const wchar_t* sgo; };
const JetWeaponRow* JetWeapon(int i) noexcept;
// Whether weapon `w` is one of the stores' (its SGO one of kStores'): the cockpit marks and sounds their locks itself.
bool IsStoreWeapon(const unsigned char* w) noexcept;
// The store weapon `w` is (its SGO one of kStores'), else nullptr.
const StoreSpec* StoreOf(const unsigned char* w) noexcept;
// The twin-barrel pair weapon `w` fires in turn within (its SGO one of kTwinBarrels'), else 0.
unsigned TwinPairOf(const unsigned char* w) noexcept;
// The file name of weapon `w`'s SGO (after the last separator of its resource key, upper case), `length` characters;
// nullptr when it cannot be read. Not zero-terminated where the key goes on.
const wchar_t* WeaponFile(const unsigned char* w,std::size_t* length) noexcept;
// Whether weapon `w` is one the installer hung on a stock vehicle besides its own (its SGO file one of the plugin's,
// EDF6VC_*: the jets' stores, tools/make_stock_stores.py's copies): the stock vehicles' switch goes round them.
bool IsLoadoutWeapon(const unsigned char* w) noexcept;
// The 506's weapon build made one weapon a holder, the other stock classes' build their holders past their own (stores.cpp,
// docs/stock-payload-re.md §4): at load. The stores' locks: lockon.h.
bool InstallStores() noexcept;
}  // namespace crew
