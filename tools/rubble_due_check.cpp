// The rubble scan's throttle (src/rubble_due.h) offline: each chassis scans at most once per kRubbleScanMs however many
// chassis step, and as often as that; the 8-slot LRU it replaced is the negative control (20 chassis: every call due).
//   cmake --build build --target rubble_due_check && build\rubble_due_check.exe      (exit code 1 on a failure)
#include "../src/rubble_due.h"
#include <cstdio>
#include <vector>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,long long a=0,long long b=0) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL: %s (%lld, %lld)\n",what,a,b);}
}

struct Lru { const void* wrapper; std::uint64_t at; };
bool LruDue(Lru (&slots)[8],const void* wrapper,std::uint64_t now) {
    Lru* slot=&slots[0];
    for(auto& s:slots) {
        if(s.wrapper==wrapper){slot=&s;break;}
        if(s.at<slot->at)slot=&s;
    }
    if(slot->wrapper==wrapper && now-slot->at<rubble::kRubbleScanMs)return false;
    *slot=Lru{wrapper,now};
    return true;
}

// `n` chassis each stepped every 16 ms frame for `ms`: the scans of each.
template<class DueFn> std::vector<int> Run(int n,std::uint64_t ms,DueFn due) {
    std::vector<unsigned char> objs(static_cast<std::size_t>(n)*0x40);
    std::vector<int> scans(static_cast<std::size_t>(n));
    for(std::uint64_t t=3600000;t<3600000+ms;t+=16)
        for(int i=0;i<n;++i)scans[i]+=due(objs.data()+static_cast<std::size_t>(i)*0x40,t);
    return scans;
}
}  // namespace

int main() {
    using namespace rubble;
    {
        static Table t{};
        const unsigned char a[16]{},b[16]{};
        Check(Due(t,a,1000),"first step: due");
        Check(!Due(t,a,1000+kRubbleScanMs-1),"within the throttle: not due");
        Check(Due(t,b,1001),"another chassis keeps its own throttle");
        Check(Due(t,a,1000+kRubbleScanMs),"the throttle over: due");
        Check(!Due(t,a,1000+kRubbleScanMs+1),"and held again");
    }
    // 1 to 900 chassis, 10 s: every one scans at most and at least once per kRubbleScanMs of 16 ms frames.
    for(int n:{1,8,9,20,100,900}) {
        static Table t;t=Table{};
        const auto scans=Run(n,10000,[&](const void* w,std::uint64_t now){return Due(t,w,now);});
        const int each=static_cast<int>((10000+kRubbleScanMs-1)/kRubbleScanMs);   // 40; 16 ms steps make it 256 ms: 40 again
        int bad=0;
        for(int i=0;i<n;++i)bad+=scans[i]<each*15/16 || scans[i]>each;
        Check(bad==0,"every chassis scans once per throttle, however many step",n,bad);
    }
    // The negative control: the old 8-slot LRU with 20 chassis scans every step (625 frames).
    {
        Lru slots[8]{};
        const auto scans=Run(20,10000,[&](const void* w,std::uint64_t now){return LruDue(slots,w,now);});
        Check(scans[0]==625,"the 8-slot LRU with 20 chassis: every step scans",scans[0]);
    }
    // Stale slots taken over: 3000 chassis come and go (100 at a time), the table never cleared; each new one due at
    // once, then held.
    {
        static Table t{};
        std::vector<unsigned char> objs(3000*0x40);
        std::uint64_t now=3600000;
        int wrong=0;
        for(int wave=0;wave<30;++wave) {
            for(int i=0;i<100;++i)wrong+=!Due(t,objs.data()+(wave*100+i)*0x40,now);
            now+=16;
            for(int i=0;i<100;++i)wrong+=Due(t,objs.data()+(wave*100+i)*0x40,now);
            now+=kRubbleScanMs;
        }
        Check(wrong==0,"chassis coming and going: due once, then held",wrong);
    }
    // More than kSlots chassis within kRubbleScanMs (the fallback: the home slot taken from a held chassis): every
    // newcomer still scans, the table keeps kSlots distinct chassis, and a second pass within the throttle scans only
    // the ones that fell out, not all of them. Each rescan takes a held chassis's home slot, so a few more fall out
    // in turn (measured 139 of 1100 for 76 over): bounded by twice the overflow, not every step as the 8-slot LRU.
    {
        static Table t{};
        constexpr int n=1100;
        std::vector<unsigned char> objs(n*0x40);
        const std::uint64_t now=3600000;
        int first=0;
        for(int i=0;i<n;++i)first+=Due(t,objs.data()+i*0x40,now);
        Check(first==n,"over kSlots chassis: every one due on its first step",first);
        int held=0,dup=0;
        for(int k=0;k<kSlots;++k) {
            held+=t.slot[k].wrapper!=nullptr && t.slot[k].at==now;
            for(int j=k+1;j<kSlots;++j)dup+=t.slot[k].wrapper && t.slot[k].wrapper==t.slot[j].wrapper;
        }
        Check(held==kSlots && dup==0,"over kSlots chassis: the table holds kSlots distinct ones",held,dup);
        int again=0;
        for(int i=0;i<n;++i)again+=Due(t,objs.data()+i*0x40,now+16);
        std::printf("  over kSlots: second pass %d of %d due\n",again,n);
        Check(again>0 && again<=2*(n-kSlots),"over kSlots chassis: within the throttle only the evicted rescan",again);
    }
    std::printf("rubble_due_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
