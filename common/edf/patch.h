// Code and vtable patch primitives shared by EDF6VehicleCrew and EDF6AutoTurret (common/patch.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>

namespace edf {
// Whether the image holds `bytes` at `rva`. Call under __try (the caller's image may not be EDF.dll).
bool Matches(const unsigned char* image,std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept;
// Swaps one vtable entry, only if it still holds `expected` (atomic compare-exchange).
bool PatchVtableSlot(void** slot,void* expected,void* replacement) noexcept;
// Hooks a vtable slot on top of whatever it holds now: the stock function, or another plugin's hook that ends
// in it (both plugins chain the same input slots), so the load order does not matter. `*next` gets what the
// hook must call through, before the slot is written (the game may call the hook at once). False when the
// slot is empty or changed under us; `*next` is then null.
bool ChainVtableSlot(void** slot,void* hook,void** next) noexcept;
// Writes `replacement` over `size` code bytes at `at`, only while they are `expected` (true when they already
// are `replacement`), and flushes the instruction cache.
bool PatchCode(unsigned char* at,const unsigned char* expected,const unsigned char* replacement,std::size_t size) noexcept;
// Turns an import call (FF 15 rel32, 6 bytes, `sig`) at `site` into a direct call to `hook` (E8 rel32 to a near
// thunk, then a nop). `*slotValue` gets what the import slot holds (the hook calls through it) before the site is
// patched; when it is already set (several sites of one import), the slot must hold the same.
bool RedirectImportCall(unsigned char* site,const unsigned char* sig,void* hook,void** slotValue) noexcept;
// Puts `size` bytes at `at` back to the `previous` protection after a write. A failure leaves the code writable
// but the write itself took, so it is counted (ProtectRestoreFailures, logged once loaded), not returned.
void RestoreProtection(void* at,std::size_t size,DWORD previous) noexcept;
unsigned ProtectRestoreFailures() noexcept;
}  // namespace edf
