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
    std::uint32_t revision=0;
    std::uint64_t epoch=0;
    char localPuid[65]{};
    pvp_protocol::Result result=pvp_protocol::Result::NotReady;
    wchar_t note[192]{};
};
struct PvpMember { char puid[65]{}; int team=2; bool local=false; };
// UI requests are queued to the game thread. true means queued; pending/result
// describe the host transaction and Complete requires every peer's native ACK.
// Only the host changes the mode; each authenticated member changes their team.
bool SetPvpEnabled(bool enabled) noexcept;
bool SetPvpLocalTeam(int team) noexcept;
// Menus retain these identity fields with each displayed action. Consumption
// rechecks all three so a stale click cannot target a replacement world/revision.
bool SetPvpEnabledAt(bool enabled,std::uint64_t epoch,std::uint32_t revision,const char* localPuid) noexcept;
bool SetPvpLocalTeamAt(int team,std::uint64_t epoch,std::uint32_t revision,const char* localPuid) noexcept;
bool ReadPvpStatus(PvpStatus* out) noexcept;
unsigned ReadPvpMembers(PvpMember* out,unsigned capacity) noexcept;
// Once per game frame, on the game thread only (native/protocol mutations).
void PvpFrame() noexcept;
// HQ / room UI thread: capability advertisement only; no protocol/native access.
void PvpRoomTick() noexcept;
// Game thread only. 0: stable mode-off, 1/2: committed owner team, -1:
// transition/restoration/unready/unknown owner; callers must never map -1 to 2.
int PvpSupportTeam(const char* puid) noexcept;
bool PvpValidateSupportPlan(const support_net::Plan& plan) noexcept;
bool PvpAttachSupportPlan(const support_net::Plan& plan,const ObjRef* objects,unsigned count) noexcept;
void PvpForgetSupportObject(const ObjRef& object) noexcept;
// Before scene release / on plugin disable. Restores native state first.
void ResetPvp() noexcept;
// Single consumer: support_net supplies the authenticated context and demux.
void UpdatePvpNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept;
bool ReceivePvpNetwork(std::uint32_t peer,const char* authenticatedPuid,
    const void* bytes,std::size_t size,std::uint64_t now) noexcept;
void ResetPvpNetwork() noexcept;
}
