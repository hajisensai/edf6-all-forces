// EDF6VehicleCrew: shared layout, config and helpers.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "edf/layout.h"
#include "edf/patch.h"
#include "stores.h"
#include "edf/seat.h"

namespace crew {
extern unsigned char* image;

struct Config {
    bool enabled=true;
    bool debug=true;
    bool autoCrew=true;        // an empty friendly vehicle gets an NPC driver (the stock RideAi)
    DWORD crewDelayMs=3000;    // ...after it has stood empty this long
    float crewRange=600.0f;    // metres from the player; 0 = any distance
    bool bump=true;            // the player can board a seat an NPC holds
    bool bumpToGunner=true;    // the bumped NPC moves to a free gunner seat instead of leaving
    bool heliPilot=true;       // NPC-crewed helicopters are flown by the plugin
    float heliHeight=35.0f;    // metres above the player the helicopter holds
    float heliFollow=45.0f;    // horizontal distance it keeps from the player
    float heliRange=350.0f;    // it engages enemies within this distance
    float heliCombatRange=120.0f;// engaged (player on foot), it stays within this of the player
    bool heliFire=true;
    bool heliAvoid=true;       // helis steer and climb clear of terrain and buildings (map rays)
    float heliFireHeight=25.0f;// engaged, its strafing runs fly this far above the target
    float heliFireCone=4.0f;   // degrees between the nose (pitch included) and the target it still fires at
    bool heliMissile=true;
    DWORD heliMissileMs=4000;  // minimum gap between missiles
    // (The flight controller's own gains are constants in heli.cpp; the old HeliMoveGain / HeliBrakeGain /
    // HeliClimbGain / HeliHoverLearn keys are ignored, plugin.cpp LoadConfig.)
    DWORD heliLandMs=0;        // it lands by a player who stood still, with no enemy near, this long; 0 = never (it orbits)
    float heliSpeed=25.0f;     // m/s at full stick (0 or below the stock speed: stock)
    float heliAgility=4.0f;    // seconds (time constant) to reach it
    float heliYawRate=50.0f;   // deg/s: the yaw rate limit is raised to this where lower
    bool heliDoorGuns=true;    // the 410's door guns are aimed and fired by the plugin
    float heliGuardRadius=120.0f;// a guard heli circles its post this far out (0: it hovers over the post)
    float heliGuardSpeed=12.0f;// ...at this speed (m/s; at most 80% of its top speed)
    bool jetPilot=true;        // jets (edf6tr_jet_* SGOs) are flown by the plugin
    DWORD jetFuelSec=120;      // a jet withdraws after this long in the air
    DWORD jetSortieSec=60;     // ...one launched by an airstrike takeover
    bool jetAirRaider=true;    // the Air Raider's bomber calls send jets instead, and its call weapons (airstrike.cpp) work
    bool jetMissionStrike=true;// the missions' strafing-plane airstrikes (DemoAirStrike) send jets instead
    bool groundPilot=true;     // NPC-crewed Depth Crawlers (502, no stock AI) are driven by the plugin (ground.cpp)
    float groundFollow=20.0f;  // metres from the player it stops at with no enemy
    float groundRange=200.0f;  // it engages enemies within this distance
    float groundLeash=100.0f;  // it goes no further than this from the player while it has one
    bool groundFire=true;
    DWORD callNextKey=0xDD;    // in a mission: the next call every call weapon brings (VK_OEM_6 `]`; 0 = off)
    DWORD callPrevKey=0xDB;    // ...the one before (VK_OEM_4 `[`)
    bool seaRescue=true;       // a heli comes for a local player in the sea and ferries them to a submarine carrier's deck
    float rescueBelow=-5.0f;   // ...once they have been below this height (metres) for 1.5 s
    bool rescueAutoBoard=false;// ...and, in the stock board reach of a free door seat, boards them by the stock board path
    float subHullHp=100000.0f; // a submarine carrier's hull HP (raised to this from its SGO's 30000; 0 = the SGO's)
    float subHeavyHit=1500.0f; // a hit on its hull (no deck part) counts only from a heavy source, or from this much
                               // damage in one hit (0 = only the listed heavy sources, subcarrier.cpp kHeavy)
    bool carrierLaser=true;    // with a submarine carrier out, the e508 teleportation ships charge and fire a portal laser (carrierlaser.cpp)
    float carrierLaserDamage=2500.0f;// the main beam's damage
    float carrierLaserBreak=0.15f;   // the share of the ship's max HP that, taken during the charge, breaks it off
    bool vehicleWelding=true;  // wheeled chassis get the VEHICLE body quality (motion welding) instead of CHARACTER (physics.cpp)
    bool giantContactCap=true; // vertical contacts with dynamic bodies limited to maxForce*dt like EDF5's hkp (physics.cpp)
    bool vehicleHud=true;      // HP / ammo / fuel over the nearest NPC-driven friendly vehicles, the carriers' panel (hud.cpp)
    int vehicleHudCount=6;     // ...over at most this many of them (nearest first)
    float vehicleHudRange=500.0f;// ...within this many metres of the player
    bool playerJet=true;       // the player jets (edf6tr_pjet_* / EDF6VC_PJET_* SGOs) fly as planes with the player at the stick (playerjet.cpp)
    bool playerJetInvertPitch=false;// ...the right stick / mouse Y pitches the other way (pulled back = nose down)
    int playerJetBoostKey=0x10;     // ...on the keyboard and mouse: the boost key (a Windows virtual-key code; VK_SHIFT)
    int playerJetBrakeKey=0x11;     // ...and the brake key (VK_CONTROL)
    int playerJetSwitchKey=0x52;    // ...and the key that switches stores ('R'; on a pad LB)
    float playerJetMouseSpeed=1.0f; // ...how fast the mouse moves its aim
    float playerJetRamDamage=1.0f;  // a player jet's ram: the enemies round it take the HP share it lost times this (0: none)
    bool jetSound=true;             // the jets' engine sound (jetsound.cpp)
    float jetSoundVolume=1.0f;      // ...its volume, times the game's own for that sound
    bool bigWorld=true;             // the physics world +-10000 m instead of +-3000 (bigworld.cpp), from the next mission load
};
// Every value is range-checked when the ini is read (plugin.cpp Validate): a value out of range is clamped and
// the change logged.
// The live config: an immutable snapshot, swapped whole by the ini reload (plugin.cpp LoadConfig) and read
// from any thread (game, call picker, HUD draw) without a torn mix of old and new values.
const Config& Cfg() noexcept;
// Between SuppressBump(true) and SuppressBump(false) on this thread, the player's board button takes no
// NPC's seat (heli.cpp PressBoard): an override of the call, not a write to the config. (A pair of calls,
// not a scoped object: the callers run under __try, which allows no destructors.)
void SuppressBump(bool on) noexcept;
bool BumpSuppressed() noexcept;

// --- Time ---
// The game clock, game thread only: wall time, except that a gap between two reads longer than 250 ms
// (pause menu, loading) counts as one 16 ms frame. Every timer of the plugin's logic, the player fix's
// included, is on this clock; wall time (GetTickCount64) is for log throttles and other threads only.
ULONGLONG GameMs() noexcept;
// The game frame number, game thread only: it steps when a vehicle's per-frame input comes round again
// (crew.cpp InputHook calls SeeFrame), so "once a frame" work compares frame numbers, not clocks.
ULONGLONG GameFrame() noexcept;
void SeeFrame(const void* vehicle) noexcept;

// --- Mission lifecycle (mission.cpp) ---
// The mission's player preload (mission.cpp hooks it): every table of per-object state from the last
// mission is dropped here, before the new mission's objects (which may reuse the old addresses) exist.
void MissionStart() noexcept;
void ResetCrew() noexcept;        // crew.cpp
void ResetHelis() noexcept;       // heli.cpp (and the player track, the rescue)
void ResetGround() noexcept;      // ground.cpp
void ResetJets() noexcept;        // jet.cpp (and the dolls, the walls learned)
void ResetAirstrikes() noexcept;  // airstrike.cpp
void ResetBoosters() noexcept;    // booster.cpp
void ResetSubs() noexcept;        // subcarrier.cpp
void ResetLaser() noexcept;       // carrierlaser.cpp
void ResetPlayerJets() noexcept;  // playerjet.cpp
void ResetHud() noexcept;         // hud.cpp
void ResetJetSound() noexcept;    // jetsound.cpp
void ResetMissiles() noexcept;    // missile.cpp
void ResetBigWorld() noexcept;    // bigworld.cpp
// A bigger physics world and the map pieces' log (bigworld.cpp): at load, before any mission.
bool InstallBigWorld() noexcept;
// The plugin's missiles guided by proportional navigation with a proximity fuse (missile.cpp).
bool InstallMissiles() noexcept;
// The jets' engine sound (jetsound.cpp): checked at load; per vehicle input (it picks the plugin's jets itself);
// once a frame, the plugin off too (the camera's motion; the sounds of jets gone, or all with the plugin off, stopped).
bool InstallJetSound() noexcept;
void JetSound(unsigned char* vehicle) noexcept;
void JetSoundTick() noexcept;
// The lock-on beeps of a vehicle's weapons: kept for a local player's seat, silenced for every other (jetsound.cpp).
void LockSound(unsigned char* vehicle) noexcept;

// --- EDF.dll layout ---
// The facts EDF6AutoTurret rests on too live in common/edf/layout.h (one definition for both plugins):
// GameObject weak-this +0x28 / +0x30, the vehicle's matrix, position, dead byte, team, seats, the seat's
// rider weak_ptr, the human's pad / player flag, the dummy rider's vtable, At / Put.
using edf::kSelf; using edf::kSelfCtrl; using edf::kMatrix; using edf::kPosition; using edf::kDead; using edf::kTeam;
using edf::kSeats; using edf::kSeatCount; using edf::kSeatStride; using edf::kSeatRider; using edf::kSeatRiderCtrl;
using edf::kHumanPad; using edf::kHumanPlayer; using edf::kDummyRiderVtable; using edf::kSlotInput;
using edf::At; using edf::Put;
// Teams (mission AsCommon.h): player 0, enemy 1, friend 2, neutral 3, vehicle 5 = nobody's vehicle,
// which anyone may board (CanRideSeat skips the team test for it).
constexpr std::int32_t kTeamVehicle=5;
// Human: the vehicle it is in (weak_ptr object +0x1548, control block +0x1550)
constexpr std::size_t kHumanVehicleCtrl=0x1550;
// VehicleBase virtual slots (input: edf::kSlotInput)
constexpr std::size_t kSlotFindSeat=49,kSlotRideAi=50;
constexpr unsigned kFindSeat=0x633B80,kRideAi=0x633030;
// Seat functions
constexpr unsigned kCanRideSeat=0x6346D0;   // (vehicle, human, seat) -> bool: team, mask, free, in reach
constexpr unsigned kCanRide=0x62DCB0;       // (vehicle, human) -> bool: any seat passes the above
constexpr unsigned kSeatRide=0x633C10;      // (vehicle, rider, index, force) -> seat or null
constexpr unsigned kSeatClear=0x634940;     // (vehicle, seat): forget the rider, no message to it
constexpr unsigned kSeatKick=0x62E1A0;      // (vehicle, seat): get-off message, then clear (a dummy rider dies)
// The on-foot ride-prompt visitor (0x5735E7): {vtable, human, bool result}; slot 1 is called per object
constexpr unsigned kPromptFunctorVtable=0x17D09B8,kPromptVisit=0x5725A0;
constexpr std::size_t kFunctorHuman=0x8,kFunctorResult=0x10;

// A game object as the plugin remembers it: its address and its weak-this control block (+0x30). A new
// object at the same address (the next mission, a respawn) has another control block, so it is not taken
// for the old one. Read under the caller's __try (the object may be gone).
struct ObjRef {
    const void* obj=nullptr;
    const void* ctrl=nullptr;
    static ObjRef Of(const void* o) noexcept { return ObjRef{o,o ? At<const void*>(o,kSelfCtrl) : nullptr}; }
    bool Is(const void* o) const noexcept { return o && o==obj && At<const void*>(o,kSelfCtrl)==ctrl; }
    explicit operator bool() const noexcept { return obj!=nullptr; }
};

// The plugin's log (EDF6VehicleCrew.log; over kLogMax it is renamed to .log.1 and a new one begun).
void Log(const char* format,...) noexcept;
void ReloadConfigIfChanged() noexcept;
// The patch primitives and the seat test are common/'s (shared with EDF6AutoTurret).
inline bool Matches(std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept { return edf::Matches(image,rva,bytes,size); }
using edf::PatchVtableSlot;

// What sits in a seat (common/seat.cpp).
using Rider=edf::Rider;
inline Rider SeatRider(const unsigned char* seat) noexcept { return edf::SeatRider(image,seat); }
using edf::SeatAt; using edf::SeatCount; using edf::IsPlayer;

// The player as last seen (on foot through the prompt visitor, or riding through a vehicle input); `at` is
// GameMs (0: never seen).
struct PlayerFix { float pos[3]; std::int32_t team; ULONGLONG at; };
extern PlayerFix player;
void SeePlayer(const float* pos,std::int32_t team) noexcept;
// A new mission (MissionStart): the last mission's fix and player human are forgotten. GameMs counts the load
// as one frame, so without this they would pass for fresh at the new mission's start.
void ResetPlayer() noexcept;

// The core modules' own declarations (crew, heli, ground, hud, mission, loadout, overlay) are in their
// headers, included at the end of this file.

// jet.cpp
bool IsJet(const void* vehicle) noexcept;          // a 506 body from an edf6tr_jet_* SGO
bool JetInLine(const float* from,const float* to,const void* self) noexcept;   // a wingman in the way (no pass-through)
void JetFrame(unsigned char* vehicle) noexcept;    // from HeliFrame, NPC-crewed jets only
void JetReap(const void* self) noexcept;           // deletes withdrawn jets; call from another object's update
bool InstallJets() noexcept;
bool InstallJetProps() noexcept;                   // jetprops.cpp: from InstallJets
bool InstallBoosters() noexcept;                   // booster.cpp: the carrier's nozzle flames (stock Booster)
void CarrierFlames(const unsigned char* v,unsigned char* const* recs,float intensity,ULONGLONG ms) noexcept;
bool JetMotionProps(void* body) noexcept;          // a jet body's own motion properties (no 200 m/s cap); each physics step
void PreloadJets() noexcept;                       // from the mission's player preload
// A jet made at run time at `from`, flying along `heading` to work round `target`; false when it cannot
// be made (not preloaded this mission, profile mismatch): the caller keeps the stock behaviour then.
// `source`: what launched it (any fixed address per kind of source); jets from one source in a row fly
// as one flight, whose rounds pass through each other.
// `role`: the jet.cpp Role it flies as (same order); one whose SGO is not installed flies as a fighter.
// `escort`: it works round the player (while seen), not round `target`.
// blastCarrier / dollCarrier: a carrier whose drones blow up next to the enemy (the doll ones carrying a
// singing, dancing hololive doll); after `carrier`, as they are no role of their own.
enum class JetRole { strike, fighter, interceptor, multirole, carrier, blastCarrier, dollCarrier, gunship };
bool JetLaunch(JetRole role,const float* from,const float* heading,const float* target,DWORD fuelSec,const void* source,
               bool escort=false) noexcept;
// A gun drone (the carrier's drone body, EDF6VC_JET_DRONE.SGO) with no carrier: launched as JetLaunch launches
// a jet, it works round `target` (the player while seen, `escort`) and withdraws, to be deleted, as a launched
// jet does (fuel, damage, ammo). The vehicle, or nullptr (not preloaded this mission, kMaxJets flying).
unsigned char* JetLaunchDrone(const float* from,const float* heading,const float* target,DWORD fuelSec,const void* source,
                              bool escort) noexcept;
// Whether jet.cpp still flies `vehicle` (the object with weak-this control block `ctrl`), alive and not
// withdrawing.
bool JetFlying(const void* vehicle,const void* ctrl) noexcept;
// A helicopter made at run time (EDF6VC_HELI_410 / _506.SGO, tools/make_jets.py) at `from` facing `heading`,
// friend, NPC pilot: the vehicle, or nullptr (not preloaded this mission, the game failed to build it).
enum class HeliBody { brute410, eros506 };
unsigned char* HeliLaunch(HeliBody body,const float* from,const float* heading) noexcept;
// A bomber's payload: BombingPlane_Init's arguments (0x5AABB0; speed in metres a frame), which a jet's bomb
// bay is set up from.
struct BombLoad { const void* owner; float damage,spread,speed,adjust,reach; const void* param; std::int32_t seed; };
// A strike jet that flies the bomber's run from `from` along `heading` over `target` and drops its bombs
// itself; false (the caller keeps the stock bomber) when it or its bay cannot be made.
// `body`: the model it flies in (BomberBody of the bomber's model instance).
enum class JetBody { kind=-1, bomber401=2, bomber501_2=3 };
// `hold`: the stock bomber's token (its weak-this control block) JetHolds answers for.
bool JetLaunchBomber(const float* from,const float* heading,const float* target,const BombLoad& load,DWORD fuelSec,const void* source,
                     JetBody body,const void* hold) noexcept;
// Whether the jet launched with `hold` still has its bay (not yet open, open, or its bombs still tracked):
// false once the bay is gone, the jet shot down or no longer flown.
bool JetHolds(const void* hold) noexcept;
// Which bomber body a BombingPlane's model instance (plane+0x660, embedded) is: kind (the BOMBER501 look,
// the strike jet's) unless its bones name BOMBER401's or BOMBER501_2's model.
JetBody BomberBody(const unsigned char* inst) noexcept;

// body506.cpp: the 506 body the plugin's jets, carriers and player jets fly in. Which one a vehicle is comes
// from its SGO's mark (veh+0x162C, kMark* in body506.cpp, the one table of them); the 506's physics step
// (slot 57) is hooked once, there, and hands each body to its owner's step, which returns the velocity and
// spin to set (false: leave the stock step's).
enum class PluginBody { none, jet, sub, playerJet };
PluginBody BodyOf(const void* vehicle) noexcept;
float BodyMark(const void* vehicle) noexcept;      // the mark of a 506 body, 0 for anything else
bool InstallBody506() noexcept;                    // before InstallJets / InstallSub / InstallPlayerJets
bool Body506Ok() noexcept;                         // the physics hook is in
bool JetBodyStep(unsigned char* v,float* lin,float* ang) noexcept;        // jet.cpp
bool SubBodyStep(unsigned char* v,float* lin,float* ang) noexcept;        // subcarrier.cpp
bool PlayerJetBodyStep(unsigned char* v,float* lin,float* ang) noexcept;  // playerjet.cpp
// An impact `by` the plugin's vehicle (a crash, jet.cpp / playerjet.cpp) at `at`: `damage` to the enemies
// of its side within `radius` metres (a charge of the vehicle's own, as the blast drones' is: its team, its
// kills, friends untouched). False when it could not be dealt (no charge preloaded this mission).
bool ImpactDamage(const unsigned char* by,const float* at,float damage,float radius) noexcept;

// airstrike.cpp
bool InstallAirstrikes() noexcept;
void CallPick(int step,wchar_t* out,std::size_t size) noexcept;   // airstrike.cpp

// subcarrier.cpp: the submarine carrier (潜水母艦, docs/subcarrier-re.md), a 506 body from EDF6VC_SUB_CARRIER.SGO
// (tools/make_sub.py) driven by the plugin: it sits surfaced, follows the player at a ship's pace, turns its bow
// on the nearest enemy, fires its turret guns and homing missiles, reloads aboard; its HP shows as a follower gauge.
bool IsSub(const void* vehicle) noexcept;         // a 506 body with the carrier's mark
void SubFrame(unsigned char* vehicle) noexcept;   // from every vehicle's input hook (crew.cpp SubStep), carriers only
bool InstallSub() noexcept;                       // after InstallJets (it chains onto the 506 physics slot)
void PreloadSub() noexcept;                       // from the mission's player preload
// A carrier made at run time on the ground at `pos` (metres; it is raised to sit on the highest ground under
// its hull), its bow along `heading` (horizontal part used), friend, NPC pilot: the vehicle, or nullptr (its
// files not installed or not preloaded this mission, three already out, the game failed to build it).
unsigned char* SubLaunch(const float* pos,const float* heading) noexcept;
// The live carrier nearest to `from`: a point on its flat bow deck (the hull box top, 193 m over its origin)
// nearest to `from`, into `deck`; false with no carrier out.
bool SubDeck(const float* from,float* deck) noexcept;
// Horizontal metres from `p` to the nearest live carrier's hull footprint (0 over it), -1 with none.
float SubHullGap(const float* p) noexcept;
// carrierlaser.cpp: the e508 teleportation ships' portal laser (warning beam, interruptible charge, main beam)
// while a submarine carrier is out; its SGOs from tools/make_jets.py (EDF6VC_PORTAL_SIGHT / _LASER.SGO).
bool InstallLaser() noexcept;                         // at load
void PreloadLaser() noexcept;                         // from the mission's player preload
void CarrierLaserFrame(const unsigned char* sub) noexcept;   // from a flown carrier's frame, at most once a frame
// physics.cpp: stock EDF6 physics defects, patched at load (needs a game restart to toggle)
bool InstallPhysics() noexcept;

// What jet.cpp flies a jet as (game thread): its role's name, seconds of fuel left (-1: none, a carrier's drone),
// a carrier's drone launches left (-1: not a carrier), whether it is withdrawing; false when it does not fly it.
struct JetHudInfo { const char* role; float fuelSec; int drones; bool leaving; };
bool JetHud(const void* vehicle,JetHudInfo* out) noexcept;
// playerjet.cpp: jets the player flies (docs/player-jet-re.md), 506 bodies with a player-jet mark (7201-7202).
// The plugin never crews them; with the player in seat 0 it flies them as fixed-wing planes.
bool IsPlayerJet(const void* vehicle) noexcept;
void PlayerJetFrame(unsigned char* vehicle) noexcept;   // from every vehicle's input hook, after the stock step
// The jet the player flies now, for its cockpit readout (hud.cpp): game thread. False with none.
// The cockpit readout (hud.cpp): load in g; stall: all the wing gives is too little to hold its path; stores: what it
// carries (name, rounds left), `store` the one the secondary fire fires; bomb: that one is a bomb, `impact` where it
// would hit now (hasImpact: the ground is under its fall); clear: its
// height over the ground, or (ground: false, none under it) over the world's zero; keys: flown with the keyboard and
// mouse; aiming: in the air the mouse's aim steers it, `aim` the point it aims at, `path` the point it flies at.
struct PlayerJetReadout {
    float speed,throttle,clear,climb,hp,hpMax,load;
    bool air,stall,ground,keys,aiming;
    float aim[3],path[3];
    int stores,store;
    const char* storeName[6];
    int storeRounds[6];
    bool bomb,hasImpact;
    float impact[3];
};
bool PlayerJetHud(PlayerJetReadout* out) noexcept;
bool InstallPlayerJets() noexcept;                      // after InstallSub (it chains onto the 506 physics slot)

// The local player's human (plugin.cpp, from SeePlayer): the object, or nullptr when not seen for
// kPlayerHumanMs or no longer the same live player object.
unsigned char* PlayerHuman() noexcept;
}  // namespace crew

// The core modules' declarations (self-contained; every file that includes crew.h sees them as before).
#include "core.h"
#include "ground.h"
#include "heli.h"
#include "hud.h"
