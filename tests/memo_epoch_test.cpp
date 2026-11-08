// memoepoch::Clock (src/memo_epoch.h), the key of VerdictOf's per-frame memo: offline, no game or EDF.dll needed.
#include "../src/memo_epoch.h"
#include <cstdio>
namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);} }
}
int main() {
    using memoepoch::kStallMs;
    memoepoch::Clock c;
    const auto e1=c.Epoch(5,1000);
    Check(c.Epoch(5,1000)==e1,"the same frame at once keeps its epoch");
    Check(c.Epoch(5,1000+kStallMs-1)==e1,"the same frame under the guard keeps its epoch");
    const auto e2=c.Epoch(5,1000+kStallMs);
    Check(e2!=e1,"the same frame at the guard starts a new epoch");
    Check(c.Epoch(5,1000+kStallMs+1)==e2,"the guard counts from the new epoch");
    Check(c.Epoch(5,1000+2*kStallMs-1)==e2,"under the guard from the new epoch keeps it");
    const auto e3=c.Epoch(6,1000+kStallMs+1);
    Check(e3!=e2,"a new frame starts a new epoch at once");
    Check(c.Epoch(7,1000+kStallMs+2)!=e3,"every new frame starts a new epoch");
    const auto before=c.Epoch(7,1000+kStallMs+2);
    c.Reset();
    const auto after=c.Epoch(7,1000+kStallMs+2);
    Check(after>before,"after a reset the same frame and time get a new, larger epoch");
    Check(c.Epoch(7,1000+kStallMs+3)==after,"after a reset the epoch holds within the frame");
    memoepoch::Clock w;   // the wall clock wrapping is unsigned arithmetic too
    const auto w1=w.Epoch(1,~0ull-10);
    Check(w.Epoch(1,5)==w1,"a wall clock wrap under the guard keeps the epoch");
    std::printf("memo_epoch_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
