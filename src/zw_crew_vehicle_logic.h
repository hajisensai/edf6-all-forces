// A support Vehicler's own vehicle (catalog kZwCrewCatalog, zw_vehicle_dispatch.h): the pure part, unit-tested
// (tests/zw_crew_vehicle_test.cpp). A support soldier of the Vehicler kind (support_call.h SupportWeapon::zwVehicler, its
// body EDF6VC_ZW_VEHICLER[_LEADER] made by the ZwVehicler factory) is a Vehicler as a player is: it has a vehicle of its
// own, made beside it once it stands on its feet (out of any transport that brought it), the host's (offline this
// machine's) to ask, one a soldier a life. The vehicle kind is fixed (kCrewKind, the start-unlocked tank ZW_V1_HSTVL):
// an NPC picks no row and owns no unlocks. The session carries one map transaction at a time (support_protocol.cpp
// MapBusy): an ask refused waits kRetryMs and asks again, at most kMostTries refusals, then the soldier fights on foot.
#pragma once
#include <cstdint>

namespace crew::zwcrew {
inline constexpr int kMostCrews=16;          // support Vehiclers waiting for / holding a vehicle at once
inline constexpr int kCrewKind=0;            // zw_vehicle_catalog.h kZwVehicles[0]: ZW_V1_HSTVL (ZwUnlock::start)
inline constexpr std::uint64_t kSettleMs=2000;   // on its feet this long (out of a transport, the spawn's fall) before it asks
inline constexpr std::uint64_t kRetryMs=3000;
inline constexpr int kMostTries=20;

enum class Phase : std::uint8_t { none, waiting, asked, made, givenUp };
struct Entry {
    Phase phase=Phase::none;
    int tries=0;
    std::uint64_t nextAt=0;
};
enum class Act : std::uint8_t { none, ask, giveUp };

// A support Vehicler noted (made on the authority machine): it waits to stand kSettleMs first.
constexpr void Note(Entry& e,std::uint64_t now) noexcept { e=Entry{};e.phase=Phase::waiting;e.nextAt=now+kSettleMs; }
// Every frame of a waiting entry: `onFoot` (in no vehicle). Aboard anything its clock starts again; on its feet long
// enough it asks (an ask counts as a try); out of tries it gives up (once: Act::giveUp, then none).
constexpr Act Step(Entry& e,std::uint64_t now,bool onFoot) noexcept {
    if(e.phase!=Phase::waiting)return Act::none;
    if(!onFoot){e.nextAt=now+kSettleMs;return Act::none;}
    if(now<e.nextAt)return Act::none;
    if(e.tries>=kMostTries){e.phase=Phase::givenUp;return Act::giveUp;}
    ++e.tries;e.phase=Phase::asked;
    return Act::ask;
}
// The ask refused / rolled back (its transaction failed): waits kRetryMs and asks again.
constexpr void Refused(Entry& e,std::uint64_t now) noexcept {
    if(e.phase!=Phase::asked)return;
    e.phase=Phase::waiting;e.nextAt=now+kRetryMs;
}
// Its vehicle made: nothing more asked for this soldier (a wreck is not replaced: it fights on foot).
constexpr void Made(Entry& e) noexcept { if(e.phase==Phase::asked || e.phase==Phase::waiting)e.phase=Phase::made; }

// The request's loadout word and the plan's unit variant (every machine sees it; only the authority reads the slot):
// the kind (8 bits), the crew slot (8), the slot's generation (16, never 0: a variant is never 0, the start's always 0).
struct Ticket { int kind=-1,slot=-1;std::uint16_t generation=0; };
constexpr std::uint64_t Encode(int kind,int slot,std::uint16_t generation) noexcept {
    return static_cast<std::uint64_t>(static_cast<std::uint8_t>(kind))|(static_cast<std::uint64_t>(static_cast<std::uint8_t>(slot))<<8)|
           (static_cast<std::uint64_t>(generation)<<16);
}
// False for a word that is no crew ticket (a bit above 32, a slot past kMostCrews, generation 0).
constexpr bool Decode(std::uint64_t word,Ticket* out) noexcept {
    if(word>>32)return false;
    Ticket t{};
    t.kind=static_cast<int>(word&0xFFu);t.slot=static_cast<int>((word>>8)&0xFFu);t.generation=static_cast<std::uint16_t>(word>>16);
    if(t.slot>=kMostCrews || !t.generation)return false;
    *out=t;
    return true;
}
// The next generation of a slot (wraps past 0).
constexpr std::uint16_t NextGeneration(std::uint16_t g) noexcept { return static_cast<std::uint16_t>(g==0xFFFFu ? 1u : g+1u); }
}  // namespace crew::zwcrew
