// A physics body's world box, as the heli's check measures it (0x64FC1A..0x64FCD6): shape 0x11B15E0(body), transform =
// the world interface (*(*(body+0x100)+0x58)+0x20) slot 0x80 (the body's id, body+0xF0), box = the shape type's entry
// of the table at kAabbTable (+type*0x100+0x18)(shape, transform, out min/max). The Shield Bearer's shield sphere
// (shield.cpp) and the plugin missiles' warhead safety (missile.cpp, fuze.h) read it.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kBodyShape=0x11B15E0,kAabbTable=0x20F1930;
constexpr std::size_t kBodyId=0xF0,kBodyWorld=0x100,kWorldIface=0x58;
constexpr unsigned kTransformSlot=0x80/8;
const unsigned char kBodyShapeSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00};
using BodyShapeFn=const unsigned char*(__fastcall*)(const void*);
using TransformFn=const void*(__fastcall*)(const void*,std::uint32_t);
using AabbFn=void(__fastcall*)(const void*,const void*,float*);
bool ok=false;
}  // namespace

bool InstallBodyBox() noexcept {
    __try { ok=Matches(kBodyShape,kBodyShapeSig,sizeof(kBodyShapeSig)); } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    Log("HOOK body boxes=%d (physics bodies' world boxes: the shield, the missiles' warhead safety)",ok);
    return ok;
}

bool BodyWorldBox(const void* wrapper,float* out) noexcept {
    if(!ok)return false;
    __try {
        const auto body=static_cast<const unsigned char*>(wrapper);
        if(!body || !Readable(body,kBodyWorld+8))return false;
        const unsigned char* shape=reinterpret_cast<BodyShapeFn>(image+kBodyShape)(body);
        const auto world=At<const unsigned char*>(body,kBodyWorld);
        if(!shape || !world || !Readable(shape,0x19) || !Readable(world,kWorldIface+8))return false;
        const unsigned char* iface=At<const unsigned char*>(world,kWorldIface)+0x20;
        const auto vt=At<const TransformFn*>(iface,0);
        const void* transform=vt[kTransformSlot](iface,At<std::uint32_t>(body,kBodyId));
        const auto table=At<const unsigned char*>(image,kAabbTable);
        if(!transform || !table)return false;
        const auto aabb=At<AabbFn>(table,static_cast<std::size_t>(shape[0x18])*0x100+0x18);
        alignas(16) float box[8]{};
        aabb(shape,transform,box);
        for(int k=0;k<3;++k){out[k]=box[k];out[3+k]=box[4+k];}
        for(int k=0;k<6;++k)if(!std::isfinite(out[k]))return false;
        return out[0]<=out[3] && out[1]<=out[4] && out[2]<=out[5];
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
