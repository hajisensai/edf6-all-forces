// The module registry's frame passes (module.h): the ticks and the vehicle steps of module_table.h, each under its
// module's fault guard, and the fault policy (module_logic.h FaultStreak; docs/module-registry.md).
#include "module_table.h"
#include "hudtext.h"
#include <cstring>
#include <cwchar>

namespace crew::modules {
namespace {

// The faults: each module's under its own id. A fault in an entry (logged per module at most every kFaultLogMs, with how
// many so far this mission) skips that entry this time, not every entry after it; kFaultLimit frames in a row of faults
// in one entry take its module off for the rest of the mission (Disable).
constexpr ULONGLONG kFaultLogMs=10000;
constexpr std::size_t kTickCount=sizeof(kTicks)/sizeof(kTicks[0]);
constexpr std::size_t kStepCount=sizeof(kVehicleSteps)/sizeof(kVehicleSteps[0]);
// The last fault's code and where (an EDF.dll RVA when `inGame`): the module's off line names it, the logged one or not.
struct Faults { unsigned count; ULONGLONG loggedAt; DWORD code; bool inGame; std::uintptr_t at; } faults[kModuleCount]{};
FaultStreak tickStreaks[kTickCount]{},stepStreaks[kStepCount]{};
bool off[kModuleCount]{};   // taken off this mission (the game thread's: the passes and the mission's start run there)

// The HUD's notice of a module taken off (ModuleNotice; hud.cpp ModuleBanner reads it on the draw thread).
SRWLOCK noticeLock=SRWLOCK_INIT;
wchar_t noticeText[160]{};
ULONGLONG noticeAt=0;

int EntryFault(Id id,const EXCEPTION_POINTERS* e) noexcept {
    const auto m=static_cast<std::size_t>(id);
    Faults& f=faults[m];
    ++f.count;
    const auto at=static_cast<const unsigned char*>(e->ExceptionRecord->ExceptionAddress);
    f.code=e->ExceptionRecord->ExceptionCode;
    f.inGame=at>=image && at<image+0x22CE000;
    f.at=f.inGame ? static_cast<std::uintptr_t>(at-image) : reinterpret_cast<std::uintptr_t>(at);
    const ULONGLONG now=GetTickCount64();
    if(f.loggedAt && now-f.loggedAt<kFaultLogMs)return EXCEPTION_EXECUTE_HANDLER;
    f.loggedAt=now;
    Log("FAULT %s: %08lX at %s%llX (%u so far; the step is skipped, the others go on)",kModules[m].name,f.code,
        f.inGame ? "EDF+" : "",static_cast<unsigned long long>(f.at),f.count);
    return EXCEPTION_EXECUTE_HANDLER;
}
// Each true: the entry faulted (its run cut short where the fault was).
bool GuardedTick(const TickEntry& e) noexcept {
    bool faulted=false;
    __try { e.run(); } __except(EntryFault(e.id,GetExceptionInformation())) { faulted=true; }
    return faulted;
}
bool GuardedVehicle(const VehicleEntry& e,unsigned char* v,int cls) noexcept {
    bool faulted=false;
    __try { e.run(v,cls); } __except(EntryFault(e.id,GetExceptionInformation())) { faulted=true; }
    return faulted;
}

// Module `m` off for the rest of the mission: its entries skipped from now on, its reset run now (what its faulted runs
// left half written dropped, what it held of the game's objects let go, as at a mission's start) under the guard (its
// state is what faulted), the line written to the log at once (a crash after this still has it), and the HUD told.
// `pass` / `entry`: the entry that reached the limit (kTicks / kVehicleSteps and its index there).
void Disable(std::size_t m,const char* pass,std::size_t entry) noexcept {
    off[m]=true;
    const Faults f=faults[m];
    const ResetRun reset=ResetModule(static_cast<Id>(m));
    Log("MODULE %s off for the rest of the mission: %s[%zu] faulted in %u frames in a row (%u faults so far, the last "
        "%08lX at %s%llX); %s; the other modules go on",kModules[m].name,pass,entry,kFaultLimit,f.count,f.code,
        f.inGame ? "EDF+" : "",static_cast<unsigned long long>(f.at),
        !reset.entries ? "it keeps no state" : reset.faulted ? "its reset faulted too" : "its state reset");
    FlushLog();
    wchar_t text[160];
    _snwprintf_s(text,_countof(text),_TRUNCATE,hudtext::Tr(hudtext::Tx::moduleOff),kModules[m].name);
    AcquireSRWLockExclusive(&noticeLock);
    std::memcpy(noticeText,text,sizeof(text));noticeAt=GetTickCount64();
    ReleaseSRWLockExclusive(&noticeLock);
}
}  // namespace

void RunTicks() noexcept {
    const ULONGLONG frame=GameFrame();
    for(std::size_t i=0;i<kTickCount;++i) {
        const auto& e=kTicks[i];
        const auto m=static_cast<std::size_t>(e.id);
        if(off[m])continue;
        if(tickStreaks[i].Run(frame,GuardedTick(e)))Disable(m,"kTicks",i);
    }
}

void RunVehicle(VehiclePhase phase,unsigned char* v,int cls,bool enabled) noexcept {
    const ULONGLONG frame=GameFrame();
    for(std::size_t i=0;i<kStepCount;++i) {
        const auto& e=kVehicleSteps[i];
        const auto m=static_cast<std::size_t>(e.id);
        if(e.phase!=phase || !(enabled || (e.flags & kRunsWhenDisabled)) || off[m])continue;
        if(stepStreaks[i].Run(frame,GuardedVehicle(e,v,cls)))Disable(m,"kVehicleSteps",i);
    }
}

void ClearFaults() noexcept {
    for(auto& f:faults)f=Faults{};
    for(auto& s:tickStreaks)s=FaultStreak{};
    for(auto& s:stepStreaks)s=FaultStreak{};
    for(auto& o:off)o=false;
    AcquireSRWLockExclusive(&noticeLock);
    noticeAt=0;
    ReleaseSRWLockExclusive(&noticeLock);
}

bool ModuleNotice(wchar_t* out,std::size_t capacity) noexcept {
    constexpr ULONGLONG kShowMs=8000;
    AcquireSRWLockShared(&noticeLock);
    const bool fresh=noticeAt && GetTickCount64()-noticeAt<=kShowMs && out && capacity;
    if(fresh)_snwprintf_s(out,capacity,_TRUNCATE,L"%ls",noticeText);
    ReleaseSRWLockShared(&noticeLock);
    return fresh;
}

void SoundNow(std::uint64_t ms) noexcept { soundMs=ms; }

}  // namespace crew::modules
