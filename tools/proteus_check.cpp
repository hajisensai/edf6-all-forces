// The Proteus rework's rules (src/proteus_logic.h, as src/proteus.cpp runs them) checked offline, without the game:
//  - the stances: the toggle starts the stagger, the stagger lasts its seconds and takes no toggle, no legs in it or
//    deployed, the legs full walking and slowed by the front shield;
//  - the directional shield's heat: up and deployed it overheats in heatSec (time alone, no hits needed), drops, cools
//    to the resume share in (1 - resume) x coolSec and stands again by itself while still switched on; walking it never
//    heats, and walking it cools;
//  - the barrier: it takes what the shield lets through, deployed only, refills only after its quiet spell, at
//    barrierRegenSec from empty to full;
//  - the salvo: deployed, marked, cooled down: once, then not until the cooldown has run;
//  - the arcs and the step height (StepNormal / StepOf).
// Exit code 1 when one is not as stated.
// Built on request only: cmake --build build --target proteus_check && build\proteus_check.exe
#include "../src/proteus_logic.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace proteus;
constexpr float kDt=1.0f/60.0f,kPi=3.14159265f;
int failures=0;

void Expect(bool ok,const char* what) noexcept {
    std::printf("  %-78s %s\n",what,ok ? "ok" : "WRONG");
    if(!ok)++failures;
}
bool Near(float a,float b,float tol) noexcept { return std::fabs(a-b)<=tol; }

// Frames of `in` (dt set here) until `stop` holds or `most` frames; the frames run.
template<class Stop> int Run(State& s,Input in,const Tunables& k,int most,Stop stop) noexcept {
    in.dt=kDt;
    for(int f=0;f<most;++f) {
        if(stop(s))return f;
        Step(s,in,k);
        in.toggle=in.shield=in.salvo=false;   // a press lasts one frame
    }
    return most;
}

void Stances(const Tunables& k) noexcept {
    std::printf("stances\n");
    State s{};
    Expect(LegShare(s,0.5f)==1.0f,"walking: the legs at their full (walk) speed");
    Input t{};t.toggle=true;
    Run(s,t,k,1,[](const State&){return false;});
    Expect(s.mode==Mode::deploying && LegShare(s,0.5f)==0.0f && TurnShare(s,0.5f,0.4f)==0.0f,"the toggle: deploying, legs and turn stopped at once");
    const int frames=Run(s,Input{},k,1000,[](const State& x){return x.mode!=Mode::deploying;});
    Expect(std::abs(frames-static_cast<int>(std::lround(k.deploySec/kDt)))<=1,"the deploy stagger lasts deploySec");
    Expect(s.mode==Mode::deployed && LegShare(s,0.5f)==0.0f,"deployed: planted (no walking)");
    Expect(TurnShare(s,0.5f,0.4f)==0.4f,"deployed: it still turns on the spot (deployTurn)");
    // A toggle in the stagger is not taken.
    Run(s,t,k,1,[](const State&){return false;});
    Expect(s.mode==Mode::stowing,"the toggle deployed: stowing");
    Run(s,t,k,3,[](const State&){return false;});   // pressed again inside the stagger
    Expect(s.mode==Mode::stowing,"a toggle inside the stagger is not taken");
    const int back=Run(s,Input{},k,1000,[](const State& x){return x.mode==Mode::walk;});
    Expect(back<=static_cast<int>(std::lround(k.stowSec/kDt)),"the stow stagger lasts stowSec");
    Input sh{};sh.shield=true;
    Run(s,sh,k,1,[](const State&){return false;});
    Expect(ShieldUp(s) && !ShieldFollowsView(s) && Near(LegShare(s,0.5f),0.5f,1e-6f),"the front shield up walking: slowed to shieldSlow");
    Run(s,Input{},k,60*60,[](const State&){return false;});
    Expect(ShieldUp(s) && s.heat==0.0f,"walking an hour behind the front shield: no heat, still up");
}

void Heat(const Tunables& k) noexcept {
    std::printf("directional shield heat\n");
    State s{};s.mode=Mode::deployed;s.shieldOn=true;
    const int over=Run(s,Input{},k,100000,[](const State& x){return x.overheated;});
    Expect(std::abs(over-static_cast<int>(std::lround(k.heatSec/kDt)))<=1,"up and deployed it overheats in heatSec, on time alone");
    Expect(!ShieldUp(s),"overheated: down");
    const int cooled=Run(s,Input{},k,100000,[](const State& x){return !x.overheated;});
    const float want=(1.0f-k.resume)*k.coolSec/kDt;
    Expect(std::fabs(static_cast<float>(cooled)-want)<=2.0f,"it cools to the resume share in (1 - resume) x coolSec");
    Expect(ShieldUp(s),"cooled: up again by itself (still switched on)");
    Input off{};off.shield=true;
    Run(s,off,k,1,[](const State&){return false;});
    const float h=s.heat;
    Run(s,Input{},k,60,[](const State&){return false;});
    Expect(!ShieldUp(s) && s.heat<h,"switched off: down, cooling");
    State w{};w.shieldOn=true;w.heat=0.8f;
    Run(w,Input{},k,60,[](const State&){return false;});
    Expect(w.heat<0.8f && ShieldUp(w),"walking: the heat goes down, the front shield stands");
}

void Barrier(const Tunables& k) noexcept {
    std::printf("barrier and absorb\n");
    const float hp=1000.0f;
    State s{};s.mode=Mode::deployed;s.barrier=1.0f;
    float through=Absorb(s,k,300.0f,false,hp);
    Expect(through==0.0f && Near(s.barrier,0.7f,1e-4f),"deployed: a hit outside the shield goes into the barrier");
    s.shieldOn=true;
    through=Absorb(s,k,300.0f,true,hp);
    Expect(through==0.0f && Near(s.barrier,0.7f,1e-4f),"inside the shield's arc: stopped (shieldBlock 1)");
    through=Absorb(s,k,900.0f,false,hp);
    Expect(Near(through,200.0f,1e-2f) && s.barrier==0.0f,"the barrier spent: the rest reaches the hull");
    Run(s,Input{},k,static_cast<int>(k.barrierDelaySec/kDt)-2,[](const State&){return false;});
    Expect(s.barrier==0.0f,"no refill inside the quiet spell");
    const int full=Run(s,Input{},k,100000,[](const State& x){return x.barrier>=1.0f;});
    Expect(std::abs(full-2-static_cast<int>(std::lround(k.barrierRegenSec/kDt)))<=3,"then full again in barrierRegenSec");
    State w{};w.barrier=0.5f;
    through=Absorb(w,k,100.0f,false,hp);
    Expect(through==100.0f && w.barrier==0.5f,"walking: the barrier takes nothing");
    w.shieldOn=true;
    through=Absorb(w,k,100.0f,true,hp);
    Expect(through==0.0f,"walking: the front shield stops a hit in its arc");
    Run(w,Input{},k,60*30,[](const State&){return false;});
    Expect(w.barrier==0.5f,"walking: no refill");
    Tunables half=k;half.shieldBlock=0.75f;
    State d{};d.mode=Mode::deployed;d.shieldOn=true;d.barrier=0.0f;
    Expect(Near(Absorb(d,half,100.0f,true,hp),25.0f,1e-3f),"shieldBlock 0.75: a quarter goes through");
    Expect(Absorb(d,k,-50.0f,true,hp)==-50.0f,"a heal (negative) is left alone");
}

void Salvo(const Tunables& k) noexcept {
    std::printf("salvo\n");
    State s{};
    Input in{};in.dt=kDt;in.salvo=true;in.marked=true;
    Expect(!Step(s,in,k).salvoFired,"walking: no salvo");
    s.mode=Mode::deployed;
    in.marked=false;
    Expect(!Step(s,in,k).salvoFired,"deployed with no mark: no salvo");
    in.marked=true;
    Expect(Step(s,in,k).salvoFired,"deployed and marked: the salvo");
    Expect(!Step(s,in,k).salvoFired,"at once again: not (cooling down)");
    const int frames=Run(s,Input{},k,100000,[](const State& x){return x.salvoWait<=0.0f;});
    Expect(std::abs(frames-static_cast<int>(std::lround(k.salvoCooldownSec/kDt)))<=2,"the cooldown is salvoCooldownSec");
    Expect(Step(s,in,k).salvoFired,"after it: the salvo again");
}

void Geometry() noexcept {
    std::printf("arcs and step height\n");
    const float half=60.0f*kPi/180.0f;
    Expect(InArc(0,1,0,10,half) && InArc(0,1,8,10,half) && !InArc(0,1,10,1,half) && !InArc(0,1,0,-10,half),"the arc: ahead in, 39 deg in, 84 deg out, behind out");
    Expect(!InArc(0,1,0,0,half),"straight over (no level part): out");
    const float stock=std::sin(50.0f*kPi/180.0f);   // the SGO's 50 deg, as the game stores it (sinf)
    Expect(Near(StepOf(stock,5.0f),1.17f,0.01f),"the stock Proteus (r 5 m, 50 deg) steps 1.17 m");
    Expect(Near(StepOf(StepNormal(2.6f,5.0f),5.0f),2.6f,1e-4f),"StepNormal / StepOf round trip (2.6 m)");
    Expect(StepNormal(100.0f,5.0f)==0.2f && StepNormal(0.0f,5.0f)==0.99f,"StepNormal clamped");
}
}  // namespace

int main() {
    const Tunables k{};
    Stances(k);
    Heat(k);
    Barrier(k);
    Salvo(k);
    Tunables quick=k;quick.deploySec=1.0f;quick.stowSec=2.0f;quick.heatSec=5.0f;quick.coolSec=3.0f;quick.resume=0.5f;
    quick.barrierRegenSec=10.0f;quick.barrierDelaySec=1.0f;quick.salvoCooldownSec=7.0f;
    std::printf("-- other tunables --\n");
    Stances(quick);
    Heat(quick);
    Barrier(quick);
    Salvo(quick);
    Geometry();
    std::printf("%s (%d wrong)\n",failures ? "FAILED" : "all as stated",failures);
    return failures ? 1 : 0;
}
