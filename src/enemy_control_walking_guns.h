// Independent native Large gun_control commands. The original ordinal, battery,
// charge/fire lifecycle, projectiles and cleanup remain the engine's.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_control { struct Ability; }
namespace crew::enemy_native {
bool InstallWalkingGunControl() noexcept;
unsigned ListWalkingGunAbilities(const NativeLease&,NativeAbility*,unsigned capacity) noexcept;
bool ExecuteWalkingGun(const NativeLease&,unsigned ordinal,std::uint64_t sequence,std::uint64_t seed) noexcept;
bool ReadWalkingGunState(const NativeLease&,unsigned ordinal,enemy_control::Ability*) noexcept;
// Called before the root lease is forgotten. Failure retains every pending weak
// identity and owned native burst so the caller can retry its restore gate.
bool RestoreWalkingGunControl(const NativeLease&) noexcept;
// Typed LargePoint bridge. Only an owned individual gun has a cached point;
// unowned native/all-gun paths retain their existing canonical aim routing.
bool WalkingGunAimPoint(const void* gunControl,float out[3]) noexcept;
// Trusted game-thread accepted-outcome bridges. Wire codecs carry only ordinal,
// definition digest, semantic input and actual native outcomes; never pointers.
bool ResolveWalkingControlledGun(const void* gunControl,NativeLease*,unsigned* ordinal) noexcept;
bool ResolveWalkingGunKey(const NativeLease&,unsigned ordinal,void** gunControl) noexcept;
bool WalkingGunReady(const NativeLease&,unsigned ordinal) noexcept;
bool WalkingGunCommitted(const NativeLease&,unsigned ordinal) noexcept;
bool WalkingGunDefinition(const NativeLease&,unsigned ordinal,std::uint64_t* digest) noexcept;
// Baseline preparation uses the complete native roster, including non-selectable
// Hector shield placeholders. Returned pointers are borrowed for immediate use.
bool WalkingGunPreparationCount(const NativeLease&,unsigned* count) noexcept;
bool ResolveWalkingGunPreparationKey(const NativeLease&,unsigned ordinal,void** gunControl,std::uint64_t* digest) noexcept;
// Shared GunBattery2 emitters: Large uses its original gun ordinal;
// Middle/Shooter have a single root gun_battery_settings at key zero.
bool WalkingBatteryDefinition(const NativeLease&,unsigned key,std::uint64_t* digest) noexcept;
bool ApplyWalkingGunAcceptedControl(const NativeLease&,unsigned ordinal,unsigned phase,
    std::uint64_t acceptedSerial,std::uint64_t commandSequence,std::uint64_t seed,const float point[3]) noexcept;
}
