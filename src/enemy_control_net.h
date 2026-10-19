#pragma once
#include "command_net.h"
#include "enemy_control_protocol.h"
#include "enemy_control_types.h"
#include <cstddef>
#include <cstdint>

namespace crew {
// Startup binds the core's request/input/exit callbacks. It installs no native hook and never polls a second transport.
bool InstallEnemyControlNetwork() noexcept;
void UpdateEnemyControlNetwork(const CommandNetworkContext&,std::uint64_t now) noexcept;
bool ReceiveEnemyControlNetwork(std::uint32_t peer,const char* authenticatedPuid,
    const void* bytes,std::size_t size,std::uint64_t now) noexcept;
// Shared game-thread scene tick, after EnemyControlFrame captured local semantic input. Axes/held samples merge to
// 30 Hz; a skill press or channel/release transition sends immediately and is never coalesced into a later look sample.
void EnemyControlNetworkFrame() noexcept;
// Keeps unresolved native restore work, including across a transport/world reset.
void ResetEnemyControlNetwork() noexcept;
struct EnemyControlNetworkStatus {
    bool online=false,ready=false;
    enemy_control_protocol::Result result=enemy_control_protocol::Result::Unavailable;
    enemy_control_protocol::Phase phase=enemy_control_protocol::Phase::empty;
    std::uint64_t request=0,lease=0,profileHash=0;
    unsigned char source[32]{},target[32]{};
};
// Draw-thread safe, copied by source-player native identity. A source with no request still gets room readiness.
bool ReadEnemyControlNetworkStatus(const unsigned char source[32],EnemyControlNetworkStatus*) noexcept;
}
