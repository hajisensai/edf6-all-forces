// The bullet step's object shape cast, its target list cut to the bodies the cast can reach (bullet_sweep_cull.cpp;
// tests/bullet_sweep_cull_test.cpp; docs/bullet-sweep-cull-re.md). Addresses: EDF.dll TimeDateStamp 0x678CCB46.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::sweep_cull {
// ---- stock EDF.dll ----
// The step-3 command build (core, the 0xB0-byte command): the hit collector's reset, then with no core+0xAF4 bit 0x1000
// and a non-empty core+0x8D8 list the sphere (radius core+0xBE0) cast from core+0x8A0 to core+0x8B0 against the list's
// bodies (0x11AA620 fetches them). Its only call: 0x2382B6 in the batch step 0x237CF0, for each bullet with core+0x8C1
// set and core+0x8E0 > 0.
constexpr unsigned kBuild=0x231720,kBuildCall=0x2382B6;
constexpr unsigned char kBuildCallStock[]{0xE8,0x65,0x94,0xFF,0xFF};
// The narrowphase batch (0x11A8E40(*(image+kSystem)+0x10, commands, n)), for the debug shadow check.
constexpr unsigned kShapeBatch=0x11A8E40;
constexpr unsigned kSystem=0x20B2958;          // the xgs System (+0x10 the world: +0x58 its interface, +0x20 the body store)
constexpr unsigned kAabbTable=0x20F1930;       // the shape types' function table (+type*0x100+0x18: calcAabb)

struct Fingerprint { unsigned begin,end; std::uint64_t fnv; };
// FNV-1a over [begin, end) of the stock code (each function's .pdata primary chunk, or the window named).
constexpr Fingerprint kFingerprints[]{
    {0x231720,0x231839,0x5124e22565d3bcf3ull},   // the command build
    {0x11AA620,0x11AA64C,0xa48660fd22e41482ull}, // the bodies' fetch (store slot 0x80 per id)
    {0x11A8A90,0x11A8E37,0x7280951b6ac02ad2ull}, // CastShapeToBodies: each body's shape +0x78 in its transform, no box test
    {0x11A8E40,0x11A9261,0x1d731dc397d98e2full},    // the batch
    {0x2359C0,0x235AD5,0xc3d39767abcb1d19ull},   // the step-1 filter: 0x8E0 cleared, 0x8A0 / 0x8B0 / 0x8C1 set, 0x8D8 refilled
    {0x238286,0x2382E2,0x873472f412a15c86ull},   // the batch's step-3 loop and its 0x11A8E40 call
    {0x237D12,0x237D24,0x93f451ea51fdae23ull},   // the batch's world: *(image+kSystem)+0x10
    {0x233C39,0x233C47,0x2e751e5799d11c8aull},   // step 1 clears core+0x8C1 for every bullet, every step
    {0x2321F8,0x232240,0xd15176f36bd07a35ull},   // the hit handling: collector slot 3 the hits, +0xC the count, 0x70 each
};

// The bullet core.
constexpr std::size_t kCoreFrom=0x8A0,kCoreTo=0x8B0,kCoreIds=0x8D8,kCoreCount=0x8E0;
constexpr std::size_t kCoreFlags=0xAF4,kCoreRadius=0xBE0,kCoreHits=0x990;
constexpr unsigned kFlagBlast=0x1000;          // no shape cast: the blast's point query over the 0x8D8 list (0x2321B0)
// The command (0xB0): the body pointers' hkArray at +0x30 (data, size, capacity), rewritten by every build.
constexpr std::size_t kCommandBytes=0xB0,kCommandBodies=0x30,kCommandBodiesEnd=0x40;
// The world and its body store.
constexpr std::size_t kSystemWorld=0x10,kWorldIface=0x58,kIfaceStore=0x20;
constexpr unsigned kStoreBodySlot=0x80/8;      // hknpBody* (id)
constexpr std::size_t kBodyShape=0x78,kShapeType=0x18,kAabbStride=0x100,kAabbEntry=0x18;
// The hit collector: vtable slot 3 the hits, +0xC their count; a hit 0x70 bytes, +0x20 its fraction, +0x48 the body id.
constexpr unsigned kHitsSlot=0x18/8;
constexpr std::size_t kHitCount=0xC,kHitBytes=0x70,kHitFraction=0x20,kHitBody=0x48;
constexpr unsigned kMostHits=0x32;             // the command's +0xA4

// A body whose world box (its shape's calcAabb in its transform: what the narrowphase casts against) is farther from
// the segment than the sphere's radius plus this cannot be hit (docs: the cast's accuracy is 0.001; the floats of
// a 10 km coordinate are 0.001 apart).
constexpr double kMargin=0.1;

template<class T> inline T Rd(const void* p,std::size_t off) noexcept { T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof(T));return v; }

// Whether the segment a -> b, swept by a cube of half size e (a superset of the sphere of radius e), touches the box
// [lo, hi]: the slab test in doubles. A degenerate axis (no motion along it) touches when a's coordinate is in the slab.
inline bool SegmentTouchesBox(const float a[3],const float b[3],const float lo[3],const float hi[3],double e) noexcept {
    double t0=0.0,t1=1.0;
    for(int k=0;k<3;++k) {
        const double l=static_cast<double>(lo[k])-e,h=static_cast<double>(hi[k])+e;
        const double s=a[k],d=static_cast<double>(b[k])-s;
        if(d==0.0) {
            if(s<l || s>h)return false;
            continue;
        }
        double u=(l-s)/d,v=(h-s)/d;
        if(u>v){const double w=u;u=v;v=w;}
        if(u>t0)t0=u;
        if(v<t1)t1=v;
        if(t0>t1)return false;
    }
    return true;
}

// The ids in place, the order kept, those `reach(id)` refuses taken out; at least the first kept (the list stays
// non-empty: the build and the batch take the same path as stock's). Returns the count kept.
template<class Reach> unsigned Cut(unsigned* ids,unsigned n,Reach&& reach) {
    unsigned kept=0;
    for(unsigned i=0;i<n;++i)
        if(reach(ids[i]))ids[kept++]=ids[i];
    if(!kept && n)kept=1;   // nothing was written: ids[0] is still the first id
    return kept;
}

struct Stats {
    unsigned long long sweeps=0;      // builds the cut looked at (the switch on, no blast, 2+ bodies)
    unsigned long long candidates=0;  // their bodies
    unsigned long long culled=0;      // of them taken out
    unsigned long long passed=0;      // builds left as stock (the switch off, a blast, 0 / 1 body)
    unsigned long long ticks=0;       // the cut's QPC ticks (PerfProbe)
    unsigned long long shadowed=0,mismatches=0;
};
}  // namespace crew::sweep_cull
