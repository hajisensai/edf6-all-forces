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
enum class StoreRole { air, ground, bomb, rocket, gun, drone, charge };
struct StoreSpec {
    const wchar_t* prefix;   // its weapon files' names: prefix + rounds + ".SGO" (vcobjects.store_file)
    const char* name;        // shown in the cockpit
    StoreRole role;
    float mass;              // kg a round
    float drag;              // a round's share of the clean jet's parasitic drag
};
// A jet kind's mass without stores (kg) and the durability its SGO gives it (the HP before the game's tier scales it).
struct JetMass { float mark,mass,durability; };

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
// Whether weapon `w` is one of the stores' (its SGO one of kStores'): the cockpit marks and sounds their locks itself.
bool IsStoreWeapon(const unsigned char* w) noexcept;
// The store weapon `w` is (its SGO one of kStores'), else nullptr.
const StoreSpec* StoreOf(const unsigned char* w) noexcept;
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
