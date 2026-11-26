// A registered jet's engine lever online (jet_lever_net.cpp; the wire and why: jet_lever_protocol.h): AFJL, a state
// message to every peer (the room protocol keeps other builds out). support_net.cpp registers the channel. Game thread.
#pragma once
#include "plugin_net_logic.h"

namespace crew {
plugin_net::Channel JetLeverNetChannel() noexcept;
// Each plugin jet's frame (crew.cpp, after its engine sound): where this machine runs a registered one, its lever sent
// (jetsound::LocalLever); where another does, its exhaust burnt with the lever heard of it (booster.cpp JetFlames).
void JetLeverFrame(unsigned char* vehicle) noexcept;
}  // namespace crew
