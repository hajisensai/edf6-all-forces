// The module registry's load and mission start passes (module.h): the installs and the resets of module_table.h.
#include "module_table.h"

namespace crew::modules {


void InstallAll(const LoadContext& context) noexcept {
    bool heliOffTold=false;
    for(const auto& e:kInstalls) {
        if(e.phase==InstallPhase::kHeliProfile && !context.heli) {
            if(!heliOffTold)Log("JET / SUB off: they are flown from the heli pilot's frame, which is off");
            heliOffTold=true;
            continue;
        }
        e.run(context);
    }
}

void ResetAll() noexcept {
    for(const auto& e:kResets)e.run();
    ClearFaults();   // every module on again: the mission's state is new
}

namespace {
bool GuardedReset(const ResetEntry& e) noexcept {
    __try { e.run(); return false; }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("FAULT %s: its reset faulted (%08lX)",kModules[static_cast<std::size_t>(e.id)].name,GetExceptionCode());
        return true;
    }
}
}  // namespace

ResetRun ResetModule(Id id) noexcept {
    ResetRun r{};
    for(const auto& e:kResets) {
        if(e.id!=id)continue;
        ++r.entries;
        r.faulted+=GuardedReset(e) ? 1 : 0;
    }
    return r;
}

}  // namespace crew::modules
