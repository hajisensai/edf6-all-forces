// EDF6's Deiroi falling state casts its landing ray against a null physics world: the state function 0x321FA0 (Deiroi vtable
// 0x17AA668, reached from 0x31ED90 when the walker is airborne) builds a ray from last frame's position to this frame's and calls
// the old Havok world raycast 0xA954F0 with `xor ecx,ecx` as the world (0x3220F8); 0xA95599 then reads [0 + 0xC0] and the game
// crashes. EDF6's own missions never drop a Deiroi into that state; EDF4.1's M412 (Deiroi401L / 401S spawned in the air by the
// 401 mothership's generators) does, every run, with or without the plugin's 4.1 classes (docs/edf41-ufomother401.md, 2026-10-15).
// The call is redirected: with a world the stock function runs; without one the same ray is cast through EDF6's map ray (edf41c::
// GroundHit, 0x11A7EE0) and the result block is filled as 0xA954F0 fills it for the caller: +0x10 the hit fraction along the ray,
// +0x50 non-null on a hit (0x32221E only tests it; a hit switches the walker to its landing state 0x321490).
#include "crew.h"
#include "edf41classes.h"
#include "memory.h"

#include <atomic>
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kCallSite=0x3220FA;      // call 0xA954F0, after `xor ecx,ecx` at 0x3220F8
constexpr std::size_t kWorldRay=0xA954F0;
constexpr unsigned char kSiteBytes[7]={0x33,0xC9,0xE8,0xF1,0x33,0x77,0x00};
constexpr unsigned char kRayBytes[8]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C};
using RayFn=void(__fastcall*)(void*,const float*,char*);
char hitMarker;

void __fastcall FallRay(void* world,const float* in,char* out) {
    if(world) { reinterpret_cast<RayFn>(image+kWorldRay)(world,in,out); return; }
    if(!in || !out || !Readable(in,32) || !Readable(out,0x58))return;
    const float* from=in;
    const float* to=in+4;
    float hit[3];
    if(!edf41c::GroundHit(from,to,hit))return;   // no hit: the block keeps the caller's "nothing" (fraction 1, null)
    const float d[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    const float h[3]={hit[0]-from[0],hit[1]-from[1],hit[2]-from[2]};
    float f=len>1e-5f ? std::sqrt(h[0]*h[0]+h[1]*h[1]+h[2]*h[2])/len : 0.0f;
    f=std::fmax(0.0f,std::fmin(1.0f,f));
    std::memcpy(out+0x10,&f,4);
    char* marker=&hitMarker;
    std::memcpy(out+0x50,&marker,8);
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<6)Log("EDF41DEIROI a falling Deiroi's landing ray (no Havok world) cast through the map ray: hit %.1f %.1f %.1f, fraction %.3f",
                               hit[0],hit[1],hit[2],f);
}
}  // namespace

bool InstallEdf41Deiroi() noexcept {
    if(!Readable(image+kCallSite-2,7) || std::memcmp(image+kCallSite-2,kSiteBytes,7)!=0 || !Readable(image+kWorldRay,8)
       || std::memcmp(image+kWorldRay,kRayBytes,8)!=0) {
        Log("EDF41DEIROI the falling state's ray call is not what this was written for: not hooked");
        return false;
    }
    bool changed=false;
    const bool ok=RedirectCall(image+kCallSite,image+kWorldRay,reinterpret_cast<void*>(&FallRay),changed);
    Log("EDF41DEIROI falling Deiroi's null-world landing ray %s",ok ? "redirected to the map ray" : "NOT hooked");
    return ok;
}
}  // namespace crew
