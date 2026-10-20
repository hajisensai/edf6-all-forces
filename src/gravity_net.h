// The gravity operations online (gravity_net.cpp; the wire and why in gravity_net_protocol.h): what this machine's player
// holds and throws, every other machine's copy of it held and thrown the same way.
// A carrier (the gravity gun) does its own moves through gravity.h as offline and tells this layer each frame:
//  - Hold every frame it carries `t` to `goal` (moving at `goalVel`): the grab's state goes to every peer (kHoldGapMs);
//  - Thrown with the velocity Throw gave it, or LetGo: once, the grab ended.
// A carrier that stops calling Hold without either (put away, the mission over) lets go here after a few frames.
// Each peer's grab is carried on this machine's copy by Frame (gravity::HoldAt), and thrown with the same velocity; its
// damage is dealt on the grabber's machine only (its own Track), never here. Offline every call is nothing.
// Game thread only.
#pragma once
#include "command_net.h"
#include "plugin_net_logic.h"
#include "gravity.h"
#include <cstdint>

namespace crew::gravity::net {
// `hit` where the grab's ray `a`->`b` met it (its first frame's; kept for the grab's later frames).
void Hold(const Target& t,const unsigned char* by,const float* hit,const float* a,const float* b,const float* goal,
          const float* goalVel) noexcept;
void Thrown(const Target& t,const unsigned char* by,const float* vel) noexcept;
void LetGo(const Target& t,const unsigned char* by) noexcept;
void Frame() noexcept;   // the shared tick, after gravity::Frame: the peers' grabs carried here
void Reset() noexcept;   // a new mission
}  // namespace crew::gravity::net

namespace crew {
// support_net.cpp's transport (CommandContext and its one poll).
void UpdateGravityNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept;
bool ReceiveGravityNetwork(std::uint32_t peer,const char* authenticatedPuid,const void* bytes,std::size_t size,
                           std::uint64_t now) noexcept;
void ResetGravityNetwork() noexcept;
// Its plugin_net.h channel (support_net.cpp registers it): GRVT, support_protocol.h kExtGravity.
plugin_net::Channel GravityNetChannel() noexcept;
}  // namespace crew
