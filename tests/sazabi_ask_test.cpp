// The Sazabi's trigger requests (src/sazabi_arms.h Ask): a tap fires one round even when the animator first puts the
// tomahawk away (up to kAskMost), a release fires no extra round, a held trigger keeps firing.
#include "sazabi_arms.h"
#include <cstdio>

using crew::szarms::Ask;
using crew::szarms::kAskMost;

namespace {
int failures=0;
void Check(bool ok,const char* what) {
    if(!ok){std::printf("FAIL %s\n",what);++failures;}
}
constexpr float kFrame=1.0f/60.0f;
// The frames the request is on, the trigger pressed for the first `heldFrames`; a round leaves at frame `shotAt`
// (-1: never).
int Run(int heldFrames,int shotAt,int frames) {
    Ask a;int on=0;
    for(int f=0;f<frames;++f) {
        if(f==shotAt)a.Shot();
        if(a.Step(f<heldFrames,kFrame))++on;
    }
    return on;
}
}

int main() {
    // a one-frame tap while the tomahawk is still put away (0.6 s): still asked for until its round leaves
    Check(Run(1,40,120)==40,"a tap is kept until its round leaves");
    // nothing leaves (the swing never ended): the request ends after kAskMost, not forever
    const int most=static_cast<int>(kAskMost/kFrame+0.5f);
    const int unanswered=Run(1,-1,600);
    Check(unanswered>=most-1 && unanswered<=most+1,"an unanswered tap ends after kAskMost");
    // held for a second with rounds leaving, then released: off on the release, no extra round asked for
    {
        Ask a;
        for(int f=0;f<60;++f){if(f%30==5)a.Shot();a.Step(true,kFrame);}
        Check(!a.Step(false,kFrame),"a release after a round asks for no extra one");
    }
    // released before its first round: kept for that one round, ended by it
    {
        Ask a;
        for(int f=0;f<5;++f)a.Step(true,kFrame);
        Check(a.Step(false,kFrame),"released before any round: still asked for");
        a.Shot();
        Check(!a.On(),"its round ends it");
    }
    // a new press after a round: asked for again (the earlier round does not end it)
    {
        Ask a;
        a.Step(true,kFrame);a.Shot();a.Step(false,kFrame);
        const bool pressed=a.Step(true,kFrame);
        Check(pressed && a.Step(false,kFrame),"a second tap is its own request");
    }
    if(failures)return 1;
    std::printf("sazabi_ask_test: passed\n");
    return 0;
}
