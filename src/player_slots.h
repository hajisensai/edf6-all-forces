#pragma once
// The player's weapon slots as a preset switches them (docs/loadout-re.md §1.2 [确认], docs/armory-re.md §4), pure:
// tests/player_slots_test.cpp checks it offline. src/armory_presets.cpp hands the plan to weapon_equip (the one writer of
// GameStatus +0x6E98 / +0x6E90), which checks every row again with the stock select's rules before writing.
//
// One table: the slots of a class and the categories a slot takes are wlist::kWeaponSlotRules (weapon_series.inc,
// generated from CONFIG.SGO SoldierInit by tools/gen_weapon_series.py), the same table the 整备 weapon page and
// wequip::SetClassSlots judge with. Nothing here repeats it.
//   class 0 Ranger 4 slots, 1 WingDiver 4, 2 AirRaider 5, 3 Fencer 6; the plugin's 4 载具手 2, 5 刻印剑士 3
//   (docs/plan-2026-11-28-new-classes-impl.md §1; their rules come into the table with tools/gen_weapon_series.py).
// A slot is planned only with a row that exists in the installed table (by name), that the player owns and whose
// category the slot takes; anything else leaves that slot as it is (counted, not an error: a weapon table without the
// row, a weapon not found yet). The level cap and duplicates are judged by wequip (wlist::PlanClassSlots).
#include "loadout_preset.h"
#include "weapon_series.h"
#include <iterator>

namespace crew::playerslots {

inline constexpr int kClasses=preset::kClasses,kSlotsMost=preset::kSlots;

constexpr int SlotsOf(int cls) noexcept {
    int n=0;
    for(const wlist::WeaponSlotRule& r:wlist::kWeaponSlotRules)
        if(r.cls==cls && r.slot+1>n)n=r.slot+1;
    return n;
}

constexpr bool SlotAccepts(int cls,int slot,int c) noexcept {
    for(const wlist::WeaponSlotRule& r:wlist::kWeaponSlotRules) {
        if(r.cls!=cls || r.slot!=slot)continue;
        for(int k=0;k<wlist::kSlotCategoriesMost && r.categories[k]>=0;++k)
            if(r.categories[k]==c)return true;
        return false;
    }
    return false;
}
static_assert(SlotsOf(0)==4 && SlotsOf(1)==4 && SlotsOf(2)==5 && SlotsOf(3)==6,
              "SoldierInit's slot counts (docs/loadout-re.md §1.2)");
// 4 / 5: their final slot counts, or not in the table yet (0: no rules, no slot planned).
static_assert((SlotsOf(4)==0 || SlotsOf(4)==2) && (SlotsOf(5)==0 || SlotsOf(5)==3),
              "the new classes' slots (docs/plan-2026-11-28-new-classes-impl.md §1)");
constexpr bool RulesInRange() noexcept {
    for(const wlist::WeaponSlotRule& r:wlist::kWeaponSlotRules)
        if(r.cls<0 || r.cls>=kClasses || r.slot<0 || r.slot>=kSlotsMost)return false;
    return true;
}
static_assert(RulesInRange(),"every slot rule is a class and slot a preset holds");

// A WeaponTable row as the game has it now.
struct RowInfo {
    int row=-1;          // -1: no row of that name
    int category=-1;
    bool owned=false;    // its record's flags bit 0
};
enum class Skip : unsigned char { none, unknown, notOwned, wrongSlot };
struct Write { int cls=0,slot=0,row=-1; };
struct Plan {
    Write write[kClasses*kSlotsMost];
    int n=0;
    int unknown=0,notOwned=0,wrongSlot=0;
    int cls=-1;   // the class to select (-1: as it is)
};

inline Skip CheckRow(int cls,int slot,const RowInfo& r) noexcept {
    if(r.row<0)return Skip::unknown;
    if(!r.owned)return Skip::notOwned;
    if(!SlotAccepts(cls,slot,r.category))return Skip::wrongSlot;
    return Skip::none;
}

// What a switch to `p` writes. `lookup(name)` gives the row of a weapon name (RowInfo).
template<class Lookup> Plan PlanWrites(const preset::Player& p,Lookup&& lookup) {
    Plan plan;
    if(p.cls<0 || p.cls>=kClasses)return plan;
    plan.cls=p.cls;
    for(int c=0;c<kClasses;++c)
        for(int s=0;s<SlotsOf(c);++s) {
            const std::string& name=p.weapon[c][s];
            if(name.empty())continue;
            const RowInfo r=lookup(name);
            switch(CheckRow(c,s,r)) {
            case Skip::none: plan.write[plan.n++]=Write{c,s,r.row}; break;
            case Skip::unknown: ++plan.unknown; break;
            case Skip::notOwned: ++plan.notOwned; break;
            case Skip::wrongSlot: ++plan.wrongSlot; break;
            }
        }
    return plan;
}

}  // namespace crew::playerslots
