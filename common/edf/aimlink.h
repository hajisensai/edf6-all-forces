// The link between the two plugins for the turrets' aim (EDF6AutoTurret's lock and lead circle, EDF6VehicleCrew's HUD):
// each DLL exports plain C functions the other looks up by name at run time, so either one works alone and neither
// links the other. A struct that changes gets a new export name (the V<n> suffix), never a new meaning under an old
// one, so a mismatched pair of DLLs finds nothing and draws nothing rather than reading garbage.
//  - EDF6VehicleCrew exports the camera's view ray (its HUD's view-projection, hud.cpp CameraRay) and its map ray
//    (heli.cpp MapRay: terrain and buildings): EDF6AutoTurret's lock-by-look picks the enemy nearest the screen's
//    centre that the eye can see. Without EDF6VehicleCrew the lock looks along the gun's own barrel and sees through
//    walls.
//  - EDF6AutoTurret exports the local player's turret readout (mode, lock, the lead circle), taken on its game thread;
//    EDF6VehicleCrew copies it into its HUD snapshot once a frame (hud.cpp HudPublish) and draws it.
// All game-thread callers; the functions take their own locks, so any thread may call them.
#pragma once
#include <Windows.h>
#include <cstdint>

namespace edf {
namespace aimlink {
// EDF6VehicleCrew's exports.
using ViewRayFn=bool(__cdecl*)(float* eye,float* dir);                       // false: no camera drawn yet
using MapRayFn=float(__cdecl*)(const float* a,const float* b,float* hit);    // metres a->b to the map, <0: clear
constexpr char kViewRay[]="EDF6VehicleCrew_ViewRayV1";
constexpr char kMapRay[]="EDF6VehicleCrew_MapRayV1";
constexpr wchar_t kCrewDll[]=L"EDF6VehicleCrew.dll";

// EDF6AutoTurret's export: the local player's turret, false with the player at none this moment.
enum class Mode : std::uint8_t { autoAim=0, leadCircle=1 };
enum class Lock : std::uint8_t { none=0, acquiring=1, locked=2 };
struct TurretReadoutV1 {
    Mode mode;
    Lock lock;               // the player's designated target (the lock key); acquiring: its track not settled yet
    bool target;             // a target the gun works on (the designated one, else the turret's own pick): `at`
    bool lead;               // the lead solution is there: `leadAt` (the aim point), `boreAt` (where the gun points)
    bool inReach;            // the round reaches the lead point within its life
    bool keys;               // the player is on the keyboard and mouse (else a pad): which binding the hint names
    bool ownGun;             // the player's own gun is the plugin's (the mode applies to it); false: a tank's driver,
                             // whose lock its gunners fight
    float lockProgress;      // 0..1 while acquiring
    float at[3];             // the target's aim point (the lock box)
    float leadAt[3];         // put the gun's line through here and the round meets the target (the lead circle)
    float boreAt[3];         // where the gun's line points now, at the lead point's distance (the bore cross)
    float range;             // m from the muzzle to the lead point
    float flight;            // s the round takes to it
    std::int32_t modeKey,lockKey,modeButton,lockButton;   // the bindings (virtual-key codes, pad button bits; 0 none)
};
using TurretReadoutFn=bool(__cdecl*)(TurretReadoutV1* out);
constexpr char kTurretReadout[]="EDF6AutoTurret_TurretReadoutV1";
constexpr wchar_t kTurretDll[]=L"EDF6AutoTurret.dll";

// V2, who turns the turret of the seat the local player sits in (the user, 2026-10-06: "auto-aim changed, no fighting
// over it any more"). EDF6VehicleCrew's turret camera (turretcam.cpp) turns that turret toward the screen's centre;
// EDF6AutoTurret turns it only onto the player's lock in its auto-aim mode. Each side says what it does, per vehicle and
// seat index, instead of the other guessing it from the turret's input:
//  - CameraTurret (EDF6VehicleCrew): the seat's view is the turret camera's, decoupled (the right stick turns the
//    camera, the turret follows the view). EDF6AutoTurret then steers that gun only onto a lock in AUTO (no target of
//    its own picking, no lead-circle steering) and never reads the stick as the player dragging the gun.
//  - Steers (EDF6AutoTurret): it wrote that seat's turn input this game frame; the turret camera hands the turret to it
//    for the frame (the view stays the player's).
// Either export missing (an older peer, or the peer absent): each plugin keeps its V1 behaviour (EDF6AutoTurret aims the
// player's gun by itself with the stick as a drag; the camera tells a foreign input from the stick's).
using SeatQueryFn=bool(__cdecl*)(const void* vehicle,unsigned seat);
constexpr char kCameraTurret[]="EDF6VehicleCrew_CameraTurretV2";
constexpr char kSteers[]="EDF6AutoTurret_SteersV2";

// V3, the gun stabilizer (EDF6VehicleCrew src/stab.cpp, 2026-10-06): after a seat's stock aim step EDF6VehicleCrew may
// turn its axes further, to keep the gun on its line in the world while the hull pitches and turns; the step's own turn
// (the input) moves that line. A controller of the gun steers in the stabilizer's frame, or it fights it (the hull's turn
// shows in its error and its want's rate, its input compensates it, the stabilizer compensates it again and takes the
// input for an aim change):
//  - Stabilizer (EDF6VehicleCrew), asked before the seat's aim step of the frame: true when it holds `vehicle`'s seat
//    `seat`; `held` (2 floats, rad, the aim's own senses: yaw, pitch negative up) the axes it holds the gun at this frame
//    with no input: the angle to steer from; `hull` how much of that is the hull's turn since the last step: taken out of
//    the want's change (the target's own motion is what is left) and of the held angle's change (the input's own turn,
//    what a learned turn per input reads). False: `held` the axes as they are, `hull` 0 (steer as ever).
//  - StabilizerAware (EDF6AutoTurret): it does the above. EDF6VehicleCrew holds no seat EDF6AutoTurret may steer while
//    that plugin is loaded without this export (an older one).
using StabilizerFn=bool(__cdecl*)(const void* vehicle,unsigned seat,float* held,float* hull);
using AwareFn=bool(__cdecl*)();
constexpr char kStabilizer[]="EDF6VehicleCrew_StabilizerV3";
constexpr char kStabilizerAware[]="EDF6AutoTurret_StabilizerAwareV3";

// What EDF6AutoTurret does with the player's own gun in a frame (a pure rule: tools/turret_lead_check.cpp checks it):
// `steer` it turns the gun onto its target, `drag` a stick past DragDeadzone is the player aiming by hand (the target
// let go). `cameraTurret` the camera turns the gun (CameraTurret above), `lead` the lead-circle mode, `locked` the
// player's lock is there. It still tracks a target either way (the lock, else the enemy nearest the gun's line): the
// time fuse and the lead circle need one; only turning the gun to it is the rule's.
struct PlayerGun { bool steer; bool drag; };
inline PlayerGun PlayerGunRule(bool cameraTurret,bool lead,bool locked) noexcept {
    if(!cameraTurret)return PlayerGun{!lead,!lead};   // V1: auto-aim at its own pick, the stick drags it off
    return PlayerGun{!lead && locked,false};           // the camera's gun: a lock in AUTO alone; the stick is the camera's
}

// The other DLL's export `name`, or nullptr while that DLL is not loaded (looked up again at most once a second: the
// plugins load in any order; once found it stays, as plugins are never unloaded).
template<class Fn> Fn Resolve(const wchar_t* dll,const char* name,Fn& cache,ULONGLONG& triedAt) noexcept {
    if(cache)return cache;
    const ULONGLONG now=GetTickCount64();
    if(triedAt && now-triedAt<1000)return nullptr;
    triedAt=now;
    const HMODULE m=GetModuleHandleW(dll);
    if(m)cache=reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m,name)));
    return cache;
}
}  // namespace aimlink
}  // namespace edf
