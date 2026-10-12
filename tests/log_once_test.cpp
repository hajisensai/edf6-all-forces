// SeenOnce (src/log_once.h) offline: a key is fresh once, an answer flipping between two keys is logged twice in all
// (the shore helicopter of report #11 wrote a line per flip), the full table says so once and counts the rest.
// cmake --build build --target log_once_test && build\log_once_test.exe
#include "../src/log_once.h"
#include <cstdio>

namespace {
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
using Seen=crew::SeenOnce<4>;

void FlipFlop() {
    Seen s;
    int logged=0;
    for(int i=0;i<10000;++i)if(s.Check(i%2 ? 7u : 9u)==Seen::Verdict::fresh)++logged;
    Expect(logged==2,"two answers flipping 10000 times are logged twice");
    Expect(s.Count()==2 && s.Dropped()==0,"two keys kept, none dropped");
}

void Full() {
    Seen s;
    for(std::uint64_t k=1;k<=4;++k)Expect(s.Check(k)==Seen::Verdict::fresh,"the first four keys are fresh");
    Expect(s.Check(5)==Seen::Verdict::full,"the fifth says the table is full");
    Expect(s.Check(6)==Seen::Verdict::seen,"the sixth is only counted");
    Expect(s.Check(2)==Seen::Verdict::seen,"a kept key stays seen");
    Expect(s.Dropped()==2,"two keys dropped");
    s.Clear();
    Expect(s.Check(5)==Seen::Verdict::fresh && s.Dropped()==0,"Clear starts over");
}
}  // namespace

int main() {
    FlipFlop();
    Full();
    std::printf("%d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
