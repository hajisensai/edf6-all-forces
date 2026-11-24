// liveset (src/live_set.*): the objects the game handed over this step, keyed like WastedFire's memos (memoepoch::Clock).
// Offline: crew::GameFrame stood in for, the wall clock real (a stalled step is waited out).
#include <Windows.h>
#include "../src/live_set.h"
#include <cstdio>

namespace crew {
ULONGLONG frame=1;
ULONGLONG GameFrame() noexcept { return frame; }
}

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);} }
}

int main() {
    int a=0,b=0,c=0;
    Check(!liveset::Has(&a),"nothing handed over yet");
    Check(!liveset::Has(nullptr),"null is never live");
    liveset::Add(nullptr);
    Check(!liveset::Has(nullptr),"null is not added");
    liveset::Add(&a);liveset::Add(&b);
    Check(liveset::Has(&a) && liveset::Has(&b) && !liveset::Has(&c),"this step's objects, only those");
    liveset::Add(&a);
    Check(liveset::Has(&a),"handed over twice in a step: still live");
    crew::frame=2;
    Check(!liveset::Has(&a) && !liveset::Has(&b),"the next step: the last step's objects are not live");
    liveset::Add(&c);
    Check(liveset::Has(&c) && !liveset::Has(&a),"the next step's own");
    // the step does not move (paused, loading, no frame clock): live no longer than memoepoch::kStallMs
    Sleep(150);
    Check(liveset::Has(&c),"a stalled step keeps its objects for a while");
    Sleep(150);   // 300 ms > kStallMs (250)
    Check(!liveset::Has(&c),"a step stalled past kStallMs: its objects are not live");
    liveset::Add(&b);
    Check(liveset::Has(&b) && !liveset::Has(&c),"handed over again after the stall: live in the new epoch");
    // many objects (a battle's ~2,000 and more)
    crew::frame=3;
    static int many[6000];
    for(int& m:many)liveset::Add(&m);
    bool all=true;
    for(int& m:many)all=all && liveset::Has(&m);
    Check(all && !liveset::Has(&b),"6,000 objects in a step, all found");
    std::printf("live_set_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
