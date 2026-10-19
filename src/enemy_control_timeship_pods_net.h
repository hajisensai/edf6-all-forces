// Host-accepted TimeShip capsule births and actual native throw destinations.
#pragma once
#include "enemy_control_native.h"
namespace crew::enemy_native {
bool RegisterTimeShipPodsNetwork() noexcept;
// Startup-install rollback only: refuses any bound root, accepted outbox or native scope.
bool RestoreTimeShipPodsNetworkHooks() noexcept;
bool TimeShipPodsNetworkAvailable(const NativeLease&,bool create) noexcept;
bool ExecuteTimeShipPodsNetwork(const NativeLease&,int argument,std::uint64_t sequence) noexcept;
bool ExecuteTimeShipPodsNetworkCreate(const NativeLease&,unsigned count,std::uint64_t sequence) noexcept;
bool ExecuteTimeShipPodsNetworkDrop(const NativeLease&,const float* point,float inner,float outer,int seed,std::uint64_t sequence) noexcept;
bool ExecuteTimeShipPodsNetworkDropOne(const NativeLease&,unsigned index,int delay,const float* point,std::uint64_t sequence) noexcept;
bool RestoreTimeShipPodsNetwork(const NativeLease&) noexcept;
}
