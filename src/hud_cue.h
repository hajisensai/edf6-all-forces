// What the HUD shows when the player switches something (pure: no game; tools/hud_cue_check.cpp checks it offline).
//  - Change: a value the HUD watches (the picked store, EDF6AutoTurret's aim mode); a change seen is shown for a moment
//    (the banner: "a switch I can see happen", the user, 2026-10-06). The first value seen is no change: boarding, or a
//    readout coming back, shows none.
//  - StoreIcon: the drawing a store gets on the loadout strip (the user, 2026-10-06: "切换挂载应该有图片显示，而非仅文字").
//    The HUD draws only flat quads with the drawer's white texture and text (docs/hud-re.md §1, §2: no texture of the
//    game's is reachable from it), so the pictures are vector silhouettes drawn with those quads, one per kind of store.
#pragma once
#include <cstring>

namespace crew::hudcue {
struct Change { bool seen; int value; unsigned long long at; };
// Watch `value` at `now` (ms): true while a change of it is younger than `ms`.
inline bool Changed(Change& c,int value,unsigned long long now,unsigned long long ms) noexcept {
    if(!c.seen){c=Change{true,value,0};return false;}
    if(c.value!=value){c.value=value;c.at=now;}
    return c.at!=0 && now-c.at<ms;
}

// The silhouettes (a side view, the nose to the right).
enum class StoreIcon { gun, aamShort, aam, aamLong, agm, agmLight, bomb, rocket, drone, energy, charge };
// A plugin store by its cockpit name (stores.inc: AIM-9X, AIM-120, AIM-54, RIM-162 ESSM, AGM-65, AGM-114, Mk 82,
// Hydra 70; the gunship's CANNON and GATLING); `role` is StoreRole (air 0, ground 1, bomb 2, rocket 3, gun 4, drone 5, charge 6).
inline StoreIcon StoreIconOf(const char* name,int role) noexcept {
    struct Named { const char* name; StoreIcon icon; };
    static constexpr Named kNamed[]={{"AIM-9X",StoreIcon::aamShort},{"AIM-120",StoreIcon::aam},{"AIM-54",StoreIcon::aamLong},
                                     {"RIM-162 ESSM",StoreIcon::aam},{"AGM-65",StoreIcon::agm},{"AGM-114",StoreIcon::agmLight},
                                     {"Mk 82",StoreIcon::bomb},{"Hydra 70",StoreIcon::rocket},
                                     {"CANNON",StoreIcon::gun},{"GATLING",StoreIcon::gun}};   // the gunship's side guns (playerjet_board.inc kSpecials): rounds, not a bomb
    if(name)for(const Named& n:kNamed)if(std::strcmp(name,n.name)==0)return n.icon;
    switch(role) {
    case 1: return StoreIcon::agm;
    case 2: return StoreIcon::bomb;
    case 3: return StoreIcon::rocket;
    case 4: return StoreIcon::gun;
    case 5: return StoreIcon::drone;
    case 6: return StoreIcon::charge;
    default: return StoreIcon::aam;
    }
}
// A stock weapon (a stock vehicle's or heli's: vhud.h StockArm, its label by the round's class, rounds.cpp) by its round
// (RoundKind as an int: none 0, arc 1, rocket 2, homing 3; `lobbed` a grenade / mortar / napalm class): a rocket the pod,
// a homing round a missile, a lobbed one a bomb, any other a gun's rounds.
constexpr StoreIcon ArmIconOf(int kind,bool lobbed,int style=0) noexcept {
    if(style!=0)return StoreIcon::energy;
    return kind==2 ? StoreIcon::rocket : kind==3 ? StoreIcon::aam : lobbed ? StoreIcon::bomb : StoreIcon::gun;
}
}  // namespace crew::hudcue
