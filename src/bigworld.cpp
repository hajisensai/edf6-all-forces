// A bigger physics world (docs/bigworld-re.md), step 1 of the user's "a bigger map, real physics all over":
//  - the Havok world's broadphase box, hard coded +-3000 m (0x11AD340 loads it from the constant at 0x17BE0E0 at
//    0x11AD395, for both worlds of every scene reset), is pointed at the game's own {10000 x4} at 0x17C3840 (the
//    disp32 of that movaps, 0x11AD398): the next world built (the next mission load) is +-10000 m. Bodies keep
//    their precision (float32 ~1 mm at 12 km; broadphase AABBs quantized ~0.3 m instead of ~0.1 m).
//  - the map's collision pieces (Preload_Fmex, vtable 0x176BF88; slot 0 attaches their havok_Body at +0x1240,
//    0x17DD40) are logged as they attach: where each is and how many bodies it adds, to find the ground pieces
//    the next step copies around the map.
// ini BigWorld (default on). All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kBoundsAt=0x11AD391,kBoundsDisp=0x11AD398,kBigBounds=0x17C3840,kStockBounds=0x17BE0E0;
const unsigned char kBoundsSig[]={0x89,0x5C,0x24,0x68,0x0F,0x28,0x0D,0x44,0x0D,0x61,0x00,0x0F,0x57,0xC0,0x0F,0x5C};
const unsigned char kStockDisp[]={0x44,0x0D,0x61,0x00};   // 0x17BE0E0 - 0x11AD39C
const unsigned char kBigDisp[]={0xA4,0x64,0x61,0x00};     // 0x17C3840 - 0x11AD39C
constexpr std::size_t kFmexVtable=0x176BF88,kFmexAttach=0x153EB0,kFmexMatrix=0x1200,kFmexBody=0x1240;
constexpr std::size_t kBodyInfos=0xA8;   // havok_Body: std::list of its body infos (head node, size)
const unsigned char kFmexAttachSig[]={0x48,0x81,0xC1,0x40,0x12,0x00,0x00,0xE9,0x84,0x9E,0x02,0x00};
constexpr int kMostLogged=96;            // a mission's pieces logged at most

using AttachFn=void(__fastcall*)(void*);
AttachFn nextAttach=nullptr;
int logged=0;

bool BoundsFour(std::size_t rva,float value) noexcept {
    const float* v=reinterpret_cast<const float*>(image+rva);
    for(int i=0;i<4;++i)if(v[i]!=value)return false;
    return true;
}

void __fastcall AttachHook(void* fmex) noexcept {
    nextAttach(fmex);
    __try {
        if(logged>=kMostLogged)return;
        const auto o=static_cast<const unsigned char*>(fmex);
        const float* m=reinterpret_cast<const float*>(o+kFmexMatrix);
        const auto infos=o+kFmexBody+kBodyInfos;
        const std::size_t bodies=At<std::size_t>(infos,8);
        ++logged;
        Log("BIGWORLD map piece %p at (%.0f,%.0f,%.0f): %zu bodies",fmex,m[12],m[13],m[14],bodies<100000 ? bodies : 0);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
}  // namespace

bool InstallBigWorld() noexcept {
    __try {
        bool bounds=false;
        if(!Cfg().bigWorld)Log("BIGWORLD off (ini BigWorld=0): the physics world stays +-3000 m");
        else if(!Matches(kBoundsAt,kBoundsSig,sizeof(kBoundsSig)) || !BoundsFour(kBigBounds,10000.0f) || !BoundsFour(kStockBounds,3000.0f))
            Log("BIGWORLD: unexpected EDF.dll code: the physics world stays +-3000 m");
        else bounds=edf::PatchCode(image+kBoundsDisp,kStockDisp,kBigDisp,sizeof(kBigDisp));
        bool pieces=false;
        if(Matches(kFmexAttach,kFmexAttachSig,sizeof(kFmexAttachSig))) {
            const auto slot=reinterpret_cast<void**>(image+kFmexVtable);
            void* const current=*slot;
            nextAttach=reinterpret_cast<AttachFn>(current);
            pieces=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&AttachHook));
        }
        Log("HOOK bigworld bounds=%d (+-%s m from the next mission load) pieces=%d",bounds,bounds ? "10000" : "3000",pieces);
        return bounds;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void ResetBigWorld() noexcept {
    logged=0;
}
}  // namespace crew
