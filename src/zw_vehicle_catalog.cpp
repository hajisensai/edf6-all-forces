// The Vehicler's vehicles: this mission's snapshot (zw_vehicle_catalog.h). Read once at the mission's preload (support_spawn.cpp
// PreloadSupportVehicles, mission.cpp MissionStart): the equipment and the weapon records do not change within a mission.
// Only plain game-status reads (weapon_equip.h: the class word, the class's slots, a row's name, the record's flag), no call
// that loads or writes anything.
#include "zw_vehicle_catalog.h"
#include "plugin_log.h"
#include "soldier_classes_logic.h"
#include "weapon_equip.h"
#include "zw_vehicle_gear.h"
#include <array>
#include <atomic>
#include <string_view>

namespace crew {
namespace {
std::atomic<int> missionKind[wequip::kPlayers]={-1,-1};
std::atomic<std::uint32_t> unlocked{0};

int ReadKind(int player) noexcept {
    if(wequip::CurrentClass(player)!=zwclass::kVehicler)return -1;
    std::array<int,wlist::kSlotsMost> slots{};
    if(!wequip::ReadEquipped(player,zwclass::kVehicler,&slots) || slots[0]<0)return -1;
    wequip::RowFacts facts;
    if(!wequip::ReadRow(slots[0],&facts))return -1;
    return ZwVehicleKindOfRow(std::wstring_view(facts.id));
}
}  // namespace

void SnapshotZwVehicles() noexcept {
    for(auto& k:missionKind)k.store(-1,std::memory_order_relaxed);
    unlocked.store(0,std::memory_order_relaxed);
    if(!wequip::Ready()){Log("ZW vehicles: the weapon table reader is off (WEQUIP REFUSED): no Vehicler vehicle this mission");return;}
    GrantZwProgress(nullptr);   // the story cleared since the save's load: its vehicles owned before the set is read
    std::uint32_t owned=0;
    for(int k=0;k<kZwVehicleCount;++k) {
        const int row=wequip::FindRow(kZwVehicles[k].row);
        if(row>=0 && wequip::Owned(row))owned|=1u<<k;
    }
    unlocked.store(owned,std::memory_order_relaxed);
    for(int p=0;p<wequip::kPlayers;++p) {
        const int kind=ReadKind(p);
        missionKind[p].store(kind,std::memory_order_relaxed);
        if(kind>=0)Log("ZW vehicles: player %d brings %ls this mission (unlocked set %X)",p,kZwVehicles[kind].row,owned);
    }
}

int ZwMissionVehicle(int player) noexcept {
    return player>=0 && player<wequip::kPlayers ? missionKind[player].load(std::memory_order_relaxed) : -1;
}

std::uint32_t ZwUnlockedKinds(int player) noexcept {
    return player>=0 && player<wequip::kPlayers ? unlocked.load(std::memory_order_relaxed) : 0u;
}

void NoteZwUnlocked(int kind) noexcept {
    if(kind>=0 && kind<kZwVehicleCount)unlocked.fetch_or(1u<<kind,std::memory_order_relaxed);
}
}  // namespace crew
