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
    bool heliFire=true;
    float heliStandoff=80.0f;  // with the player aboard: how close it closes on its target
    float heliFireCone=10.0f;  // degrees off the nose it still fires at
    bool heliMissile=true;
    DWORD heliMissileMs=4000;  // minimum gap between missiles
    float heliMoveGain=0.04f;  // stick per metre off the goal
    float heliBrakeGain=0.12f; // stick per m/s of speed (damping)
    float heliClimbGain=0.08f; // rotor speed per m/s of climb-rate error
    float heliHoverLearn=0.03f;// how fast it learns the hover rotor speed
    DWORD heliLandMs=6000;     // it lands by a player who stood still this long; 0 = never lands
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
}  // namespace crew
