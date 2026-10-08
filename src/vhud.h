// The stock vehicles' HUD (vhud.cpp gathers it, hud.cpp StockVehicleHud draws it; docs/hud-re.md §7) and the round
// models it shares with the helis' sight (rounds.cpp, rounds.h). Included by crew.h.
#pragma once
#include <Windows.h>
#include <cstdint>
#include "gunsight.h"
#include "roundaim.h"

namespace crew {
// --- rounds.cpp: a weapon's round as the game will fly it ---
// arc: a ballistic round (rounds::Arc); rocket: MissileBullet01 flying straight (rounds::Motor); homing: it steers at
// what its weapon locks (MissileBullet01 / 02 guided, HomingLaserBullet01): no path to show, its lock is.
enum class RoundKind : std::uint8_t { none, arc, rocket, homing };
// Weapon behavior/presentation is separate from its projectile integrator: a beam
// factory may still advance straight segments using the core arc update.
enum class WeaponStyle : std::uint8_t { projectile, laser, beam, maser };
constexpr bool EnergyWeapon(WeaponStyle style) noexcept { return style!=WeaponStyle::projectile; }
// A weapon's label on the HUD (by its round's class: rounds.cpp kLabels), "WPN" for a class not in the list.
struct RoundModel {
    RoundKind kind;
    WeaponStyle style;
    bool lobbed;                 // a grenade / mortar class: its point is drawn as the artillery's cross
    const char* label;
    const char* rtti;            // its factory's class (".?AVFactory@SolidBullet01Rail@@"), nullptr for one not in the list
    float speed,factor;          // AmmoSpeed m/frame, AmmoGravityFactor
    std::int32_t alive;          // AmmoAlive frames
    float accel,top,keepInh,keepOwn;   // the motor (rocket): Ammo_CustomParameter[4], [6], [7][1], [7][2]
    std::int32_t ignite;               // [7][0]
    bool pluginMotor;                 // unguided CP[8]=1000000 / CP[9]=4242: missile.cpp owns its burn and coast
    float burn;                       // CP[3][0], frames (the runtime clamps to 0..3600)
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
// An arc round of `w` (`m`, RoundKind::arc) as roundaim.h flies it (the same step and numbers RoundLands flies it by:
// AmmoSpeed, the world gravity x AmmoGravityFactor / 3600, AmmoOwnerMove, AmmoAlive) and the shooter's velocity it takes a
// share of (m/s, the weapon's +0x190). False for another kind, or with the gravity unread.
bool ArcRoundOf(const unsigned char* w,const RoundModel& m,roundaim::Round* round,float* shooter) noexcept;

// --- vhud.cpp: the stock vehicle the player rides ---
// crew.cpp: the vehicle's class as crew.cpp hooks it ("403_Tank", "Car"...), "vehicle" for none of them.
const char* VehicleClassName(const void* vehicle) noexcept;
// crew.cpp: whether `vehicle` is of a class crew.cpp knows (kClasses: every stock vehicle class and the 506 body).
bool KnownVehicle(const void* vehicle) noexcept;
// The selected store of the player's seat, for the HUD to bracket: whoever lets the player pick one (feat/ov-payload's
// payload switch) calls this every frame from the game thread with the weapon's index in the seat's holder list
// (seat+0xC8, the order the HUD lists them in), -1 for none. Not called for a few frames: no selection is shown.
void SetStockSelectedStore(const void* vehicle,unsigned seat,int store) noexcept;

constexpr int kStockArms=8,kStockThreats=6;
struct StockArm {
    WeaponStyle style;
    wchar_t name[32];            // installed weapon name, not just its projectile category
    char label[12];
    std::int32_t ammo,ammoMax;   // rounds left and the magazine (AmmoCount)
    float reload;                // 0..1 share reloaded while it reloads (ammo 0); 1 not reloading
    float reloadSec;             // seconds left of it, < 0 unknown
    bool canReload;              // ReloadTime >= 0: it reloads once empty (else EMPTY is the end)
    RoundKind kind;
    bool lobbed,lofted;          // lofted: the Katyusha's launcher (launcher.cpp draws its point)
    bool aimed;                  // `bore` and the rest below were worked out this frame
    bool hit;                    // `at` is where it meets the map
    bool ranged,inReach;         // optional articulated-gun target lead; independent of physical `at`/`hit`
    float bore[3],at[3],lead[3],range,flight;   // first real barrel path; at/range/flight never replaced by target selection
    bool physicalOnly=true,coFired=false; // fixed/partial/unknown mount; this weapon shares the active trigger
    float targetRange=0;         // optional articulated-gun lead cue, never replaces physical path data above
    int paths=0;
    roundaim::Impact path[roundaim::kSightPaths]{};
    int lock;                    // homing: 2 locked / 1 locking (lockProgress) on `at`, 0 none (LockonRange `range`)
    float lockProgress;
    gunsight::Ladder ladder;     // a direct-fire arc gun's range ladder (gunsight.h; no ticks: none), hud.cpp GunReticle
};
struct StockHudReadout {
    char kind[16];
    unsigned seat;               // the player's seat (0 the driver's)
    bool heli;                   // a stock heli (its sight and HUD are helisight.cpp's / HeliHud's: only the stores here)
    float pos[3],hull[3],aim[3],look[3];   // hull: its nose; aim: its first weapon's muzzle way; look: the camera's
    bool aimOk,lookOk;
    float speed;                 // m/s, level (its own position's change)
    float hp,hpMax;
    float zoom;                  // the sight's magnification (sightzoom.cpp SightZoomNow: 1 none)
    int stab;                    // the seat's gun stabilizer (stab.cpp StabState): 1 holding, 2 outrun by the hull, 0 none
    FuelReading fuel;            // its fuel tank (a bike's; a heli's is HeliStrip's, PlayerHeliReadout), not among the arms
    int arms,selected;           // selected: secondary payload/list selection (-1 none)
    int sight=-1;               // independently selected actual primary/secondary fire-control weapon
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

// --- stockgauge.cpp: the stock weapon gauges and the fuel tank (docs/hud-re.md §9) ---
// At load: the stock vehicle weapon gauge's update (HUiHudWeapon slot 1) chained, the FuelTank reads checked; each part
// off on its own when its EDF.dll code is not as expected.
bool InstallStockGauges() noexcept;
// hud.cpp HudPublish, once a frame: whether the HUD it publishes lists the weapons of the vehicle the player is in (the
// jets' and rotor craft's stores line, a stock heli's HeliStrip, StockBlock). With HideStockGauges the stock gauge's
// panels (a weapon a panel, the fuel tank's FUEL too) are taken off the screen for that vehicle, put back when it stops.
void SetStockGaugeCover(bool lists) noexcept;
void ResetStockGauges() noexcept;   // mission.cpp MissionStart
// The fuel tank of `vehicle` (a helicopter's, the 506 bodies' of the plugin's aircraft, the 503 / 511 bikes'), its burn
// measured over the game clock; game thread. False (out->ok false) with no tank or the reads off.
bool FuelGauge(const void* vehicle,FuelReading* out) noexcept;
// Whether a tank reads low: under kLowFuelShare, or under kLowFuelSec at its burn.
constexpr float kLowFuelShare=0.15f,kLowFuelSec=60.0f;
constexpr bool FuelLow(const FuelReading& f) noexcept { return f.ok && (f.share<kLowFuelShare || (f.sec>=0.0f && f.sec<kLowFuelSec)); }
// Whether `w` is a vehicle's fuel tank (its weapon file V_FUEL*): no weapon, listed in every seat (payload.cpp's test).
bool IsFuelTank(const unsigned char* w) noexcept;
}  // namespace crew
