// Native TimeShip capsule/hatch commands. Child objects retain their own native
// factory, parent links, falling states, passengers and lifetime after release.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_control {struct Ability;}
namespace crew::enemy_native {
bool InstallTimeShipPodControl() noexcept;
unsigned ListTimeShipPodAbilities(const NativeLease&,NativeAbility*,unsigned) noexcept;
bool ReadTimeShipPodAbilityState(const NativeLease&,const NativeAbility&,enemy_control::Ability*) noexcept;
bool ExecuteTimeShipPodAbility(const NativeLease&,const NativeAbility&,std::uint64_t sequence,std::uint64_t seed) noexcept;
bool RestoreTimeShipPodControl(const NativeLease&) noexcept;
}
