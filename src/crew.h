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
    bool jetPilot=true;        // jets (edf6tr_jet_* SGOs) are flown by the plugin
    DWORD jetFuelSec=120;      // a jet withdraws after this long in the air
    DWORD jetSortieSec=60;     // ...one launched by an airstrike takeover
    bool jetAirRaider=true;    // the Air Raider's bomber calls send jets instead, and its call weapons (airstrike.cpp) work
    bool jetMissionStrike=true;// the missions' strafing-plane airstrikes (DemoAirStrike) send jets instead
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
enum class JetRole { strike, fighter, interceptor, multirole, carrier };
bool JetLaunch(JetRole role,const float* from,const float* heading,const float* target,DWORD fuelSec,const void* source,
               bool escort=false) noexcept;
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
}  // namespace crew
