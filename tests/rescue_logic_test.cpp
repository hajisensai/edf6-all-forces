// The sea rescue's and the room's pure decisions, offline (2026-10-10): src/rescue_logic.h (why a rescue is called off
// for its requester), src/support_entry.h TakeoffRoute (where a rescue takes off: a carrier's deck, else the edge) and
// src/version_notice.h (what the HUD says when the room's builds differ).
#include "../src/rescue_logic.h"
#include "../src/support_entry.h"
#include "../src/support_protocol.h"
#include "../src/version_notice.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::printf("FAIL %s\n",what);std::exit(1);}
}

void Cancels() {
    using namespace crew::rescue;
    const Requester swimming{true,false,false,true,false,false};
    Check(PickupCancel(swimming)==Cancel::none,"a requester swimming: the pickup goes on");
    Requester r=swimming;r.present=false;
    Check(PickupCancel(r)==Cancel::gone,"the requester's object gone (left the room): called off");
    r=swimming;r.dead=true;
    Check(PickupCancel(r)==Cancel::dead,"the requester dead: called off");
    r=swimming;r.onFoot=false;
    Check(PickupCancel(r)==Cancel::otherVehicle,"in another vehicle: called off");
    r.inOtherRescue=true;
    Check(PickupCancel(r)==Cancel::otherRescue,"picked up by another rescue: called off, said as such");
    r=swimming;r.dry=true;
    Check(PickupCancel(r)==Cancel::ashore,"out of the sea long enough: called off");
    r=swimming;r.onFoot=false;r.aboard=true;r.dry=true;
    Check(PickupCancel(r)==Cancel::none,"aboard this heli (no longer on foot, out of the water): not a cancel");
    r.dead=true;
    Check(PickupCancel(r)==Cancel::dead,"dead aboard: called off");
    for(Cancel c:{Cancel::gone,Cancel::dead,Cancel::otherRescue,Cancel::otherVehicle,Cancel::ashore})
        Check(std::strlen(CancelText(c))>10 && std::strcmp(CancelText(c),CancelText(Cancel::none))!=0,"every cancel has its own log words");
}

void Takeoff() {
    using namespace crew::support;
    crew::PlayArea area{};area.lo[0]=area.lo[1]=-1500;area.hi[0]=area.hi[1]=1500;area.ground=true;
    const float swimmer[3]={100,-3,0};
    const float deck[2][3]={{900,193,0},{400,193,300}};
    int calls=0;
    const auto open=[&](const float*,const float*) noexcept {++calls;return true;};
    Route route{};
    Check(TakeoffRoute(area,swimmer,deck,2,open,route)==Refusal::none,"a deck within reach: a takeoff route");
    Check(route.from[0]==400 && route.from[2]==300 && route.from[1]==193+kTakeoffLift,"the nearer deck, kTakeoffLift over it");
    Check(std::fabs(route.heading[0]*route.heading[0]+route.heading[2]*route.heading[2]-1.0f)<1e-4f && route.heading[1]==0 &&
          route.heading[2]<0,"heading level, towards the swimmer");
    // The climb column blocked over the nearer deck: the farther one.
    const auto blockNear=[&](const float* a,const float* b) noexcept {return !(a[0]==400 && b[0]==400);};
    Check(TakeoffRoute(area,swimmer,deck,2,blockNear,route)==Refusal::none && route.from[0]==900,"a blocked climb: the next spot");
    const auto closed=[&](const float*,const float*) noexcept {return false;};
    Check(TakeoffRoute(area,swimmer,deck,2,closed,route)==Refusal::noEntry,"no clear climb anywhere: none (the caller takes the edge)");
    const float far[1][3]={{100+kTakeoffReach+10,193,0}};
    crew::PlayArea big=area;big.hi[0]=10000;
    Check(TakeoffRoute(big,swimmer,far,1,open,route)==Refusal::noEntry,"a deck past kTakeoffReach: none");
    const float outside[1][3]={{1600,193,0}};
    Check(TakeoffRoute(area,swimmer,outside,1,open,route)==Refusal::noEntry,"a spot outside the measured area: none");
    Check(TakeoffRoute(area,swimmer,deck,0,open,route)==Refusal::noEntry,"no spot at all: none");
    crew::PlayArea unmeasured=area;unmeasured.ground=false;
    Check(TakeoffRoute(unmeasured,swimmer,deck,2,open,route)==Refusal::noArea,"an unmeasured area: refused");
    const float over[1][3]={{100,193,0}};
    calls=0;
    Check(TakeoffRoute(area,swimmer,over,1,open,route)==Refusal::none && calls==1 && route.heading[2]==1.0f,
          "a deck right over the swimmer: only the climb checked, a default heading");
}

void Versions() {
    using namespace crew::versionnote;
    using crew::support_net::kCapabilities;using crew::support_net::kCapSoldierVariants;using crew::support_net::kCapAirborneAir;using crew::support_net::kCapSeaRescue;
    State s;s.online=true;s.mine=kCapabilities;s.silentHost=kCapSoldierVariants|kCapAirborneAir;
    Check(Compare(s).kind==Kind::none,"a guest before the welcome: nothing said");
    s.hostKnown=true;s.hostCaps=kCapabilities;
    Check(Compare(s).kind==Kind::none,"the same build: nothing said");
    s.hostCaps=0;
    Notice n=Compare(s);
    Check(n.kind==Kind::hostOlder && n.missing==kCapSeaRescue,"a host that announces nothing: older, the sea rescue named");
    s.hostCaps=kCapSoldierVariants;n=Compare(s);
    Check(n.kind==Kind::hostOlder && n.missing==(kCapAirborneAir|kCapSeaRescue),"an older host: what it lacks named");
    s.hostCaps=kCapabilities|16u;n=Compare(s);
    Check(n.kind==Kind::selfOlder,"a host with a capability this build does not know: this guest is older");
    s.hostCaps=kCapabilities;s.roomBehind=true;n=Compare(s);
    Check(n.kind==Kind::roomOlder,"the host says another guest is behind it");
    s.roomBehind=false;s.commandPeers=1;n=Compare(s);
    Check(n.kind==Kind::commandOnly && n.command,"map command protocol differs only: said");
    State h;h.online=true;h.host=true;h.mine=kCapabilities;
    Check(Compare(h).kind==Kind::none,"a host with every guest the same: nothing");
    h.peersBehind=2;h.peersMissing=kCapSeaRescue;n=Compare(h);
    Check(n.kind==Kind::peersOlder && n.count==2 && n.missing==kCapSeaRescue,"a host: how many guests are older and what they lack");
    h.peersBehind=0;h.peersAhead=1;n=Compare(h);
    Check(n.kind==Kind::peersNewer && n.count==1,"a host: a guest newer than it");
    h.peersAhead=0;h.commandPeers=1;h.peersBehind=1;h.peersMissing=kCapAirborneAir;n=Compare(h);
    Check(n.kind==Kind::peersOlder && n.command,"older guests and a different map command protocol: both said");
    State off;off.mine=kCapabilities;off.hostKnown=true;
    Check(Compare(off).kind==Kind::none,"offline: nothing");
    Check(Compare(s)!=Compare(h) && Compare(h)==Compare(h),"notices compare by what they say (said once a change)");
}
}  // namespace

int main() {
    Cancels();
    Takeoff();
    Versions();
    std::printf("rescue_logic_test: %d checks passed\n",checks);
    return 0;
}
