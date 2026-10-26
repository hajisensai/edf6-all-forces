// The plugin missiles' warhead safety (src/fuze.h) offline, on the case the user met in game (2026-10-26, the test range,
// the Apocalypse locked on a target 12.5 m off): the proximity fuse went off 6.8 m from the tank, 7 frames out of the pod,
// and its 12 m blast took 175 off the tank every shot.
#include "../src/fuze.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}
}  // namespace

int main() {
    using namespace crew::fuze;
    // The Apocalypse at (-272, 1, 184): world boxes for its hull and turret round where it stood (approximate: the hull's
    // 6.6 x 3.0 x 12.5 m box, models/wt_apocalypse collision `body`, turned off the axes makes a world box about this size).
    const float boxes[2][6]={{-279.8f,1.1f,176.2f,-264.2f,4.2f,191.8f},{-274.2f,3.0f,181.0f,-269.8f,4.9f,187.2f}};
    const float blast=12.0f;
    // Where the fuse went off (the log: 6.8 m from the origin, 2.8 m out of the pod) and where the next two burst (22 m off,
    // the tank untouched).
    const float early[3]={-276.22f,3.84f,180.27f},clear[3]={-288.7f,2.7f,158.3f};
    Check(!Armed(early,blast,boxes,2),"7 frames out of the pod its burst reaches its own hull: unarmed");
    Check(Armed(clear,blast,boxes,2),"22 m off, its blast clear of every box: armed");
    // The query is the stock AABB of radius `blast`: a box corner inside the cube but outside the sphere still counts
    // (the stock query is a box), so the hold is never short of what the blast takes.
    const float corner[3]={-279.8f-11.0f,4.2f+11.0f,176.2f-11.0f};
    Check(Reaches(corner,blast,boxes[0]),"a box corner 19 m off on the diagonal is in the 12 m box query");
    Check(!Armed(corner,blast,boxes,2),"so the warhead stays unarmed there");
    // Nothing to burst, or no launcher left: nothing to hold.
    Check(Armed(early,0.0f,boxes,2),"a round with no blast is never held");
    Check(Armed(early,blast,boxes,0),"a round whose launcher is gone is armed");
    // The hit's area damage follows the arming, and a round with no blast keeps its flags.
    constexpr std::uint32_t stock=0x610;   // the Apocalypse missile's flags as the stock init leaves them (the log)
    Check(HitFlags(stock,true,false)==0x600,"unarmed: its hit is single-target (0x10 off)");
    Check(HitFlags(0x600,true,true)==0x610,"armed again: its hit bursts");
    Check(HitFlags(0x600,false,true)==0x600,"no blast: the flags are left as they are");
    Check(HitFlags(stock|0x20,true,false)==0x620,"other flags (blast on expiry) are untouched");
    std::printf("fuze: %d checks passed\n",checks);
    return 0;
}
