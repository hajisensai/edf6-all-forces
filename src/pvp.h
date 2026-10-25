#pragma once
#include "command_net.h"
#include "pvp_protocol.h"
#include <cstddef>
#include <cstdint>

namespace crew {
struct ObjRef;
namespace support_net { struct Plan; }
// Plugin initialization only, before game/UI updates; installs guarded pass-through hooks.
bool InstallPvp() noexcept;
struct PvpStatus {
    bool ready=false,host=false,enabled=false,pending=false;
    int localTeam=2;
    pvp_protocol::Entry local{};   // this member's faction, role and unit (its team is localTeam)
    std::uint8_t preset=0;         // pvp_match::Preset of the match's relations
    std::uint32_t revision=0;
    std::uint64_t epoch=0;
    char localPuid[65]{};
    pvp_protocol::Result result=pvp_protocol::Result::NotReady;
    wchar_t note[192]{};
};
struct PvpMember { char puid[65]{}; int team=2; bool local=false; crew::pvp_match::Pick pick{}; };
// UI requests are queued to the game thread. true means queued; pending/result
// describe the host transaction and Complete requires every peer's native ACK.
// Only the host changes the mode and the relations; each authenticated member changes their own pick.
bool SetPvpEnabled(bool enabled) noexcept;
bool SetPvpLocalPick(const pvp_match::Pick& pick) noexcept;
// Menus retain these identity fields with each displayed action. Consumption
// rechecks all three so a stale click cannot target a replacement world/revision.
bool SetPvpEnabledAt(bool enabled,std::uint64_t epoch,std::uint32_t revision,const char* localPuid) noexcept;
bool SetPvpLocalPickAt(const pvp_match::Pick& pick,std::uint64_t epoch,std::uint32_t revision,const char* localPuid) noexcept;
bool SetPvpPresetAt(int preset,std::uint64_t epoch,std::uint32_t revision,const char* localPuid) noexcept;
// Host, game thread: the room's locked match (room_pvp.h) as one transaction once the mission's protocol is ready.
bool QueuePvpMatch(const pvp_protocol::State& match) noexcept;
// The pick a click on `faction` makes from the member's current one: its role kept when the faction allows it, else
// the faction's default; a unit kept when it is of the faction's side, else the side's default (pvp_units.h).
pvp_match::Pick PvpPickForFaction(int faction,const pvp_protocol::Entry& current) noexcept;
bool ReadPvpStatus(PvpStatus* out) noexcept;
unsigned ReadPvpMembers(PvpMember* out,unsigned capacity) noexcept;
// Once per game frame, on the game thread only (native/protocol mutations).
void PvpFrame() noexcept;
// HQ / room UI thread: capability advertisement only; no protocol/native access.
void PvpRoomTick() noexcept;
// Game thread only. 0: stable mode-off, a faction team (pvp_match.h PvpTeam): committed owner team, -1:
// transition/restoration/unready/unknown owner; callers must never map -1 to 2.
int PvpSupportTeam(const char* puid) noexcept;
// Game thread: the committed pick of member `puid` (false: no committed enabled match, or not a member).
bool PvpMemberPick(const char* puid,pvp_match::Pick* out) noexcept;
bool PvpValidateSupportPlan(const support_net::Plan& plan) noexcept;
bool PvpAttachSupportPlan(const support_net::Plan& plan,const ObjRef* objects,unsigned count) noexcept;
// Re-authenticate a canonical requester against this world and overwrite its current stable team.
bool PvpRefreshSupportOwner(support_net::Plan* plan) noexcept;
// Dynamic child planning: retain an existing canonical requester only when it owns this exact parent.
bool PvpMakeInheritedSupportPlan(const ObjRef& parent,support_net::Plan* plan) noexcept;
// Child support inherits authenticated PUID ownership, never a parent's mutable native team.
// A missing owner preserves legacy behavior only in stable mode-off.
bool PvpInheritSupport(const ObjRef& parent,const ObjRef* children,unsigned count) noexcept;
void PvpForgetSupportObject(const ObjRef& object) noexcept;
// Before scene release / on plugin disable. Restores native state first.
void ResetPvp() noexcept;
// Single consumer: support_net supplies the authenticated context and demux.
void UpdatePvpNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept;
bool ReceivePvpNetwork(std::uint32_t peer,const char* authenticatedPuid,
    const void* bytes,std::size_t size,std::uint64_t now) noexcept;
void ResetPvpNetwork() noexcept;
}
