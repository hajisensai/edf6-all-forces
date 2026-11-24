// Who patched which code bytes (common/code_owner.cpp), one registry per DLL: each links its own edf6common.
//
// Every patch primitive (edf::RedirectCall, PatchCode, RedirectImportCall, PatchVtableSlot, ChainVtableSlot) claims the
// bytes it is asked to write, before it looks at them, for the source file that called it: an owner argument the
// compiler fills at the call (__builtin_FILE() as a default argument). A code write of a module's own (VirtualProtect,
// memcpy) claims them with ClaimCode first. Two files claiming one byte is a conflict whatever the write did after:
// the second module's patch is refused when the first changed the bytes it expected (the first person's culling was
// left out that way when the frame rate redirected 0x1198FA4 first: #395), or silently undoes it when it did not. A
// conflict goes to the sink (the plugin's log), the ones before the sink is set replayed into it.
//
// Claim::kLayered is a write meant to sit on top of another's (a chain over whatever a site calls now, perf_probe.cpp
// Chain; ChainVtableSlot): it conflicts with nothing and nothing conflicts with it.
#pragma once
#include <cstddef>
#include <cstdint>

namespace edf {
enum class Claim { kSole,kLayered };
struct ClaimConflict {
    std::uintptr_t at;std::size_t size;   // the second claim's bytes
    const char* first;const char* second; // the source files (their names, not paths)
};
using ConflictSink=void(*)(const ClaimConflict&) noexcept;

// Claims [at, at+size) for `owner` (a source file: compared by its name, so a path's spelling does not matter). False
// when another file holds a sole claim on one of the bytes (and this one is sole): a conflict, sent to the sink. The
// claim is kept either way (a later third claimant is told about both).
bool ClaimCode(const void* at,std::size_t size,Claim kind=Claim::kSole,const char* owner=__builtin_FILE()) noexcept;
// The sink for conflicts (null: none). Those found before are sent to it now, in order (the first kKeptConflicts).
constexpr unsigned kKeptConflicts=32;
void SetClaimConflictSink(ConflictSink sink) noexcept;
// The source file holding a sole claim on `at` (the first one's), or null.
const char* CodeOwner(const void* at) noexcept;
struct ClaimStats { unsigned claims,conflicts,dropped; };   // dropped: claims past the registry's room (not checked)
ClaimStats GetClaimStats() noexcept;
// Tests only: forgets every claim and conflict, the sink too.
void ResetClaims() noexcept;
// The file name of a path (__builtin_FILE() gives what the compiler was handed).
const char* SourceName(const char* path) noexcept;
}  // namespace edf
