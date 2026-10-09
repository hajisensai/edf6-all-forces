#pragma once
#include <cstdint>
#include "mapcmd_logic.h"
namespace crew {
struct ObjRef;
// Stable wire reasons. Success means an actual order/boarding assignment was accepted, not that
// the squad has already reached its destination or entered its seats.
enum class NpcCommandReason : std::uint32_t {
    none,invalidRequester,notFound,notLeader,notAuthority,notOwner,scripted,notFriendly,
    cooldown,noTarget,noSeat,unsupported,failed,disabled,stale,boardingUnavailable,noVehicle,
    riding, // a squad seated in a vehicle: recruiting / follow / point orders go to its vehicle (appended: wire value)
    count
};
struct NpcCommandResult { NpcCommandReason reason=NpcCommandReason::failed;std::uint32_t affected=0;
    bool Accepted() const noexcept {return reason==NpcCommandReason::none && affected>0;}
};
NpcCommandResult NpcSquadCommandForRequester(const ObjRef& leader,const mapcmd::Command& command,
    const ObjRef& requester,const ObjRef& focus) noexcept;
NpcCommandResult LastNpcCommandResult() noexcept; // most recent local attempt, game thread only
}
