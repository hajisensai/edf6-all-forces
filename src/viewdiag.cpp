// A diagnosis of "the map turns into flat grey under the sky" (the user, 2026-10-25, on EDF4.1's first mission:
// 打着打着会变成灰白样子地图; the screenshot: sky, a faint band at the horizon, nothing nearer). Three causes give that
// picture and the plugin cannot see the screen:
//  - the eye off the map: no ground under it, only the far pass's scenery and the fog remain;
//  - the eye under the ground: the ground is culled from below;
//  - the near pass not drawing (docs/view-distance-re.md: it draws everything but the far-only scenery, 0.1 m to its far
//    clip): its clip planes broken (near >= far, a far clip too short).
// Every kDiagMs in a mission whose ground is measured (playarea.cpp), the view's eye (hud.cpp CameraRay) is classed by
// map rays straight down and up, and camera 0's and the env's clip planes (view.cpp's fields) are checked; a change of
// class is logged once with the numbers (VIEWDIAG), so a player's log tells which of them it was. Reads only.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "heli.h"
#include "memory.h"
#include "playarea.h"
#include <cmath>

namespace crew {
namespace {
// view.cpp's: env = *(*(image+0x20B2990)+0x258), +0x1A0 far clip, +0x1A4 / +0x1A8 the far pass; camera i =
// *(*(image+0x20B2958)+0x4D8+i*0x188), +0x28 near, +0x2C far, +0x30 / +0x34 the far pass.
constexpr unsigned kEnvHolder=0x20B2990,kCameraMgr=0x20B2958;
constexpr std::size_t kEnv=0x258,kFarClip=0x1A0,kCameras=0x4D8,kCamNear=0x28,kCamFar=0x2C;
constexpr ULONGLONG kDiagMs=2000;
constexpr float kDiagReach=3000.0f;
enum class Diag { none,ok,noGround,underGround,nearPass };
const char* DiagName(Diag d) noexcept {
    switch(d) {
    case Diag::ok:return "ok";
    case Diag::noGround:return "no ground under the eye (off the map)";
    case Diag::underGround:return "ground above the eye (under the map)";
    case Diag::nearPass:return "near pass clip broken";
    default:return "none";
    }
}
Diag was=Diag::none;
ULONGLONG at=0;

const unsigned char* EnvNow() noexcept {
    const auto holder=At<const unsigned char*>(image,kEnvHolder);
    if(!holder || !Readable(holder+kEnv,8))return nullptr;
    const auto env=At<const unsigned char*>(holder,kEnv);
    return env && Readable(env+kFarClip,12) ? env : nullptr;
}
const unsigned char* CameraZero() noexcept {
    const auto mgr=At<const unsigned char*>(image,kCameraMgr);
    if(!mgr || !Readable(mgr+kCameras,8))return nullptr;
    const auto cam=At<const unsigned char*>(mgr,kCameras);
    return cam && Readable(cam+kCamNear,16) ? cam : nullptr;
}
}  // namespace

void ViewDiagTick() noexcept {
    if(!PlayAreaMeasured()){was=Diag::none;return;}
    const ULONGLONG now=GetTickCount64();
    if(now-at<kDiagMs)return;
    at=now;
    float eye[3],dir[3];
    if(!CameraRay(eye,dir) || !std::isfinite(eye[0]) || !std::isfinite(eye[1]) || !std::isfinite(eye[2]))return;
    const float down[3]={eye[0],eye[1]-kDiagReach,eye[2]},up[3]={eye[0],eye[1]+kDiagReach,eye[2]};
    float below[3]={},above[3]={};
    const float dBelow=MapRay(eye,down,below),dAbove=MapRay(eye,up,above);
    float cam[4]={-1.0f,-1.0f,-1.0f,-1.0f},env[3]={-1.0f,-1.0f,-1.0f};
    if(const auto c=CameraZero())for(int i=0;i<4;++i)cam[i]=At<float>(c,kCamNear+4*static_cast<std::size_t>(i));
    if(const auto e=EnvNow())for(int i=0;i<3;++i)env[i]=At<float>(e,kFarClip+4*static_cast<std::size_t>(i));
    Diag d=Diag::ok;
    if(cam[0]>=0.0f && !(cam[0]>0.0f && cam[1]>cam[0]*2.0f && cam[1]>=100.0f))d=Diag::nearPass;
    else if(dAbove>=0.0f && dBelow<0.0f)d=Diag::underGround;
    else if(dBelow<0.0f)d=Diag::noGround;
    if(d==was)return;
    was=d;
    const PlayArea a=MapPlayArea();
    Log("VIEWDIAG %s: eye (%.1f %.1f %.1f) dir (%.2f %.2f %.2f), ground below %.1f m, above %.1f m (-1: none); camera 0 "
        "near %.3f far %.0f, far pass %.0f..%.0f; env far %.0f, far pass %.0f..%.0f; play area x %.0f..%.0f z %.0f..%.0f (%s)",
        DiagName(d),eye[0],eye[1],eye[2],dir[0],dir[1],dir[2],dBelow,dAbove,cam[0],cam[1],cam[2],cam[3],env[0],env[1],env[2],
        a.lo[0],a.hi[0],a.lo[1],a.hi[1],a.ground ? "measured" : "square");
}
}  // namespace crew
