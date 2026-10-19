#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_native {
// Register before listing abilities so online leases bind their accepted role.
bool InstallCommandAcceptedActions() noexcept;
// The catalogue calls this after exact definition validation, before its native
// command branches. Unrelated and verified offline commands set handled=false.
// Online guests consume intent; only a verified host acceptance creates an event.
bool ExecuteAcceptedCommand(const NativeLease&,const NativeAbility&,std::uint64_t sequence,
                            std::uint64_t seed,bool* handled) noexcept;
}
