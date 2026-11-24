// The heap probe (heap_probe.h): live allocations born after a starting point, by call path. Test-only (EDF6Autopilot).
#include "heap_probe.h"

#include <psapi.h>
#include <intrin.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace autopilot {
namespace {
LogFn logFn=nullptr;

using HeapAllocFn=LPVOID(WINAPI*)(HANDLE,DWORD,SIZE_T);
using HeapReAllocFn=LPVOID(WINAPI*)(HANDLE,DWORD,LPVOID,SIZE_T);
using HeapFreeFn=BOOL(WINAPI*)(HANDLE,DWORD,LPVOID);
using VirtualAllocFn=LPVOID(WINAPI*)(LPVOID,SIZE_T,DWORD,DWORD);
using VirtualFreeFn=BOOL(WINAPI*)(LPVOID,SIZE_T,DWORD);
HeapAllocFn realHeapAlloc=nullptr;
HeapReAllocFn realHeapReAlloc=nullptr;
HeapFreeFn realHeapFree=nullptr;
VirtualAllocFn realVirtualAlloc=nullptr;
VirtualFreeFn realVirtualFree=nullptr;

volatile LONG tracking=0,marks=0,startAt=2;
volatile LONG64 dropped=0;

// The call path: the first frames outside the allocators' own modules (system DLLs, the shared CRT), so a site is
// the code that asked, not malloc.
constexpr int kDepth=6;
struct Range { std::uintptr_t from,to; };
Range skip[8]{};
int skipCount=0;

bool Skipped(std::uintptr_t at) noexcept {
    for(int i=0;i<skipCount;++i)if(at>=skip[i].from && at<skip[i].to)return true;
    return false;
}

// Sites: open addressing, insert-only, lock-free (state 0 empty, 1 being written, 2 ready).
constexpr std::uint32_t kSites=1u<<16;
struct Site {
    volatile LONG state;
    std::uint32_t hash;
    std::uintptr_t frames[kDepth];
    volatile LONG64 bytes[2],count[2];   // [0] heap, [1] VirtualAlloc
};
Site* sites=nullptr;
volatile LONG64 sitesFull=0;

std::uint32_t HashFrames(const std::uintptr_t* f) noexcept {
    std::uint64_t h=1469598103934665603ull;
    for(int i=0;i<kDepth;++i){h^=f[i];h*=1099511628211ull;}
    return static_cast<std::uint32_t>(h^(h>>32));
}

std::uint32_t SiteOf(const std::uintptr_t* f) noexcept {
    const std::uint32_t h=HashFrames(f);
    for(std::uint32_t n=0,i=h&(kSites-1);n<kSites;++n,i=(i+1)&(kSites-1)) {
        Site& s=sites[i];
        for(;;) {
            const LONG state=s.state;
            if(state==2) {
                if(s.hash==h && !std::memcmp(s.frames,f,sizeof(s.frames)))return i;
                break;
            }
            if(state==0) {
                if(InterlockedCompareExchange(&s.state,1,0)!=0)continue;
                std::memcpy(s.frames,f,sizeof(s.frames));
                s.hash=h;
                InterlockedExchange(&s.state,2);
                return i;
            }
            YieldProcessor();   // another thread writing this slot: read it again
        }
    }
    InterlockedIncrement64(&sitesFull);
    return 0;
}

// Live allocations: 256 shards of linear probing (backward-shift deletion: no tombstones under the game's churn).
constexpr std::uint32_t kShards=256,kShardCap=1u<<14;
struct Entry { std::uintptr_t p; std::uint64_t size; std::uint32_t site,kind; };
struct Shard { SRWLOCK lock; std::uint32_t count; Entry e[kShardCap]; };
Shard* shards=nullptr;

std::uint64_t Mix(std::uintptr_t p) noexcept {
    std::uint64_t x=p;x^=x>>33;x*=0xff51afd7ed558ccdull;x^=x>>33;x*=0xc4ceb9fe1a85ec53ull;x^=x>>33;
    return x;
}

void Account(std::uint32_t site,std::uint32_t kind,std::int64_t bytes,std::int64_t count) noexcept {
    InterlockedAdd64(&sites[site].bytes[kind],bytes);
    InterlockedAdd64(&sites[site].count[kind],count);
}

void Track(void* p,SIZE_T size,std::uint32_t kind) noexcept {
    std::uintptr_t frames[24]{},path[kDepth]{};
    const USHORT got=RtlCaptureStackBackTrace(2,24,reinterpret_cast<PVOID*>(frames),nullptr);
    int n=0;
    for(USHORT i=0;i<got && n<kDepth;++i)if(n || !Skipped(frames[i]))path[n++]=frames[i];
    const std::uint32_t site=SiteOf(path);
    const std::uint64_t h=Mix(reinterpret_cast<std::uintptr_t>(p));
    Shard& s=shards[h>>56];
    AcquireSRWLockExclusive(&s.lock);
    std::uint32_t i=static_cast<std::uint32_t>(h)&(kShardCap-1);
    for(;;i=(i+1)&(kShardCap-1)) {
        Entry& e=s.e[i];
        if(e.p==reinterpret_cast<std::uintptr_t>(p)) {   // (a commit again inside a tracked VirtualAlloc block)
            Account(e.site,e.kind,-static_cast<std::int64_t>(e.size),-1);
            e.size=size;e.site=site;e.kind=kind;
            break;
        }
        if(!e.p) {
            if(s.count>=kShardCap-kShardCap/8){InterlockedIncrement64(&dropped);ReleaseSRWLockExclusive(&s.lock);return;}
            e={reinterpret_cast<std::uintptr_t>(p),size,site,kind};
            ++s.count;
            break;
        }
    }
    Account(site,kind,static_cast<std::int64_t>(size),1);
    ReleaseSRWLockExclusive(&s.lock);
}

void Untrack(void* p) noexcept {
    const std::uint64_t h=Mix(reinterpret_cast<std::uintptr_t>(p));
    Shard& s=shards[h>>56];
    AcquireSRWLockExclusive(&s.lock);
    std::uint32_t i=static_cast<std::uint32_t>(h)&(kShardCap-1);
    for(;;i=(i+1)&(kShardCap-1)) {
        Entry& e=s.e[i];
        if(!e.p){ReleaseSRWLockExclusive(&s.lock);return;}
        if(e.p==reinterpret_cast<std::uintptr_t>(p))break;
    }
    Account(s.e[i].site,s.e[i].kind,-static_cast<std::int64_t>(s.e[i].size),-1);
    // Backward shift: pull later members of the run into the hole when their home is at or before it.
    std::uint32_t hole=i;
    for(std::uint32_t j=(i+1)&(kShardCap-1);s.e[j].p;j=(j+1)&(kShardCap-1)) {
        const std::uint32_t home=static_cast<std::uint32_t>(Mix(s.e[j].p))&(kShardCap-1);
        const bool movable=hole<=j ? (home<=hole || home>j) : (home<=hole && home>j);
        if(movable){s.e[hole]=s.e[j];hole=j;}
    }
    s.e[hole]=Entry{};
    --s.count;
    ReleaseSRWLockExclusive(&s.lock);
}

LPVOID WINAPI HookHeapAlloc(HANDLE heap,DWORD flags,SIZE_T size) {
    void* const p=realHeapAlloc(heap,flags,size);
    if(p && tracking)Track(p,size,0);
    return p;
}
LPVOID WINAPI HookHeapReAlloc(HANDLE heap,DWORD flags,LPVOID was,SIZE_T size) {
    if(!tracking)return realHeapReAlloc(heap,flags,was,size);
    void* const p=realHeapReAlloc(heap,flags,was,size);
    if(p){if(was)Untrack(was);Track(p,size,0);}
    return p;
}
BOOL WINAPI HookHeapFree(HANDLE heap,DWORD flags,LPVOID p) {
    if(p && tracking)Untrack(p);   // before the free: the address may be handed out again at once
    return realHeapFree(heap,flags,p);
}
LPVOID WINAPI HookVirtualAlloc(LPVOID at,SIZE_T size,DWORD type,DWORD protect) {
    void* const p=realVirtualAlloc(at,size,type,protect);
    if(p && tracking && (type&MEM_COMMIT))Track(p,size,1);
    return p;
}
BOOL WINAPI HookVirtualFree(LPVOID at,SIZE_T size,DWORD type) {
    if(at && tracking)Untrack(at);
    return realVirtualFree(at,size,type);
}

struct Swap { const char* name; void* hook; };
const Swap kSwaps[]={{"HeapAlloc",reinterpret_cast<void*>(&HookHeapAlloc)},
                     {"HeapReAlloc",reinterpret_cast<void*>(&HookHeapReAlloc)},
                     {"HeapFree",reinterpret_cast<void*>(&HookHeapFree)},
                     {"VirtualAlloc",reinterpret_cast<void*>(&HookVirtualAlloc)},
                     {"VirtualFree",reinterpret_cast<void*>(&HookVirtualFree)}};

int PatchModule(HMODULE module) noexcept {
    const auto base=reinterpret_cast<unsigned char*>(module);
    const auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return 0;
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress)return 0;
    int patched=0;
    for(auto d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress);d->Name;++d) {
        if(!d->OriginalFirstThunk)continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+d->OriginalFirstThunk);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+d->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
            const char* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData)->Name;
            for(const Swap& s:kSwaps) {
                if(std::strcmp(name,s.name))continue;
                DWORD old=0;
                if(!VirtualProtect(&slots->u1.Function,sizeof(void*),PAGE_READWRITE,&old))break;
                InterlockedExchangePointer(reinterpret_cast<PVOID*>(&slots->u1.Function),s.hook);
                VirtualProtect(&slots->u1.Function,sizeof(void*),old,&old);
                ++patched;
                break;
            }
        }
    }
    return patched;
}

void ModuleOf(std::uintptr_t at,char* out,std::size_t size,std::uintptr_t* base) noexcept {
    HMODULE m=nullptr;
    *base=0;
    if(!at || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(at),&m)){strcpy_s(out,size,"?");return;}
    char path[MAX_PATH]{};
    GetModuleFileNameA(m,path,MAX_PATH);
    const char* slash=std::strrchr(path,'\\');
    strcpy_s(out,size,slash ? slash+1 : path);
    *base=reinterpret_cast<std::uintptr_t>(m);
}

void Dump(const char* why) noexcept {
    constexpr double kMb=1024.0*1024.0;
    static std::uint32_t order[kSites];
    std::uint32_t used=0;
    std::int64_t total[2]{},count[2]{};
    struct Mod { std::uintptr_t base; char name[48]; std::int64_t bytes,count; };
    static Mod mods[64];
    int modCount=0;
    for(std::uint32_t i=0;i<kSites;++i) {
        const Site& s=sites[i];
        if(s.state!=2)continue;
        const std::int64_t b=s.bytes[0]+s.bytes[1],c=s.count[0]+s.count[1];
        for(int k=0;k<2;++k){total[k]+=s.bytes[k];count[k]+=s.count[k];}
        if(!c)continue;
        order[used++]=i;
        char name[48];std::uintptr_t base=0;
        ModuleOf(s.frames[0],name,sizeof(name),&base);
        int m=0;
        while(m<modCount && mods[m].base!=base)++m;
        if(m==modCount && modCount<64){mods[m]=Mod{base,{},0,0};strcpy_s(mods[m].name,name);++modCount;}
        if(m<modCount){mods[m].bytes+=b;mods[m].count+=c;}
    }
    logFn("HEAPLIVE %s (mark %ld, since mark %ld): heap %.2f MB in %lld blocks, VirtualAlloc %.2f MB in %lld; %u sites, "
          "%lld dropped, %lld without a site slot",why,marks,startAt,total[0]/kMb,count[0],total[1]/kMb,count[1],used,
          dropped,sitesFull);
    std::sort(mods,mods+modCount,[](const Mod& a,const Mod& b){return a.bytes>b.bytes;});
    for(int m=0;m<modCount && m<12;++m)
        logFn("HEAPMOD %s %s %.3f MB in %lld",why,mods[m].name,mods[m].bytes/kMb,mods[m].count);
    const auto bytesOf=[](std::uint32_t i){return sites[i].bytes[0]+sites[i].bytes[1];};
    const std::uint32_t top=std::min<std::uint32_t>(used,30);
    std::partial_sort(order,order+top,order+used,[&](std::uint32_t a,std::uint32_t b){return bytesOf(a)>bytesOf(b);});
    for(std::uint32_t r=0;r<top;++r) {
        const Site& s=sites[order[r]];
        char path[400]{};std::size_t at=0;
        for(int f=0;f<kDepth && s.frames[f];++f) {
            char name[48];std::uintptr_t base=0;
            ModuleOf(s.frames[f],name,sizeof(name),&base);
            const int w=base ? std::snprintf(path+at,sizeof(path)-at,"%s%s+%#llx",f ? " < " : "",name,
                                             static_cast<unsigned long long>(s.frames[f]-base))
                             : std::snprintf(path+at,sizeof(path)-at,"%s%#llx",f ? " < " : "",
                                             static_cast<unsigned long long>(s.frames[f]));
            if(w<0 || static_cast<std::size_t>(w)>=sizeof(path)-at)break;
            at+=static_cast<std::size_t>(w);
        }
        logFn("HEAPSITE %s %.3f MB in %lld (heap %lld, VA %lld): %s",why,bytesOf(order[r])/kMb,s.count[0]+s.count[1],
              s.count[0],s.count[1],path);
    }
}
}  // namespace

bool InstallHeapProbe(LogFn log,int startMark) noexcept {
    logFn=log;
    if(shards)return true;
    const HMODULE k32=GetModuleHandleW(L"kernel32.dll");
    realHeapAlloc=reinterpret_cast<HeapAllocFn>(GetProcAddress(k32,"HeapAlloc"));
    realHeapReAlloc=reinterpret_cast<HeapReAllocFn>(GetProcAddress(k32,"HeapReAlloc"));
    realHeapFree=reinterpret_cast<HeapFreeFn>(GetProcAddress(k32,"HeapFree"));
    realVirtualAlloc=reinterpret_cast<VirtualAllocFn>(GetProcAddress(k32,"VirtualAlloc"));
    realVirtualFree=reinterpret_cast<VirtualFreeFn>(GetProcAddress(k32,"VirtualFree"));
    if(!realHeapAlloc || !realHeapReAlloc || !realHeapFree || !realVirtualAlloc || !realVirtualFree)return false;
    shards=static_cast<Shard*>(VirtualAlloc(nullptr,sizeof(Shard)*kShards,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    sites=static_cast<Site*>(VirtualAlloc(nullptr,sizeof(Site)*kSites,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!shards || !sites)return false;
    for(std::uint32_t i=0;i<kShards;++i)InitializeSRWLock(&shards[i].lock);
    sites[0].state=2;   // site 0: "no slot left" (its frames all zero)
    for(const wchar_t* name:{L"ntdll.dll",L"kernel32.dll",L"KernelBase.dll",L"ucrtbase.dll",L"msvcp140.dll",
                             L"vcruntime140.dll",L"vcruntime140_1.dll"}) {
        MODULEINFO info{};
        const HMODULE m=GetModuleHandleW(name);
        if(m && skipCount<8 && GetModuleInformation(GetCurrentProcess(),m,&info,sizeof(info)))
            skip[skipCount++]={reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll),
                               reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll)+info.SizeOfImage};
    }
    HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&InstallHeapProbe),&self);
    HMODULE modules[512]{};DWORD bytes=0;
    if(!EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes))return false;
    const HMODULE own[]={GetModuleHandleW(L"ntdll.dll"),k32,GetModuleHandleW(L"KernelBase.dll"),self};
    int patched=0,touched=0;
    for(DWORD i=0;i<bytes/sizeof(HMODULE) && i<512;++i) {
        if(std::find(std::begin(own),std::end(own),modules[i])!=std::end(own))continue;
        const int n=PatchModule(modules[i]);
        patched+=n;touched+=n ? 1 : 0;
    }
    startAt=startMark<1 ? 1 : startMark;
    log("HEAPPROBE installed: %d import entries in %d modules, counting from cycle mark %ld (%zu MB of tables)",patched,
        touched,startAt,(sizeof(Shard)*kShards+sizeof(Site)*kSites)>>20);
    return patched>0;
}

void HeapProbeMark(const char* why) noexcept {
    if(!shards)return;
    const LONG n=InterlockedIncrement(&marks);
    if(n==startAt){InterlockedExchange(&tracking,1);logFn("HEAPLIVE %s (mark %ld): counting starts",why,n);return;}
    if(n>startAt)Dump(why);
}
}  // namespace autopilot
