// A Dreadnought's hangar bringing a fighter home (src/hangar_recover.h: the very Planned / Fly dreadnought_hangar.inc
// FlyTo runs), test hub #42 (「凭空出现速度的平移」): flown home from all round its ship, the fighter's velocity stays on
// its nose (as BodyAttitude turns it, at the production gains) until its bay's capture takes it, that capture is the tube
// round its final line and nothing wider, and in it the fighter never moves faster than it entered at. Run under
// tools/playerjet_recovery_check.py's negative controls too: the replaced laws (the hold's any-way velocity from afar, the
// 225 m capture measured to the mouth, a closing not slowed for the capture) each fail here.
#include "../src/hangar_recover.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
using namespace crew::recover;
using crew::wing::Len3;
constexpr float dt=1.0f/60.0f;
constexpr float kHoldGain=1.2f,kHoldMost=1.5f,kWingGain=6.0f;   // dreadnought_hangar.inc kHoldGainJet, kHoldMostJet, kWingGainJet

// Unit `n` turned toward unit `to` at gain x the angle, at most `most` rad/s (body506.cpp BodyAttitude's law, the nose).
void TurnNose(float* n,const float* to,float gain,float most) {
    float c=Dot(n,to);c=c>1.0f ? 1.0f : c< -1.0f ? -1.0f : c;
    const float angle=std::acos(c),step=std::fmin(angle*gain,most)*dt;
    if(angle<1e-6f)return;
    float across[3]={to[0]-n[0]*c,to[1]-n[1]*c,to[2]-n[2]*c};
    float l=Len3(across);
    if(l<1e-6f){across[0]=-n[2];across[1]=0.0f;across[2]=n[0];l=Len3(across);}
    for(float& a:across)a/=l;
    const float co=std::cos(step),si=std::sin(step);
    for(int i=0;i<3;++i)n[i]=n[i]*co+across[i]*si;
    l=Len3(n);for(int i=0;i<3;++i)n[i]/=l;
}
// m/s of `vel` not along the nose `n` (across it, or backwards): the slide the player sees of a wing.
float Across(const float* vel,const float* n) {
    const float along=std::fmax(0.0f,Dot(vel,n));
    const float s[3]={vel[0]-n[0]*along,vel[1]-n[1]*along,vel[2]-n[2]*along};
    return Len3(s);
}

struct Result {
    bool home=false;           // touched its mouth (BayFrame: capture, within kTouch, slower than kFinalSpeed / 3)
    float seconds=0.0f;
    float wingSlide=0.0f;      // the most across its nose flying as a wing (m/s)
    float holdSpeed=0.0f;      // the fastest over the ship's in its bay's hold (m/s)
    float firstWingFrames=0.0f;   // frames flown as a wing before its first capture
    bool capturedWide=false;   // a capture farther than kCaptureRange from its final line (never)
};
// A fighter from `start` at `vel0`, its ship's centre at the origin moving at `ship`, its bay's mouth `mouth0` outboard
// +x (the ship's up +y). Flown at most `most` s as BayFrame flies it.
Result Home(const float* start,const float* vel0,const float* ship,const float* mouth0,float most) {
    Result r;
    float p[3]={start[0],start[1],start[2]},vel[3]={vel0[0],vel0[1],vel0[2]};
    float centre[3]={0,0,0},mouth[3]={mouth0[0],mouth0[1],mouth0[2]};
    const float outboard[3]={1,0,0},up[3]={0,1,0};
    float nose[3]={vel[0],vel[1],vel[2]};
    {const float l=Len3(nose);for(float& c:nose)c=l>0.0f ? c/l : 0.0f;if(l<=0.0f)nose[2]=1.0f;}
    bool captured=false;
    for(int frame=0;frame<static_cast<int>(most/dt);++frame) {
        const Plan plan=Planned(p,vel,centre,ship,mouth,outboard,up);
        const Flight fl=Fly(p,vel,ship,plan,nose,dt);
        std::memcpy(vel,fl.vel,12);
        if(plan.capture) {
            captured=true;
            float end[3],at[3];
            for(int i=0;i<3;++i)end[i]=mouth[i]+outboard[i]*kApproachOut+up[i]*kApproachUp;
            if(Nearest(p,mouth,end,at)>=kCaptureRange)r.capturedWide=true;
            const float rel[3]={vel[0]-ship[0],vel[1]-ship[1],vel[2]-ship[2]};
            r.holdSpeed=std::fmax(r.holdSpeed,Len3(rel));
            TurnNose(nose,outboard,kHoldGain,kHoldMost);
            const float d[3]={mouth[0]-p[0],mouth[1]-p[1],mouth[2]-p[2]};
            if(Len3(d)<1.5f && Len3(rel)<kFinalSpeed/3.0f){r.home=true;r.seconds=frame*dt;return r;}
        } else {
            if(!captured)r.firstWingFrames+=1.0f;
            TurnNose(nose,fl.nose,kWingGain,kHoldMost);
            r.wingSlide=std::fmax(r.wingSlide,Across(vel,nose));
        }
        for(int i=0;i<3;++i){p[i]+=vel[i]*dt;centre[i]+=ship[i]*dt;mouth[i]+=ship[i]*dt;}
    }
    return r;
}
void Report(const char* name,const Result& r) {
    std::printf("hangar_recover: %-34s home %d in %5.1f s, wing slide %4.1f m/s, hold speed %4.1f m/s\n",name,r.home,
                r.seconds,r.wingSlide,r.holdSpeed);
}
}

int main() {
    const float still[3]={0,0,0},sailing[3]={0,0,25};
    const float mouth[3]={kShipBeam*0.6f,10.0f,60.0f};   // a broadside bay's mouth
    // Its capture is the tube round its final line and no wider: the old 225 m to the mouth took a fighter 200 m off
    // its mouth abeam (and its whole approach point, 151 m out) into the hold.
    {
        const float outboard[3]={1,0,0},up[3]={0,1,0},centre[3]={0,0,0},slow[3]={-10,0,0},fast[3]={-60,0,0};
        const float abeam[3]={mouth[0]+100.0f,mouth[1]+30.0f,mouth[2]+150.0f};
        Check(!Planned(abeam,slow,centre,still,mouth,outboard,up).capture,"200 m off its mouth, 150 m off its final line: flying as a wing");
        const float onEnd[3]={mouth[0]+kApproachOut,mouth[1]+kApproachUp,mouth[2]};
        Check(Planned(onEnd,slow,centre,still,mouth,outboard,up).capture,"on its approach point: in its bay's hold");
        const float justOff[3]={onEnd[0],onEnd[1],onEnd[2]+kCaptureRange+1.0f};
        Check(!Planned(justOff,slow,centre,still,mouth,outboard,up).capture,"past kCaptureRange off its final line: not held");
        Check(!Planned(onEnd,fast,centre,still,mouth,outboard,up).capture,"through it faster than kCaptureSpeed: not held, a wing");
        // Far out on its final line it makes for the approach point at its approach speed (it used to creep in from
        // any distance at kFinalSpeed, the line taken for its last stretch).
        const float farOut[3]={mouth[0]+900.0f,mouth[1]+kApproachUp,mouth[2]};
        const Plan plan=Planned(farOut,slow,centre,still,mouth,outboard,up);
        Check(!plan.capture && plan.most>=kApproachSpeed && std::fabs(plan.aim[0]-(mouth[0]+kApproachOut))<0.01f,
              "far out on its line: a wing to its approach point, at speed");
    }
    struct Case { const char* name; float start[3],vel[3]; const float* ship; float most; };
    const Case cases[]={
        {"1.5 km out, flying away at 200",{mouth[0]+1500.0f,300.0f,mouth[2]+800.0f},{0,0,200},still,120.0f},
        {"abeam 200 m, along the hull at 150",{mouth[0]+100.0f,40.0f,mouth[2]-150.0f},{0,0,150},still,90.0f},
        {"inside 225 m, crossing at 120",{mouth[0]+60.0f,20.0f,mouth[2]-120.0f},{-120,0,0},still,90.0f},
        {"over the hull at 100",{0.0f,90.0f,0.0f},{0,0,100},still,120.0f},
        {"far side, low, at 80",{-kShipBeam-80.0f,0.0f,0.0f},{0,0,80},still,150.0f},
        {"900 m out on its line, in at 150",{mouth[0]+900.0f,30.0f,mouth[2]},{-150,0,0},still,60.0f},
        {"ship sailing, 1 km behind it",{mouth[0]+300.0f,200.0f,-1000.0f},{0,0,90},sailing,120.0f},
    };
    for(const Case& c:cases) {
        const Result r=Home(c.start,c.vel,c.ship,mouth,c.most);
        Report(c.name,r);
        Check(r.home,"brought home to its mouth");
        Check(!r.capturedWide,"held only within kCaptureRange of its final line");
        // As a wing its velocity stays on its nose: what is left is its nose's lag behind its turn at kWingGainJet.
        Check(r.wingSlide<6.0f,"flying home as a wing: no slide across its nose");
        // In its bay's hold no faster than it entered: the most it moves across its nose there.
        Check(r.holdSpeed<=kCaptureSpeed,"in its bay's hold: never faster than kCaptureSpeed over the ship");
    }
    // From inside the old 225 m it starts as a wing (it used to be held, and slid, from the first frame).
    {
        const Result r=Home(cases[2].start,cases[2].vel,still,mouth,90.0f);
        Check(r.firstWingFrames>60.0f,"inside the old 225 m: flown as a wing first, not held");
    }
    // Far out on its line it is home in a reasonable time (900 m at kFinalSpeed took 75 s).
    {
        const Result r=Home(cases[5].start,cases[5].vel,still,mouth,60.0f);
        Check(r.home && r.seconds<45.0f,"900 m out on its line: home within 45 s");
    }
    std::printf("hangar_recover: %d checks passed\n",checks);
    return 0;
}
