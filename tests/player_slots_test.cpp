// The player's weapon slots a preset switches (src/player_slots.h), offline: slot counts and categories per class as
// docs/loadout-re.md §1.2 lists them (read from wlist::kWeaponSlotRules), and a switch's plan writing only owned rows of the right category.
#include "../src/player_slots.h"
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
using namespace crew::playerslots;
}  // namespace

int main() {
    Check(SlotsOf(0)==4 && SlotsOf(1)==4 && SlotsOf(2)==5 && SlotsOf(3)==6 && SlotsOf(6)==0 && SlotsOf(-1)==0,"slot counts");
    // the plugin's classes (docs/plan-2026-11-28-new-classes-impl.md §1), once their rules are in the generated table
    if(SlotsOf(4))Check(SlotsOf(4)==2 && SlotAccepts(4,0,400) && SlotAccepts(4,1,401) && !SlotAccepts(4,0,401) &&
                        !SlotAccepts(4,1,0),"vehicler: 400 vehicle, 401 pistol");
    if(SlotsOf(5))Check(SlotsOf(5)==3 && SlotAccepts(5,0,500) && SlotAccepts(5,1,501) && SlotAccepts(5,2,502) &&
                        !SlotAccepts(5,0,200),"rune blade: 500 melee, 501 ranged, 502 support");
    static_assert(SlotsOf(3)<=kSlotsMost && crew::preset::kSlots==kSlotsMost,"a preset holds every slot");
    // Ranger
    Check(SlotAccepts(0,0,0) && SlotAccepts(0,1,6) && !SlotAccepts(0,0,7),"ranger main weapons 0..6");
    Check(SlotAccepts(0,2,20) && SlotAccepts(0,2,23) && !SlotAccepts(0,2,24) && !SlotAccepts(0,2,6),"ranger grenades");
    Check(SlotAccepts(0,3,7) && SlotAccepts(0,3,10) && !SlotAccepts(0,3,11),"ranger support / vehicles");
    Check(!SlotAccepts(0,4,0),"ranger has 4 slots");
    // Wing Diver
    Check(SlotAccepts(1,0,100) && SlotAccepts(1,1,106) && !SlotAccepts(1,1,107),"wing diver weapons");
    Check(SlotAccepts(1,2,110) && SlotAccepts(1,2,116) && !SlotAccepts(1,2,120),"wing diver support");
    Check(SlotAccepts(1,3,120) && !SlotAccepts(1,3,116),"wing diver core");
    // Air Raider (3xx: class 2)
    Check(SlotAccepts(2,0,310) && SlotAccepts(2,2,314) && SlotAccepts(2,1,303) && SlotAccepts(2,1,305),"air raider weapons");
    Check(!SlotAccepts(2,0,304) && !SlotAccepts(2,0,302) && !SlotAccepts(2,0,306),"air raider weapons exclude support / vehicles");
    Check(SlotAccepts(2,3,302) && SlotAccepts(2,3,330) && SlotAccepts(2,3,334) && !SlotAccepts(2,3,303),"air raider support");
    Check(SlotAccepts(2,4,306) && SlotAccepts(2,4,309) && SlotAccepts(2,4,320) && !SlotAccepts(2,4,310),"air raider vehicle");
    Check(!SlotAccepts(2,5,306),"air raider has 5 slots");
    // Fencer (2xx: class 3)
    Check(SlotAccepts(3,0,200) && SlotAccepts(3,3,205) && !SlotAccepts(3,3,206),"fencer weapons");
    Check(SlotAccepts(3,4,206) && SlotAccepts(3,5,209) && !SlotAccepts(3,5,210) && !SlotAccepts(3,6,206),"fencer support");
    Check(!SlotAccepts(-1,0,0) && !SlotAccepts(6,0,0) && !SlotAccepts(4,0,0) && !SlotAccepts(5,0,200),"no other class");
    // one table: every category of every wlist::kWeaponSlotRules slot accepted, nothing else in that class
    for(const crew::wlist::WeaponSlotRule& r:crew::wlist::kWeaponSlotRules) {
        Check(r.slot<SlotsOf(r.cls),"each rule's slot counted");
        for(int c=-1;c<600;++c) {
            bool listed=false;
            for(int k=0;k<crew::wlist::kSlotCategoriesMost && r.categories[k]>=0;++k)listed=listed || r.categories[k]==c;
            Check(SlotAccepts(r.cls,r.slot,c)==listed,"SlotAccepts is kWeaponSlotRules");
        }
    }

    // the plan
    const std::map<std::string,RowInfo> table{{"AssultRifle01",{5,0,true}},{"aHandGrenade01",{40,20,true}},
                                              {"pRapier01",{300,100,false}},{"eDrone_sa01",{900,302,true}},
                                              {"hShield01",{1200,200,true}}};
    auto lookup=[&](const std::string& n){ auto it=table.find(n); return it==table.end() ? RowInfo{} : it->second; };
    crew::preset::Player p;
    const Plan none=PlanWrites(p,lookup);
    Check(none.n==0 && none.cls==-1,"no player stored: nothing written");
    p.cls=3;
    p.weapon[0][0]="AssultRifle01";      // fits
    p.weapon[0][1]="aHandGrenade01";     // grenade in a main slot: wrong slot
    p.weapon[0][2]="aHandGrenade01";     // fits
    p.weapon[1][0]="pRapier01";          // not owned
    p.weapon[2][3]="eDrone_sa01";        // fits
    p.weapon[2][4]="Missing";            // not in the table
    p.weapon[3][0]="hShield01";          // fits
    p.weapon[0][5]="AssultRifle01";      // past the Ranger's slots: never looked at
    const Plan plan=PlanWrites(p,lookup);
    Check(plan.cls==3,"class selected");
    Check(plan.n==4 && plan.wrongSlot==1 && plan.notOwned==1 && plan.unknown==1,"only owned rows of the right category");
    bool rifle=false,grenade=false,drone=false,shield=false;
    for(int i=0;i<plan.n;++i) {
        const Write& w=plan.write[i];
        rifle=rifle || (w.cls==0 && w.slot==0 && w.row==5);
        grenade=grenade || (w.cls==0 && w.slot==2 && w.row==40);
        drone=drone || (w.cls==2 && w.slot==3 && w.row==900);
        shield=shield || (w.cls==3 && w.slot==0 && w.row==1200);
    }
    Check(rifle && grenade && drone && shield,"each write at its class and slot");
    // a preset of the plugin's class 5: selected; slots past its 3 (or all, before its rules are in) never looked at
    crew::preset::Player q;
    q.cls=5;
    q.weapon[5][3]="AssultRifle01";
    q.weapon[4][2]="AssultRifle01";
    const Plan rune=PlanWrites(q,lookup);
    Check(rune.cls==5 && rune.n==0 && rune.unknown==0 && rune.wrongSlot==0,"class 5 selected, no slot it lacks");
    std::printf("player_slots: %d checks passed\n",checks);
    return 0;
}
