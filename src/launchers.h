// The player's launchers (launcher.cpp, andryusha.cpp, katyusha.cpp, barga_aim.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace bargaaim { struct Readout; }   // barga_aim.h

namespace crew {
void ResetLauncher() noexcept;    // launcher.cpp
void ResetBargaAim() noexcept;    // barga_aim.cpp
// launcher.cpp: the Katyusha's impact point (CCIP) while the player rides a vehicle whose seat 0 holds a launcher marked
// lofted (edf::Lofted, common/edf/weapon.h): where a rocket fired now comes down (reach: it does within its life), `ring` points
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
    int fuse;      // the Andryusha's fuse (andryusha.cpp AndryushaSeat: 1 impact, 2 airburst), 0 another launcher
    bool keys;     // the rider plays on the keyboard (the fuse switch's key named), else the pad
};
// A round's arc as the game steps it (a frame: vel += drop, pos += vel; m/frame, m/frame^2) from `pos` for at most
// `frames` frames: the first ground a map ray finds along it (`hit`) and the frames it took. launcher.cpp.
bool RoundImpact(const float* pos,const float* vel,const float* drop,int frames,float* hit,float* took) noexcept;
void LauncherFrame(unsigned char* vehicle) noexcept;
// andryusha.cpp: the Andryusha's incendiary cluster rockets (docs/andryusha-re.md, src/andryusha.h). InstallAndryusha wraps
// GrenadeBullet01's update (chained with EDF6AutoTurret's); AndryushaTick (SharedFrameTick) runs the airburst fuses' ground
// probes and splits the carriers that burst into bomblets (AndryushaBomblet); AndryushaSeat (LauncherFrame, the player
// on seat 0 of a lofted launcher whose weapon is `weapon`) turns the fuse on VehicleSecondarySwitchKey / -Button and
// returns it (andryusha::Fuse: 0 not the Andryusha's weapon, 1 impact, 2 airburst).
bool InstallAndryusha() noexcept;
void AndryushaTick() noexcept;
int AndryushaSeat(const unsigned char* vehicle,const unsigned char* seat,unsigned char* weapon) noexcept;
// Each vehicle's frame (crew.cpp, after LauncherFrame): an Andryusha's fuse sent to its copies by the machine that speaks
// for it (andryusha_net.h: its player on the launcher's seat, else the machine that runs it); the copies write it.
void AndryushaFrame(unsigned char* vehicle) noexcept;
void ResetAndryusha() noexcept;
bool PlayerLauncher(LauncherReadout* out) noexcept;
// barga_aim.cpp: the Balam's impact marker (docs/feature-barga-aim.md). BargaAimFrame from every vehicle's input (game
// thread; only a 501 the player drives reads); PlayerBargaAim the last readout, false with none this step.
void BargaAimFrame(unsigned char* vehicle) noexcept;
bool PlayerBargaAim(bargaaim::Readout* out) noexcept;
// Whether the marker draws `weapon`'s rounds this step (V605's SwingShoot while it charges or sweeps): vhud.cpp leaves
// that weapon's own impact point out, so the two do not show one cannon twice.
bool BargaOwnsCannon(const void* weapon) noexcept;
// katyusha.cpp: the Katyusha's launcher pose. The 402 class's pose call is detoured: after the stock pose the launcher
// is held at the elevation LauncherFrame asks for with SetLauncherLoft (the arc onto where the player's camera looks;
// `aim` false: none, the stock pose, the player's own aim), eased at the stock turret's rate, and the telescopic ram's
// two bones are aimed at each other's pivot; the launcher is never posed under the floor over the cab at the turntable's
// azimuth (common/edf/katyusha_cab.h). LauncherLoft: the last pose of `vehicle` (rad; held: off the stock pose).
bool InstallKatyusha() noexcept;
void SetLauncherLoft(const void* vehicle,bool aim,float elevation) noexcept;
struct LoftReadout { bool held; float elevation,stock,ramTurn,ramLength,azimuth,floor; };
bool LauncherLoft(const void* vehicle,LoftReadout* out) noexcept;
void ResetKatyushas() noexcept;
}  // namespace crew
