// UfoMother301's rest poses (src/edf41mother_pose.h): the hull holds its closed pose and the cannons their folded one until the script
// enables them, as 4.1 shows them; the parts that already rest as 4.1 are left alone. No game needed.
#include "../src/edf41mother_pose.h"

#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
using namespace crew::edf41pose;
}  // namespace

int main() {
    // the two classes whose EDF6 constructor starts a moving clip 4.1 never plays at spawn
    Check(RestHold(Group::kCannon)==Hold::kFirstKeyStill,"a cannon holds appear's first key (folded), not its end (unfolded)");
    Check(RestHold(Group::kHull)==Hold::kLastKey,"the hull holds close's last key (closed), not its first (open)");
    // the rest already rest as 4.1
    Check(RestHold(Group::kBigCannon)==Hold::kNone,"the main cannon: its default is the still close_state (4.1 0x256950)");
    Check(RestHold(Group::kHatch)==Hold::kNone,"a hatch: its default is the still close_state (4.1 0x25BB00)");
    Check(RestHold(Group::kCover)==Hold::kNone,"a cover: as a hatch");
    Check(RestHold(Group::kPanel)==Hold::kNone,"a panel: the borrowed hatch CAS moves none of its bones");

    // times and speeds
    Check(HoldTime(Hold::kFirstKeyStill,558.56f)==0.0f,"folded: time 0");
    Check(HoldSpeed(Hold::kFirstKeyStill,1.0f)==0.0f,"folded: stopped, or appear would unfold it in 9 s");
    Check(HoldTime(Hold::kLastKey,987.0f)==987.0f,"closed: the clip's end");
    Check(HoldSpeed(Hold::kLastKey,1.0f)==1.0f,"closed: a clip that does not loop stays on its last key at any speed");
    Check(HoldTime(Hold::kLastKey,-1.0f)==0.0f,"no clip length (-1, 0x116C2B3): nothing past the start");
    Check(HoldTime(Hold::kNone,987.0f)==0.0f && HoldSpeed(Hold::kNone,1.0f)==1.0f,"no hold: untouched values");

    // once, and only before 4.1's class plays a state of its own
    Check(HoldNow(Group::kCannon,false,true),"a dormant cannon's first frame: held");
    Check(!HoldNow(Group::kCannon,true,true),"held once");
    Check(!HoldNow(Group::kCannon,false,false),"a cannon the script enabled before its first frame: appear plays");
    Check(HoldNow(Group::kHull,false,true),"the hull's first frame, closed: held");
    Check(!HoldNow(Group::kHull,false,false),"the hull already opening (ObjectAction 3 on M603's creation frame): open plays");
    Check(!HoldNow(Group::kBigCannon,false,true),"the main cannon: nothing to hold");

    // appear afterwards (4.1 0x259D20 writes the speed 1.0 itself)
    Check(kAppearTime==0.0f && kAppearSpeed==1.0f,"appear from its start at speed 1");

    std::printf("edf41mother_pose: %d checks passed\n",checks);
    return 0;
}
