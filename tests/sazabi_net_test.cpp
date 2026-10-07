#include "../src/sazabi_net.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
int checks=0;
void Check(bool ok,const char* why) { ++checks;if(!ok){std::printf("FAIL: %s\n",why);std::exit(1);} }
}
int main() {
    using namespace crew::sazabi_net;
    State s;s.sender=1;s.sequence=1;s.controller=42;s.flags=kDriven|kAir;s.pose.guard=1;s.pose.swing=0.3f;
    s.funnels[0].phase=3;s.funnels[0].target=91;s.funnels[0].at[2]=100;
    State copy;
    Check(sizeof(s)==888 && Decode(&s,sizeof(s),copy),"bounded explicit wire roundtrip");
    Check(copy.pose.guard==1 && copy.funnels[0].target==91 && copy.funnels[0].at[2]==100,"guard and world target snapshot survive");
    Check(!Decode(&s,sizeof(s)-1,copy) && !Decode(&s,sizeof(s)+1,copy),"exact wire size required");
    State bad=s;bad.reserved2=1;Check(!Valid(bad),"reserved tail checked");
    bad=s;bad.pose.t=std::numeric_limits<float>::quiet_NaN();Check(!Valid(bad),"NaN pose refused");
    bad=s;bad.funnels[0].dir[2]=0;Check(!Valid(bad),"invalid funnel axis refused");
    bad=s;bad.funnels[0].target=-2;Check(!Valid(bad),"invalid target id refused");
    bad=s;bad.arms.guardShare=2;Check(!Valid(bad),"guard damage multiplier bounded");
    bad=s;bad.sounds[0].kind=14;Check(!Valid(bad),"invalid sound enum refused");
    Gate g;
    Check(!g.Admit(s,false,false,42) && !g.Admit(s,true,true,42),"offline and local authority ignore replay");
    Check(g.Admit(s,true,false,42) && !g.Admit(s,true,false,42),"first state accepted exactly once");
    s.sequence=4;Check(g.Admit(s,true,false,42),"lost intermediate states recover from absolute snapshot");
    s.sequence=2;Check(!g.Admit(s,true,false,42),"late state cannot roll pose backwards");
    s.sequence=5;Check(!g.Admit(s,true,false,-1),"previous client cannot reclaim host epoch");
    s.sender=2;s.sequence=1;s.controller=-1;Check(g.Admit(s,true,false,-1),"host takeover");
    Check(!g.Admit(s,true,false,99),"host late state cannot reclaim new pilot");
    s.sender=3;s.controller=99;Check(g.Admit(s,true,false,99),"new registered pilot");
    g=Gate{};Check(g.Admit(s,true,false,99),"new ObjRef/session has fresh gate");
    for(std::uint64_t i=4;i<=1026;++i) {s.sender=i;Check(g.Admit(s,true,false,99),"1024 room senders retained");}
    std::printf("sazabi_net_test: %d checks passed\n",checks);
}
