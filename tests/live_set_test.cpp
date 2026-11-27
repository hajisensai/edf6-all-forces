// liveset (src/live_set.*): the objects the game handed over this step, keyed like WastedFire's memos (memoepoch::Clock).
// Offline: crew::GameFrame stood in for, and the wall clock too (AddAt / HasAt take it): a stalled step's kStallMs is
// stepped over, not slept through, so a loaded machine cannot stretch it (2026-11-27: Sleep(150) x2 against 250 ms).
#include <Windows.h>
#include "../src/live_set.h"
#include "../src/memo_epoch.h"
#include <cstdio>

namespace crew {
ULONGLONG frame=1;
ULONGLONG GameFrame() noexcept { return frame; }
}

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);} }
}

namespace {
std::uint64_t ms=1000000;   // the stood-in wall clock
bool Has(const void* o) noexcept { return liveset::HasAt(o,ms); }
void Add(const void* o) noexcept { liveset::AddAt(o,ms); }
}

int main() {
    int a=0,b=0,c=0;
    Check(!Has(&a),"nothing handed over yet");
    Check(!Has(nullptr),"null is never live");
    Add(nullptr);
    Check(!Has(nullptr),"null is not added");
    Add(&a);Add(&b);
    Check(Has(&a) && Has(&b) && !Has(&c),"this step's objects, only those");
    Add(&a);
    Check(Has(&a),"handed over twice in a step: still live");
    crew::frame=2;
    Check(!Has(&a) && !Has(&b),"the next step: the last step's objects are not live");
    Add(&c);
    Check(Has(&c) && !Has(&a),"the next step's own");
    // the step does not move (paused, loading, no frame clock): live no longer than memoepoch::kStallMs
    ms+=memoepoch::kStallMs-1;
    Check(Has(&c),"a stalled step keeps its objects for a while (kStallMs-1 ms on)");
    ms+=1;   // kStallMs on: a new epoch
    Check(!Has(&c),"a step stalled kStallMs: its objects are not live");
    Add(&b);
    Check(Has(&b) && !Has(&c),"handed over again after the stall: live in the new epoch");
    // many objects (a battle's ~2,000 and more)
    crew::frame=3;
    static int many[6000];
    for(int& m:many)Add(&m);
    bool all=true;
    for(int& m:many)all=all && Has(&m);
    Check(all && !Has(&b),"6,000 objects in a step, all found");
    std::printf("live_set_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
