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
#include "body506.h"
#include "heli.h"
#include "memory.h"
#include <cmath>
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

namespace {
// The map's ground as the physics sees it (the user, 2026-10-05: the big map; the far ground pieces reach about
// +-5.6 km in their models, but beyond some distance the map ray finds nothing): once a mission, kProbeAfterMs into
// it, a ray straight down every kProbeStep m over +-kProbeHalf cells: a log row a cell line, '.' no ground, a digit its
// height band (kProbeBand m a step from the lowest seen, '+' above 9 bands). The tiling is laid out from this.
constexpr int kProbeHalf=16;
constexpr float kProbeStep=500.0f,kProbeTop=3000.0f,kProbeBottom=-3000.0f,kProbeBand=25.0f;
constexpr ULONGLONG kProbeAfterMs=5000;
ULONGLONG probeFrom=0;
bool probed=false;
}  // namespace

// The move area (docs/map-edge-re.md §1): the map's own move_limit box (the test range +-999 m) clamps players, NPCs
// and vehicles; with the world raised (ini BigWorld) it is widened, once a mission when the probe runs, to the
// world's bounds less kMoveMargin, its centre and height as the map set them (0x5AA5C0(manager, {centre, half})).
constexpr std::size_t kMoveArea=0x20B2998,kMoveMin=0x10,kMoveMax=0x20;
constexpr unsigned kSetMoveBox=0x5AA5C0;
constexpr float kMoveMargin=100.0f;
const unsigned char kSetMoveBoxSig[]={0x48,0x83,0xEC,0x18,0x0F,0x10,0x12,0x0F,0x28,0xDA,0x0F,0xC6,0xD2,0xFF};

void WidenMoveArea() noexcept {
    const float want=Cfg().bigWorld-kMoveMargin;
    if(!(want>1000.0f) || !Matches(kSetMoveBox,kSetMoveBoxSig,sizeof(kSetMoveBoxSig)))return;
    const auto p=At<unsigned char*>(image,kMoveArea);
    unsigned char* const m=p ? p-8 : nullptr;
    if(!m || !Readable(m,kMoveMax+16))return;
    const float* lo=reinterpret_cast<const float*>(m+kMoveMin);
    const float* hi=reinterpret_cast<const float*>(m+kMoveMax);
    alignas(16) float box[8]={(lo[0]+hi[0])*0.5f,(lo[1]+hi[1])*0.5f,(lo[2]+hi[2])*0.5f,1.0f,
                              want,(hi[1]-lo[1])*0.5f,want,0.0f};
    const float was[2]={hi[0]-lo[0],hi[2]-lo[2]};
    reinterpret_cast<void(__fastcall*)(void*,const float*)>(image+kSetMoveBox)(m,box);
    Log("BIGWORLD move area %.0f x %.0f m -> %.0f x %.0f m (centre %.0f,%.0f)",was[0],was[1],2.0f*want,2.0f*want,box[0],box[2]);
}

// The sky four times as high (the user, 2026-10-05: "the sky still has a limit; raise the stock one four times").
// The move area manager m holds two heights: +0x44 the ceiling the heli input clamps to (patched out, body506.cpp)
// and the NPC jets keep under (CeilingY), +0x40 the top the area clamp holds every body under (0x5A9E50 bit 1: the
// lower of it and the box's top). Both kSkyScale times as high once a mission, and the box's top raised to the new
// limit when lower, so nothing still stops a climb at the stock height.
constexpr std::size_t kAreaTop=0x40,kAreaCeiling=0x44;
constexpr float kSkyScale=4.0f;
void RaiseSky() noexcept {
    const auto p=At<unsigned char*>(image,kMoveArea);
    unsigned char* const m=p ? p-8 : nullptr;
    if(!m || !Readable(m,kAreaCeiling+4,true))return;
    const float top=At<float>(m,kAreaTop),ceiling=At<float>(m,kAreaCeiling);
    if(!(top>0.0f && top<1.0e5f) || !(ceiling>0.0f && ceiling<1.0e5f))return;
    Put<float>(m,kAreaTop,top*kSkyScale);
    Put<float>(m,kAreaCeiling,ceiling*kSkyScale);
    const float* lo=reinterpret_cast<const float*>(m+kMoveMin);
    const float* hi=reinterpret_cast<const float*>(m+kMoveMax);
    const float wantTop=top*kSkyScale,boxTop=hi[1];
    if(boxTop<wantTop && Matches(kSetMoveBox,kSetMoveBoxSig,sizeof(kSetMoveBoxSig))) {
        alignas(16) float box[8]={(lo[0]+hi[0])*0.5f,(lo[1]+wantTop)*0.5f,(lo[2]+hi[2])*0.5f,1.0f,
                                  (hi[0]-lo[0])*0.5f,(wantTop-lo[1])*0.5f,(hi[2]-lo[2])*0.5f,0.0f};
        reinterpret_cast<void(__fastcall*)(void*,const float*)>(image+kSetMoveBox)(m,box);
    }
    Log("SKY area top %.0f -> %.0f m, ceiling %.0f -> %.0f m, box top %.0f -> %.0f m",top,top*kSkyScale,ceiling,
        ceiling*kSkyScale,boxTop,std::fmax(boxTop,wantTop));
}

void BigWorldProbe() noexcept {
    const ULONGLONG ms=GameMs();
    if(probed)return;
    if(!probeFrom){probeFrom=ms;return;}
    if(ms-probeFrom<kProbeAfterMs)return;
    probed=true;
    WidenMoveArea();
    RaiseSky();   // after the widening: it keeps the box's height as it found it
    constexpr int n=2*kProbeHalf+1;
    static float height[n][n];
    float low=1e9f,high=-1e9f;
    int found=0;
    for(int r=0;r<n;++r)for(int c=0;c<n;++c) {
        const float x=static_cast<float>(c-kProbeHalf)*kProbeStep,z=static_cast<float>(kProbeHalf-r)*kProbeStep;
        const float a[3]={x,kProbeTop,z},b[3]={x,kProbeBottom,z};
        float hit[3];
        height[r][c]=MapRay(a,b,hit)>=0.0f ? hit[1] : kNoGround;
        if(height[r][c]!=kNoGround){++found;low=std::fmin(low,hit[1]);high=std::fmax(high,hit[1]);}
    }
    Log("BIGWORLD probe: %d of %d cells (%.0f m apart, +-%.0f m) have ground, %.0f to %.0f m; rows from +z down, columns from -x;"
        " digit = %.0f m bands over the lowest",found,n*n,kProbeStep,kProbeStep*kProbeHalf,found ? low : 0.0f,found ? high : 0.0f,kProbeBand);
    for(int r=0;r<n;++r) {
        char line[n+1];
        for(int c=0;c<n;++c) {
            const float h=height[r][c];
            const int band=h==kNoGround ? -1 : static_cast<int>((h-low)/kProbeBand);
            line[c]=band<0 ? '.' : band>9 ? '+' : static_cast<char>('0'+band);
        }
        line[n]=0;
        Log("BIGWORLD probe z=%+6.0f %s",static_cast<float>(kProbeHalf-r)*kProbeStep,line);
    }
}

void ResetBigWorld() noexcept {
    logged=0;probeFrom=0;probed=false;
}
}  // namespace crew
