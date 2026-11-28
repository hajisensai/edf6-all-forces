// The two new player classes, ZwVehicler (4) and ZwRuneBlade (5) (src/soldier_classes.cpp,
// docs/plan-2026-11-28-new-classes-impl.md): the constants every module shares, and the patch that gives the game six
// SoldierInit elements and the new weapon categories. Their data: src/soldier_classes_logic.h.
#pragma once

namespace crew {
constexpr int kSoldierClassCount=6;        // 0..3 stock, 4 ZwVehicler, 5 ZwRuneBlade
constexpr int kStockSoldierClassCount=4;
constexpr int kSoldierZwVehicler=4;
constexpr int kSoldierZwRuneBlade=5;

constexpr bool IsNewClass(int type) noexcept { return type>=kStockSoldierClassCount && type<kSoldierClassCount; }
// The factory / internal name (L"ZwVehicler" / L"ZwRuneBlade") and the text table's name key
// (L"SoldierType_ZwVehicler" / ...) of a new class; nullptr for any other type.
const wchar_t* SoldierClassInternalName(int type) noexcept;
const wchar_t* SoldierClassNameKey(int type) noexcept;

// Patches the game (once, from EML6_Load, before the game status exists; module table kBeforeGameStatus). False:
// refused, nothing written (logged; the game keeps its four classes).
bool InstallSoldierClasses() noexcept;
// Whether the game reads six classes (the SoldierInit / category reads are redirected and the per-class counts
// clamped). Set by the install; the copies themselves are made when the game loads its mode data.
bool SoldierClassesActive() noexcept;
}  // namespace crew
