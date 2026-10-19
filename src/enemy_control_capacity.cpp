#include "enemy_control_capacity.h"
#include <Windows.h>
namespace crew {
bool EnemyControlLocalCapacityReady() noexcept {
    const auto module=GetModuleHandleW(L"EDF6Coop.dll");if(!module)return false;
    const auto get=reinterpret_cast<EDF6CoopGetExtensionCapacityFn>(GetProcAddress(module,"EDF6CoopGetExtensionCapacity"));
    if(!get)return false;EDF6CoopExtensionCapacity capacity{};
    __try {return get(EDF6COOP_EXTENSION_CAPACITY_VERSION,sizeof(capacity),&capacity)!=0 && ValidEnemyControlCapacity(capacity);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
