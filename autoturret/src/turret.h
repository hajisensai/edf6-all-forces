// Shared by the flak turret (plugin.cpp) and the tank gunners (gunner.cpp): the EDF.dll layout,
// the config, the enemy scan, the ballistic solve and the turret axis control.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md. The layout facts
// and the memory / patch / seat code EDF6VehicleCrew shares are in common/ (edf6common).
#pragma once
#include <Windows.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "edf/layout.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include "edf/seat.h"
#include "edf/weapon.h"
#include "edf/aimlink.h"

namespace autoturret {
using edf::At;
using edf::Put;
using edf::Readable;
using edf::kMatrix;
using edf::kPosition;
using edf::kDead;
using edf::kTeam;
using edf::kSeats;
using edf::kSeatStride;
using edf::kSelfCtrl;
using edf::VehicleInputFn;
extern unsigned char* image;

struct Config {
    bool enabled=true;
    bool debug=false;
    float gain=3.0f;           // stick input per radian of aim error, clamped to +-1
    float yawSign=1.0f;        // axis angle = sign * geometric angle + offset
    float yawOffset=0.0f;
    float pitchSign=-1.0f;     // the pitch axis is negative-up (Kepler: -60 deg up .. +5 deg down)
    float pitchOffset=0.0f;
    float pivotHeight=2.5f;    // turret pivot above the vehicle origin, metres
    float airHeight=12.0f;     // a target this far above the pivot counts as air
    bool lead=true;            // aim ahead of moving targets, using the gun's own AmmoSpeed
    float trackRange=0.75f;    // auto-aim only at targets within this share of the gun's range
    bool feedForward=true;     // drive the turret at the aim point's own angular rate, not only on the error
    float slewWeight=250.0f;   // metres a new target may be farther per radian it saves the turret turning
    float dragDeadzone=0.3f;   // aim stick past this aims by hand while held (0 = never)
    DWORD dragDropMs=3000;     // the target dragged away from is not picked again for this long
    bool fuse=true;            // set each round's lifetime (its burst point) to the target's range
    float fuseBias=0.0f;       // frames added to the computed fuse
    int fuseMin=4;             // never burst closer than this many frames
    float proximity=5.0f;      // burst when a round passes this close to a target (0 = off); keep below AmmoExplosion
    bool contact=true;         // burst the moment a round sticks to something or stops
    float burstVisual=2.5f;    // burst effect size as a multiple of the stock one (AmmoExplosion/5)
    // Tank gunners (gunner.cpp): the side guns of the Titan and the Ranger's gunner-seat tanks
    bool gunnerAi=true;        // an empty gunner seat aims and fires its gun by itself
    bool gunnerAssist=true;    // a player in a gunner seat gets the auto-aim (keeps the trigger)
    float gunnerRange=300.0f;  // metres; never farther than the gun's own range
    float gunnerCone=0.02f;    // rad; the AI fires once the barrel is this close to the aim (or the target's size)
    float gunnerMinDistance=15.0f;  // metres from the muzzle; the AI holds fire on anything closer
    float gunnerYawSign=1.0f;  // side-gun axis angle change per radian of geometric turn
    float gunnerPitchSign=-1.0f;
    // The player's turret (designate.cpp): the mode it starts in (0 auto-aim, 1 lead circle) and the bindings that flip
    // it and lock the target nearest the view (virtual-key codes, pad button bits; 0 = none), the lock's cone (deg off
    // the view) and range (m, 0 = the gun's), how long a held lock binding takes to let the lock go.
    int aimMode=0;
    int modeKey=0x5A,modeButton=0;      // Z; no pad button (L3 is EDF6VehicleCrew's FreeLookButton in the same seats)
    int lockKey=0x51,lockButton=0x04;   // Q, X (the jets' next-target bindings)
    float lockCone=20.0f;
    float lockRange=0.0f;
    DWORD lockClearMs=600;
};
extern Config cfg;

// --- EDF.dll layout ---
constexpr unsigned kFlakVtable=0x17DC620,kFlakInput=0x621460;   // Vehicle603_Flak, slot 55
// Vehicle: the turn input the input slot writes (seat i at +0x2AA0 + i*0x10: yaw, pitch)
constexpr std::size_t kTurn=0x2AA0,kTurnStride=0x10;
// Seat (stride 0x340): weapon holders, aim controller, rider stick
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kStick=0x2D0;
constexpr std::size_t kHolderWeapon=0x10;
constexpr std::uint64_t kMaxHolders=8;
// The seat's aim and its axes: common/edf/layout.h
using edf::kSeatAim; using edf::kAimAxes; using edf::kAxisStride; using edf::kAxisMin; using edf::kAxisMax; using edf::kAxisAngle;
// Weapon lock-on profile, filled from the SGO at 0x68D4A0: LockonType, LockonTargetType, LockonRange.
constexpr std::size_t kLockonType=0x6B0,kLockonTargetType=0x6B4,kLockonRange=0x6D0;
// Our guns' marker: LockonTargetType set to one of the mod's marks (common/edf/weapon.h: kMarkAir, kMarkGround,
// kMarkLofted) by tools/build.py and tools/make_*.py. The only stock reader of the field is the lock query
// (0x696792, which maps it to a lock class), and our guns never lock: LockonType 0 (fire-start 0x690BB0 fires a
// type 0 gun with or without a lock) and LockonRange 0. So the mark changes nothing in the stock game: with the
// plugin off, refused or deleted, the guns fire as any no-lock gun.
// kMarkAir: an anti-air gun (air targets first; GrenadeBullet01 rounds get the fuses). kMarkGround: a
// ground-attack gun (the Bohr's launchers: ground targets first, the low arc, stock impact fuse). kMarkLofted: a
// ground-attack launcher on the high arc (the Katyusha: ground targets first, its rounds lobbed).
using edf::kMarkAir;
using edf::kMarkGround;
using edf::kMarkLofted;
// Data built before 0.3.0 marked our guns with LockonType 4 (LockonTargetType 1: ground) and needed the fire
// gate patched (LegacyFireGate in plugin.cpp). Still recognized so an old install keeps working with this
// DLL; tools/build.py install rewrites the data with the mark above. Drop with the next data break.
constexpr std::int32_t kLegacyLockonType=4,kLegacyGroundTargetType=1;
enum class Mark { none, air, ground, lofted };
// Which of our guns `weapon` is; `legacy` (optional) says it carries the pre-0.3.0 mark.
Mark GunMark(const unsigned char* weapon,bool* legacy=nullptr) noexcept;
// Weapon ammo parameters, copied into each round when it is fired; speed is metres per frame.
// Never written by the plugin (a round's own lifetime is set on the round, plugin.cpp Fuze).
constexpr std::size_t kAmmoSpeed=0x894,kAmmoAlive=0x898,kAmmoDamage=0x89C,kAmmoExplosion=0x8B0,kAmmoGravity=0x8E0;
// The round factory AmmoClass resolved to (0x68D53A); fire (0x6970A5) spawns rounds through it.
// Only GrenadeBullet01 rounds can be fused, so only guns with its factory get a time fuse.
constexpr std::size_t kAmmoFactory=0x7F8;
constexpr unsigned kGrenadeFactoryVtable=0x17A1688;
// World gravity (edf::WorldGravity, common/edf/weapon.h): the game's own vehicle aim (0x622706) reads it and
// drops a round by AmmoGravityFactor x gravity / 3600 metres per frame^2 (0x622B65).
constexpr float kFramesPerSecondSq=3600.0f;
// Lock-target registry: global pointer -> object holding std::list<{raw T*, weak_ptr}> at +8.
// Each T is one lock point of an object: +0 kind (0 = enemy kind), +8 the object, +0x10 its aim
// point (rewritten every frame from the bone matrix by 0x6C7700), +0x29 valid, +0x2A lockable.
// The lock query (0x696710) walks the same list; it is only touched on the game thread.
constexpr std::size_t kRegistry=0x20B2AB0,kRegList=0x8,kNodeTarget=0x10;
constexpr std::size_t kTargetObject=0x8,kTargetAim=0x10,kTargetValid=0x29,kTargetLockable=0x2A;
// Team relations: manager -> array (stride 0x38) per team -> int relation[team]; 2 = enemy.
constexpr std::size_t kTeams=0x20B2978,kTeamArray=0x38,kTeamStride=0x38,kTeamRelation=0x18;
constexpr std::int32_t kEnemyRelation=2,kMaxTeam=64;
constexpr int kMaxNodes=8192;
constexpr float kPi=3.14159265f;

// The turret axes turn at (input x k) rad per frame; measured from the log at ~1.1 rad/s for a
// full input on both axes, and refined online from how far each axis actually moved.
constexpr float kPitchMargin=0.03f;   // rad past a pitch stop still counted as reachable
constexpr float kTurnPerInput=1.1f/60.0f,kTurnPerInputMin=0.2f/60.0f,kTurnPerInputMax=6.0f/60.0f;

// --- Frames ---
// The game frame number: it steps when a vehicle's input (slot 55, which every live vehicle gets once a
// frame) comes round again. Game thread only.
void SeeVehicle(const void* vehicle) noexcept;
ULONGLONG Frame() noexcept;

// --- Enemies ---
// One lock point of an object, as the registry had it this frame. One object can own several.
struct Enemy { const void* object; std::int32_t team; float pos[3]; float origin[3]; };
// Every live, valid, lockable lock point in the world (all teams, no range limit), taken once a game frame
// in the vehicle input phase, which runs before the bullet update phase of the same frame: the vehicles'
// scans filter it, and the proximity fuse reads it for each round against the enemies of the side that
// fired it, wherever that vehicle is.
constexpr int kMaxWorld=1024;
// The enemies of `vehicle`'s side within `range` of its turret pivot: pointers into the world snapshot,
// valid for the current vehicle's step only (game thread; the snapshot is retaken next frame).
constexpr int kMaxEnemies=256;
struct Nearby { const Enemy* e[kMaxEnemies]; int count; };
void ScanEnemies(const unsigned char* vehicle,float range,Nearby& out) noexcept;
// The relation row of `team` (relation[other] == kEnemyRelation: enemies), or null.
const std::int32_t* Relations(std::int32_t team) noexcept;

// --- Tracks ---
// What the aim keeps from frame to frame for one gun position: a flak turret (seat 0) or a tank gunner seat.
struct Track {
    const void* vehicle;   // the key: the vehicle (weak-this control block `ctrl`) and the seat index
    const void* ctrl;
    unsigned seat;
    const void* target;
    float last[3];
    float vel[3];          // smoothed target velocity, metres per frame
    int frames;            // consecutive frames on this target
    float want[2];         // last wanted yaw/pitch
    float rate[2];         // smoothed change of the wanted angles, rad per frame
    float axis[2];         // last yaw/pitch angle
    float in[2];           // last input written
    float k[2];            // learned rad per frame per unit input
    bool player;           // a player aims with it: never given up for an NPC's seat (TrackFor)
    ULONGLONG steered;     // the game frame (Frame) this plugin last wrote the seat's turn input (aimlink Steers)
    bool dragging;         // the rider is aiming by hand
    const void* dropped;   // target dragged away from
    ULONGLONG droppedUntil;
    ULONGLONG at;          // last update tick
    ULONGLONG loggedAt;
    // Tank gunners: axis angle and barrel angle last frame, and the learned sign between them
    float geoAxis[2];
    float geo[2];
    float geoSign[2];      // 0 = not yet set
    bool geoValid;
    bool firing;           // the AI held the trigger last frame
    unsigned pulls;        // trigger pulls since the last log line
    unsigned stale;        // ... of which the previous pull was still unread (the gun is not updating)
    // Tank gunners: the seat's homing second weapon (gunner.cpp FireMissiles): its lock count, when it last
    // grew and when the first lock came.
    std::uint64_t locks;
    ULONGLONG locksGrewAt,locksFirstAt;
};
// The track of `vehicle`'s `seat`, made on first use; nullptr when every track is held by a live vehicle
// seen within the last kTrackIdleMs (logged), so that seat is left stock this frame.
// The track of `vehicle`'s seat `seat`, made when there is none. `player`: a player aims from that seat; with
// the table full, a player's seat takes the least recently refreshed NPC seat's track (every NPC tank refreshes
// its own each frame, so idle ones alone never make room), an NPC's seat gets none.
Track* TrackFor(const unsigned char* vehicle,unsigned seat,bool player) noexcept;

// A gun's round as the aim sees it: muzzle speed (m/frame), the drop it picks up along the
// vehicle's down axis (m/frame^2), and whether the gun hunts ground targets first. lofted: its rounds take the high
// arc while its pitch (axis angle) stays within [pitchMin, pitchMax] (the axis' stops), else the low one.
struct Shot { float speed; float drop; bool ground; bool lofted; float pitchMin,pitchMax; };

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline float Clamp(float v,float lo,float hi) noexcept { return v<lo?lo:(v>hi?hi:v); }
inline float Wrap(float a) noexcept {
    while(a>kPi)a-=2*kPi;
    while(a<-kPi)a+=2*kPi;
    return a;
}

void Log(const char* format,...) noexcept;
bool Finite(const unsigned char* base,std::size_t offset,float* out) noexcept;
float Down(const unsigned char* vehicle) noexcept;
bool Ballistic(const float* local,const Shot& shot,float& elevation,float& time) noexcept;
float AxisInput(Track& track,int a,float want,float angle,float error,bool wrap,float gain) noexcept;
void ReloadConfigIfChanged() noexcept;

// --- The player's turret (designate.cpp) ---
// The world's lock points this frame (taken on first use in a frame, as ScanEnemies takes them), and their count.
const Enemy* World(int* count) noexcept;
// The object at `obj` is still the one whose weak-this control block was `ctrl`, and alive (plugin.cpp).
bool Same(const void* obj,const void* ctrl) noexcept;
// The seat's own gun: the weapon in its first holder (the one its aim turns), or nullptr.
const unsigned char* SeatGun(const unsigned char* seat) noexcept;
// Once a frame for the seat this machine's player is at, in a vehicle whose aim this plugin runs: its bindings (the
// mode, the lock) and its lock kept up. `muzzle` / `bore`: the seat's gun (null: none read; the lock then looks along
// the camera, else the vehicle's nose), `range` the lock range when the ini gives none (the gun's, m).
void PilotFrame(const unsigned char* vehicle,unsigned seatIndex,const unsigned char* seat,const float* muzzle,const float* bore,float range) noexcept;
// The player's lock in `vehicle` (the object; `world` its first lock point), or nullptr: none, or the player is elsewhere.
const void* Designated(const unsigned char* vehicle,float* world) noexcept;
// The lead-circle mode: the player's own turret is not steered, the HUD shows where to aim.
bool LeadCircle() noexcept;
// EDF6VehicleCrew's turret camera turns `vehicle`'s seat `seat` after the player's view (common/edf/aimlink.h
// CameraTurret); false without that plugin (or an older one): the player's gun is this plugin's as in V1.
bool CameraTurret(const unsigned char* vehicle,unsigned seat) noexcept;
// The readout for EDF6VehicleCrew's HUD (common/edf/aimlink.h), from the player's seat this frame: `ownGun` the seat's
// gun is the plugin's; `target` what the gun works on (null: nothing) at `world`, led from `muzzle` along `bore` with
// `shot` against the target's velocity `vel` (m/frame); `life` the round's frames (0: unknown).
void PublishAim(const unsigned char* vehicle,bool ownGun,const void* target,const float* world,const float* muzzle,const float* bore,
                const Shot* shot,const float* vel,float life) noexcept;

// gunner.cpp: hooks the Titan's and the gunner-seat tanks' input and weapon-user slots; the number of
// slots it patched (0: it left them stock).
int HookGunners() noexcept;
}  // namespace autoturret
