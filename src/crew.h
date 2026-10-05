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
    float playerHeliStopSec=1.0f;// a stock heli the player flies: its horizontal speed's time constant (s; 0: stock)
    bool playerHeliGunSight=true;   // a stock heli the player flies or mans: our gun sight (boresight, impact pipper), its gun's red aim line hidden
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
    float subHullHp=100000.0f; // a submarine carrier's hull HP at the base tier (its SGO's is 30000), times its tier (25 at the highest); 0 = the game's
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
    bool playerJetCatch=true;       // ...and after ejecting, another of the same jet catches the player in the air
    int playerJetFlareKey=0x58;     // the key that drops a pair of flares ('X'; missile.cpp FlareDrop)
    int playerJetFlares=8;          // the pairs a player jet carries
    int playerJetChuteCutKey=0x58;  // the key that cuts the parachute after an ejection ('X')
    int playerJetTargetKey=0x51;    // ...and the key that locks the next target in the cone ('Q'; on a pad X)
    float playerJetMouseSpeed=1.0f; // ...how fast the mouse moves its aim
    bool playerJetMouseFlight=true; // ...the mouse's aim steers the plane once the mouse moves, the keys once pressed (off: the keys alone)
    float playerJetRamDamage=1.0f;  // a player jet's ram: the enemies round it take its kinetic energy's damage times this (0: none)
    bool playerJetGunSight=true;    // the aircraft the player flies: our gun sight (pipper, lead, boresight), the stock red aim lines hidden
    bool playerJetFlightHud=true;   // ...and its flight HUD: flight path marker, horizon and pitch ladder, heading tape, speed / altitude
    bool playerJetThreatHud=true;   // ...and the threats' directions (enemy locks, missiles coming for it) round the screen's centre
    int playerJetGearKey=0x47;      // the key that raises / lowers the landing gear ('G'; gear.cpp PlayerGear)
    int playerJetGearButton=0x40;   // ...and the pad button (the seat's button bits, docs/stores-re.md §4: 0x40 L3; 0 none)
    bool playerJetAll=true;         // the player can board every other aircraft of the plugin too (playerjet_kinds.h)
    int playerJetHailKey=0x48;      // ...and this key calls the nearest one down to them ('H'; 0: off)
    bool jetSound=true;             // the jets' engine sound (jetsound.cpp)
    float jetSoundVolume=1.0f;      // ...its volume, times the game's own for that sound
    bool drill=true;                // the drill tank's drill (drill.cpp): spun by the trigger, bites what it touches
    float drillMaxRpm=300.0f;       // ...its top RPM (what it shows and turns at)
    float drillSpinUpSec=1.8f;      // ...seconds from still to the top RPM, the trigger held
    float drillSpinDownSec=2.5f;    // ...seconds from the top RPM to still, let go
    float drillDamage=2000.0f;      // ...damage a second to an enemy it touches, at the top RPM (less in proportion)
    float drillBreak=600.0f;        // ...HP a second off a building or rock it bores into, at the top RPM
    float viewDistance=3000.0f;     // the near camera's far clip, m (view.cpp; stock 1000; 0: as the mission has it)
    float bigWorld=0.0f;            // the physics world +-this many m instead of +-3000 (bigworld.cpp), from the game's start;
                                    // 0: stock. At 10000 parked vehicles fell through the ground (2026-10-04): an experiment
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
void ResetShields() noexcept;     // shield.cpp
void ViewTick() noexcept;         // view.cpp: once a frame, the view distance raised
void ResetSubs() noexcept;        // subcarrier.cpp
void ResetLaser() noexcept;       // carrierlaser.cpp
void ResetPlayerJets() noexcept;  // playerjet.cpp
void ResetHud() noexcept;         // hud.cpp
void ResetLauncher() noexcept;    // launcher.cpp
void ResetHeliSight() noexcept;   // helisight.cpp
void ResetJetSound() noexcept;    // jetsound.cpp
void ResetMissiles() noexcept;    // missile.cpp
// What the plugin spawns is scaled to the mission's difficulty as a script's CreateFriend scales it (jet_spawn.cpp).
void LevelVehicle(unsigned char* vehicle) noexcept;
void ResetBigWorld() noexcept;    // bigworld.cpp
void BigWorldProbe() noexcept;
// m: the physics world's half size (3000 stock, ini BigWorld when raised): the plugin's walls stand inside it.
// The edge of the play area every flyer keeps inside (the user, 2026-10-05: "don't let them go out there; a buffer
// before it; past the line, coming back comes first"): the stock world's 2400 (600 m inside its +-3000), or the big
// map's ground's own edge, its BigWorld less the margin tools/make_bigmap.py adds past the last block (WORLD_MARGIN;
// selftest holds the two equal). It was the physics world less 600 m on the big map too: 150 m out over no ground,
// where the player slid along the wall below the ground with the heading snapping +-17 deg (2026-10-05 11:47).
constexpr float kBigWorldMargin=750.0f,kStockEdgeIn=600.0f;
// The buffer inside the edge: from here in the flyers are turned in, the more the nearer the edge (EdgeTurn).
constexpr float kEdgeBuffer=800.0f;
inline float WorldHalf() noexcept;
inline float PlayEdge() noexcept { return Cfg().bigWorld>3000.0f ? Cfg().bigWorld-kBigWorldMargin : 3000.0f-kStockEdgeIn; }
inline float WorldHalf() noexcept { return Cfg().bigWorld>3000.0f ? Cfg().bigWorld : 3000.0f; }    // bigworld.cpp: once a mission, the map's ground on a grid (log)
// The camera's view-projection (row vectors, the HUD's) as of the last frame drawn; false before one (hud.cpp).
bool LastViewProj(float* out) noexcept;
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
constexpr std::int32_t kTeamNeutral=3;   // hostile to nobody (playerjet.cpp: the parachute's canopy)
// The game's SetTeam 0x54EE70(object, team, registered). The team manager (*(image+0x20B2978)) keeps a set of objects
// per team (+0x38, 0x38 bytes a team) and finds an object's set by its team +0x314, when it adds it (0x5E0B70), takes
// it out (0x5E1C60: SetTeam, the object's destruction) or walks a team's objects. A write to +0x314 alone leaves the
// object in its old team's set while everything after looks in the new one's: freed, it stays in the old set, and the
// next walk of it reads freed memory (the crash at the next mission's start after a crewed 603_Flak, 2026-10-04).
// Every team change goes through SetTeam, the object's registration (+0x380 bit 6) kept as it is.
constexpr unsigned kSetTeam=0x54EE70;
constexpr std::size_t kObjectFlags=0x380;
// Never from inside a team walk's visitor (the board prompt, FindSeat): crew.cpp WithTeamField.
void SetObjectTeam(unsigned char* object,std::int32_t team) noexcept;
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
bool InstallShields() noexcept;                    // shield.cpp: the Shield Bearer's shield lets slow things through
// shield.cpp: whether a round (its candidate collector) passes a shield layer's body (slow: true, so it is left out)
bool ShieldLetsThrough(void* collector,std::uint32_t body) noexcept;
// shield.cpp: a fast vehicle's velocity (m/s) kept from crossing a hostile shield's face; the speed it lost
float ShieldBlock(const unsigned char* vehicle,float* vel) noexcept;
void ShieldVehicle(unsigned char* vehicle) noexcept;   // shield.cpp: the same for a vehicle with no plugin body
void CarrierFlames(const unsigned char* v,unsigned char* const* recs,float intensity,ULONGLONG ms) noexcept;
// booster.cpp: a jet's exhaust flames on its nozzles (by its mark), burning `intensity` (0..1), `burner` longer.
void JetFlames(const unsigned char* v,float intensity,bool burner,ULONGLONG ms) noexcept;
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
// jet_bay.cpp: a bite of the drill tank's drill: its charge fired by `by` straight from `from` at `at` with `damage`
// (its side's enemies, its kills, the map's buildings and rocks). False when not fired (not preloaded this mission).
bool DrillCharge(const unsigned char* by,const float* from,const float* at,float damage) noexcept;

// drill.cpp: the drill tank (EDF6VC_DRILL.SGO, docs/drill-re.md). DrillInput before the stock input (the player's
// trigger taken for the drill), DrillFrame after it (spin, pose, bites).
bool InstallDrill() noexcept;
bool IsDrillTank(const void* vehicle) noexcept;
void DrillInput(unsigned char* vehicle) noexcept;
void DrillFrame(unsigned char* vehicle) noexcept;
void ResetDrills() noexcept;
// The local player's drill (hud.cpp): its RPM, the top RPM, whether it touches something now. False with none.
struct DrillCue { float rpm,maxRpm; bool touching; };
bool PlayerDrillCue(DrillCue* out) noexcept;

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
// Any other aircraft of the plugin (playerjet_kinds.h) is the player's too: they may board it now (one of ours, low and
// slow enough: crew.cpp bumps its NPC pilot for them); the plugin holds it for them (they fly it, it comes down for
// them, catches them or waits where they left it): jet.cpp does not fly it then, crew.cpp does not crew it.
bool PlayerJetBoardable(const void* vehicle) noexcept;
bool PlayerJetHolds(const void* vehicle) noexcept;
void PlayerJetFrame(unsigned char* vehicle) noexcept;   // from every vehicle's input hook, after the stock step
// The jet the player flies now, for its cockpit readout (hud.cpp): game thread. False with none.
// The cockpit readout (hud.cpp): load in g; stall: all the wing gives is too little to hold its path; stores: what it
// carries (name, rounds left), `store` the one the secondary fire fires; bomb: that one is a bomb, `impact` where it
// would hit now (hasImpact: the ground is under its fall); clear: its
// height over the ground, or (ground: false, none under it) over the world's zero; keys: flown with the keyboard and
// mouse; aiming: in the air the mouse's aim steers it, `aim` the point it aims at, `path` the point it flies at.
// The fighter HUD's symbols for the aircraft the player flies (playerjet.cpp Sight / Threats, hud.cpp FighterHud).
constexpr int kMostThreats=6;
struct PlayerJetSymbols {
    float pos[3],nose[3];        // the aircraft and its body's nose (the guns fire along it: the boresight)
    float dir[3]; bool moving;   // its flight path's direction; moving: fast enough to have one (a hovering craft has not)
    bool gun;                    // a gun to sight: its rounds' `pipper` point at the range they are sighted for
    float pipper[3];
    bool lead,leadInRange;       // the picked store's target led for the guns (leadAt: aim the pipper there), within reach
    float leadAt[3],leadRange,gunRange;
    int threats;                 // what threatens it: where, and 2 a missile coming for it, 1 an enemy jet's lock on it
    float threatAt[kMostThreats][3];
    int threatKind[kMostThreats];
};
struct PlayerJetReadout {
    float speed,throttle,clear,climb,hp,hpMax,load;
    float rotate;                // m/s: the speed it can lift off from (the kind's rotate), for the takeoff cue
    bool air,stall,ground,keys,aiming;   // ground: there is ground under it (clear is its height over it), not on it
    bool pullUp;                 // in the air and about to hit the ground or what stands on it (PullUpNeeded)
    int threat;                  // 2 a missile homing on it, 1 an enemy's missile lock on it, 0 none
    int flares;                  // flare pairs left
    float aim[3],path[3];
    int stores,store;
    const char* storeName[6];
    int storeRounds[6];
    bool bomb,hasImpact;
    float impact[3];
    int lock;                    // the picked store's lock: 2 locked, 1 locking (lockProgress 0..1), 0 none (StoreLock)
    float lockAt[3],lockProgress;
    PlayerJetSymbols sym;        // the fighter HUD's (hud.cpp FighterHud)
};
bool PlayerJetHud(PlayerJetReadout* out) noexcept;
// launcher.cpp: the Katyusha's impact point (CCIP) while the player rides a vehicle whose seat 0 holds a launcher marked
// kMarkLofted (common/edf/weapon.h): where a rocket fired now comes down (reach: it does within its life), `ring` points
// round it its ripple's spread reaches (FireAccuracy's cone), the horizontal range, the flight in seconds and the
// launcher's elevation in degrees. LauncherFrame from every vehicle's input (game thread); PlayerLauncher the last one,
// false with none this moment.
constexpr int kLauncherRing=16;
struct LauncherReadout {
    float impact[3];
    float ring[kLauncherRing][3];
    int rings;
    float range,flight,elevation;
    bool reach;
};
// A round's arc as the game steps it (a frame: vel += drop, pos += vel; m/frame, m/frame^2) from `pos` for at most
// `frames` frames: the first ground a map ray finds along it (`hit`) and the frames it took. launcher.cpp.
bool RoundImpact(const float* pos,const float* vel,const float* drop,int frames,float* hit,float* took) noexcept;
void LauncherFrame(unsigned char* vehicle) noexcept;
bool PlayerLauncher(LauncherReadout* out) noexcept;
// Whether the player in `vehicle` sees our gun sight instead of the stock aim lines (crew.cpp AimLines): an aircraft
// the player-jet flight flies (playerjet.cpp), ini PlayerJetGunSight on.
bool PlayerJetOwnSight(const void* vehicle) noexcept;
// helisight.cpp: the gun sight of a stock helicopter (heli.cpp IsHelicopter, no plugin body) the player flies or mans
// (ini PlayerHeliGunSight): the seat's gun (crew.cpp HiddenAimGuns: its stock aim line hidden), `bore` the way its
// muzzle points (a direction), `pipper` where a round fired now first hits the map along its real arc (hit), else
// where it is at the end of its life; `range` m from the muzzle to it. HeliSightFrame from every vehicle's input (game
// thread); PlayerHeliSight the last one, false with none this moment (hud.cpp HudPublish, the snapshot carries it).
struct HeliSightReadout { float bore[3],pipper[3],range; bool hit; };
bool PlayerHeliOwnSight(const void* vehicle) noexcept;
void HeliSightFrame(unsigned char* vehicle) noexcept;
// netprobe.cpp: Debug=1, online only: once a second per helicopter-class vehicle, which machine runs it and how its
// pose replication stands (the NET lines, docs/online-re.md). Reads only.
void NetProbe(unsigned char* vehicle) noexcept;
bool PlayerHeliSight(HeliSightReadout* out) noexcept;
// crew.cpp: the seat's weapons whose stock aim line AimLines has hidden now (the walk it hides them by), at most
// `most`; how many.
int HiddenAimGuns(const unsigned char* seat,const unsigned char** out,int most) noexcept;
void PlayerEjectTick() noexcept;   // playerjet.cpp: the player's ejection and parachute, a frame
void PreloadPlayerJets() noexcept; // playerjet.cpp: at a mission's start, the player jets' SGOs (the catch)
namespace jet { bool SpawnReady() noexcept; bool ModFileThere(const wchar_t* file) noexcept; bool LockingOn(const void* target) noexcept;
// jet.cpp: the jets locking on to `target` lose their lock with `chance` each (a flare drop); how many did
int BreakLocks(const void* target,float chance) noexcept;
// jet.cpp: the jets with `target` in their missile lock now (LockingOn's): where they are, at most `most`; how many
int LockersOf(const void* target,float (*at)[3],int most) noexcept; }
// missile.cpp: a guided round now homing on a point within `radius` m of `at` (its lock point there)
bool MissileHoming(const float* at,float radius) noexcept;
// missile.cpp: MissileHoming's rounds: where they are, at most `most`; how many (all of them, past `most` too)
int MissilesHomingAt(const float* at,float radius,float (*pos)[3],int most) noexcept;
// missile.cpp: flares. A flare dropped by `owner` at `at` (m/s `vel`): the rounds homing there may take it for their
// target. FlaresStep moves them once a frame; FlaresOf: `owner`'s burning flares, at most `most` (their places, speeds).
// `nose`: the jet's nose (the aspect); `pairStart`: the first flare of a drop (each drop is judged once a round).
void FlareDrop(const void* owner,const float* at,const float* vel,const float* nose,bool pairStart) noexcept;
void FlaresStep() noexcept;
int FlaresOf(const void* owner,float (*at)[3],float (*vel)[3],int most) noexcept;
// booster.cpp: the flares' fire, drawn as Booster flames on `v` (their owner) at `at`, trailing against `vel`.
void FlareFlames(const unsigned char* v,const float (*at)[3],const float (*vel)[3],int n,ULONGLONG ms) noexcept;
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
