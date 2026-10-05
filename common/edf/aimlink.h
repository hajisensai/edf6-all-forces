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
