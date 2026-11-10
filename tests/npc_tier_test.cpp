// npc_tier.h offline (docs/feature-npc-weapon-tier.md): the tier table's shape, the one-tier-down rule, the requester's
// base weapon, the 64-bit arms word and the CreateObject path. No game.
#include "npc_tier.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <set>
#include <utility>

namespace {
using namespace crew;
using namespace crew::npctier;
int checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}

Arms Make(int id,std::uint8_t star) {
    Arms a;a.on=true;a.id=static_cast<std::uint16_t>(id);
    for(auto& s:a.stars)s=star;
    return a;
}
struct Fake { std::set<int> owned; std::uint8_t star=0; };
bool FakeOwned(const void* c,int id) noexcept { return static_cast<const Fake*>(c)->owned.count(id)!=0; }
bool FakeStars(const void* c,int,std::uint8_t* out) noexcept {
    for(int i=0;i<kStarCount;++i)out[i]=static_cast<const Fake*>(c)->star;
    return true;
}
Inventory Inv(const Fake& f) {
    Inventory inv;inv.context=&f;inv.owned=&FakeOwned;inv.stars=&FakeStars;return inv;
}

void Table() {
    Check(kTierRowCount>100,"the tier table has the series of every armed kind");
    std::set<std::pair<int,int>> seen;
    for(const auto& r:kTierRows) {
        Check(r.kind<kSupportWeaponCount,"a row's kind is a soldier kind");
        Check(seen.insert({r.kind,r.id}).second,"a weapon once per kind");
        Check(r.sgo && r.sgo[0],"a row names its WEAPONTABLE weapon");
        if(r.prev<0)continue;
        const TierRow* p=RowOf(static_cast<SupportWeapon>(r.kind),r.prev);
        Check(p!=nullptr,"a previous model is a row of the same kind");
        Check(p && p->series==r.series && p->level<=r.level,"the previous model: the same series, not a higher level");
    }
    for(int k=0;k<kSupportWeaponCount;++k) {
        const auto kind=static_cast<SupportWeapon>(k);
        Check(KindArmed(kind),"every kind armed, the X-Morph infantry too (2026-11-10)");
        // Each series has exactly one first model.
        std::set<int> firsts,series;
        for(const auto& r:kTierRows)if(r.kind==k){series.insert(r.series);if(r.prev<0)firsts.insert(r.series);}
        Check(firsts==series,"each series one first model");
    }
}

void Tier() {
    // Minion Buster MK2 (15) -> Minion Buster Burst (9), the same stars.
    Arms down=TierDown(Make(15,7));
    Check(down.on && down.id==9 && down.stars[0]==7 && down.stars[7]==7,"a later model: the previous one with its stars");
    // The first of a series: itself, each star one less, 0 staying 0.
    Arms first=Make(1,3);first.stars[2]=0;first.stars[5]=10;
    down=TierDown(first);
    Check(down.id==1 && down.stars[0]==2 && down.stars[2]==0 && down.stars[5]==9,"the first model: its stars one less, 0 stays 0");
    down=TierDown(Make(1,0));
    Check(down==Make(1,0),"the first model at 0 stars: unchanged");
    Check(!TierDown(Arms{}).on,"none stays none");
}

void Pick() {
    Fake f;f.star=4;
    // Equipped in the kind's series: the first slot that is.
    Inventory inv=Inv(f);inv.equipped[0]=385;inv.equipped[1]=26;inv.equipped[2]=15;
    Arms a=PickArms(SupportWeapon::rifle,inv);
    Check(a.on && a.id==15 && a.stars[0]==4,"equipped Minion Buster MK3 (slot 1): its previous model MK2, its stars");
    a=PickArms(static_cast<SupportWeapon>(5),inv);
    Check(a.on && a.id==385 && a.stars[0]==3,"equipped Power Lance (the first of its series): itself, a star less");
    // Not equipped: the highest-level owned one.
    Fake g;g.star=2;g.owned={1,9,30,803};
    inv=Inv(g);
    a=PickArms(SupportWeapon::rifle,inv);
    Check(a.on && a.id==26 && a.stars[0]==2,"owned Minion Blast the highest: Minion Buster MK3");
    a=PickArms(static_cast<SupportWeapon>(10),inv);
    Check(a.on && a.id==803 && a.stars[0]==1,"owned NC101 only: itself a star less");
    // Nothing of the kind: its lowest weapon at 0 stars.
    Fake none;inv=Inv(none);
    a=PickArms(SupportWeapon::rifle,inv);
    Check(a.on && a.id==0 && a.stars[0]==0,"a new save: the kind's lowest weapon, 0 stars");
    {
        // The X-Morph infantry (the rifle Ranger's soldier on its own model) carries the Ranger's assault rifles: the very
        // rows of the rifle kind, so the same requester gives it the same weapon.
        const Arms xm=PickArms(SupportWeapon::xmInfantry,inv);
        Check(xm.on && xm==PickArms(SupportWeapon::rifle,inv),"the X-Morph infantry: the rifle Ranger's pick");
        int rifle=0,infantry=0;
        for(const auto& r:kTierRows) {
            if(r.kind==static_cast<int>(SupportWeapon::rifle))++rifle;
            if(r.kind!=static_cast<int>(SupportWeapon::xmInfantry))continue;
            ++infantry;
            const TierRow* same=RowOf(SupportWeapon::rifle,r.id);
            Check(same && same->series==r.series && same->prev==r.prev && same->level==r.level,"each X-Morph row is a rifle row");
        }
        Check(infantry>0 && infantry==rifle,"the X-Morph infantry: every assault rifle, nothing else");
        Check(kTierRows[5].kind==static_cast<int>(SupportWeapon::rifle),"the rifle rows come first");
        Fake eq;eq.star=4;
        Inventory einv=Inv(eq);einv.equipped[0]=static_cast<int>(kTierRows[5].id);
        Check(PickArms(SupportWeapon::xmInfantry,einv)==PickArms(SupportWeapon::rifle,einv) &&
              PickArms(SupportWeapon::xmInfantry,einv).id!=kTierRows[5].id,"an equipped rifle: the same tier below it");
    }
    // Stars above the unlock cap clamp to it.
    Fake hi;hi.star=200;hi.owned={803};inv=Inv(hi);
    a=PickArms(static_cast<SupportWeapon>(10),inv);
    Check(a.stars[0]==kStarMost-1,"a star byte over 10 read as 10");
}

void Pack() {
    Arms a=Make(26,0);for(int i=0;i<kStarCount;++i)a.stars[i]=static_cast<std::uint8_t>(i+3>kStarMost ? kStarMost : i+3);
    const std::uint64_t v=PackArms(a);
    Arms b;
    Check(v && (v&kArmsOn) && UnpackArms(v,SupportWeapon::rifle,&b) && b==a,"arms round trip");
    Check(PackArms(Arms{})==0 && UnpackArms(0,SupportWeapon::rifle,&b) && !b.on,"none is 0");
    Check(!UnpackArms(v,static_cast<SupportWeapon>(5),nullptr),"another kind's weapon refused");
    Check(!UnpackArms(v&~kArmsOn,SupportWeapon::rifle,nullptr),"no on bit refused");
    Check(!UnpackArms(v|(1ull<<50),SupportWeapon::rifle,nullptr),"reserved bits refused");
    Check(!UnpackArms((v&~(0xFull<<16))|(0xBull<<16),SupportWeapon::rifle,nullptr),"a star over 10 refused");
    Check(!UnpackArms(kArmsOn|9999u,SupportWeapon::rifle,nullptr),"an unknown weapon refused");
}

void Path() {
    wchar_t out[96];
    Check(ArmsPath(SupportWeapon::rifle,Make(26,0),out,96)>0 && !wcscmp(out,L"app:/weapon/aweapon026.sgo"),"the CreateObject path, lower case");
    Check(ArmsPath(SupportWeapon::rifle,Make(1376,0),out,96)>0 && !wcscmp(out,L"app:/weapon/mpack_a_weapon002.sgo"),"a DLC weapon's path");
    Check(ArmsPath(SupportWeapon::rifle,Make(26,0),out,10)==0 && out[0]==0,"a path that does not fit: none");
    Check(ArmsPath(SupportWeapon::rifle,Arms{},out,96)==0,"none: no path");
    for(const auto& r:kTierRows) {
        Arms x=Make(r.id,0);
        Check(ArmsPath(static_cast<SupportWeapon>(r.kind),x,out,96)>0,"every row's path fits the soldier's buffer");
    }
}
}  // namespace

int main() {
    Table();Tier();Pick();Pack();Path();
    std::printf("npc_tier_test: %d checks passed\n",checks);
    return 0;
}
