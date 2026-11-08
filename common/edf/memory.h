// Memory primitives shared by EDF6VehicleCrew and EDF6AutoTurret (common/memory.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace edf {
// Whether [ptr, ptr+size) is committed and readable (with `writable`, also writable). Not a probing read:
// touching a PAGE_GUARD page would take the guard away. Regions are cached per thread for about a tick.
bool Readable(const void* ptr,std::size_t size,bool writable=false) noexcept;
// The pages of [ptrs[k], ptrs[k]+size) for k < n that Readable has not asked this tick, asked of the working set in one
// call (or a few: 64 pages a call) instead of one call each, and kept as Readable keeps them: its answers are the same,
// it only finds them in its cache. For a batch of objects about to be asked one by one (a soldier's weapons): the
// call's own cost was most of Readable's (stress:100, 2026-11-07: 10,600 calls a frame, 1.1 pages each, 6.7 ms).
// A null pointer, or a span Readable would ask by region (over 16 pages), is left out.
void PrefetchReadable(const void* const* ptrs,std::size_t size,int n) noexcept;
// PrefetchReadable over spans of their own sizes: the pages a caller is about to ask of several objects of different
// sizes (a soldier's weapon list and WeaponSets, then its weapons and the sets' entries: npcai.cpp ArmsOf), one call
// for all of them instead of one each. The same answers as Readable's own, kept for the tick as Readable keeps them.
struct ReadSpan { const void* ptr; std::size_t size; };
void PrefetchReadableSpans(const ReadSpan* spans,int n) noexcept;
// The calling thread's Readable counts since its last take (then zeroed): the PERF line's (debug) evidence of what the
// asks cost. QPC ticks in the working-set and VirtualQuery calls.
struct ReadableStats { unsigned long long asks,pageHits,wsCalls,wsPages,vqCalls,wsTicks,vqTicks; };
ReadableStats TakeReadableStats() noexcept;
// A 12-byte absolute jump to `target` in a page within rel32 reach of `anchor`, or nullptr.
// Copies `size` bytes of code into a fresh executable page within rel32 reach of `anchor`.
void* AllocateNearCode(const void* anchor,const unsigned char* code,std::size_t size) noexcept;
void* AllocateNearThunk(const void* anchor,void* target) noexcept;
// Points the rel32 call (E8) or tail jump (E9) at `callSite` to `replacement` (through a near thunk), only
// while it still targets `expectedTarget`. `changed`: whether the site was written.
bool RedirectCall(unsigned char* callSite,void* expectedTarget,void* replacement,bool& changed) noexcept;
}  // namespace edf
