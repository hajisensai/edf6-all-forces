// The NPC soldiers' own dashes (src/npc_mobility.h, docs/npc-mobility-re.md) checked offline: the Wing Diver's gates,
// energy budget, the route's straight run on routes ground_navigation.h's Navigate searches and the reasons to dash (the 2026-11-21 drive.py
// walks: falling back 37 m while firing, to its combat spot, keeping up); the Fencer's hands (SecondaryFire_Type and the swap flags), the choice of
// dash-jump / dash / boost / nothing by distance, rise, walls, uses left and the reserve, the dash-jump's timing (the
// boost inside the dash's carry window, never one set two frames running), the misses that turn it off.
//   cmake --build build --target npc_mobility_check && build\npc_mobility_check.exe      (exit code 1 on a failure)
#include "../src/npc_logic.h"
#include "../src/ground_navigation.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

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

// A route as the soldiers walk it: ground_navigation.h's own Navigate (the cells placed from `from`, its tie rule among
// equally short walks, MoveTo's rolling horizon) on flat open ground, searched until it has one; what MoveTo hands
// StraightRun (the path from its cursor on). nullptr when no route came.
const npc::navigation::Point* Route(npc::navigation::State& s,const float* from,const float* to,int* n) {
    using namespace npc::navigation;
    Profile p;p.horizon=48.0f;   // npcai.cpp SoldierRoute (kRouteHorizon)
    const Point a{from[0],0.0f,from[2]},b{to[0],0.0f,to[2]};
    Point next{};std::uint64_t ms=1;Result r=Result::pending;
    const auto flat=[](Point,Point q,Point& out) noexcept {out=q;out.y=0.0f;return Edge::open;};
    for(int i=0;i<200000 && r==Result::pending;++i){ms+=16;r=Navigate(s,a,b,0.2f,ms,next,flat,p);}
    *n=r==Result::moving && s.cursor<s.length ? s.length-s.cursor : 0;
    return *n ? s.path+s.cursor : nullptr;
}

void WingRuns() {
    auto nav=std::make_unique<npc::navigation::State>();
    float dir[3]={0.0f,0.0f,0.0f};
    int n=0;
    // run10 15:16:00-04: falling back from (-271,2,208) to (-245,15,234) (the soldier's walk, 45 deg). Its next waypoint
    // is the next 2 m cell (the old "route turns within 8 m" refused every walk on that), its run the whole look.
    const float from[3]={-271.0f,2.0f,208.0f},back[3]={-245.0f,15.0f,234.0f};
    const npc::navigation::Point* path=Route(*nav,from,back,&n);
    Check(n>0,"the fall back has a route",n);
    if(n>0) {
        const float first=std::hypot(path[0].x-from[0],path[0].z-from[2]);
        Check(first<3.0f,"the next waypoint is the next 2 m cell",first);
        const float run=StraightRun(from,path,n,kRunLook,kRunSlack,dir);
        Check(run==kRunLook,"the fall back's 37 m diagonal: straight as far as it looks (30 m)",run);
        Check(dir[0]>0.6f && dir[2]>0.6f && dir[1]==0.0f,"its way: along the walk (north-east)",dir[0],dir[2]);
    }
    // 40 m open walks at bearings off the grid's axes and diagonals (15 deg: 5 diagonal cells among 14 straight ones,
    // in the order the search's tie rule puts them), from a point off the cells: the next waypoint within a cell, a run a
    // dash fits along, its way within 20 deg of the walk's.
    const float o[3]={0.3f,0.0f,0.4f};
    const float bearings[]={0.0f,15.0f,30.0f,45.0f,60.0f,75.0f,105.0f,165.0f,200.0f,250.0f,340.0f};
    for(float deg:bearings) {
        const float r=deg*3.14159265f/180.0f,c=std::cos(r),sn=std::sin(r);
        const float to[3]={o[0]+40.0f*c,0.0f,o[2]+40.0f*sn};
        path=Route(*nav,o,to,&n);
        Check(n>0,"an open walk has a route",deg);
        if(!n)continue;
        const float first=std::hypot(path[0].x-o[0],path[0].z-o[2]);
        Check(first<3.0f,"the next waypoint within a cell",deg,first);
        const float run=StraightRun(o,path,n,kRunLook,kRunSlack,dir);
        Check(run>=kWingRun,"an open 40 m walk: a straight run the dash fits",deg,run);
        Check(dir[0]*c+dir[2]*sn>=std::cos(20.0f*3.14159265f/180.0f),"the run's way along the walk's",deg,dir[0]*c+dir[2]*sn);
    }
    // To its combat spot 30 m east, 10 m north (18 deg).
    const float spot[3]={30.0f,0.0f,10.0f};
    path=Route(*nav,o,spot,&n);
    if(n>0) {
        const float run=StraightRun(o,path,n,kRunLook,kRunSlack,dir);
        Check(run>=kWingRun && dir[0]>0.9f,"30 m east 10 m north: one straight run east-north-east",run,dir[0]);
    } else Check(false,"the combat spot has a route");
    // Round a wall across the way (x 8..10, |z| < 6) to a point 30 m east: the route turns off the line to pass it, and
    // the run either stops short of the dash or keeps off the wall (the line through it is not the walk's).
    {
        using namespace npc::navigation;
        auto s=std::make_unique<State>();
        Profile p;p.horizon=48.0f;
        const auto walled=[](Point a,Point b,Point& out) noexcept {
            for(int i=0;i<=40;++i) {
                const float t=static_cast<float>(i)/40.0f,x=a.x+(b.x-a.x)*t,z=a.z+(b.z-a.z)*t;
                if(x>=7.3f && x<=10.7f && std::fabs(z)<6.7f)return Edge::blocked;   // the wall and the soldier's 0.65 m
            }
            out=b;out.y=0.0f;return Edge::open;
        };
        Point next{};std::uint64_t ms=1;Result r=Result::pending;
        for(int i=0;i<200000 && r==Result::pending;++i){ms+=16;r=Navigate(*s,Point{o[0],0.0f,o[2]},Point{30.0f,0.0f,0.0f},0.2f,ms,next,walled,p);}
        const int m=r==Result::moving ? s->length-s->cursor : 0;
        Check(m>0,"round the wall: a route",m);
        if(m>0) {
            const float run=StraightRun(o,s->path+s->cursor,m,kRunLook,kRunSlack,dir);
            bool crosses=false;
            for(int i=0;i<=60;++i) {
                const float t=static_cast<float>(i)/60.0f,x=o[0]+dir[0]*run*t,z=o[2]+dir[2]*run*t;
                if(x>=8.0f && x<=10.0f && std::fabs(z)<6.0f)crosses=true;
            }
            Check(run<kWingRun || !crosses,"round the wall: the run never through it",run,dir[2]);
        }
    }
    // Round a corner 6 m ahead: 3 cells east, then 20 m north.
    struct Pt { float x,y,z; };
    Pt hand[64];
    int k=0;
    for(int i=1;i<=3;++i)hand[k++]=Pt{2.0f*i,0.0f,0.0f};
    for(int i=1;i<=10;++i)hand[k++]=Pt{6.0f,0.0f,2.0f*i};
    const float zero[3]={0.0f,0.0f,0.0f};
    float run=StraightRun(zero,hand,k,kRunLook,kRunSlack,dir);
    Check(run<kWingRun,"a corner 6 m ahead: too short a run to dash",run);
    // A corner 14 m ahead (a run shorter than the dash carries): the dash would leave the walk past it.
    k=0;
    for(int i=1;i<=7;++i)hand[k++]=Pt{2.0f*i,0.0f,0.0f};
    for(int i=1;i<=10;++i)hand[k++]=Pt{14.0f,0.0f,2.0f*i};
    run=StraightRun(zero,hand,k,kRunLook,kRunSlack,dir);
    Check(run<kWingRun,"a corner 14 m ahead: under the dash's carry, no dash",run);
    Check(StraightRun(zero,hand,0,kRunLook,kRunSlack,dir)==0.0f,"no route: no run");
    // A zig-zag of cells (the grid's own walk along 1:2) stays one straight run.
    k=0;
    for(int i=1;i<=16;++i)hand[k++]=Pt{2.0f*i,0.0f,2.0f*((i+1)/2)};
    run=StraightRun(zero,hand,k,kRunLook,kRunSlack,dir);
    Check(run>=kWingRun,"a 1:2 zig-zag of cells: straight",run);
}

void WingReasons() {
    const float range=25.0f;
    // WingGoal: pressed, need, paced, firing, threat, left, run, threatDot, clear.
    WingGoal g{true,Need::fight,false,true,false,0.0f,0.0f,0.0f,0.0f};
    Check(WingDecide(g,range).why==Why::evade,"pressed by a crowd: dash away (whatever the way, firing or not)");
    g.paced=true;Check(WingDecide(g,range).why==Why::evade,"pressed on a paced march: dash away still");
    // run10: falling back 37 m, a laser's trigger held all the way (fire=1): the falling back outranks the shot.
    g=WingGoal{false,Need::retreat,false,true,false,37.0f,30.0f,-1.0f,kWingClear};
    Check(WingDecide(g,range).why==Why::goal,"falling back 37 m while firing: dash");
    g.left=20.0f;Check(WingDecide(g,range).block==Block::tooNear,"falling back 20 m: walk (the dash overshoots)");
    g.left=37.0f;g.threat=true;g.threatDot=0.9f;
    Check(WingDecide(g,range).block==Block::enemyAhead,"falling back towards the nearest enemy: no dash (only an order may)");
    g.threatDot=-0.5f;Check(WingDecide(g,range).why==Why::goal,"falling back away from it: dash");
    // The player's move order.
    g=WingGoal{false,Need::order,false,true,true,37.0f,30.0f,0.9f,kWingClear};
    Check(WingDecide(g,range).why==Why::goal,"the player's order, firing, towards the enemy: dash");
    // A paced march (the squad at its top's share): the dash would break out of it.
    g.paced=true;Check(WingDecide(g,range).block==Block::paced,"a move order's paced march: walk with the squad");
    g=WingGoal{false,Need::keepUp,true,false,false,60.0f,30.0f,-1.0f,kWingClear};
    Check(WingDecide(g,range).block==Block::paced,"keeping its place in a paced march: walk");
    // To its combat spot (a fight's walk), 30 m.
    g=WingGoal{false,Need::fight,false,false,false,30.0f,30.0f,-1.0f,kWingClear};
    Check(WingDecide(g,range).why==Why::goal,"to its combat spot 30 m, not firing (between two shots): dash");
    g.firing=true;Check(WingDecide(g,range).block==Block::firing,"to its combat spot 30 m, a shot going out: the shot first");
    g.left=55.0f;Check(WingDecide(g,range).why==Why::goal,"55 m out of place: the walk first, firing or not");
    g.left=30.0f;g.firing=false;g.threat=true;g.threatDot=0.9f;
    Check(WingDecide(g,range).block==Block::enemyAhead,"never towards the nearest enemy on its own");
    g.threatDot=0.2f;Check(WingDecide(g,range).why==Why::goal,"across or away from it: dash");
    g.need=Need::order;g.threatDot=0.9f;Check(WingDecide(g,range).why==Why::goal,"the player's order: towards it too");
    g=WingGoal{false,Need::fight,false,false,false,30.0f,30.0f,-1.0f,10.0f};
    Check(WingDecide(g,range).block==Block::wall,"a wall 10 m ahead: no dash");
    g.clear=kWingClear-1.0f;Check(WingDecide(g,range).block==Block::wall,"free for less than the dash carries: no dash");
    g.clear=kWingClear;g.run=6.0f;Check(WingDecide(g,range).block==Block::turn,"the route turns 6 m ahead: no dash");
    g.run=kWingRun-1.0f;Check(WingDecide(g,range).block==Block::turn,"a run shorter than the dash carries: no dash");
    g.run=kWingRun;Check(WingDecide(g,range).why==Why::goal,"a run as long as the dash carries: dash");
    Check(kWingRun>=17.0f && kWingClear>=17.0f,"the run and the free way at least the ~17 m the dash carries");
    // Keeping up (a formation behind its player, its post): twice the range.
    g=WingGoal{false,Need::keepUp,false,false,false,40.0f,30.0f,-1.0f,kWingClear};
    Check(WingDecide(g,range).block==Block::tooNear,"keeping up 40 m behind: walk");
    g.left=60.0f;Check(WingDecide(g,range).why==Why::follow,"keeping up 60 m behind: dash");
    g.firing=true;Check(WingDecide(g,range).why==Why::follow,"60 m is past twice the range: a shot does not hold it");
    Check(WingFar(Need::keepUp,range)==50.0f && WingFar(Need::fight,range)==25.0f && WingFar(Need::order,range)==25.0f &&
          WingFar(Need::retreat,range)==25.0f,"how far");
    Check(WingDecide(g,0.0f).why==Why::none,"no range: no dash");
    g.left=std::nanf("");Check(WingDecide(g,range).why==Why::none,"not finite: no dash");
    Check(Rested(1500,1.5f) && !Rested(1499,1.5f) && Rested(0,0.0f) && !Rested(100000,std::nanf("")),"the cooldown");
    // The gates named for the log, in 0x580B06's order, the budget last.
    WingState s{true,false,false,false,false,false,80.0f,100.0f,20.0f};
    Check(WingGate(s,0.5f)==nullptr,"all open: no gate");
    WingState t=s;t.locked=true;Check(WingGate(t,0.5f) && WingGate(t,0.5f)[0]=='w',"a reload: weapon lock");
    t=s;t.air=true;t.energy=79.0f;Check(WingGate(t,0.5f) && std::string(WingGate(t,0.5f))=="energy (air)","in the air short: energy (air)");
    t=s;t.permitted=false;t.overheated=true;Check(std::string(WingGate(t,0.5f))=="not permitted","the first refusing gate");
    Check(std::string(BlockName(Block::firing))=="firing" && std::string(NeedName(Need::order))=="order" &&
          std::string(NeedName(Need::retreat))=="retreat" && std::string(BlockName(Block::paced))=="paced march","the log's names");
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
    WingRuns();
    WingReasons();
    FencerHands();
    FencerChoices();
    Timing();
    Misses();
    std::printf("npc_mobility_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
