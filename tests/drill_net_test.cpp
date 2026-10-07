#include "../src/drill_net.h"
#include "../src/online_authority.h"
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
    using namespace crew::drill_net;
    State launch;launch.sender=17;launch.sequence=1;launch.controller=42;
    launch.phase=Phase::out;launch.dir[2]=launch.axis[2]=1;launch.speed=90;launch.heat=0.25f;
    State decoded;
    Check(Decode(&launch,sizeof(launch),decoded),"launch serialized without native pointers");
    Check(decoded.controller==42 && decoded.phase==Phase::out && decoded.speed==90,"launch fields");
    Check(!Decode(&launch,sizeof(launch)-1,decoded),"truncated packet");
    Check(!Decode(&launch,sizeof(launch)+1,decoded),"unknown extended packet");
    State bad=launch;bad.version=2;Check(!Valid(bad),"version mismatch");
    bad=launch;bad.pos[0]=std::numeric_limits<float>::quiet_NaN();Check(!Valid(bad),"NaN pose");
    bad=launch;bad.dir[2]=0;Check(!Valid(bad),"invalid flight direction");
    bad=launch;bad.phase=static_cast<Phase>(3);Check(!Valid(bad),"unknown phase");
    bad=launch;bad.heat=1.01f;Check(!Valid(bad),"invalid heat");
    bad=launch;bad.sequence=0;Check(!Valid(bad),"reserved sequence");
    bad=launch;bad.reserved=1;Check(!Decode(&bad,sizeof(bad),decoded),"nonzero reserved wire bytes");
    Gate remote;
    Check(!remote.Admit(launch,false,false,42),"no session does not consume a packet");
    Check(!remote.Admit(launch,true,true,42),"authority ignores its own broadcast");
    Check(!remote.Admit(launch,true,false,99),"previous driver packet is rejected");
    Check(remote.Admit(launch,true,false,42),"remote same vehicle driver accepts launch");
    Check(!remote.Admit(launch,true,false,42),"duplicate launch cannot restart flight");
    State back=launch;back.sequence=3;back.phase=Phase::back;back.pos[2]=100;back.flown=100;back.backAgeMs=500;
    Check(remote.Admit(back,true,false,42),"newer snapshot supplies return and handover integrator state");
    State reordered=launch;reordered.sequence=2;
    Check(!remote.Admit(reordered,true,false,42),"out of order launch cannot undo return");
    State caught=back;caught.sequence=4;caught.phase=Phase::home;
    Check(remote.Admit(caught,true,false,42),"catch is replicated");
    Check(!remote.Admit(back,true,false,42),"late return cannot undo catch");
    State newDriver=launch;newDriver.sender=29;newDriver.controller=99;
    Check(remote.Admit(newDriver,true,false,99),"new driver's launch");
    Check(!remote.Admit(caught,true,false,99),"old driver's late catch cannot undo new flight");
    Check(!remote.Admit(caught,true,false,42),"returning driver retains sequence watermark");
    launch.sequence=5;Check(remote.Admit(launch,true,false,42),"returning driver's new event");
    State host=launch;host.sender=99;host.sequence=1;host.controller=-1;
    Check(remote.Admit(host,true,false,-1),"host takeover after driver exits uses a separate epoch");
    launch.sequence=6;Check(!remote.Admit(launch,true,false,-1),"old driver's newer delayed packet cannot override host takeover");
    newDriver.sequence=2;Check(remote.Admit(newDriver,true,false,99),"new registered driver replaces host epoch");
    host.sequence=2;Check(!remote.Admit(host,true,false,99),"old host snapshot cannot override new registered driver");
    Gate newObject;
    launch.sequence=1;
    Check(newObject.Admit(launch,true,false,42),"new ObjRef has separate replay state");
    remote=Gate{};
    Check(!remote.Admit(launch,false,false,42),"session exit ignores queued packet");
    Check(remote.Admit(launch,true,false,42),"new mission clears prior watermarks");
    Gate wrap;launch.sequence=0xFFFFFFFFu;
    Check(wrap.Admit(launch,true,false,42),"sequence before wrap");
    launch.sequence=1;Check(wrap.Admit(launch,true,false,42),"sequence wrap skips zero");
    launch.sequence=0xFFFFFFFEu;Check(!wrap.Admit(launch,true,false,42),"old sequence across wrap");
    Gate fullRoom;
    launch.sequence=1;
    for(std::uint64_t peer=1;peer<=Gate::kMaxSenders;++peer) {
        launch.sender=peer;
        Check(fullRoom.Admit(launch,true,false,42),"all 1024 supported peers can become driver");
    }
    launch.sender=17;Check(!fullRoom.Admit(launch,true,false,42),"17th driver's watermark survives full-room turnover");
    launch.sequence=2;Check(fullRoom.Admit(launch,true,false,42),"17th driver can return after full-room turnover");
    Check(!Replicated(false,2) && !Replicated(true,0) && Replicated(true,1) && Replicated(true,2),
          "offline and unregistered objects keep their local simulation");
    const crew::online::Facts authority{true,true,true,2,true,false,0,1};
    const crew::online::Facts copy{true,true,false,1,true,false,0,2};
    Check(crew::online::ShotCounts(authority,crew::online::Shooter::vehicle) &&
        !crew::online::ShotCounts(copy,crew::online::Shooter::vehicle),"only one machine counts the drill's damage");
    std::printf("drill_net_test: %d checks passed\n",checks);
}
