// The bullets' broadphase group expansion cut to the parts the sweep can reach (bullet_expand_cull.cpp;
// tests/bullet_expand_cull_test.cpp runs the hooks on a private EDF.dll mapping against the stock addBody;
// docs/bullet-expand-cull-re.md). Addresses: EDF.dll TimeDateStamp 0x678CCB46.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace crew::expand_cull {
// ---- stock EDF.dll ----
constexpr unsigned kAddBody=0x109DA0,kAddBodyEnd=0x10A002;        // hit_query::NpTargetCollector::addBody(this, body id)
constexpr std::uint64_t kAddBodyFnv=0x4cfbba1b6be3fbd5ull;          // FNV-1a over [kAddBody, kAddBodyEnd)
constexpr unsigned kAppend=0x11A9EC0,kAppendEnd=0x11A9F30;          // the collector's list append(this, id)
constexpr std::uint64_t kAppendFnv=0xd95186e44c367004ull;
// addBody's group walk: `mov rdi,[rdi+0A0h]; mov rbx,[rdi]; cmp rbx,rdi; je 109FEDh` and the loop after it. rbp the
// collector, rdi the group, esi the overlapped body's id; 0x109FED the epilogue (restores rbx / rbp / rsi).
constexpr unsigned kWalk=0x109F6F,kResume=0x109FED;
constexpr unsigned char kWalkStock[]{0x48,0x8B,0xBF,0xA0,0x00,0x00,0x00,0x48,0x8B,0x1F,0x48,0x3B,0xDF,0x74,0x6F};
// addBody's unwind info; the cave's: the same codes at offset 0, no prolog (it runs in addBody's frame as it is).
constexpr unsigned kAddBodyUnwind=0x1D2C420;
constexpr unsigned char kAddBodyUnwindStock[]{0x01,0x14,0x08,0x00,0x14,0x64,0x0B,0x00,0x14,0x54,0x0A,0x00,0x14,0x34,0x09,
                                              0x00,0x14,0x52,0x10,0x70};
constexpr unsigned char kCaveUnwind[]{0x01,0x00,0x08,0x00,0x00,0x64,0x0B,0x00,0x00,0x54,0x0A,0x00,0x00,0x34,0x09,
                                      0x00,0x00,0x52,0x00,0x70};
static_assert(sizeof(kCaveUnwind)==sizeof(kAddBodyUnwindStock));
// xgs World::CastShape(collector, world, &from, &to, &extent): the box of half size extent swept from -> to through
// the broadphase, each overlapped body to the collector's addBody. The bullet step's three calls (rcx = core+0x800):
// 0x234334 (move type 1), 0x234982 (2), 0x234B98 (3); all under the step 1 0x233BD0, whose only call is in the batch
// step 0x237CF0. Each core's ray (0x8A0 -> 0x8B0) and its step-3 sphere (radius 0xBE0 = the extent) lie in that box.
constexpr unsigned kCast=0x11A70B0;
constexpr unsigned kCastCalls[]{0x234334,0x234982,0x234B98};
// The batch step's only call (0x704EB0): one step of every bullet. The box cache's generation goes up per call.
constexpr unsigned kStep=0x237CF0,kStepCall=0x704ED4;
// The step-3 command build (core, the 0xB0-byte command) and its only call, for the debug shadow check.
constexpr unsigned kBuild=0x231720,kBuildCall=0x2382B6;
constexpr unsigned kShapeBatch=0x11A8E40;     // the narrowphase batch (world, commands, n)
constexpr unsigned kSystem=0x20B2958;         // the xgs System (+0x10 the world)
constexpr unsigned kAabbTable=0x20F1930;      // the shape types' function table (+type*0x100+0x18: calcAabb)
// The damage settle (core+0x6E0 the queue: +8 its 0xA0-byte entries, +0x18 their count; each sent to its target's
// slots 10 / 9 / 11, where a death is handled, synchronously) and its only call inside step 1: 0x23426F, move type 1,
// before that type's cast 0x234334 (the other settles of the step, 0x2321B0's from 0x23832D, come after every cast).
// A settle with entries may change bodies (a death: removed, re-added, moved, another shape): the box cache's
// generation goes up after it. Chained at the load's end (damagestats redirects the same call).
constexpr unsigned kSettle=0x543920,kSettleCall=0x23426F;
constexpr std::size_t kQueueCount=0x18;
// The ray command build (core, the 0x70-byte command: core+0x8C0 on and 0x8D0 ids at 0x8C8, the collector inline at
// core+0x8F0 with its count at +0xC and the closest hit at +0x30) and its only call, in the batch step right after
// each bullet's step 1; the commands' batch 0x11A7AC0 (world, commands, n) after every bullet's, then each command's
// hits read (0x238700: the hit body +0x48; 0x234D20: the count, core+0x920 to the ray's end). For the shadow check.
constexpr unsigned kRayBuild=0x231840,kRayBuildCall=0x238014,kRayBatch=0x11A7AC0;
constexpr std::size_t kCoreRayOn=0x8C0,kCoreRayIds=0x8C8,kCoreRayCount=0x8D0,kCoreRayHits=0x8F0,kCoreRayBest=0x920;
constexpr std::size_t kRayCommandBytes=0x70;

struct Fingerprint { unsigned begin,end; std::uint64_t fnv; };
// FNV-1a over [begin, end) of the stock code the cut rests on.
constexpr Fingerprint kFingerprints[]{
    {kAddBody,kAddBodyEnd,kAddBodyFnv},
    {kAppend,kAppendEnd,kAppendFnv},
    {0x231720,0x231839,0x5124e22565d3bcf3ull},   // the step-3 build: no box test, the list's bodies as they are
    {0x11AA620,0x11AA64C,0xa48660fd22e41482ull}, // the bodies' fetch (reader slot 0x80 per id)
    {0x11A8A90,0x11A8E37,0x7280951b6ac02ad2ull}, // CastShapeToBodies: each body's shape in its transform
    {0x11A8E40,0x11A9261,0x1d731dc397d98e2full}, // the narrowphase batch
    {0x2359C0,0x235AD5,0xc3d39767abcb1d19ull},   // the step-1 filter: an empty list -> no cast; 0x8C8 / 0x8D8 from 0x818
    {0x238286,0x2382E2,0x873472f412a15c86ull},   // the batch's step-3 loop and its 0x11A8E40 call
    {0x233C39,0x233C47,0x2e751e5799d11c8aull},   // step 1 clears core+0x8C1 every step
    {0x2321F8,0x232240,0xd15176f36bd07a35ull},   // the hit handling: collector slot 3 the hits, +0xC the count, 0x70 each
    {0xDAB170,0xDAB183,0x40943da61589fbf8ull},   // hknpWorld reader slot 0x80 getBody: bodies + (id & 0xFFFFFF) * 0xB0
    {0xDAC200,0xDAC234,0x52b86e4e8bfbccceull},   // slot 0x68 isValid: index in range, flags & 3, +0x50 == id (int 0 / 1)
    {0x234250,0x23427C,0x35488a59917b0ebbull},   // step 1 type 1: the settle 0x543920 (rcx core+0x6E0) or the queue cleared
    {0x543920,0x54396C,0x9cbf22e249abefc8ull},   // the settle's head: the entries +8, their count +0x18 (0: no loop)
    {0x231840,0x2318DF,0x96c38df7a8a77646ull},   // the ray build: 0x8C0 / 0x8D0, the ids at 0x8C8 fetched as they are
    {0x237FF0,0x2380AF,0x8a5b473701efeb0full},   // the batch: step 1, the ray build per bullet; the rays' batch; their hits
    {0x234D20,0x234D49,0xce61e3362f498030ull},   // the ray's hit: core+0x8FC the count, core+0x920 the closest
};

// The collector (core+0x800), the group, its nodes (a std::list: +0 next), the node's object.
constexpr std::size_t kCollectorCore=0x800;   // the bullet's collector is core+0x800
constexpr std::size_t kCollectorList=0x18,kCollectorCount=0x20,kCollectorCapacity=0x24;
constexpr unsigned kCapacityMask=0x3FFFFFFF;
constexpr std::size_t kCollectorFilter=0x80;  // a functor (vtable +0x10: bool(functor, unsigned* id)) or null
constexpr std::size_t kGroupList=0xA0;
constexpr std::size_t kNodeBody=0x10,kNodeOn=0xA0;
constexpr std::size_t kBodyId=0xF0;
// The bullet core.
constexpr std::size_t kCoreIds=0x8D8,kCoreCount=0x8E0,kCoreFlags=0xAF4,kCoreHits=0x990;
constexpr unsigned kFlagBlast=0x1000;         // set at the bullet's setup only (0x231D70); its 0x8D8 list the blast's query
// The command (0xB0): the body pointers' hkArray at +0x30, rewritten by every build.
constexpr std::size_t kCommandBytes=0xB0,kCommandBodies=0x30,kCommandBodiesEnd=0x40;
// The world (the cast's rdx, System+0x10): +0x58 the hknpWorld, its reader interface at +0x20 (vtable 0x18BC9C0).
constexpr std::size_t kWorldHknp=0x58,kHknpReader=0x20;
constexpr unsigned kReaderValidSlot=0x68/8;   // int isValid(id): the filter 0x1079D0 asks it before the fetch
constexpr unsigned kReaderBodySlot=0x80/8;    // hknpBody* (id)
constexpr std::size_t kBodyShape=0x78,kShapeType=0x18,kAabbStride=0x100,kAabbEntry=0x18;
// The hit collector: vtable slot 3 the hits, +0xC their count; a hit 0x70 bytes.
constexpr unsigned kHitsSlot=0x18/8;
constexpr std::size_t kHitCount=0xC,kHitBytes=0x70;
// The hit fields the game reads (0x2321B0 / 0x230CA0 / 0x238700): the position xyz, the normal xyz, the fraction, the
// hit body's id. 0x230CA0 copies the two vectors whole (w too); no reader of the copies' w was found.
constexpr std::size_t kHitPos=0x00,kHitNormal=0x10,kHitFraction=0x20,kHitBody=0x48;
constexpr std::size_t kHitReadDwords[]{0,1,2,4,5,6,8,0x12};

// A body whose world box (its shape's calcAabb in its transform) is farther from the swept segment than the extent
// plus this cannot be hit by the ray or the sphere (the cast's accuracy 0.001; 10 km floats 0.001 apart).
constexpr double kMargin=0.1;

template<class T> inline T Rd(const void* p,std::size_t off) noexcept { T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof(T));return v; }

// The cast's sweep: the segment from -> to swept by a cube of half size e = max |extent| + kMargin (a superset of the
// box the broadphase sweeps, of the ray and of the step-3 sphere). Touches: the boxes overlap and the slab test (in
// doubles, the reciprocals once a cast).
struct Sweep {
    double lo[3],hi[3];        // the swept box: min(from, to) - e .. max(from, to) + e
    float flo[3],fhi[3];       // the same rounded outward to floats: the first, cheap test
    double s[3],inv[3];
    bool flat[3];
    double e;
    // False (do not cut) for a non-finite input.
    bool Set(const float from[3],const float to[3],const float extent[3]) noexcept {
        double m=0;
        for(int k=0;k<3;++k) {
            if(!std::isfinite(from[k]) || !std::isfinite(to[k]) || !std::isfinite(extent[k]))return false;
            m=(std::fmax)(m,std::fabs(static_cast<double>(extent[k])));
        }
        e=m+kMargin;
        for(int k=0;k<3;++k) {
            const double a=from[k],b=to[k];
            lo[k]=(std::fmin)(a,b)-e;hi[k]=(std::fmax)(a,b)+e;
            s[k]=a;
            const double d=b-a;
            flat[k]=d==0.0;
            inv[k]=flat[k] ? 0.0 : 1.0/d;
            flo[k]=static_cast<float>(lo[k]);
            if(static_cast<double>(flo[k])>lo[k])flo[k]=std::nextafter(flo[k],-std::numeric_limits<float>::infinity());
            fhi[k]=static_cast<float>(hi[k]);
            if(static_cast<double>(fhi[k])<hi[k])fhi[k]=std::nextafter(fhi[k],std::numeric_limits<float>::infinity());
        }
        return true;
    }
    bool Touches(const float blo[3],const float bhi[3]) const noexcept {
        // The boxes' overlap in floats (flo <= lo, fhi >= hi: never refuses what the doubles keep); most parts end here.
        if(!((blo[0]<=fhi[0]) & (bhi[0]>=flo[0]) & (blo[1]<=fhi[1]) & (bhi[1]>=flo[1]) & (blo[2]<=fhi[2]) & (bhi[2]>=flo[2])))return false;
        for(int k=0;k<3;++k)
            if(!(static_cast<double>(blo[k])<=hi[k] && static_cast<double>(bhi[k])>=lo[k]))return false;
        double t0=0.0,t1=1.0;
        for(int k=0;k<3;++k) {
            if(flat[k])continue;   // the overlap above: s within [blo - e, bhi + e]
            double u=(static_cast<double>(blo[k])-e-s[k])*inv[k],v=(static_cast<double>(bhi[k])+e-s[k])*inv[k];
            if(u>v){const double w=u;u=v;v=w;}
            if(u>t0)t0=u;
            if(v<t1)t1=v;
            if(t0>t1)return false;
        }
        return true;
    }
};

// One generation's body boxes on one thread (a step's, up to a damage settle): direct-mapped by the body index (id & 0xFFFFFF), tagged with the whole id and
// the generation (0 never one). An unbounded box (-inf .. +inf: no shape, no calcAabb, not a box) is kept by
// every sweep.
struct BoxEntry { float lo[3]; unsigned id; float hi[3]; unsigned gen; };
static_assert(sizeof(BoxEntry)==32);
class BoxCache {
public:
    static constexpr unsigned kBits=17,kSlots=1u<<kBits;
    explicit BoxCache(BoxEntry* storage) noexcept : slots(storage) {}   // kSlots zeroed entries
    // The id's slot: its box this step when Current, else to be filled (Fill).
    BoxEntry& Slot(unsigned id) noexcept { return slots[id&(kSlots-1)]; }
    static bool Current(const BoxEntry& s,unsigned id,unsigned gen) noexcept { return s.gen==gen && s.id==id; }
    // `ok` false: unbounded (kept by every sweep).
    static void Fill(BoxEntry& s,unsigned id,unsigned gen,bool ok) noexcept { if(!ok)Unbounded(s);s.id=id;s.gen=gen; }
    static void Unbounded(BoxEntry& s) noexcept {
        const float inf=std::numeric_limits<float>::infinity();
        s.lo[0]=s.lo[1]=s.lo[2]=-inf;s.hi[0]=s.hi[1]=s.hi[2]=inf;
    }
private:
    BoxEntry* slots;
};

struct Stats {
    unsigned long long casts=0,culling=0,blast=0,off=0;   // bullet casts; of them cut / a blast bullet / ExpandCull=0 or no world
    unsigned long long walks=0,parts=0,kept=0,culled=0,keptOne=0;   // the cut walks: their active parts, kept, culled, kept to stay non-empty
    unsigned long long lookups=0,misses=0,unbounded=0;    // the box cache
    unsigned long long walkTicks=0,boxTicks=0;            // TSC: the cut walks (the box computes in them)
    unsigned long long outside=0,filtered=0;              // walks as stock: outside a bullet cast / with a filter functor
    unsigned long long sampled=0,shadowed=0,mismatches=0,unreadDiffs=0;
    unsigned unreadMask=0;                                // the 0x70-byte hits' dwords that differed but no reader reads
    unsigned long long unarmed=0;                         // casts left whole: the settle not chained (yet)
    unsigned long long rechecked=0,stale=0;               // a body's box filled again after a settle in the step; changed
    unsigned long long settleSampled=0,settleShadowed=0,settleMismatches=0;   // samples: the first cast after a settle
    unsigned long long spanShadowed=0,spanMismatches=0;   // samples with a settle between the cast and the step-3 build
    unsigned long long rayShadowed=0,rayMismatches=0,unmatched=0;   // the ray list's check; a list not the record's
};
}  // namespace crew::expand_cull
