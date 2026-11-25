// Room-stage pre-sync over EDF6Coop's member attributes (EDF6CoopGetMemberAttributeApi v1, edf-coop-stable
// edf-coop-stable:docs/member-attribute-api.md): every member announces its own support loadout (member_config.h), which applies to the
// supports that member calls; every member generates the variant files of every member's loadout it lacks before the
// mission starts, and the host plans a member's request with that member's loadout (RoomMemberConfig).
// Contract between the menu screen (src/room_ui.cpp, stage 4) and the sync (src/room_sync.cpp, stage 6).
// Every call comes from the game's UI thread (the HUiRoom OnUpdate wrapper; offline HQ never calls it).
#pragma once
#include "support_loadout.h"
#include <cstddef>
#include <cstdint>

namespace crew { struct SupportConfig; }   // support_config.h (not included: the NPC fixtures #define `vehicle`)

namespace crew::roomsync {

enum class SyncState { unavailable, alone, waiting, ready };
// One line the menu shows (UTF-16, already localised), e.g. "全员已就绪" / "某某缺 2 个文件，生成中".
struct SyncLine { wchar_t text[128]; bool warn; };

// Once per room frame.
void TickRoomSync() noexcept;
// The room is gone (left / mission started): stop publishing.
void LeaveRoomSync() noexcept;
SyncState RoomSyncState() noexcept;
// The host's mission start (room_pvp.cpp StartGate, the room's frame): false while the room's files are not ready
// (room_sync_logic.h HoldOf: on their way, or a generation failed); the hold logged once each change.
bool RoomSyncAllowsStart() noexcept;
// A start held in the last moments: its line (warn), for the 整备 status strip. false when none.
bool RoomSyncStartHeldLine(wchar_t* out,std::size_t n) noexcept;
// The next TickRoomSync re-reads the room at once (a preset switched in, the loadout republished).
void RoomSyncNudge() noexcept;
// Up to `capacity` lines for the menu's status area; returns how many.
int RoomSyncLines(SyncLine* out,int capacity) noexcept;
// The pre-sync turned off (RoomPreSync / RoomLoadout / Enabled): the room's set and the members' loadouts kept for the
// next mission dropped too.
void ForgetRoomList() noexcept;
// Any thread (a mission's start): each file of the room's set the last room completed (every member's loadout's, this
// machine's own included), the names support_loadout.h writes. Kept after the room is left (a mission starting leaves
// it): what that mission's plans can ask of this machine (support_variants.h PreloadSupportVariants).
int ForEachRoomVariant(void (*each)(void* context,const wchar_t* file),void* context) noexcept;
// Any thread (the host's plans): the support loadout room member `puid` (EOS ProductUserId text) published in the last
// room, read (member_config.h Decode). False when that member published none that reads (its calls: the defaults).
bool RoomMemberConfig(const char* puid,SupportConfig* out) noexcept;
// Any thread: every other member's player loadouts (the jets they summon themselves, EDF6X_VCPVEH) the last room
// published, validated; kept after the room is left. The mission preloads their files (player_vehicle.cpp).
int ForEachRoomPlayerVehicle(void (*each)(void* context,VehicleBody body,std::uint64_t bits),void* context) noexcept;

}  // namespace crew::roomsync
