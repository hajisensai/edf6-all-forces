// The salvo store's volleys without the game (src/lockon.cpp ShotHook fires them; tools/sortie_check.cpp checks them;
// docs/lockon-re.md §3 and §7). The stock burst step (0x6940BC) fires one shot a weapon update and each shot leaves from
// one muzzle (Weapon_VehicleShoot slot 16, 0x6B3640: muzzle = round % muzzles), so 256 rounds took 256 frames. The
// plugin's shot fires a volley instead: kVolley rounds back to back, round n from muzzle n % 4 (each of the four rails
// twice), then FireBurstInterval = kVolleyGap frames to the next volley.
// Targets: the lock list's live nodes grouped by enemy in the order first locked; round n goes to enemy n % E (every
// enemy one before any gets a second, the rest shared evenly), cycling through that enemy's own lock points.
#pragma once
#include "py_consts.inc"

namespace crew {
namespace salvo {
constexpr int kRounds=pyconst::kSalvoRounds;   // pylib/vcobjects.py SALVO_ROUNDS (stores.h kSalvoLockLimit)
constexpr int kVolley=pyconst::kSalvoVolley;   // pylib/vcobjects.py SALVO_VOLLEY: rounds a volley
constexpr int kVolleyGap=pyconst::kSalvoVolleyGap;   // pylib/vcobjects.py SALVO_VOLLEY_GAP: frames between volleys (FireBurstInterval)
constexpr int kMuzzles=4;
static_assert(kVolley%kMuzzles==0,"a volley leaves from every rail alike");

// Frames from the first volley to the last of `rounds` (the first at frame 0).
constexpr int VolleyFrames(int rounds) noexcept { return rounds>0 ? ((rounds+kVolley-1)/kVolley-1)*kVolleyGap : 0; }
static_assert(VolleyFrames(kRounds)*1000/60>=1500 && VolleyFrames(kRounds)*1000/60<=2000,"the salvo goes in 1.5 to 2 s");

// Numbers the nodes' enemies: enemyOf[i] = the index of objects[i] among the distinct non-null objects in the order
// first seen, -1 for a null one (a dead lock). Returns how many distinct enemies.
inline int NumberEnemies(const void* const* objects,int nodes,int* enemyOf) noexcept {
    int enemies=0;
    for(int i=0;i<nodes;++i) {
        enemyOf[i]=-1;
        if(!objects[i])continue;
        for(int j=0;j<i;++j)if(objects[j]==objects[i]){enemyOf[i]=enemyOf[j];break;}
        if(enemyOf[i]<0)enemyOf[i]=enemies++;
    }
    return enemies;
}

// The node round `round` follows: enemy round % enemies, its (round / enemies % its nodes)-th node; -1: no live node.
inline int SalvoNode(int round,const int* enemyOf,int nodes,int enemies) noexcept {
    if(round<0 || nodes<=0 || enemies<=0)return -1;
    const int e=round%enemies;
    int own=0;
    for(int i=0;i<nodes;++i)own+=enemyOf[i]==e;
    if(!own)return -1;
    int nth=round/enemies%own;
    for(int i=0;i<nodes;++i)
        if(enemyOf[i]==e && nth--==0)return i;
    return -1;
}
}  // namespace salvo
}  // namespace crew
