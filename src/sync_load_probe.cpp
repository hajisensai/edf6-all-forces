// The synchronous loads on the game thread (sync_load_probe.h; docs/sync-load-re.md).
#include "sync_load_probe.h"

#include <Windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>

#include "crew.h"
#include "memory.h"
#include "perf_probe.h"

namespace crew::syncload {
namespace {
// The call sites inside the resource cache's get-or-load 0x84E80 and the stock function each calls (EDF.dll, checked by
// disassembly 2026-11-24; tests/sync_load_probe_native_audit.py proves them and kSigs on the supported EDF.dll).
constexpr unsigned kLookupCall=0x84ECF,kLookup=0x9C950;   // the first lookup (r8 = the key); a hit on a pending entry waits in it
constexpr unsigned kMissCall=0x84FDA,kMiss=0x89360;       // the miss: the pending entry's insert, just before the read
constexpr unsigned kFailCall=0x850CD,kFail=0xBAF80;       // the read failed (no such file): the entry's removal
constexpr unsigned kExitCall=0x8515D,kExit=0x50BE0;       // the last unlock, every path (hit, read, not found) ends here
// The bytes around them: the frame (one rsp for every call after the prologue, so the hooks pair by it), the key in r8 from
// r15, the hit test, the read (the reader factory [0x2005540], Open at +8, the blocking Wait(0) at +0x10) and the branches
// that all reach the exit.
struct Sig { unsigned rva; const char* hex; };
constexpr Sig kSigs[]={
    {0x84E80,"48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC A0 00 00 00"},
    {0x84EAA,"4D 8B F8"},                          // r15 = the key
    {0x84EC5,"4D 8B C7 48 8D 55 CF 49 8B CD E8"},  // the lookup (rcx cache, rdx a local handle, r8 key)
    {0x84ED5,"4C 8B 75 D7 4D 85 F6 74 55"},        // a hit, or je to the miss at 0x84F33
    {0x84F2E,"E9 26 02 00 00"},                    // the hit: jmp to the exit 0x85159
    {0x84FD0,"4D 8B C7 48 8D 55 BF 49 8B CD E8"},  // the miss's insert
    {0x85009,"48 8B 4F 68 48 85 C9 75 0D FF 15"},  // the entry's reader, made by the factory
    {0x8501F,"48 8B 01 4C 8D 45 EF 48 8D 57 48 FF 50 08"},   // Open(buffer, path): queues the read
    {0x8504D,"48 8B 01 33 D2 FF 50 10 85 C0 41 0F 94 C6"},   // Wait(0): blocks until it is read
    {0x850C7,"49 8B D7 49 8B CD E8"},              // not found: the entry's removal (rdx key)
    {0x8510F,"EB 48"},                             // then the exit
    {0x85159,"48 8D 4D A7 E8"},                    // the exit's unlock
    {0x85162,"48 8B C6"},
};

constexpr double kWaitMinMs=0.5;
constexpr ULONGLONG kStatsEveryMs=10000;
constexpr unsigned kDepth=16;      // nested 0x84E80 calls on the game thread
constexpr unsigned kRecords=64;    // the lines one outermost call leaves (flushed when it returns)
constexpr unsigned kPath=160;      // a path's characters kept

using LookupFn=void*(__fastcall*)(void*,void*,const void*);
using FailFn=void*(__fastcall*)(void*,const void*);
using ExitFn=void*(__fastcall*)(void*);
LookupFn nextLookup=nullptr,nextMiss=nullptr;
FailFn nextFail=nullptr;
ExitFn nextExit=nullptr;
std::atomic<bool> active{false};   // every site chained: else the hooks only call through

double tickMs=0.0;
LONGLONG Qpc() noexcept { LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart; }

// Game thread only from here.
struct Call { const void* frame; const void* key; LONGLONG begin,wait; bool miss,missing; };
Call calls[kDepth];
unsigned depth=0;   // calls past kDepth are not timed
enum class Kind : unsigned char { load,missing,wait };
struct Record { Kind kind; double ms; wchar_t path[kPath]; };
Record records[kRecords];
unsigned recordCount=0,recordsDropped=0;
struct Stats { unsigned long long calls,loads,waits,missing,dropped; double loadMs,waitMs,worstMs; LONGLONG self; } stats{};
ULONGLONG statsAt=0;

bool OnGameThread() noexcept { const DWORD t=perfprobe::GameThread();return t && t==GetCurrentThreadId(); }

// The key: an MSVC std::wstring (8 characters inline, else a pointer; size +0x10, capacity +0x18), its hash at +0x20.
void CopyPath(const void* key,wchar_t* out) noexcept {
    out[0]=L'\0';
    __try {
        const auto* k=static_cast<const unsigned char*>(key);
        std::size_t size=0,cap=0;std::memcpy(&size,k+0x10,sizeof(size));std::memcpy(&cap,k+0x18,sizeof(cap));
        const wchar_t* s=reinterpret_cast<const wchar_t*>(k);
        if(cap>7){const wchar_t* p=nullptr;std::memcpy(&p,k,sizeof(p));s=p;}
        if(size>kPath-1)size=kPath-1;
        std::wmemcpy(out,s,size);
        out[size]=L'\0';
    } __except(EXCEPTION_EXECUTE_HANDLER){out[0]=L'?';out[1]=L'\0';}
}

void Keep(Kind kind,const Call& c,double ms) noexcept {
    if(recordCount>=kRecords){++recordsDropped;++stats.dropped;return;}
    Record& r=records[recordCount++];
    r.kind=kind;r.ms=ms;
    CopyPath(c.key,r.path);
}

void Flush() noexcept {
    const bool debug=Cfg().debug;
    for(unsigned i=0;i<recordCount && debug;++i) {
        const Record& r=records[i];
        if(r.kind==Kind::wait)Log("SYNC WAIT %ls %.2f ms",r.path,r.ms);
        else Log("SYNC LOAD %ls %.2f ms%s",r.path,r.ms,r.kind==Kind::missing ? " not found" : "");
    }
    if(recordsDropped && debug)Log("SYNC LOAD %u more not logged (at most %u lines a call)",recordsDropped,kRecords);
    recordCount=recordsDropped=0;
    const ULONGLONG now=GetTickCount64();
    if(debug && now-statsAt>=kStatsEveryMs && stats.calls) {
        statsAt=now;
        Log("SYNC LOAD STATS since load, game thread: %llu cache calls, %llu loads (%.1f ms, worst %.1f, %llu not found), "
            "%llu waits (%.1f ms), %llu lines dropped; the hook's own time %.2f us a call",stats.calls,stats.loads,
            stats.loadMs,stats.worstMs,stats.missing,stats.waits,stats.waitMs,stats.dropped,
            static_cast<double>(stats.self)*tickMs*1000.0/static_cast<double>(stats.calls));
    }
}

Call* Mine(const void* frame) noexcept { return depth && calls[depth-1].frame==frame ? &calls[depth-1] : nullptr; }

void* __fastcall LookupHook(void* cache,void* out,const void* key) {
    if(!active.load(std::memory_order_relaxed) || !OnGameThread())return nextLookup(cache,out,key);
    const LONGLONG in=Qpc();
    if(depth>=kDepth)return nextLookup(cache,out,key);
    Call& c=calls[depth++];
    c=Call{_AddressOfReturnAddress(),key,0,0,false,false};
    c.begin=Qpc();
    void* const r=nextLookup(cache,out,key);
    const LONGLONG back=Qpc();
    c.wait=back-c.begin;
    stats.self+=(c.begin-in)+(Qpc()-back);
    return r;
}
void* __fastcall MissHook(void* cache,void* out,const void* key) {
    if(active.load(std::memory_order_relaxed) && OnGameThread())if(Call* c=Mine(_AddressOfReturnAddress()))c->miss=true;
    return nextMiss(cache,out,key);
}
void* __fastcall FailHook(void* cache,const void* key) {
    if(active.load(std::memory_order_relaxed) && OnGameThread())if(Call* c=Mine(_AddressOfReturnAddress()))c->missing=true;
    return nextFail(cache,key);
}
void* __fastcall ExitHook(void* guard) {
    void* const r=nextExit(guard);
    if(!active.load(std::memory_order_relaxed) || !OnGameThread())return r;
    const LONGLONG end=Qpc();
    const void* const frame=_AddressOfReturnAddress();
    // drop the calls an exception unwound: their frames are below this one (the stack grows down). A call not recorded
    // (past kDepth, or begun before the probe was active) is below every recorded one and finds nothing.
    while(depth && calls[depth-1].frame<frame)--depth;
    if(Call* c=Mine(frame)) {
        --depth;
        ++stats.calls;
        const double ms=static_cast<double>(end-c->begin)*tickMs;
        if(c->miss) {
            ++stats.loads;stats.loadMs+=ms;if(ms>stats.worstMs)stats.worstMs=ms;stats.missing+=c->missing;
            Keep(c->missing ? Kind::missing : Kind::load,*c,ms);
        } else {
            const double wait=static_cast<double>(c->wait)*tickMs;
            if(wait>=kWaitMinMs){++stats.waits;stats.waitMs+=wait;Keep(Kind::wait,*c,wait);}
        }
        stats.self+=Qpc()-end;
        if(!depth)Flush();
    }
    return r;
}

bool Matches(const Sig& s) noexcept {
    const unsigned char* p=image+s.rva;
    for(const char* h=s.hex;*h;) {
        if(*h==' '){++h;continue;}
        auto nib=[](char ch)->int{ return ch>='0'&&ch<='9' ? ch-'0' : ch>='A'&&ch<='F' ? ch-'A'+10 : -1; };
        const int hi=nib(h[0]),lo=h[1] ? nib(h[1]) : -1;
        if(hi<0 || lo<0 || !Readable(p,1) || *p!=static_cast<unsigned char>(hi*16+lo))return false;
        ++p;h+=2;
    }
    return true;
}
}  // namespace

void Install() noexcept {
    LARGE_INTEGER hz;QueryPerformanceFrequency(&hz);
    tickMs=1000.0/static_cast<double>(hz.QuadPart);
    const Config& c=Cfg();
    if(!c.debug || !c.syncLoadProbe)return;
    for(const Sig& s:kSigs) {
        if(!Matches(s)){Log("HOOK sync load probe NOT installed: the bytes at 0x%X differ (another EDF.dll build?)",s.rva);return;}
    }
    // every site chained or the hooks stay pass-through (a site chained before one failed only calls through)
    const bool ok=perfprobe::ChainCall(kLookupCall,kLookup,reinterpret_cast<void*>(&LookupHook),reinterpret_cast<void**>(&nextLookup)) &&
        perfprobe::ChainCall(kMissCall,kMiss,reinterpret_cast<void*>(&MissHook),reinterpret_cast<void**>(&nextMiss)) &&
        perfprobe::ChainCall(kFailCall,kFail,reinterpret_cast<void*>(&FailHook),reinterpret_cast<void**>(&nextFail)) &&
        perfprobe::ChainCall(kExitCall,kExit,reinterpret_cast<void*>(&ExitHook),reinterpret_cast<void**>(&nextExit));
    active.store(ok,std::memory_order_relaxed);
    Log("HOOK sync load probe=%d (0x84E80 on the game thread: SYNC LOAD / SYNC WAIT lines)",ok);
}
}  // namespace crew::syncload
