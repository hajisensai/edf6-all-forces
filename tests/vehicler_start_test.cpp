// The Vehicler's start seat's pure part (src/vehicler_start_logic.h): the request's gate (who asks, who waits, who walks in),
// what follows the request (ride / wait / walk), the NPCs' driver-seat bar, the board button's +0x3F0 tail.
#include "vehicler_start_logic.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace vs=crew::vstart;
namespace {
int checks=0,failed=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { ++failed; std::printf("FAIL %s\n",what); }
}

// A local, live Vehicler offline whose slot-0 vehicle is preloaded: it asks.
vs::Facts Asker() {
    vs::Facts f;
    f.zwType=vs::kVehiclerType;
    f.local=true;
    f.alive=true;
    f.kind=2;
    f.kindReady=true;
    return f;
}
vs::AfterFacts Ready() {
    vs::AfterFacts f;
    f.state=vs::Asked::ready;
    f.haveVehicle=true;
    f.alive=true;
    f.onFoot=true;
    return f;
}

void Gate() {
    Check(vs::StartGate(Asker())==vs::Gate::request,"offline asker asks");
    auto f=Asker();f.online=true;f.sessionOpen=true;
    Check(vs::StartGate(f)==vs::Gate::request,"online asker asks once the session is open");
    f.sessionOpen=false;
    Check(vs::StartWhy(f)==vs::Why::sessionClosed && vs::StartGate(f)==vs::Gate::wait,"online, handshake not done: waits");
    f=Asker();f.sessionOpen=false;
    Check(vs::StartGate(f)==vs::Gate::request,"offline: the session is not consulted");

    for(int type:{-1,0,1,2,3,5}) {
        f=Asker();f.zwType=type;
        Check(vs::StartWhy(f)==vs::Why::notVehicler && vs::StartGate(f)==vs::Gate::ignore,"not type 4: never asks, silent");
    }
    f=Asker();f.local=false;
    Check(vs::StartWhy(f)==vs::Why::notLocal && vs::StartGate(f)==vs::Gate::ignore,"another machine's player: never asks");
    f=Asker();f.asked=true;
    Check(vs::StartWhy(f)==vs::Why::asked && vs::StartGate(f)==vs::Gate::ignore,"asked once: never again");
    f.alive=false;f.underground=true;f.kind=-1;
    Check(vs::StartGate(f)==vs::Gate::ignore,"asked wins over everything after it");
    f=Asker();f.alive=false;
    Check(vs::StartWhy(f)==vs::Why::dead && vs::StartGate(f)==vs::Gate::wait,"dead: waits");
    f=Asker();f.underground=true;
    Check(vs::StartWhy(f)==vs::Why::underground && vs::StartGate(f)==vs::Gate::walk,"underground: walks in");
    f=Asker();f.kind=-1;f.kindReady=false;
    Check(vs::StartWhy(f)==vs::Why::noVehicle && vs::StartGate(f)==vs::Gate::walk,"ZW_VehicleNone: walks in");
    f=Asker();f.kindReady=false;
    Check(vs::StartWhy(f)==vs::Why::notPreloaded && vs::StartGate(f)==vs::Gate::walk,"not preloaded: walks in, no other vehicle");
    f=Asker();f.kindReady=false;f.online=true;f.sessionOpen=false;
    Check(vs::StartGate(f)==vs::Gate::walk,"not preloaded online: walks in, does not wait for the session");
    f=Asker();f.zwType=1;f.local=false;f.alive=false;
    Check(vs::StartWhy(f)==vs::Why::notVehicler,"type first");

    for(int w=0;w<=static_cast<int>(vs::Why::notPreloaded);++w) {
        const char* t=vs::WhyText(static_cast<vs::Why>(w));
        Check(t && std::strcmp(t,"?")!=0,"every why has a text");
    }
}

void After() {
    Check(vs::AfterAsk(Ready())==vs::Next::ride,"ready, hull there, alive, on foot: rides");
    auto f=Ready();f.state=vs::Asked::pending;f.haveVehicle=false;
    Check(vs::AfterAsk(f)==vs::Next::wait,"pending: waits");
    f=Ready();f.state=vs::Asked::refused;f.haveVehicle=false;
    Check(vs::AfterAskWhy(f)==vs::AfterWhy::refused && vs::AfterAsk(f)==vs::Next::walk,"refused: walks in");
    f=Ready();f.state=vs::Asked::none;
    Check(vs::AfterAskWhy(f)==vs::AfterWhy::lost && vs::AfterAsk(f)==vs::Next::walk,"forgotten: walks in");
    f=Ready();f.haveVehicle=false;
    Check(vs::AfterAskWhy(f)==vs::AfterWhy::hullGone && vs::AfterAsk(f)==vs::Next::walk,"hull gone: walks in");
    f=Ready();f.alive=false;
    Check(vs::AfterAskWhy(f)==vs::AfterWhy::dead && vs::AfterAsk(f)==vs::Next::walk,"dead at the ride: walks in");
    f=Ready();f.onFoot=false;
    Check(vs::AfterAskWhy(f)==vs::AfterWhy::aboardOther && vs::AfterAsk(f)==vs::Next::walk,"aboard another: left there");
    f=Ready();f.busy=true;
    Check(vs::AfterAskWhy(f)==vs::AfterWhy::busy && vs::AfterAsk(f)==vs::Next::wait,"+0x39C: waited out");
    f=Ready();f.busy=true;f.onFoot=false;
    Check(vs::AfterAsk(f)==vs::Next::walk,"aboard wins over busy");
    for(int w=0;w<=static_cast<int>(vs::AfterWhy::aboardOther);++w) {
        const char* t=vs::AfterText(static_cast<vs::AfterWhy>(w));
        Check(t && std::strcmp(t,"?")!=0,"every after-why has a text");
    }
}

void Bar() {
    int owner=0,npc=0;
    Check(vs::DriverSeatBarred(0,&owner,&npc),"an NPC on the owner's driver seat: barred");
    Check(!vs::DriverSeatBarred(0,&owner,&owner),"the owner itself: not barred");
    Check(!vs::DriverSeatBarred(1,&owner,&npc) && !vs::DriverSeatBarred(5,&owner,&npc),"other seats of the owned vehicle: open");
    Check(!vs::DriverSeatBarred(0,nullptr,&npc),"a vehicle no Vehicler owns: open");
    Check(vs::kDriverSeat==0u,"the driver seat is seat 0");
}

void SeatFlag() {
    Check(vs::SetsSeatFlag(3),"key row 3 sets +0x3F0");
    for(int k:{-1,0,1,2,4})Check(!vs::SetsSeatFlag(k),"other key rows leave it");
}
}  // namespace

int main() {
    Gate();
    After();
    Bar();
    SeatFlag();
    std::printf("vehicler_start: %d checks, %d failed\n",checks,failed);
    return failed ? 1 : 0;
}
