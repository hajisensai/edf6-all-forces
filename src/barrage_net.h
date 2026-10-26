// The long-range barrages' plugin_net.h channel (barrage.cpp; the wire barrage_net_protocol.h): BRGS, sent only to the
// peers that announced support_protocol.h kExtBarrage. support_net.cpp registers it.
#pragma once
#include "plugin_net_logic.h"

namespace crew {
plugin_net::Channel BarrageNetChannel() noexcept;
}  // namespace crew
