// Memory primitives shared by EDF6VehicleCrew and EDF6AutoTurret: one copy (the two plugins had drifted
// apart: only one had the region cache and the E9 tail-jump redirect).
#include "edf/memory.h"
#include <psapi.h>
#include <cstring>
namespace edf {
// The plugin asks this many times per object per frame (WastedFire / PickTarget per gunner per enemy, the jets' and
// crews' frames): hundreds to thousands of asks a frame in a battle.
//
// VirtualQuery was the answer and the cost: it describes the run of like pages from the asked page to the end of the
// region, so the kernel walks the page tables over all of it, and a game heap region is hundreds of MB. With ~100
// ants and the NPC tanks fighting them it was 35% of the game thread, with the grand battle 47% (~7 ms a frame;
// tools/perfrun.py, 2026-11-07). Its answer was also cached badly: the run starts at the asked page, so a pointer
// lower in the same region never hit, and asked again from there to the end.
//
// So the pages themselves are asked: QueryWorkingSetEx tells each page's protection, in time by the number of pages
// asked, not by the region. It knows only pages in the working set (Valid); any other page (paged out, reserved,
// free, a guard page, whose PTE is never valid) is asked with VirtualQuery as before. The answers are kept per thread
// by page for the tick (each frame or so), as the regions were, so a page freed and remapped is asked again.
// Why the tick and not the game frame (weighed 2026-11-08, stress:100 at 131 ms a frame re-asking pages ~8 times a
// frame): the tick (GetTickCount64, ~16 ms) is the longest a page's answer is trusted, and a window no longer than it
// is the condition (a page the game decommits meanwhile is read on our word). A frame window bounded by it ("the
// shorter of the two": a new epoch when the frame or the tick moves) answers from the cache exactly as the tick alone
// at stress (a frame there spans ~8 ticks, the tick moves first every time) and less at 60 fps (frame ~= tick: their
// two edges both end a window); only a window longer than the tick saves asks, and that is the trust this code does
// not give. Besides, memory.cpp is both plugins' and any thread's (AutoTurret, Coop callbacks, the plugins' own
// threads): a frame epoch would be the game thread's notion pushed into a shared primitive. So the asks are cut where
// they are made instead: PrefetchReadable for a batch, and no ask for an object known live (seat.h IsAnyPlayerSelf).
// Not a probing read: touching a PAGE_GUARD page (a thread's stack guard) would take the guard away.
namespace {
constexpr std::uintptr_t kPage=0x1000;
// What a page allows, as Readable checks it: 0 nothing (or not known), else these bits.
constexpr unsigned char kRead=1,kWrite=2;
unsigned char Allows(DWORD protect) noexcept {
    if(protect&(PAGE_NOACCESS|PAGE_GUARD)) return 0;
    switch(protect&0xff) {
    case PAGE_READWRITE: case PAGE_WRITECOPY: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
        return kRead|kWrite;
    case PAGE_READONLY: case PAGE_EXECUTE_READ:
        return kRead;
    default:
        return 0;
    }
}

// Per thread, direct-mapped by page number; an entry counts only in the tick it was written. An entry is the page's
// address with what it allows in the low bits (a page address has 12 zero bits), and its tick: 16 bytes, 128 KB. A
// battle's game thread looks at ~5,000 pages a frame (300 ants and their gunners): 1024 entries were too few, each
// frame pushed out what it would ask again (perfrun blast, 2026-11-07: 4,800 working-set calls a frame).
struct PageCache {
    static constexpr int kSize=8192;
    struct Entry { std::uintptr_t pageAllows; ULONGLONG tick; };
    Entry e[kSize];
    Entry& Slot(std::uintptr_t page) noexcept { return e[(page/kPage)%kSize]; }
};
// Made on a thread's first ask, freed when the thread ends (a game or Coop thread that comes and goes leaks nothing).
struct PageCacheOwner {
    PageCache* pages=nullptr;
    bool tried=false;
    ~PageCacheOwner() { if(pages) VirtualFree(pages,0,MEM_RELEASE); }
};
PageCache* Pages() noexcept {
    thread_local PageCacheOwner owner;
    if(!owner.pages && !owner.tried) {
        owner.tried=true;
        owner.pages=static_cast<PageCache*>(VirtualAlloc(nullptr,sizeof(PageCache),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    }
    return owner.pages;
}

thread_local ReadableStats stats{};
std::uint64_t Qpc() noexcept { LARGE_INTEGER t; QueryPerformanceCounter(&t); return static_cast<std::uint64_t>(t.QuadPart); }

// VirtualQuery's answer for the pages from `at`, the old way: the region cache, then the call.
struct Region { std::uintptr_t base,end; DWORD state,protect; };
bool Query(std::uintptr_t at,MEMORY_BASIC_INFORMATION& mbi,ULONGLONG now) noexcept {
    constexpr int kKept=32;
    thread_local Region kept[kKept];
    thread_local int count=0,next=0;
    thread_local ULONGLONG tick=0;
    if(now!=tick){tick=now;count=0;next=0;}
    for(int i=0;i<count;++i)
        if(at>=kept[i].base && at<kept[i].end) {
            mbi.BaseAddress=reinterpret_cast<void*>(kept[i].base);mbi.RegionSize=kept[i].end-kept[i].base;
            mbi.State=kept[i].state;mbi.Protect=kept[i].protect;
            return true;
        }
    const std::uint64_t t0=Qpc();
    const bool ok=VirtualQuery(reinterpret_cast<void*>(at),&mbi,sizeof(mbi))!=0;
    ++stats.vqCalls;stats.vqTicks+=Qpc()-t0;
    if(!ok)return false;
    const auto base=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    kept[next]={base,base+mbi.RegionSize,mbi.State,mbi.Protect};
    next=(next+1)%kKept;
    if(count<kKept)++count;
    return true;
}

bool ReadableByRegion(std::uintptr_t at,std::uintptr_t end,unsigned char need,ULONGLONG now) noexcept {
    while(at<end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if(!Query(at,mbi,now) || mbi.State!=MEM_COMMIT || (Allows(mbi.Protect)&need)!=need) return false;
        const auto next=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
        if(next<=at) return false;
        at=next;
    }
    return true;
}

// The pages asked at once (an object, a few KB at most); a longer span is asked by region.
constexpr std::uintptr_t kMaxPages=16;
// PrefetchReadable's pages a call (the stack array's size).
constexpr int kPrefetchPages=64;

// What a page asked of the working set allows, kept in `cache` for the tick `now`.
unsigned char Record(PageCache& cache,const PSAPI_WORKING_SET_EX_INFORMATION& ask,ULONGLONG now) noexcept {
    const auto page=reinterpret_cast<std::uintptr_t>(ask.VirtualAddress);
    unsigned char allows=0;
    if(ask.VirtualAttributes.Valid) {
        allows=Allows(static_cast<DWORD>(ask.VirtualAttributes.Win32Protection));
    } else {
        // Not in the working set (paged out, never touched, reserved, free, a guard page): VirtualQuery's answer,
        // kept like any other for the tick (Readable never touches the page, so it stays out and would be asked
        // again every time: a trimmed working set would be the old cost back).
        MEMORY_BASIC_INFORMATION mbi{};
        if(Query(page,mbi,now) && mbi.State==MEM_COMMIT) allows=Allows(mbi.Protect);
    }
    cache.Slot(page)={page|allows,now};
    return allows;
}
}  // namespace

bool Readable(const void* ptr,std::size_t size,bool writable) noexcept {
    const auto at=reinterpret_cast<std::uintptr_t>(ptr);
    if (!at || size>UINTPTR_MAX-at) return false;
    const auto end=at+size;
    if(end==at) return true;
    ++stats.asks;
    const unsigned char need=writable ? kRead|kWrite : kRead;
    const ULONGLONG now=GetTickCount64();
    const std::uintptr_t first=at&~(kPage-1),last=(end-1)&~(kPage-1);
    const std::uintptr_t pages=(last-first)/kPage+1;
    PageCache* cache=Pages();
    if(pages>kMaxPages || !cache) return ReadableByRegion(at,end,need,now);
    PSAPI_WORKING_SET_EX_INFORMATION ask[kMaxPages];
    int asked=0;
    for(std::uintptr_t page=first;;page+=kPage) {
        const auto& hit=cache->Slot(page);
        if((hit.pageAllows&~(kPage-1))==page && hit.tick==now) {
            ++stats.pageHits;
            if((hit.pageAllows&need)!=need) return false;
        } else {
            ask[asked]={};
            ask[asked].VirtualAddress=reinterpret_cast<void*>(page);
            ++asked;
        }
        if(page==last) break;
    }
    if(!asked) return true;
    const std::uint64_t t0=Qpc();
    const bool got=K32QueryWorkingSetEx(GetCurrentProcess(),ask,static_cast<DWORD>(asked*sizeof(ask[0])))!=0;
    ++stats.wsCalls;stats.wsPages+=static_cast<unsigned>(asked);stats.wsTicks+=Qpc()-t0;
    if(!got) return ReadableByRegion(at,end,need,now);
    for(int i=0;i<asked;++i)
        if((Record(*cache,ask[i],now)&need)!=need) return false;
    return true;
}
void PrefetchReadable(const void* const* ptrs,std::size_t size,int n) noexcept {
    PageCache* cache=Pages();
    if(!cache || !ptrs || n<=0 || !size) return;
    const ULONGLONG now=GetTickCount64();
    PSAPI_WORKING_SET_EX_INFORMATION ask[kPrefetchPages];
    int asked=0;
    const auto flush=[&]() noexcept {
        if(!asked) return;
        const std::uint64_t t0=Qpc();
        const bool got=K32QueryWorkingSetEx(GetCurrentProcess(),ask,static_cast<DWORD>(asked*sizeof(ask[0])))!=0;
        ++stats.wsCalls;stats.wsPages+=static_cast<unsigned>(asked);stats.wsTicks+=Qpc()-t0;
        if(got) for(int i=0;i<asked;++i) Record(*cache,ask[i],now);   // refused: nothing kept, each Readable asks as before
        asked=0;
    };
    for(int k=0;k<n;++k) {
        const auto at=reinterpret_cast<std::uintptr_t>(ptrs[k]);
        if(!at || size>UINTPTR_MAX-at) continue;
        const std::uintptr_t first=at&~(kPage-1),last=(at+size-1)&~(kPage-1);
        if((last-first)/kPage+1>kMaxPages) continue;   // Readable asks such a span by region
        for(std::uintptr_t page=first;;page+=kPage) {
            const auto& hit=cache->Slot(page);
            bool queued=(hit.pageAllows&~(kPage-1))==page && hit.tick==now;
            for(int i=0;i<asked && !queued;++i) queued=reinterpret_cast<std::uintptr_t>(ask[i].VirtualAddress)==page;
            if(!queued) {
                if(asked==kPrefetchPages) flush();
                ask[asked]={};
                ask[asked].VirtualAddress=reinterpret_cast<void*>(page);
                ++asked;
            }
            if(page==last) break;
        }
    }
    flush();
}
ReadableStats TakeReadableStats() noexcept { const ReadableStats s=stats; stats={}; return s; }
void* AllocateNearCode(const void* anchor,const unsigned char* code,std::size_t size) noexcept {
    if(!anchor || !code || !size || size>0x1000) return nullptr;
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto granularity=info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
    const auto base=reinterpret_cast<std::uintptr_t>(anchor) & ~static_cast<std::uintptr_t>(granularity-1);
    // A rel32 reaches +/-2GB; stay well inside that on both sides of the anchor.
    constexpr std::uintptr_t kReach=0x60000000;
    unsigned char* page=nullptr;
    for(std::uintptr_t step=granularity; step<kReach && !page; step+=granularity) {
        if(base>step)
            page=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(base-step),0x1000,
                MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!page)
            page=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(base+step),0x1000,
                MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    }
    if(!page) return nullptr;
    std::memcpy(page,code,size);
    DWORD previous=0;
    if(!VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&previous)) {
        VirtualFree(page,0,MEM_RELEASE);
        return nullptr;
    }
    FlushInstructionCache(GetCurrentProcess(),page,size);
    return page;
}
void* AllocateNearThunk(const void* anchor,void* target) noexcept {
    if(!target) return nullptr;
    unsigned char thunk[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(thunk+2,&address,sizeof(address));
    return AllocateNearCode(anchor,thunk,sizeof(thunk));
}

bool RedirectCall(unsigned char* callSite,void* expectedTarget,void* replacement,bool& changed) noexcept {
    changed=false;
    __try {
        if(!callSite || !expectedTarget || !replacement) return false;
        // A call (E8) or a tail jump (E9): the same rel32, the hook returns where the target would.
        if(!Readable(callSite,5) || (callSite[0]!=0xE8 && callSite[0]!=0xE9)) return false;
        std::int32_t relative=0;
        std::memcpy(&relative,callSite+1,sizeof(relative));
        if(callSite+5+relative!=static_cast<unsigned char*>(expectedTarget)) return false;
        auto thunk=AllocateNearThunk(callSite,replacement);
        if(!thunk) return false;
        const auto delta=static_cast<unsigned char*>(thunk)-(callSite+5);
        if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0) { VirtualFree(thunk,0,MEM_RELEASE); return false; }
        const auto next=static_cast<std::int32_t>(delta);
        DWORD previous=0;
        if(!VirtualProtect(callSite,5,PAGE_EXECUTE_READWRITE,&previous)) { VirtualFree(thunk,0,MEM_RELEASE); return false; }
        std::memcpy(callSite+1,&next,sizeof(next));
        changed=true;
        DWORD restored=0;
        const bool ok=VirtualProtect(callSite,5,previous,&restored)!=0;
        FlushInstructionCache(GetCurrentProcess(),callSite,5);
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}  // namespace edf
