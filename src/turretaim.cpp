// The turrets' aim link with EDF6AutoTurret (common/edf/aimlink.h): this plugin hands it the camera's view ray and the
// map ray its lock-by-look needs, and takes its player turret readout for the HUD (hud.cpp TurretAimMarks). Either DLL
// alone: nothing found, nothing drawn, the other works as before.
#include "crew.h"
#include "turretaim.h"

namespace crew {
namespace {
edf::aimlink::TurretReadoutFn turretReadout=nullptr;
ULONGLONG turretTried=0;
}  // namespace

bool PlayerTurretAim(edf::aimlink::TurretReadoutV1* out) noexcept {
    namespace link=edf::aimlink;
    if(!Cfg().enabled || !Cfg().turretAimHud)return false;
    const auto fn=link::Resolve(link::kTurretDll,link::kTurretReadout,turretReadout,turretTried);
    return fn && fn(out);
}
}  // namespace crew

extern "C" __declspec(dllexport) bool __cdecl EDF6VehicleCrew_ViewRayV1(float* eye,float* dir) {
    if(!eye || !dir)return false;
    __try { return crew::CameraRay(eye,dir); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

extern "C" __declspec(dllexport) float __cdecl EDF6VehicleCrew_MapRayV1(const float* a,const float* b,float* hit) {
    if(!a || !b || !hit)return -1.0f;
    __try { return crew::MapRay(a,b,hit); }
    __except(EXCEPTION_EXECUTE_HANDLER){return -1.0f;}
}
