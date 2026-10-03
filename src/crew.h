// EDF6VehicleCrew: shared layout, config and helpers.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

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
    float heliStandoff=80.0f;  // unused since the attack runs (kept so old ini files still load)
    bool heliAvoid=true;       // helis steer and climb clear of terrain and buildings (map rays)
    float heliFireHeight=25.0f;// engaged, its strafing runs fly this far above the target
    float heliFireCone=4.0f;   // degrees between the nose (pitch included) and the target it still fires at
    bool heliMissile=true;
    DWORD heliMissileMs=4000;  // minimum gap between missiles
    float heliMoveGain=0.04f;  // stick per metre off the goal
    float heliBrakeGain=0.12f; // stick per m/s of speed (damping)
    float heliClimbGain=0.08f; // rotor speed per m/s of climb-rate error
    float heliHoverLearn=0.03f;// how fast it learns the hover rotor speed
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
    bool vehicleHud=true;      // HP / ammo / fuel over the nearest NPC-driven friendly vehicles, the carriers' panel (hud.cpp)
    int vehicleHudCount=6;     // ...over at most this many of them (nearest first)
    float vehicleHudRange=500.0f;// ...within this many metres of the player
};
extern Config cfg;

// --- EDF.dll layout ---
// GameObject: weak-this at +0x28 (object) / +0x30 (control block, use count at +8)
constexpr std::size_t kSelf=0x28,kSelfCtrl=0x30;
// Vehicle
constexpr std::size_t kMatrix=0x60,kPosition=0x90,kDead=0x2E8,kTeam=0x314;
// Teams (mission AsCommon.h): player 0, enemy 1, friend 2, neutral 3, vehicle 5 = nobody's vehicle,
// which anyone may board (CanRideSeat skips the team test for it).
constexpr std::int32_t kTeamVehicle=5;
constexpr std::size_t kSeats=0x608,kSeatCount=0x618,kSeatStride=0x340;
// Seat: rider object / its weak_ptr control block (occupied while the use count is non-zero)
constexpr std::size_t kSeatRider=0x260,kSeatRiderCtrl=0x268;
// Human: pad / player-controlled (the test 0x572EFF and 0x673AC2 make before reading a pad),
// the vehicle it is in (weak_ptr object +0x1548, control block +0x1550)
constexpr std::size_t kHumanPad=0x340,kHumanPlayer=0x354,kHumanVehicleCtrl=0x1550;
// The rider RideAi (VehicleBase slot 50, 0x633030) seats: it drives through the vehicle AI
constexpr unsigned kDummyRiderVtable=0x17D7320;
// VehicleBase virtual slots
constexpr std::size_t kSlotFindSeat=49,kSlotRideAi=50,kSlotInput=55;
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

template<class T> T At(const void* base,std::size_t offset) noexcept {
    T value;std::memcpy(&value,static_cast<const unsigned char*>(base)+offset,sizeof(T));return value;
}
template<class T> void Put(void* base,std::size_t offset,T value) noexcept {
    std::memcpy(static_cast<unsigned char*>(base)+offset,&value,sizeof(T));
}

void Log(const char* format,...) noexcept;
// Forced test-range loadout (loadout.cpp); off unless EDF6TestRange.loadout.ini says Enabled=1.
bool InstallLoadout(const wchar_t* pluginIni) noexcept;
void ReloadConfigIfChanged() noexcept;
bool Matches(std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept;
bool PatchVtableSlot(void** slot,void* expected,void* replacement) noexcept;

// What sits in a seat.
enum class Rider { none, dummy, player, other };
Rider SeatRider(const unsigned char* seat) noexcept;
unsigned char* SeatAt(unsigned char* vehicle,unsigned index) noexcept;
unsigned SeatCount(const unsigned char* vehicle) noexcept;
bool IsPlayer(const unsigned char* human) noexcept;

// The player as last seen (on foot through the prompt visitor, or riding through a vehicle input).
struct PlayerFix { float pos[3]; std::int32_t team; ULONGLONG at; };
extern PlayerFix player;
void SeePlayer(const float* pos,std::int32_t team) noexcept;

// crew.cpp
bool InstallCrew() noexcept;           // prompt + seat hooks, at load
void CrewFrame(unsigned char* vehicle) noexcept;   // from every vehicle's input hook

// heli.cpp
bool IsHelicopter(const void* vehicle) noexcept;
void HeliCrewed(const void* vehicle) noexcept;      // the plugin seated an NPC pilot: it flies this heli
void HeliFrame(unsigned char* vehicle) noexcept;   // after the stock input, NPC-crewed helicopters only
bool CheckHeliProfile() noexcept;
// Shared with jet.cpp: map ray (metres a->b to terrain/buildings, -1 with none; `hit` gets the point),
// every enemy lock point of `vehicle`'s side.
float MapRay(const float* a,const float* b,float* hit) noexcept;
// Whether there is water at (x, z) (docs/water-re.md): the game's own water areas; `surface` gets the
// highest surface there. unknown: the probe is off (EDF.dll differs) or the map's areas are not there.
enum class Sea { unknown, land, water };
Sea SeaAt(float x,float z,float* surface) noexcept;
using EnemyVisitor=void(*)(void* ctx,const void* object,const float* aim);
bool VisitEnemies(const unsigned char* vehicle,EnemyVisitor visit,void* ctx) noexcept;
bool VisitEnemiesOf(std::int32_t team,EnemyVisitor visit,void* ctx) noexcept;   // the enemies of a side
// Whether `point` is within `radius` of the segment from->to (between its ends).
bool NearLine(const float* from,const float* to,const float* point,float radius) noexcept;
// Whether a burst from->to would pass by the player (as the helis' guns check) or a jet the plugin flies
// other than `self` (JetInLine). Other friends are hit as the stock game hits them.
bool FriendInLine(const float* from,const float* to,const void* self) noexcept;

// jet.cpp
bool IsJet(const void* vehicle) noexcept;          // a 506 body from an edf6tr_jet_* SGO
bool JetInLine(const float* from,const float* to,const void* self) noexcept;   // a wingman in the way (no pass-through)
void JetFrame(unsigned char* vehicle) noexcept;    // from HeliFrame, NPC-crewed jets only
ULONGLONG GameMs() noexcept;   // the game clock (crew.cpp): stops while paused or loading
void JetReap(const void* self) noexcept;           // deletes withdrawn jets; call from another object's update
void HeliReap(const void* self) noexcept;          // ...and called helis that have left (heli.cpp)
bool InstallJets() noexcept;
void PreloadJets() noexcept;                       // from the mission's player preload
// A jet made at run time at `from`, flying along `heading` to work round `target`; false when it cannot
// be made (not preloaded this mission, profile mismatch): the caller keeps the stock behaviour then.
// `source`: what launched it (any fixed address per kind of source); jets from one source in a row fly
// as one flight, whose rounds pass through each other.
// `role`: the jet.cpp Role it flies as (same order); one whose SGO is not installed flies as a fighter.
// `escort`: it works round the player (while seen), not round `target`.
// blastCarrier / dollCarrier: a carrier whose drones blow up next to the enemy (the doll ones carrying a
// singing, dancing hololive doll); after `carrier`, as they are no role of their own.
enum class JetRole { strike, fighter, interceptor, multirole, carrier, blastCarrier, dollCarrier };
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
// A heli the Air Raider called (heli.cpp): `guard` holds over `post` and fights round it, else it follows
// the player; its weapons are not refilled, and out of ammo, after `fuelSec` or badly damaged it flies off
// away from the player and is deleted far from them.
void HeliCalled(unsigned char* vehicle,bool guard,const float* post,DWORD fuelSec) noexcept;
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
// Where a vehicle weapon's barrel is and points (the mean of its muzzles' frames, heli.cpp).
bool GunBarrel(const unsigned char* v,const unsigned char* weapon,float* pos,float* dir) noexcept;

// airstrike.cpp
bool InstallAirstrikes() noexcept;
void CallPick(int step,wchar_t* out,std::size_t size) noexcept;   // airstrike.cpp
void StartCallPicker() noexcept;                                    // overlay.cpp

// ground.cpp: the Depth Crawler (502), which has no stock AI
bool IsGroundRobo(const void* vehicle) noexcept;
void GroundFrame(unsigned char* vehicle) noexcept;   // after its stock pre-update, NPC-driven crawlers only
bool CheckGroundProfile() noexcept;
// subcarrier.cpp: the submarine carrier (潜水母艦, docs/subcarrier-re.md), a 506 body from EDF6VC_SUB_CARRIER.SGO
// (tools/make_sub.py) driven by the plugin: it sits surfaced, follows the player at a ship's pace, turns its bow
// on the nearest enemy, fires its turret guns and homing missiles, reloads aboard; its HP shows as a follower gauge.
bool IsSub(const void* vehicle) noexcept;         // a 506 body with the carrier's mark
void SubFrame(unsigned char* vehicle) noexcept;   // from HeliFrame, NPC-crewed carriers only
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

// hud.cpp: the vehicle HUD (docs/hud-re.md), drawn from the follower gauge's call (subcarrier.cpp GaugeHook).
bool InstallHud() noexcept;                         // at load: checks the draw and text functions it calls
void HudSee(unsigned char* vehicle) noexcept;       // from every vehicle's input hook (game thread): a readout's data
// A carrier's panel (subcarrier.cpp fills it every draw from its game-thread copies): the hull, each deck part.
struct CarrierPanel {
    float hull,hullMax;
    int parts;
    struct Part { const char* name; float hp,max,repairSec; bool down; } part[4];
};
// From the follower gauge's draw (any thread): the readouts and `count` carrier panels. `viewProj`, `ctx` and
// `viewport` as the gauge drawer 0x804300 gets them.
void HudDraw(const float* viewProj,void* ctx,const void* viewport,const CarrierPanel* panels,int count) noexcept;
// What jet.cpp flies a jet as (game thread): its role's name, seconds of fuel left (-1: none, a carrier's drone),
// a carrier's drone launches left (-1: not a carrier), whether it is withdrawing; false when it does not fly it.
struct JetHudInfo { const char* role; float fuelSec; int drones; bool leaving; };
bool JetHud(const void* vehicle,JetHudInfo* out) noexcept;
// A called heli's fuel (heli.cpp, game thread): seconds until it flies off (0: leaving); false with no limit.
bool HeliFuel(const void* vehicle,float* sec) noexcept;

// The local player's human (plugin.cpp, from SeePlayer): the object, or nullptr when not seen for
// kPlayerHumanMs or no longer the same live player object.
unsigned char* PlayerHuman() noexcept;
// Sea rescue (heli.cpp): from every flown helicopter's frame (HeliFrame), at most once per game frame.
void RescueTick() noexcept;
}  // namespace crew
