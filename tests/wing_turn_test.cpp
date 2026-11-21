// A wing's velocity steered as a wing steers it (src/wing_turn.h; dreadnought_hangar.inc FlyTo bringing a fighter home):
// test hub #42 (「凭空出现速度的平移」): its speed changes only along its path, its path turns at most `turn` across it, and
// nothing pushes it sideways. Against the law it replaced (the whole velocity closed on the wanted one at `accel` in any
// direction, the nose sent toward the goal), which slid a fighter called home across its nose. The hangar's whole flight
// home (its capture and nose) against the same: tests/hangar_recover_test.cpp.
#include "../src/wing_turn.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
using crew::wing::Len3;
float Dot(const float* a,const float* b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
// The fighter's sideways velocity against the nose a body following its last frame's path would have (m/s): what the
// player sees as a slide. The body's nose here is the last path (it follows the path, kHoldGainJet, at best a frame late).
float Slide(const float* was,const float* now) {
    const float l=Len3(was);
    if(l<1e-3f)return 0.0f;
    const float n[3]={was[0]/l,was[1]/l,was[2]/l},along=Dot(now,n);
    const float s[3]={now[0]-n[0]*along,now[1]-n[1]*along,now[2]-n[2]*along};
    return Len3(s);
}
}

int main() {
    constexpr float dt=1.0f/60.0f,accel=22.0f,turn=30.0f,kPi=3.14159265f;
    // From rest it sets off along the wanted velocity at its acceleration, no more.
    {
        float vel[3]={0,0,0};const float want[3]={0,0,50};float lat[3];
        crew::wing::Step(vel,want,accel,turn,dt,lat);
        Check(std::fabs(vel[2]-accel*dt)<1e-4f && std::fabs(vel[0])<1e-6f && Len3(lat)<1e-4f,"from rest: off along want at accel");
    }
    // Called home flying the other way at 160 m/s: the path turns round at `turn`, the speed along it at `accel`; never a
    // velocity across the path larger than one frame's turn, never a jump.
    {
        float vel[3]={160,0,0},want[3]={-70,0,0};
        float worstSlide=0.0f,worstLat=0.0f,worstSpeedStep=0.0f;
        int frames=0;
        for(;frames<60*60;++frames) {
            const float was[3]={vel[0],vel[1],vel[2]};
            float lat[3];
            crew::wing::Step(vel,want,accel,turn,dt,lat);
            worstSlide=std::fmax(worstSlide,Slide(was,vel));
            worstLat=std::fmax(worstLat,Len3(lat));
            worstSpeedStep=std::fmax(worstSpeedStep,std::fabs(Len3(vel)-Len3(was)));
            const float d[3]={vel[0]-want[0],vel[1]-want[1],vel[2]-want[2]};
            if(Len3(d)<0.01f)break;
        }
        Check(frames<60*60,"it comes round onto the wanted velocity");
        Check(worstLat<=turn*1.001f,"its path turns at most `turn` m/s^2 across it");
        Check(worstSpeedStep<=accel*dt*1.001f,"its speed changes at most `accel` along its path");
        // A frame's turn at `turn` m/s^2 moves the velocity sideways by turn*dt against the last path: that and no more.
        Check(worstSlide<=turn*dt*1.001f,"no slide: the velocity across the last path is a frame's turn at most");
    }
    // A wanted velocity across the path (the replaced law slid the fighter sideways onto it at 22 m/s^2 with its nose
    // still the old way): the new one turns the path, the speed held, the bank toward the turn.
    {
        float vel[3]={0,0,100};const float want[3]={100,0,0};
        float lat[3];
        crew::wing::Step(vel,want,accel,turn,dt,lat);
        const float angle=std::atan2(vel[0],vel[2]);
        Check(std::fabs(angle-turn*dt/100.0f)<1e-4f,"a frame's turn is turn * dt / speed rad");
        Check(std::fabs(Len3(vel)-100.0f)<1e-3f,"the speed held through a level turn to the same speed");
        Check(lat[0]>0.0f && std::fabs(lat[1])<1e-3f,"its lift (the bank) toward the turn, level");
    }
    // Straight back at rest-speed equal (antiparallel, no preferred side): it turns round over the world's up, level.
    {
        float vel[3]={50,0,0};const float want[3]={-50,0,0};
        crew::wing::Step(vel,want,accel,turn,dt,nullptr);
        Check(std::fabs(vel[1])<1e-5f && std::fabs(vel[2])>0.0f,"turned round level");
        Check(std::isfinite(vel[0]+vel[1]+vel[2]),"finite");
    }
    // Slow, its path turns no faster than `rate` (its nose's to follow): at 5 m/s, 30 m/s^2 would be 6 rad/s.
    {
        float vel[3]={0,0,5};const float want[3]={5,0,0};
        crew::wing::Step(vel,want,accel,turn,dt,nullptr,1.2f);
        Check(std::fabs(std::atan2(vel[0],vel[2])-1.2f*dt)<1e-4f,"slow: a frame's turn is `rate` * dt");
    }
    // Half a turn's worth of frames at 70 m/s: the path's heading changes at turn / speed rad/s.
    {
        float vel[3]={0,0,70};const float want[3]={0,0,-70};
        const int n=static_cast<int>(kPi/(turn/70.0f)/dt/2.0f);
        for(int i=0;i<n;++i)crew::wing::Step(vel,want,accel,turn,dt,nullptr);
        const float heading=std::fabs(std::atan2(vel[0],vel[2]));
        Check(heading>kPi/2.0f-0.05f && heading<kPi/2.0f+0.05f,"a quarter turn in a quarter of its turning time");
    }
    std::printf("wing_turn: %d checks passed\n",checks);
    return 0;
}
