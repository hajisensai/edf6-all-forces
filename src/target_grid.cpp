// The nearest-enemy pick through the team grids (target_grid.h): its hooks on the stock EDF.dll.
//  - 0x54C506, the pick's walk call (0x54C4B0 -> 0x5E0F20): Query, under the manager's lock, the index built on the
//    first pick of each step. Only inside an object's update (the slot-5 loops below, the game thread: the only path to
//    slot 14, docs/havok-mt-re.md) and with the pick's own functor; any other call is the stock walk.
//  - 0x5E0C52 / 0x5E1CD8, the only calls of the set's insert (0x5E2350) and erase (0x5E2A70), both under the lock
//    (0x5E0B70 / 0x5E1C60): a member in is kept apart this step, a member out is never read again. Anything else
//    that changes a set (the manager's copy or destruction: 0x5E2950 / 0x5E1AC0) changes its rows or sizes: rebuilt.
//  - 0x119940D / 0x1199BE3, the System's two slot-5 update calls (`call [rax+28h]; or byte [rdi+18h],8`): a cave
//    counts the depth and, after the update, keeps the object apart if it left its cell (an object's own update
//    moves it; GameObjectBase::Update 0x54BE40 is not the end of it: the derived updates move after).
// With debug, 1 pick in 64 also runs the stock walk on a copy of the functor and compares the pick and its score
// (TARGETGRID MISMATCH: logged and counted, nothing replaced), a summary every 5 s.
#include "target_grid.h"
#include "config.h"
#include "edf_layout.h"
#include "plugin_log.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <Windows.h>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kTeams=0x20B2978,kSystem=0x20B2958,kSystemScene=0x68,kSystemStep=0xCE0;
constexpr unsigned kPick=0x54C4B0,kPickEnd=0x54C516,kPickCall=0x54C506,kPickVtable=0x17C2948;
constexpr unsigned kScore=0x54A3B0,kScoreEnd=0x54A4A4;
constexpr unsigned kWalk=0x5E0F20,kWalkEnd=0x5E1090;
constexpr unsigned kAdd=0x5E0B70,kAddEnd=0x5E0C78,kInsertCall=0x5E0C52,kInsert=0x5E2350;
constexpr unsigned kRemove=0x5E1C60,kRemoveEnd=0x5E1CF2,kEraseCall=0x5E1CD8,kErase=0x5E2A70;
constexpr unsigned kUpdateCalls[]{0x119940D,0x1199BE3};
constexpr unsigned char kUpdateStock[]{0xFF,0x50,0x28,0x80,0x4F,0x18,0x08};   // call [rax+28h]; or byte [rdi+18h],8
struct Fingerprint { unsigned begin,end; std::uint64_t fnv; };
constexpr Fingerprint kFingerprints[]{
    {kPick,kPickEnd,0x1023f8ba55e2b94eull},{kScore,kScoreEnd,0xa0d9d4bc5df2accfull},{kWalk,kWalkEnd,0xf4245d2af5d26c84ull},
    {kAdd,kAddEnd,0xa2a9952a95157fd2ull},{kRemove,kRemoveEnd,0x1c44c065d87ff4e4ull},
};
using WalkFn=void(__fastcall*)(void*,int,void*);
using ForwardFn=std::uintptr_t(__fastcall*)(void*,void*,void*,void*);
using UpdateFn=std::uintptr_t(__fastcall*)(void*,void*);

target_grid::Index index;
thread_local int updateDepth=0;
unsigned long long shadowRuns=0,mismatches=0,stockWalks=0,lastMismatches=0;
ULONGLONG lastSummary=0;

bool NativeCodeMatches(unsigned begin,unsigned end,std::uint64_t expected) noexcept {
    const auto* const code=image+begin;
    const unsigned bytes=end-begin;
    if(!edf::Readable(code,bytes))return false;
    std::uint64_t fingerprint=14695981039346656037ull;
    for(unsigned i=0;i<bytes;++i)fingerprint=(fingerprint^code[i])*1099511628211ull;
    return fingerprint==expected;
}
CRITICAL_SECTION* LockOf(void* mgr) noexcept { return reinterpret_cast<CRITICAL_SECTION*>(static_cast<unsigned char*>(mgr)+target_grid::kMgrLock); }
// The step the index was built for: the System, its scene and step count (gameframe::Step's, read raw).
bool FrameKey(std::uint64_t& key) noexcept {
    const auto* const system=edf::At<const unsigned char*>(image,kSystem);
    if(!system)return false;
    const auto scene=reinterpret_cast<std::uintptr_t>(edf::At<const void*>(system,kSystemScene));
    key=(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(system)^(scene<<16))<<32)^edf::At<std::uint32_t>(system,kSystemStep);
    return true;
}

void Summary() noexcept {
    if(!Cfg().debug)return;
    const ULONGLONG now=GetTickCount64();
    if(now-lastSummary<5000)return;
    lastSummary=now;
    const auto& s=index.stats;
    Log("TARGETGRID %llu picks (%llu on the grid, %llu stock walks), %llu builds (%llu untracked), %llu kept apart; scored %llu+%llu of %llu; shadow %llu, mismatches %llu (+%llu)",
        s.queries,s.answered,stockWalks,s.builds,s.untracked,s.apartMoves,s.first,s.second,s.members,shadowRuns,mismatches,mismatches-lastMismatches);
    lastMismatches=mismatches;
}

void __fastcall PickWalk(void* mgr,int team,void* functor) {
    const auto walk=reinterpret_cast<WalkFn>(image+kWalk);
    std::uint64_t key=0;
    if(updateDepth<=0 || team<0 || !mgr || target_grid::Rd<const void*>(functor,0)!=image+kPickVtable || !FrameKey(key)) {
        ++stockWalks;walk(mgr,team,functor);return;
    }
    EnterCriticalSection(LockOf(mgr));
    if(!index.Fresh(mgr,key)) {
        if(index.Covers(mgr,key))++index.stats.untracked;
        ++index.stats.builds;
        index.Build(mgr,key);
    }
    const bool shadow=Cfg().debug && (index.stats.queries&63)==0;
    alignas(16) unsigned char copy[target_grid::kFnBytes];
    if(shadow)std::memcpy(copy,functor,sizeof(copy));
    if(!index.Query(mgr,team,functor)) {
        LeaveCriticalSection(LockOf(mgr));
        ++stockWalks;walk(mgr,team,functor);return;
    }
    if(shadow) {
        ++shadowRuns;
        walk(mgr,team,copy);   // the manager's lock again (a critical section: the same thread may)
        const auto want=target_grid::Rd<const void*>(copy,target_grid::kFnResult),got=target_grid::Rd<const void*>(functor,target_grid::kFnResult);
        const auto wantBest=target_grid::Rd<std::uint32_t>(copy,target_grid::kFnBest),gotBest=target_grid::Rd<std::uint32_t>(functor,target_grid::kFnBest);
        if(want!=got || wantBest!=gotBest) {
            if(++mismatches<=32)
                Log("TARGETGRID MISMATCH team %d self %p: stock %p %08X, grid %p %08X",team,target_grid::Rd<const void*>(functor,target_grid::kFnSelf),
                    want,wantBest,got,gotBest);
        }
    }
    LeaveCriticalSection(LockOf(mgr));
    Summary();
}

std::uintptr_t __fastcall InsertHook(void* row,void* hint,void* node,void* r9) {
    const auto r=reinterpret_cast<ForwardFn>(image+kInsert)(row,hint,node,r9);
    index.Inserted(row,target_grid::Rd<void*>(node,target_grid::kNodeValue));
    return r;
}
std::uintptr_t __fastcall EraseHook(void* row,void* key,void* r8,void* r9) {
    void* const o=target_grid::Rd<void*>(key,0);
    const auto r=reinterpret_cast<ForwardFn>(image+kErase)(row,key,r8,r9);
    index.Erased(row,o);
    return r;
}
// The cave's call: (object, the update's argument, the update). Not noexcept: an exception out of an update goes on.
std::uintptr_t __fastcall UpdateObject(void* o,void* arg,UpdateFn update) {
    struct Depth { Depth(){++updateDepth;} ~Depth(){--updateDepth;} };
    std::uintptr_t r;
    { const Depth depth;r=update(o,arg); }
    if(index.Built())
        if(void* const mgr=edf::At<void*>(image,kTeams)) {
            EnterCriticalSection(LockOf(mgr));
            index.Updated(o);
            LeaveCriticalSection(LockOf(mgr));
        }
    return r;
}

// `sub rsp,28h; mov r8,[rax+28h]; call [rip+10] (UpdateObject); add rsp,28h; or byte [rdi+18h],8; ret`, the absolute
// address at 24, its unwind info (UWOP_ALLOC_SMALL 0x28 after the 4-byte prolog) at 32 and its function table entry
// at 40, registered so a stack walk (an exception out of an update, a crash dump) goes on to the System's loop.
unsigned char* MakeUpdateCave() noexcept {
    unsigned char page[64]={};
    const unsigned char code[]={0x48,0x83,0xEC,0x28,0x4C,0x8B,0x40,0x28,0xFF,0x15,0x0A,0,0,0,0x48,0x83,0xC4,0x28,
                                0x80,0x4F,0x18,0x08,0xC3};
    static_assert(sizeof(code)<=24,"the cave's code fits before its target");
    std::memcpy(page,code,sizeof(code));
    const auto target=reinterpret_cast<std::uintptr_t>(&UpdateObject);
    std::memcpy(page+24,&target,sizeof(target));
    const unsigned char unwind[]={0x01,0x04,0x01,0x00,0x04,0x42,0x00,0x00};
    std::memcpy(page+32,unwind,sizeof(unwind));
    const RUNTIME_FUNCTION entry{0,static_cast<DWORD>(sizeof(code)),32};
    std::memcpy(page+40,&entry,sizeof(entry));
    auto* const at=static_cast<unsigned char*>(edf::AllocateNearCode(image+kUpdateCalls[0],page,sizeof(page)));
    if(!at)return nullptr;
    if(!RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(at+40),1,reinterpret_cast<DWORD64>(at))) {
        VirtualFree(at,0,MEM_RELEASE);
        return nullptr;
    }
    return at;
}

struct Done { unsigned rva; std::size_t size; unsigned char stock[8]; };
bool Undo(const Done* done,unsigned n) noexcept {
    bool ok=true;
    while(n) {
        const Done& d=done[--n];
        unsigned char now[8];
        std::memcpy(now,image+d.rva,d.size);
        ok=edf::PatchCode(image+d.rva,now,d.stock,d.size) && ok;
    }
    return ok;
}
}  // namespace

bool InstallTargetGrid() noexcept {
    if(!image)return false;
    for(const auto& f:kFingerprints)
        if(!NativeCodeMatches(f.begin,f.end,f.fnv)){Log("TARGETGRID %X..%X not the stock code: off",f.begin,f.end);return false;}
    for(const unsigned site:kUpdateCalls)
        if(!edf::Matches(image,site,kUpdateStock,sizeof(kUpdateStock))){Log("TARGETGRID update call %X not the stock code: off",site);return false;}
    Done done[5]{};
    unsigned n=0;
    const auto redirect=[&](unsigned site,unsigned target,void* hook) noexcept {
        Done& d=done[n];
        d.rva=site;d.size=5;std::memcpy(d.stock,image+site,5);
        bool changed=false;
        if(!edf::RedirectCall(image+site,image+target,hook,changed) || !changed)return false;
        ++n;return true;
    };
    // The sets' changes first, the updates, the pick last: nothing reads the index before it follows every change.
    bool ok=redirect(kEraseCall,kErase,reinterpret_cast<void*>(&EraseHook)) &&
            redirect(kInsertCall,kInsert,reinterpret_cast<void*>(&InsertHook));
    unsigned char* const cave=ok ? MakeUpdateCave() : nullptr;
    ok=ok && cave;
    for(const unsigned site:kUpdateCalls) {
        if(!ok)break;
        Done& d=done[n];
        d.rva=site;d.size=sizeof(kUpdateStock);std::memcpy(d.stock,kUpdateStock,sizeof(kUpdateStock));
        ok=edf::RedirectToThunk(image+site,kUpdateStock,sizeof(kUpdateStock),cave);
        if(ok)++n;
    }
    ok=ok && redirect(kPickCall,kWalk,reinterpret_cast<void*>(&PickWalk));
    if(ok){Log("TARGETGRID nearest-enemy pick on team grids (shadow check with debug)");return true;}
    const bool undone=Undo(done,n);
    Log("TARGETGRID hooks not all in (%u): off%s",n,undone ? "" : ", NOT all put back");
    return false;
}
}  // namespace crew
