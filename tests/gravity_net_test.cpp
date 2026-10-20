// The gravity operations' and the float guns' wire (src/gravity_net_protocol.h): no game, no transport.
//  - a grab and a gun message: round trip; another magic, the wrong size, a newer version, an unknown kind, a non-canonical
//    ID, a number not finite refused;
//  - the grab inbox: a hold starts a grab and later holds carry it, a throw or a let-go ends it, a late hold of an ended
//    grab or an older one of the same run is nothing, a throw of a grab never held still throws, a silent hold lets go,
//    a new run (the sender restarted) starts over, peers apart.
#include "../src/gravity_net_protocol.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace crew::gravity_net;
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);} }

const unsigned char kPlayer[32]={0x11,0x22,0,0,0,0,0,0,0,0,0,0,5,0,0,0,7,0,0,0,0,0,0,0,9,9};
const unsigned char kEnemy[32]={0x33,0x44,0,0,0,0,0,0,0,0,0,0,5,0,0,0,8,0,0,0,0,0,0,0,1,2};

Grab MakeGrab(GrabKind kind,std::uint32_t origin,std::uint32_t grab) {
    Grab g{};
    g.kind=kind;g.origin=origin;g.grab=grab;
    std::memcpy(g.grabber,kPlayer,32);std::memcpy(g.target,kEnemy,32);
    const float at[3]={10,2,30},a[3]={0,1.5f,0},b[3]={0,1.5f,400},goal[3]={0,4,8},vel[3]={1,0,-2};
    std::memcpy(g.at,at,12);std::memcpy(g.rayA,a,12);std::memcpy(g.rayB,b,12);std::memcpy(g.goal,goal,12);std::memcpy(g.vel,vel,12);
    return g;
}

void WireChecks() {
    unsigned char bytes[kGrabWire];
    const Grab g=MakeGrab(GrabKind::thrown,7,3);
    Check(Encode(g,bytes,sizeof(bytes)),"a grab encodes");
    Grab back{};
    Check(OwnedGrab(bytes,sizeof(bytes)) && !OwnedGun(bytes,sizeof(bytes)),"a grab is GRVT, not FGUN");
    Check(Decode(bytes,sizeof(bytes),back) && back.kind==GrabKind::thrown && back.origin==7 && back.grab==3 &&
          !std::memcmp(back.grabber,kPlayer,32) && !std::memcmp(back.target,kEnemy,32) && back.at[2]==30.0f &&
          back.rayB[2]==400.0f && back.goal[1]==4.0f && back.vel[2]==-2.0f,"a grab round trip");
    Check(!Encode(g,bytes,sizeof(bytes)-1),"a grab: the wrong size is refused");
    unsigned char copy[kGrabWire];
    std::memcpy(copy,bytes,sizeof(copy));copy[4]=static_cast<unsigned char>(kVersion+1);
    Check(OwnedGrab(copy,sizeof(copy)) && !Decode(copy,sizeof(copy),back),"a grab of a newer version: owned, not taken");
    std::memcpy(copy,bytes,sizeof(copy));copy[8]=9;
    Check(!Decode(copy,sizeof(copy),back),"a grab of an unknown kind is refused");
    Grab bad=g;bad.target[21]=1;
    Check(!Encode(bad,bytes,sizeof(bytes)),"a grab with a non-canonical ID is refused");
    bad=g;bad.goal[0]=std::nanf("");
    Check(!Encode(bad,bytes,sizeof(bytes)),"a grab with a number not finite is refused");
    Grab none=g;std::memset(none.target,0,32);
    Check(Encode(none,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),back) && back.target[0]==0,"a grab of a thing with no ID");

    unsigned char gb[kGunWire];
    Gun m{};
    m.kind=GunKind::owner;m.origin=5;m.serial=0x1234;m.index=2;std::memcpy(m.who,kPlayer,32);m.at[0]=-7.5f;m.leftMs=90000;
    Check(Encode(m,gb,sizeof(gb)),"a gun message encodes");
    Gun mb{};
    Check(OwnedGun(gb,sizeof(gb)) && !OwnedGrab(gb,sizeof(gb)),"a gun message is FGUN, not GRVT");
    Check(Decode(gb,sizeof(gb),mb) && mb.kind==GunKind::owner && mb.serial==0x1234 && mb.index==2 && mb.origin==5 &&
          !std::memcmp(mb.who,kPlayer,32) && mb.at[0]==-7.5f && mb.leftMs==90000,"a gun message round trip");
    m.leftMs=kMostLeftMs+1;
    Check(!Encode(m,gb,sizeof(gb)),"a gun message past the longest float is refused");
    Check(!Decode(bytes,sizeof(bytes),mb),"a grab is not a gun message");
}

void InboxChecks() {
    GrabInbox in;
    Check(in.Receive(1,MakeGrab(GrabKind::hold,7,1),1000)==GrabEvent::started,"a hold starts a grab");
    Check(in.Receive(1,MakeGrab(GrabKind::hold,7,1),1050)==GrabEvent::held,"a later hold carries it");
    Check(in.Holding(1,1500),"held while heard of");
    Check(in.Receive(1,MakeGrab(GrabKind::thrown,7,1),1100)==GrabEvent::thrown,"a throw ends it");
    Check(!in.Holding(1,1100),"thrown: no longer held");
    Check(in.Receive(1,MakeGrab(GrabKind::hold,7,1),1150)==GrabEvent::none,"a late hold of an ended grab is nothing");
    Check(in.Receive(1,MakeGrab(GrabKind::hold,7,2),1200)==GrabEvent::started,"the next grab starts");
    Check(in.Receive(1,MakeGrab(GrabKind::hold,7,1),1210)==GrabEvent::none,"an older grab of the same run is nothing");
    Check(in.Receive(1,MakeGrab(GrabKind::letGo,7,2),1250)==GrabEvent::letGo,"a let-go ends it");
    Check(in.Receive(1,MakeGrab(GrabKind::thrown,7,5),1300)==GrabEvent::thrown,"a throw of a grab never held still throws");
    Check(in.Receive(1,MakeGrab(GrabKind::letGo,7,6),1310)==GrabEvent::none,"a let-go of a grab never held is nothing");
    Check(in.Receive(1,MakeGrab(GrabKind::hold,7,8),2000)==GrabEvent::started && in.Holding(1,2000+kHoldSilentMs),"held up to its silence");
    Check(!in.Holding(1,2001+kHoldSilentMs),"a silent hold is let go");
    Check(in.Receive(1,MakeGrab(GrabKind::hold,9,1),3000)==GrabEvent::started,"a new run starts over (its serials anew)");
    Check(in.Receive(2,MakeGrab(GrabKind::hold,9,1),3000)==GrabEvent::started && in.Holding(1,3000) && in.Holding(2,3000),"peers apart");
    Check(in.Receive(0,MakeGrab(GrabKind::hold,9,1),3000)==GrabEvent::none && in.Receive(kMaxPeers+1,MakeGrab(GrabKind::hold,9,1),3000)==GrabEvent::none,
          "a peer index out of range is nothing");
    in.Reset();
    Check(!in.Holding(1,3000),"reset: nothing held");
}

// Version 2: a thrown grab's flight states, the radial (beside a hold), their bounds on the wire.
void FlightChecks() {
    GrabInbox in;
    Check(in.Receive(1,MakeGrab(GrabKind::flight,7,1),900)==GrabEvent::none,"a flight of a grab never thrown is nothing");
    Check(in.Receive(1,MakeGrab(GrabKind::hold,7,1),1000)==GrabEvent::started,"a hold");
    Check(in.Receive(1,MakeGrab(GrabKind::flight,7,1),1010)==GrabEvent::none,"a flight while still held is nothing");
    Check(in.Receive(1,MakeGrab(GrabKind::thrown,7,1),1100)==GrabEvent::thrown,"thrown");
    Check(in.Receive(1,MakeGrab(GrabKind::flight,7,1),1200)==GrabEvent::flight,"its flight's states follow");
    Check(in.Receive(1,MakeGrab(GrabKind::flight,7,2),1210)==GrabEvent::none,"a flight of another grab is nothing");
    Check(in.Receive(1,MakeGrab(GrabKind::hold,7,2),1300)==GrabEvent::started,"the next grab");
    Grab radial=MakeGrab(GrabKind::radial,7,2);radial.goal[0]=30.0f;radial.goal[1]=-25.0f;
    Check(in.Receive(1,radial,1310)==GrabEvent::radial && in.Holding(1,1310),"a radial beside a hold leaves the hold");
    Check(in.Receive(1,MakeGrab(GrabKind::letGo,7,2),1400)==GrabEvent::letGo,"its let-go");
    Check(in.Receive(1,MakeGrab(GrabKind::flight,7,2),1410)==GrabEvent::none,"a flight of a grab let go is nothing");

    unsigned char bytes[kGrabWire];Grab back{};
    Check(Encode(radial,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),back) && back.kind==GrabKind::radial &&
          back.goal[0]==30.0f && back.goal[1]==-25.0f,"a radial round trip");
    Grab far=radial;far.goal[0]=kMostRadial+1.0f;
    Check(!Encode(far,bytes,sizeof(bytes)),"a radial past its reach is refused");
    Grab fast=radial;fast.goal[1]=kMostRadialSpeed+1.0f;
    Check(!Encode(fast,bytes,sizeof(bytes)),"a radial past its speed is refused");
    Grab none=radial;none.goal[0]=0.0f;
    Check(!Encode(none,bytes,sizeof(bytes)),"a radial of no reach is refused");
    const Grab flight=MakeGrab(GrabKind::flight,7,1);
    Check(Encode(flight,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),back) && back.kind==GrabKind::flight,"a flight round trip");
}
}  // namespace

int main() {
    WireChecks();
    InboxChecks();
    FlightChecks();
    // Two grabs of one thing: the same winner seen from both machines.
    Check(OtherOutranks(false,true,"0002b","0002a") && !OtherOutranks(true,false,"0002a","0002b"),"the host's grab wins, from both sides");
    Check(OtherOutranks(false,false,"0002b","0002a") && !OtherOutranks(false,false,"0002a","0002b"),"between guests the lower PUID's, from both sides");
    Check(!OtherOutranks(false,false,"0002a","0002a") && !OtherOutranks(false,false,"","0002a"),"the same PUID or none: neither");
    std::printf("gravity_net: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
