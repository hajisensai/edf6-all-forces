// The frame step's wait for the next tick (src/frame_pacing_logic.h, frame_pacing.cpp TickWait) against the game's tick
// clock 0x11DAE60 written out here as the disassembly reads (docs/frame-pacing-re.md), offline:
//  - the period: freq*100/5994 is what the compiler's multiply by 0xAEF006D5604455C3 >> 76 gives, for the usual QPC
//    frequencies and random ones;
//  - the boundary: right after a boundary a whole period to wait, exactly on it and past it (one tick or many) none,
//    and on random states the clock gives 0 ticks one count before what TickWait waits for and 1 at it;
//  - QPC wrapping around: the start-up reading near 2^64 and now past 0, the boundary before and after the wrap;
//  - the due time in 100 ns: rounded up (never before the boundary), exact where the frequency divides, no overflow;
//  - the loop played out on a fake clock: each frame whose work ends inside a tick waits once and steps one tick.
// Exit code 1 when one fails. Built with offline_checks: cmake --build build --target frame_pacing_check && build\frame_pacing_check.exe
#include "../src/frame_pacing_logic.h"
#include <cstdint>
#include <cstdio>
#include <random>

namespace {
using namespace crew::pacing;
using u64=std::uint64_t;
int failures=0,checks=0;

void Expect(bool ok,const char* what,u64 a=0,u64 b=0) {
    ++checks;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%llu, %llu)\n",what,static_cast<unsigned long long>(a),static_cast<unsigned long long>(b));
}

// 0x11DAE81..0x11DAEA1: rcx = freq*100; rdx:rax = rcx*0xAEF006D5604455C3; r9 = rdx >> 12.
u64 GamePeriod(u64 freq) {
    u64 high=0;
    _umul128(freq*100u,0xAEF006D5604455C3ull,&high);
    return high>>12;
}

// 0x11DAE60 itself on a state {base, last}: returns the ticks and moves last the way the game does.
struct GameClock {
    u64 base,last,freq;
    u64 Count(u64 now) {
        const u64 period=GamePeriod(freq);
        const u64 n=(now-base-last)/period;
        if(n>=1)last+=n*period;
        return n;
    }
};

void Period() {
    for(u64 f:{10'000'000ull,3'579'545ull,2'441'406ull,1'000'000'000ull,14'318'180ull,24'000'000ull})
        Expect(TickPeriod(f)==GamePeriod(f),"the period is the game's at a usual frequency",TickPeriod(f),GamePeriod(f));
    Expect(TickPeriod(10'000'000)==166'833,"10 MHz: a tick is 166833 counts (1/59.94 s)",TickPeriod(10'000'000));
    std::mt19937_64 rng(7);
    for(int i=0;i<200000;++i){
        const u64 f=rng()>>(rng()%57+7);   // freq*100 must not wrap for the plain division to be the game's
        Expect(TickPeriod(f)==GamePeriod(f),"the period is the game's at a random frequency",f);
    }
}

void Boundary() {
    const u64 freq=10'000'000,period=TickPeriod(freq),base=123'456'789,last=40*period;
    const u64 edge=base+last+period;   // the next boundary
    Expect(CountsToBoundary(base+last,base,last,period)==period,"right after a boundary: a whole tick to wait");
    Expect(CountsToBoundary(base+last+1,base,last,period)==period-1,"one count after it");
    Expect(CountsToBoundary(edge-1,base,last,period)==1,"one count before the next");
    Expect(CountsToBoundary(edge,base,last,period)==0,"exactly on the boundary: no wait");
    Expect(TicksPassed(edge,base,last,period)==1,"exactly on the boundary the game counts one tick");
    Expect(CountsToBoundary(edge+5,base,last,period)==0,"past the boundary: no wait");
    Expect(CountsToBoundary(edge+3*period+7,base,last,period)==0,"several ticks past: no wait");
    Expect(TicksPassed(edge+3*period+7,base,last,period)==4,"several ticks past the game counts them all");
    Expect(CountsToBoundary(edge,base,last,0)==0 && TicksPassed(edge,base,last,0)==0,"no period: nothing to wait for");

    std::mt19937_64 rng(11);
    for(int i=0;i<200000;++i){
        const u64 f=1'000'000+rng()%2'000'000'000,p=TickPeriod(f);
        GameClock clock{rng(),(rng()%1'000'000)*p,f};   // any start-up reading (wrapping included), any boundary
        const u64 now=clock.base+clock.last+rng()%p;   // inside the tick: 0x11DAE60 just returned 0
        if(clock.Count(now)!=0){Expect(false,"inside the tick the game counts none",now);continue;}
        const u64 wait=CountsToBoundary(now,clock.base,clock.last,p);
        Expect(wait>=1 && wait<=p,"inside a tick: between one count and one tick",wait,p);
        GameClock early=clock;
        Expect(early.Count(now+wait-1)==0,"one count before the wait ends: still no tick",wait);
        Expect(clock.Count(now+wait)==1,"when the wait ends: one tick",wait);
        Expect(CountsToBoundary(now+wait,clock.base,clock.last,p)==p,"after the step: the next whole tick ahead");
    }
}

void Wrap() {
    const u64 freq=10'000'000,period=TickPeriod(freq);
    // Start-up just under 2^64: the boundary itself lies before the wrap, now after it.
    const u64 base=~u64{0}-period/2,last=0;
    const u64 now=base+period/4;   // still before the wrap
    Expect(now>base,"the reading before the wrap");
    Expect(CountsToBoundary(now,base,last,period)==period-period/4,"before the wrap: the rest of the tick");
    const u64 after=base+period-3;   // past 0: wrapped
    Expect(after<base,"the reading after the wrap");
    Expect(CountsToBoundary(after,base,last,period)==3,"across the wrap: three counts left");
    Expect(CountsToBoundary(base+period,base,last,period)==0 && TicksPassed(base+period,base,last,period)==1,
           "the boundary after the wrap is on time");
    // The boundary past the wrap and the game's own step across it.
    GameClock clock{u64{0}-10*period+period/2,9*period,freq};   // the next boundary at period/2 past 0
    const u64 inTick=clock.base+clock.last+period/3;   // before the wrap
    const u64 wait=CountsToBoundary(inTick,clock.base,clock.last,period);
    Expect(inTick+wait<inTick,"the boundary lies past the wrap");
    Expect(clock.Count(inTick+wait-1)==0 && clock.Count(inTick+wait)==1,"the game steps exactly there across the wrap");
}

void DueTime() {
    Expect(CountsTo100ns(0,10'000'000)==0,"no counts: no wait");
    Expect(CountsTo100ns(1,10'000'000)==1,"10 MHz: a count is 100 ns");
    Expect(CountsTo100ns(166'833,10'000'000)==166'833,"10 MHz: a tick is its counts");
    Expect(CountsTo100ns(1,3'000'000)==4,"3 MHz: a count rounded up to 400 ns",CountsTo100ns(1,3'000'000));
    Expect(CountsTo100ns(3,3'000'000)==10,"3 MHz: three counts exactly 1 us");
    Expect(CountsTo100ns(1,1'000'000'000)==1,"1 GHz: a count still waits 100 ns");
    Expect(CountsTo100ns(~u64{0},1)==UINT64_MAX,"a quotient past 64 bits is the most there is");
    std::mt19937_64 rng(13);
    for(int i=0;i<200000;++i){
        const u64 f=1+rng()%4'000'000'000ull,c=1+rng()%(TickPeriod(f)+1);
        const u64 due=CountsTo100ns(c,f);
        // due*f >= c*10^7 > (due-1)*f, in 128 bits.
        u64 h1=0,h2=0,h3=0;
        const u64 l1=_umul128(due,f,&h1),l2=_umul128(c,10'000'000u,&h2),l3=_umul128(due-1,f,&h3);
        const bool notEarly=h1>h2 || (h1==h2 && l1>=l2),tight=h3<h2 || (h3==h2 && l3<l2);
        Expect(notEarly,"the due time never ends before the boundary",c,f);
        Expect(tight,"the due time is the least that does",c,f);
    }
}

// The frame step on a fake clock: work, `while(!(n=0x11DAE60())) TickWait();`, with the timer ending `late` after due.
void Loop() {
    for(u64 freq:{10'000'000ull,3'579'545ull,1'000'000'000ull}){
        const u64 period=TickPeriod(freq);
        GameClock clock{~u64{0}-50*period,0,freq};   // wraps during the run
        u64 now=clock.base;
        std::mt19937_64 rng(freq);
        int waits=0,frames=0,ticks=0;
        for(int frame=0;frame<5000;++frame){
            now+=rng()%(period-freq/10'000-2);   // the frame's work: ends inside the tick after the last wait's lateness
            u64 n=clock.Count(now);
            while(!n){
                const u64 due=CountsTo100ns(CountsToBoundary(now,clock.base,clock.last,period),freq);
                // The timer: due*100 ns as counts (rounded up as the kernel's clock would reach it), then a latency.
                u64 h=0;const u64 l=_umul128(due,freq,&h);u64 r=0;
                u64 counts=_udiv128(h,l,10'000'000u,&r);
                counts+=(r?1u:0u)+rng()%(freq/10'000);   // up to 0.1 ms late
                now+=counts;
                ++waits;
                n=clock.Count(now);
            }
            ++frames;ticks+=static_cast<int>(n);
        }
        Expect(waits==frames,"each frame inside a tick waits exactly once",static_cast<u64>(waits),static_cast<u64>(frames));
        Expect(ticks==frames,"and steps exactly one tick",static_cast<u64>(ticks),static_cast<u64>(frames));
    }
}
}  // namespace

int main() {
    Period();
    Boundary();
    Wrap();
    DueTime();
    Loop();
    if(failures){std::printf("frame pacing: %d of %d checks failed\n",failures,checks);return 1;}
    std::printf("frame pacing: %d checks ok\n",checks);
    return 0;
}
