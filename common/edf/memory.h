// Memory primitives shared by EDF6VehicleCrew and EDF6AutoTurret (common/memory.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace edf {
// Whether [ptr, ptr+size) is committed and readable (with `writable`, also writable). Not a probing read:
// touching a PAGE_GUARD page would take the guard away. Regions are cached per thread for about a tick.
bool Readable(const void* ptr,std::size_t size,bool writable=false) noexcept;
// A 12-byte absolute jump to `target` in a page within rel32 reach of `anchor`, or nullptr.
void* AllocateNearThunk(const void* anchor,void* target) noexcept;
// Points the rel32 call (E8) or tail jump (E9) at `callSite` to `replacement` (through a near thunk), only
// while it still targets `expectedTarget`. `changed`: whether the site was written.
bool RedirectCall(unsigned char* callSite,void* expectedTarget,void* replacement,bool& changed) noexcept;
}  // namespace edf
