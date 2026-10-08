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
//    while a result that must count once (damage) counts on one machine's copies only: the machine whose player made
//    it (a call's caller, a drone's thrower, a rescue's player; a carrier's drones its carrier's), recorded as each copy
//    is made (NoteLocalCopy, the owner set by SetSpawnOwner), else the host's (IsOnlineAuthority). A player riding a copy
//    another machine owns deals none with it: that machine's copy of the same aircraft fights in its place;
//  - a vehicle without a live REGISTERED seat-0 rider belongs to the host for plugin work, both on the host with its
//    DummyVehicleRider and on clients whose seat is empty. Deliberately ignore the stock last-driver fallback here:
//    after a client gets out it would call that client local, while the host's NPC also calls the host local.
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
    bool vehicle;         // it has seats: `stock` and `hostSeat0` apply
    bool hostSeat0;       // no live registered seat-0 rider: empty, expired or an unregistered NPC -> host
    std::uint8_t copyOwner; // an unregistered copy's owner (CopyOwner): who made it, as recorded here
    int stock;            // vehicles: 0x630F90(v, 1, 1), 1 this machine, 2 another (other objects: their flags decide)
};

constexpr std::uint16_t kNetRemote=1,kNetLocal=2;

// Whose an unregistered copy is: not recorded (the host's), this machine's player's, another machine's player's.
enum CopyOwner : std::uint8_t { kCopyHost=0, kCopyHere=1, kCopyElsewhere=2 };

// Who pulls the trigger of a round: the vehicle (its NPC crew, its driver's features: the ram, the drill, the EMC) or a
// player of this machine at one of its guns (their own input: no NPC on another machine fires for them).
enum class Shooter : std::uint8_t { vehicle, localPlayer };

// Never registered: each machine has its own copy, which nothing replicates.
constexpr bool LocalCopy(std::uint16_t net) noexcept { return (net&(kNetRemote|kNetLocal))==0; }

// Work that must happen once in the room (seat an NPC driver, spawn what every machine would otherwise spawn): offline,
// or on the host. Online with unknown session functions: nowhere (as a client).
constexpr bool HostOnly(bool session,bool known,bool host) noexcept { return !session || (known && host); }

// The one machine whose result counts for the object (damage it deals, its NPC's decisions): see the top.
constexpr bool Authority(const Facts& f) noexcept {
    if(!f.session)return true;
    if(LocalCopy(f.net))return f.copyOwner==kCopyHere || (f.copyOwner==kCopyHost && f.known && f.host);
    if(!f.known)return false;
    if(f.vehicle && f.hostSeat0)return f.host;
    if(f.vehicle)return f.stock==1;
    return (f.net&kNetRemote)==0;
}

// Whether this machine runs the object's own simulation (its flight, its AI's steering): its authority, and every
// machine for an unregistered object (nothing else moves its copy there).
constexpr bool RunsHere(const Facts& f) noexcept { return !f.session || LocalCopy(f.net) || Authority(f); }

// Whether a round `by` fires from the object counts here (its damage): every round counts on exactly one machine.
//  - an unregistered copy: its owner's machine (Authority), whoever fires: each machine has its own copy, the owner's
//    copies stand for the aircraft;
//  - a registered vehicle, a player of this machine at the trigger: here (the input is theirs; on any other machine that
//    player is another machine's, and nothing fires for them there);
//  - a registered vehicle, its NPC crew or its driver's features: its authority (Authority).
constexpr bool ShotCounts(const Facts& f,Shooter by) noexcept {
    if(!f.session)return true;
    if(LocalCopy(f.net))return Authority(f);
    if(by==Shooter::localPlayer)return true;
    return Authority(f);
}

// Whether a round a player of this machine fires from the object names that player as its attacker (its IFC's owner)
// instead of the object. Online, on a registered vehicle: the room's hit authority (coop W3, docs/net-re/damage.md §9)
// takes the attacker's machine for the one that judges the hit, and a vehicle's machine (0x630F90; the host for an NPC
// driver) may be another than the gunner's, where ShotCounts deals the damage: named as the vehicle, the round would be
// dropped here and dealt as 0 there. The player is this machine's registered object, so both rules pick this machine.
// Offline (the kill credit, the team, the hull the round spares stay the vehicle's, as origin/main) and on the plugin's own
// copies (no identity: the hit is settled where the round is) the vehicle stays the attacker.
constexpr bool ShooterIsAttacker(const Facts& f,Shooter by) noexcept {
    return f.session && by==Shooter::localPlayer && !LocalCopy(f.net);
}

// Whether a round passes through `target` because it is the vehicle its owner rides: online, a round ShooterIsAttacker
// named a player for (`ownerIsPlayer`: this machine's) must spare that player's vehicle as the vehicle-owned round spares
// it. The stock collector leaves out only the owner itself, by pointer (0x232AA0 step 1, core +0x9A8; its base's single
// ignored object +0x30 is 0 for bullets: docs/bullet-pass-re.md section 3.2), nothing along the owner's ride: the round,
// made at the muzzle inside the vehicle's own collision, would hit it. The rounds' blasts need nothing: the
// IndirectFireUnit's blast hurts only what is hostile to its team (+0xD0, the owner's +0x314: docs/carrier-laser-re.md),
// and the player's vehicle is on the player's side. Offline the owner is never the player (the vehicle stays it), and
// the test is off besides, so the stock path is untouched.
constexpr bool SparesRide(bool session,bool ownerIsPlayer,const void* ownerVehicle,const void* target) noexcept {
    return session && ownerIsPlayer && ownerVehicle && ownerVehicle==target;
}

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
// Request existing soldiers to walk to this vehicle behind OnlineMaySeatNpc. Never calls RideAi.
// True means boarding is pending, not that a pilot is seated; callers must inspect the actual seat.
bool SeatNpcRider(unsigned char* vehicle,bool spawned) noexcept;
// Whether a round fired from `owner` by `by` deals its damage here (online::ShotCounts).
bool OnlineShotCounts(const void* owner,online::Shooter by) noexcept;
// The attacker a round fired from `owner` by `by` names (online::ShooterIsAttacker): this machine's player in one of
// `owner`'s seats, else `owner`.
const unsigned char* OnlineAttacker(const unsigned char* owner,online::Shooter by) noexcept;

// The owner of the unregistered copies made from now on (game thread): kCopyHere for this machine's player's call /
// throw / rescue, kCopyElsewhere for another machine's player's call replayed here, kCopyHost otherwise (the rest
// state). Returns the one before, which the caller sets back once its copies are made (a function pair, not a scope
// object: the spawning code runs under __try, which allows no object unwinding).
online::CopyOwner SetSpawnOwner(online::CopyOwner owner) noexcept;
// The copy owner of a call / throw by `human` (its owner): this machine's player, another machine's, or neither.
online::CopyOwner CopyOwnerOfCaller(const unsigned char* human) noexcept;
// Records copy `object` just made: its parent's owner when `parent` is a recorded copy (a carrier's drone), else the
// scope's.
void NoteLocalCopy(const void* object,const void* parent) noexcept;
}  // namespace crew
