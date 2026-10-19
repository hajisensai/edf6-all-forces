#include "../src/edf41bike.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace b=crew::edf41bike;
int checks=0;
void Check(bool ok,const char* reason){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
int main(){
    const auto l=b::kNativeLimits;
    Check(std::fabs(l.yawLo/b::kDegrees+20)<0.001f && std::fabs(l.yawHi/b::kDegrees-30)<0.001f,"native yaw equals negated original 408 physical limits");
    Check(std::fabs(l.pitchLo/b::kDegrees+30)<0.001f && std::fabs(l.pitchHi/b::kDegrees-15)<0.001f,"native pitch equals negated original 408 physical limits");
    Check(b::Bound(100,l.yawLo,l.yawHi)==l.yawHi && b::Bound(-100,l.yawLo,l.yawHi)==l.yawLo,"targets cannot exceed the source gun arc");
    auto d=b::Decide(true,false,true,true,false,false,true);
    Check(d.update&&d.input&&d.fire,"local gunner may fire even when another peer drives");
    d=b::Decide(true,false,true,false,false,false,true);Check(d.update&&!d.input&&!d.fire,"remote gunner interpolates but never duplicates input or shots");
    d=b::Decide(true,false,true,false,true,false,true);Check(d.update&&d.input&&d.fire,"authoritative NPC gunner fires");
    d=b::Decide(true,false,true,true,false,true,true);Check(d.update&&!d.input&&!d.fire,"map holds gunner input without replaying fire");
    for(int kind=0;kind<3;++kind){d=b::Decide(kind!=0,kind==1,kind!=2,true,true,false,true);Check(!d.update&&!d.input&&!d.fire,"disabled dead and empty operators own no work");}
    d=b::Decide(true,false,false,false,false,false,true,true);Check(d.update&&!d.input&&!d.fire,"remote empty NPC seat interpolates without input or fire ownership");
    Check(!b::Decide(true,false,true,true,false,false,false).fire,"trigger release stops fire");
    std::printf("%d original 408 control checks passed\n",checks);
}
