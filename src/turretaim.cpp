// The turrets' aim link with EDF6AutoTurret (common/edf/aimlink.h): this plugin hands it the camera's view ray and the
// map ray its lock-by-look needs, and takes its player turret readout for the HUD (hud.cpp TurretAimMarks); V2, who
// turns the player's turret: this plugin says when its turret camera turns a seat after the view (CameraTurret), that
// one when it turned it this frame (Steers, turretcam.cpp asks). Either DLL alone: nothing found, nothing drawn, the
// other works as before.
#include "crew.h"
#include "turretaim.h"

namespace crew {
namespace {
edf::aimlink::TurretReadoutFn turretReadout=nullptr;
edf::aimlink::SeatQueryFn turretSteers=nullptr;
ULONGLONG turretTried=0,steersTried=0;
}  // namespace

bool AutoTurretReadout(edf::aimlink::TurretReadoutV1* out) noexcept {
    namespace link=edf::aimlink;
    const auto fn=link::Resolve(link::kTurretDll,link::kTurretReadout,turretReadout,turretTried);
    return fn && fn(out);
}

bool PlayerTurretAim(edf::aimlink::TurretReadoutV1* out) noexcept {
    return Cfg().enabled && Cfg().turretAimHud && AutoTurretReadout(out);
}

int AutoTurretSteers(const void* vehicle,unsigned seat) noexcept {
    namespace link=edf::aimlink;
    const auto fn=link::Resolve(link::kTurretDll,link::kSteers,turretSteers,steersTried);
    return fn ? (fn(vehicle,seat) ? 1 : 0) : -1;
}
}  // namespace crew

extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_ViewRayV1(float* eye,float* dir) {
    if(!eye || !dir)return false;
    __try { return crew::CameraRay(eye,dir); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// EDF6AutoTurret asks whether the turret camera turns `vehicle`'s seat `seat` after the view (aimlink.h CameraTurret).
extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_CameraTurretV2(const void* vehicle,unsigned seat) {
    __try { return crew::TurretCamTurret(vehicle,seat); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

extern "C" __declspec(dllexport) float __cdecl EDF6VehicleCrew_MapRayV1(const float* a,const float* b,float* hit) {
    if(!a || !b || !hit)return -1.0f;
    __try { return crew::MapRay(a,b,hit); }
    __except(EXCEPTION_EXECUTE_HANDLER){return -1.0f;}
}
