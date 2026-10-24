// What the HUD shows when the player switches something (pure: no game; tools/hud_cue_check.cpp checks it offline).
//  - Change: a value the HUD watches (the picked store, EDF6AutoTurret's aim mode); a change seen is shown for a moment
//    (the banner: "a switch I can see happen", the user, 2026-10-06). The first value seen is no change: boarding, or a
//    readout coming back, shows none.
//  - StoreIcon: the drawing a store gets on the loadout strip (the user, 2026-10-06: "切换挂载应该有图片显示，而非仅文字").
//    The HUD draws only flat quads with the drawer's white texture and text (docs/hud-re.md §1, §2: no texture of the
//    game's is reachable from it), so the pictures are vector silhouettes drawn with those quads, one per kind of store.
#pragma once
#include "cockpit_fields.inc"
#include "reticle.h"
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
enum class StoreIcon { gun, aamShort, aam, aamLong, agm, agmLight, bomb, rocket, drone, energy, charge, shell };
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
// a homing round a missile, a lobbed one a bomb, an energy weapon (`style` WeaponStyle not projectile) its emitter, a gun
// laid shot by shot (`cannon`: reticle.h Classify's cannon or precision sight, a tank's main gun) one big shell (the user,
// 2026-10-12: 「这个榴弹炮图标也不对」), any other a gun's rounds.
constexpr StoreIcon ArmIconOf(int kind,bool lobbed,int style=0,bool cannon=false) noexcept {
    if(style!=0)return StoreIcon::energy;
    return kind==2 ? StoreIcon::rocket : kind==3 ? StoreIcon::aam : lobbed ? StoreIcon::bomb : cannon ? StoreIcon::shell : StoreIcon::gun;
}
// What a physical cockpit's weapons screen calls a weapon (cockpit_instruments.h arm / rowRound): its round's class, each
// with its tag, colour and picture there (pylib/cockpit_instruments.py ROUNDS, in this order). The user, 2026-10-19:
// 「看不出来1，2是啥弹种啊。你可以做成彩色的或者有区分之类的。或者战雷」.
// CockpitRound: src/cockpit_fields.inc (generated from pylib/cockpit_fields.py ROUND_CLASSES).
// A plugin aircraft's store by its picture.
constexpr CockpitRound CockpitRoundOf(StoreIcon icon) noexcept {
    switch(icon) {
    case StoreIcon::gun: return CockpitRound::mg;
    case StoreIcon::shell: return CockpitRound::gun;
    case StoreIcon::rocket: return CockpitRound::rkt;
    case StoreIcon::aamShort: case StoreIcon::aam: case StoreIcon::aamLong: return CockpitRound::aam;
    case StoreIcon::agm: case StoreIcon::agmLight: return CockpitRound::msl;
    case StoreIcon::bomb: return CockpitRound::bomb;
    case StoreIcon::energy: return CockpitRound::beam;
    case StoreIcon::drone: case StoreIcon::charge: return CockpitRound::spc;
    }
    return CockpitRound::mg;
}
// A stock weapon by its round (ArmIconOf's `kind`, `lobbed`, `style`, `bigGun`: a gun laid shot by shot) and the round
// its sight names (reticle.h Shell): a gun's round by that tag, a lobbed one without a tag HE, a big gun without one GUN,
// any other gun MG.
constexpr CockpitRound CockpitRoundOfArm(int kind,bool lobbed,int style,bool bigGun,reticle::Shell shell) noexcept {
    if(style!=0)return CockpitRound::beam;
    if(kind==2)return CockpitRound::rkt;
    if(kind==3)return CockpitRound::msl;
    switch(shell) {
    case reticle::Shell::ap: return CockpitRound::ap;
    case reticle::Shell::he: return CockpitRound::he;
    case reticle::Shell::beam: return CockpitRound::beam;
    case reticle::Shell::heat: return CockpitRound::heat;
    case reticle::Shell::canister: return CockpitRound::can;
    case reticle::Shell::none: break;
    }
    return lobbed ? CockpitRound::he : bigGun ? CockpitRound::gun : CockpitRound::mg;
}
// A weapon cell's loading bar (the user, 2026-10-12: 「两发之间显示『装填 1.8s』，在挂载上加上那种读条呢」): the share loaded,
// 0..1, or < 0 for no bar. Out of rounds and reloading the magazine (`reload` < 1: vhud.cpp Reload) its share; with rounds,
// the next round going in (`cycleSec` >= 0: vhud.cpp Cycle, a gun of 0.5 s or more between shots) `cycle`; ready: none.
constexpr float CellLoad(int ammo,float reload,float cycleSec,float cycle) noexcept {
    return ammo<=0 ? (reload<1.0f ? (reload<0.0f ? 0.0f : reload) : -1.0f)
                   : cycleSec>=0.0f ? (cycle<0.0f ? 0.0f : cycle>1.0f ? 1.0f : cycle) : -1.0f;
}

// The order the loadout strip lays a seat's weapons out in, left to right on the screen (the user, 2026-10-12, a Nix:
// 「左侧武器放在右边右侧武器放在左边」: the seat lists its right arm's gun first, and the strip drew the list's order). `side`
// is each weapon's muzzle off the vehicle's middle across its own bore, m (+ its left: vhud.cpp SideOf; 0 unknown): not
// across the view, which the free-look key turns round the vehicle (2026-10-21: the strip reordered while looking round).
// Those more than kSideApart to the left first, then those about the middle, then those to the right, each group in the
// seat's own order (stable: a turret's stores on one gun keep their R order, and a seat with no muzzle read is as listed).
constexpr float kSideApart=0.5f;
inline void SideOrder(const float* side,int n,int* order) noexcept {
    int k=0;
    for(int group=0;group<3;++group)
        for(int i=0;i<n;++i) {
            const int g=side[i]>kSideApart ? 0 : side[i]<-kSideApart ? 2 : 1;
            if(g==group)order[k++]=i;
        }
}

// A seat's weapons in the one order and with the one pick every read-out of them uses: the cockpit's weapons screen
// (cockpit_screens.inc PublishInstruments), the loadout strips (hud.cpp StockCells, JetCells) and the switch banner. Each
// row is where its weapon is read from: a stock vehicle's arm (vhud.h StockHudReadout.arm[index]), a plugin aircraft's
// store (PlayerJetReadout store `index`) or its gun (index -1). They read the same pick: a strip once framed only the
// fire-control's selected arm while the screen fell back to the sight's (a heli's door gunner: no selected arm).
enum class ArmSource : unsigned char { stock, store, gun };
struct SeatArmRow { ArmSource source; int index; };
constexpr int kMostSeatArms=16;
struct SeatArms { int n=0; int picked=-1; SeatArmRow rows[kMostSeatArms]{}; };
// A stock vehicle's `arms` weapons laid out as they stand (SideOrder); the picked one the fire-control's `selected`, else
// the one the sight is laid by (`sight`), else the first.
inline SeatArms StockSeatArms(const float* side,int arms,int selected,int sight) noexcept {
    SeatArms a{};
    if(!side || arms<=0)return a;
    if(arms>kMostSeatArms)arms=kMostSeatArms;
    int order[kMostSeatArms];
    SideOrder(side,arms,order);
    const int fired=selected>=0 && selected<arms ? selected : sight>=0 && sight<arms ? sight : 0;
    for(int k=0;k<arms;++k) {
        a.rows[a.n++]=SeatArmRow{ArmSource::stock,order[k]};
        if(order[k]==fired)a.picked=k;
    }
    return a;
}
// A plugin aircraft's `stores` (the secondary's, `store` the one it fires), then its gun (the primary, when it has one);
// without a picked store the gun.
inline SeatArms JetSeatArms(int stores,int store,bool gun) noexcept {
    SeatArms a{};
    if(stores<0)stores=0;
    if(stores>kMostSeatArms-1)stores=kMostSeatArms-1;
    for(int i=0;i<stores;++i) {
        a.rows[a.n++]=SeatArmRow{ArmSource::store,i};
        if(i==store)a.picked=i;
    }
    if(gun) {
        a.rows[a.n++]=SeatArmRow{ArmSource::gun,-1};
        if(a.picked<0)a.picked=a.n-1;
    }
    return a;
}

// Whether a stock vehicle's turret aim overlay (the auto-aim state and keys, the lock box, the range, the lead circle:
// hud.cpp TurretAimMarks) is the fire-control pick's: arm `sight` (vhud.h StockHudReadout.sight) is the gun the turret is
// laid by, arm `turret` (turretcam.cpp TurretGun, the one EDF6AutoTurret leads). Not "the first arm" (2026-10-10, the
// user: 「自瞄之类的只有原始的弹药有。ap和he之类的少了」: the APFSDS / HE stores on the main gun's holder are arms 1.., and
// the turret is laid by them once picked); a pick the turret does not turn with (the Titan's hull gatling) has none.
constexpr bool TurretOverlayOwned(int sight,int turret) noexcept { return sight>=0 && sight==turret; }
}  // namespace crew::hudcue
