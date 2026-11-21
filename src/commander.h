// The commander mode (ini CommanderMode, online the host's: host_settings.h CommanderModeInEffect; the player, 2026-11-21:
// "加一个指挥官模式，没有玩家角色，只有m视角。初始玩家生成换成玩家对应的npc角色。npc全死亡失败";
// docs/feature-2026-11-21-commander-mode.md). Every player of the mission is a commander:
//  - its own soldier stays where the mission put it, a command post: NoTarget (+0x380 0x80: the hostile walk 0x5E0F20 and
//    the locks skip it, as a PvP commander's) and NoHpLoss (+0x380 bit 0, enemy_control.cpp ProtectSource's), set on every
//    machine for every player, given back when the mode ends;
//  - the M map opens by itself and its keys do not shut it (map.h MapSetCommanderMode): the player's input is the map's;
//  - an NPC of the player's class carrying its own equipped weapon (commander_logic.h KindFor, npc_tier.h PickBase) is
//    made at its soldier through the support transaction (catalog commander::kCatalog: the requester asks, the host plans
//    it at the requester's authenticated player, every peer makes the same soldier; offline this machine), and follows
//    that player: the map's squad of that player, commanded like any;
//  - once this player's own NPC has been alive, no live friendly NPC soldier for commander_logic.h kLostMs is the
//    mission lost: every machine counts the same replicated soldiers and gives its own player the stock death, so the
//    stock "every player down" ends the mission (KillLocalPlayer).
#pragma once
#include "support_net.h"
#include <cstdint>

namespace crew {
// support_dispatch.cpp's hooks for catalog commander::kCatalog.
// Host (or offline): the plan of `peer`'s request (its authenticated player; `loadout` the soldier kind it asked for).
support_net::PlanResult PlanCommander(std::uint32_t peer,const float* target,std::uint64_t loadout,SupportPlan* out) noexcept;
// Every peer, before anything is made: one soldier of a player class's kind, at its target, the mode on here.
bool ValidateCommanderPlan(const SupportPlan& plan) noexcept;
// Every peer: the soldier made (held until the transaction is active), recorded for the follow, the count and rollback.
bool ApplyCommanderPlan(std::uint64_t transaction,const SupportPlan& plan,bool remote) noexcept;
void DestroyCommanderPlan(std::uint64_t transaction) noexcept;
// The request notices (support_net Hooks::notice): true for this machine's commander request (consumed here).
bool CommanderRequestNotice(std::uint32_t request,support_net::RequestStatus status) noexcept;
// map.cpp MapHumanFrame, the local player's pre-update, once a frame: the mode's whole per-frame work.
void CommanderFrame(unsigned char* human) noexcept;
// mission.cpp MissionStart: the last mission's state dropped (its objects are the teardown's), the map's lock let go.
void ResetCommander() noexcept;
}  // namespace crew
