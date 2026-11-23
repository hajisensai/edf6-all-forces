#pragma once
// The player's weapon slots as a preset switches them (docs/loadout-re.md §1.2 [确认], docs/armory-re.md §4), pure:
// tests/player_slots_test.cpp checks it offline. src/armory_presets.cpp writes the plan into GameStatus +0x6E98 on
// the UI thread (where the stock weapon select writes it, 0x7DD8AB).
//   class 0 Ranger    4 slots: 0,1 categories 0..6; 2: 20..23; 3: 7..10 (support gear and Ranger vehicles)
//   class 1 WingDiver 4 slots: 0,1 100..106; 2: 110..116; 3: 120 (the core)
//   class 2 AirRaider 5 slots: 0..2 310..314, 303, 305; 3: 302, 330..334; 4: 306..309, 320 (the vehicle)
//   class 3 Fencer    6 slots: 0..3 200..205; 4,5: 206..209
// A slot is written only with a row that exists in the installed table (by name), that the player owns and whose
// category the slot takes; anything else leaves that slot as it is (counted, not an error: a weapon table without the
// row, a weapon not found yet).
#include "loadout_preset.h"

namespace crew::playerslots {

inline constexpr int kClasses=4,kSlotsMost=6;

constexpr int SlotsOf(int cls) noexcept { return cls==0 || cls==1 ? 4 : cls==2 ? 5 : cls==3 ? 6 : 0; }

constexpr bool SlotAccepts(int cls,int slot,int c) noexcept {
    if(slot<0 || slot>=SlotsOf(cls))return false;
    switch(cls) {
    case 0: return slot<=1 ? c>=0 && c<=6 : slot==2 ? c>=20 && c<=23 : c>=7 && c<=10;
    case 1: return slot<=1 ? c>=100 && c<=106 : slot==2 ? c>=110 && c<=116 : c==120;
    case 2: return slot<=2 ? (c>=310 && c<=314) || c==303 || c==305
                 : slot==3 ? c==302 || (c>=330 && c<=334)
                 : (c>=306 && c<=309) || c==320;
    case 3: return slot<=3 ? c>=200 && c<=205 : c>=206 && c<=209;
    default: return false;
    }
}

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
