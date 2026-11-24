// The ultimate strike's plugin_net.h channel (ultimate.cpp; the wire ultimate_net_protocol.h): ULTS, sent to every peer.
// support_net.cpp registers it.
#pragma once
#include "plugin_net_logic.h"

namespace crew {
plugin_net::Channel UltimateNetChannel() noexcept;
}  // namespace crew
