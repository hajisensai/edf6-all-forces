// The route guide's field corrections (src/route_guide.h): every guide the game makes, as each caller makes it.
#include "../src/route_guide.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
using namespace routeguide;
// What the constructor leaves, the InitParam each caller builds (docs/route-guide-re.md §1): 15 m / 7 m drops,
// the curve, 0.04 smoothing (0.08 with the global 0x20 flag).
Fields Stock(std::uint32_t mode,bool target) { return {mode,target,15.0f,7.0f,false,0.04f}; }
bool Along(const Fields& f) {
    return f.polyline && f.smoothing==1.0f && f.nearDrop<=kKeepCorners && f.mergeDrop<=kKeepCorners;
}
}  // namespace

int main() {
    const Fields bvm=Corrected(Stock(kModeNone,true));
    Check(bvm.mode==kModeObject,"the EDF5 scripts' guide follows the object they named (mode 0 never sets a goal)");
    Check(Along(bvm),"...along the route's corners");
    Check(Corrected(Stock(kModeNone,false)).mode==kModeNone,"no object named: nothing to follow, left as made");
    const Fields area=Corrected(Stock(kModeArea,false));
    Check(area.mode==kModeArea && Along(area),"DispRouteGuideToArea keeps its area, drawn along the corners");
    const Fields object=Corrected(Stock(kModeObject,true));
    Check(object.mode==kModeObject && Along(object),"DispRouteGuideToObject keeps its object");
    Fields script={kModeArea,false,0.5f,0.25f,true,0.08f};
    const Fields kept=Corrected(script);
    Check(kept.nearDrop==0.5f && kept.mergeDrop==0.25f,"a script's drops already under a metre are its own");
    script.nearDrop=std::numeric_limits<float>::quiet_NaN();
    script.mergeDrop=std::numeric_limits<float>::infinity();
    const Fields odd=Corrected(script);
    Check(odd.nearDrop==kKeepCorners && odd.mergeDrop==kKeepCorners,"a NaN or endless drop is no drop of every corner");
    Check(Corrected(Corrected(Stock(kModeNone,true))).mode==kModeObject,"correcting twice changes nothing more");
    std::printf("route_guide_test: %d checks passed\n",checks);
    return 0;
}
