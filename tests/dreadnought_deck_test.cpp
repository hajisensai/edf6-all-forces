// When a Dreadnought fighter's pilot gets out without a parachute (src/dreadnought_deck.h StandsAboard, used by
// src/dreadnought.cpp DreadnoughtTakesBack for playerjet.cpp Leave / playerjet_board.inc Left): only on its foredeck
// spot, held there by the hangar running on this machine, moving with the ship. Near, over or flying through the hull's
// box, on a machine whose hangar is another's, or unmeasured: they eject (review 2026-10-24: the old test, the hull's box
// alone, left a pilot falling with no parachute).
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
    using dreadnought::StandsAboard;
    using dreadnought::kOnSpot;
    using dreadnought::kOnSpotSpeed;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    static_assert(StandsAboard(true,true,0.0f,0.0f),"parked on its spot");
    Check(StandsAboard(true,true,1.4f,3.9f),"just touched down (the hangar's touch: 1.5 m, kFinalSpeed / 3)");
    Check(StandsAboard(true,true,kOnSpot,kOnSpotSpeed),"at the limits");
    Check(!StandsAboard(true,true,kOnSpot+0.1f,0.0f),"beside its spot: off the deck it may be");
    Check(!StandsAboard(true,true,40.0f,0.5f),"over the deck, still coming down (homeLow, 25 m over it)");
    Check(!StandsAboard(true,true,0.5f,kOnSpotSpeed+0.5f),"on its bow run: leaving the deck at speed");
    Check(!StandsAboard(true,true,2.0f,90.0f),"thrown off the bow");
    Check(!StandsAboard(true,false,0.0f,0.0f),"over the hull but no foredeck fighter of its hangar (flying through its box)");
    Check(!StandsAboard(false,true,0.0f,0.0f),"its hangar runs on another machine: nothing here puts them aboard");
    Check(!StandsAboard(true,true,nan,0.0f),"no distance");
    Check(!StandsAboard(true,true,0.0f,nan),"no speed (no motion of its flight)");
    Check(!StandsAboard(true,true,-1.0f,0.0f),"a negative distance");
    Check(!StandsAboard(true,true,0.0f,1.0e9f),"Relative's 'unknown'");
    std::printf("dreadnought_deck: %d checks passed\n",checks);
    return 0;
}
