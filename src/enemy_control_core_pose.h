// Game-thread bridge to the active core lease; the native implementation retains physical ownership.
#pragma once
#include <cstddef>
#include <cstdint>
namespace crew {
bool EnemyControlNeedsNetworkPose(std::uint64_t lease) noexcept;
bool EnemyControlCaptureNetworkPose(std::uint64_t lease,void* bytes,std::size_t capacity,std::size_t* used) noexcept;
bool EnemyControlApplyNetworkPose(std::uint64_t lease,const void* bytes,std::size_t size) noexcept;
}
