// EDF4.1's real 408 sidecar bike on the native EDF6 CarBase chassis.
// See docs/edf41-vehicle-model-port.md; every address is EDF.dll 0x678CCB46.
#pragma once
#include <cmath>

namespace crew {
bool InstallEdf41Bike() noexcept;
bool IsEdf41Bike(const void* vehicle) noexcept;
namespace edf41bike {
constexpr wchar_t kMarker[]=L"edf41_motorbike";
constexpr wchar_t kSchema[]=L"edf41_bike";
constexpr unsigned kOperatorSeat=1;
constexpr float kDegrees=3.14159265358979323846f/180.0f;
// 4.1 VehicleBike's same-bone pose negates both angles; EDF6 5FACD0 does
// not. Reversing the bounds preserves exactly the original physical range.
struct Limits { float yawLo,yawHi,pitchLo,pitchHi; };
constexpr Limits kNativeLimits{-20*kDegrees,30*kDegrees,-30*kDegrees,15*kDegrees};
struct Decision { bool update,input,fire; };
constexpr Decision Decide(bool enabled,bool dead,bool occupied,bool localPlayer,bool npcAuthority,bool mapHold,bool trigger,bool remoteAim=false) noexcept {
    const bool update=enabled && !dead && (occupied || remoteAim);
    const bool input=update && occupied && (localPlayer || npcAuthority) && !mapHold;
    return {update,input,input && trigger};
}
inline float Bound(float angle,float lo,float hi) noexcept {
    return !std::isfinite(angle) ? 0.0f : angle<lo ? lo : angle>hi ? hi : angle;
}
}  // namespace edf41bike
}  // namespace crew
