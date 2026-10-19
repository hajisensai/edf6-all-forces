// Per-weapon selection filters the original Humanoid fire dispatch only inside
// this controlled body's native input call. Equipment vectors/network indices,
// aim, native weapon state machines and passive shields remain native.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_control { struct Ability; }
namespace crew::enemy_native {
bool InstallHumanoidControl() noexcept;
unsigned HumanoidWeaponCount(const NativeLease&) noexcept;
// Validation failure is distinct from a confirmed zero live guns.
bool ReadHumanoidWeaponCount(const NativeLease&,unsigned* count) noexcept;
bool HumanoidWeaponIndex(const NativeLease&,unsigned visibleIndex,unsigned* nativeIndex) noexcept;
bool ReadHumanoidWeaponState(const NativeLease&,unsigned nativeIndex,enemy_control::Ability*) noexcept;
bool RunHumanoidInput(NativeLease&,const Input&,InputFn original,void* dt,
                      std::uint64_t weaponMask,std::uint64_t* result=nullptr) noexcept;
}
