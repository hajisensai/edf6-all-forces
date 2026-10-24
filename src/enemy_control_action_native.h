// Host-accepted main action states for families without a stock root action receiver.
#pragma once
#include "enemy_control_native.h"
#include "enemy_control_action_types.h"
namespace crew::enemy_native {
// Trusted game-thread adapters register typed outcome codecs. No callbacks or
// native addresses are serialized. Registration is idempotent by a local tag.
struct NativeActionExtension {
    std::uint32_t tag=0;
    bool (*Needs)(const NativeLease&) noexcept=nullptr;
    bool (*Before)(const NativeLease&,bool authority,Input*) noexcept=nullptr;
    bool (*After)(const NativeLease&) noexcept=nullptr;
    bool (*Capture)(const NativeLease&,void*,std::size_t,std::size_t*) noexcept=nullptr;
    bool (*Owns)(const void*,std::size_t) noexcept=nullptr;
    enemy_control::ActionResult (*Apply)(const NativeLease&,const void*,std::size_t) noexcept=nullptr;
    bool (*Quiesce)(const NativeLease&) noexcept=nullptr;
    bool (*Empty)(const NativeLease&) noexcept=nullptr;
    bool (*Abort)(const NativeLease&) noexcept=nullptr;
    bool (*Restore)(const NativeLease&) noexcept=nullptr;
};
bool RegisterNativeActionExtension(const NativeActionExtension&) noexcept;
bool NeedsNativeActions(const NativeLease&) noexcept;
// Returns the active root action role for child adapters; never adopts another weak identity.
bool NativeActionRole(const void* root,bool* authority) noexcept;
bool NativeActionQuiescing(const void* root) noexcept;
// Must bracket the admitted native player-input call on every peer. Authority is
// the host, independent of the peer which owns the player input lease.
bool BeforeNativeActionInput(const NativeLease&,bool authority,Input*) noexcept;
bool AfterNativeActionInput(const NativeLease&) noexcept;
// true + used==0 means no accepted event. A nonempty batch is removed exactly once.
bool CaptureNativeAction(const NativeLease&,void*,std::size_t,std::size_t*) noexcept;
// Only applied may ACK. Pending preflight has made no native changes.
enemy_control::ActionResult ApplyNativeAction(const NativeLease&,const void*,std::size_t) noexcept;
// Normal close: stop future triggers/samples, drain every already accepted event,
// then Restore. Abort is only for a trusted local old-epoch/link loss, never an
// ordinary remote Fault. Failed native restoration retains its role/journal.
bool QuiesceNativeActions(const NativeLease&) noexcept;
bool NativeActionsEmpty(const NativeLease&) noexcept;
bool AbortNativeActions(const NativeLease&) noexcept;
bool RestoreNativeActions(const NativeLease&) noexcept;
}
