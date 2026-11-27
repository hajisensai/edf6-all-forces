// memoepoch::Clock (src/memo_epoch.h), the key of VerdictOf's per-frame memo, and memoepoch::Table, the memo itself:
// offline, no game or EDF.dll needed.
#include "../src/memo_epoch.h"
#include <cstdint>
#include <cstdio>
#include <vector>
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
    {   // Table: every key of a frame kept (bench:4000, 2026-11-28: a 4096-slot direct-mapped memo lost a third of 4000
        // targets to each other, asked again and again, and the parallel Decide's peeks found none of them).
        memoepoch::Table<int> t;
        std::vector<unsigned char> objects(4000*0x2000+16);
        const auto key=[&](int i){ return static_cast<const void*>(objects.data()+static_cast<std::size_t>(i)*0x2000); };
        Check(!t.Find(key(0),1),"an empty table finds nothing");
        for(int i=0;i<4000;++i)t.Make(key(i),7)=i+1;
        int kept=0;
        for(int i=0;i<4000;++i)if(const int* v=t.Find(key(i),7);v && *v==i+1)++kept;
        Check(kept==4000,"4000 objects 0x2000 apart: every one kept in its frame");
        Check(t.Size()==4000 && t.Capacity()>=8000,"the table grows to keep half its cells free");
        Check(t.Make(key(5),7)==6,"Make of a kept key: its value");
        Check(!t.Find(key(5),8),"another epoch's ask finds none of the live one's");
        Check(t.Make(key(5),8)==0 && t.Size()==1,"a new epoch: the old one's keys gone, a new value");
        Check(!t.Find(key(6),8),"a new epoch: an old key not found");
        Check(!t.Find(key(5),7),"the old epoch: not found once a newer is live");
        t.Reserve(10000);
        Check(t.Capacity()>=20002 && t.Find(key(5),8) && t.Size()==1,"Reserve grows, keeping the live keys");
        t.Clear();
        Check(!t.Find(key(5),8) && t.Size()==0,"Clear forgets every key");
        Check(!t.Find(nullptr,8),"a null key not found");
    }
    std::printf("memo_epoch_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
