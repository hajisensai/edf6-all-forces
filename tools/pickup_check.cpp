// The squad's box sweep (src/pickup.h) checked offline:
//  - spread out: one soldier a box, one box a soldier; four soldiers by four boxes each take the one in front of them,
//    not all the nearest; more boxes than soldiers: every soldier busy, the rest left for later; more soldiers than
//    boxes: the nearest ones go;
//  - health boxes: never with NpcPickupHealth off, never online, never for a soldier at full health; a hurt one goes;
//    a full soldier still takes the weapon box next to the health box;
//  - range: a box past it (from the player) is left alone; a box at a nonsense position too;
//  - an unknown kind is left alone; the heal shares are the game's (0.15, 0.30).
//   cmake --build build --target pickup_check && build\pickup_check.exe      (exit code 1 on a failure)
#include "../src/pickup.h"
#include <cstdio>

namespace {
using namespace npc::pickup;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
Rules Open() { return Rules{true,false,80.0f,{0.0f,0.0f,0.0f}}; }
Picker Full(float x,float z) { return Picker{{x,0.0f,z},1000.0f,1000.0f}; }
Picker Hurt(float x,float z) { return Picker{{x,0.0f,z},400.0f,1000.0f}; }

void Spread() {
    // Four soldiers in a row, four boxes 20 m in front of each: each its own.
    Picker p[4];Box b[4];
    for(int i=0;i<4;++i){p[i]=Full(static_cast<float>(i)*10.0f,0.0f);b[i]=Box{{static_cast<float>(i)*10.0f,0.0f,20.0f},kWeapon};}
    int out[4];
    Check(Assign(b,4,p,4,Open(),out)==4,"four soldiers, four boxes: all given");
    bool own=true;
    for(int i=0;i<4;++i)own=own && out[i]==i;
    Check(own,"each takes the box in front of it",out[0],out[3]);
    // One box between them all: only the nearest goes.
    const Box one{{11.0f,0.0f,5.0f},kArmour};
    Check(Assign(&one,1,p,4,Open(),out)==1 && out[1]==0 && out[0]<0 && out[2]<0 && out[3]<0,"one box: the nearest soldier alone",out[1]);
    // Six boxes, two soldiers: both busy, each a different box.
    Box six[6];
    for(int i=0;i<6;++i)six[i]=Box{{static_cast<float>(i)*5.0f,0.0f,3.0f},i%2 ? kArmour : kWeapon};
    Check(Assign(six,6,p,2,Open(),out)==2 && out[0]>=0 && out[1]>=0 && out[0]!=out[1],"more boxes than soldiers: all busy, apart",out[0],out[1]);
}

void Health() {
    const Box heal{{5.0f,0.0f,0.0f},kHealBig},small{{5.0f,0.0f,0.0f},kHealSmall};
    const Picker full=Full(0.0f,0.0f),hurt=Hurt(0.0f,0.0f);
    int out[2];
    Rules off=Open();off.health=false;
    Check(Assign(&heal,1,&hurt,1,off,out)==0,"health boxes off: a hurt soldier leaves it");
    Rules online=Open();online.online=true;
    Check(Assign(&heal,1,&hurt,1,online,out)==0,"online: a hurt soldier leaves it");
    Check(Assign(&heal,1,&full,1,Open(),out)==0 && Assign(&small,1,&full,1,Open(),out)==0,"full health: left for the player");
    Check(Assign(&heal,1,&hurt,1,Open(),out)==1 && out[0]==0,"hurt, allowed, alone: it goes");
    // A full soldier nearer the health box, a hurt one farther: the hurt one goes for it.
    const Picker two[2]={Full(4.0f,0.0f),Hurt(-20.0f,0.0f)};
    Check(Assign(&heal,1,two,2,Open(),out)==1 && out[0]<0 && out[1]==0,"the hurt one goes, not the nearer full one",out[0],out[1]);
    // A full soldier, a health box and a weapon box: it takes the weapon box.
    const Box pair[2]={heal,Box{{8.0f,0.0f,0.0f},kWeapon}};
    Check(Assign(pair,2,&full,1,Open(),out)==1 && out[0]==1,"full: the weapon box beside the health box",out[0]);
    Check(HealShare(kHealSmall)==0.15f && HealShare(kHealBig)==0.30f && HealShare(kWeapon)==0.0f,"the heal shares");
    Picker dead=Hurt(0.0f,0.0f);dead.hpMax=0.0f;
    Check(Assign(&heal,1,&dead,1,Open(),out)==0,"no full health read: no health box");
}

void Range() {
    const Picker p=Full(0.0f,0.0f);
    int out[1];
    const Box far{{0.0f,0.0f,81.0f},kWeapon},near{{0.0f,0.0f,79.0f},kWeapon};
    Check(Assign(&far,1,&p,1,Open(),out)==0,"past the range from the player: left");
    Check(Assign(&near,1,&p,1,Open(),out)==1,"within it: taken");
    Rules away=Open();away.centre[0]=200.0f;
    Check(Assign(&near,1,&p,1,away,out)==0,"the range is the player's, not the soldier's");
    const Box nan{{std::nanf(""),0.0f,1.0f},kWeapon},odd{{1.0f,0.0f,1.0f},7};
    Check(Assign(&nan,1,&p,1,Open(),out)==0 && Assign(&odd,1,&p,1,Open(),out)==0,"nonsense position, unknown kind: left");
}
}  // namespace

int main() {
    Spread();
    Health();
    Range();
    std::printf(failures ? "pickup_check: %d of %d FAILED\n" : "pickup_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
