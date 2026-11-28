// The bullet step's object shape cast against only the bodies it can reach (bullet_sweep_cull.h,
// docs/bullet-sweep-cull-re.md): the hook on the step-3 build's only call, 0x2382B6 in the batch step 0x237CF0.
#include "bullet_sweep_cull.h"
#include "bullet_sweep_cull_logic.h"
#include "config.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace crew {
namespace {
using namespace sweep_cull;
using BuildFn=std::uint64_t(__fastcall*)(unsigned char*,unsigned char*);
using BatchFn=void(__fastcall*)(void*,void*,unsigned);
using BodyFn=const unsigned char*(__fastcall*)(void*,unsigned);
using AabbFn=void(__fastcall*)(const void*,const void*,float*);
using HitsFn=const unsigned char*(__fastcall*)(void*);

bool counting=false,shadowing=false;
double tickMs=0;
struct Totals {
    std::atomic<unsigned long long> sweeps{0},candidates{0},culled{0},passed{0},ticks{0},shadowed{0},mismatches{0};
} totals;
std::atomic<unsigned> loggedMismatches{0};
thread_local unsigned shadowTick=0;

bool NativeCodeMatches(const Fingerprint& f) noexcept {
    const auto* const code=image+f.begin;
    const unsigned bytes=f.end-f.begin;
    if(!edf::Readable(code,bytes))return false;
    std::uint64_t fingerprint=14695981039346656037ull;
    for(unsigned i=0;i<bytes;++i)fingerprint=(fingerprint^code[i])*1099511628211ull;
    return fingerprint==f.fnv;
}

// The body store the build's fetch (0x11AA620) asks: *(*(image+kSystem)+0x10+0x58)+0x20, or null.
void* Store() noexcept {
    auto* const system=Rd<unsigned char*>(image,kSystem);
    if(!system)return nullptr;
    auto* const iface=Rd<unsigned char*>(system+kSystemWorld,kWorldIface);
    return iface ? iface+kIfaceStore : nullptr;
}

// The body's world box as the narrowphase sees the body: its shape (+0x78) in its transform (the body), through the
// shape type's calcAabb. False (keep the body) when any of it is missing or the box is not a box.
bool BodyBox(const unsigned char* body,const unsigned char* table,float lo[3],float hi[3]) noexcept {
    const auto* const shape=Rd<const unsigned char*>(body,kBodyShape);
    if(!shape)return false;
    const auto aabb=Rd<AabbFn>(table,static_cast<std::size_t>(shape[kShapeType])*kAabbStride+kAabbEntry);
    if(!aabb)return false;
    alignas(16) float box[8]{};
    aabb(shape,body,box);
    for(int k=0;k<3;++k) {
        lo[k]=box[k];hi[k]=box[4+k];
        if(!std::isfinite(lo[k]) || !std::isfinite(hi[k]) || lo[k]>hi[k])return false;
    }
    return true;
}

// The collector's hits after a cast: (count, then count * 0x70 bytes).
void TakeHits(unsigned char* core,std::vector<unsigned char>& out) {
    auto* const collector=Rd<unsigned char*>(core,kCoreHits);
    const int count=Rd<int>(collector,kHitCount);
    const unsigned n=count>0 ? static_cast<unsigned>(count) : 0u;
    out.assign(sizeof(n),0);
    std::memcpy(out.data(),&n,sizeof(n));
    if(!n)return;
    const auto hits=reinterpret_cast<HitsFn>(Rd<void* const*>(collector,0)[kHitsSlot])(collector);
    out.insert(out.end(),hits,hits+static_cast<std::size_t>(n)*kHitBytes);
}

// The narrowphase on `ids` (the core's list for the time), its hits into `out`; the command as it was after.
void CastOn(unsigned char* core,unsigned char* cmd,const unsigned* ids,unsigned n,const unsigned char* saved,
            std::vector<unsigned char>& out) {
    std::memcpy(Rd<unsigned*>(core,kCoreIds),ids,static_cast<std::size_t>(n)*sizeof(unsigned));
    std::memcpy(core+kCoreCount,&n,sizeof(n));
    out.clear();
    if(reinterpret_cast<BuildFn>(image+kBuild)(core,cmd)) {
        reinterpret_cast<BatchFn>(image+kShapeBatch)(Rd<unsigned char*>(image,kSystem)+kSystemWorld,cmd,1);
        TakeHits(core,out);
    }
    // The command's bytes as before, but its body array (the build's: its data may have grown).
    std::memcpy(cmd,saved,kCommandBodies);
    std::memcpy(cmd+kCommandBodiesEnd,saved+kCommandBodiesEnd,kCommandBytes-kCommandBodiesEnd);
}

// Debug: the whole list's hits and the cut list's; a difference logged and the whole list put back.
void Shadow(unsigned char* core,unsigned char* cmd,const std::vector<unsigned>& whole,unsigned kept) {
    thread_local std::vector<unsigned> cut;
    thread_local std::vector<unsigned char> a,b;
    const unsigned* const ids=Rd<unsigned*>(core,kCoreIds);
    cut.assign(ids,ids+kept);
    unsigned char saved[kCommandBytes];
    std::memcpy(saved,cmd,sizeof(saved));
    const unsigned n=static_cast<unsigned>(whole.size());
    CastOn(core,cmd,whole.data(),n,saved,a);
    CastOn(core,cmd,cut.data(),kept,saved,b);
    totals.shadowed.fetch_add(1,std::memory_order_relaxed);
    if(a==b)return;   // the cut list stays the core's
    totals.mismatches.fetch_add(1,std::memory_order_relaxed);
    const auto hitsOf=[](const std::vector<unsigned char>& v) noexcept { unsigned c=0;if(v.size()>=sizeof(c))std::memcpy(&c,v.data(),sizeof(c));return c; };
    const unsigned ha=hitsOf(a),hb=hitsOf(b);
    unsigned first=0;
    while(first<ha && first<hb && !std::memcmp(a.data()+4+first*kHitBytes,b.data()+4+first*kHitBytes,kHitBytes))++first;
    const auto field=[&](const std::vector<unsigned char>& v,unsigned h,unsigned i,std::size_t off) noexcept {
        unsigned x=0;if(i<h)std::memcpy(&x,v.data()+4+i*kHitBytes+off,sizeof(x));return x; };
    if(loggedMismatches.fetch_add(1,std::memory_order_relaxed)<32)
        Log("SWEEPCULL MISMATCH core %p: %u bodies, %u kept; hits %u whole, %u cut; first differing hit %u: body %u fraction %08X whole, body %u fraction %08X cut",
            static_cast<void*>(core),n,kept,ha,hb,first,field(a,ha,first,kHitBody),field(a,ha,first,kHitFraction),
            field(b,hb,first,kHitBody),field(b,hb,first,kHitFraction));
    std::memcpy(Rd<unsigned*>(core,kCoreIds),whole.data(),static_cast<std::size_t>(n)*sizeof(unsigned));
    std::memcpy(core+kCoreCount,&n,sizeof(n));
}

// The batch's step-3 build call (0x231720(core, command)). Not noexcept: the build's and the casts' exceptions go on.
std::uint64_t __fastcall Build(unsigned char* core,unsigned char* cmd) {
    const auto build=reinterpret_cast<BuildFn>(image+kBuild);
    const unsigned n=Rd<unsigned>(core,kCoreCount);
    auto* const ids=Rd<unsigned*>(core,kCoreIds);
    const float r=Rd<float>(core,kCoreRadius);
    void* const store=Cfg().sweepCull ? Store() : nullptr;
    const auto* const table=Rd<const unsigned char*>(image,kAabbTable);
    if(!store || !table || (Rd<unsigned>(core,kCoreFlags)&kFlagBlast) || n<2 || !ids || !(r>=0.0f) || !std::isfinite(r)) {
        totals.passed.fetch_add(1,std::memory_order_relaxed);
        return build(core,cmd);
    }
    LARGE_INTEGER t0{};
    if(counting)QueryPerformanceCounter(&t0);
    float from[3],to[3];
    std::memcpy(from,core+kCoreFrom,sizeof(from));
    std::memcpy(to,core+kCoreTo,sizeof(to));
    const double e=static_cast<double>(r)+kMargin;
    const bool shadow=shadowing && (++shadowTick&255)==0;
    thread_local std::vector<unsigned> whole;
    if(shadow)whole.assign(ids,ids+n);
    const auto body=reinterpret_cast<BodyFn>(Rd<void* const*>(store,0)[kStoreBodySlot]);
    const unsigned kept=Cut(ids,n,[&](unsigned id) noexcept {
        const unsigned char* const b=body(store,id);
        float lo[3],hi[3];
        return !b || !BodyBox(b,table,lo,hi) || SegmentTouchesBox(from,to,lo,hi,e);
    });
    std::memcpy(core+kCoreCount,&kept,sizeof(kept));
    if(counting){LARGE_INTEGER t1;QueryPerformanceCounter(&t1);totals.ticks.fetch_add(static_cast<unsigned long long>(t1.QuadPart-t0.QuadPart),std::memory_order_relaxed);}
    totals.sweeps.fetch_add(1,std::memory_order_relaxed);
    totals.candidates.fetch_add(n,std::memory_order_relaxed);
    totals.culled.fetch_add(n-kept,std::memory_order_relaxed);
    if(shadow && kept<n)Shadow(core,cmd,whole,kept);
    return build(core,cmd);
}
}  // namespace

bool InstallBulletSweepCull() noexcept {
    if(!image)return false;
    for(const auto& f:kFingerprints)
        if(!NativeCodeMatches(f)){Log("SWEEPCULL REFUSED: %X..%X not the stock code, nothing installed",f.begin,f.end);return false;}
    if(!edf::Matches(image,kBuildCall,kBuildCallStock,sizeof(kBuildCallStock))){Log("SWEEPCULL REFUSED: the build call %X not the stock code, nothing installed",kBuildCall);return false;}
    counting=Cfg().perfProbe!=0;
    shadowing=Cfg().debug;
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);tickMs=1000.0/static_cast<double>(f.QuadPart);
    bool changed=false;
    if(!edf::RedirectCall(image+kBuildCall,image+kBuild,reinterpret_cast<void*>(&Build),changed) || !changed) {
        Log("SWEEPCULL REFUSED: the build call %X could not be redirected, nothing installed",kBuildCall);
        return false;
    }
    Log("SWEEPCULL bullets' object shape casts only against the bodies the sweep can reach (ini SweepCull, now %d)%s",
        Cfg().sweepCull ? 1 : 0,shadowing ? " (shadow check every 256th cut)" : "");
    return true;
}

void ReportBulletSweepCull(unsigned frames) noexcept {
    const auto take=[](std::atomic<unsigned long long>& v) noexcept { return v.exchange(0,std::memory_order_relaxed); };
    const unsigned long long sweeps=take(totals.sweeps),candidates=take(totals.candidates),culled=take(totals.culled),
        passed=take(totals.passed),ticks=take(totals.ticks),shadowed=take(totals.shadowed),mismatches=take(totals.mismatches);
    if(!Cfg().perfProbe || !(sweeps+passed))return;
    const double n=frames ? frames : 1.0;
    Log("PERF SWEEPCULL %u frames: %llu sweeps (%.1f a frame), %llu candidate parts, %llu culled (%.1f%%), %llu left as stock; "
        "cut %.3f ms (%.3f a frame); shadow %llu checked %llu mismatched",
        frames,sweeps,static_cast<double>(sweeps)/n,candidates,culled,candidates ? 100.0*static_cast<double>(culled)/static_cast<double>(candidates) : 0.0,
        passed,static_cast<double>(ticks)*tickMs,static_cast<double>(ticks)*tickMs/n,shadowed,mismatches);
}
}  // namespace crew
