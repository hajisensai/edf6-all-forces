// src/runeblade_weapons_logic.h: the rounds' marks and Matins Bell's 2-enemy pierce limit.
#include "runeblade_weapons_logic.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok) { std::printf("FAIL: %s\n",why); std::exit(1); }
}
float FromBits(std::uint32_t b) { float f; std::memcpy(&f,&b,sizeof f); return f; }
}  // namespace

int main() {
    using namespace crew::runeblade;
    // marks: ours round-trip, the stock 1.0 and every other registry's range are not ours
    for(std::uint32_t n=1;n<kMarks;++n)
        Check(MarkOf(FromBits(MarkBits(static_cast<Mark>(n))))==static_cast<Mark>(n),"a mark reads back");
    Check(MarkOf(1.0f)==Mark::none,"stock 1.0 is no mark");
    Check(MarkOf(FromBits(kMarkBase))==Mark::none,"the base itself is no mark");
    Check(MarkOf(FromBits(kMarkBase|kMarks))==Mark::none,"past the last mark");
    Check(MarkOf(FromBits(0x3F801005u))==Mark::none,"a barrage mark is not ours");
    Check(MarkOf(FromBits(0x3F800E01u))==Mark::none,"the gravity gun's is not ours");
    Check(MarkOf(FromBits(0x3F8F4A36u))==Mark::none,"the Andryusha's is not ours");
    const float m=FromBits(MarkBits(Mark::carbine));
    Check(m>1.0f && m<1.001f,"a mark keeps the hit radius as good as 1.0");

    // the pierce limit
    PierceTable t;
    int a=0,b=0,c=0,d=0,r1=0,r2=0;
    Check(t.Hit(&r1,&a) && t.Hit(&r1,&b),"first two enemies: on");
    Check(t.Hit(&r1,&a),"an enemy it already hit: on (a hit sphere stays on a body several frames)");
    Check(!t.Hit(&r1,&c),"a third enemy: ended");
    Check(t.Hit(&r2,&c) && t.Hit(&r2,&d) && !t.Hit(&r2,&a),"rounds counted apart");
    t.Drop(&r1);
    Check(t.Find(&r1)==nullptr && t.Hit(&r1,&c),"a dropped round's entry is free, a new round at that address starts over");
    t.Clear();
    int rounds[kPierceRounds+1]={};
    bool full=false;
    for(int i=0;i<kPierceRounds;++i)Check(t.Hit(&rounds[i],&a,&full) && !full,"rounds up to the table");
    Check(t.Hit(&rounds[kPierceRounds],&a,&full) && full,"a round past the table goes through as the SGO says, said full");
    t.Drop(&rounds[0]);
    Check(t.Hit(&rounds[kPierceRounds],&a,&full) && !full,"a freed entry is used");
    std::printf("runeblade_weapons: %d checks passed\n",checks);
    return 0;
}
