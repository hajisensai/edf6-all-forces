// The scoped weapons' spread rule (src/scope_spread_logic.h), offline:
//   - sniper rifles are the weapon table's categories 2 and 104 only;
//   - a sniper rifle zoomed: the stock cone; unzoomed: x ScopeSniperHipSpread, at least ScopeSniperHipMinSpread (a
//     FireAccuracy-0 one spreads too);
//   - another scoped weapon: unzoomed x ScopeHipSpread (wider), zoomed x ScopeAimSpread (tighter); a cone of 0 stays 0;
//   - ScopeSpread=0: every cone the stock;
//   - the round's write: E14 only when both factors are positive, else E14 1 and FireAccuracy the cone; nothing when the
//     cone is already the stock;
//   - the ranges clamp (NaN to the low end).
// cmake --build build --target scope_spread_test && build\scope_spread_test.exe
#include "scope_spread_logic.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace {
using namespace crew::scopespread;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
bool Near(float a,float b) { return std::fabs(a-b)<=1e-6f*(1.0f+std::fabs(b)); }

void Categories() {
    Expect(SniperCategory(2) && SniperCategory(104),"Ranger SniperRifle and Wing Diver Pale_Long are sniper rifles");
    for(const int c:{0,1,3,4,5,6,7,8,100,101,105,200,204,300,301,-1})
        Expect(!SniperCategory(c),"assault rifles, launchers, Fencer cannons, Air Raider calls are not");
}

void Sniper() {
    const Params p;   // the defaults: x4, at least 0.015 rad
    Expect(Near(Cone(true,true,0.002f,p),0.002f),"a sniper rifle zoomed: the stock cone");
    Expect(Near(Cone(true,true,0.0f,p),0.0f),"a FireAccuracy-0 sniper rifle zoomed: exact as stock");
    Expect(Near(Cone(true,false,0.01f,p),0.04f),"unzoomed: x4");
    Expect(Near(Cone(true,false,0.002f,p),0.015f),"unzoomed: at least the floor");
    Expect(Near(Cone(true,false,0.0f,p),0.015f),"a FireAccuracy-0 sniper rifle unzoomed: the floor");
    Expect(Cone(true,false,0.002f,p)>Cone(true,true,0.002f,p),"unzoomed clearly wider than zoomed");
    Expect(Near(Factor(true,false,0.01f,p),4.0f) && Factor(true,false,0.0f,p)==0.0f,"the logged factor");
}

void Other() {
    const Params p;   // x1.25 unzoomed, x0.8 zoomed
    Expect(Near(Cone(false,false,0.02f,p),0.025f),"an assault rifle unzoomed: a little wider");
    Expect(Near(Cone(false,true,0.02f,p),0.016f),"zoomed: a little tighter");
    Expect(Cone(false,false,0.0f,p)==0.0f && Cone(false,true,0.0f,p)==0.0f,"a cone of 0 (Air Raider markers) stays 0");
}

void Off() {
    Params p;p.on=false;
    Expect(Cone(true,false,0.002f,p)==0.002f && Cone(true,false,0.0f,p)==0.0f && Cone(false,true,0.02f,p)==0.02f,
           "ScopeSpread=0: the stock everywhere");
    const Params on;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    Expect(std::isnan(Cone(false,false,nan,on)) && Cone(true,false,-1.0f,on)==-1.0f,"a cone no weapon has: untouched");
}

void Plans() {
    Write w=Plan(1.0f,0.02f,0.025f);
    Expect(w.any && Near(w.cone*w.accuracy,0.025f) && w.accuracy==0.02f,"both positive: only E14 written");
    w=Plan(0.5f,0.02f,0.04f);
    Expect(w.any && Near(w.cone,2.0f) && w.accuracy==0.02f,"a charge weapon's E14 scaled, FireAccuracy kept");
    w=Plan(1.0f,0.0f,0.015f);
    Expect(w.any && w.cone==1.0f && w.accuracy==0.015f,"FireAccuracy 0: E14 1, FireAccuracy the floor");
    w=Plan(0.0f,0.02f,0.015f);
    Expect(w.any && w.cone==1.0f && w.accuracy==0.015f,"E14 0: E14 1, FireAccuracy the cone");
    w=Plan(1.0f,0.02f,0.02f);
    Expect(!w.any,"the stock cone: nothing written");
    w=Plan(1.0f,0.0f,0.0f);
    Expect(!w.any,"a cone of 0 kept: nothing written");
    w=Plan(1.0f,0.02f,std::numeric_limits<float>::quiet_NaN());
    Expect(!w.any,"no cone: nothing written");
}

void Ranges() {
    Params p;
    p.sniperHip=50.0f;p.sniperHipMin=-1.0f;p.hip=0.5f;p.aim=std::numeric_limits<float>::quiet_NaN();
    const Params c=Clamped(p);
    Expect(c.sniperHip==kSniperHipRange.hi && c.sniperHipMin==kSniperHipMinRange.lo && c.hip==kHipRange.lo && c.aim==kAimRange.lo,
           "the ranges clamp (NaN to the low end)");
    const Params d=Clamped(Params{});
    Expect(d.sniperHip==4.0f && d.sniperHipMin==0.015f && d.hip==1.25f && d.aim==0.8f,"the defaults are inside the ranges");
    // Unzoomed never tighter than zoomed, at any value in the ranges.
    Expect(kHipRange.lo>=1.0f && kAimRange.hi<=1.0f && kSniperHipRange.lo>=1.0f,"the ranges keep unzoomed no tighter than zoomed");
}
}  // namespace

int main() {
    Categories();
    Sniper();
    Other();
    Off();
    Plans();
    Ranges();
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
