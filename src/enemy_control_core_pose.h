// Game-thread bridge to the active core lease; the native implementation retains physical ownership.
#pragma once
#include <cstddef>
#include <cstdint>
#include "enemy_control_pose_codec.h"
namespace crew {
bool EnemyControlNeedsNetworkPose(std::uint64_t lease) noexcept;
bool EnemyControlCaptureNetworkPose(std::uint64_t lease,enemy_native::pose::State* out) noexcept;
bool EnemyControlApplyNetworkPose(std::uint64_t lease,const enemy_native::pose::State& state) noexcept;
}
