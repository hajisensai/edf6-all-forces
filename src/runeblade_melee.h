// The RuneBlade's melee and special arms at run time (src/runeblade_melee.cpp; the pure part src/runeblade_melee_logic.h, the
// design and its gaps docs/newclass-re/runeblade-melee-plan.md): AssultSoldier's live frame slot chained (the new classes'
// clones copy it), the local RuneBlade player's arms stepped after its frame, their hits dealt on that machine and told to
// every peer through plugin_net channel "runeblade melee" (ZWRM, src/runeblade_melee_net.h), shown there with damage 0.
#pragma once
#include "plugin_net.h"

namespace crew {
bool InstallRuneBladeMelee() noexcept;   // module "runeblade melee" (src/module_table.h), kBeforeGameStatus after "vehicler foot"
plugin_net::Channel RuneBladeMeleeNetChannel() noexcept;   // support_net.cpp RegisterPluginChannels
}  // namespace crew
