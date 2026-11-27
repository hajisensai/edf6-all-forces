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
}

}  // namespace crew::modules
