// The rubble scan (physics.cpp HeavyChassis): the debris-preset bodies within kRadius of a heavy chassis get the DEBRIS
// category on their material (physics.cpp, the chassis mass ratio). Pure logic over the hknp body array and material
// table; tools/rubble_pass_check.cpp tests it offline against the per-chassis scan it replaced.
//
// Until 2026-11-07 each chassis walked the whole body array (up to 0x40000 bodies, stride 0xB0) on its own every
// kScanMs: K chassis, K full walks, the walk being one cache miss per body (stress:100: 4.8 ms a frame). Now each
// chassis step only notes where the chassis is (Note), and every kScanMs one pass walks the array once and tests each
// debris body against every chassis noted since the last pass. A material is marked when any chassis' own scan would
// have marked it: the same union, one walk.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rubble {
constexpr std::uint64_t kScanMs=250;
constexpr float kRadius=15.0f;
// hknpBody (stride 0xB0): position +0x30, id +0x50 (low 24 bits: its index), flags +0x54 (motion &7), quality +0x89,
// material id +0x8A. hknpMaterial (stride 0x50): flags +0x14, category +0x30.
constexpr std::size_t kBodyStride=0xB0,kBodyPos=0x30,kBodyId=0x50,kBodyFlags=0x54,kBodyQuality=0x89,kBodyMaterial=0x8A;
constexpr std::size_t kMaterialStride=0x50,kMatFlags=0x14,kMatCategory=0x30;
constexpr std::uint32_t kMotionMask=7,kDynamic=2,kFlagMassChanger=0x800000;
constexpr std::uint8_t kQualityDebris=1,kCategoryDebris=1;
constexpr std::int32_t kMostBodies=0x40000;
constexpr int kMost=1024;            // chassis noted between two passes; a full window passes early (Note: false)
constexpr int kSlots=2*kMost;        // the wrapper -> entry index (open addressing, a power of 2)

template<class T> inline T Get(const void* base,std::size_t offset) noexcept {
    T v;std::memcpy(&v,static_cast<const unsigned char*>(base)+offset,sizeof(T));return v;
}
template<class T> inline void Set(void* base,std::size_t offset,T v) noexcept {
    std::memcpy(static_cast<unsigned char*>(base)+offset,&v,sizeof(T));
}

struct Chassis { const void* wrapper; float at[3]; std::uint16_t own; std::uint64_t seen; };
struct Window {
    const void* world;          // the body world (its iface) the chassis below were stepped in
    bool passed;                // a pass has run since the world was taken (last valid)
    std::uint64_t last;         // the last pass (ms)
    int n;
    Chassis c[kMost];
    std::int16_t index[kSlots];   // -1: empty
};

inline unsigned Home(const void* wrapper) noexcept {
    const auto p=reinterpret_cast<std::uintptr_t>(wrapper);
    return static_cast<unsigned>((static_cast<std::uint64_t>(p>>4)*0x9E3779B97F4A7C15ull)>>32)&(kSlots-1);
}
inline void Clear(Window& w) noexcept { w.n=0;std::memset(w.index,0xFF,sizeof w.index); }

// The chassis `wrapper` (its body's position `at`, its own material `own`) stepped at `now` in `world`. Another world
// than the noted chassis' drops them (their positions belong to a world not walked now) and starts a new throttle.
// False when the window is full and the chassis is not in it: the caller passes now (Pass) and notes it again.
inline bool Note(Window& w,const void* world,const void* wrapper,const float* at,std::uint16_t own,
                 std::uint64_t now) noexcept {
    if(w.world!=world){Clear(w);w.world=world;w.passed=false;}
    unsigned k=Home(wrapper);
    for(;;k=(k+1)&(kSlots-1)) {
        const int i=w.index[k];
        if(i<0)break;
        if(w.c[i].wrapper==wrapper) {
            std::memcpy(w.c[i].at,at,12);w.c[i].own=own;w.c[i].seen=now;
            return true;
        }
    }
    if(w.n>=kMost)return false;
    w.index[k]=static_cast<std::int16_t>(w.n);
    Chassis& c=w.c[w.n++];
    c.wrapper=wrapper;std::memcpy(c.at,at,12);c.own=own;c.seen=now;
    return true;
}

// A pass is due: the first in this world, or kScanMs after the last.
inline bool Due(const Window& w,std::uint64_t now) noexcept { return !w.passed || now-w.last>=kScanMs; }

// One walk of `count` bodies at `bodies` against every chassis noted within kScanMs: a dynamic DEBRIS-quality body
// within kRadius of a chassis whose own material it does not share, its material (of `materialCount` at `materials`)
// still category 0, becomes DEBRIS (category 1, the mass-changer flag); `marked(body, material)` is called just before
// (the log). The window is emptied, the throttle restarted.
template<class F>
void Pass(Window& w,const unsigned char* bodies,std::int32_t count,unsigned char* materials,std::int32_t materialCount,
          std::uint64_t now,F&& marked) noexcept {
    // Chassis seen long ago (stepped no more: gone, or out of the world) do not count.
    float at[kMost][3];
    std::uint16_t own[kMost];
    int n=0;
    for(int i=0;i<w.n;++i) {
        if(now-w.c[i].seen>=kScanMs)continue;
        std::memcpy(at[n],w.c[i].at,12);own[n]=w.c[i].own;++n;
    }
    Clear(w);w.passed=true;w.last=now;
    if(!n || !bodies || !materials || materialCount<=0)return;
    constexpr float r2=kRadius*kRadius;
    for(std::int32_t b=0;b<count && b<kMostBodies;++b) {
        const unsigned char* body=bodies+std::size_t(b)*kBodyStride;
        if((Get<std::uint32_t>(body,kBodyId)&0xFFFFFF)!=std::uint32_t(b))continue;
        if((Get<std::uint32_t>(body,kBodyFlags)&kMotionMask)!=kDynamic)continue;
        if(Get<std::uint8_t>(body,kBodyQuality)!=kQualityDebris)continue;
        const std::uint16_t id=Get<std::uint16_t>(body,kBodyMaterial);
        const float p[3]={Get<float>(body,kBodyPos),Get<float>(body,kBodyPos+4),Get<float>(body,kBodyPos+8)};
        bool close=false;
        for(int k=0;k<n && !close;++k) {
            if(id==own[k])continue;
            const float dx=p[0]-at[k][0],dy=p[1]-at[k][1],dz=p[2]-at[k][2];
            close=dx*dx+dy*dy+dz*dz<=r2;
        }
        if(!close || id>=materialCount)continue;
        unsigned char* const material=materials+std::size_t{id}*kMaterialStride;
        if(Get<std::uint8_t>(material,kMatCategory)!=0)continue;
        marked(body,material);
        Set<std::uint32_t>(material,kMatFlags,Get<std::uint32_t>(material,kMatFlags)|kFlagMassChanger);
        Set<std::uint8_t>(material,kMatCategory,kCategoryDebris);
    }
}
}  // namespace rubble
