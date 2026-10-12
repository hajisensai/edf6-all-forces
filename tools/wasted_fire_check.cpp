// Fire that cannot hurt its target (src/wasted_fire.h), offline: a shooter whose hits on a target keep hurting nothing
// (a teleportation ship's hull, its hatch's damage window shut) holds its fire at it for a while and tries again; one hit
// that hurts clears it; another shooter (under the open hatch) is not held by the first one's hits; the table's room.
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
}  // namespace

int main() {
    Run();
    std::printf("wasted_fire_check: %d cases, %d failed\n",cases,failures);
    return failures ? 1 : 0;
}
