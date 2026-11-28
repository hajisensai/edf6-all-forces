#pragma once
// A soldier of any player class: the stock four (npcai.h IsSoldierClass), or a clone of AssultSoldier's vtable (All
// Forces' classes 4 Vehicler / 5 Rune Blade) by the RTTI pointer it keeps (soldier_identity_logic.h).
#include <cstdint>
#include <cstring>
#include "memory.h"
#include "npcai.h"
#include "soldier_identity_logic.h"

namespace crew {

namespace soldier_identity {
// EDF.dll's base, set once at load by InstallSoldierIdentity (before any mission) when AssultSoldier's locator checks
// out; 0: no clone is recognised.
inline std::uintptr_t checkedBase=0;
}  // namespace soldier_identity

// At load (module_table.h kSoldierIdentity): AssultSoldier's vtable-8 is its CompleteObjectLocator, and that names
// .?AVAssultSoldier@@. False (one log line): classes 4 / 5 are not recognised, the stock four still are.
bool InstallSoldierIdentity() noexcept;

// `object` (any pointer; read guarded) is a soldier of All Forces' classes 4 / 5.
inline bool IsAllForcesClassSoldier(const void* object) noexcept {
    const std::uintptr_t base=soldier_identity::checkedBase;
    if(!base || !object || !Readable(object,8))return false;
    std::uintptr_t vt=0,rtti=0;
    std::memcpy(&vt,object,8);
    if(vt<=8 || !Readable(reinterpret_cast<const void*>(vt-8),8))return false;
    std::memcpy(&rtti,reinterpret_cast<const void*>(vt-8),8);
    return soldier_identity::AssultSoldierClone(base,vt,rtti,true);
}
// `object` is a soldier of any player class, player or NPC.
inline bool IsPlayerClassSoldier(const void* object) noexcept { return IsSoldierClass(object) || IsAllForcesClassSoldier(object); }

}  // namespace crew
