// EDF4.1's ObjectAction on its fortress (src/edf41fortress_action.h) checked offline: each of 4.1's seven actions maps
// to what Alien4LegTank slot 6 (EDF41.exe 0x1CA500) does with it, and none falls through to EDF6 MovingFortress's own
// action table (whose 1 crashed M117 at EDF+0x4C3A30, docs/feedback-2026-10-18-edf41-m16-crash.md).
//   cmake --build build --target edf41_fortress_check && build\edf41_fortress_check.exe      (exit code 1 on a failure)
#include "../src/edf41fortress_action.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,int a=0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%d)\n",what,a);
}
}  // namespace

int main() {
    using namespace fortress41;
    // 4.1's jump table 0x1CA5EC: 0x1C9B50(this, group, on) for 0..5, 0x1C9520 (destroyed) for 6.
    struct Want { int action; Kind kind; unsigned mask; bool on; };
    constexpr Want kWant[]={{0,Kind::Parts,1u,true},{1,Kind::Parts,1u,false},{2,Kind::Parts,2u,true},
                            {3,Kind::Parts,2u,false},{4,Kind::Parts,4u,true},{5,Kind::Parts,4u,false},
                            {6,Kind::Destroy,0u,false}};
    static_assert(sizeof(kWant)/sizeof(kWant[0])==kActions,"one row per 4.1 action");
    for(const Want& w:kWant) {
        const Action a=FromEdf41(w.action);
        Check(a.kind==w.kind,"kind",w.action);
        Check(a.groupMask==w.mask,"group mask (1 << parts_setting group)",w.action);
        Check(a.on==w.on,"on / off",w.action);
        Check(std::strcmp(Name(w.action),"not in 4.1's table")!=0,"named",w.action);
    }
    // Outside 4.1's table: 4.1 answers handled and does nothing; nothing here either (never the class's own table).
    for(int action:{-1,7,8,17,0x7FFFFFFF}) {
        const Action a=FromEdf41(action);
        Check(a.kind==Kind::Nothing && a.groupMask==0u && !a.on,"outside 4.1's table does nothing",action);
        Check(std::strcmp(Name(action),"not in 4.1's table")==0,"outside 4.1's table named so",action);
    }
    // Every part-group action touches exactly one of 4.1's three groups (cannons, gatling, bits: FORTRESS_PARTS_SETTING).
    for(int action=0;action<6;++action) {
        const unsigned m=FromEdf41(action).groupMask;
        Check(m!=0u && (m&(m-1u))==0u && m<=4u,"one group of three",action);
    }
    std::printf("%d / %d checks passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
