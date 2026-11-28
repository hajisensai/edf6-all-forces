#pragma once
// The 整备 screen's tabs, one table for both strips that show them: 整备's own title bar (armory.cpp kPages, in this
// order) and the world map's copy of it (worldmap.cpp, worldmap_logic.h kTabCount / kMapTab: the map is 任务).
// 任务 stays last: in HQ stepping onto it opens the map, so Q / E reach every other tab without passing it.
#include "hudtext.h"

namespace crew::armory::tabs {

enum : int { kPlayer, kSquad, kVehicle, kLook, kMission, kCount };

inline constexpr hudtext::Tx kName[kCount]={hudtext::Tx::armoryTabPlayer,hudtext::Tx::armoryTabSquad,
                                            hudtext::Tx::armoryTabVehicle,hudtext::Tx::armoryTabLook,
                                            hudtext::Tx::armoryTabMission};
static_assert(kMission==kCount-1,"任务 is the last tab");

inline const wchar_t* Name(int tab) noexcept { return hudtext::Tr(kName[tab]); }

}  // namespace crew::armory::tabs
