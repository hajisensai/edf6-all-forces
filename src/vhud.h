// The stock vehicles' HUD (vhud.cpp gathers it, hud.cpp StockVehicleHud draws it; docs/hud-re.md §7) and the round
// models it shares with the helis' sight (rounds.cpp, rounds.h). Included by crew.h.
#pragma once
#include <Windows.h>
#include <cstdint>

namespace crew {
// --- rounds.cpp: a weapon's round as the game will fly it ---
// arc: a ballistic round (rounds::Arc); rocket: MissileBullet01 flying straight (rounds::Motor); homing: it steers at
// what its weapon locks (MissileBullet01 / 02 guided, HomingLaserBullet01): no path to show, its lock is.
enum class RoundKind : std::uint8_t { none, arc, rocket, homing };
// A weapon's label on the HUD (by its round's class: rounds.cpp kLabels), "WPN" for a class not in the list.
struct RoundModel {
    RoundKind kind;
    bool lobbed;                 // a grenade / mortar class: its point is drawn as the artillery's cross
    const char* label;
    float speed,factor;          // AmmoSpeed m/frame, AmmoGravityFactor
    std::int32_t alive;          // AmmoAlive frames
    float accel,top,keepInh,keepOwn;   // the motor (rocket): Ammo_CustomParameter[4], [6], [7][1], [7][2]
    std::int32_t ignite;               // [7][0]
};
bool InstallRounds() noexcept;   // at load: the reads below (their EDF.dll code checked); off: every round an arc
// Whether the weapon status fields (rounds, magazine, reload: 0x692100) are where vhud.cpp reads them.
bool WeaponStatusOk() noexcept;
// The round of weapon `w` (under the caller's __try); false when it cannot be read.
bool ReadRound(const unsigned char* w,RoundModel* out) noexcept;
// Where a round of `w` (`m`) fired now from `pos` along the unit `dir` first meets the map (terrain and buildings: heli.cpp
// MapRay), at most `reach` m away: `at` and the seconds it flies there; false (`at` where it ends: its life over or past
// `reach`) when it meets none. The shooter's velocity it takes on is the weapon's (+0x190 x AmmoOwnerMove / 60).
bool RoundLands(const unsigned char* w,const RoundModel& m,const float* pos,const float* dir,float reach,float* at,float* sec) noexcept;

// --- vhud.cpp: the stock vehicle the player rides ---
// crew.cpp: the vehicle's class as crew.cpp hooks it ("403_Tank", "Car"...), "vehicle" for none of them.
const char* VehicleClassName(const void* vehicle) noexcept;
// crew.cpp: whether `vehicle` is of a class crew.cpp knows (kClasses: every stock vehicle class and the 506 body).
bool KnownVehicle(const void* vehicle) noexcept;
// The selected store of the player's seat, for the HUD to bracket: whoever lets the player pick one (feat/ov-payload's
// payload switch) calls this every frame from the game thread with the weapon's index in the seat's holder list
// (seat+0xC8, the order the HUD lists them in), -1 for none. Not called for a few frames: no selection is shown.
void SetStockSelectedStore(const void* vehicle,unsigned seat,int store) noexcept;

constexpr int kStockArms=6,kStockThreats=6;
struct StockArm {
    char label[12];
    std::int32_t ammo,ammoMax;   // rounds left and the magazine (AmmoCount)
    float reload;                // 0..1 share reloaded while it reloads (ammo 0); 1 not reloading
    float reloadSec;             // seconds left of it, < 0 unknown
    bool canReload;              // ReloadTime >= 0: it reloads once empty (else EMPTY is the end)
    RoundKind kind;
    bool lobbed,lofted;          // lofted: the Katyusha's launcher (launcher.cpp draws its point)
    bool aimed;                  // `bore` and the rest below were worked out this frame
    bool hit;                    // `at` is where it meets the map; else where it ends (dim)
    float bore[3],at[3],range,flight;
    int lock;                    // homing: 2 locked / 1 locking (lockProgress) on `at`, 0 none (LockonRange `range`)
    float lockProgress;
};
struct StockHudReadout {
    char kind[16];
    unsigned seat;               // the player's seat (0 the driver's)
    bool heli;                   // a stock heli (its sight and HUD are helisight.cpp's / HeliHud's: only the stores here)
    float pos[3],hull[3],aim[3],look[3];   // hull: its nose; aim: its first weapon's muzzle way; look: the camera's
    bool aimOk,lookOk;
    float speed;                 // m/s, level (its own position's change)
    float hp,hpMax;
    int arms,selected;           // selected: SetStockSelectedStore's (-1 none)
    StockArm arm[kStockArms];
    int threats;                 // 2 a missile homing on it, 1 a jet's lock (missile.cpp, jet.cpp: as the jets' threat ring)
    float threatAt[kStockThreats][3];
    int threatKind[kStockThreats];
};
// Whether the player in `vehicle` sees this HUD's sights in place of the stock aim lines (crew.cpp AimLines): a stock
// vehicle (no plugin body, not a heli: those have their own), ini StockVehicleHud on.
bool PlayerStockOwnSight(const void* vehicle) noexcept;
bool HudReady() noexcept;   // hud.cpp: its quads can be drawn (InstallHud): no line is hidden for a HUD that cannot show
void StockHudFrame(unsigned char* vehicle) noexcept;   // every vehicle's input (crew.cpp InputHook), after AimLines
bool PlayerStockHud(StockHudReadout* out) noexcept;    // the last frame's, false with none (hud.cpp HudPublish)
void ResetStockHud() noexcept;                         // mission.cpp MissionStart
}  // namespace crew
