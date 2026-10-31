// Fire that cannot hurt its target (src/wasted_fire.h), offline: a shooter whose hits on a target keep hurting nothing
// (a teleportation ship's hull, its hatch's damage window shut) holds its fire at it for a while and tries again; one hit
// that hurts clears it; another shooter (under the open hatch) is not held by the first one's hits; the table's room;
// the WASTEDFIRE plan lines bounded (a shooter and target kind every 3 s, a few a second in all).
//   cmake --build build --target wasted_fire_check && build\wasted_fire_check.exe      (exit code 1 on a failure)
#include "../src/wasted_fire.h"
#include <cstdio>

using namespace crew::wastedfire;

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}

int ship=0,other=0,gunner=0,soldier=0;   // stand-in objects
Book book{};

void Run() {
    std::uint64_t ms=1000;
    // A soldier's rifle on the shut ship's hull: three hits hurt nothing, it still fires; the fourth: held.
    for(int i=0;i<kWastedHits-1;++i)Hit(book,&soldier,&ship,false,ms+=100);
    Check(!Wasted(book,&soldier,&ship,ms),"three hits that hurt nothing: still fires");
    Hit(book,&soldier,&ship,false,ms+=100);
    Check(Wasted(book,&soldier,&ship,ms),"four hits that hurt nothing: its fire held");
    // Hits that go on while it is held (the player's own fire: nothing holds it) start no new hold.
    bool again=false;
    for(int i=0;i<3*kWastedHits;++i)again=Hit(book,&soldier,&ship,false,ms+1+static_cast<std::uint64_t>(i)) || again;
    Check(!again,"held: hits meanwhile start no new hold (one log line a hold)");
    Check(!Wasted(book,&gunner,&ship,ms),"another shooter is not held by its hits");
    Check(!Wasted(book,&soldier,&other,ms),"its fire at another target is not held");
    // Held for kWastedHoldMs, then it tries again.
    Check(Wasted(book,&soldier,&ship,ms+kWastedHoldMs-1),"held until the hold runs out");
    ms+=kWastedHoldMs;
    Check(!Wasted(book,&soldier,&ship,ms),"after the hold: it tries again");
    // Trying again on the still-shut hull: held again after another kWastedHits.
    for(int i=0;i<kWastedHits;++i)Hit(book,&soldier,&ship,false,ms+=100);
    Check(Wasted(book,&soldier,&ship,ms),"still nothing: held again");
    // The hatch opens and a hit on the core hurts: cleared at once.
    Hit(book,&soldier,&ship,true,ms+=100);
    Check(!Wasted(book,&soldier,&ship,ms),"a hit that hurts clears it at once");
    // Hits that hurt in between keep it firing: alternating never holds.
    for(int i=0;i<20;++i)Hit(book,&gunner,&ship,(i%3)==0,ms+=100);
    Check(!Wasted(book,&gunner,&ship,ms),"hits that hurt every few: never held");
    // Long apart, the futile hits do not add up (kForgetMs).
    for(int i=0;i<kWastedHits;++i){ms+=kForgetMs+1;Hit(book,&soldier,&other,false,ms);}
    Check(!Wasted(book,&soldier,&other,ms),"hits far apart: the count starts over each time");
    // A hit that hurts with no record makes none (the table is for the futile ones).
    Hit(book,&gunner,&other,true,ms);
    Check(Find(book,&gunner,&other,false)==nullptr,"a hurting hit with no record makes none");
    // Nobody's or its own hit: nothing.
    Hit(book,nullptr,&ship,false,ms);Hit(book,&ship,&ship,false,ms);
    Check(Find(book,nullptr,&ship,false)==nullptr && Find(book,&ship,&ship,false)==nullptr,"no shooter / itself: not booked");
    // The table full: the least recently hit pair goes, the newest stays.
    static int many[kPairs+8];
    for(int i=0;i<kPairs+8;++i)for(int k=0;k<kWastedHits;++k)Hit(book,&many[i],&ship,false,ms+=10);
    Check(Wasted(book,&many[kPairs+7],&ship,ms),"full: the newest pair kept");
    Check(Find(book,&many[0],&ship,false)==nullptr,"full: the least recently hit pair gone");
}

// The WASTEDFIRE plan lines (planlog): a moving fortress's hundred parts under a squad's fire, planned every frame, give
// one line a shooter and target kind every kEveryMs and at most kPerSecond lines in any second; the line says how many
// plans it stands for. (2026-10-31: a line a part rotated the 32 MB log within seconds.)
void PlanLog() {
    using namespace crew::planlog;
    constexpr int kFortress=6,kShip=3,kParts=200,kSquad=30,kFrames=625;   // 10 s at 16 ms
    static Gate gate{};
    static int squad[kSquad];
    static int lines[kSquad][8];
    static int perSecond[16];
    long long plans=0,stood=0;
    bool over=false;
    std::uint64_t ms=5000;
    for(int f=0;f<kFrames;++f,ms+=16)
        for(int s=0;s<kSquad;++s)
            for(int t=0;t<kParts;++t) {
                const int kind=t<kParts-4 ? kFortress : kShip;   // a few ships among the parts
                ++plans;
                int quiet=-1;
                if(!Pass(gate,&squad[s],kind,ms,&quiet))continue;
                ++lines[s][kind];stood+=1+quiet;
                const int sec=static_cast<int>((ms-5000)/1000);
                if(++perSecond[sec]>kPerSecond)over=true;
            }
    Check(!over,"fortress under a squad: at most kPerSecond lines in any second");
    int total=0;bool perKey=true;
    for(int s=0;s<kSquad;++s)for(int k=0;k<8;++k) {
        total+=lines[s][k];
        if(lines[s][k]>static_cast<int>(kFrames*16/kEveryMs)+1)perKey=false;
    }
    Check(perKey,"fortress under a squad: a shooter and kind at most one line every kEveryMs");
    Check(total>0 && total<=11*kPerSecond,"fortress under a squad: a few dozen lines in 10 s, not a line a part");
    Check(stood<=plans,"the lines' plan counts are plans that happened");

    // One shooter alone: its first plan logged at once, then one line every kEveryMs counting the plans between.
    static Gate one{};
    int a=0,q=-1,got=0,lastQuiet=-1;
    std::uint64_t t=100,firstAt=0,secondAt=0;
    for(int i=0;i<500;++i,t+=16)
        for(int p=0;p<kParts;++p)
            if(Pass(one,&a,kFortress,t,&q)){if(!got)firstAt=t;else if(got==1){secondAt=t;lastQuiet=q;}++got;}
    Check(firstAt==100,"one shooter: its first plan logged at once (also before game ms 3000)");
    Check(secondAt-firstAt>=kEveryMs && secondAt-firstAt<kEveryMs+16,"one shooter: the next line kEveryMs later");
    Check(lastQuiet==static_cast<int>((secondAt-firstAt)/16)*kParts-1,"one shooter: the line counts the plans it stood for");
    // Another kind for the same shooter: a line of its own.
    Check(Pass(one,&a,kShip,t,&q),"same shooter, another kind of target: its own line");
    // More shooters than the table holds, each new to it: still the per-second cap.
    static Gate many{};
    static int crowd[kKeys*4];
    int burst=0;
    for(int i=0;i<kKeys*4;++i)burst+=Pass(many,&crowd[i],kFortress,9000,&q);
    Check(burst==kPerSecond,"more shooters than the table: kPerSecond lines that second");
    Check(Pass(many,&crowd[0],kFortress,10000,&q),"the next second: lines again");
}
}  // namespace

int main() {
    Run();
    PlanLog();
    std::printf("wasted_fire_check: %d cases, %d failed\n",cases,failures);
    return failures ? 1 : 0;
}
