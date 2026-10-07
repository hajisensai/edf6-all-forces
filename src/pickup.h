// The squad's sweep for item boxes (the user, 2026-10-08: "one key: spread out and pick up the boxes; health boxes
// allowed or not (the player needs them too), and never taken at full health"), the pure part: which soldier goes for
// which box. No game here (tools/pickup_check.cpp runs it offline); src/npcai.cpp reads the boxes (docs/itembox-re.md:
// DropItemManager's list, its kinds) and the soldiers, assigns them with Assign, walks each to its box and takes it.
//  - Spread out: one box a soldier and one soldier a box, pairs made nearest first (the closest box and soldier of all
//    the ones left, again and again), so the squad fans out over the boxes instead of running at one together.
//  - Weapon and armour boxes: any free soldier (the game counts them for the mission, whoever brings them in).
//  - Health boxes: only with the ini's NpcPickupHealth on, never online (the heal is the plugin's own, not the game's
//    synced one), and only a soldier under its full health goes for one (a full one leaves it for the player).
//  - Only boxes within `range` of `centre` (the player): the squad does not run off across the map.
#pragma once
#include <cmath>

namespace npc {
namespace pickup {
// DropItemManager::Type (docs/itembox-re.md §1, the model table 0x1F57358).
enum Kind : int { kWeapon=0, kArmour=1, kHealSmall=2, kHealBig=3 };
constexpr bool IsHeal(int kind) noexcept { return kind==kHealSmall || kind==kHealBig; }
// The share of its full health a health box gives (Apply 0x2C7540: 0.15, 0.30); 0 for the others.
constexpr float HealShare(int kind) noexcept { return kind==kHealBig ? 0.30f : kind==kHealSmall ? 0.15f : 0.0f; }
constexpr float kReach=1.5f;   // m (level) from its box the soldier takes it

struct Box { float pos[3]; int kind; };
struct Picker { float pos[3]; float hp,hpMax; };
struct Rules { bool health,online; float range; float centre[3]; };

inline float Level(const float* a,const float* b) noexcept {
    const float dx=b[0]-a[0],dz=b[2]-a[2];
    return std::sqrt(dx*dx+dz*dz);
}

// Whether picker `p` may go for box `b`.
inline bool Takes(const Box& b,const Picker& p,const Rules& r) noexcept {
    if(!std::isfinite(b.pos[0]+b.pos[1]+b.pos[2]) || !(Level(r.centre,b.pos)<=r.range))return false;
    if(!IsHeal(b.kind))return b.kind==kWeapon || b.kind==kArmour;
    return r.health && !r.online && p.hpMax>0.0f && p.hp<p.hpMax;
}

// Each picker's box (`out[i]`: an index into `boxes`, -1 none), nearest pairs first. Returns how many got one.
// At most kMost boxes and pickers are looked at.
constexpr int kMost=128;
inline int Assign(const Box* boxes,int nb,const Picker* ps,int np,const Rules& r,int* out) noexcept {
    if(nb>kMost)nb=kMost;
    if(np>kMost)np=kMost;
    bool boxTaken[kMost]{};
    for(int i=0;i<np;++i)out[i]=-1;
    int given=0;
    for(;;) {
        int bi=-1,pi=-1;
        float best=1e30f;
        for(int p=0;p<np;++p) {
            if(out[p]>=0)continue;
            for(int b=0;b<nb;++b) {
                if(boxTaken[b] || !Takes(boxes[b],ps[p],r))continue;
                const float d=Level(ps[p].pos,boxes[b].pos);
                if(d<best){best=d;bi=b;pi=p;}
            }
        }
        if(bi<0)return given;
        out[pi]=bi;boxTaken[bi]=true;++given;
    }
}
}  // namespace pickup
}  // namespace npc
