#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_native {
bool InstallTimeShipPodScriptGates() noexcept;
bool RestoreTimeShipPodScriptGates() noexcept;
// Local-only AS adapters. A valid active host role is required; no guessed Object wrapper is serialized.
bool TimeShipPodScriptLease(void*,NativeLease*) noexcept;
std::uint64_t TimeShipPodScriptSequence(const NativeLease&) noexcept;
bool AcceptTimeShipScriptHatch(const NativeLease&,bool) noexcept;
void ObserveTimeShipVanillaHatch(void*,bool) noexcept;
}
