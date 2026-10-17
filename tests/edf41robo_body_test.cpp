// UfoRobo's death is final (src/edf41robo_body.h): after Die only S10 -> S11, whatever asks for another state. No game needed.
#include "../src/edf41robo_body.h"

#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
using crew::edf41robo::BodyStepAllowed;
using crew::edf41robo::kDying;
using crew::edf41robo::kWreck;
}  // namespace

int main() {
    // alive: every living state, never dying / the wreck without Die
    for(int from=0;from<kDying;++from)
        for(int to=0;to<kDying;++to)Check(BodyStepAllowed(from,false,to),"alive: a living state is allowed");
    for(int from=0;from<kDying;++from) {
        Check(!BodyStepAllowed(from,false,kDying),"alive: dying only through Die (dead first)");
        Check(!BodyStepAllowed(from,false,kWreck),"alive: never straight to the wreck");
    }
    // Die: dead, then into dying from any living state, and nothing else
    for(int from=0;from<kDying;++from) {
        Check(BodyStepAllowed(from,true,kDying),"Die: into dying");
        for(int to=0;to<kDying;++to)Check(!BodyStepAllowed(from,true,to),"dead: no living state again");
        Check(!BodyStepAllowed(from,true,kWreck),"dead: the wreck comes after dying");
    }
    // dying: only on to the wreck (the host's S7 / S8 / S9 on a guest copy that died first is refused; S10 again would
    // restart the dying clock)
    for(int to=0;to<=kWreck;++to)Check(BodyStepAllowed(kDying,true,to)==(to==kWreck),"dying: only the wreck");
    for(int to=0;to<=kWreck;++to)Check(BodyStepAllowed(kDying,false,to)==(to==kWreck),"dying (dead flag lost): only the wreck");
    // the wreck: final
    for(int to=0;to<=kWreck;++to) {
        Check(!BodyStepAllowed(kWreck,true,to),"the wreck is final");
        Check(!BodyStepAllowed(kWreck,false,to),"the wreck is final (dead flag lost)");
    }
    // out of range
    Check(!BodyStepAllowed(0,false,-1) && !BodyStepAllowed(0,false,kWreck+1),"no state out of range");
    std::printf("edf41robo_body: %d checks OK\n",checks);
    return 0;
}
