// Typed host-pose fallback for native classes whose original network writer omits motion or heading.
#pragma once
#include "enemy_control_native.h"
#include <cstddef>

namespace crew::enemy_native {
bool PoseRequired(const NativeLease& lease) noexcept;
bool PoseReady(const NativeLease& lease) noexcept;
bool CapturePose(const NativeLease& lease,void* out,std::size_t capacity,std::size_t* used) noexcept;
bool ApplyPose(NativeLease& lease,const void* bytes,std::size_t size) noexcept;
// Retry a failed physics/cache rollback before the owning native lease can be released.
bool RestorePose(NativeLease& lease) noexcept;
} // namespace crew::enemy_native
