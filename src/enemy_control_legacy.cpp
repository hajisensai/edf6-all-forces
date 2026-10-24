#include "enemy_control_legacy.h"
#include "enemy_control_legacy_mother.h"

namespace crew {
bool InstallEnemyControlLegacy() noexcept {
    const bool robo=RegisterEnemyControlAdapter(&Edf41RoboControlAdapter());
    const bool mother301=RegisterEnemyControlAdapter(&Edf41Mother301ControlAdapter());
    const bool mother401=RegisterEnemyControlAdapter(&Edf41Mother401ControlAdapter());
    return robo && mother301 && mother401;
}
}
