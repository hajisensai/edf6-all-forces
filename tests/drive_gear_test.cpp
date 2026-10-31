// War Thunder's cruise gears (src/drive_gear.h): a press shifts one gear, a held key one more each kRepeatSec, R and 4
// the ends; each gear holds its speed (a simple vehicle driven by the stick it is given settles in the gear's band), the
// top gear holds the throttle open, R backs, N lets go.
#include "../src/drive_gear.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
int checks=0;
void Check(bool ok,const char* text) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}

// A vehicle with `top` m/s at the throttle open: it speeds up at `accel` m/s^2 toward it with the stick forward, slows at
// `coast` m/s^2 let go, backs at a third of its top. In `gear` from `speed` for `sec` s at 60 Hz: the least and the most
// speed in its last half.
struct Band { float least,most; };
Band Run(drivegear::State& s,float top,float speed,float sec) {
    constexpr float dt=1.0f/60.0f,accel=3.0f,coast=1.5f;
    Band b{1e9f,-1e9f};
    const int n=static_cast<int>(sec/dt);
    for(int i=0;i<n;++i) {
        const float stick=drivegear::Drive(s,speed,top);
        if(stick<0.0f)speed+=accel*(1.0f-speed/top)*dt;
        else if(stick>0.0f)speed+=(-top/3.0f-speed)*dt;
        else speed-=std::fmin(coast*dt,std::fabs(speed))*(speed>0.0f ? 1.0f : -1.0f);
        if(i>n/2){b.least=std::fmin(b.least,speed);b.most=std::fmax(b.most,speed);}
    }
    return b;
}
}  // namespace

int main() {
    using namespace drivegear;
    constexpr float dt=1.0f/60.0f;
    State s;
    Check(s.gear==kNeutral,"a fresh drive is in N");
    Shift(s,1,dt);Check(s.gear==1,"W pressed: 1st");
    for(int i=0;i<10;++i)Shift(s,1,dt);
    Check(s.gear==1,"W held under kRepeatSec: still 1st");
    Shift(s,0,dt);Shift(s,1,dt);Check(s.gear==2,"W pressed again: 2nd");
    for(int i=0;i<static_cast<int>(kRepeatSec/dt)+2;++i)Shift(s,1,dt);
    Check(s.gear==3,"W held kRepeatSec: one more (3rd)");
    for(int i=0;i<600;++i)Shift(s,1,dt);
    Check(s.gear==kTopGear,"W held on: the top gear, no further");
    Shift(s,0,dt);
    for(int i=0;i<1200;++i)Shift(s,-1,dt);
    Check(s.gear==kReverse,"S held: down to R, no further");
    Shift(s,0,dt);Shift(s,1,dt);Check(s.gear==kNeutral,"W from R: N");

    // Each gear holds its speed, within its band; the top gear runs at the top; R backs; N coasts to a stop.
    const float top=16.0f;
    for(int gear=1;gear<kTopGear;++gear) {
        State g;g.gear=gear;
        const Band b=Run(g,top,0.0f,40.0f);
        const float want=kShare[gear]*top;
        char text[96];
        std::snprintf(text,sizeof(text),"gear %d holds %.1f m/s (%.2f..%.2f)",gear,want,b.least,b.most);
        Check(b.least>=want-kBand*top-0.2f && b.most<=want+0.2f,text);
        State down;down.gear=gear;
        const Band from=Run(down,top,top*0.95f,40.0f);   // shifted down at speed: it slows to the gear's
        Check(from.most<=want+0.2f,"shifted down at speed: it slows to the gear's");
    }
    State g4;g4.gear=kTopGear;
    Check(Run(g4,top,0.0f,40.0f).least>top*0.95f,"the top gear: the throttle open");
    State r;r.gear=kReverse;
    Check(Run(r,top,0.0f,20.0f).most<0.0f,"R backs");
    State n;n.gear=kNeutral;
    Check(Drive(n,10.0f,top)==0.0f && Run(n,top,10.0f,20.0f).most<0.01f,"N lets go: it coasts to a stop");

    // Each vehicle's own top (its native gear table's last row): the stock tops span 5 m/s (402) to 57.2 m/s (503), the
    // requests up to 80 m/s (the Striker); every gear is a share of that vehicle's own, not of its class's.
    Check(TopOfGears(1,14.0f)==14.0f,"a simple gear table (one row): its row's speed");
    Check(TopOfGears(6,27.0f)==27.0f,"a six-row table: the last row's speed");
    Check(TopOfGears(0,14.0f)==0.0f,"no rows: no top (no gears for it)");
    Check(TopOfGears(kMaxGearRows+1,14.0f)==0.0f,"a row count past any table: not a table (no gears)");
    Check(TopOfGears(1,0.0f)==0.0f && TopOfGears(1,-14.0f)==0.0f,"a top of 0 or less: none");
    Check(TopOfGears(1,std::nanf(""))==0.0f && TopOfGears(1,std::numeric_limits<float>::infinity())==0.0f,"a top not finite: none");
    const float owns[]={5.0f,14.0f,57.2f,80.0f};
    for(const float own:owns) {
        for(int gear=1;gear<kTopGear;++gear) {
            State g;g.gear=gear;
            const Band b=Run(g,own,0.0f,120.0f);
            const float want=kShare[gear]*own;
            char text[112];
            std::snprintf(text,sizeof(text),"top %.1f m/s: gear %d holds its own share %.2f m/s (%.2f..%.2f)",own,gear,want,
                          b.least,b.most);
            Check(b.least>=want-kBand*own-0.02f*own && b.most<=want+0.02f*own,text);
        }
    }
    // The gears on keys of their own, W / S override them (Input): +1 the cruise-up key, -1 the cruise-down key.
    {
        State k;
        Check(!Input(k,0,false,dt) && k.gear==kNeutral,"nothing pressed: N, no stick written");
        Check(Input(k,1,false,dt) && k.gear==1,"the gear key pressed: 1st, the stick is the gear's");
        for(int i=0;i<10;++i)Input(k,1,false,dt);
        Check(k.gear==1,"the gear key held under kRepeatSec: still 1st");
        for(int i=0;i<static_cast<int>(kRepeatSec/dt)+2;++i)Input(k,1,false,dt);
        Check(k.gear==2,"the gear key held kRepeatSec: one more");
        Input(k,0,false,dt);
        Check(Input(k,1,false,dt) && k.gear==3,"pressed anew: 3rd");
        Input(k,0,false,dt);Input(k,-1,false,dt);Check(k.gear==2,"the cruise-down key: down one");

        // W or S in hand: back to N, false (the keys go through), whatever the gear key does at the same time.
        Check(!Input(k,0,true,dt) && k.gear==kNeutral,"W / S pressed: the cruise is cancelled (N), not written");
        Check(!Input(k,1,true,dt) && k.gear==kNeutral,"W / S held: the gear key does nothing");
        Check(!Input(k,-1,true,dt) && k.gear==kNeutral,"W / S held: the cruise-down key does nothing either");
        Check(!Input(k,0,false,dt) && k.gear==kNeutral,"W / S let go: it stays in N, nothing written");
        for(int i=0;i<600;++i)Check(!Input(k,0,false,dt) && k.gear==kNeutral,"let go: N held, nothing written");
        Check(Input(k,1,false,dt) && k.gear==1,"the gear key pressed again: cruise from N up to 1st");

        // A gear key already down when W / S went in is not a new press when W / S are let go.
        State h;h.gear=3;
        Input(h,1,true,dt);
        Check(!Input(h,1,false,dt) && h.gear==kNeutral,"a gear key held through W / S: no gear on its release of W / S");
        for(int i=0;i<600;++i)Input(h,1,false,dt);
        Check(h.gear==kNeutral,"...and no key repeat from it either");
        Input(h,0,false,dt);
        Check(Input(h,1,false,dt) && h.gear==1,"...until it is let go and pressed anew");

        // A drive that starts (boarding while the sprint's Shift is down; another vehicle; a stale one): a gear key down then
        // is no press.
        {
            State f=Fresh();
            Check(f.gear==kNeutral,"a fresh drive is in N");
            for(int i=0;i<600;++i)Check(!Input(f,1,false,dt) && f.gear==kNeutral,"started with the gear key down: no gear, however long");
            Input(f,0,false,dt);
            Check(Input(f,1,false,dt) && f.gear==1,"...let go and pressed anew: 1st");
            State g2=Fresh();
            Check(!Input(g2,0,false,dt) && Input(g2,1,false,dt) && g2.gear==1,"started with no key down: the first press shifts");
            State d2=Fresh();
            Check(!Input(d2,-1,false,dt) && d2.gear==kNeutral,"started with the cruise-down key down: no gear (R) either");
        }
        // The keys come back (pause over, the map closed, the window in front again) with a gear key still down.
        {
            State p;p.gear=2;
            Hold(p);
            Check(Input(p,1,false,dt) && p.gear==2,"keys back with the gear key down: the gear held, not shifted");
            for(int i=0;i<600;++i)Input(p,1,false,dt);
            Check(p.gear==2,"...and no key repeat");
            Input(p,0,false,dt);
            Check(Input(p,1,false,dt) && p.gear==3,"...let go and pressed anew: shifts");
            State q;q.gear=2;Hold(q);
            Check(Input(q,0,false,dt) && q.gear==2 && !q.blocked,"keys back with none down: the gear goes on");
        }

        // From R / the top gear W / S cancels the same way.
        State r2;r2.gear=kReverse;
        Check(!Input(r2,0,true,dt) && r2.gear==kNeutral,"in R: W / S cancels to N");
        State t;t.gear=kTopGear;
        Check(!Input(t,0,true,dt) && t.gear==kNeutral && !t.open,"in the top gear: W / S cancels to N");
    }
    std::printf("drive_gear: %d checks passed\n",checks);
    return 0;
}
