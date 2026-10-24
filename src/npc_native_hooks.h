// Authentication for the plugin's own SoldierBase Think wrapper; never permits an unknown chained hook.
#pragma once
namespace crew {
bool NpcOwnsNativeThink(const void* nativeTable,const void* function) noexcept;
}
