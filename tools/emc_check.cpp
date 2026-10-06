// The EMC's charged beam without the game (src/emc_plan.h as the plugin uses it; docs/emc-re.md):
//  - the trigger's cycle: a held trigger fires on the frame the charge time ends, the beam lasts its seconds, a charge
//    let go drains and fires nothing, a trigger held through a beam and its rearm does not start the next, no rounds no charge;
//  - the damage budget: a beam spends a stock burst's rounds and carries their damage, every round its share, the
//    magazine's total the stock one's at every request tier (EMC / EMCS / EMCX), a last short burst its own rounds;
//  - the sweep: a stand-in line of buildings and the ground (boxes along the beam, a hill's face; a ray started inside
//    a box does not meet it, as Havok's), the plugin's scan and break charges stepped frame by frame: when the beam sees
//    the ground behind them, when the last of them is down, and with their collision kept for a while after they fall;
//  - the beam turned while it fires: the break charges (a 12 m blast every 0.1 s) leave no gap at its far end up to a
//    turn rate the check prints.
//
//   cmake --build build --target emc_check && build\emc_check.exe        exit code 0: every check passed
#include "../src/emc_plan.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
using namespace crew::emc;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

// The trigger held for `holdFrames` frames, then let go for `restFrames`: the frames (from 1) the events fell on.
struct Run { int start=-1,fire=-1,end=-1,cancel=-1,fires=0; float chargeAfter=0.0f; };
Run Trigger(const Tuning& t,int holdFrames,int restFrames,bool loaded=true) {
    State s{};Run r{};
    for(int f=1;f<=holdFrames+restFrames;++f) {
        const Event e=Step(s,t,f<=holdFrames,loaded,kFrameSec);
        if(e==Event::start && r.start<0)r.start=f;
        if(e==Event::fire){if(r.fire<0)r.fire=f;++r.fires;}
        if(e==Event::end && r.end<0)r.end=f;
        if(e==Event::cancel && r.cancel<0)r.cancel=f;
    }
    r.chargeAfter=s.charge;
    return r;
}

void CycleChecks() {
    const Tuning t{3.0f,2.5f};
    const Run full=Trigger(t,1200,0);
    char line[200];
    std::snprintf(line,sizeof(line),"held 20 s: charge from frame %d, fire on frame %d (3 s = 180), beam over on frame %d (+150), %d beam(s)",full.start,
                  full.fire,full.end,full.fires);
    Check(full.start==1 && full.fire==180 && full.end==330 && full.fires==1,line);
    const Run again=Trigger(t,300,120+120);
    State s{};
    int fires=0,second=-1;
    for(int f=1;f<=1000;++f) {   // held 300 frames (through the beam), let go 10, held again
        const bool held=f<=300 || f>310;
        if(Step(s,t,held,true,kFrameSec)==Event::fire && ++fires==2)second=f;
    }
    std::snprintf(line,sizeof(line),"let go and pressed again after the rearm: the second beam on frame %d (rearm ends 450, +180 = 630)",second);
    Check(again.fires==1 && second==630,line);
    const Run tap=Trigger(t,90,60);
    std::snprintf(line,sizeof(line),"let go at half: no beam, cancelled on frame %d, the charge drained to %.2f in 1 s (drains in 0.75 s)",tap.cancel,tap.chargeAfter);
    Check(tap.fires==0 && tap.cancel==91 && tap.chargeAfter==0.0f,line);
    const Run dry=Trigger(t,600,0,false);
    Check(dry.fires==0 && dry.start<0,"no rounds: the trigger charges nothing");
    const Tuning fast{0.5f,0.5f};
    const Run quick=Trigger(fast,200,0);
    std::snprintf(line,sizeof(line),"the shortest settings (0.5 s / 0.5 s): fire on frame %d, over on frame %d",quick.fire,quick.end);
    Check(quick.fire==30 && quick.end==60,line);
}

void BudgetChecks() {
    // The stock weapon (V_510_MASER_THUNDER01.SGO): 5 damage a round, a 1000-round burst, 7000 rounds; the requests'
    // damage factors (AWEAPON348 / 358 / 363 vehicle_setup[0][1]): EMC 1.3, EMCS 7.5, EMCX 12.5.
    const float factors[]={1.3f,7.5f,12.5f};
    const char* names[]={"EMC","EMCS","EMCX"};
    for(int i=0;i<3;++i) {
        const float perHit=5.0f*factors[i];
        int ammo=7000,beams=0;
        double total=0.0;
        while(ammo>0) {
            const Budget b=Plan(perHit,1000,ammo,2.5f,1.0f);
            total+=static_cast<double>(b.perRound)*b.rounds;
            ammo-=b.used;++beams;
            if(b.used<=0)break;
        }
        const Budget b=Plan(perHit,1000,7000,2.5f,1.0f);
        char line[220];
        std::snprintf(line,sizeof(line),"%s: a beam %.0f damage to every enemy on its line (%d rounds of %.2f), blast %.0f; %d beams, %.0f in all = the "
                      "stock magazine's %.0f",names[i],b.line,b.rounds,b.perRound,b.blast,beams,total,perHit*7000.0f);
        Check(beams==7 && b.used==1000 && b.rounds==150 && std::fabs(total-perHit*7000.0)<1e-3*perHit*7000.0 &&
              std::fabs(b.blast-b.line)<1e-3f,line);
    }
    const Budget last=Plan(6.5f,1000,400,2.5f,0.5f);
    Check(last.used==400 && std::fabs(last.line-2600.0f)<1e-2f && std::fabs(last.blast-1300.0f)<1e-2f,
          "a last short burst (400 rounds left): its own rounds' damage, the blast its share");
    Check(Plan(6.5f,1000,0,2.5f,1.0f).line==0.0f,"no rounds: no damage");
}

// The stand-in line: buildings [x0, x1] along the beam (it runs along +x from 0) and a hill's face at `hill` (the
// ground: none <= 0). Rays along the line: the nearest face ahead of `a` of what does not hold `a`.
struct Building { float x0,x1,hp; float downAt; bool down; };
struct World {
    std::vector<Building> b;
    float hill;
    float now=0.0f,collapse=0.0f;   // s a fallen building's collision stays
    bool Solid(const Building& k) const { return !k.down || now<k.downAt+collapse; }
    float Ray(float a,float end,bool ground) const {
        float best=-1.0f;
        for(const auto& k:b) {
            if(!Solid(k) || (a>=k.x0 && a<=k.x1) || k.x0<a || k.x0>end)continue;
            if(best<0.0f || k.x0-a<best)best=k.x0-a;
        }
        if(ground && hill>0.0f && hill>=a && hill<=end && (best<0.0f || hill-a<best))best=hill-a;
        return best;
    }
};

struct Sweep { float seeGround,lastDown,firstCharge; int charges; bool allDown; };
// The beam fired along the line for `beamSec` with EmcBreak `breakPerSec` (emc.cpp Beam: a scan a frame, the break charges
// every 0.1 s on the scan's buildings, each a 12 m blast at the face + 2 m).
Sweep Fire(World w,float beamSec,float breakPerSec) {
    const float from[3]={0.0f,0.0f,0.0f},dir[3]={1.0f,0.0f,0.0f};
    const float charge=breakPerSec*0.1f,radius=12.0f,into=2.0f;
    Sweep r{-1.0f,-1.0f,-1.0f,0,false};
    float breakLeft=0.0f;
    for(int f=0;f*kFrameSec<beamSec;++f) {
        w.now=f*kFrameSec;
        const Scan s=ScanLine(from,dir,kBeamRange,[&](const float* a,const float* b){return w.Ray(a[0],b[0],true);},
                              [&](const float* a,const float* b){return w.Ray(a[0],b[0],false);});
        if(r.seeGround<0.0f && s.ground && std::fabs(s.end-w.hill)<0.5f)r.seeGround=w.now;
        breakLeft-=kFrameSec;
        if(breakLeft<=0.0f) {
            breakLeft+=0.1f;
            for(int k=0;k<s.breaks;++k) {
                ++r.charges;
                if(r.firstCharge<0.0f)r.firstCharge=w.now;
                const float at=s.at[k]+into;
                for(auto& bl:w.b) {
                    if(bl.down)continue;
                    const float gap=at<bl.x0 ? bl.x0-at : at>bl.x1 ? at-bl.x1 : 0.0f;
                    if(gap>radius)continue;
                    bl.hp-=charge;
                    if(bl.hp<=0.0f){bl.down=true;bl.downAt=w.now;}
                }
            }
        }
        bool all=true;
        float last=0.0f;
        for(const auto& bl:w.b){all=all && bl.down;if(bl.down && bl.downAt>last)last=bl.downAt;}
        if(all && !r.allDown){r.allDown=true;r.lastDown=last;}
    }
    return r;
}

World Row(int n,float first,float spacing,float width,float hp,float hill) {
    World w{};w.hill=hill;
    for(int i=0;i<n;++i)w.b.push_back(Building{first+i*spacing,first+i*spacing+width,hp,0.0f,false});
    return w;
}

void SweepChecks() {
    std::printf("\nthe sweep (beam 2.5 s, EmcBreak 20000/s: 2000 a charge every 0.1 s):\n");
    std::printf("  %-52s %10s %10s %8s %8s\n","line","sees ground","all down","charges","first");
    struct Case { const char* name; World w; float collapse; float wantDown; };
    Case cases[]={
        {"5 buildings 1500 HP, 40 m apart, hill at 450 m",Row(5,100.0f,40.0f,20.0f,1500.0f,450.0f),0.0f,0.2f},
        {"the same, their collision kept 1 s after they fall",Row(5,100.0f,40.0f,20.0f,1500.0f,450.0f),1.0f,0.2f},
        {"10 buildings 900 HP, 15 m apart (a city block)",Row(10,60.0f,15.0f,12.0f,900.0f,500.0f),0.0f,0.2f},
        {"3 towers 5000 HP, 60 m apart, hill at 400 m",Row(3,120.0f,60.0f,30.0f,5000.0f,400.0f),0.0f,0.4f},
        {"12 buildings 1500 HP, 25 m apart (past the scan's steps)",Row(12,50.0f,25.0f,15.0f,1500.0f,550.0f),0.0f,1.0f},
    };
    for(auto& c:cases) {
        c.w.collapse=c.collapse;
        const Sweep r=Fire(c.w,2.5f,20000.0f);
        std::printf("  %-52s %9.2fs %9.2fs %8d %7.2fs\n",c.name,r.seeGround,r.lastDown,r.charges,r.firstCharge);
        char line[200];
        std::snprintf(line,sizeof(line),"%s: every building down within %.1f s of a 2.5 s beam, the ground seen",c.name,c.wantDown);
        Check(r.allDown && r.lastDown<=c.wantDown && r.seeGround>=0.0f && r.seeGround<=2.5f,line);
    }
    World open=Row(0,0.0f,0.0f,0.0f,0.0f,-1.0f);
    const Sweep none=Fire(open,2.5f,20000.0f);
    Check(none.charges==0 && none.seeGround<0.0f,"open air to its reach: no charge, no ground (no blast at its end)");
}

// The beam turned at `rate` deg/s: its far end (kBeamRange) moves rate x range per second; a break charge every 0.1 s
// with a 12 m blast covers the swept line without a gap while the end moves no more than the blast's width a tick.
void TurnChecks() {
    const float width=2.0f*12.0f,tick=0.1f;
    const float most=width/(tick*kBeamRange)*57.29578f;
    char line[160];
    std::snprintf(line,sizeof(line),"turning the beam: no gap between break charges at its far end up to %.0f deg/s (%.0f m a tick at 600 m)",most,width);
    Check(most>=20.0f,line);
}
}  // namespace

int main() {
    CycleChecks();
    BudgetChecks();
    SweepChecks();
    TurnChecks();
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n",failures);
    return failures ? 1 : 0;
}
