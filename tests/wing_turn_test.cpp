// A wing's velocity steered as a wing steers it (src/wing_turn.h; dreadnought_hangar.inc FlyTo bringing a fighter home):
// test hub #42 (「凭空出现速度的平移」): its speed changes only along its path, its path turns at most `turn` across it, and
// nothing pushes it sideways. Against the law it replaced (the whole velocity closed on the wanted one at `accel` in any
// direction, the nose sent toward the goal), which slid a fighter called home across its nose.
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
// The replaced law (dreadnought.cpp Approach): the velocity closes on `want` at `accel` as a whole.
void OldApproach(float* vel,const float* want,float accel,float dt) {
    float d[3]={want[0]-vel[0],want[1]-vel[1],want[2]-vel[2]};
    const float len=Len3(d),most=accel*dt,k=len>most && len>0.0f ? most/len : 1.0f;
    for(int i=0;i<3;++i)vel[i]+=d[i]*k;
}
// Unit `n` turned toward unit `to` at gain x the angle, at most `most` rad/s (body506.cpp BodyAttitude's law, the nose).
void TurnNose(float* n,const float* to,float gain,float most,float dt) {
    float c=Dot(n,to);c=c>1.0f ? 1.0f : c< -1.0f ? -1.0f : c;
    const float angle=std::acos(c),step=std::fmin(angle*gain,most)*dt;
    if(angle<1e-6f)return;
    float across[3]={to[0]-n[0]*c,to[1]-n[1]*c,to[2]-n[2]*c};
    float l=Len3(across);
    if(l<1e-6f){across[0]=-n[2];across[1]=0.0f;across[2]=n[0];l=Len3(across);}
    for(float& a:across)a/=l;
    const float co=std::cos(step),si=std::sin(step);
    for(int i=0;i<3;++i)n[i]=n[i]*co+across[i]*si;
}
// m/s of `vel` across the nose `n`: the slide the player sees.
float Across(const float* vel,const float* n) {
    const float along=Dot(vel,n);
    const float s[3]={vel[0]-n[0]*along,vel[1]-n[1]*along,vel[2]-n[2]*along};
    return Len3(s);
}
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
    // Pursuit home (FlyTo far out: the want toward the point at min(most, sqrt(accel d))): from 1.5 km behind it at
    // 200 m/s heading away, it comes within the hangar's capture (90 m) without ever sliding, in reasonable time.
    {
        float pos[3]={0,300,0},vel[3]={200,0,0};const float goal[3]={-1500,320,200};
        const float most=160.0f,capture=90.0f;
        float worstSlide=0.0f;int frames=0;bool in=false;
        for(;frames<60*180 && !in;++frames) {
            const float d[3]={goal[0]-pos[0],goal[1]-pos[1],goal[2]-pos[2]};
            const float dist=Len3(d);
            if(dist<capture){in=true;break;}
            const float closing=std::fmin(most,std::sqrt(accel*dist));
            const float want[3]={d[0]/dist*closing,d[1]/dist*closing,d[2]/dist*closing};
            const float was[3]={vel[0],vel[1],vel[2]};
            crew::wing::Step(vel,want,accel,turn,dt,nullptr);
            worstSlide=std::fmax(worstSlide,Slide(was,vel));
            for(int i=0;i<3;++i)pos[i]+=vel[i]*dt;
        }
        Check(in,"flying home it reaches its hangar's capture");
        Check(frames<60*120,"within two minutes from 1.5 km going the other way");
        Check(worstSlide<=turn*dt*1.001f,"never a slide on the way");
    }
    // Flying home as the hangar flew it, the body's nose turned as BodyAttitude turns it: the replaced law (the velocity
    // closed as a whole, the nose on the way to its goal at the hold's 1.2/s) against the wing (the nose on its path at
    // kWingGainJet 6/s). The slide is the velocity across the nose.
    {
        const float goal[3]={-1500,320,200},most=160.0f,capture=90.0f;
        auto fly=[&](bool wing,float* worst) {
            float pos[3]={0,300,0},vel[3]={200,0,0},nose[3]={1,0,0};
            *worst=0.0f;
            for(int frame=0;frame<60*180;++frame) {
                const float d[3]={goal[0]-pos[0],goal[1]-pos[1],goal[2]-pos[2]};
                const float dist=Len3(d);
                if(dist<capture)return true;
                const float closing=std::fmin(most,std::sqrt(accel*dist));
                const float want[3]={d[0]/dist*closing,d[1]/dist*closing,d[2]/dist*closing};
                float to[3];
                if(wing){crew::wing::Step(vel,want,accel,turn,dt,nullptr);const float l=Len3(vel);for(int i=0;i<3;++i)to[i]=vel[i]/l;}
                else{OldApproach(vel,want,accel,dt);const float l=std::hypot(d[0],d[2]);to[0]=d[0]/l;to[1]=0.0f;to[2]=d[2]/l;}
                TurnNose(nose,to,wing ? 6.0f : 1.2f,1.5f,dt);
                for(int i=0;i<3;++i)pos[i]+=vel[i]*dt;
                *worst=std::fmax(*worst,Across(vel,nose));
            }
            return false;
        };
        float oldSlide=0.0f,newSlide=0.0f;
        const bool oldIn=fly(false,&oldSlide),newIn=fly(true,&newSlide);
        Check(oldIn && newIn,"both bring it home");
        Check(oldSlide>60.0f,"the replaced law: the fighter slid at tens of m/s across its nose (the report's slide)");
        Check(newSlide<10.0f,"the wing: its velocity stays on its nose (a few m/s of attitude lag at most)");
        std::printf("wing_turn: slide across the nose flying home: replaced %.0f m/s, wing %.1f m/s\n",oldSlide,newSlide);
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
