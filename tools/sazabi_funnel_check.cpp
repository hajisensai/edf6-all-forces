// The Sazabi's funnels (src/sazabi_funnels.h) flown offline: a mech at the origin facing +z, its six pack slots on its
// back, enemies ahead (still, then moving, then none), stepped at 60 Hz. Checks: they launch one after another
// kLaunchGap apart; each closes on a station kStationLo..Hi m round its target and fires kShots there; stations
// spread (no two within kMinApart m at a first shot); they never pass through the mech's body; their speed stays
// sane; all come home and dock. With no enemy they hang round the mech and come home when the sortie is up.
//   sazabi_funnel_check                 the checks (exit 1 on a failure)
//   sazabi_funnel_check --dump FILE     also every step's positions (tools: the preview folder's funnel render)
#define _CRT_SECURE_NO_WARNINGS
#include "../src/sazabi_funnels.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace sazabi::funnels;
constexpr float kDt=1.0f/60.0f,kMinApart=10.0f,kBodyRadius=5.0f,kMostSpeed=320.0f;
int failures=0;
std::FILE* dump=nullptr;

void Fail(const char* s,const char* what,float got) {
    std::printf("FAIL %s: %s (%.2f)\n",s,what,got);
    ++failures;
}
float Dist(const float* a,const float* b) {
    const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    return Len3(d);
}

// The scene: `n` targets (moving with `vel` m/s each), flown for `secs`.
void Fly(const char* name,const Target* start,const float (*vel)[3],int n,float secs) {
    Funnel fs[kCount];
    float docks[kCount][3];
    for(int i=0;i<kCount;++i) {   // the packs: left (+x) slots 0-2, right 3-5, high on its back
        const float side=i<3 ? 1.0f : -1.0f;
        const int k=i%3;
        docks[i][0]=side*(2.5f+0.4f*static_cast<float>(k));docks[i][1]=24.0f-0.7f*static_cast<float>(k);docks[i][2]=-4.5f+0.8f*static_cast<float>(k);
    }
    const float mech[3]={0.0f,13.0f,0.0f},fwd[3]={0.0f,0.0f,1.0f};
    Target ts[8];
    for(int i=0;i<n;++i)ts[i]=start[i];
    int launched=0,docked=0,shots[kCount]{};
    float launchAt[kCount];
    bool firstShot[kCount]{};
    float t=0.0f;
    for(int step=0;t<secs;++step,t+=kDt) {
        for(int i=0;i<n;++i)for(int k=0;k<3;++k)ts[i].at[k]+=vel[i][k]*kDt;
        const Events ev=Step(fs,docks,mech,fwd,ts,n,step==0,kDt);
        for(int e=0;e<ev.launched;++e)launchAt[launched++]=t;
        docked+=ev.docked;
        for(int e=0;e<ev.shots;++e) {
            const Shot& s=ev.shot[e];
            ++shots[s.funnel];
            const float r=Dist(s.from,s.at);
            if(r<kStationLo-5.0f || r>kStationHi+2.0f*kHopHi*kShots)Fail(name,"a shot from its station's distance",r);
            if(!firstShot[s.funnel]) {
                firstShot[s.funnel]=true;
                for(int j=0;j<kCount;++j)
                    if(j!=s.funnel && fs[j].phase!=Phase::docked && fs[j].phase!=Phase::launching && Dist(fs[j].at,s.from)<kMinApart)
                        Fail(name,"two funnels on one spot",Dist(fs[j].at,s.from));
            }
        }
        for(int i=0;i<kCount;++i) {
            const Funnel& f=fs[i];
            if(f.phase==Phase::docked || f.phase==Phase::launching)continue;
            const float sp=Len3(f.vel);
            if(sp>kMostSpeed)Fail(name,"a funnel too fast",sp);
            const float flat=std::sqrt(f.at[0]*f.at[0]+f.at[2]*f.at[2]);
            if(flat<kBodyRadius && f.at[1]>0.0f && f.at[1]<20.0f && Dist(f.at,docks[i])>8.0f)Fail(name,"a funnel through the mech's body",flat);
        }
        if(dump && step%3==0) {
            std::fprintf(dump,"%s %.3f",name,t);
            for(int i=0;i<kCount;++i)std::fprintf(dump," %d %.2f %.2f %.2f %.3f %.3f %.3f",static_cast<int>(fs[i].phase),fs[i].at[0],fs[i].at[1],
                                                  fs[i].at[2],fs[i].dir[0],fs[i].dir[1],fs[i].dir[2]);
            for(int i=0;i<n;++i)std::fprintf(dump," T %.2f %.2f %.2f",ts[i].at[0],ts[i].at[1],ts[i].at[2]);
            for(int e=0;e<ev.shots;++e)std::fprintf(dump," S %d",ev.shot[e].funnel);
            std::fprintf(dump,"\n");
        }
    }
    if(launched!=kCount)Fail(name,"all six launch",static_cast<float>(launched));
    for(int i=1;i<launched;++i)if(std::fabs((launchAt[i]-launchAt[i-1])-kLaunchGap)>kDt*1.5f)Fail(name,"launches kLaunchGap apart",launchAt[i]-launchAt[i-1]);
    for(int i=0;i<kCount && n>0;++i)if(shots[i]!=kShots)Fail(name,"each fires kShots",static_cast<float>(shots[i]));
    if(docked!=kCount)Fail(name,"all six dock",static_cast<float>(docked));
    std::printf("  %-8s launched %d, shots %d %d %d %d %d %d, docked %d\n",name,launched,shots[0],shots[1],shots[2],shots[3],shots[4],shots[5],docked);
}
}  // namespace

int main(int argc,char** argv) {
    for(int a=1;a+1<argc;++a)if(std::strcmp(argv[a],"--dump")==0)dump=std::fopen(argv[a+1],"w");
    const Target three[3]={{{0.0f,5.0f,250.0f}},{{60.0f,15.0f,220.0f}},{{-80.0f,5.0f,300.0f}}};
    const float still[3][3]={};
    Fly("still",three,still,3,14.0f);
    const Target one[1]={{{30.0f,40.0f,200.0f}}};
    const float moving[1][3]={{-25.0f,0.0f,-10.0f}};   // an enemy flyer crossing
    Fly("moving",one,moving,1,14.0f);
    Fly("none",nullptr,nullptr,0,14.0f);
    if(dump)std::fclose(dump);
    std::printf(failures ? "sazabi_funnel_check: %d failures\n" : "sazabi_funnel_check: all scenarios pass\n",failures);
    return failures ? 1 : 0;
}
