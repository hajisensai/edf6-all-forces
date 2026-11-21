// The requesting player's side of a support soldier's weapon (npc_tier.h; docs/feature-npc-weapon-tier.md): what this
// machine's player would give each soldier kind it calls, and the loads of the weapons a plan's soldiers carry.
#pragma once
#include "support_net.h"
#include "support_call.h"
#include <cstdint>

namespace crew {
// This machine's player (GameStatus slot 0, its current class: docs/loadout-re.md) as the requester: for each soldier kind
// the weapon one tier below its own (npc_tier.h PickArms), packed (npctier::PackArms). All zero when the soldiers' weapon
// hooks are not in (support_soldier.cpp: the profile did not match, logged), the resource preload is not as read (logged
// at the mission start) or the game status is not readable: the soldiers then come as the stock templates, under the
// stock rule. The requester: whoever calls (support_dispatch.cpp, support_net.cpp), for a sea rescue the swimmer, for a
// mission vehicle's crew the machine that builds its plan (offline this one, online the host: mission_crew_support.cpp).
void LocalRequesterArms(support_net::RequesterArms* out) noexcept;
// The commander mode (commander.cpp): this machine's player's class (docs/loadout-re.md: 0 Ranger, 1 Wing Diver, 2 Air
// Raider, 3 Fencer) and its class's equipped weapon ids (-1 empty; `slots` of them). False: GameStatus not readable.
bool LocalPlayerLoadout(int* cls,int* equipped,int slots) noexcept;
// A commander's NPC request: kind `kind` carries this player's own weapon of that kind's series (npc_tier.h PickBase: the
// equipped one, else the best owned; no tier down), every other kind none. All zero as LocalRequesterArms' are.
void CommanderRequesterArms(int kind,support_net::RequesterArms* out) noexcept;
// Each soldier of `plan` given this machine's player's weapons of its kind (LocalRequesterArms: its main hand and, a
// Fencer kind's, its second hand; a unit not a soldier: 0): the plans this machine builds as their requester (an offline
// call or rescue, a host's mission crew). A Fencer the planner already gave both hands (a preset's #R:L,
// PresetHandArms) keeps them, as the host's Session keeps them (support_protocol.cpp Advance).
inline void GiveLocalArms(support_net::Plan* plan) noexcept {
    if(!plan)return;
    support_net::RequesterArms arms;
    LocalRequesterArms(&arms);
    for(std::uint32_t i=0;i<plan->count && i<support_net::kMaxUnits;++i) {
        auto& unit=plan->units[i];
        const bool soldier=IsSupportSoldierResource(unit.resourceId);
        if(soldier && unit.arms && unit.offhand)continue;
        const int k=static_cast<int>(SupportSoldierWeapon(unit.resourceId));
        unit.arms=soldier ? arms.kind[k] : 0;
        unit.offhand=soldier ? arms.offhand[k] : 0;
    }
}
// A preset's two Fencer hands (support_loadout.h FencerHands: weapon ids) as the words of a unit (npc_tier.h PresetHand:
// this machine's player's stars, each one less): the planner (offline, or the host) gives them to that soldier. False
// (both 0) when the soldiers' weapon hooks or the preload are not in (as LocalRequesterArms: the soldier is then the
// stock template), the game status is not readable or an id is no Fencer hand weapon.
bool PresetHandArms(int right,int left,std::uint64_t* arms,std::uint64_t* offhand) noexcept;
// Mission start (after PreloadSupportSoldiers, before the game's WaitPreload): this machine's player's weapons for every
// kind queued with the mission's own preload, so its own calls need no load.
void PreloadNpcArms() noexcept;
// Whether every weapon `plan`'s soldiers carry is loaded here: ready; pending while the loads this asked for (the
// resource manager's 0x7A3780, as debug_spawn.cpp's on-demand rows) are going on, until its completion query (0x7A3600)
// reports them done; refused when a unit's arms are not its kind's or the loads cannot be asked for.
support_net::PlanResult NpcArmsLoaded(const support_net::Plan& plan) noexcept;
}  // namespace crew
