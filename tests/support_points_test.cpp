// src/support_points_logic.h offline: the map's support pt charged from the team score as the stock pt reload takes it
// (EDF.dll 0x693EAC..0x693F1E: whole points, a frame's rise past 1500 cut to 1500 + the rest^0.8), the pool's start and
// most, spending, refunds and the online calls owed.
#include "../src/support_points_logic.h"
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
    std::printf("support points: %d checks passed\n",checks);
    return 0;
}
