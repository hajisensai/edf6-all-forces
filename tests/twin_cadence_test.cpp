// src/twin_cadence.h: the BMPT's twin barrels in turn, against a model of the native weapon's own cooldown (a shot sets
// +0xE0C to FireInterval, 0x6981F7; the update first counts it down by its step +0xE10 to 0, 0x693A58..0x693A64, then
// a pulled weapon fires when it is down, 0x693A95). The sequence of shots is what the player sees: left, right, left at
// half the interval.
#include "twin_cadence.h"
#include <cstdio>
#include <vector>

using crew::twin::Barrel;
using crew::twin::Withheld;

namespace {
int fails=0,checks=0;
void Check(bool ok,const char* what) {++checks;if(!ok){++fails;std::printf("FAIL: %s\n",what);}}

struct Sim {
    Barrel b[3];int n;std::vector<int> shots;std::vector<int> at;
    // One frame: both pulled (the V603's primary), the cadence, the weapons' updates.
    void Frame(int frame,bool pulled=true) {
        for(int i=0;i<n;++i)b[i].latched=pulled;
        const auto held=Withheld(b,n);
        for(int i=0;i<n;++i)b[i].cooldown=crew::twin::After(b[i]);
        for(int i=0;i<n;++i)if(b[i].latched && !((held>>i)&1u) && b[i].rounds>0 && !(b[i].cooldown>crew::twin::kReady)) {
            b[i].cooldown=static_cast<float>(b[i].interval);--b[i].rounds;shots.push_back(i);at.push_back(frame);
        }
    }
};
}  // namespace

int main() {
    // The 2A42 pair: one belt (pair 1), FireInterval 12 each, 425 rounds each.
    Sim s{{{1,0,false,0.0f,1.0f,12,425},{1,1,false,0.0f,1.0f,12,425}},2,{},{}};
    for(int f=0;f<60;++f)s.Frame(f);
    bool alternate=s.shots.size()>=8;
    for(std::size_t k=1;k<s.shots.size();++k)alternate=alternate && s.shots[k]!=s.shots[k-1];
    Check(alternate,"held fire: left, right, left ... never the same barrel twice running, never both in one frame");
    bool even=s.at.size()>=8;
    for(std::size_t k=1;k<s.at.size();++k)even=even && s.at[k]-s.at[k-1]==6;
    Check(even,"a round every half interval (6 frames), each barrel every 12");
    Check(s.shots.size()==10 && s.shots[0]==0,"60 frames: ten rounds, the left first");
    Check(s.b[0].rounds-s.b[1].rounds<=1 && s.b[1].rounds<=s.b[0].rounds,"the belts stay even");

    // A pause, then fire again: the barrel with more rounds left goes first (it did not fire last).
    for(int f=60;f<100;++f)s.Frame(f,false);
    const std::size_t before=s.shots.size();
    const int first=s.b[0].rounds>=s.b[1].rounds ? 0 : 1;
    s.Frame(100);
    Check(s.shots.size()==before+1 && s.shots.back()==first && first==0 && s.shots[before-1]==1,
          "a new burst: one barrel, the one with more rounds left (the left on a tie: the right fired last)");

    // One barrel empty (reloading): the other fires at its own rate, alone.
    Sim lone{{{1,0,false,0.0f,1.0f,12,0},{1,1,false,0.0f,1.0f,12,425}},2,{},{}};
    for(int f=0;f<48;++f)lone.Frame(f);
    Check(lone.shots.size()==4,"a partner with no rounds holds nothing back: 4 shots in 48 frames at FireInterval 12");

    // Another pair (the APDS belt) is not a partner of the HE pair; a barrel of no pair is never touched.
    Barrel mixed[3]={{1,0,true,0.0f,1.0f,12,10},{2,3,true,10.0f,1.0f,12,10},{0,2,true,11.0f,1.0f,4,4}};
    Check(Withheld(mixed,3)==0,"different pairs and no pair: nothing held back");
    Barrel recent[2]={{1,0,true,8.0f,1.0f,12,9},{1,1,true,0.0f,1.0f,12,10}};
    Check(Withheld(recent,2)==2u,"the partner's cooldown above half its interval after this frame's countdown: held");
    recent[0].cooldown=7.0f;
    Check(Withheld(recent,2)==0u,"at half the interval after the countdown: free");
    Barrel unpulled[2]={{1,0,false,0.0f,1.0f,12,10},{1,1,true,0.0f,1.0f,12,9}};
    Check(Withheld(unpulled,2)==0u,"a partner not pulled this frame is no partner");
    Check(Withheld(nullptr,2)==0u && Withheld(recent,0)==0u && Withheld(recent,33)==0u,"no barrels or too many: nothing");

    std::printf("twin cadence: %d checks, %d failures\n",checks,fails);
    return fails ? 1 : 0;
}
