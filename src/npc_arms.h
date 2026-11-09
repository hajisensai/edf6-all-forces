// The requesting player's side of a support soldier's weapon (npc_tier.h; docs/feature-npc-weapon-tier.md): what this
// machine's player would give each soldier kind it calls, and the loads of the weapons a plan's soldiers carry.
#pragma once
#include "support_net.h"
#include <cstdint>

namespace crew {
// This machine's player (GameStatus slot 0, its current class: docs/loadout-re.md) as the requester: for each soldier kind
// the weapon one tier below its own (npc_tier.h PickArms), packed (npctier::PackArms). All zero when the soldiers' weapon
// hooks are not in (support_soldier.cpp: the profile did not match, logged) or the game status is not readable: the
// soldiers then come as the stock templates, under the stock rule.
void LocalRequesterArms(support_net::RequesterArms* out) noexcept;
// Mission start (after PreloadSupportSoldiers, before the game's WaitPreload): this machine's player's weapons for every
// kind queued with the mission's own preload, so its own calls need no load.
void PreloadNpcArms() noexcept;
// Whether every weapon `plan`'s soldiers carry is loaded here: ready; pending while the loads this asked for (the
// resource manager's 0x7A3780, as debug_spawn.cpp's on-demand rows) are going on, until its completion query (0x7A3600)
// reports them done; refused when a unit's arms are not its kind's or the loads cannot be asked for.
support_net::PlanResult NpcArmsLoaded(const support_net::Plan& plan) noexcept;
}  // namespace crew
