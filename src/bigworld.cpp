// A bigger physics world (docs/bigworld-re.md), step 1 of the user's "a bigger map, real physics all over":
//  - the Havok world's broadphase box, hard coded +-3000 m (0x11AD340 loads it from the constant at 0x17BE0E0 at
//    0x11AD395, for both worlds of every scene reset), is pointed at the plugin's own {BigWorld x4} (the disp32 of that
//    movaps, 0x11AD398): the next world built (the next mission load) is +-BigWorld m. At +-10000 a parked vehicle
//    fell through the terrain (2026-10-04); suspected: hknp's 16-bit AABB grid (0.09 m a step at 3000, 0.31 at 10000).
//    The ini value is the experiment's: the largest that keeps parked vehicles on the ground is the one to keep.
//  - the map's collision pieces (Preload_Fmex, vtable 0x176BF88; slot 0 attaches their havok_Body at +0x1240,
//    0x17DD40) are logged as they attach: where each is and how many bodies it adds, to find the ground pieces
//    the next step copies around the map.
// ini BigWorld (default off: with the bounds raised, a parked vehicle (the player jet waiting on the ground)
// fell through the terrain and was put back by the game again and again, 2026-10-04 test range; under study). All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cstdint>
#include <cstring>

namespace crew {
namespace {
// The movaps at 0x11AD395 reads its 16 bytes rip-relative (the next instruction 0x11AD39C): the plugin's own
// {bounds x4}, 16-byte aligned in a page within rel32 reach, takes the stock constant's place.
constexpr std::size_t kBoundsAt=0x11AD391,kBoundsDisp=0x11AD398,kBoundsNext=0x11AD39C,kStockBounds=0x17BE0E0;
const unsigned char kBoundsSig[]={0x89,0x5C,0x24,0x68,0x0F,0x28,0x0D,0x44,0x0D,0x61,0x00,0x0F,0x57,0xC0,0x0F,0x5C};
const unsigned char kStockDisp[]={0x44,0x0D,0x61,0x00};   // 0x17BE0E0 - 0x11AD39C
constexpr std::size_t kFmexVtable=0x176BF88,kFmexAttach=0x153EB0,kFmexMatrix=0x1200,kFmexBody=0x1240;
// havok_Body (ctor 0x17C9F0): the list of its body infos (cinfo) at +0xA8 (size +0xB0), the list of the bodies it
// made at +0xB8 (size +0xC0, filled by the attach 0x17DD40).
constexpr std::size_t kBodyInfoCount=0xB0,kBodyCount=0xC0;
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
        const std::size_t infos=At<std::size_t>(o+kFmexBody,kBodyInfoCount),bodies=At<std::size_t>(o+kFmexBody,kBodyCount);
        ++logged;
        Log("BIGWORLD map piece %p at (%.0f,%.0f,%.0f): %zu body infos, %zu bodies made",fmex,m[12],m[13],m[14],
            infos<100000 ? infos : 0,bodies<100000 ? bodies : 0);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
}  // namespace

bool InstallBigWorld() noexcept {
    __try {
        bool bounds=false;
        const float want=Cfg().bigWorld;
        if(!(want>3000.0f))Log("BIGWORLD off (ini BigWorld=0): the physics world stays +-3000 m");
        else if(!Matches(kBoundsAt,kBoundsSig,sizeof(kBoundsSig)) || !BoundsFour(kStockBounds,3000.0f))
            Log("BIGWORLD: unexpected EDF.dll code: the physics world stays +-3000 m");
        else {
            alignas(16) const float four[4]={want,want,want,want};
            void* const at=edf::AllocateNearCode(image+kBoundsNext,reinterpret_cast<const unsigned char*>(four),sizeof(four));
            const std::int64_t rel=at ? static_cast<unsigned char*>(at)-(image+kBoundsNext) : 0;
            if(!at || (reinterpret_cast<std::uintptr_t>(at)&15) || rel<INT32_MIN || rel>INT32_MAX)
                Log("BIGWORLD: no aligned page within reach: the physics world stays +-3000 m");
            else {
                const auto disp=static_cast<std::int32_t>(rel);
                unsigned char bytes[4];std::memcpy(bytes,&disp,4);
                bounds=edf::PatchCode(image+kBoundsDisp,kStockDisp,bytes,sizeof(bytes));
            }
            if(!bounds && at)VirtualFree(at,0,MEM_RELEASE);
        }
        bool pieces=false;
        if(Matches(kFmexAttach,kFmexAttachSig,sizeof(kFmexAttachSig))) {
            const auto slot=reinterpret_cast<void**>(image+kFmexVtable);
            void* const current=*slot;
            nextAttach=reinterpret_cast<AttachFn>(current);
            pieces=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&AttachHook));
        }
        Log("HOOK bigworld bounds=%d (+-%.0f m from the next mission load) pieces=%d",bounds,bounds ? want : 3000.0f,pieces);
        return bounds;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void ResetBigWorld() noexcept {
    logged=0;
}
}  // namespace crew
