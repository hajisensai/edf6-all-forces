// support_level.h offline (docs/feature-npc-weapon-tier.md §4.1): the support level is the stock online story's weapon
// level cap (EDF.dll 0xE2710's arithmetic on CONFIG.SGO's rows), the damage multiplier a request's of that level on the
// vehicle's stock request curve (tools/call_weapons.py request_tier). The tables themselves are compared with the game's
// data by tools/selftest.py support_level_tables_match_the_game. No game.
#include "support_level.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
using namespace crew::supportlevel;
int checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
bool Near(float a,float b){return std::fabs(a-b)<=1e-4f*(1.0f+std::fabs(b));}

void Levels() {
    // Each difficulty's first and last mission: the online story's cap in UI levels (docs/weapon-level-cap-re.md §1:
    // easy / normal about 2, hard 5, hardest 35, inferno 60 at the start).
    const int first[kDifficulties]={2,2,5,35,60};
    const int last[kDifficulties]={27,27,52,77,99};
    for(int d=0;d<kDifficulties;++d) {
        Check(SupportLevel(d,0.0f)==first[d],"the first mission's level");
        Check(SupportLevel(d,1.0f)==last[d],"the last mission's level");
        int was=-1;
        for(int i=0;i<=100;++i) {
            const int l=SupportLevel(d,static_cast<float>(i)/100.0f);
            Check(l>=was,"the level never falls along the mission list");
            was=l;
        }
    }
    for(int d=0;d+1<kDifficulties;++d)
        for(int i=0;i<=20;++i) {
            const float p=static_cast<float>(i)/20.0f;
            Check(SupportLevel(d,p)<=SupportLevel(d+1,p),"a harder difficulty is never a lower level");
        }
    // 0xE2710's clamp: before the list and past it, and a NaN, as its ends.
    Check(SupportLevel(2,-3.0f)==SupportLevel(2,0.0f),"progress held at 0");
    Check(SupportLevel(2,7.0f)==SupportLevel(2,1.0f),"progress held at 1");
    Check(SupportLevel(2,std::nanf(""))==SupportLevel(2,0.0f),"a NaN progress is the first mission");
    // Interpolation: hard halfway, (0.2 + 2.1) / 2 x 25 = 28.75; inferno's four elements, a third in: 3.2 x 25 = 80.
    Check(SupportLevel(2,0.5f)==28,"hard, halfway");
    Check(SupportLevel(4,1.0f/3.0f)==80,"inferno, the second element");
    Check(SupportLevel(4,0.5f)==89,"inferno, between its second and third element");
    // Out of range: no level (the caller then refuses the vehicle).
    Check(SupportLevel(-1,0.0f)==-1 && SupportLevel(kDifficulties,0.0f)==-1,"no level past the difficulties");
    const CapRow none{2,{-1.0f,-1.0f}};
    Check(CapLevel(none,0.5f)==-1,"a negative element is no cap");
    const CapRow bad{0,{}};
    Check(CapLevel(bad,0.0f)==-1,"an empty row is no cap");
}

void Curves() {
    for(Curve c:{Curve::eros,Curve::blacker,Curve::grape}) {
        const Points p=PointsOf(c);
        Check(p.n>=2,"a curve has points");
        for(int i=0;i<p.n;++i) {
            Check(Near(TierDamage(c,p.at[i].level),p.at[i].damage),"a stock request's own multiplier at its level");
            if(i)Check(p.at[i].level>p.at[i-1].level && p.at[i].damage>=p.at[i-1].damage,"a curve rises");
        }
        Check(Near(TierDamage(c,0.0f),p.at[0].damage),"held below the lowest request");
        Check(Near(TierDamage(c,10.0f),p.at[p.n-1].damage),"held past the highest request");
        float was=0.0f;
        for(int l=0;l<=100;++l) {
            const float m=LevelDamage(c,l);
            Check(m>=was && m>0.0f,"a higher level never deals less");
            was=m;
        }
    }
    // request_tier's arithmetic, by hand: the Eros between 1.5 (x5) and 2.73 (x14) at 2.4 (inferno's first mission).
    Check(Near(LevelDamage(Curve::eros,60),5.0f+9.0f*(2.4f-1.5f)/(2.73f-1.5f)),"the Eros at level 60");
    Check(Near(LevelDamage(Curve::eros,2),1.3f),"the Eros at easy's first mission: its lowest request");
    Check(Near(LevelDamage(Curve::eros,99),25.0f),"the Eros at inferno's last mission: its highest request");
    Check(Near(LevelDamage(Curve::blacker,5),0.7f),"the Blacker's damage column (not its durability 0.5)");
    Check(Near(LevelDamage(Curve::grape,99),22.0f),"the Grape at inferno's last mission");
    Check(Near(LevelDamage(Curve::blacker,35),3.0f+4.0f*(1.4f-1.02f)/(1.92f-1.02f)),"the Blacker at hardest's first");
}
}  // namespace

int main() {
    Levels();
    Curves();
    std::printf("support_level_test: %d checks passed\n",checks);
    return 0;
}
