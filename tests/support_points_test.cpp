// src/support_points_logic.h offline: the map's support pt charged from the team score as the stock pt reload takes it
// (EDF.dll 0x693EAC..0x693F1E: whole points, a frame's rise past 1500 cut to 1500 + the rest^0.8), the pool's start and
// most, spending, refunds and the online calls owed; the pool shared by a team (support_points_net.h): the host's pools,
// a guest's mirror and unconfirmed changes, the SPTS codec.
#include "../src/support_points_net.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}  // namespace

int main() {
    using namespace crew::support_points;
    // A frame's points: whole ones (cvttss2si truncates), the stock cut past 1500.
    Check(FramePoints(100.9f,0.0f)==100,"a rise is taken in whole points, truncated");
    Check(FramePoints(1500.0f,0.0f)==1500,"1500 is taken whole (jbe)");
    Check(FramePoints(1501.0f,0.0f)==1501,"1501: 1500 + 1^0.8");
    Check(FramePoints(2500.0f,0.0f)==1500+static_cast<int>(std::pow(1000.0f,0.8f)),"2500: 1500 + 1000^0.8 = 1751");
    Check(FramePoints(2500.0f,0.0f)==1751,"the cut, as a number");
    Check(FramePoints(50.0f,100.0f)==0,"a fall brings nothing (a new mission's score)");
    Check(FramePoints(100.0f,100.0f)==0,"no rise, nothing");
    Check(FramePoints(NAN,0.0f)==0 && FramePoints(INFINITY,0.0f)==0,"a score that is no number brings nothing");
    Check(FramePoints(3.0e9f,0.0f)==0,"a rise past any frame's is taken for garbage");

    Settings s;   // 6000 start, 30000 most, x1
    Pool p;p.Start(s);
    Check(p.points==6000,"a mission begins with SupportPtStart");
    Check(p.Take(0,12345.0f,s)==0 && p.points==6000,"the first read only sees the score (the stock weapon takes it when made)");
    Check(p.Take(0,12345.0f+800.0f,s)==800 && p.points==6800,"the next frame's rise comes in");
    Check(p.Take(1,99999.0f,s)==0 && p.points==6800,"another team (the player changed sides): only seen");
    Check(p.Take(1,99999.0f+2500.0f,s)==1751,"that team's rise, cut as the stock reload cuts it");
    Check(p.Take(7,1.0e6f,s)==0 && p.Take(-1,1.0e6f,s)==0 && p.team==1,"teams 0..6 only (the stock read's bound)");
    // The most: a pool never grows past it.
    float score=99999.0f+2500.0f;
    for(int i=0;i<100;++i){score+=1500.0f;p.Take(1,score,s);}
    Check(p.points==s.most,"the pool stops at SupportPtMost");
    Check(p.Take(1,score+100.0f,s)==0,"nothing comes in once full");
    // Spending and refunds.
    Check(p.Afford(30000) && !p.Afford(30001),"what it can pay");
    Check(p.Spend(29000) && p.points==1000,"a call paid");
    Check(!p.Spend(1001) && p.points==1000,"not enough: nothing taken");
    Check(p.Spend(0) && p.points==1000,"a free call takes nothing");
    p.Refund(29000,s);
    Check(p.points==s.most,"a refund comes back, up to the most");
    Check(p.spent==29000 && p.refunded==29000,"the mission's ledger for the log");
    // A start past the most is the most; a negative start none.
    {Settings t;t.start=50000;Pool q;q.Start(t);Check(q.points==t.most,"the start is kept under the most");
     t.start=-5;q.Start(t);Check(q.points==0,"a negative start is none");}
    // Prices.
    Check(Price(1800,1.0f)==1800 && Price(1800,0.5f)==900 && Price(1800,0.0f)==0,"a price scales, 0 frees");
    Check(Price(1801,0.5f)==901,"rounded to whole pt");
    Check(Price(15000,-1.0f)==0 && Price(15000,NAN)==0,"no scale that is no number");
    // The calls owed (online): settled once, by their request.
    Ledger l;
    Check(l.Keep(7,1800) && l.Keep(8,2500),"two requests owed");
    Check(l.Settle(8)==2500 && l.Settle(8)==0,"settled once");
    Check(l.Settle(99)==0,"a request never owed settles nothing (a second wave, the rescue)");
    Check(l.Keep(9,0) && l.Settle(9)==0,"a free call owes nothing");
    Check(l.Keep(0,500) && l.Settle(0)==0,"no request id: nothing kept");
    for(std::uint32_t r=100;r<100+kOwedMost;++r)l.Keep(r,10);
    Check(l.Settle(7)==0,"the oldest is forgotten when more are owed than kept (its pt stay spent)");
    Check(l.Settle(100+kOwedMost-1)==10,"the newest is kept");
    // ---- The pool shared by a team (support_points_net.h) ----
    {
        Settings h;   // the host's
        Pools pools;pools.Start(h);
        Check(pools.team[0].points==6000 && pools.team[6].points==6000,"every team begins with the start");
        Check(pools.Take(2,100.0f,h)==0 && pools.Take(2,600.0f,h)==500 && pools.team[2].points==6500 && pools.team[0].points==6000,
              "each team charged from its own score");
        Check(pools.Take(7,1.0f,h)==0 && pools.Take(-1,1.0f,h)==0,"teams 0..6 only");
        pools.Apply(2,-7000,h);
        Check(pools.team[2].points==0 && pools.team[2].spent==7000,"two machines spending at once: the pool stops at 0");
        pools.Apply(2,40000,h);
        Check(pools.team[2].points==h.most,"a refund stops at the most");

        // A guest: its change shown at once, confirmed by the host's state.
        Mirror g;g.Start(h,0xABCDu);
        Check(!g.heard && g.Shown(0)==6000,"before the host's word: the start");
        Msg c;
        Check(g.Keep(0,-2500,10,&c) && c.kind==Kind::change && c.serial==1 && c.origin==0xABCDu && c.team==0 && c.pt==-2500,
              "a change kept and its message");
        Check(g.Shown(0)==3500 && g.Shown(1)==6000,"a call paid shows at once, on its team only");
        unsigned char bytes[kWire];
        Msg d;
        Check(Encode(c,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),d) && d.kind==Kind::change && d.origin==c.origin &&
              d.serial==1 && d.team==0 && d.pt==-2500,"a change round-trips");
        Peer peer;
        Check(peer.Next(d),"the host takes the guest's first change");
        Check(!peer.Next(d),"the same change again (its confirmation lost): not taken twice");
        Pools hp;hp.Start(h);hp.Apply(d.team,d.pt,h);
        Msg st;st.kind=Kind::state;st.most=h.most;st.scale=h.scale;st.origin=peer.origin;st.serial=peer.taken;
        for(int t=0;t<kTeams;++t)st.points[t]=hp.team[t].points;
        Msg e;
        Check(Encode(st,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),e) && e.kind==Kind::state && e.points[0]==3500 &&
              e.serial==1 && e.most==h.most,"a state round-trips");
        // A second change before the host's state that confirms the first arrives.
        Check(g.Keep(0,-1000,20,&c) && c.serial==2 && g.Shown(0)==2500,"a second call paid");
        g.Hear(e);
        Check(g.heard && g.count==1 && g.Shown(0)==2500,"the first confirmed (in the host's pool now), the second still shown");
        // A state with no confirmation for this run (another run's, or none) drops nothing.
        Msg other=e;other.origin=0x1234u;other.serial=9;
        g.Hear(other);
        Check(g.count==1,"another run's confirmation drops none of this run's changes");
        // The host misses change 2 and gets change 3 first: out of order, not taken; then 2 and 3 again, in order.
        Msg c3;Check(g.Keep(0,500,30,&c3) && c3.serial==3,"a refund kept as a change too");
        Msg c2=g.ChangeMsg(g.changes[0]);
        Check(!peer.Next(c3),"a change out of order: not taken");
        Check(peer.Next(c2) && peer.Next(c3) && peer.taken==3,"the guest's changes sent again in order: each taken once");
        // A host that has not seen this run (a new host, a new mission's table) begins at the first change that comes.
        Peer fresh;Check(fresh.Next(c2) && fresh.taken==2,"a run not seen yet begins at its oldest unconfirmed change");
        // The guest's list has room for kChangesMost unconfirmed.
        Mirror full;full.Start(h,1u);Msg x;
        for(int i=0;i<kChangesMost;++i)full.Keep(1,-1,0,&x);
        Check(!full.Keep(1,-1,0,&x) && full.count==kChangesMost,"no room: nothing kept (the call is not paid)");
        Check(full.Shown(1)==6000-kChangesMost,"what it kept is shown");
        Check(!full.Keep(7,-1,0,&x) && !full.Keep(1,0,0,&x),"a team out of range or no pt: nothing kept");
        // The codec's bounds.
        Msg bad=st;bad.points[3]=h.most+1;
        Check(!Encode(bad,bytes,sizeof(bytes)),"a pool past the most is not sent");
        Check(Encode(st,bytes,sizeof(bytes)),"the state encodes again");
        bytes[4]=2;
        Check(!Decode(bytes,sizeof(bytes),e),"another version is not read");
        Check(Encode(st,bytes,sizeof(bytes)) && !Decode(bytes,sizeof(bytes)-1,e),"the wrong size is not read");
        Msg zero=c;zero.pt=0;
        Check(!Encode(zero,bytes,sizeof(bytes)),"a change of no pt is not sent");
        Msg team=c;team.team=7;
        Check(!Encode(team,bytes,sizeof(bytes)),"a change of a team past 6 is not sent");
    }
    std::printf("support points: %d checks passed\n",checks);
    return 0;
}
