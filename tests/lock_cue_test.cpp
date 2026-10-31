// The lock tone as a few cues (src/lock_cue.h): locking sounds exactly maxBeeps beeps and is silent, locked the long tone
// sounds lockedMs and is silent, a lock that holds on one target stays silent, a new weapon / target / a state that fell
// back counts again, maxBeeps = 0 keeps the old always-on tone, and the beeps quicken with the lock's progress.
#include "../src/lock_cue.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* text) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}

using lock_cue::LockCue;
using lock_cue::Step;
constexpr unsigned long long kFrame=16;   // ms
int weaponA,weaponB,targetA,targetB;

// Runs `ms` of frames from `*now`; the beeps begun (rising edges of the tone), the last time it sounded.
struct Run { int beeps; unsigned long long lastOn; bool onAtEnd; };
Run Go(LockCue& c,unsigned long long* now,unsigned long long ms,const void* w,const void* t,int state,float progress,int maxBeeps,
       unsigned long long lockedMs) {
    Run r{0,0,false};
    bool was=false;
    const unsigned long long end=*now+ms;
    for(;*now<end;*now+=kFrame) {
        const bool on=Step(c,w,t,state,progress,*now,maxBeeps,lockedMs).on;
        if(on && !was)++r.beeps;
        if(on)r.lastOn=*now;
        was=on;r.onAtEnd=on;
    }
    return r;
}
}  // namespace

int main() {
    unsigned long long now=1000;
    constexpr unsigned long long locked=450;

    // Locking: maxBeeps beeps, then silent for as long as the lock goes on.
    {
        LockCue c;
        const Run r=Go(c,&now,20000,&weaponA,&targetA,1,0.5f,3,locked);
        Check(r.beeps==3,"locking: exactly 3 beeps");
        Check(!r.onAtEnd,"locking: silent after the beeps");
        Check(r.lastOn-1000<3000,"locking: the beeps are at the start");
    }
    // The tone's pitch: locking 1.0, locked kLockedRatio.
    {
        LockCue c;
        Check(Step(c,&weaponA,&targetA,1,0.0f,now,3,locked).ratio==1.0f,"locking: the base pitch");
        Check(Step(c,&weaponA,&targetA,2,1.0f,now+16,3,locked).ratio==lock_cue::kLockedRatio,"locked: the higher pitch");
    }
    // maxBeeps = 1 / a different count.
    {
        LockCue c;now+=100000;
        Check(Go(c,&now,10000,&weaponA,&targetA,1,0.2f,1,locked).beeps==1,"maxBeeps 1: one beep");
        LockCue d;now+=100000;
        Check(Go(d,&now,30000,&weaponA,&targetA,1,0.2f,5,locked).beeps==5,"maxBeeps 5: five beeps");
    }
    // Locked: the long tone for lockedMs, then silent; the same target kept locked stays silent.
    {
        LockCue c;now+=100000;
        const unsigned long long t0=now;
        const Run r=Go(c,&now,10000,&weaponA,&targetA,2,1.0f,3,locked);
        Check(r.beeps==1,"locked: one tone");
        Check(r.lastOn-t0<locked+kFrame && r.lastOn-t0>=locked-kFrame,"locked: sounds lockedMs");
        Check(!r.onAtEnd,"locked: silent after lockedMs");
        Check(!Go(c,&now,60000,&weaponA,&targetA,2,1.0f,3,locked).onAtEnd,"locked: a held lock stays silent");
    }
    // Locking then locked on the same target: the locking beeps count, the long tone follows, then silence.
    {
        LockCue c;now+=100000;
        Check(Go(c,&now,600,&weaponA,&targetA,1,0.9f,3,locked).beeps>=1,"locking then locked: beeps first");
        const Run r=Go(c,&now,locked*3,&weaponA,&targetA,2,1.0f,3,locked);
        Check(r.beeps==1 && !r.onAtEnd,"...then the long tone, then silence");
    }
    // Starting over: state 0, a different target, a different weapon, locked falling back to locking.
    {
        LockCue c;now+=100000;
        Go(c,&now,20000,&weaponA,&targetA,1,0.5f,3,locked);
        Go(c,&now,kFrame,&weaponA,&targetA,0,0.0f,3,locked);
        Check(Go(c,&now,20000,&weaponA,&targetA,1,0.5f,3,locked).beeps==3,"state 0 between: counted again");
        Check(Go(c,&now,20000,&weaponA,&targetB,1,0.5f,3,locked).beeps==3,"a new target: counted again");
        Check(Go(c,&now,20000,&weaponB,&targetB,1,0.5f,3,locked).beeps==3,"a new weapon: counted again");
        Go(c,&now,5000,&weaponB,&targetB,2,1.0f,3,locked);
        Check(Go(c,&now,20000,&weaponB,&targetB,1,0.5f,3,locked).beeps==3,"locked fell back to locking: counted again");
        Go(c,&now,5000,&weaponB,&targetB,2,1.0f,3,locked);
        Check(Go(c,&now,locked*3,&weaponB,&targetA,2,1.0f,3,locked).beeps==1,"locked on another target: its tone again");
        Go(c,&now,kFrame,&weaponB,&targetA,0,0.0f,3,locked);
        Check(Go(c,&now,locked*3,&weaponB,&targetA,2,1.0f,3,locked).beeps==1,"lost and locked again: its tone again");
    }
    // maxBeeps = 0: the old tone, beeping on as long as it locks and the long tone as long as it is locked.
    {
        LockCue c;now+=100000;
        const Run r=Go(c,&now,30000,&weaponA,&targetA,1,0.5f,0,locked);
        Check(r.beeps>50 && r.lastOn+400>now,"maxBeeps 0: the beeps go on");
        const Run l=Go(c,&now,30000,&weaponA,&targetA,2,1.0f,0,locked);
        Check(l.onAtEnd && l.beeps==1,"maxBeeps 0: the locked tone goes on");
    }
    // The beeps quicken as the lock closes in: more of them in a second at the end than at the start.
    {
        LockCue a,b;now+=100000;
        const int slow=Go(a,&now,2000,&weaponA,&targetA,1,0.0f,0,locked).beeps;
        now+=100000;
        const int fast=Go(b,&now,2000,&weaponA,&targetA,1,1.0f,0,locked).beeps;
        Check(fast>slow,"the beeps quicken with the progress");
        Check(lock_cue::Period(0.0f)==lock_cue::kBeepSlow && lock_cue::Period(1.0f)==lock_cue::kBeepFast,"the period's ends");
        Check(lock_cue::Period(-1.0f)==lock_cue::kBeepSlow && lock_cue::Period(7.0f)==lock_cue::kBeepFast,"the period clamps");
    }
    // The first beep sounds at once (the lock just began).
    {
        LockCue c;now+=100000;
        Check(Step(c,&weaponA,&targetA,1,0.0f,now,3,locked).on,"the first beep at once");
    }
    std::printf("lock_cue: %d checks passed\n",checks);
    return 0;
}
