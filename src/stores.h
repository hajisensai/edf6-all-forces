// The jets' stores (src/stores.cpp, docs/stores-re.md): the missiles and bombs a jet carries besides its guns, each in
// a holder of its own (pylib/vcobjects.py JETS: guns L / R, the stores, the fuel tank). The plugin tells them apart by
// their weapon's SGO, picks and fires them itself (the 506 fires only holders 0-2 from its two fire bytes), and
// weighs them: a jet with stores aboard is heavier and draggier.
#pragma once
#include <cstdint>

namespace crew {
enum class StoreRole { air, ground, bomb, rocket };
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
constexpr int kMostStores=6;

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
// A homing store's lock as the weapon holds it (docs/stores-re.md §7): 2 locked (`point` its target's lock point), 1
// locking (`progress` 0..1, `point` the target), 0 none.
int StoreLock(const Store& s,float* point,float* progress) noexcept;
// Whether weapon `w` is one of the stores' (its SGO one of kStores'): the cockpit marks and sounds their locks itself.
bool IsStoreWeapon(const unsigned char* w) noexcept;
// Drops the store's locks now (the weapon relocks on its next tick).
void ClearStoreLock(const Store& s) noexcept;
// Drops the store's lock and keeps its target last in the crosshair's order for a while: the next one in the cone
// is locked (the cockpit's target cycle).
void NextStoreTarget(const Store& s) noexcept;
// The 506's weapon build made one weapon a holder, the stores' lock search ordered by the crosshair (stores.cpp): at
// load.
bool InstallStores() noexcept;
}  // namespace crew
