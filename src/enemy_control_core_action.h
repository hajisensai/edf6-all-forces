// Game-thread bridge for native host-accepted actions. Payloads are typed semantic records, never native memory.
#pragma once
#include "enemy_control_action_types.h"
#include <cstddef>
#include <cstdint>
namespace crew {
bool EnemyControlQuiesceNetworkActions(std::uint64_t lease) noexcept;
bool EnemyControlNetworkActionsEmpty(std::uint64_t lease) noexcept;
bool EnemyControlAbortNetworkActions(std::uint64_t lease) noexcept;
bool EnemyControlNeedsHostActions(std::uint64_t lease) noexcept;
// Capture a complete batch of accepted native steps (up to capacity) when the bounded transport window has space.
// Successful empty capture means no new accepted event; nonempty batch is dequeued once. Remaining steps stay queued.
bool EnemyControlCaptureNetworkAction(std::uint64_t lease,void* bytes,std::size_t capacity,std::size_t* used) noexcept;
enemy_control::ActionResult EnemyControlApplyNetworkAction(std::uint64_t lease,std::uint64_t actionSequence,const void* bytes,std::size_t size) noexcept;
}
