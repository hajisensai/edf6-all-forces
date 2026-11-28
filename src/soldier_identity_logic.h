#pragma once
// Whether a game object is one of All Forces' new soldier classes, from its vptr and its vtable's RTTI pointer (vt[-1]):
// pure, no game memory (tests/soldier_identity_test.cpp). soldier_identity.h reads the two words, soldier_identity.cpp
// checks the addresses at load.
//
// The stock four classes are their own vtables (npcai.cpp kSoldiers / IsSoldierClass: AssultSoldier, PaleWing,
// HeavyArmor, Engineer). All Forces' two (docs/plan-2026-11-28-new-classes-impl.md: 4 Vehicler ZwVehicler, 5 Rune Blade
// ZwRuneBlade) are AssultSoldier's constructor (0x54FF10) with a cloned vtable that keeps AssultSoldier's RTTI pointer at
// vt[-1] (edf41classes.cpp BuildClone copies it), so their vptr is the plugin's own block and only vt[-1] names the class.
// A player of class 4 / 5 joins a room as every member's MissionSync record says (docs/newclass-re/online.md): the
// online code that names players (net_object_resolve.cpp, mission_participants.cpp, the PvP owners, enemy control's
// requester) has to know it is a soldier like the others.
#include <cstddef>
#include <cstdint>

namespace crew::soldier_identity {

// EDF.dll RVAs (tests/soldier_identity_native_audit.py reads them from the file).
constexpr std::uint32_t kStockVtables[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};  // AssultSoldier, PaleWing, HeavyArmor, Engineer
constexpr std::uint32_t kAssultSoldierVtable=0x17CDF28;
constexpr std::uint32_t kAssultSoldierCol=0x1C7B848;  // its CompleteObjectLocator (the qword at vtable-8, relocated)
constexpr std::uint32_t kAssultSoldierTd=0x2044A60;   // its TypeDescriptor (COL +0xC)
constexpr char kAssultSoldierName[]=".?AVAssultSoldier@@";  // TypeDescriptor +0x10

// MSVC x64 CompleteObjectLocator: signature 1, offset of the vtable in the complete object, cd offset, then image RVAs.
constexpr std::size_t kColSignature=0x0,kColOffset=0x4,kColTd=0xC,kColSelf=0x14,kTdName=0x10;

// The locator (its first 0x18 bytes as six dwords) is AssultSoldier's: the primary vtable's (offset 0), x64 (signature
// 1, its own RVA at +0x14), naming kAssultSoldierTd.
constexpr bool AssultSoldierLocator(const std::uint32_t (&col)[6]) noexcept {
    return col[kColSignature/4]==1 && col[kColOffset/4]==0 && col[kColTd/4]==kAssultSoldierTd && col[kColSelf/4]==kAssultSoldierCol;
}

// A clone of AssultSoldier's vtable: not the stock one (IsSoldierClass knows that), its RTTI pointer AssultSoldier's.
// `vt`: the object's vptr, `rtti`: the qword before it, `base`: EDF.dll's base. `clones`: the load's check of
// AssultSoldier's locator passed (soldier_identity.cpp); without it none is.
constexpr bool AssultSoldierClone(std::uintptr_t base,std::uintptr_t vt,std::uintptr_t rtti,bool clones) noexcept {
    return clones && base && vt && vt!=base+kAssultSoldierVtable && rtti==base+kAssultSoldierCol;
}

}  // namespace crew::soldier_identity
