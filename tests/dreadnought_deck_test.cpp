// The Dreadnought's hangar rules (src/dreadnought_deck.h; src/dreadnought_hangar.inc, src/dreadnought.cpp):
//  - its hangar modules fitted per socket (the user, 2026-10-24: 「比如两个机库模块，就应该两个机库」): a fitting has the
//    bays of its fitted modules and no others, its escort flights and sorties come out of those alone;
//  - a player boards from the hangar they stand over (OverHangar), out of one of that hangar's own bays (BoardBay);
//  - a fighter's pilot gets out without a parachute (StandsAboard) only when its own bay's hangar, running on this machine,
//    holds it at that bay's mouth putting them out (BayJob::alight). Near, over or flying through the hull's box, on the
//    way home, thrown off, on a machine whose hangar is another's, or unmeasured: they eject (review 2026-10-24: the old
//    test, the hull's box alone, left a pilot falling with no parachute).
#include "../src/dreadnought_deck.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}
int main() {
    using namespace dreadnought;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    constexpr unsigned kLeftFore=1u,kLeftAft=2u,kRightFore=4u,kRightAft=8u;

    // Fitted per socket: module m holds bays 2m, 2m + 1, and nothing of a module not fitted
    static_assert(FittedBays(kAllHangars)==0xFFu,"all four fitted: the original's eight bays");
    Check(FittedBays(0u)==0u,"none fitted: no bay");
    Check(FittedBays(kLeftFore|kRightFore)==0x33u,"left fore + right fore: bays 0, 1 and 4, 5 only (2 hangars, 4 bays)");
    Check(FittedBays(kLeftAft|kRightAft)==0xCCu,"left aft + right aft: bays 2, 3 and 6, 7 only");
    for(int i=0;i<kBays;++i)Check(HangarOfBay(i)==i/2 && KindOfBay(i)==i%4,"bay i: module i / 2, kind i % 4");

    // Escort flights: one of each kind its fitted, docked bays have (a kind that could not be made left out)
    Check(EscortKinds(kAllHangars,0xFFu,0u)==0xFu,"all fitted and docked: every kind");
    Check(EscortKinds(kLeftFore|kRightFore,0xFFu,0u)==0x3u,"left fore + right fore: fighters and defenders only");
    Check(EscortKinds(kLeftFore|kRightFore,0xCCu,0u)==0u,"docked bits of bays not fitted give nothing");
    Check(EscortKinds(kLeftAft,0xFFu,0u)==0xCu,"left aft: bombers and interceptors");
    Check(EscortKinds(kAllHangars,0xFFu,0x1u)==0xEu,"a kind that failed this mission: not asked");
    Check(EscortKinds(kAllHangars,0xFEu,0u)==0xFu,"bay 0 out: the fighter of bay 4 goes");
    Check(EscortKinds(kAllHangars,0xEEu,0u)==0xEu,"both fighters out (bays 0, 4): no fighter, the others go");
    Check(EscortKinds(0u,0xFFu,0u)==0u,"no hangar fitted: no escort flight");

    // A player's sortie: out of a bay of the hangar they stand over, its first docked one
    Check(BoardBay(kAllHangars,0xFFu,0u,0)==0,"over left fore: bay 0");
    Check(BoardBay(kAllHangars,0xFFu,0u,3)==6,"over right aft: bay 6");
    Check(BoardBay(kAllHangars,0xFEu,0u,0)==1,"over left fore, bay 0 out: bay 1 (still its own hangar)");
    Check(BoardBay(kAllHangars,0xFCu,0u,0)==-1,"over left fore, both its bays out: none (never another hangar's)");
    Check(BoardBay(kAllHangars,0xFFu,0x1u,0)==1,"its first bay's kind failed: its second");
    Check(BoardBay(kLeftFore|kRightFore,0xFFu,0u,1)==-1,"over a socket not fitted: no hangar there");
    Check(BoardBay(kLeftFore|kRightFore,0xFFu,0u,2)==4,"over right fore, fitted: bay 4");
    for(int m=0;m<kHangars;++m)
        for(unsigned docked=0;docked<256u;++docked) {
            const int i=BoardBay(kAllHangars,docked,0u,m);
            Check(i<0 || (HangarOfBay(i)==m && ((docked>>i)&1u)),"a sortie leaves from a docked bay of the hangar pressed over");
        }
    Check(BoardBay(kAllHangars,0xFFu,0u,-1)==-1 && BoardBay(kAllHangars,0xFFu,0u,kHangars)==-1,"no such hangar");

    // Standing over a hangar (ship frame from its roof's centre: across, up, along)
    static_assert(OverHangar(0.0f,1.0f,0.0f),"on its roof");
    Check(OverHangar(25.0f,0.8f,-47.0f),"on its roof's corner (the module 51.7 x 95.6 m)");
    Check(OverHangar(-20.0f,11.0f,10.0f),"on the hull's deck over its inboard edge");
    Check(!OverHangar(kHangarAcross+0.5f,1.0f,0.0f),"beyond it across");
    Check(!OverHangar(0.0f,1.0f,kHangarAlong+0.5f),"beyond it along (the next hangar's, or none)");
    Check(!OverHangar(0.0f,-kHangarBelow-0.5f,0.0f),"under its roof (in it, or below the ship)");
    Check(!OverHangar(0.0f,kHangarAbove+0.5f,0.0f),"high over it (flying, on the turrets)");
    Check(!OverHangar(nan,1.0f,0.0f) && !OverHangar(0.0f,nan,0.0f) && !OverHangar(0.0f,1.0f,nan),"unmeasured: no");
    // the four modules' origins 96.5 m apart along and 139.5 m across (the model, tools/make_dreadnought.py): one at most
    Check(2.0f*kHangarAlong<96.5f && 2.0f*kHangarAcross<139.5f,"over one hangar at most");

    // No parachute only when its own bay takes it in, its pilot put out there
    static_assert(StandsAboard(true,true,0.0f,0.0f),"held at its bay's mouth, its pilot put out");
    Check(StandsAboard(true,true,1.4f,3.9f),"just touched its mouth (the hangar's touch: 1.5 m, kFinalSpeed / 3)");
    Check(StandsAboard(true,true,kOnSpot,kOnSpotSpeed),"at the limits");
    Check(!StandsAboard(true,true,kOnSpot+0.1f,0.0f),"beside its mouth: in the air off the hull");
    Check(!StandsAboard(true,false,0.0f,0.0f),"at its mouth but not being taken in (out for a sortie, thrown off it)");
    Check(!StandsAboard(true,false,40.0f,0.5f),"on its way home (recover), not yet at its mouth");
    Check(!StandsAboard(true,false,2.0f,90.0f),"thrown off its mouth");
    Check(!StandsAboard(true,true,0.5f,kOnSpotSpeed+0.5f),"moving off its mouth at speed");
    Check(!StandsAboard(true,false,0.0f,0.0f),"over the hull, its hangar not taking it in (flying through its box)");
    Check(!StandsAboard(false,true,0.0f,0.0f),"its hangar runs on another machine: nothing here puts them aboard");
    Check(!StandsAboard(true,true,nan,0.0f),"no distance");
    Check(!StandsAboard(true,true,0.0f,nan),"no speed (no motion of its flight)");
    Check(!StandsAboard(true,true,-1.0f,0.0f),"a negative distance");
    Check(!StandsAboard(true,true,0.0f,1.0e9f),"Relative's 'unknown'");
    std::printf("dreadnought_deck: %d checks passed\n",checks);
    return 0;
}
