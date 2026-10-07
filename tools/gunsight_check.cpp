// A vehicle gun's range ladder (src/gunsight.h) against the round flown as the game flies it (roundaim.h), offline:
//  - a tank cannon's round (900 m/s, the world's gravity, 3 s of life): 500 m steps, each tick level that far from
//    the muzzle and on the round's own path (the per-frame model at that moment), each lower than the one before;
//  - the machine gun (slower, shorter life): finer steps, never more than kMostTicks, none past the round's reach;
//  - a lofted round: ticks only up to its farthest point, none on the way back;
//  - the shooter's motion the round takes a share of moves its ticks with it;
//  - no speed, no life, fired straight up: no ladder;
//  - the sight's magnification (src/sightzoom.h): the field of view the camera is set to (the stock pi/4 at 1x, a third
//    of it at 3x, the soldier's own zoom on top), the same however often it is set (set, not scaled), never wider
//    than the stock view for a value that is not a number or under 1; the turret camera's rate slowed by as much.
// Exit code 1 when one fails. cmake --build build --target gunsight_check && build\gunsight_check.exe
#include "../src/gunsight.h"
#include "../src/sightzoom.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew;
int failures=0;

void Expect(bool ok,const char* what,float a=0.0f,float b=0.0f) {
    std::printf("%s  %s (%.3f, %.3f)\n",ok ? "ok  " : "FAIL",what,a,b);
    if(!ok)++failures;
}

constexpr float kGravity=-9.8f/3600.0f;   // m/frame^2: the world's gravity a frame (AmmoGravityFactor 1)

roundaim::Round RoundOf(float speedMs,float factor,int alive) {
    return roundaim::Round{speedMs/60.0f,{0.0f,kGravity*factor,0.0f},0.5f,alive};
}

// Where the round is (per-frame model) when it has gone `d` m level: the frame it gets there and the one before,
// straight between them.
void PathAt(const roundaim::Round& r,const float* pos,const float* dir,const float* shooter,float d,float* out) {
    float vel[3];roundaim::LaunchVel(r,dir,shooter,vel);
    float prev[3]={pos[0],pos[1],pos[2]};
    for(int n=1;n<=r.alive;++n) {
        float p[3];roundaim::At(r,pos,vel,static_cast<float>(n),p);
        const float a=gunsight::Level(pos,prev),b=gunsight::Level(pos,p);
        if(b>=d) {
            const float u=(d-a)/(b-a);
            for(int i=0;i<3;++i)out[i]=prev[i]+(p[i]-prev[i])*u;
            return;
        }
        for(int i=0;i<3;++i)prev[i]=p[i];
    }
    for(int i=0;i<3;++i)out[i]=prev[i];
}

void OnPath(const char* what,const roundaim::Round& r,const float* pos,const float* dir,const float* shooter) {
    const gunsight::Ladder l=gunsight::Of(r,pos,dir,shooter);
    bool level=true,path=true,falls=true;
    float worst=0.0f;
    for(int k=0;k<l.ticks;++k) {
        level=level && std::fabs(gunsight::Level(pos,l.at[k])-l.range[k])<0.01f && std::fabs(l.range[k]-(k+1)*l.step)<0.001f;
        float want[3];PathAt(r,pos,dir,shooter,l.range[k],want);
        const float off=std::sqrt((want[0]-l.at[k][0])*(want[0]-l.at[k][0])+(want[1]-l.at[k][1])*(want[1]-l.at[k][1])+
                                  (want[2]-l.at[k][2])*(want[2]-l.at[k][2]));
        if(off>worst)worst=off;
        path=path && off<0.01f;
        if(k>0)falls=falls && l.at[k][1]<l.at[k-1][1];
    }
    char line[160];
    std::snprintf(line,sizeof(line),"%s: %d ticks of %.0f m, each that far level",what,l.ticks,l.step);
    Expect(l.ticks>0 && level,line,static_cast<float>(l.ticks),l.step);
    std::snprintf(line,sizeof(line),"%s: every tick on the round's path (worst, m)",what);
    Expect(path,line,worst);
    std::snprintf(line,sizeof(line),"%s: each tick under the one before",what);
    Expect(falls,line);
}
}  // namespace

int main() {
    const float pos[3]={100.0f,20.0f,-50.0f},still[3]={0.0f,0.0f,0.0f};
    float flat[3]={0.6f,0.0f,0.8f};
    // The tank cannon: 900 m/s, 3 s: 2700 m, 500 m steps, 5 ticks.
    const roundaim::Round cannon=RoundOf(900.0f,1.0f,180);
    OnPath("cannon",cannon,pos,flat,still);
    {
        const gunsight::Ladder l=gunsight::Of(cannon,pos,flat,still);
        Expect(l.step==500.0f && l.ticks==5,"cannon: 500 m steps to 2500 m",l.step,static_cast<float>(l.ticks));
        // Its drop at 2000 m, against the closed form: t = 2000 / 900 s, drop g t^2 / 2 (the per-frame sum a hair more).
        const float t=2000.0f/900.0f,closed=0.5f*9.8f*t*t;
        const float drop=pos[1]-l.at[3][1];
        Expect(drop>closed*0.98f && drop<closed*1.04f,"cannon: drop at 2000 m as the parabola's (m, closed form)",drop,closed);
    }
    // The machine gun: 600 m/s, 1.5 s: 900 m, 200 m steps, 4 ticks.
    const roundaim::Round mg=RoundOf(600.0f,1.0f,90);
    OnPath("mg",mg,pos,flat,still);
    {
        const gunsight::Ladder l=gunsight::Of(mg,pos,flat,still);
        Expect(l.step==200.0f && l.ticks==4,"mg: 200 m steps to 800 m",l.step,static_cast<float>(l.ticks));
    }
    // Never more than kMostTicks, never past kMostReach: a very long-lived fast round.
    {
        const gunsight::Ladder l=gunsight::Of(RoundOf(1500.0f,0.2f,600),pos,flat,still);
        Expect(l.ticks<=gunsight::kMostTicks && l.range[l.ticks-1]<=gunsight::kMostReach,"long round: within the ticks and the reach",
               static_cast<float>(l.ticks),l.range[l.ticks-1]);
    }
    // Lofted 45 degrees, slow: ticks only to its farthest point.
    {
        float up[3]={0.6f*0.7071f,0.7071f,0.8f*0.7071f};
        const roundaim::Round lob=RoundOf(120.0f,1.0f,1200);
        const gunsight::Ladder l=gunsight::Of(lob,pos,up,still);
        float far=0.0f;
        float vel[3];roundaim::LaunchVel(lob,up,still,vel);
        for(int n=1;n<=lob.alive;++n){float p[3];roundaim::At(lob,pos,vel,static_cast<float>(n),p);const float d=gunsight::Level(pos,p);if(d>far)far=d;}
        bool rising=true;
        for(int k=1;k<l.ticks;++k)rising=rising && l.range[k]>l.range[k-1];
        Expect(l.ticks>0 && l.range[l.ticks-1]<=far && rising,"lofted: ticks up to its farthest only (last tick, farthest)",
               l.ticks ? l.range[l.ticks-1] : 0.0f,far);
    }
    // The shooter's motion: driving forward at 20 m/s, the round keeps half of it (ownerMove 0.5): ticks still on its path.
    {
        const float moving[3]={12.0f,0.0f,16.0f};
        OnPath("cannon from a moving tank",cannon,pos,flat,moving);
    }
    // No ladder.
    Expect(gunsight::Of(RoundOf(0.0f,1.0f,180),pos,flat,still).ticks==0,"no speed: no ladder");
    Expect(gunsight::Of(RoundOf(900.0f,1.0f,0),pos,flat,still).ticks==0,"no life: no ladder");
    {
        float straightUp[3]={0.0f,1.0f,0.0f};
        Expect(gunsight::Of(cannon,pos,straightUp,still).ticks==0,"fired straight up: no ladder");
    }
    Expect(gunsight::StepFor(0.0f)==0.0f && gunsight::StepFor(250.0f)==50.0f && gunsight::StepFor(1200.0f)==200.0f &&
           gunsight::StepFor(1e6f)==500.0f,"steps: none, 50, 200, and the reach held to kMostReach");
    {
        using namespace crew::sightzoom;
        const float pi4=0.785398163f;
        Expect(std::fabs(Fov(1.0f,1.0f)-pi4)<1e-6f && std::fabs(Fov(1.0f,3.0f)-pi4/3.0f)<1e-6f && std::fabs(Fov(1.0f,6.0f)-pi4/6.0f)<1e-6f,
               "zoom: the field of view at 1x, 3x, 6x",Fov(1.0f,3.0f),pi4/3.0f);
        Expect(std::fabs(Fov(5.5f,3.0f)-pi4/16.5f)<1e-6f,"zoom: the soldier's own zoom on top",Fov(5.5f,3.0f),pi4/16.5f);
        float fov=Fov(1.0f,6.0f);
        for(int i=0;i<100;++i)fov=Fov(1.0f,6.0f);   // a hundred frames: set, never compounding
        Expect(fov==Fov(1.0f,6.0f),"zoom: the same after a hundred frames",fov,Fov(1.0f,6.0f));
        Expect(Fov(std::nanf(""),std::nanf(""))==pi4 && Fov(0.2f,0.5f)==pi4 && Fov(1.0f,-3.0f)==pi4,"zoom: nonsense is the stock view");
        Expect(std::fabs(Rate(0.03f,3.0f)-0.01f)<1e-7f && Rate(0.03f,1.0f)==0.03f && Rate(0.03f,std::nanf(""))==0.03f,
               "zoom: the turret camera turns slower by as much",Rate(0.03f,3.0f),0.01f);
        Expect(At(0)==1.0f && At(Next(0))==3.0f && At(Next(Next(0)))==6.0f && At(Next(Next(Next(0))))==1.0f,"zoom: 1x -> 3x -> 6x -> 1x");
    }
    std::printf(failures ? "gunsight_check: %d FAILED\n" : "gunsight_check: all ok\n",failures);
    return failures ? 1 : 0;
}
