// The stock spot (原版 Q 标记, key config "OptionPlayer_KeyboardBaseSpot") and the ray it is cast along
// (docs/feedback-2026-10-09-aim.md §3; EDF.dll TimeDateStamp 0x678CCB46 RVAs):
//  - The soldier's update 0x59ABB0, with the spot flag soldier+0xD7A set (0x59B689), takes origin = its eye vector and
//    dir = soldier+0x80; riding (soldier+0x1550's control block alive), it takes the seat's camera locators instead
//    (seat = soldier+0x1540, camera Type seat+0x200: 0 -> the eye locator's matrix, row 3 the origin and row 2 the
//    direction (0x6BB5A0); 1 -> origin the eye locator seat+0x208, direction the look-at locator seat+0x218 minus it
//    (0x6BB420)), and calls 0x5A1120(soldier, &origin, &dir) at 0x59B75C.
//  - 0x5A1120 normalizes dir, scales it to kReach (1000.0f at 0x1765A80), lifts the origin's y by kLift (addss at
//    0x5A11EC from 0x1C369B8: 2.0f), casts origin -> origin + dir (filter 0x58F300 skips the soldier and its own
//    vehicle) and puts a SpotEffect (0x59F630) where it hits; the point goes online through the soldier's messages.
// The seat's locators are the stock riding camera's targets. EDF6VehicleCrew's turret camera (turretcam.cpp) and high
// view place the camera elsewhere (37 m behind a Titan, raised, decoupled from the turret), so the spot went where the
// stock view would look, not under the player's crosshair. The fix hands 0x5A1120 the camera actually drawn (CameraRay):
// pure arithmetic here, tools/spot_ray_check.cpp checks it offline.
#pragma once
#include <cmath>

namespace crew {
namespace spotray {
constexpr float kLift=2.0f,kReach=1000.0f;

// The arguments 0x5A1120 needs to cast from `eye` along the unit `dir` (the drawn camera): the origin lowered by the lift
// it adds back (the ray starts on the eye), w 1; the direction as is, w 0. False when they are not finite.
inline bool NativeArgs(const float* eye,const float* dir,float* origin,float* direction) noexcept {
    for(int i=0;i<3;++i)if(!std::isfinite(eye[i]) || !std::isfinite(dir[i]))return false;
    const float l=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    if(!(l>1e-6f))return false;
    origin[0]=eye[0];origin[1]=eye[1]-kLift;origin[2]=eye[2];origin[3]=1.0f;
    for(int i=0;i<3;++i)direction[i]=dir[i]/l;
    direction[3]=0.0f;
    return true;
}
// The segment 0x5A1120 casts for `origin`, `dir` (its own arithmetic, for the checks).
inline void NativeSegment(const float* origin,const float* dir,float* start,float* end) noexcept {
    const float l=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    for(int i=0;i<3;++i)start[i]=origin[i];
    start[1]+=kLift;
    for(int i=0;i<3;++i)end[i]=start[i]+dir[i]/l*kReach;
}
}  // namespace spotray
}  // namespace crew
