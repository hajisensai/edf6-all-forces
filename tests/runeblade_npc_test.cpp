// A Rune Blade NPC's hop (src/runeblade_npc_logic.h): when it takes off, steps and rests, and what keeps it on foot.
// Pure, no game.
#include "runeblade_npc_logic.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL: %s\n",what);}
}
namespace h=crew::runeblade::npchop;
namespace rb=crew::runeblade;

h::In Walk(float left,float ether=100.0f) {
    h::In in;
    in.walking=true;in.free=true;in.left=left;in.ground=true;in.airFrames=0;in.ether=ether;in.depleted=false;
    return in;
}
h::In Air(h::In in,int frames) { in.ground=false;in.airFrames=frames;return in; }

void Hop() {
    h::Hop hop;
    std::uint64_t now=10000;
    Check(h::Decide(hop,Walk(h::kHopFar),now)==h::Act::none,"a walk of kHopFar left: walked");
    Check(h::Decide(hop,Walk(h::kHopFar+1.0f),now)==h::Act::takeoff && hop.phase==h::Phase::lifting,"a long walk: takes off");
    Check(h::Decide(hop,Walk(80.0f),now+16)==h::Act::none && hop.phase==h::Phase::lifting,"still on the ground: waits for the lift");
    // in the air: no step before the steps' own frames, nor before the gap
    Check(h::Decide(hop,Air(Walk(80.0f),1),now+32)==h::Act::none && hop.phase==h::Phase::air,"off the ground: in the air");
    Check(h::Decide(hop,Air(Walk(80.0f),rb::kAirborneFrames),now+h::kNpcStepGapMs)==h::Act::none,
          "not past kAirborneFrames: no step (StepFrame would not take it)");
    Check(h::Decide(hop,Air(Walk(80.0f),rb::kAirborneFrames+1),now+h::kNpcStepGapMs-1)==h::Act::none,"before the gap: no step");
    std::uint64_t t=now+h::kNpcStepGapMs;
    Check(h::Decide(hop,Air(Walk(80.0f),30),t)==h::Act::step && hop.steps==1,"past both: a step");
    Check(h::Decide(hop,Air(Walk(80.0f),31),t+1)==h::Act::none,"the next one waits the gap");
    t+=h::kNpcStepGapMs;
    Check(h::Decide(hop,Air(Walk(80.0f),60),t)==h::Act::step && hop.steps==2,"a second step");
    t+=h::kNpcStepGapMs;
    Check(h::Decide(hop,Air(Walk(80.0f),90),t)==h::Act::none,"kNpcSteps a hop at most");
    // landing: the rest from there
    const std::uint64_t land=t+500;
    Check(h::Decide(hop,Walk(80.0f),land)==h::Act::none && hop.phase==h::Phase::idle,"landed: idle, no takeoff at once");
    Check(h::Decide(hop,Walk(80.0f),land+h::kHopRestMs-1)==h::Act::none,"not rested yet");
    Check(h::Decide(hop,Walk(80.0f),land+h::kHopRestMs)==h::Act::takeoff && hop.steps==0,"rested: the next hop");
}

void Holds() {
    const std::uint64_t now=100000;
    h::In in=Walk(80.0f);
    struct Case { const char* what; h::In in; };
    h::In a=in;a.walking=false;
    h::In b=in;b.free=false;
    h::In c=in;c.depleted=true;
    h::In d=in;d.ether=h::kTakeoffEther-0.5f;
    h::In e=in;e.pressed=true;
    h::In f=in;f.ground=false;f.airFrames=20;   // falling off a ledge: not its own takeoff
    h::In g=in;g.left=std::nanf("");
    const Case cases[]={{"not walking",a},{"the move held",b},{"depleted",c},{"too little ether",d},
                        {"the jump already pressed",e},{"in the air, not its own",f},{"NaN left",g}};
    for(const Case& k:cases) {
        h::Hop hop;
        Check(h::Decide(hop,k.in,now)==h::Act::none && hop.phase==h::Phase::idle,k.what);
    }
    // in the air: a step keeps one more step's ether, needs the walk long enough, stops on a held move
    h::Hop hop;
    Check(h::Decide(hop,in,now)==h::Act::takeoff,"takes off");
    h::In low=Air(in,30);low.ether=rb::kStepCost*2.0f-0.5f;
    Check(h::Decide(hop,low,now+h::kNpcStepGapMs)==h::Act::none,"a step only with one more after it");
    h::In near=Air(in,30);near.left=h::kStepFar;
    Check(h::Decide(hop,near,now+h::kNpcStepGapMs)==h::Act::none,"near the goal: no step");
    h::In held=Air(in,30);held.free=false;
    Check(h::Decide(hop,held,now+h::kNpcStepGapMs)==h::Act::none,"the move held: no step");
    // a takeoff that never left the ground ends after kLiftMs
    h::Hop stuck;
    Check(h::Decide(stuck,in,now)==h::Act::takeoff,"takes off (stuck)");
    Check(h::Decide(stuck,in,now+h::kLiftMs-1)==h::Act::none && stuck.phase==h::Phase::lifting,"still lifting");
    Check(h::Decide(stuck,in,now+h::kLiftMs)==h::Act::none && stuck.phase==h::Phase::idle,"never lifted: over, resting");
    Check(h::Decide(stuck,in,now+h::kLiftMs+h::kHopRestMs)==h::Act::takeoff,"rested: tries again");
}

void Tuning() {
    Check(h::kTakeoffEther>=rb::kStepCost*(h::kNpcSteps+1),"the takeoff's ether: its steps and one more");
    Check(h::kNpcStepGapMs*60>=static_cast<std::uint64_t>(rb::kStepGapFrames)*1000,"the NPC's gap at least StepFrame's");
    Check(h::kStepFar<h::kHopFar && h::kNpcSteps<=5,"steps while the walk lasts, fewer than the player's five");
    Check(!std::strcmp(h::ActName(h::Act::takeoff),"takeoff") && !std::strcmp(h::ActName(h::Act::step),"step"),"act names");
}
}  // namespace

int main() {
    Hop();
    Holds();
    Tuning();
    if(failures){std::printf("%d of %d checks failed\n",failures,checks);return 1;}
    std::printf("runeblade_npc: all %d checks passed\n",checks);
    return 0;
}
