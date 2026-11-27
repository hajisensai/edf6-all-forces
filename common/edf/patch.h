// Code and vtable patch primitives shared by EDF6VehicleCrew and EDF6AutoTurret (common/patch.cpp). Each claims the
// bytes it writes for the calling file first (edf/code_owner.h; a vtable chain as kLayered).
#pragma once
#include <Windows.h>
#include <cstddef>
#include "edf/code_owner.h"

namespace edf {
// Whether the image holds `bytes` at `rva`. Call under __try (the caller's image may not be EDF.dll).
bool Matches(const unsigned char* image,std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept;
// Swaps one vtable entry, only if it still holds `expected` (atomic compare-exchange).
bool PatchVtableSlot(void** slot,void* expected,void* replacement,Claim kind=Claim::kSole,
                     const char* owner=__builtin_FILE()) noexcept;
// Hooks a vtable slot on top of whatever it holds now: the stock function, or another plugin's hook that ends
// in it (both plugins chain the same input slots), so the load order does not matter. `*next` gets what the
// hook must call through, before the slot is written (the game may call the hook at once). False when the
// slot is empty or changed under us; `*next` is then null.
bool ChainVtableSlot(void** slot,void* hook,void** next,const char* owner=__builtin_FILE()) noexcept;
// Writes `replacement` over `size` code bytes at `at`, only while they are `expected` (true when they already
// are `replacement`), and flushes the instruction cache.
bool PatchCode(unsigned char* at,const unsigned char* expected,const unsigned char* replacement,std::size_t size,
               Claim kind=Claim::kSole,const char* owner=__builtin_FILE()) noexcept;
// The branch RedirectToThunk writes: a rel32 call (E8: the thunk returns to the site's end) or jump (E9: it goes on
// wherever it likes, a cave jumping back itself).
enum class Branch : unsigned char { kCall=0xE8,kJump=0xE9 };
// The bytes RedirectToThunk writes at `site`: the branch to `thunk`, then nops to `size` (5..kMostRedirectBytes). False
// when `thunk` is out of rel32 reach of the site's end, or the size is not one it writes.
constexpr std::size_t kMostRedirectBytes=32;
bool BuildRedirect(const unsigned char* site,std::size_t size,const void* thunk,Branch branch,
                   unsigned char* out) noexcept;
// Turns the `size` code bytes at `site` into a call (or jump) to `thunk` and nops (BuildRedirect), only while they are
// `expected`, through PatchCode (claimed for the caller first, the cache flushed). `written` (size bytes, may be null)
// gets the bytes written, for the caller's own check that they are still there. False, nothing written, when the bytes
// differ, the thunk is out of reach or the code cannot be made writable. Takes no lock but the code owner registry's
// (a leaf one: held over its own scan, nothing called under it), so it may run under the loader lock.
bool RedirectToThunk(unsigned char* site,const unsigned char* expected,std::size_t size,const void* thunk,
                     Branch branch=Branch::kCall,unsigned char* written=nullptr,Claim kind=Claim::kSole,
                     const char* owner=__builtin_FILE()) noexcept;
// `code` copied into a fresh executable page near `site` (AllocateNearCode), then RedirectToThunk there. The page, or
// null with the page given back (nothing is left pointing at it).
const unsigned char* RedirectToCode(unsigned char* site,const unsigned char* expected,std::size_t size,
                                    const unsigned char* code,std::size_t codeSize,Branch branch=Branch::kCall,
                                    unsigned char* written=nullptr,Claim kind=Claim::kSole,
                                    const char* owner=__builtin_FILE()) noexcept;
// Turns an import call (FF 15 rel32, 6 bytes, `sig`) at `site` into a direct call to `hook` (E8 rel32 to a near
// thunk, then a nop). `*slotValue` gets what the import slot holds (the hook calls through it) before the site is
// patched; when it is already set (several sites of one import), the slot must hold the same.
bool RedirectImportCall(unsigned char* site,const unsigned char* sig,void* hook,void** slotValue,
                        const char* owner=__builtin_FILE()) noexcept;
// Puts `size` bytes at `at` back to the `previous` protection after a write. A failure leaves the code writable
// but the write itself took, so it is counted (ProtectRestoreFailures, logged once loaded), not returned.
void RestoreProtection(void* at,std::size_t size,DWORD previous) noexcept;
unsigned ProtectRestoreFailures() noexcept;
}  // namespace edf
