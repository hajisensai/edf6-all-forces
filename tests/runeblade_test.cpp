// src/runeblade_ether_logic.h / _combo_logic.h / _net.h offline (docs/newclass-re/runeblade-plan.md): the ether's spend /
// drain / bleed / refill and depletion, a frame of rune steps (ground refill, air steps, the gap, the cost, the glide, the
// push along the stick), the combo machine (entries, buffering, cancel, window, air branch, ranged insert, land end, a
// changed spec), the ZWRB codec (round trips; a wrong size, magic, version, kind or field refused).
#include "../src/runeblade_net.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
bool Near(float a,float b) { return std::fabs(a-b)<1e-3f; }
using namespace crew::runeblade;

StepIn Air(bool press,bool hold=false,float vy=-1.0f) {
    StepIn in;in.press=press;in.hold=hold;in.ground=false;in.carry[1]=vy;return in;
}
StepIn Ground(bool press=false) { StepIn in;in.ground=true;in.press=press;return in; }
// Up in the air past kAirborneFrames and the step gap.
void Lift(Motion& m,Ether& e) { for(int i=0;i<kStepGapFrames+kAirborneFrames+1;++i)StepFrame(m,e,Air(false)); }
}  // namespace

int main() {
    // ---- the gauge ----
    {
        Ether e;
        Check(Near(e.level,kEtherMax) && !e.depleted,"full at first");
        Check(Spend(e,30.0f) && Near(e.level,70.0f),"a spend takes it");
        Check(!Spend(e,80.0f) && Near(e.level,70.0f),"too much: refused, nothing taken");
        Check(!Spend(e,-1.0f),"a negative spend: refused");
        Check(Spend(e,70.0f) && e.level==0.0f && e.depleted,"to zero: depleted");
        Check(!Spend(e,0.0f),"depleted: even nothing is refused");
        Check(Drain(e)==0.0f,"depleted: nothing to drain");
        for(int i=0;i<24;++i)Refill(e);
        Check(e.depleted,"under kRecoverAt still depleted");
        Refill(e);
        Check(!e.depleted && Near(e.level,kRecoverAt),"back at kRecoverAt");
        Check(Near(Drain(e),kRecoverAt) && e.depleted && e.level==0.0f,"a drain takes all and depletes");
        Ether f;
        Check(Near(Bleed(f,3.0f),3.0f) && Near(f.level,97.0f),"a bleed takes what is asked");
        f.level=1.0f;
        Check(Near(Bleed(f,3.0f),1.0f) && f.depleted,"a bleed past the bottom takes what is left and depletes");
        for(int i=0;i<500;++i)Refill(f);
        Check(Near(f.level,f.max),"the refill stops at max");
        const Ether s=Sane(250.0f,-3.0f,false);
        Check(Near(s.max,kEtherMax) && Near(s.level,kEtherMax),"a bad max is the class's, the level clamped");
        Check(Near(Sane(NAN,50.0f,true).level,50.0f) && Sane(NAN,50.0f,true).depleted,"a level not finite: full");
    }
    // ---- rune steps ----
    {
        Motion m;Ether e;
        StepOut o=StepFrame(m,e,Ground(true));
        Check(o.cue==Cue::takeoff && !o.jump,"a ground press: the stock jump, a takeoff cue");
        o=StepFrame(m,e,Air(true));
        Check(!o.jump,"a press right after leaving the ground: no step (the ground jump's frames)");
        Lift(m,e);
        o=StepFrame(m,e,Air(true));
        Check(o.jump && o.cue==Cue::step && Near(e.level,kEtherMax-kStepCost) && m.steps==1,"an air press: a step, its cost");
        o=StepFrame(m,e,Air(true));
        Check(!o.jump && Near(e.level,kEtherMax-kStepCost),"inside the gap: no step, nothing spent");
        int steps=1;
        for(int k=0;k<10;++k) {
            for(int i=0;i<kStepGapFrames;++i)StepFrame(m,e,Air(false,false,1.0f));
            if(StepFrame(m,e,Air(true)).jump)++steps;
        }
        Check(steps==static_cast<int>(kEtherMax/kStepCost),"as many steps as the gauge holds");
        Check(e.depleted && e.level==0.0f,"spent out: depleted");
        o=StepFrame(m,e,Air(true,true,-20.0f));
        Check(!o.jump && !o.glide,"depleted: no step, no glide");
        for(int i=0;i<kRefillDelay;++i)StepFrame(m,e,Ground());
        Check(e.level==0.0f && m.steps==0,"landed: the delay before the refill; the steps counted again");
        StepFrame(m,e,Ground());
        Check(Near(e.level,kRefillPerFrame),"then it refills");
        StepIn off=Ground();off.able=false;
        StepFrame(m,e,off);
        Check(Near(e.level,kRefillPerFrame),"not able (riding, dead): no refill");
    }
    {   // the glide
        Motion m;Ether e;
        Lift(m,e);
        StepOut o=StepFrame(m,e,Air(false,true,-10.0f));
        Check(o.glide && m.gliding && Near(e.level,kEtherMax-kGlideDrain),"held while falling: a glide, its drain");
        o=StepFrame(m,e,Air(false,true,3.0f));
        Check(!o.glide,"rising: no glide");
        o=StepFrame(m,e,Air(false,false,-10.0f));
        Check(!o.glide,"not held: no glide");
        Check(Near(GlideFall(-10.0f),-kGlideSink) && Near(GlideFall(-1.0f),-1.0f) && Near(GlideFall(2.0f),2.0f),"the fall held");
    }
    {   // the push
        float x=0,z=0;
        const float none[2]{0.1f,0.1f};
        Check(!WorldPush(none,0.0f,&x,&z),"a stick in its dead zone: no way");
        const float fwd[2]{0.0f,-1.0f};   // +0xD50 holds -LY: the stick forward
        Check(WorldPush(fwd,0.0f,&x,&z) && Near(x,0.0f) && Near(z,1.0f),"forward at yaw 0: +z");
        Check(WorldPush(fwd,1.5707963f,&x,&z) && Near(x,1.0f) && Near(z,0.0f),"forward at yaw pi/2: +x");
        const float right[2]{-1.0f,0.0f};
        Check(WorldPush(right,0.0f,&x,&z) && Near(x,1.0f) && Near(z,0.0f),"right at yaw 0: +x");
        Motion m;Ether e;Lift(m,e);
        StepIn in=Air(true);in.stick[1]=-1.0f;
        StepOut o=StepFrame(m,e,in);
        Check(o.push && Near(o.pushZ,kStepPush) && Near(o.pushX,0.0f),"a step pushes along the stick at kStepPush");
        for(int i=0;i<kStepGapFrames;++i)StepFrame(m,e,Air(false));
        in.carry[0]=40.0f;
        o=StepFrame(m,e,in);
        Check(o.push && Near(o.pushZ,kMostAirSpeed),"a fast carry: turned to the stick, kMostAirSpeed at most");
    }
    // ---- the combo ----
    {
        using namespace combo;
        // three ground slashes, an air dive (air only, ends on landing), a stage that takes no insert
        const Stage st[]={
            {20,6,10,12,1,0},
            {20,6,10,12,2,0},
            {30,8,30,15,-1,kNoInsert},
            {40,0,40,0,-1,kAirOnly|kLandEnds},
        };
        const Spec spec{st,4,3,3,10};
        Check(Valid(spec),"the spec is valid");
        Stage bad[]={{10,10,5,0,-1,0}};
        Check(!Valid(Spec{bad,1,-1,-1,0}),"a hit frame past the stage: invalid");
        State s;
        Output o=Step(s,&spec,Input{true,false,false});
        Check(o.started==0 && Active(s),"a press: stage 0");
        for(int i=1;i<=6;++i)o=Step(s,&spec,Input{});
        Check(o.hit && o.stage==0,"the hit frame");
        o=Step(s,&spec,Input{true,false,false});
        Check(o.started<0 && s.buffered,"a press before cancelFrom: buffered");
        for(int i=8;i<=10;++i)o=Step(s,&spec,Input{});
        Check(o.started==1,"at cancelFrom: on to stage 1");
        // let stage 1 run out, then press inside the window
        for(int i=0;i<20+5;++i)o=Step(s,&spec,Input{});
        Check(s.phase==Phase::window && o.stage==1,"stage 1 over: the window");
        o=Step(s,&spec,Input{true,false,false});
        Check(o.started==2,"a press in the window: stage 2");
        // a ranged insert holds the clock
        o=Step(s,&spec,Input{false,false,true});
        Check(!o.insert,"stage 2 takes no insert");
        bool ended=false;
        for(int i=0;i<30+15+1 && Active(s);++i){o=Step(s,&spec,Input{});ended=ended || o.ended;}
        Check(!Active(s) && ended,"the window out: ended");
        o=Step(s,&spec,Input{true,false,false});
        const int startFrame=s.frame;
        o=Step(s,&spec,Input{false,false,true});
        Check(o.insert && s.frozen==9 && s.frame==startFrame,"an insert: taken, the clock held");
        for(int i=0;i<9;++i)Step(s,&spec,Input{});
        Check(s.frame==startFrame && s.frozen==0,"held insertFrames frames");
        Step(s,&spec,Input{});
        Check(s.frame==startFrame+1,"then on");
        // the air branch: a press while airborne in a ground stage goes to stage 3
        for(int i=0;i<12;++i)o=Step(s,&spec,Input{false,true,false});
        o=Step(s,&spec,Input{true,true,false});
        Check(o.started==3,"airborne mid-combo: the air branch");
        o=Step(s,&spec,Input{false,false,false});
        Check(o.ended && !Active(s),"a kLandEnds stage: landing ends it");
        o=Step(s,&spec,Input{true,true,false});
        Check(o.started==3,"the first press in the air: airEntry");
        Spec other=spec;
        o=Step(s,&other,Input{});
        Check(!Active(s) && s.spec==&other,"another spec: idle over it");
        const Spec ground{st,4,-1,-1,0};
        State g;
        o=Step(g,&ground,Input{true,true,false});
        Check(o.started==0,"no airEntry: stage 0 in the air too");
        const Stage airOnly[]={{10,0,10,0,-1,kAirOnly}};
        const Spec ao{airOnly,1,-1,-1,0};
        State a;
        Check(Step(a,&ao,Input{true,false,false}).started<0,"an air-only stage: not from the ground");
        Check(Step(s,nullptr,Input{true,false,false}).started<0 && !Active(s),"no spec: nothing");
    }
    // ---- ZWRB ----
    {
        using namespace net;
        unsigned char bytes[kWire];
        Msg m;m.kind=Kind::state;crew::netref::MakePlayer(1,m.player);m.ether=42.37f;m.flags=kDepleted;m.serial=7;
        Msg o;
        Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),o),"a state round-trips");
        Check(o.kind==Kind::state && Near(o.ether,42.37f) && o.flags==kDepleted && o.serial==7 && !std::memcmp(o.player,m.player,32),
              "and reads the same");
        Check(TakesState(bytes,sizeof(bytes)),"a state comes as state");
        unsigned char bad[kWire];
        std::memcpy(bad,bytes,kWire);bad[46]=8;Check(!Decode(bad,kWire,o),"a flag not defined: refused");
        std::memcpy(bad,bytes,kWire);bad[9]=1;Check(!Decode(bad,kWire,o),"a pad byte set: refused");
        std::memcpy(bad,bytes,kWire);bad[4]=2;Check(!Decode(bad,kWire,o),"another version: refused");
        std::memcpy(bad,bytes,kWire);bad[0]^=1;Check(!Decode(bad,kWire,o),"another magic: refused");
        std::memcpy(bad,bytes,kWire);bad[8]=9;Check(!Decode(bad,kWire,o),"a kind not defined: refused");
        Check(!Decode(bytes,kWire-1,o),"the wrong size: refused");
        Msg n=m;std::memset(n.player,0,32);n.player[0]=3;n.player[1]=0x5A;
        Check(!Encode(n,bytes,sizeof(bytes)),"a NetId, not a player: not encoded");
        Msg st;st.kind=Kind::step;crew::netref::MakePlayer(0,st.player);st.serial=200;st.cue=static_cast<std::uint8_t>(Cue::step);
        Check(Encode(st,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),o) && o.kind==Kind::step && o.serial==200 &&
              o.cue==st.cue,"a step round-trips");
        Check(!TakesState(bytes,sizeof(bytes)),"a step is no state");
        std::memcpy(bad,bytes,kWire);bad[46]=1;Check(!Decode(bad,kWire,o),"an unused byte set: refused");
        st.cue=0;Check(!Encode(st,bytes,sizeof(bytes)),"a step without its cue: not encoded");
        Msg sg;sg.kind=Kind::stage;crew::netref::MakePlayer(2,sg.player);sg.tag=4;sg.stage=3;
        Check(Encode(sg,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),o) && o.tag==4 && o.stage==3,"a stage round-trips");
        sg.stage=combo::kMostStages;Check(!Encode(sg,bytes,sizeof(bytes)),"a stage past the most: not encoded");
        Pace p;
        Check(Due(p,50.0f,0,1000),"never sent: due");
        Sent(p,50.0f,0,1000);
        Check(!Due(p,50.5f,0,1050) && !Due(p,52.0f,0,1050),"a small move, or inside the gap: not due");
        Check(Due(p,52.0f,0,1100),"a unit's move past the gap: due");
        Check(Due(p,50.0f,kGliding,1001),"a flag changed: due at once");
        Check(Due(p,50.0f,0,2000),"the heartbeat");
    }
    std::printf("runeblade: %d checks passed\n",checks);
    return 0;
}
