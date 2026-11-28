// The new classes' player objects (docs/plan-2026-11-28-new-classes-impl.md §1, §2 B; docs/newclass-re/spawn.md): the factories
// ZwVehicler (type 4) and ZwRuneBlade (type 5), registered in the game's own factory table (edf41classes AddFactory). Each object
// is built by AssultSoldier's constructor 0x54FF10 (0x2090 bytes, then the plugin's own part) and given cloned vtables: the main
// one (94 slots, its RTTI kept, so the dynamic_cast to SoldierBase at 0x5A4065 still holds), +0x120 (22) and +0x1940 (2). Slot 83
// (+0x298) returns the new type; the deleting destructor (slot 1, and slot 0 of both secondary tables, which reach it through the
// main table) frees the plugin's part before the stock destructor and the delete. Every other slot is copied from AssultSoldier's
// live tables right before each object is made, so the hooks patched into them (EDF6Coop's player sync, this plugin's) hold
// for the new classes too (soldier_factory.cpp; docs/newclass-re/spawn.md §6.4).
#pragma once
#include <cstdint>

namespace crew {
bool InstallSoldierFactory() noexcept;   // module "soldier factory" (src/module_table.h), kBeforeGameStatus before "soldier classes"

namespace soldier {
// Both factories are added to edf41classes' table and their clones built (InstallSoldierFactory succeeded). The game
// registers them with its own classes later in its start-up 0x7004F0 (0x701859), after it made the game status (0x701145,
// whose SoldierInit read asks this): the furthest that read can know. Classes 4 / 5 are offered only then (soldier_classes.cpp).
bool FactoriesReady() noexcept;
// 4 / 5 for an object a new-class factory made (alive: its vptr is the clone's), -1 for anything else (or null).
int ZwType(const void* object) noexcept;
// Whether `vptr` is a new-class clone's main vtable: the plugin's soldier checks that compare an object's vptr with the four stock
// soldier vtables (0x17CDF28 ...) must accept these too (an object's vptr is *(void**)object).
bool IsZwVtable(const void* vptr) noexcept;
// The plugin's own state of a new-class object: one pointer the class's behaviour owns, released by `release` when the object is
// destroyed (or replaced by another SetExtension). False when `object` is no new-class object.
using ReleaseFn=void(*)(void*) noexcept;
bool SetExtension(void* object,void* data,ReleaseFn release) noexcept;
void* Extension(const void* object) noexcept;   // null when none or not a new-class object
// Objects made / destroyed since load (the debug line and the tests' evidence).
struct Counts { std::uint32_t made[2],destroyed[2]; };
Counts Made() noexcept;
}  // namespace soldier
}  // namespace crew
