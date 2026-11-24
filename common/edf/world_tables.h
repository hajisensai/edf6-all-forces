// The game's team table and lock-target registry, as both plugins read them: a side's relation row (who its enemies
// are) and the registry's list head and node count, which edf/enemy_points.h walks. One copy for EDF6VehicleCrew
// (heli.cpp, damagestats.cpp) and EDF6AutoTurret (plugin.cpp, designate.cpp). Game thread; call under __try.
#pragma once
#include "edf/enemy_points.h"
#include "edf/layout.h"
#include "edf/memory.h"
#include <cstddef>
#include <cstdint>

namespace edf {
// Team relations: manager *(EDF.dll+0x20B2978) -> its rows (+0x38, stride 0x38, one per team) -> int relation[team]
// (+0x18); relation[other] == enemypoints::kEnemyRelation (2): an enemy.
constexpr std::size_t kTeamManager=0x20B2978,kTeamRows=0x38,kTeamStride=0x38,kTeamRelation=0x18;
// Lock-target registry: *(EDF.dll+0x20B2AB0) holds an MSVC std::list<{raw T*, weak_ptr}>: its head at +8 and its size
// at +0x10 (0x22E210 bumps it, 0x22E330, against max_size). A walk goes over that many nodes at most -- the list's own
// count, not a fixed cap a battle's lock points pass (tests/lock_registry_native_audit.py).
constexpr std::size_t kLockRegistry=0x20B2AB0,kRegList=0x8,kRegSize=0x10;

// The relation row of `team` (relation[other] == kEnemyRelation: enemies), or null.
inline const std::int32_t* Relations(const unsigned char* image,std::int32_t team) noexcept {
    if(!image || team<0 || team>=enemypoints::kMaxTeam)return nullptr;   // no image: the plugin refused the load
    const auto manager=At<const unsigned char*>(image,kTeamManager);
    if(!Readable(manager,kTeamRows+8))return nullptr;
    const auto rows=At<const unsigned char*>(manager,kTeamRows);
    if(!Readable(rows+team*kTeamStride,kTeamStride))return nullptr;
    const auto relation=At<const std::int32_t*>(rows+team*kTeamStride,kTeamRelation);
    return Readable(relation,enemypoints::kMaxTeam*4) ? relation : nullptr;
}

// The registry's list head and node count, or false while it cannot be read.
inline bool LockList(const unsigned char* image,const unsigned char*& head,std::uint64_t& nodes) noexcept {
    if(!image)return false;
    const auto registry=At<const unsigned char*>(image,kLockRegistry);
    if(!Readable(registry,kRegList+0x10))return false;
    head=At<const unsigned char*>(registry,kRegList);
    nodes=At<std::uint64_t>(registry,kRegSize);
    return Readable(head,0x10);
}
}  // namespace edf
