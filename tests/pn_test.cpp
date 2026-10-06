// Proportional navigation (src/pn.h) flown offline against the stock pure pursuit at the same turn limit: the stock
// homing rounds keep their strength (their turn a frame, their thrust), only the law changes (src/guidance.cpp).
#include "../src/pn.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using crew::vec::Dot;using crew::vec::Len;
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}

// The stock type 1 (0x269AF0): the velocity turned at the target, at most `turn` rad a frame.
void Pursue(float* vel,const float* pos,const float* aim,float turn) {
    float los[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
    if(!crew::vec::Normalize(los))return;
    const float speed=Len(vel);
    float dir[3]={vel[0]/speed,vel[1]/speed,vel[2]/speed},out[3];
    crew::pn::Toward(dir,los,turn,out);
    for(int i=0;i<3;++i)vel[i]=out[i]*speed;
}

struct Run { int frames; float miss; float peak; };

// A type 1 round from the origin flying +z at `speed` m a frame, `turn` rad a frame, at a target starting at `target`
// moving `tv` a frame; PN (`pn` true, navigation 3) or pure pursuit. Flown until it passes within `hit` m or `most`
// frames; the closest it came, and the most it turned in a frame (rad).
Run Fly(bool pn,float speed,float turn,const float* target,const float* tv,float hit,int most) {
    float pos[3]={0.0f,0.0f,0.0f},vel[3]={0.0f,0.0f,speed},aim[3];
    std::memcpy(aim,target,12);
    Run run{most,1e9f,0.0f};
    for(int f=0;f<most;++f) {
        const float was[3]={vel[0]/speed,vel[1]/speed,vel[2]/speed};
        if(pn) {
            float a[3];
            crew::pn::Lateral(pos,vel,aim,tv,3.0f,speed*turn,a);
            Check(std::fabs(Dot(a,vel))<1e-3f*speed,"PN pushes across the flight only");
            Check(Len(a)<=speed*turn*1.0001f,"PN keeps within the stock turn");
            crew::pn::Turn(vel,a,speed);
        } else Pursue(vel,pos,aim,turn);
        const float now[3]={vel[0]/speed,vel[1]/speed,vel[2]/speed};
        const float turned=std::acos(crew::vec::Clamp(Dot(was,now),-1.0f,1.0f));
        if(turned>run.peak)run.peak=turned;
        for(int i=0;i<3;++i){pos[i]+=vel[i];aim[i]+=tv[i];}
        const float d[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
        const float dist=Len(d);
        if(dist<run.miss)run.miss=dist;
        if(dist<hit){run.frames=f+1;return run;}
    }
    return run;
}

void CrossingTarget() {
    // An Emerald-like round (top speed 2 m a frame, 0.075 rad a frame) at a flyer crossing at 1 m a frame 400 m out.
    const float target[3]={-150.0f,30.0f,400.0f},tv[3]={1.0f,0.0f,0.0f};
    const Run pn=Fly(true,2.0f,0.075f,target,tv,3.0f,1200);
    const Run chase=Fly(false,2.0f,0.075f,target,tv,3.0f,1200);
    std::printf("crossing: PN %d frames (closest %.1f m, peak turn %.3f), pursuit %d frames (closest %.1f m)\n",
                pn.frames,pn.miss,pn.peak,chase.frames,chase.miss);
    Check(pn.miss<3.0f,"PN hits a crossing target");
    Check(pn.peak<=0.075f*1.001f,"PN turns no harder than the stock round may");
    Check(pn.frames<chase.frames,"PN leads it: it hits sooner than the stock pursuit");
}

void TightCircle() {
    // The user's circling (2026-10-04): a slow round (0.4 m a frame, 0.01 rad: a 40 m turn) passing a target 25 m to
    // its side moving away at its own pace's half. Pursuit goes round; PN cuts in ahead and hits.
    const float target[3]={25.0f,0.0f,20.0f},tv[3]={0.0f,0.0f,0.2f};
    const Run pn=Fly(true,0.4f,0.01f,target,tv,2.0f,3000);
    const Run chase=Fly(false,0.4f,0.01f,target,tv,2.0f,3000);
    std::printf("close pass: PN %d frames (closest %.1f m), pursuit %d frames (closest %.1f m)\n",pn.frames,pn.miss,
                chase.frames,chase.miss);
    Check(pn.frames<=chase.frames,"PN is never slower to the target than pursuit");
}

void Thrust() {
    // Type 2: a nose 0.03 rad a frame (Tempest), its push 0.03 a frame.
    const float vel[3]={0.0f,0.0f,1.0f},nose[3]={0.0f,0.0f,1.0f};
    const float lat[3]={0.03f,0.0f,0.0f},none[3]={0.0f,0.0f,0.0f};
    float out[3];
    crew::pn::Thrust(vel,nose,lat,0.03f,0.03f,out);
    Check(std::fabs(Len(out)-1.0f)<1e-4f,"the nose is a unit vector");
    Check(std::acos(crew::vec::Clamp(Dot(out,nose),-1.0f,1.0f))<=0.03f+1e-4f,"the nose turns at most the stock turn");
    Check(out[0]>0.0f,"the nose turns the way PN asks");
    crew::pn::Thrust(vel,nose,none,0.03f,0.03f,out);
    Check(std::fabs(out[2]-1.0f)<1e-5f,"no lateral asked: the push is along the flight");
    // A full demand: all of the push across (the nose 90 degrees off its flight), reached a turn at a time.
    float cur[3]={0.0f,0.0f,1.0f};
    for(int f=0;f<200;++f){crew::pn::Thrust(vel,cur,lat,0.03f,0.03f,out);std::memcpy(cur,out,12);}
    Check(std::fabs(cur[0]-1.0f)<1e-3f,"all of the thrust across when PN asks for all of it");
}

void Lateral() {
    const float pos[3]={0.0f,0.0f,0.0f},vel[3]={0.0f,0.0f,2.0f};
    const float aim[3]={0.0f,0.0f,300.0f},still[3]={0.0f,0.0f,0.0f};
    float a[3];
    crew::pn::Lateral(pos,vel,aim,still,3.0f,1.0f,a);
    Check(Len(a)<1e-6f,"a target dead ahead asks for no turn");
    const float near[3]={0.5f,0.0f,0.2f};
    crew::pn::Lateral(pos,vel,near,still,3.0f,1.0f,a);
    Check(Len(a)==0.0f,"no line of sight within a metre: no turn");
    const float side[3]={50.0f,0.0f,300.0f},fast[3]={3.0f,0.0f,0.0f};
    crew::pn::Lateral(pos,vel,side,fast,3.0f,0.05f,a);
    Check(std::fabs(Len(a)-0.05f)<1e-5f && a[0]>0.0f,"a hard demand is limited, toward the target's way");
}
}  // namespace

int main() {
    Lateral();
    Thrust();
    CrossingTarget();
    TightCircle();
    std::printf("pn_test: %d checks passed\n",checks);
    return 0;
}
