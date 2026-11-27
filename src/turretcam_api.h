// turretcam.cpp's hooks and readouts (the logic is turretcam.h).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
bool TurretCamHighTransition(const void* vehicle) noexcept; // includes the return from overhead to the normal camera
// The weapon seat `seat` of `vehicle`'s turret is laid by (turretcam.cpp TurretGun: the fire-control pick when it turns
// with both of the seat's axes, else the seat's first weapon that does); nullptr: none. vhud.cpp: whose aim overlay it is.
const unsigned char* PlayerTurretGun(const unsigned char* vehicle,const unsigned char* seat) noexcept;

// turretcam.cpp: the turret camera (README 炮塔镜头, docs/camera-re.md §3b, §5). InstallTurretCam at load (the riding
// camera's look-at fetch, the seat aim's step); TurretCamFrame from every vehicle's input, the plugin off too (it lets
// go then). TurretCamServes: it places the camera of `vehicle` (the player's turret, seat 0); TurretCamLarge: and its
// rig is a big vehicle's (highcam.cpp HighCamClass 2).
bool InstallTurretCam() noexcept;
void TurretCamFrame(unsigned char* vehicle) noexcept;
bool TurretCamServes(const void* vehicle) noexcept;
bool TurretCamLarge(const void* vehicle) noexcept;
// TurretCamTurret: the camera is decoupled in `vehicle`'s seat `seat` (the player's turret follows the view: the right
// stick turns the camera; EDF6AutoTurret asks, common/edf/aimlink.h CameraTurret). TurretCamSteers: and the camera
// turned that seat's aim itself in its last step (not another plugin's hand): nix.cpp then leaves the torso's yaw to it.
bool TurretCamTurret(const void* vehicle,unsigned seat) noexcept;
bool TurretCamSteers(const void* vehicle) noexcept;
// TurretCamFreeLook: the free-look key in `vehicle`'s seat `seat` is the turret camera's own (War Thunder's: the view
// swings round the vehicle on the rig, the turret holds the point it was on and the HUD keeps that point's circle,
// TurretMark): seat 0 of the turret it places, in third person, the optic not up. vehicle_look.cpp yields to it there
// (the user, 2026-10-21: "鼠标中键的时候应该是绕着车转…战雷一样的那个圆的鼠标指示点怎么没了").
bool TurretCamFreeLook(const void* vehicle,unsigned seat) noexcept;
void ResetTurretCam() noexcept;
// The chase follower's roll (turretcam.cpp, chase_cam.h; README 载具镜头跟随): from the camera step after the native one
// (firstperson.cpp CameraAfterNative), with the camera's rows (+0x220) in third person to turn them about their forward
// row onto this step's followed up (a jet's or a heli's partial bank), null where another view owns them (first person,
// a controlled enemy): either way this step's roll is used up.
void ChaseCamUp(const unsigned char* cam,float* rows) noexcept;
// The player's turret against their view (hud.cpp's marker; any HUD may draw it), fresh within 200 ms while the camera
// is decoupled or looking round: `aim` the point the turret is sent to (under the screen's centre, or the one it holds
// in free look), `gun` where the gun's round would be at that point's range as it points now (on its arc), the muzzle
// and its direction; `onTarget` both axes within half a degree of their want.
struct TurretCamReadout { bool decoupled,freeLook,high,onTarget; float aim[3],gun[3],muzzle[3],gunDir[3]; bool physicalOnly=true; bool aimValid=false; };
bool PlayerTurretCam(TurretCamReadout* out) noexcept;
}  // namespace crew
