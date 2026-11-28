// The new classes' player objects (docs/plan-2026-11-28-new-classes-impl.md §1, §2 B; docs/newclass-re/spawn.md): the factories
// ZwVehicler (type 4) and ZwRuneBlade (type 5), registered in the game's own factory table (edf41classes AddFactory). Each object
// is built by AssultSoldier's constructor 0x54FF10 (0x2090 bytes, then the plugin's own part) and given cloned vtables: the main
// one (94 slots, its RTTI kept, so the dynamic_cast to SoldierBase at 0x5A4065 still holds), +0x120 (22) and +0x1940 (2). Slot 83
// (+0x298) returns the new type; the deleting destructor (slot 1, and slot 0 of both secondary tables, which reach it through the
// main table) frees the plugin's part before the stock destructor and the delete. Everything else is AssultSoldier's until the
// classes' own behaviour is written (later stages of the plan).
#pragma once
#include <cstdint>

namespace crew {
bool InstallSoldierFactory() noexcept;   // module "soldier factory" (src/module_table.h), the kFeatures phase

namespace soldier {
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
