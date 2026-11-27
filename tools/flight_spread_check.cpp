// A flight's aircraft kept apart offline (src/flight_spread.h; the player, 2026-11-27, of the wolf pack: 「他到那个交点意义
// 是什么，互相撞吗」). Flown as point masses at a jet's speed and the turn its g allows, a step of 1/60 s:
//  - the old way reproduced: four abreast 70 m apart each flying at one point close in on each other (under kSepMin);
//  - the lanes: in the formation's order (no two cross), a lane gap apart, the line itself for one aircraft, the
//    gunship's lap round the mark;
//  - a marked strafe flown by four from the formation onto their lanes, down the line and out on their breaks: the whole
//    flight's minimum distance at least kSepMin, the lanes covering the line drawn, the breaks fanning out;
//  - four at one point (the unmarked gun run's one target) and two met head on: kSepMin held by the separation alone;
//  - the claims: four jets and four or more targets take one each, two targets two each, one target all.
//
//   cmake --build build --target flight_spread_check && build\flight_spread_check.exe   exit code 0: every check passed
#include "../src/flight_spread.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew;
using namespace crew::spread;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}
float Len3(const float* a) { return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }
float Dist(const float* a,const float* b) { const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};return Len3(d); }

// A jet: point mass, its velocity turned toward `want` at most the rate `g` g allows at its speed, its speed toward the
// one asked for at 15 m/s^2.
constexpr float kDt=1.0f/60.0f,kGees=5.0f,kSpeed=230.0f;
struct Sim { float pos[3],vel[3]; };
void Step(Sim& s,const float* want,float speed) {
    float v=Len3(s.vel);
    float dir[3]={s.vel[0]/v,s.vel[1]/v,s.vel[2]/v};
    const float c=dir[0]*want[0]+dir[1]*want[1]+dir[2]*want[2];
    const float angle=std::acos(c>1.0f ? 1.0f : c<-1.0f ? -1.0f : c);
    const float most=kGees*9.8f/v*kDt;
    if(angle>1e-5f) {
        const float t=angle<=most ? 1.0f : most/angle;   // slerp-ish: along the great circle by t of the way
        float n[3]={dir[0]+(want[0]-dir[0])*t,dir[1]+(want[1]-dir[1])*t,dir[2]+(want[2]-dir[2])*t};
        const float l=Len3(n);
        if(l>1e-4f)for(int i=0;i<3;++i)dir[i]=n[i]/l;
    }
    const float dv=speed-v,step=15.0f*kDt;
    v+=dv>step ? step : dv<-step ? -step : dv;
    for(int i=0;i<3;++i){s.vel[i]=dir[i]*v;s.pos[i]+=s.vel[i]*kDt;}
}
void Toward(const float* from,const float* to,float* want) {
    float d[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    const float l=Len3(d);
    for(int i=0;i<3;++i)want[i]=l>1e-4f ? d[i]/l : 0.0f;
}
// One frame of the flight: each its own `want` (Guide), then the separation over the others (jet.cpp FlightSeparation).
template<class Guide>
float Fly(Sim* jets,int n,float seconds,bool separate,Guide guide) {
    float least=1e9f;
    for(float t=0.0f;t<seconds;t+=kDt) {
        float wants[8][3],speeds[8];
        for(int k=0;k<n;++k){speeds[k]=kSpeed;guide(k,jets[k],wants[k],&speeds[k]);}
        for(int k=0;k<n && separate;++k) {
            Mate mates[8];int m=0;
            for(int o=0;o<n;++o)if(o!=k) {
                for(int i=0;i<3;++i){mates[m].pos[i]=jets[o].pos[i];mates[m].vel[i]=jets[o].vel[i];}
                mates[m].wing=o;++m;
            }
            float share=1.0f;
            Separate(jets[k].pos,jets[k].vel,k,mates,m,wants[k],&share);
            speeds[k]*=share;
        }
        for(int k=0;k<n;++k)Step(jets[k],wants[k],speeds[k]);
        for(int a=0;a<n;++a)for(int b=a+1;b<n;++b){const float d=Dist(jets[a].pos,jets[b].pos);if(d<least)least=d;}
    }
    return least;
}
// Four abreast 70 m apart (support_entry.h AirFormationSlot: kFormationSide), `back` m short of the mark along +z.
void Abreast(Sim* jets,int n,float back,float y) {
    for(int k=0;k<n;++k) {
        const float side=SlotSide(k)*70.0f;   // side (dir.z, -dir.x) with dir +z: +x
        jets[k]={{side,y,-back},{0.0f,0.0f,kSpeed}};
    }
}

void OldWay() {
    Sim jets[4];Abreast(jets,4,3000.0f,300.0f);
    const float point[3]={0.0f,60.0f,0.0f};
    const float least=Fly(jets,4,11.0f,false,[&](int,const Sim& s,float* want,float*){Toward(s.pos,point,want);});
    std::printf("      four at one point, no separation: %.1f m at the nearest\n",least);
    Check(least<kSepMin,"the old way (each at the one point) brings them under kSepMin");
}

void Lanes() {
    const float mark[3]={0,0,0},heading[3]={0,0,1},from[3]={0,300,-3000};
    float at[4];
    for(int k=0;k<4;++k)at[k]=LaneLine(mark,from,heading,kPassGunRun,Place{k,4}).at[0];   // side +x
    bool ordered=true;
    for(int a=0;a<4;++a)for(int b=0;b<4;++b)if(SlotSide(a)<SlotSide(b) && !(at[a]<at[b]))ordered=false;
    Check(ordered,"the lanes in the order the formation flies abreast (none crosses another to its lane)");
    float lo=1e9f,hi=-1e9f;
    for(int k=0;k<4;++k){if(at[k]<lo)lo=at[k];if(at[k]>hi)hi=at[k];}
    bool apart=true;
    for(int a=0;a<4;++a)for(int b=a+1;b<4;++b)if(std::fabs(at[a]-at[b])<kLaneGun-0.01f)apart=false;
    Check(apart && std::fabs(hi-lo-3.0f*kLaneGun)<0.01f && std::fabs(hi+lo)<0.01f,
          "four gun lanes kLaneGun apart, centred on the line drawn");
    Check(LaneLine(mark,from,heading,kPassGunRun,Place{0,1}).at[0]==0.0f,"one aircraft flies the line itself");
    Check(LaneLine(mark,from,heading,kPassGunshipLap,Place{3,4}).at[0]==0.0f,"the gunship laps the mark itself");
    const float c0=LaneLine(mark,from,heading,kPassCarpet,Place{1,3}).at[0],c1=LaneLine(mark,from,heading,kPassCarpet,Place{2,3}).at[0];
    Check(std::fabs(std::fabs(c0-c1)-2.0f*kLaneCarpet)<0.01f,"three carpets lay three bands kLaneCarpet apart");
    const float none[3]={0,0,0},side[3]={500,300,-3000};
    const sortie::Line l=LaneLine(mark,side,none,kPassCarpet,Place{0,1});
    Check(std::fabs(l.at[0])<0.01f && l.dir[2]>0.98f,"no heading: toward the mark, still through it");
    // Where the flight comes in: one aircraft 250 m off flies its own LineThrough (kMaxOffset across, no turn to make); a
    // flight 40 m off has its lanes about its centre's line, 40 m across, each still kLaneGun from the next.
    const float off[3]={250,300,-3000};
    Check(std::fabs(LaneLine(mark,off,heading,kPassGunRun,Place{}).at[0]-sortie::LineThrough(mark,off,heading).at[0])<0.01f,
          "one aircraft's lane is its LineThrough");
    bool centred=true;
    for(int k=0;k<4;++k) {
        Place p{k,4};p.offset[0]=SlotSide(k)*70.0f;
        const float own[3]={40.0f+p.offset[0],300,-3000};
        const float want=40.0f+LaneUnits(p)*kLaneGun;
        if(std::fabs(LaneLine(mark,own,heading,kPassGunRun,p).at[0]-want)>0.01f)centred=false;
    }
    Check(centred,"a flight's lanes about its centre's line, wherever each of them is in its formation");
}

// The marked strafe: 2.0 s settling onto the lane's line from the formation 3 km out, down a 600 m line (the lead point
// 600 m ahead, as sortie::Ahead), then the break.
void Strafe() {
    const float mark[3]={0,0,0},heading[3]={0,0,1},from[3]={0,300,-3000};
    Sim jets[4];Abreast(jets,4,3000.0f,300.0f);
    sortie::Line lines[4];
    for(int k=0;k<4;++k)lines[k]=LaneLine(mark,from,heading,kPassGunRun,Place{k,4});
    float coverMin=1e9f,coverMax=-1e9f;
    bool broke[4]={};
    const float least=Fly(jets,4,30.0f,true,[&](int k,const Sim& s,float* want,float*) {
        const sortie::Line& l=lines[k];
        const float along=sortie::Along(l,s.pos);
        if(along>300.0f) {   // past the line's end: out on its break, climbing
            float away[3];BreakAway(l,Place{k,4},away);
            want[0]=away[0];want[1]=0.15f;want[2]=away[2];
            const float n=Len3(want);for(int i=0;i<3;++i)want[i]/=n;
            broke[k]=true;
            return;
        }
        float p[3];sortie::Ahead(l,s.pos,600.0f,p);
        p[1]=along<-300.0f ? 200.0f : 45.0f;
        Toward(s.pos,p,want);
        if(along>=-300.0f) {
            const float across=s.pos[0];   // where it strafes, across the line drawn
            if(across<coverMin)coverMin=across;
            if(across>coverMax)coverMax=across;
        }
    });
    std::printf("      the marked strafe: %.1f m at the nearest, strafed %.0f..%.0f m across the line\n",least,coverMin,coverMax);
    Check(least>=kSepMin,"four on a marked strafe never nearer than kSepMin, in, along and out");
    Check(coverMin<-100.0f && coverMax>100.0f && coverMin<0.0f && coverMax>0.0f,"their lanes strafe a band either side of the line");
    Check(broke[0] && broke[1] && broke[2] && broke[3],"all four out on their breaks");
    float spread0=jets[2].vel[0]/Len3(jets[2].vel),spread3=jets[3].vel[0]/Len3(jets[3].vel);
    Check(spread0<-0.2f && spread3>0.2f,"the outer ones break outward, a fan");
}

void OnePoint() {
    Sim jets[4];Abreast(jets,4,3000.0f,300.0f);
    const float point[3]={0.0f,60.0f,0.0f};
    const float least=Fly(jets,4,13.0f,true,[&](int,const Sim& s,float* want,float*) {
        if(s.pos[2]>-150.0f){want[0]=0;want[1]=0.3f;want[2]=1;const float n=Len3(want);for(int i=0;i<3;++i)want[i]/=n;return;}
        Toward(s.pos,point,want);
    });
    std::printf("      four at one point, separated: %.1f m at the nearest\n",least);
    Check(least>=kSepMin,"four at one target never nearer than kSepMin (the separation alone)");
    Sim two[2]={{{0,300,-1500},{0,0,kSpeed}},{{10,300,1500},{0,0,-kSpeed}}};
    const float head=Fly(two,2,10.0f,true,[&](int k,const Sim& s,float* want,float*) {
        const float end[3]={0.0f,300.0f,k==0 ? 5000.0f : -5000.0f};
        Toward(s.pos,end,want);
    });
    std::printf("      two head on: %.1f m at the nearest\n",head);
    Check(head>=kSepMin,"two met head on pass kSepMin apart");
    // From four ways at one point (four calls' jets on one mark, no formation).
    Sim ways[4]={{{-2000,300,0},{kSpeed,0,0}},{{2000,320,0},{-kSpeed,0,0}},{{0,280,-2000},{0,0,kSpeed}},{{0,300,2000},{0,0,-kSpeed}}};
    const float cross=Fly(ways,4,14.0f,true,[&](int k,const Sim& s,float* want,float*) {
        const float on[3]={k==0 ? 5000.0f : k==1 ? -5000.0f : 0.0f,300.0f,k==2 ? 5000.0f : k==3 ? -5000.0f : 0.0f};
        Toward(s.pos,on,want);
    });
    std::printf("      four crossing over one point: %.1f m at the nearest\n",cross);
    Check(cross>=kSepMin,"four crossing over one point from four ways pass kSepMin apart");
    // The formation itself (70 m abreast, parallel) is left alone: no push.
    const float a[3]={0,300,0},v[3]={0,0,kSpeed};
    const Mate wingman{{70,300,0},{0,0,kSpeed},1};
    float want[3]={0,0,1},share=0.0f;
    Check(!Separate(a,v,0,&wingman,1,want,&share) && share==1.0f && want[2]==1.0f,"the formation abreast is let be");
}

// The claims (jet_combat.cpp VisitTarget): each in turn the least distance + ClaimScore of those its mates are on.
void Claims() {
    auto assign=[](int targets,int* out) {
        const float jetX[4]={-105,-35,35,105};
        for(int k=0;k<4;++k) {
            int best=-1;float score=1e30f;
            for(int t=0;t<targets;++t) {
                const float x=static_cast<float>(t)*40.0f-20.0f*static_cast<float>(targets-1);
                int mates=0;for(int o=0;o<k;++o)if(out[o]==t)++mates;
                const float s=std::sqrt((x-jetX[k])*(x-jetX[k])+3000.0f*3000.0f)+ClaimScore(mates);
                if(s<score){score=s;best=t;}
            }
            out[k]=best;
        }
    };
    int four[4],two[4],one[4],six[4];
    assign(4,four);assign(2,two);assign(1,one);assign(6,six);
    bool distinct=true;
    for(int a=0;a<4;++a)for(int b=a+1;b<4;++b)if(four[a]==four[b] || six[a]==six[b])distinct=false;
    Check(distinct,"four jets, four or more targets: one each");
    int perTwo[2]={0,0};for(int k=0;k<4;++k)++perTwo[two[k]];
    Check(perTwo[0]==2 && perTwo[1]==2,"two targets: two jets each");
    Check(one[0]==0 && one[1]==0 && one[2]==0 && one[3]==0,"one target: all of them on it");
}
}  // namespace

int main() {
    OldWay();
    Lanes();
    Strafe();
    OnePoint();
    Claims();
    std::printf("%s\n",failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
