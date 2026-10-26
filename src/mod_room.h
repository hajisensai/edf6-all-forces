#pragma once
#include <cstdint>
namespace crew {
// Wire namespace is outside every vanilla/Coop SEARCH_TYPE range. The low byte
// preserves the room kind and Coop capacity family; protocol changes get a new prefix.
// The room protocol: every machine in a room runs the same wire protocols (PvP, support, room sync). A change any of
// them cannot read takes the next number: an older build then neither finds nor joins the room (its own namespace
// check), and this one refuses the older build's rooms. No compatibility with older builds (CLAUDE.md).
//   1: up to 2026-10-25;  2: the named PvP factions (pvp_protocol.h v3, team_native.h's 16 teams);
//   3 (2026-10-26): every support from the map's bar for pt (the support catalog with the strikes, support_call.h; the
//      barrages' BRGS channel; the support call weapons retired from the weapon table).
// EDF6Coop reads both values from All Forces (EDF6AF_RoomPrefix / EDF6AF_RoomProfile) rather than keeping its own.
constexpr std::int64_t kAllForcesRoomProtocol=3;
constexpr std::int64_t kAllForcesRoomPrefix=0x41460000+(kAllForcesRoomProtocol<<8);
constexpr std::int64_t kAllForcesRoomInvalid=-1;
constexpr bool AllForcesWireType(std::int64_t value) noexcept {
    return value>=kAllForcesRoomPrefix && value<kAllForcesRoomPrefix+0x100;
}
constexpr std::int64_t EncodeAllForcesRoom(std::int64_t value) noexcept {
    return value>=0 && value<0x100 ? kAllForcesRoomPrefix+value : value;
}
constexpr std::int64_t DecodeAllForcesRoom(std::int64_t value) noexcept {
    return AllForcesWireType(value) ? value-kAllForcesRoomPrefix : kAllForcesRoomInvalid;
}
bool InstallModRoom() noexcept;
// The mission this machine's room last published (the game's DLC and MISSION_NO attributes, as the host publishes them
// whenever it picks one): false until it published both. Any thread.
bool PublishedRoomMission(std::int64_t* content,std::int64_t* mission) noexcept;
}
