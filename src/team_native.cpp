// The native TeamManager with room for the PvP factions (docs/team-manager-re.md, team_native_logic.h):
//  - the constructor's 7 and the every-team walk's 7 rows become kTeamCapacity before the application's init builds
//    the manager, so the game itself makes and frees every row with its own containers;
//  - a match's relations are written under the manager's lock and again after each stock reset (mission start /
//    reload), which would otherwise put the stock defaults back.
#include "team_native.h"
#include "crew.h"
#include "memory.h"
#include <atomic>
#include <cstring>

namespace crew {
namespace {
using namespace team_native;
using ResetFn=void(__fastcall*)(void*);
using LockEnterFn=void*(__fastcall*)(void*,void*);
using LockLeaveFn=void(__fastcall*)(void*);
constexpr unsigned char kResetDefaultsSig[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0xE8};
constexpr unsigned char kResetIdentitySig[]={0xE9,0x7B,0xE9,0xFF,0xFF};   // jmp 0x5E0910
constexpr unsigned char kLockEnterSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x89,0x11};
constexpr unsigned char kLockLeaveSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x09};
bool capacity=false;
Overlay overlay{};
std::atomic<bool> overlayActive{false};

unsigned char* Manager() noexcept {
    auto* const manager=At<unsigned char*>(image,kManager);
    return manager && Readable(manager,kRowCount+8) ? manager : nullptr;
}
// Row count and row array, both checked readable for every row's relation vector.
int Rows(unsigned char* manager,unsigned char** rows) noexcept {
    const auto count=At<std::uint64_t>(manager,kRowCount);
    auto* const array=At<unsigned char*>(manager,kRows);
    if(!count || count>static_cast<std::uint64_t>(kTeamCapacity) || !array || !Readable(array,count*kRowStride))return 0;
    for(std::uint64_t i=0;i<count;++i) {
        const unsigned char* row=array+i*kRowStride;
        const auto* rel=At<const unsigned char*>(row,kRelData);
        if(At<std::uint64_t>(row,kRelSize)!=count || !rel || !Readable(rel,count*4,true))return 0;
    }
    *rows=array;
    return static_cast<int>(count);
}
// Writes `source` into every live row under the manager's own lock (a CRITICAL_SECTION: re-entrant).
bool Write(const Overlay& source) noexcept {
    __try {
        unsigned char* manager=Manager();
        unsigned char* rows=nullptr;
        const int count=manager ? Rows(manager,&rows) : 0;
        if(count!=kTeamCapacity)return false;
        void* guard=nullptr;
        reinterpret_cast<LockEnterFn>(image+kLockEnter)(&guard,manager+kLock);
        for(int a=0;a<count;++a) {
            auto* rel=At<std::int32_t*>(rows+a*kRowStride,kRelData);
            for(int b=0;b<count;++b)rel[b]=source.rel[a][b];
        }
        reinterpret_cast<LockLeaveFn>(image+kLockLeave)(&guard);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Reapply() noexcept {
    if(!overlayActive.load(std::memory_order_acquire))return;
    if(!Write(overlay))Log("TEAMS overlay not written again after the stock reset: the manager is unreadable");
}
void __fastcall ResetDefaultsHook(void* manager) noexcept {
    reinterpret_cast<ResetFn>(image+kResetDefaults)(manager);
    Reapply();
}
void __fastcall ResetIdentityHook(void* manager) noexcept {
    reinterpret_cast<ResetFn>(image+kResetIdentity)(manager);
    Reapply();
}
bool PatchSites(bool forward) noexcept {
    for(std::size_t i=0;i<kSiteCount;++i) {
        const Site& s=kSites[i];
        if(edf::PatchCode(image+s.rva,forward ? s.stock : s.patched,forward ? s.patched : s.stock,s.size))continue;
        for(std::size_t j=0;j<i;++j)
            edf::PatchCode(image+kSites[j].rva,forward ? kSites[j].patched : kSites[j].stock,forward ? kSites[j].stock : kSites[j].patched,kSites[j].size);
        return false;
    }
    return true;
}
}  // namespace

bool InstallTeamCapacity() noexcept {
    __try {
        for(const Site& s:kSites)if(!Matches(s.rva,s.stock,s.size)){Log("TEAMS refused: unexpected code at %#x",s.rva);return false;}
        if(!Matches(kResetDefaults,kResetDefaultsSig,sizeof(kResetDefaultsSig)) ||
           !Matches(kResetIdentity,kResetIdentitySig,sizeof(kResetIdentitySig)) ||
           !Matches(kLockEnter,kLockEnterSig,sizeof(kLockEnterSig)) || !Matches(kLockLeave,kLockLeaveSig,sizeof(kLockLeaveSig))) {
            Log("TEAMS refused: unexpected reset / lock code");
            return false;
        }
        // Built already: its rows are 7, and the every-team walk must stay 7 with them.
        if(At<void*>(image,kManager)){Log("TEAMS refused: the team manager was built before the plugin loaded");return false;}
        if(!PatchSites(true)){Log("TEAMS refused: a capacity site could not be written");return false;}
        void* const targets[]={reinterpret_cast<void*>(&ResetDefaultsHook),reinterpret_cast<void*>(&ResetIdentityHook)};
        unsigned redirected=0;
        for(const ResetSite& site:kResetSites) {
            bool changed=false;
            const bool defaults=site.target==kResetDefaults;
            if(RedirectCall(image+site.rva,image+site.target,targets[defaults ? 0 : 1],changed))++redirected;
            else Log("TEAMS reset site %#x not redirected: a match's relations are lost after it",site.rva);
        }
        capacity=true;
        Log("TEAMS capacity=%d (stock 7, factions from %d) reset sites=%u/%u",kTeamCapacity,kFirstFactionTeam,redirected,
            static_cast<unsigned>(sizeof(kResetSites)/sizeof(kResetSites[0])));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

int NativeTeamRows() noexcept {
    __try {
        unsigned char* manager=Manager();
        unsigned char* rows=nullptr;
        return manager ? Rows(manager,&rows) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}

bool NativeTeamValid(int team) noexcept { return team>=0 && team<NativeTeamRows(); }

bool SetTeamOverlay(const Overlay& next) noexcept {
    if(!capacity || !next.Valid())return false;
    overlay=next;
    if(!Write(overlay)){overlayActive.store(false,std::memory_order_release);return false;}
    overlayActive.store(true,std::memory_order_release);
    return true;
}

void ClearTeamOverlay() noexcept {
    if(!overlayActive.exchange(false,std::memory_order_acq_rel))return;
    Overlay stock{};
    stock.Stock();
    if(!Write(stock))Log("TEAMS stock relations not written back: the manager is unreadable");
}

bool TeamOverlayActive() noexcept { return overlayActive.load(std::memory_order_acquire); }

int NativeTeamRelation(int a,int b) noexcept {
    __try {
        unsigned char* manager=Manager();
        unsigned char* rows=nullptr;
        const int count=manager ? Rows(manager,&rows) : 0;
        if(a<0 || b<0 || a>=count || b>=count)return -1;
        return At<const std::int32_t*>(rows+a*kRowStride,kRelData)[b];
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
}  // namespace crew
