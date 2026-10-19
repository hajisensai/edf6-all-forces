#pragma once
#include <cstdint>
namespace crew {
struct ObjRef;
// Canonical registered source identity for dynamic support. No team/slot/actor address is authority.
// A 64-bit descriptor is accepted only when the complete current all-team walk resolves one native identity.
std::uint64_t SupportSourceKey(const ObjRef&) noexcept;
bool ResolveSupportSource(std::uint64_t,ObjRef* out) noexcept;
}
