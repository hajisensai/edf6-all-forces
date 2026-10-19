// Manual Deiroi foot batteries retain their native object, weapon and state lifecycle.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_control {struct Ability;}
namespace crew::enemy_native {
bool InstallDeiroiControl() noexcept;
// Current private aim relays AND accepted main/foot factory-provider ownership.
// Freshness/canonical-point checks remain per accepted attack.
bool DeiroiWeaponAimReady() noexcept;
unsigned DeiroiGunCount(const NativeLease&) noexcept;
bool ReadDeiroiGunCount(const NativeLease&,unsigned* count) noexcept;
bool DeiroiGunOrdinal(const NativeLease&,unsigned visibleIndex,unsigned* ordinal) noexcept;
bool UpdateDeiroiAim(const NativeLease&,float yaw,float elevation) noexcept;
bool ExecuteDeiroiAbility(const NativeLease&,unsigned ordinal,bool automatic,
                          std::uint64_t sequence,std::uint64_t seed) noexcept;
bool ReadDeiroiAbilityState(const NativeLease&,unsigned ordinal,bool automatic,enemy_control::Ability*) noexcept;
// Trusted local bridges for the accepted-action codec; no pointers are serialized.
bool ResolveDeiroiControlledGun(const void* child,NativeLease* root,unsigned* ordinal) noexcept;
bool ResolveDeiroiControlledGunKey(const NativeLease&,unsigned ordinal,void** child) noexcept;
bool ResolveDeiroiRetainedGunKey(const NativeLease&,unsigned ordinal,void** child) noexcept;
// Main gun rows are plain root-owned storage, not independent SceneObjects.
// Group 32 is reserved for their stable native row ordinal.
bool ReadDeiroiMainGunCount(const NativeLease&,unsigned* count) noexcept;
bool ResolveDeiroiControlledHolder(const void* holder,NativeLease* root,unsigned* ordinal) noexcept;
bool ResolveDeiroiControlledHolderKey(const NativeLease&,unsigned ordinal,void** actor,void** holder) noexcept;
// Read-only source mode; automatic batteries keep their native target selection.
bool DeiroiGunManual(const NativeLease&,unsigned ordinal) noexcept;
bool DeiroiExplicitTransition(const void* child) noexcept;
// Retained source native setter callbacks cannot retire their accepted stream.
bool DeiroiNativeTransitionActive(const NativeLease&) noexcept;
bool ApplyDeiroiAcceptedControl(const NativeLease&,unsigned ordinal,unsigned phase,std::uint64_t acceptedSequence,
                                std::uint64_t seed,const float point[3]) noexcept;
bool RestoreDeiroiControl(const NativeLease&) noexcept;
}
