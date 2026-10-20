// The Dreadnought's turret block (src/dreadnought_net.h): its wire layout under the common tag 15 head, and what a
// receiver refuses (another kind or version, a round from a turret the sender does not man, an angle or a point that is
// no number, a block of another size).
#include "../src/dreadnought_net.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}
int main() {
    using namespace crew;
    using dreadnought_net::State;
    Check(offsetof(State,head)==0 && offsetof(State,manned)==sizeof(seat_net::Head),"the block starts with the common head");
    State s;
    s.head.sender=7;s.head.sequence=1;
    s.manned=0x05;s.fired=0x04;
    s.yaw[2]=1.0f;s.pitch[2]=0.3f;s.aim[2][0]=10.0f;s.aim[2][1]=20.0f;s.aim[2][2]=30.0f;
    Check(dreadnought_net::Valid(s),"a gunner's block");
    State got;
    Check(dreadnought_net::Decode(&s,sizeof(s),got) && got.fired==0x04 && got.aim[2][2]==30.0f,"decoded as sent");
    Check(!dreadnought_net::Decode(&s,sizeof(s)-1,got),"another size refused");
    State bad=s;bad.fired=0x08;
    Check(!dreadnought_net::Valid(bad),"a round from a turret the sender does not man");
    bad=s;bad.yaw[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!dreadnought_net::Valid(bad),"an angle that is no number");
    bad=s;bad.pitch[1]=4.0f;
    Check(!dreadnought_net::Valid(bad),"an angle out of range");
    bad=s;bad.aim[0][1]=std::numeric_limits<float>::infinity();
    Check(!dreadnought_net::Valid(bad),"a point that is no number");
    bad=s;bad.head.magic=0x49425A53u;
    Check(!dreadnought_net::Valid(bad),"another kind");
    bad=s;bad.head.version=2;
    Check(!dreadnought_net::Valid(bad),"another version");
    bad=s;bad.head.sequence=0;
    Check(!dreadnought_net::Valid(bad),"no sequence");
    bad=s;bad.tail=1;
    Check(!dreadnought_net::Valid(bad),"its padding written");
    seat_net::BasicGate<4> gate;
    Check(gate.Admit(s.head,true,false,-1),"the first block admitted");
    Check(!gate.Admit(s.head,true,false,-1),"the same block again refused");
    State next=s;next.head.sequence=2;
    Check(gate.Admit(next.head,true,false,-1) && !gate.Admit(s.head,true,false,-1),"an older block after a newer one refused");
    std::printf("dreadnought_net: %d checks passed\n",checks);
    return 0;
}
