#pragma once
#include <cstdint>

namespace crew {
namespace enemy_radar {
// Game-thread values from a live local control lease. Object/control are opaque
// identity keys; the render hook never dereferences either actor or control.
struct View {
    std::uintptr_t ownerObject=0,ownerControl=0;
    float center[3]{},side[3]{},forward[3]{};
    int team=-1;
};
}
// Publish the complete current set, including an empty set on exit/reset. Only
// local active sessions belong here; pending/remote/restoring sessions do not.
void EnemyControlRadarPublish(const enemy_radar::View*,unsigned count) noexcept;
// Native HUiHudRader input hooks, guarded to the supported EDF.dll profile.
bool InstallEnemyControlRadar() noexcept;
// The existing PVP native friendly wrapper shares this observer resolver. False
// preserves its existing PVP/ordinary-player path and all native arguments.
bool EnemyControlRadarObserverTeam(const void* hud,int* team) noexcept;
}
