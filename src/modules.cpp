// The module registry's frame passes (module.h): the ticks and the vehicle steps of module_table.h, each under its
// module's fault guard.
#include "module_table.h"

namespace crew::modules {
namespace {

// The faults: each module's under its own id. A fault in an entry (logged per module at most every kFaultLogMs, with how
// many so far) skips that entry this time, not every entry after it.
constexpr ULONGLONG kFaultLogMs=10000;
struct Faults { unsigned count; ULONGLONG loggedAt; } faults[kModuleCount]{};

int EntryFault(Id id,const EXCEPTION_POINTERS* e) noexcept {
    const auto m=static_cast<std::size_t>(id);
    Faults& f=faults[m];
    ++f.count;
    const ULONGLONG now=GetTickCount64();
    if(f.loggedAt && now-f.loggedAt<kFaultLogMs)return EXCEPTION_EXECUTE_HANDLER;
    f.loggedAt=now;
    const auto at=static_cast<const unsigned char*>(e->ExceptionRecord->ExceptionAddress);
    const bool inGame=at>=image && at<image+0x22CE000;
    Log("FAULT %s: %08lX at %s%llX (%u so far; the step is skipped, the others go on)",kModules[m].name,e->ExceptionRecord->ExceptionCode,
        inGame ? "EDF+" : "",static_cast<unsigned long long>(inGame ? static_cast<std::uintptr_t>(at-image) : reinterpret_cast<std::uintptr_t>(at)),f.count);
    return EXCEPTION_EXECUTE_HANDLER;
}
void GuardedTick(const TickEntry& e) noexcept {
    __try { e.run(); } __except(EntryFault(e.id,GetExceptionInformation())) {}
}
void GuardedVehicle(const VehicleEntry& e,unsigned char* v,int cls) noexcept {
    __try { e.run(v,cls); } __except(EntryFault(e.id,GetExceptionInformation())) {}
}
}  // namespace

void RunTicks() noexcept {
    for(const auto& e:kTicks)GuardedTick(e);
}

void RunVehicle(VehiclePhase phase,unsigned char* v,int cls,bool enabled) noexcept {
    for(const auto& e:kVehicleSteps) {
        if(e.phase!=phase || !(enabled || (e.flags & kRunsWhenDisabled)))continue;
        GuardedVehicle(e,v,cls);
    }
}

void SoundNow(std::uint64_t ms) noexcept { soundMs=ms; }

}  // namespace crew::modules
