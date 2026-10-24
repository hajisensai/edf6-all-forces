// The paced march (src/march_pace.h): the slowest unit sets the group's pace, the others wait for it, all arrive
// together; a stuck unit stops holding the group up, and a unit held for the slowest is never taken for stuck.
#include "../src/march_pace.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* text) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}

// `n` units at `speed` m/s with `way` m to go, simulated at 10 Hz (each moves its speed times the share it was given):
// when each arrives (s), and whether any was ever taken for stuck on the way.
struct Run { double arrived[4]; bool stuck; float slowestShare; };
Run March(int n,const float* speed,const float* way) {
    using namespace pace;
    Member m[4];float left[4];Run r{{-1.0,-1.0,-1.0,-1.0},false,1.0f};
    std::uint64_t now=1000;
    for(int i=0;i<n;++i){left[i]=way[i];Begin(m[i],left[i],now);}
    for(int step=0;step<20000;++step) {
        float s[4];Scales(m,n,s);
        float least=2.0f;int slowest=-1;
        for(int i=0;i<n;++i)if(m[i].active && Progress(m[i])<least){least=Progress(m[i]);slowest=i;}
        if(slowest>=0 && s[slowest]<r.slowestShare)r.slowestShare=s[slowest];
        now+=100;
        bool all=true;
        for(int i=0;i<n;++i) {
            if(!m[i].active)continue;
            left[i]=std::fmax(0.0f,left[i]-speed[i]*s[i]*0.1f);
            Update(m[i],left[i],now);
            if(Stuck(m[i]))r.stuck=true;
            if(left[i]==0.0f){m[i].active=false;r.arrived[i]=(now-1000)/1000.0;}
            all=all && !m[i].active;
        }
        if(all)break;
    }
    return r;
}
}
int main() {
    using namespace pace;
    // Three units 300 m from their points: a soldier at 5 m/s, a tank at 12 m/s, a heli at 25 m/s, simulated at 10 Hz.
    {
        Member m[3];const float speed[3]={5.0f,12.0f,25.0f};float left[3]={300.0f,300.0f,300.0f};
        std::uint64_t now=1000;
        for(int i=0;i<3;++i)Begin(m[i],left[i],now);
        float most=0.0f;int arrivedTogether=-1;
        for(int step=0;step<1000;++step) {
            float s[3];Scales(m,3,s);
            Check(s[0]==1.0f,"the slowest unit always goes at its own full speed");
            now+=100;
            for(int i=0;i<3;++i){left[i]=std::fmax(0.0f,left[i]-speed[i]*s[i]*0.1f);Update(m[i],left[i],now);}
            const float gap=std::fmax(std::fabs(left[1]-left[0]),std::fabs(left[2]-left[0]));
            if(gap>most)most=gap;
            if(left[0]==0.0f){arrivedTogether=left[1]<=40.0f && left[2]<=40.0f ? 1 : 0;break;}
        }
        Check(most<=2.0f*kSlackLeast+25.0f*0.1f+0.01f,"the fast units never get more than twice the slack ahead");
        Check(arrivedTogether==1,"they arrive together, at the soldier's pace");
    }
    // A far slow unit and a near fast one (review of PR #241: a soldier 1500 m out at 3 m/s, a vehicle 100 m out at
    // 15 m/s). The vehicle, held for the soldier, was taken for "stuck" in 8 s (not 2 m gained while told to stand), let
    // go at full speed, caught by the pace again, over and over, and in 290 s against the soldier's 500. Now: never stuck,
    // the soldier always at full speed, both in within twice the march's slack of its time.
    {
        const float speed[2]={3.0f,15.0f},way[2]={1500.0f,100.0f};
        const Run r=March(2,speed,way);
        Check(!r.stuck,"far slow + near fast: neither is ever taken for stuck");
        Check(r.slowestShare==1.0f,"far slow + near fast: the slowest is never slowed");
        Check(r.arrived[0]>0.0 && r.arrived[1]>0.0,"far slow + near fast: both arrive");
        Check(r.arrived[0]<=1500.0/3.0+0.2,"far slow + near fast: the soldier at its own full speed");
        Check(r.arrived[1]>=r.arrived[0]*(1.0-2.0*kSlackShare)-0.2,"far slow + near fast: the vehicle in together with it");
    }
    // The same with three: a soldier far, a tank middling, a heli near.
    {
        const float speed[3]={3.0f,12.0f,25.0f},way[3]={900.0f,400.0f,120.0f};
        const Run r=March(3,speed,way);
        Check(!r.stuck && r.slowestShare==1.0f,"three ways and speeds: none stuck, the slowest never slowed");
        for(int i=1;i<3;++i)Check(r.arrived[i]>=r.arrived[0]*(1.0-2.0*kSlackShare)-0.2,"three ways and speeds: in together");
    }
    // No pacing with fewer than two units on their way.
    Member one[1];Begin(one[0],100.0f,0);float s1[1];Scales(one,1,s1);Check(s1[0]==1.0f,"alone: full speed");
    // A unit truly stuck (let go at full speed, no 2 m gained for 8 s) is no longer waited for: the one far ahead goes on.
    {
        Member st[2];Begin(st[0],300.0f,0);Begin(st[1],300.0f,0);Update(st[1],100.0f,0);
        float s2[2];Scales(st,2,s2);Check(s2[0]==1.0f && s2[1]==0.0f,"far ahead of a moving slowest: held");
        std::uint64_t now=0;
        for(int i=0;i<20;++i){now+=500;Update(st[0],300.0f,now);Update(st[1],100.0f,now);Scales(st,2,s2);}
        Check(Stuck(st[0]) && s2[1]==1.0f,"the slowest stuck at full speed for 10 s: the others go on");
        Check(!Stuck(st[1]),"the one held all that while: not stuck");
        // It gets on again: waited for again.
        Update(st[0],290.0f,now+100);Update(st[1],100.0f,now+100);Scales(st,2,s2);
        Check(!Stuck(st[0]) && s2[1]==0.0f,"the stuck one on its way again: waited for again");
    }
    // Time at a share counts by that share: at half speed, 16 s without getting on is stuck, 12 s is not.
    {
        Member h;Begin(h,300.0f,0);h.scale=0.5f;
        Update(h,300.0f,12000);Check(!Stuck(h),"12 s at half: not stuck");
        Update(h,300.0f,16100);Check(Stuck(h),"16 s at half: stuck");
    }
    // Different starting distances arrive together (progress is each one's own share of its way).
    Member d[2];Begin(d[0],400.0f,0);Begin(d[1],100.0f,0);Update(d[0],200.0f,0);Update(d[1],50.0f,0);
    float s3[2];Scales(d,2,s3);Check(s3[0]==1.0f && s3[1]==1.0f,"half of either way: both at full speed");
    std::printf("march_pace_test: %d checks passed\n",checks);
    return 0;
}
