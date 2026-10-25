// The native every-team walk 0x5E0C80 (docs/team-manager-re.md 3): it visits the manager's row array (+0x38) up to the
// byte bound of its `cmp rsi,imm32` at 0x5E0D28, stock 0x188 (7 rows), kCapRows once team_native.cpp has installed
// the capacity (team_native_logic.h kSites). Every caller checks that array before it runs the walk: the rows the
// walk visits, 0 when its bound is not whole rows within kTeamCapacity, is not the manager's live row count (+0x48:
// a walk past the rows would read beyond the array) or the array is not readable that far.
#pragma once
#include "crew.h"
#include "memory.h"
#include "team_native_logic.h"
#include <cstdint>

namespace crew::team_walk {
constexpr unsigned kWalkBound=0x5E0D28;
constexpr unsigned char kWalkBoundOp[]={0x48,0x81,0xFE};   // cmp rsi,imm32
// `manager` already checked readable through +0x50.
inline std::uint64_t WalkRows(const void* manager) noexcept {
    using namespace team_native;
    if(!manager || !Matches(kWalkBound,kWalkBoundOp,sizeof(kWalkBoundOp)))return 0;
    const auto bound=At<std::uint32_t>(image,kWalkBound+sizeof(kWalkBoundOp));
    const auto rows=At<std::uint64_t>(manager,kRowCount);
    if(!bound || bound%kRowStride || bound/kRowStride!=rows || rows>static_cast<std::uint64_t>(kTeamCapacity))return 0;
    return Readable(At<const void*>(manager,kRows),bound) ? rows : 0;
}
}  // namespace crew::team_walk
