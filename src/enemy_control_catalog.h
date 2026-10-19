// Names and commands for the admitted native enemy controller, derived from the
// live class/equipment. Command entries use the same native action lifecycle as
// the object's original ai_script; they never play a naked CAS state.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_control { struct Ability; }
namespace crew::enemy_native {
// Total count; at most capacity entries are copied. nullptr queries only count.
unsigned Abilities(const NativeLease&,NativeAbility* out,unsigned capacity) noexcept;
// Called once on an admitted, deduplicated press by the owner runtime. Pad
// entries belong to RunInput; this executes only catalogue command entries.
bool ReadAbilityState(const NativeLease&,const NativeAbility&,enemy_control::Ability*) noexcept;
bool ExecuteAbility(const NativeLease&,const NativeAbility&,std::uint64_t sequence,std::uint64_t seed) noexcept;
}
