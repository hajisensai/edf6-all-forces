// TimeShip commands use the original complete AS/native action lifecycle.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_control {struct Ability;}
namespace crew::enemy_native {
enum class TimeShipCommand : std::int32_t {
    warp=0,warp2=1,anchor=2,vanish=3,vanish2=4,
    forceHatchOpen=7,forceHatchClose=8,createPods=10,dropAllPods=11
};
bool InstallTimeShipControl() noexcept;
unsigned ListTimeShipAbilities(const NativeLease&,NativeAbility*,unsigned capacity) noexcept;
bool ReadTimeShipAbilityState(const NativeLease&,const NativeAbility&,enemy_control::Ability*) noexcept;
bool ExecuteTimeShipAbility(const NativeLease&,const NativeAbility&,std::uint64_t sequence,std::uint64_t seed) noexcept;
// Called while the original weak identity is still retained. A failed child
// restoration keeps this binding, so a retry never replays an accepted command.
bool RestoreTimeShipControl(const NativeLease&) noexcept;
}
