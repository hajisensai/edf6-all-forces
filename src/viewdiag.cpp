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
//
// None of the three was it (the real cause, 2026-10-25, docs/feedback-2026-10-25-greymap.md): a fourth way to the same
// picture is Umbra's spatial database. Every object's box reaches umbra_sandlot.dll as a matrix (0x11B30C0 ->
// Object::setObjectToCellMatrix through the import 0x17567D0; 0x11B30C0 clamps the box's axes to 1e6 but neither a
// NaN nor the centre). One box out of any map's reach (a V301 gun bone whose matrix had grown past 1e20) wrecks the
// database: from then on the near view, the shadows and the far view find next to nothing, and only the sky, the HUD,
// the particles and the first-person gun are drawn, for the rest of the mission. So the import is watched too (the
// wrapper only checks the 16 floats and calls on): such a placement is logged (VIEWDIAG umbra) with the call path and,
// when the stack holds it, the object it belongs to, so a player's log names the source. Nothing about the call changes.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "heli.h"
#include "memory.h"
#include "playarea.h"
#include <atomic>
#include <cmath>
#include <cstdint>

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

// Umbra's input (see the top). Each object's box at a vehicle's or soldier's model node: node+0x10 the Umbra object;
// a vehicle's model at +0xE40, a soldier's at +0x860 / +0x8A0.
constexpr unsigned kUmbraObjectMatrix=0x17567D0;   // EDF.dll's import of Object::setObjectToCellMatrix
constexpr float kUmbraReach=1.0e6f;                 // no map reaches a million metres; 0x11B30C0 clamps the axes to it
constexpr int kUmbraLogs=8;                         // a session's rows (one wreck is enough to name its source)
using UmbraMatrixFn=void(__fastcall*)(void*,const float*);
UmbraMatrixFn umbraNext=nullptr;
std::atomic<int> umbraBad{0};

bool OutOfReach(const float* m) noexcept {
    for(int i=0;i<16;++i)if(!std::isfinite(m[i]) || std::fabs(m[i])>kUmbraReach)return true;
    return false;
}

// The owner whose model node holds `object`: a pointer on the stack above the call to a vehicle / soldier (its node's
// +0x10 is the object). Its class from the RTTI next to its vtable. Empty when none is found.
void UmbraOwner(const void* object,char* out,std::size_t cap) noexcept {
    out[0]='\0';
    CONTEXT c{};
    RtlCaptureContext(&c);
    const auto* sp=reinterpret_cast<const std::uintptr_t*>(c.Rsp);
    constexpr std::size_t kNodes[]={0xE40,0x860,0x8A0,0};
    for(const std::size_t node:kNodes)
        for(int i=0;i<0x200;++i) {
            __try {
                const auto q=reinterpret_cast<const unsigned char*>(sp[i]);
                if(reinterpret_cast<std::uintptr_t>(q)<0x10000 || *reinterpret_cast<void* const*>(q+node+0x10)!=object)continue;
                const auto vt=*reinterpret_cast<unsigned char* const*>(q);
                const char* name="?";
                if(vt>image && Readable(vt-8,8)) {
                    const auto col=*reinterpret_cast<unsigned char* const*>(vt-8);
                    if(col>image && Readable(col+0xC,4))name=reinterpret_cast<const char*>(image+*reinterpret_cast<const std::uint32_t*>(col+0xC)+0x10);
                }
                std::snprintf(out,cap," owner %p (model +0x%zX, vtable EDF+0x%zX %.60s)",q,node,static_cast<std::size_t>(vt-image),
                              Readable(name,1) ? name : "?");
                return;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
}

void __fastcall UmbraObjectMatrix(void* object,const float* m) noexcept {
    if(m && OutOfReach(m) && umbraBad.fetch_add(1)<kUmbraLogs) {
        void* frames[6]{};
        const USHORT n=RtlCaptureStackBackTrace(1,6,frames,nullptr);
        char path[96]{},owner[160]{};
        std::size_t used=0;
        for(USHORT i=0;i<n && used+12<sizeof(path);++i) {
            const auto p=static_cast<unsigned char*>(frames[i]);
            used+=static_cast<std::size_t>(std::snprintf(path+used,sizeof(path)-used,p>image && p<image+0x2400000 ? " %zX" : " ?",
                                                         static_cast<std::size_t>(p-image)));
        }
        UmbraOwner(object,owner,sizeof(owner));
        Log("VIEWDIAG umbra: object %p placed out of reach (the near view's culling breaks: the grey map): axes %g %g %g, "
            "centre %g %g %g; path%s%s",object,m[0],m[5],m[10],m[12],m[13],m[14],path,owner);
    }
    umbraNext(object,m);
}

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

bool InstallUmbraWatch() noexcept {
    auto** const entry=reinterpret_cast<void**>(image+kUmbraObjectMatrix);
    void* const now=*entry;
    // The import resolved into umbra_sandlot.dll (outside EDF.dll); anything else is not the build this was read from.
    if(!now || (static_cast<unsigned char*>(now)>=image && static_cast<unsigned char*>(now)<image+0x2400000))return false;
    umbraNext=reinterpret_cast<UmbraMatrixFn>(now);
    DWORD old=0;
    if(!VirtualProtect(entry,sizeof(void*),PAGE_READWRITE,&old))return false;
    const bool swapped=InterlockedCompareExchangePointer(entry,reinterpret_cast<void*>(&UmbraObjectMatrix),now)==now;
    edf::RestoreProtection(entry,sizeof(void*),old);
    return swapped;
}

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
