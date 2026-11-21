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
    // The hit's area damage and blast sound follow the arming, and a round with no blast keeps its flags.
    constexpr std::uint32_t stock=0x610;   // the Apocalypse missile's flags as the stock init leaves them (the log)
    Check(kHitSound==0x400 && kAreaOnHit==0x10,"the stock bits: 0x400 the hit's sound (0x2325CD), 0x10 its area damage (0x232616)");
    Check(HitFlags(stock,true,false)==0x200,"unarmed: its hit is single-target (0x10 off) and silent (0x400 off)");
    Check(HitFlags(0x200,true,true)==0x610,"armed again: its hit bursts with its blast sound");
    Check(HitFlags(stock,true,true)==stock,"armed from the pod: the stock flags as they were");
    Check(HitFlags(0x600,false,true)==0x600 && HitFlags(0x600,false,false)==0x600,"no blast: the flags are left as they are");
    Check(HitFlags(stock|0x20,true,false)==0x220,"other flags (blast on expiry, 0x200) are untouched");
    // The hit's fireball (drawn at a fifth of core+0xA20, 0x26A8C1): none for a dud, the blast's own armed.
    Check(HitFireball(blast,true,false)==0.0f,"unarmed: no fireball where it strikes (2026-10-26: the stock one, sized by its 12 m blast)");
    Check(HitFireball(blast,true,true)==blast,"armed: the stock fireball, sized by its blast");
    Check(HitFireball(0.0f,false,false)==0.0f && HitFireball(0.0f,false,true)==0.0f,"no blast: left as it is");
    // A whole flight: unarmed out of the pod, armed past its own tank, the fireball back with the arming.
    std::uint32_t flags=stock;float fireball=blast;
    const float* const flight[]={early,clear};
    for(const float* at:flight) {
        const bool armed=Armed(at,blast,boxes,2);
        flags=HitFlags(flags,true,armed);fireball=HitFireball(blast,true,armed);
        if(at==early)Check(flags==0x200 && fireball==0.0f,"by its own tank: a dud (no area damage, sound or fireball)");
        else Check(flags==stock && fireball==blast,"clear of it: the stock burst, sound and fireball");
    }
    std::printf("fuze: %d checks passed\n",checks);
    return 0;
}
