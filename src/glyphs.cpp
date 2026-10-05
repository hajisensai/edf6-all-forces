// The game's glyph cache, made safe to share between threads (docs/hud-re.md §2.2).
// Every font keeps one glyph cache: an unordered_map of glyphs (character + scale), each in a cell of one of a
// few atlas pages, the pages' CPU copies copied to their GPU textures when marked dirty, and a sweep (every
// 0.3 s of wall clock, from the text renderer's End) that frees the cells of glyphs left unused. Stock EDF.dll
// locks only the GDI rasterization (the spin lock at font+0x29F0, 0x1180ED0); the map, the cells, the dirty
// flags and the sweep are touched from every thread that draws or measures text with no lock at all. Two
// threads in it at once lose a page's dirty mark or hand a cell to a second glyph: a character then shows as
// whatever glyph last sat in its cell (a class name's 剑 as 水) or not at all, and stays so while it is cached.
// Here every call into the cache (the 10 call sites of its 4 entry points) runs under one lock, taken before
// the game's own GDI lock (always in that order: no deadlock). The first entry from each thread is logged
// ("GLYPH thread"), the evidence of how many threads share the cache.
#include "crew.h"
#include "memory.h"
#include <cstdint>
#include <cstring>
#include <iterator>

namespace crew {
namespace {
using Arg=std::uintptr_t;
using PtrFn=Arg(__fastcall*)(Arg,Arg,Arg,Arg);
using FloatFn=float(__fastcall*)(Arg,Arg,Arg,Arg);
using VoidFn=void(__fastcall*)(Arg,Arg,Arg,Arg);

// The cache's entry points (EDF.dll TimeDateStamp 0x678CCB46) and the calls that reach them.
constexpr unsigned kLookup=0x117B340;   // (cache, char, draw): the glyph, rasterized into a cell when drawn
constexpr unsigned kAdvance=0x117B5E0;  // (cache, char): the glyph's advance, a metrics-only glyph added on a miss
constexpr unsigned kSweep=0x117AFA0;    // (cache): ages the glyphs, frees the cells of those unused 3 sweeps
constexpr unsigned kUpload=0x11740B0;   // (font, -, page): copies a dirty page to its texture, clears the mark
constexpr unsigned kLookupCalls[]={0x1139767,0x1139901,0x113BEB2,0x113C07D,0x113C877};
constexpr unsigned kAdvanceCalls[]={0x11398E0,0x113C856};
constexpr unsigned kSweepCalls[]={0x113C6C5};
constexpr unsigned kUploadCalls[]={0x113BEED,0x113C3EC};
const unsigned char kLookupSig[]={0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41};
const unsigned char kAdvanceSig[]={0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x30,0xF3,0x0F};
const unsigned char kSweepSig[]={0x48,0x89,0x5C,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B};
const unsigned char kUploadSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x41,0x0F};

CRITICAL_SECTION lock;
DWORD threads[16];   // the threads seen in the cache (written under the lock)
int threadCount=0;

void Enter(const char* entry) noexcept {
    EnterCriticalSection(&lock);
    const DWORD self=GetCurrentThreadId();
    for(int i=0;i<threadCount;++i)if(threads[i]==self)return;
    if(threadCount<static_cast<int>(std::size(threads)))threads[threadCount++]=self;
    Log("GLYPH thread %lu enters the glyph cache (first by %s): %d thread(s) so far",self,entry,threadCount);
}

Arg __fastcall LookupHook(Arg a,Arg b,Arg c,Arg d) {
    Enter("lookup");
    __try { return reinterpret_cast<PtrFn>(image+kLookup)(a,b,c,d); }
    __finally { LeaveCriticalSection(&lock); }
}
float __fastcall AdvanceHook(Arg a,Arg b,Arg c,Arg d) {
    Enter("advance");
    __try { return reinterpret_cast<FloatFn>(image+kAdvance)(a,b,c,d); }
    __finally { LeaveCriticalSection(&lock); }
}
void __fastcall SweepHook(Arg a,Arg b,Arg c,Arg d) {
    Enter("sweep");
    __try { reinterpret_cast<VoidFn>(image+kSweep)(a,b,c,d); }
    __finally { LeaveCriticalSection(&lock); }
}
Arg __fastcall UploadHook(Arg a,Arg b,Arg c,Arg d) {
    Enter("upload");
    __try { return reinterpret_cast<PtrFn>(image+kUpload)(a,b,c,d); }
    __finally { LeaveCriticalSection(&lock); }
}

struct Entry { unsigned target; const unsigned char* sig; std::size_t sigSize; const unsigned* sites; std::size_t count; void* hook; };
const Entry kEntries[]={
    {kLookup,kLookupSig,sizeof(kLookupSig),kLookupCalls,std::size(kLookupCalls),reinterpret_cast<void*>(&LookupHook)},
    {kAdvance,kAdvanceSig,sizeof(kAdvanceSig),kAdvanceCalls,std::size(kAdvanceCalls),reinterpret_cast<void*>(&AdvanceHook)},
    {kSweep,kSweepSig,sizeof(kSweepSig),kSweepCalls,std::size(kSweepCalls),reinterpret_cast<void*>(&SweepHook)},
    {kUpload,kUploadSig,sizeof(kUploadSig),kUploadCalls,std::size(kUploadCalls),reinterpret_cast<void*>(&UploadHook)},
};
// Whether EDF+site is still the stock rel32 call to EDF+target.
bool CallsTarget(unsigned site,unsigned target) noexcept {
    const unsigned char* p=image+site;
    if(!Readable(p,5) || p[0]!=0xE8)return false;
    std::int32_t rel;
    std::memcpy(&rel,p+1,4);
    return p+5+rel==image+target;
}
}  // namespace

// All or nothing: a cache locked at some of its calls only is no safer than none, so a mismatch anywhere
// leaves every call stock.
bool InstallGlyphLock() noexcept {
    int sites=0;
    for(const auto& e:kEntries) {
        if(!Matches(e.target,e.sig,e.sigSize)){Log("HOOK glyph lock=0 (EDF+%#x does not match)",e.target);return false;}
        for(std::size_t i=0;i<e.count;++i)
            if(!CallsTarget(e.sites[i],e.target)){Log("HOOK glyph lock=0 (EDF+%#x is no call to EDF+%#x)",e.sites[i],e.target);return false;}
        sites+=static_cast<int>(e.count);
    }
    InitializeCriticalSectionAndSpinCount(&lock,4000);
    int done=0;
    for(const auto& e:kEntries)
        for(std::size_t i=0;i<e.count;++i) {
            bool changed=false;
            if(RedirectCall(image+e.sites[i],image+e.target,e.hook,changed))++done;
            else Log("GLYPH call site %#x %s",e.sites[i],changed ? "half patched" : "not patched");
        }
    Log("HOOK glyph lock=%d (%d/%d calls into the glyph cache under one lock)",done==sites,done,sites);
    return done==sites;
}
}  // namespace crew
