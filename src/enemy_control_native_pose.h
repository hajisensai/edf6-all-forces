// Typed host-pose fallback for native classes whose original network writer omits motion or heading.
#pragma once
#include "enemy_control_native.h"
#include "enemy_control_pose_codec.h"
#include <cstddef>

namespace crew::enemy_native {
bool PoseRequired(const NativeLease& lease) noexcept;
bool PoseReady(const NativeLease& lease) noexcept;
// The host's current state of the leased body (pose::Valid); the network layer stamps and encodes it.
bool CapturePose(const NativeLease& lease,pose::State* out) noexcept;
// One complete transactional write of a pose::Valid state of this lease's kind/profile. The guest calls it once per
// logic frame with the interpolated sample (enemy_control_pose_interp.h); PoseReady requires the previous write's
// journal to be cleared, so a failed write leaves the lease to the restore gate.
bool ApplyPose(NativeLease& lease,const pose::State& state) noexcept;
// Retry a failed physics/cache rollback before the owning native lease can be released.
bool RestorePose(NativeLease& lease) noexcept;
} // namespace crew::enemy_native
