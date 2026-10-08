#include "../src/proteus_net.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL: %s\n",why);std::exit(1);}}
}
int main(){
    using namespace crew::proteus_net;
    State s;s.sender=1;s.sequence=1;s.controller=42;s.flags=kActive|kShieldOn;s.mode=2;
    State out;Check(Decode(&s,sizeof(s),out) && out.mode==2,"versioned state roundtrip");
    Check(!Decode(&s,sizeof(s)-1,out),"truncated state rejected");
    State bad=s;bad.reserved2=1;Check(!Valid(bad),"reserved wire bytes rejected");
    bad=s;bad.nose[2]=0;Check(!Valid(bad),"degenerate shield direction rejected");
    bad=s;bad.heat=std::numeric_limits<float>::quiet_NaN();Check(!Valid(bad),"nonfinite heat rejected");
    bad=s;bad.barrier=1.1f;Check(!Valid(bad),"barrier cannot exceed its pool");
    bad=s;bad.deploySec=bad.stowSec=0;Check(Valid(bad),"configured instant transitions are supported");
    Gate g;
    Check(g.Admit(s,true,false,true,42),"target owner accepts another machine's driver controls");
    Check(!g.Admit(s,true,false,true,42),"duplicate control rejected");
    s.sequence=3;Check(g.Admit(s,true,false,true,42),"new absolute control recovers lost state");
    s.sequence=2;Check(!g.Admit(s,true,false,true,42),"out of order control rejected");
    s.kind=Kind::defense;s.sender=2;s.sequence=1;
    Check(!g.Admit(s,true,false,true,42),"target owner refuses another peer's defense accounting");
    Check(g.Admit(s,true,true,false,42),"driver accepts distinct target-owner defense stream");
    Check(!g.Admit(s,true,true,false,-1),"old driver epoch defense cannot overwrite host takeover");
    s.controller=-1;s.sequence=2;Check(g.Admit(s,true,true,false,-1),"host NPC/empty epoch has its own valid stream");
    Check(!g.Admit(s,false,false,false,-1),"session exit rejects queued messages");
    std::printf("proteus_net_test: %d checks passed\n",checks);
}
