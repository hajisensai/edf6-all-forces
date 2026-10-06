// Online authority: the one place the plugin asks which machine an object's plugin work runs on (docs/online-re.md
// sections 2-3). EDF6 replicates inputs and events, not spawns: every machine runs its own copy of each object, and a
// registered network object is run (its pose sent, its driver's stick sent) by one machine. What the plugin adds to an
// object (an NPC driver, a flight controller, damage it deals) must run where that object is run, or each machine does
// it once and the copies disagree (or the damage is dealt once per machine).
//
//   session   0x7748F0: a session is on (crew::InSession, netprobe.cpp)
//   host      0x784210: this machine is the room's owner (1 offline)
//   operator  0x630F90(v, hostFallback 1, preferSeat0 1): 1 this machine runs the vehicle, 2 another: seat 0's rider's
//             machine, else seat 0's last rider's (+0x300), else the host
//   net word  object +0x128 (its NetworkObject +8): bit 0 another machine's, bit 1 ours, 0 never registered
//
// Two cases the stock answer gets wrong for the plugin, both because the object has no network identity:
//  - an object the plugin made itself (CreateObject: the call aircraft, the thrown drones, the creatures) is never
//    registered: each machine has its own copy that nothing replicates, so each must run its own (OnlineRunsHere),
//    while a result that must count once (damage) counts on the copy a player of this machine rides, else on the
//    host's (IsOnlineAuthority);
//  - a vehicle whose seat 0 holds an NPC rider with no identity (RideAi's DummyVehicleRider) counts as "this machine's"
//    on every machine that seated one: the host is its authority, and only the host seats one (OnlineMaySeatNpc).
// The decisions are the pure functions below (tests/online_authority_test.cpp runs them on every case); the game is read
// in src/online_authority.cpp. Nothing else in the plugin calls 0x7748F0 / 0x784210 / 0x630F90 / 0x630DF0 for a
// decision (tests/online_gate_guard.py checks it), the NET probe (netprobe.cpp) reads them for its log only.
#pragma once
#include <cstdint>

namespace crew {
namespace online {
// What one authority question reads from the game.
struct Facts {
    bool session;         // a session is on
    bool known;           // the session functions are the ones read (else: online, nothing is this machine's)
    bool host;            // this machine is the room's owner
    std::uint16_t net;    // the object's network flags word (+0x128)
    bool vehicle;         // it has seats: `stock` and `npcSeat0` apply
    bool npcSeat0;        // seat 0 holds a live rider with no network identity (an NPC the plugin or a script seated)
    bool localPlayer;     // a seat holds a player of this machine (a pad here, not another machine's)
    int stock;            // vehicles: 0x630F90(v, 1, 1), 1 this machine, 2 another (other objects: their flags decide)
};

constexpr std::uint16_t kNetRemote=1,kNetLocal=2;

// Never registered: each machine has its own copy, which nothing replicates.
constexpr bool LocalCopy(std::uint16_t net) noexcept { return (net&(kNetRemote|kNetLocal))==0; }

// Work that must happen once in the room (seat an NPC driver, spawn what every machine would otherwise spawn): offline,
// or on the host. Online with unknown session functions: nowhere (as a client).
constexpr bool HostOnly(bool session,bool known,bool host) noexcept { return !session || (known && host); }

// The one machine whose result counts for the object (damage it deals, its NPC's decisions): see the top.
constexpr bool Authority(const Facts& f) noexcept {
    if(!f.session)return true;
    if(!f.known)return false;
    if(LocalCopy(f.net))return f.localPlayer || f.host;   // the copy a player of this machine rides is theirs
    if(f.vehicle && f.npcSeat0)return f.host;
    if(f.vehicle)return f.stock==1;
    return (f.net&kNetRemote)==0;
}

// Whether this machine runs the object's own simulation (its flight, its AI's steering): its authority, and every
// machine for an unregistered object (nothing else moves its copy there).
constexpr bool RunsHere(const Facts& f) noexcept { return !f.session || LocalCopy(f.net) || Authority(f); }

// Whether this machine may seat an NPC rider (RideAi) in the vehicle: an unregistered copy is this machine's alone; a
// registered vehicle gets one on the host only (a client's would make it the vehicle's authority there too).
constexpr bool MaySeatNpc(const Facts& f) noexcept { return LocalCopy(f.net) || HostOnly(f.session,f.known,f.host); }
}  // namespace online

// The game's answers (src/online_authority.cpp). `object` may be any GameObject; a null or unreadable one: true with no
// session on, else false.
bool OnlineHostOnly() noexcept;                        // offline, or this machine hosts the room
bool IsOnlineAuthority(const void* object) noexcept;   // the result of this object's plugin work counts here
bool OnlineRunsHere(const void* object) noexcept;      // this machine runs the object's own simulation
bool OnlineMaySeatNpc(const void* vehicle) noexcept;   // an NPC rider may be seated in it here
// The stock RideAi (VehicleBase slot 50, 0x633030) behind OnlineMaySeatNpc: the one way the plugin seats an NPC rider.
// `spawned`: the call's flag (true for a vehicle just made, as CreateFriend passes). False when not seated here.
bool SeatNpcRider(unsigned char* vehicle,bool spawned) noexcept;
}  // namespace crew
