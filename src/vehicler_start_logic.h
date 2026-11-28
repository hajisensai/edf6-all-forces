// The Vehicler's start seat, its pure part (docs/newclass-re/vehicler-plan.md §2.1, §2.3, §5 M3; the runtime is
// vehicler_start.cpp, its tests tests/vehicler_start_test.cpp). No game, no Windows.
//
// Per local player object, once a mission:
//  1. StartGate: whether to ask now for its start vehicle (zw_vehicle_dispatch.h RequestZwStart), wait, or never ask;
//  2. once asked, AfterAsk: by the request's state, wait, ride (the hull is there and the player on foot), or give up;
//  3. the ride itself is the stock RideVehicle on the driver seat (ride_native.h), on the owner's machine alone.
// A player who does not get a vehicle walks in as any soldier: one log line why, nothing in its place.
#pragma once
#include <cstdint>

namespace crew::vstart {
inline constexpr int kVehiclerType=4;    // soldier::ZwType of the Vehicler (soldier_factory.h)
inline constexpr unsigned kDriverSeat=0; // seat 0 of a vehicle's +0x608 array: the driver's (npcai.cpp SeatPriority)

// What the tick knows of a player object this frame.
struct Facts {
    int zwType=-1;            // soldier::ZwType(obj)
    bool local=false;         // this machine's player (+0x128 bit 0 clear, its pad +0x340 set; online PlayerHuman())
    bool alive=false;
    bool asked=false;         // its request made this mission already (whatever came of it)
    bool underground=false;   // the mission's (support_policy.h): no support hull is made there
    int kind=-1;              // its mission vehicle (zw_vehicle_catalog.h ZwMissionVehicle), -1 none (ZW_VehicleNone)
    bool kindReady=false;     // that hull preloaded and not broken (ZwVehicleReady)
    bool online=false;        // in a session
    bool sessionOpen=false;   // online: the support session takes requests (its handshake done); offline unused
};
enum class Gate : std::uint8_t {
    request,       // ask now
    wait,          // not yet (dead for now, the session's handshake not done): ask on a later frame
    ignore,        // never for this object, nothing to say (not a Vehicler, not this machine's, asked already)
    walk,          // never this mission, logged once: the player walks in (Why says why)
};
enum class Why : std::uint8_t { none,notVehicler,notLocal,asked,dead,sessionClosed,underground,noVehicle,notPreloaded };

constexpr Why StartWhy(const Facts& f) noexcept {
    if(f.zwType!=kVehiclerType)return Why::notVehicler;
    if(!f.local)return Why::notLocal;
    if(f.asked)return Why::asked;
    if(!f.alive)return Why::dead;
    if(f.underground)return Why::underground;
    if(f.kind<0)return Why::noVehicle;
    if(!f.kindReady)return Why::notPreloaded;
    if(f.online && !f.sessionOpen)return Why::sessionClosed;
    return Why::none;
}
constexpr Gate GateOf(Why w) noexcept {
    switch(w) {
    case Why::none:return Gate::request;
    case Why::dead:case Why::sessionClosed:return Gate::wait;
    case Why::notVehicler:case Why::notLocal:case Why::asked:return Gate::ignore;
    case Why::underground:case Why::noVehicle:case Why::notPreloaded:return Gate::walk;
    }
    return Gate::ignore;
}
constexpr Gate StartGate(const Facts& f) noexcept { return GateOf(StartWhy(f)); }
constexpr const char* WhyText(Why w) noexcept {
    switch(w) {
    case Why::none:return "ask";
    case Why::notVehicler:return "not a Vehicler";
    case Why::notLocal:return "not this machine's player";
    case Why::asked:return "asked already this mission";
    case Why::dead:return "dead";
    case Why::sessionClosed:return "the support session's handshake not done";
    case Why::underground:return "underground (no vehicle is made there)";
    case Why::noVehicle:return "no vehicle in slot 0 (ZW_VehicleNone)";
    case Why::notPreloaded:return "its vehicle not preloaded (not installed / broken)";
    }
    return "?";
}

// After the request: zw_vehicle_dispatch.h ZwStartState, mirrored (none, pending, ready, refused).
enum class Asked : std::uint8_t { none,pending,ready,refused };
enum class Next : std::uint8_t {
    wait,      // pending (the host planning / every machine making it), or the player in a state RideVehicle refuses
    ride,      // ready, the hull there, the player alive and on foot: seat them
    walk,      // refused / the hull gone / the player in another vehicle already: on foot, logged once
};
enum class AfterWhy : std::uint8_t { none,pending,busy,refused,lost,hullGone,dead,aboardOther };
struct AfterFacts {
    Asked state=Asked::none;
    bool haveVehicle=false;   // ready gave a live hull
    bool alive=false;
    bool onFoot=false;        // human +0x1548 empty
    bool busy=false;          // human +0x39C set: RideVehicle refuses then (0x57686E), it is waited out
};
constexpr AfterWhy AfterAskWhy(const AfterFacts& f) noexcept {
    switch(f.state) {
    case Asked::pending:return AfterWhy::pending;
    case Asked::refused:return AfterWhy::refused;
    case Asked::none:return AfterWhy::lost;          // the records reset under it (a new mission): nothing to seat
    case Asked::ready:break;
    }
    if(!f.haveVehicle)return AfterWhy::hullGone;
    if(!f.alive)return AfterWhy::dead;
    if(!f.onFoot)return AfterWhy::aboardOther;
    if(f.busy)return AfterWhy::busy;
    return AfterWhy::none;
}
constexpr Next AfterAsk(const AfterFacts& f) noexcept {
    const AfterWhy w=AfterAskWhy(f);
    return w==AfterWhy::none ? Next::ride : w==AfterWhy::pending || w==AfterWhy::busy ? Next::wait : Next::walk;
}
constexpr const char* AfterText(AfterWhy w) noexcept {
    switch(w) {
    case AfterWhy::none:return "ride";
    case AfterWhy::pending:return "pending";
    case AfterWhy::busy:return "in a state the ride waits out (+0x39C)";
    case AfterWhy::refused:return "refused by the host / not made";
    case AfterWhy::lost:return "its request forgotten";
    case AfterWhy::hullGone:return "its vehicle gone before the ride";
    case AfterWhy::dead:return "dead before the ride";
    case AfterWhy::aboardOther:return "aboard another vehicle already";
    }
    return "?";
}

// The NPC seat pick (npcai.cpp SeatTakes): a Vehicler's own vehicle's driver seat is its owner's alone
// (vehicler-plan.md §2.3). `owner`: zw_vehicle_dispatch.h ZwVehicleOwner(vehicle), null for any other vehicle.
constexpr bool DriverSeatBarred(unsigned seat,const void* owner,const void* who) noexcept {
    return seat==kDriverSeat && owner && owner!=who;
}

// The board button's tail after the ride (0x56D7D1): human +0x3F0 set when the seat's key row (+0x2B4) is 3; never cleared
// here (the leave 0x56FE80 clears it).
inline constexpr int kKeyRowSpecial=3;
constexpr bool SetsSeatFlag(int keyRow) noexcept { return keyRow==kKeyRowSpecial; }
}  // namespace crew::vstart
