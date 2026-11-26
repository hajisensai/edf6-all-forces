// Offline check of a jet's escort of the player it follows (src/jet_escort.h): the player's motion measured, the slot
// behind a fast player held, the racetrack round a slow one flown by a jet that can only turn so tight and never flies
// under its least speed. Not part of the plugin build:
// cmake --build build --target jet_escort_check && build\jet_escort_check.exe
#include "../src/jet_escort.h"
#include <cmath>
#include <cstdio>

using namespace jetescort;

namespace {
int fails=0,checks=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++checks;
    if(ok)return;
    ++fails;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
bool Near(float a,float b,float tol) { return std::fabs(a-b)<=tol; }
float Horiz(const float* a,const float* b) { return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[2]-b[2])*(a[2]-b[2])); }

// A strike jet's (jet_internal.h kKinds: least 85, cruise 190, attack 215 m/s; ring step 120 m), its turn at the
// loiter speed 250 m.
constexpr Params kStrike{85.0f,190.0f,215.0f,250.0f,120.0f};

// A point-mass jet: flies at the speed asked (changed at most kAccel a second), turned toward the way asked at most
// its speed over its turn radius a second.
struct Plane { float pos[3]; float head; float speed; };
constexpr float kAccel=30.0f;
void Step(Plane& j,const Params& p,const Steer& s,float dt) {
    const float want=std::atan2(s.dir[0],s.dir[2]);
    float d=want-j.head;
    while(d>3.14159265f)d-=6.2831853f;
    while(d<-3.14159265f)d+=6.2831853f;
    const float rate=j.speed/p.turn*dt;
    j.head+=d>rate ? rate : d<-rate ? -rate : d;
    const float dv=s.speed-j.speed,most=kAccel*dt;
    j.speed+=dv>most ? most : dv<-most ? -most : dv;
    j.pos[0]+=std::sin(j.head)*j.speed*dt;
    j.pos[2]+=std::cos(j.head)*j.speed*dt;
}

void Observes() {
    Track t{};
    float at[3]={0.0f,0.0f,0.0f};
    Observe(t,at,0);
    Check(t.set && t.vel[0]==0.0f && t.fwd[1]==1.0f,"the first sample: at rest, facing +z");
    for(std::uint64_t ms=16;ms<=4000;ms+=16){at[0]=8.0f*static_cast<float>(ms)*0.001f;Observe(t,at,ms);}
    Check(Near(t.vel[0],8.0f,0.3f) && Near(t.vel[1],0.0f,0.05f),"a steady 8 m/s measured",t.vel[0],t.vel[1]);
    Check(Near(t.fwd[0],1.0f,1e-3f),"its heading taken from its motion",t.fwd[0]);
    at[0]+=400.0f;Observe(t,at,4100);
    Check(t.vel[0]==0.0f && t.vel[1]==0.0f,"a warp: no speed",t.vel[0]);
    Check(Near(t.fwd[0],1.0f,1e-3f),"its heading kept over the warp",t.fwd[0]);
    Observe(t,at,9000);
    Check(t.vel[0]==0.0f,"a gap of seconds: no speed",t.vel[0]);
    // Standing: the heading kept.
    for(std::uint64_t ms=9100;ms<=12000;ms+=16)Observe(t,at,ms);
    Check(Speed2(t.vel)<0.01f && Near(t.fwd[0],1.0f,1e-3f),"standing keeps its heading",t.fwd[0]);
    // A reset (the clock gone back, a new mission) keeps the heading too, at rest.
    Observe(t,at,100);
    Check(t.vel[0]==0.0f && Near(t.fwd[0],1.0f,1e-3f),"started over facing the way it last went");
}

void Slots() {
    const float a[3]={0.0f,50.0f,0.0f},f[2]={0.0f,1.0f};
    float s0[3],s1[3],s2[3];
    SlotOf(a,f,0,s0);SlotOf(a,f,1,s1);SlotOf(a,f,2,s2);
    Check(Near(s0[2],-kSlotBack,1e-3f) && Near(s1[2],-kSlotBack,1e-3f),"the first pair kSlotBack behind",s0[2],s1[2]);
    Check(s0[0]*s1[0]<0.0f && Near(std::fabs(s0[0]),kSlotSide,1e-3f),"one each side",s0[0],s1[0]);
    Check(s2[2]<s0[2] && std::fabs(s2[0])>std::fabs(s0[0]),"the next pair farther back and out",s2[2],s2[0]);
    Check(s0[1]==50.0f,"its height the caller's");
    // Hysteresis: in over kSlotIn of the least speed, out only under kSlotOut.
    Track t{{0,0,0},{0.0f,0.0f},{0.0f,1.0f},0,true,false};
    const float pos[3]={0.0f,0.0f,-500.0f};
    t.vel[1]=kStrike.minSpeed*1.1f;
    Check(!Fly(t,kStrike,pos,a,0).slot,"under kSlotIn: the racetrack");
    t.vel[1]=kStrike.minSpeed*1.3f;
    Check(Fly(t,kStrike,pos,a,0).slot,"over kSlotIn: the slot");
    t.vel[1]=kStrike.minSpeed*1.1f;
    Check(Fly(t,kStrike,pos,a,0).slot,"between: still the slot");
    t.vel[1]=kStrike.minSpeed*1.0f;
    Check(!Fly(t,kStrike,pos,a,0).slot,"under kSlotOut: the racetrack again");
}

// A player flying a jet at `speed` along +z, turning gently after a while: the escort, set off 600 m behind, catches
// up and holds its slot, at their speed flying straight.
void HoldsSlot(float speed) {
    Track t{};
    Plane j{{-300.0f,0.0f,-600.0f},0.0f,kStrike.minSpeed*kLoiter};
    float player[3]={0.0f,0.0f,0.0f};
    float head=0.0f,worst=0.0f,straight=0.0f;
    const float dt=1.0f/60.0f;
    std::uint64_t ms=0;
    bool slow=false;
    for(int i=0;i<60*150;++i) {
        ms+=16;
        if(i>60*90)head+=0.1f*dt;   // a gentle turn from 90 s on (0.1 rad/s)
        if(i==60*88)straight=j.speed;
        player[0]+=std::sin(head)*speed*dt;player[2]+=std::cos(head)*speed*dt;
        Observe(t,player,ms);
        const Steer s=Fly(t,kStrike,j.pos,player,0);
        if(s.speed<kStrike.minSpeed-1e-3f)slow=true;
        Step(j,kStrike,s,dt);
        if(i>60*60) {
            float slot[3];
            SlotOf(player,t.fwd,0,slot);
            const float d=Horiz(j.pos,slot);
            if(d>worst)worst=d;
        }
    }
    Check(!slow,"never asked under its least speed",speed);
    Check(worst<80.0f,"its slot held",speed,worst);
    Check(Near(straight,speed,3.0f),"at the player's speed",speed,straight);
}

// A player on foot (or driving) at `speed`, turning at `turn` rad/s after a while, then standing: the escort flies the
// racetrack round them, never under its loiter speed, never far, never off round where they stood.
void Racetrack(float speed,float turn) {
    Track t{};
    Plane j{{1200.0f,0.0f,-600.0f},1.0f,kStrike.minSpeed*kLoiter};
    float player[3]={0.0f,0.0f,0.0f};
    float head=0.0f,worst=0.0f,inMin=1e9f,inMax=0.0f;
    const float dt=1.0f/60.0f;
    std::uint64_t ms=0;
    bool slow=false,slotted=false;
    float along,across;
    TrackSize(kStrike,0,&along,&across);
    for(int i=0;i<60*300;++i) {
        ms+=16;
        const bool walking=i<60*200;
        if(walking && i>60*100)head+=turn*dt;
        if(walking){player[0]+=std::sin(head)*speed*dt;player[2]+=std::cos(head)*speed*dt;}
        Observe(t,player,ms);
        const Steer s=Fly(t,kStrike,j.pos,player,0);
        if(s.speed<LoiterSpeed(kStrike)-1e-3f)slow=true;
        if(s.slot)slotted=true;
        Step(j,kStrike,s,dt);
        if(i>60*60) {
            const float d=Horiz(j.pos,player);
            if(d>worst)worst=d;
        }
        if(i>60*240) {   // standing a minute: on the ellipse round them
            const float cx=player[0]+t.vel[0]*kTrackLead,cz=player[2]+t.vel[1]*kTrackLead;
            const float dx=j.pos[0]-cx,dz=j.pos[2]-cz;
            const float u=dx*t.fwd[0]+dz*t.fwd[1],w=-dx*t.fwd[1]+dz*t.fwd[0];
            const float e=std::sqrt((u/along)*(u/along)+(w/across)*(w/across));
            if(e<inMin)inMin=e;
            if(e>inMax)inMax=e;
        }
    }
    Check(!slow,"never under its loiter speed",speed);
    Check(!slotted,"a slow player: no slot",speed);
    Check(worst<along*1.6f+speed*kTrackLead,"kept near the player",speed,worst);
    Check(inMin>0.75f && inMax<1.25f,"on its racetrack round the standing player",inMin,inMax);
}

// The racetrack moves with the player: walked off a kilometre, the jet's mean place over a lap follows.
void Drifts() {
    Track t{};
    Plane j{{0.0f,0.0f,-800.0f},1.5f,kStrike.minSpeed*kLoiter};
    float player[3]={0.0f,0.0f,0.0f};
    const float dt=1.0f/60.0f,speed=6.0f;
    std::uint64_t ms=0;
    double sx=0.0,sz=0.0;int n=0;
    for(int i=0;i<60*240;++i) {
        ms+=16;
        player[2]+=speed*dt;
        Observe(t,player,ms);
        Step(j,kStrike,Fly(t,kStrike,j.pos,player,0),dt);
        if(i>60*180){sx+=j.pos[0];sz+=j.pos[2];++n;}
    }
    const float mean[3]={static_cast<float>(sx/n),0.0f,static_cast<float>(sz/n)};
    const float mid[3]={0.0f,0.0f,player[2]-speed*30.0f};   // the player's mean place over the last minute
    Check(Horiz(mean,mid)<speed*kTrackLead+250.0f,"the racetrack went along with them",Horiz(mean,mid),player[2]);
}

void Wings() {
    float a0,b0,a1,b1;
    TrackSize(kStrike,0,&a0,&b0);TrackSize(kStrike,1,&a1,&b1);
    Check(b0>=kTrackLeast && Near(a0,b0*kLong,1e-3f),"its racetrack's size",a0,b0);
    Check(Near(b1-b0,kStrike.step,1e-3f),"each wing a ring step out",b1-b0);
    // The racetrack's tightest bend (its ends: b^2/a) no tighter than the jet turns at its loiter speed.
    Check(b0*b0/a0>=kStrike.turn-1.0f,"its ends flyable",b0*b0/a0,kStrike.turn);
    // A degenerate place (right on the centre) still steers somewhere, at the loiter speed at least.
    Track t{{0,0,0},{0.0f,0.0f},{0.0f,1.0f},0,true,false};
    const float at[3]={0.0f,0.0f,0.0f};
    const Steer s=Fly(t,kStrike,at,at,0);
    Check(std::isfinite(s.dir[0]) && std::isfinite(s.dir[2]) && Near(s.dir[0]*s.dir[0]+s.dir[2]*s.dir[2],1.0f,1e-3f),"on the centre: a way");
    Check(s.speed>=LoiterSpeed(kStrike),"on the centre: flying",s.speed);
}
}  // namespace

int main() {
    Observes();
    Slots();
    HoldsSlot(130.0f);
    HoldsSlot(200.0f);
    Racetrack(0.0f,0.0f);
    Racetrack(6.0f,0.05f);
    Racetrack(25.0f,0.03f);
    Racetrack(60.0f,0.02f);
    Drifts();
    Wings();
    if(fails){std::printf("jet_escort_check: %d of %d failed\n",fails,checks);return 1;}
    std::printf("jet_escort_check: all %d ok\n",checks);
    return 0;
}
