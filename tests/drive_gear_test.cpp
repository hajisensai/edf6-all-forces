// War Thunder's cruise gears (src/drive_gear.h): a press shifts one gear, a held key one more each kRepeatSec, R and 4
// the ends; each gear holds its speed (a simple vehicle driven by the stick it is given settles in the gear's band), the
// top gear holds the throttle open, R backs, N lets go.
#include "../src/drive_gear.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
    std::printf("drive_gear: %d checks passed\n",checks);
    return 0;
}
