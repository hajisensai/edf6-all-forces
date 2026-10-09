// The Proteus rework's rules (src/proteus_logic.h, as src/proteus.cpp runs them) checked offline, without the game:
//  - the stances: the toggle starts the stagger, the stagger lasts its seconds and takes no toggle, no legs in it or
//    deployed, the legs full walking and slowed by the standing shield;
//  - the shield: it stands switched on with HP left; deployed it heats with time alone (no hits needed), drops
//    overheated, cools to the resume share in (1 - resume) x coolSec and stands again by itself while still switched
//    on; walking it never heats; its HP (Sense: the native barrier's) breaks it at 0 and keeps it down until refilled
//    to the resume share; down and unhit for the delay it refills at shieldRegenSec from empty to full, never while it
//    stands; a hit restarts the quiet spell;
//  - the weapons: who operates each stock mount (Operator) for every occupancy of the four seats and both stances:
//    a mount's own soldier always, else the chain (the gunner both cannons, the driver both cannons with nobody at the
//    guns and the launcher deployed only);
//  - the step height (StepNormal / StepOf).
// Exit code 1 when one is not as stated.
// Built on request only: cmake --build build --target proteus_check && build\proteus_check.exe
#include "../src/proteus_logic.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace proteus;
constexpr float kDt=1.0f/60.0f;
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
        in.toggle=in.shield=false;   // a press lasts one frame
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
    Run(s,t,k,1,[](const State&){return false;});
    Expect(s.mode==Mode::stowing,"the toggle deployed: stowing");
    Run(s,t,k,1,[](const State&){return false;});
    Expect(s.mode==Mode::stowing,"a toggle in the stagger is not taken");
    Run(s,Input{},k,1000,[](const State& x){return x.mode!=Mode::stowing;});
    Expect(s.mode==Mode::walk,"the stow stagger ends walking");
}

void Shield(const Tunables& k) noexcept {
    std::printf("shield\n");
    State s{};
    Input on{};on.shield=true;
    Run(s,on,k,1,[](const State&){return false;});
    Expect(ShieldUp(s) && !ShieldFollowsView(s),"walking: the switch raises it, facing the nose");
    Expect(LegShare(s,0.5f)==0.5f && TurnShare(s,0.5f,0.4f)==0.5f,"walking with it up: legs and turn slowed");
    Run(s,Input{},k,static_cast<int>(60.0f/kDt),[](const State&){return false;});
    Expect(s.heat==0.0f && ShieldUp(s),"walking it never heats (a minute up)");
    // Deployed: heat by time alone.
    s.mode=Mode::deployed;
    const int hot=Run(s,Input{},k,100000,[](const State& x){return x.overheated;});
    Expect(Near(hot*kDt,k.heatSec,2*kDt) && !ShieldUp(s),"deployed: overheated after heatSec of standing, down");
    const int cool=Run(s,Input{},k,100000,[](const State& x){return !x.overheated;});
    Expect(Near(cool*kDt,(1.0f-k.resume)*k.coolSec,2*kDt) && ShieldUp(s),"cooled to the resume share, it stands again by itself");
    Run(s,on,k,1,[](const State&){return false;});
    Expect(!ShieldUp(s) && ShieldFollowsView(s),"the switch takes it down (deployed it faces the view)");
    // HP: the native barrier's share sensed each frame.
    State h{};h.shieldOn=true;
    Sense(h,0.6f);
    Expect(h.shield==0.6f && h.quiet==0.0f && ShieldUp(h),"a hit on the barrier: its HP share, the quiet spell restarts");
    Run(h,Input{},k,static_cast<int>((k.shieldDelaySec+5.0f)/kDt),[](const State&){return false;});
    Expect(h.shield==0.6f,"standing, it never refills");
    Sense(h,0.0f);
    Expect(h.broken && !ShieldUp(h),"no HP left: broken, down");
    Run(h,Input{},k,static_cast<int>((k.shieldDelaySec-0.1f)/kDt),[](const State&){return false;});
    Expect(h.shield==0.0f && h.broken,"down: no refill within the quiet delay");
    const int refill=Run(h,Input{},k,100000,[](const State& x){return !x.broken;});
    Expect(Near(refill*kDt,0.1f+k.resume*k.shieldRegenSec,3*kDt) && ShieldUp(h),"refilled to the resume share it stands again (switch still on)");
    State f{};f.shield=0.0f;
    Run(f,Input{},k,static_cast<int>((k.shieldDelaySec+k.shieldRegenSec+1.0f)/kDt),[](const State&){return false;});
    Expect(f.shield==1.0f,"down and unhit: empty to full in the delay plus shieldRegenSec");
    State g{};g.shieldOn=true;
    Sense(g,1.5f);
    Expect(g.shield==1.0f,"a sensed share is clamped to 1");
}

void Weapons() noexcept {
    std::printf("weapons\n");
    const Mount& left=kMounts[0];
    const Mount& right=kMounts[1];
    const Mount& launcher=kMounts[2];
    constexpr unsigned D=1u<<kDriver,G=1u<<kGunner,R=1u<<kRightGunner,L=1u<<kLauncherSeat;
    Expect(Operator(left,D,Mode::walk)==0 && Operator(right,D,Mode::walk)==0,"alone: the driver works both cannons, walking too");
    Expect(Operator(launcher,D,Mode::walk)==-1 && Operator(launcher,D,Mode::deployed)==0,"alone: the launcher is the driver's deployed only");
    Expect(Operator(left,D|G,Mode::walk)==1 && Operator(right,D|G,Mode::walk)==1,"a gunner works both cannons (the right one paired)");
    Expect(Operator(launcher,D|G,Mode::deployed)==0,"with a gunner the driver still works the launcher deployed");
    Expect(Operator(right,D|G|R,Mode::walk)==2 && Operator(launcher,D|L,Mode::walk)==3,"a soldier in a mount's own seat keeps it (any stance)");
    Expect(Operator(left,0,Mode::deployed)==-1 && Operator(right,R,Mode::walk)==2,"nobody: no operator; the right seat alone keeps its own");
    Expect(Operator(left,G,Mode::walk)==1 && Operator(launcher,G,Mode::deployed)==-1,"no driver: the gunner never borrows the launcher");
    Expect(!Borrowed(left,1) && Borrowed(right,1) && Borrowed(launcher,0) && !Borrowed(left,-1),"borrowed = operated from another seat");
    unsigned seatsSeen=0;
    for(unsigned m=0;m<kMountCount;++m)seatsSeen|=1u<<kMounts[m].seat;
    Expect(seatsSeen==(G|R|L) && kMountCount==3,"one mount per weapon seat (1, 2, 3), none for the driver's");
    for(unsigned occ=0;occ<16;++occ)for(int mode=0;mode<4;++mode)for(unsigned m=0;m<kMountCount;++m) {
        const int op=Operator(kMounts[m],occ,static_cast<Mode>(mode));
        if(op>=0 && !(occ&(1u<<op))){Expect(false,"an operator is always a seated soldier");return;}
        if((occ&(1u<<kMounts[m].seat)) && op!=static_cast<int>(kMounts[m].seat)){Expect(false,"a mount's own soldier is never overruled");return;}
    }
    Expect(true,"every occupancy x stance: operators seated, own seats never overruled");
}

void Steps() noexcept {
    std::printf("step height\n");
    Expect(Near(StepOf(0.766f,5.0f),1.17f,0.01f),"stock: sin 50 deg on the 5 m foot is a 1.17 m step");
    Expect(Near(StepOf(StepNormal(2.6f,5.0f),5.0f),2.6f,1e-4f),"StepNormal / StepOf round trip (2.6 m)");
    Expect(StepNormal(9.0f,5.0f)==0.2f && StepNormal(0.0f,5.0f)==0.99f,"clamped to [0.2, 0.99]");
}
}  // namespace

int main() {
    Tunables k{};
    Stances(k);
    Shield(k);
    Weapons();
    Steps();
    Tunables quick{};quick.deploySec=0.5f;quick.stowSec=0.25f;quick.heatSec=3.0f;quick.coolSec=2.0f;quick.resume=0.5f;
    quick.shieldRegenSec=10.0f;quick.shieldDelaySec=1.0f;
    Stances(quick);
    Shield(quick);
    std::printf(failures ? "proteus_check: %d WRONG\n" : "proteus_check: all as stated\n",failures);
    return failures ? 1 : 0;
}
