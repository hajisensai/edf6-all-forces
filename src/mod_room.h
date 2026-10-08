#pragma once
#include <cstdint>
namespace crew {
// Wire namespace is outside every vanilla/Coop SEARCH_TYPE range. The low byte
// preserves the room kind and Coop capacity family; protocol changes get a new prefix.
constexpr std::int64_t kAllForcesRoomPrefix=0x41460100;
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
}
