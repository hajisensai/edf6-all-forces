// The NPC soldiers' own dashes (src/npc_mobility.h, docs/npc-mobility-re.md) checked offline: the Wing Diver's gates,
// energy budget and reasons to dash; the Fencer's hands (SecondaryFire_Type and the swap flags), the choice of
// dash-jump / dash / boost / nothing by distance, rise, walls, uses left and the reserve, the dash-jump's timing (the
// boost inside the dash's carry window, never one set two frames running), the misses that turn it off.
//   cmake --build build --target npc_mobility_check && build\npc_mobility_check.exe      (exit code 1 on a failure)
#include "../src/npc_logic.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
using namespace npc::mobility;

void WingBudgets() {
    // Tank 100, a dash 20: keep half the tank and one dash.
    Check(WingBudget(80.0f,100.0f,20.0f,0.5f),"80 - 20 = 60 keeps 50");
    Check(WingBudget(70.0f,100.0f,20.0f,0.5f),"70 - 20 = 50 keeps 50 (exactly)");
    Check(!WingBudget(69.0f,100.0f,20.0f,0.5f),"69 - 20 = 49 is under half");
    // A dash dearer than the reserve: one more dash kept.
    Check(WingBudget(60.0f,100.0f,30.0f,0.1f) && !WingBudget(59.0f,100.0f,30.0f,0.1f),"reserve under a dash: keep a dash");
    // Never the stock's overheat (cost over what is left) nor an empty tank (cost == energy).
    for(float e=0.0f;e<=100.0f;e+=0.5f)
        for(float r=0.0f;r<=1.0f;r+=0.1f)
            if(WingBudget(e,100.0f,25.0f,r))Check(e>25.0f && e-25.0f>=25.0f,"a budgeted dash never empties nor overheats",e,r);
    Check(!WingBudget(100.0f,100.0f,0.0f,0.5f),"no cost read: no dash");
    Check(!WingBudget(100.0f,0.0f,10.0f,0.5f),"no tank read: no dash");
    Check(!WingBudget(100.0f,100.0f,120.0f,0.0f),"a dash over the whole tank: never");
    Check(!WingBudget(std::nanf(""),100.0f,10.0f,0.5f) && !WingBudget(90.0f,100.0f,std::nanf(""),0.5f),"not finite: no dash");
    Check(WingReserve(0.5f,true)==kAirReserve && WingReserve(0.5f,false)==0.5f && WingReserve(0.8f,true)==0.8f,
          "in the air the reserve is at least 0.6");
    WingState s{true,false,false,false,false,false,80.0f,100.0f,20.0f};
    Check(WingReady(s,0.5f),"all gates open, the budget kept: ready");
    s.air=true;s.energy=79.0f;
    Check(!WingReady(s,0.5f),"in the air 79 - 20 is under the 0.6 kept",WingReserve(0.5f,true));
    s.energy=81.0f;
    Check(WingReady(s,0.5f),"in the air 81 - 20 keeps 0.6");
    s.air=false;s.energy=80.0f;
    WingState g=s;g.permitted=false;Check(!WingReady(g,0.5f),"mask bit 10 off: no dash");
    g=s;g.busy=true;Check(!WingReady(g,0.5f),"an occupying action: no dash");
    g=s;g.downed=true;Check(!WingReady(g,0.5f),"downed / grabbed: no dash");
    g=s;g.overheated=true;Check(!WingReady(g,0.5f),"overheated: no dash");
    g=s;g.locked=true;Check(!WingReady(g,0.5f),"a weapon holds the lock (reload): no dash");
    Check(WingLocked(true,0) && WingLocked(true,1),"a holder at +0x1A28: locked");
    Check(WingLocked(false,2) && WingLocked(false,3),"no holder, level +0x1A30 >= 2 (0x590A10(h,0,2)): 0x590D70(h,2) refuses");
    Check(!WingLocked(false,0) && !WingLocked(false,1),"no holder, level under 2: free");
}

void WingReasons() {
    const float range=25.0f;
    WingGoal g{true,false,false,0.0f,0.0f,0.0f,0.0f};
    Check(WingWhy(g,range)==Why::evade,"pressed by a crowd: dash away (whatever the way)");
    g=WingGoal{false,true,false,30.0f,0.0f,40.0f,20.0f};
    Check(WingWhy(g,range)==Why::goal,"a fight's goal 30 m off, clear: dash");
    g.goalDist=20.0f;Check(WingWhy(g,range)==Why::none,"20 m: walk (the dash overshoots)");
    g.goalDist=30.0f;g.threat=true;g.threatDot=0.9f;
    Check(WingWhy(g,range)==Why::none,"never towards the nearest enemy");
    g.threatDot=0.2f;Check(WingWhy(g,range)==Why::goal,"across or away from it: dash");
    g.threat=false;g.clear=10.0f;Check(WingWhy(g,range)==Why::none,"a wall 10 m ahead: no dash");
    g.clear=40.0f;g.wayDist=5.0f;Check(WingWhy(g,range)==Why::none,"the route turns 5 m ahead: no dash");
    g=WingGoal{false,false,false,40.0f,0.0f,40.0f,20.0f};
    Check(WingWhy(g,range)==Why::none,"keeping up 40 m behind: walk");
    g.goalDist=60.0f;Check(WingWhy(g,range)==Why::follow,"keeping up 60 m behind: dash");
    Check(WingWhy(g,0.0f)==Why::none,"no range: no dash");
    Check(Rested(1500,1.5f) && !Rested(1499,1.5f) && Rested(0,0.0f) && !Rested(100000,std::nanf("")),"the cooldown");
}

void FencerHands() {
    Check(Effective(5,false,false)==Kind::dash,"type 5 (hammer, spear): dash");
    Check(Effective(4,false,false)==Kind::boost,"type 4 (gatling, cannon shot, blade): boost");
    Check(Effective(5,true,false)==Kind::boost,"307 (+0x2082): the dash a boost");
    Check(Effective(4,false,true)==Kind::dash,"306 (+0x2083): the boost a dash");
    Check(Effective(4,true,false)==Kind::boost && Effective(5,false,true)==Kind::dash,"each flag swaps only its own kind");
    Check(Effective(5,true,true)==Kind::boost && Effective(4,true,true)==Kind::dash,"both: swapped both ways (0x5672B1 tests the sent kind)");
    const int others[]={0,1,2,3,6,7,-1};
    for(int t:others)Check(Effective(t,true,true)==Kind::none,"no move for other types (6: the shield)",t);
    Ability a=AbilityOf(Kind::dash,Kind::none);
    Check(a.dashSet==0 && a.boostSet<0 && Any(a),"the stock SP: dash in set 0, the shield in set 1");
    a=AbilityOf(Kind::boost,Kind::dash);
    Check(a.dashSet==1 && a.boostSet==0,"a boost hand and a dash hand");
    a=AbilityOf(Kind::dash,Kind::dash);
    Check(a.dashSet==0 && a.boostSet<0,"two dash hands: the first");
    Check(!Any(AbilityOf(Kind::none,Kind::none)),"no move at all");
}

Situation Open(float goal) {
    Situation s{};
    s.goalDist=goal;s.wayDist=40.0f;s.rise=0.0f;s.clear=40.0f;s.landed=true;s.dashLeft=2;s.boostLeft=2;
    return s;
}

void FencerChoices() {
    const Ranges r{45.0f,15.0f,1};
    const Ability both{0,1},dashOnly{0,-1},boostOnly{-1,0},none{};
    Check(Choose(both,Open(60.0f),r)==Act::dashJump,"60 m, clear, both hands: dash-jump");
    Check(Choose(both,Open(30.0f),r)==Act::dash,"30 m: dash only");
    Check(Choose(both,Open(10.0f),r)==Act::none,"10 m: walk");
    Check(Choose(dashOnly,Open(60.0f),r)==Act::dash,"60 m, no boost: dash");
    Check(Choose(boostOnly,Open(60.0f),r)==Act::none,"60 m, only a boost, flat and open: walk (no jump for nothing)");
    Check(Choose(none,Open(60.0f),r)==Act::none,"no move: nothing");
    Situation s=Open(60.0f);s.clear=20.0f;
    Check(Choose(both,s,r)==Act::dash,"60 m but 20 m clear: dash, no dash-jump");
    s.clear=10.0f;Check(Choose(both,s,r)==Act::none,"10 m clear: neither");
    s=Open(60.0f);s.boostLeft=0;Check(Choose(both,s,r)==Act::dash,"no boost left: dash");
    s=Open(60.0f);s.landed=false;Check(Choose(both,s,r)==Act::none,"in the air: nothing (the uses refill on the ground)");
    s=Open(60.0f);s.dashBusy=true;Check(Choose(both,s,r)==Act::none,"dashing, not free to chain: nothing");
    // The reserve: an enemy near keeps one dash for dodging.
    s=Open(30.0f);s.threatNear=true;s.dashLeft=1;Check(Choose(both,s,r)==Act::none,"an enemy near, one dash left: kept");
    s.dashLeft=2;Check(Choose(both,s,r)==Act::dash,"an enemy near, two left: one may go");
    s.threatNear=false;s.dashLeft=1;Check(Choose(both,s,r)==Act::dash,"no enemy near: the last one goes");
    s.dashLeft=0;Check(Choose(both,s,r)==Act::none,"none left: nothing");
    // Dodging spends the reserve.
    s=Open(0.0f);s.evade=true;s.threatNear=true;s.dashLeft=1;
    Check(Choose(both,s,r)==Act::dash,"pressed: dash out on the last one");
    s.dashLeft=0;Check(Choose(both,s,r)==Act::none,"pressed, none left: back off on foot");
    Check(Choose(boostOnly,s,r)==Act::none,"pressed, only a boost: back off on foot");
    // The way up: a boost.
    s=Open(20.0f);s.rise=4.0f;s.wayDist=6.0f;Check(Choose(both,s,r)==Act::boost,"a ledge 4 m up 6 m off: boost");
    s.wayDist=20.0f;Check(Choose(both,s,r)==Act::dash,"the rise 20 m off: not yet");
    s.rise=3.0f;s.wayDist=10.0f;Check(Choose(both,s,r)==Act::dash,"a ramp 3 m up over 10 m: walked, no boost");
    s=Open(20.0f);s.clear=2.0f;Check(Choose(both,s,r)==Act::boost,"a wall at the waist 2 m ahead: boost over");
    s=Open(20.0f);s.stuck=true;Check(Choose(both,s,r)==Act::boost,"stuck: boost");
    s.boostLeft=0;Check(Choose(both,s,r)==Act::none,"stuck, no boost left: nothing (never a dash into the wall)");
    s=Open(20.0f);s.stuck=true;s.landed=false;Check(Choose(both,s,r)==Act::none,"stuck in the air: nothing");
    s=Open(20.0f);s.stuck=true;Check(Choose(dashOnly,s,r)==Act::none,"stuck with only a dash: nothing");
    s=Open(std::nanf(""));Check(Choose(both,s,r)==Act::none,"no goal: nothing");
}

void Timing() {
    const Ability both{0,1};
    // Dash pressed at frame 100, taken (seen) from 101; boost in frame 102 (k = 2), taken at 103.
    Combo c;
    Check(Begin(c,Act::dashJump,both,100)==0 && Busy(c),"the dash-jump begins with the dash set");
    Step s=Advance(c,101,true,false);
    Check(s.press<0 && s.outcome==Outcome::pending,"k = 1: the dash seen, no boost yet");
    s=Advance(c,102,true,false);
    Check(s.press==1 && s.outcome==Outcome::pending,"k = 2: the boost set pressed",s.press);
    Check(s.press!=0,"the boost is never the dash's set");
    s=Advance(c,103,true,true);
    Check(s.outcome==Outcome::done && !Busy(c),"the boost taken: done");
    // Every frame the boost can go in lies in the carry window, whenever the dash is seen.
    for(int late=1;late<=kConfirmFrames;++late) {
        Combo d;Begin(d,Act::dashJump,both,200);
        int at=-1;
        for(int k=1;k<=10 && Busy(d);++k) {
            const Step t=Advance(d,200+static_cast<std::uint64_t>(k),k>=late,at>=0 && k>at);
            if(t.press==1)at=k;
        }
        Check(at>=kBoostFirst && at<=kBoostLast && at<=kCarryLast && 60.0f*std::pow(0.95f,static_cast<float>(at))>=42.0f,
              "the boost while the dash carries over 42",late,at);
    }
    // The dash never taken: a miss after kConfirmFrames.
    Combo m;Begin(m,Act::dash,both,300);
    Outcome o=Outcome::pending;
    for(std::uint64_t f=301;f<=310 && Busy(m);++f)o=Advance(m,f,false,false).outcome;
    Check(o==Outcome::missed,"a dash not taken is a miss");
    // A dash-jump whose boost is refused: a miss.
    Combo b;Begin(b,Act::dashJump,both,400);
    o=Outcome::pending;
    for(std::uint64_t f=401;f<=420 && Busy(b);++f)o=Advance(b,f,true,false).outcome;
    Check(o==Outcome::missed,"a boost not taken is a miss");
    // Boost alone.
    Combo u;
    Check(Begin(u,Act::boost,both,500)==1,"a boost presses the boost set");
    Check(Advance(u,501,false,true).outcome==Outcome::done,"taken next frame: done");
    // Never one set two frames running.
    Combo r;
    Check(Begin(r,Act::dash,both,600)==0 && Advance(r,601,true,false).outcome==Outcome::done,"a dash done next frame");
    Check(Begin(r,Act::dash,both,601)<0,"the same set the next frame: refused");
    Check(Begin(r,Act::boost,both,601)==1,"the other set the next frame: fine");
    Combo q;
    Check(Begin(q,Act::dashJump,Ability{0,0},700)<0 && !Busy(q),"a dash-jump needs two sets");
    Check(Begin(q,Act::dashJump,Ability{0,-1},700)<0,"a dash-jump needs a boost set");
    Check(Begin(q,Act::boost,Ability{0,-1},700)<0,"a boost needs a boost set");
    Check(Begin(q,Act::dash,Ability{-1,0},700)<0,"a dash needs a dash set");
    Combo z;Check(Begin(z,Act::dash,both,0)==0,"frame 0 is pressable");
}

void Misses() {
    int n=0;
    bool off=false;
    for(int i=0;i<kMissOff-1;++i)off=CountMiss(n,Outcome::missed);
    Check(!off && n==kMissOff-1,"four misses: still on");
    CountMiss(n,Outcome::done);
    Check(n==0,"a dash taken clears the count");
    for(int i=0;i<kMissOff;++i)off=CountMiss(n,Outcome::missed);
    Check(off,"five in a row: off");
    CountMiss(n,Outcome::pending);
    Check(n==kMissOff,"pending changes nothing");
    Check(Stuck(0.5f,1500) && !Stuck(0.5f,1499) && !Stuck(2.0f,3000),"stuck: 1.5 s and under 1 m");
}
}  // namespace

int main() {
    WingBudgets();
    WingReasons();
    FencerHands();
    FencerChoices();
    Timing();
    Misses();
    std::printf("npc_mobility_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
