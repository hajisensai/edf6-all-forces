// Exact native WalkingEnemy AttackSetting selection; no CAS-only action plays.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_control {struct Ability;}
namespace crew::enemy_native {
bool InstallWalkingControl() noexcept;
// Complete validated count may be zero when inherited template rows have no real CAS state.
bool ReadWalkingActionCount(const NativeLease&,unsigned* count) noexcept;
bool WalkingNativeMainAttackAvailable(const NativeLease&) noexcept;
unsigned ListWalkingActionAbilities(const NativeLease&,NativeAbility*,unsigned capacity) noexcept;
bool ExecuteWalkingAction(const NativeLease&,unsigned ordinal,std::uint64_t sequence,std::uint64_t seed) noexcept;
bool ReadWalkingActionState(const NativeLease&,unsigned ordinal,enemy_control::Ability*) noexcept;
bool RestoreWalkingControl(const NativeLease&) noexcept;
// Accepted-outcome codec boundary. These never send the old whole-action tag10.
bool WalkingActionDefinition(const NativeLease&,unsigned ordinal,std::uint64_t* digest) noexcept;
bool WalkingActionReady(const NativeLease&,unsigned ordinal) noexcept;
bool WalkingActionEntered(const NativeLease&,unsigned ordinal) noexcept;
bool WalkingActionCommitted(const NativeLease&,unsigned ordinal) noexcept;
bool CommitWalkingAction(const NativeLease&,unsigned ordinal,std::uint64_t sequence,
                         std::uint64_t pre3E8,std::uint64_t pre490) noexcept;
bool CommitWalkingAction(const NativeLease&,unsigned ordinal,std::uint64_t sequence,
                         std::uint64_t pre3E8,std::uint64_t pre490,std::uint8_t prePartOrdinal) noexcept;
}
