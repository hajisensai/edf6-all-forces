// Reliable host-accepted Walking action and native emitter outcomes.
#pragma once
#include "enemy_control_native.h"
#include "enemy_control_action_types.h"
namespace crew::enemy_native {
bool InstallWalkingAcceptedActions() noexcept;
bool WalkingAcceptedCanQueue(const NativeLease&) noexcept;
// Parts capture yields until the native entry prefix has entered the reliable journal.
bool WalkingAcceptedEntryPending(const NativeLease&) noexcept;
bool BeginWalkingAcceptedAction(const NativeLease&,unsigned ordinal,std::uint64_t sequence,
    std::uint64_t pre3E8,std::uint64_t pre490) noexcept;
bool CancelWalkingAcceptedAction(const NativeLease&) noexcept;
bool QueueWalkingAcceptedAction(const NativeLease&,unsigned ordinal,std::uint64_t sequence,
    std::uint64_t pre3E8,std::uint64_t pre490) noexcept;
bool QueueWalkingAcceptedGun(const NativeLease&,unsigned ordinal,unsigned phase,
    std::uint64_t sequence,std::uint64_t seed,const float point[3]) noexcept;
bool WalkingAcceptedAimPoint(const void* root,float out[3]) noexcept;
bool CaptureWalkingAcceptedAction(const NativeLease&,void*,std::size_t,std::size_t*) noexcept;
bool OwnsWalkingAcceptedAction(const void*,std::size_t) noexcept;
enemy_control::ActionResult ApplyWalkingAcceptedAction(const NativeLease&,const void*,std::size_t) noexcept;
bool QuiesceWalkingAcceptedActions(const NativeLease&) noexcept;
bool WalkingAcceptedActionsEmpty(const NativeLease&) noexcept;
bool AbortWalkingAcceptedActions(const NativeLease&) noexcept;
bool RestoreWalkingAcceptedActions(const NativeLease&) noexcept;
}
