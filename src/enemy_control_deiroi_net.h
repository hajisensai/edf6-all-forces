// Reliable host-accepted Deiroi child actions. No native pointers cross the wire.
#pragma once
#include "enemy_control_native.h"
#include "enemy_control_action_types.h"
namespace crew::enemy_native {
bool InstallDeiroiAcceptedActions() noexcept;
bool DeiroiAcceptedActionsReady() noexcept;
bool DeiroiAcceptedEntityFactoryScope(const void* nativeParameter) noexcept;
bool DeiroiAcceptedNativeInFlight(const NativeLease&) noexcept;
bool BindDeiroiAcceptedContext(const NativeLease&) noexcept;
bool CaptureDeiroiAcceptedAction(const NativeLease&,void*,std::size_t,std::size_t*) noexcept;
bool OwnsDeiroiAcceptedAction(const void*,std::size_t) noexcept;
enemy_control::ActionResult ApplyDeiroiAcceptedAction(const NativeLease&,const void*,std::size_t) noexcept;
bool QueueDeiroiAcceptedControl(const NativeLease&,unsigned key,unsigned phase,
    std::uint64_t sequence,std::uint64_t seed,const float point[3]) noexcept;
bool QuiesceDeiroiAcceptedActions(const NativeLease&) noexcept;
bool DeiroiAcceptedQuiescing(const void* root) noexcept;
bool DeiroiAcceptedActionsEmpty(const NativeLease&) noexcept;
// Only a trusted local epoch/link reset may discard an old outbox.
bool AbortDeiroiAcceptedActions(const NativeLease&) noexcept;
bool RestoreDeiroiAcceptedActions(const NativeLease&) noexcept;
}
