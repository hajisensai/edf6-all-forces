// The long-range barrages offline (src/barrage_plan.h as src/barrage.cpp plans and flies them;
// docs/plan-2026-10-25-strikes.md §1.1):
//  - every kind plans its rounds, under kMaxRounds, none leaving before the call;
//  - the replay: the same call (point, heading, seed) gives the same plan, another seed another scatter;
//  - the curves: each round leaves its launcher and reaches its point, no jump between frames, shells over their chord;
//  - each kind's shape: the MRSI's shells land in one frame, the creeping barrage's lines a gap apart walking along the
//    heading, the cluster's bomblets leave their carrier where and when it splits 260 m up, the guided rockets and the
//    ballistic warhead come straight down, the cruise missile clears a hill on its way;
//  - the launchers stay inside the physics world near its edge.
//
//   cmake --build build --target barrage_check && build\barrage_check.exe        exit code 0: every check passed
#include "../src/barrage_plan.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew::barrage;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}
float Dist(const float* a,const float* b) {
    const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}
float Flat(const float* a,const float* b) { return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[2]-b[2])*(a[2]-b[2])); }

// A field at 10 m with a 120 m hill 1 km behind the mark (against the heading +z).
float Ground(float x,float z,float) {
    const float dz=z+1000.0f,r=std::sqrt(x*x+dz*dz);
    return 10.0f+(r<300.0f ? 120.0f*(1.0f-r/300.0f) : 0.0f);
}
Setup At(float x,float z,std::uint64_t seed) {
    Setup s;
    s.target[0]=x;s.target[1]=Ground(x,z,0.0f);s.target[2]=z;
    s.seed=seed;s.worldHalf=3000.0f;
    return s;
}
int PlanOf(Kind k,const Setup& s,Round* out) { return Plan(k,s,Ground,out,kMaxRounds); }

void Counts() {
    Round r[kMaxRounds];
    const int want[kKinds]={kKatyushaRounds,kGuidedRounds,kThermoRounds,kClusterCarriers*(1+kBomblets),kCreepLines*kCreepPer,
                            kMrsiRounds,1,1,1};
    bool ok=true,early=false;
    for(int k=0;k<kKinds;++k) {
        const int n=PlanOf(static_cast<Kind>(k),At(0,0,7),r);
        std::printf("      %-12s %3d rounds, over at frame %d\n",kKindNames[k],n,EndFrame(r,n));
        if(n!=want[k] || n>kMaxRounds)ok=false;
        for(int i=0;i<n;++i)if(r[i].launch<0)early=true;
    }
    Check(ok,"every kind plans its rounds (under kMaxRounds)");
    Check(!early,"no round leaves before the call");
}

void Replay() {
    Round a[kMaxRounds],b[kMaxRounds],c[kMaxRounds];
    bool same=true;
    for(int k=0;k<kKinds;++k) {
        const int na=PlanOf(static_cast<Kind>(k),At(100,-50,42),a),nb=PlanOf(static_cast<Kind>(k),At(100,-50,42),b);
        if(na!=nb)same=false;
        for(int i=0;i<na && i<nb;++i)
            if(a[i].launch!=b[i].launch || a[i].flight!=b[i].flight || Dist(a[i].to,b[i].to)!=0.0f || Dist(a[i].from,b[i].from)!=0.0f)same=false;
    }
    Check(same,"the same call plans the same barrage (every machine replays it alike)");
    const int na=PlanOf(Kind::katyusha,At(0,0,1),a),nc=PlanOf(Kind::katyusha,At(0,0,2),c);
    bool differ=false;
    for(int i=0;i<na && i<nc;++i)if(Dist(a[i].to,c[i].to)>1.0f)differ=true;
    Check(differ,"another seed scatters it otherwise");
    const float t[3]={1,2,3},h[3]={0,0,1},h2[3]={0,0,-1};
    Check(Seed(t,h,0)==Seed(t,h,0) && Seed(t,h,0)!=Seed(t,h2,0) && Seed(t,h,0)!=Seed(t,h,1),"the seed follows the point, heading and kind");
}

void Curves() {
    Round r[kMaxRounds];
    bool ends=true,smooth=true,over=true;
    float fastest=0.0f;
    for(int k=0;k<kKinds;++k) {
        const int n=PlanOf(static_cast<Kind>(k),At(0,0,9),r);
        for(int i=0;i<n;++i) {
            float p0[3],p1[3];
            PathAt(r[i],0.0f,p0);PathAt(r[i],static_cast<float>(r[i].flight),p1);
            if(Dist(p0,r[i].from)>0.5f || Dist(p1,r[i].to)>0.5f)ends=false;
            float last[3];PathAt(r[i],0.0f,last);
            float prevStep=-1.0f;
            for(int f=1;f<=r[i].flight+30;++f) {
                float q[3];PathAt(r[i],static_cast<float>(f),q);
                const float step=Dist(q,last);
                if(step*60.0f>fastest)fastest=step*60.0f;
                if(prevStep>0.0f && step>prevStep*1.6f+2.0f)smooth=false;   // no sudden lurch
                prevStep=step;
                if(r[i].path==Path::arc && f<r[i].flight) {
                    const float u=static_cast<float>(f)/static_cast<float>(r[i].flight);
                    const float chord=r[i].from[1]+(r[i].to[1]-r[i].from[1])*u;
                    if(r[i].rise>0.0f && q[1]<chord-0.01f)over=false;
                }
                last[0]=q[0];last[1]=q[1];last[2]=q[2];
            }
        }
    }
    Check(ends,"each round leaves its launcher and reaches its point");
    Check(smooth,"no round jumps from one frame to the next");
    Check(over,"a shell flies over its chord");
    std::printf("      fastest %.0f m/s\n",fastest);
    Check(fastest<1500.0f,"no round faster than 1500 m/s");
}

void Shapes() {
    Round r[kMaxRounds];
    int n=PlanOf(Kind::mrsi,At(0,0,3),r);
    bool one=n>1;
    for(int i=1;i<n;++i)if(r[i].launch+r[i].flight!=r[0].launch+r[0].flight)one=false;
    bool spread=n>1 && r[0].launch!=r[n-1].launch;
    Check(one && spread,"MRSI: launched apart, every shell lands in the same frame");

    n=PlanOf(Kind::creeping,At(0,0,3),r);
    bool walks=n==kCreepLines*kCreepPer;
    for(int line=1;line<kCreepLines && walks;++line) {
        const Round& a=r[(line-1)*kCreepPer];const Round& b=r[line*kCreepPer];
        const int ga=a.launch+a.flight,gb=b.launch+b.flight;
        if(std::abs(gb-ga-kCreepGap)>10)walks=false;
        float za=0,zb=0;
        for(int j=0;j<kCreepPer;++j){za+=r[(line-1)*kCreepPer+j].to[2];zb+=r[line*kCreepPer+j].to[2];}
        if((zb-za)/kCreepPer<kCreepStep*0.6f)walks=false;   // along the heading (+z)
    }
    Check(walks,"creeping: each line a gap after the last, a step further along the heading");

    n=PlanOf(Kind::cluster,At(0,0,3),r);
    bool split=true;
    int bursts=0;
    for(int i=0;i<n;) {
        const Round& c=r[i];
        if(c.splitAt<0){split=false;break;}
        float at[3];PathAt(c,static_cast<float>(c.splitAt),at);
        if(std::fabs(at[1]-c.to[1]-kClusterSplitHeight)>30.0f)split=false;
        for(int b=1;b<=kBomblets;++b) {
            const Round& m=r[i+b];
            if(m.launch!=c.launch+c.splitAt || Dist(m.from,at)>0.01f || m.splitAt>=0)split=false;
            if(Flat(m.to,c.to)>kBombletSpread+0.5f)split=false;
            if(m.burst)++bursts;
        }
        i+=1+kBomblets;
    }
    Check(split,"cluster: the bomblets leave their carrier where and when it splits, 260 m over its point");
    Check(bursts==kClusterCarriers,"cluster: one flash per carrier");

    auto steep=[](const Round& x,float least) {
        float a[3],b[3];
        PathAt(x,static_cast<float>(x.flight-1),a);PathAt(x,static_cast<float>(x.flight),b);
        const float down=a[1]-b[1],across=Flat(a,b);
        return down>0.0f && down>least*across;
    };
    n=PlanOf(Kind::guided,At(0,0,3),r);
    bool dives=n==kGuidedRounds;
    for(int i=0;i<n;++i)if(!steep(r[i],5.0f))dives=false;
    Check(dives,"guided: every rocket comes straight down on its aim");
    float aims[2][3]={{30,10,40},{-60,10,5}};
    Setup s=At(0,0,3);s.aims=2;s.aim=aims;
    n=PlanOf(Kind::guided,s,r);
    Check(n==kGuidedRounds && Dist(r[0].to,aims[0])<0.01f && Dist(r[1].to,aims[1])<0.01f,"guided: the first rockets on the given aims");
    n=PlanOf(Kind::ballistic,At(0,0,3),r);
    Check(n==1 && steep(r[0],5.0f),"ballistic: the warhead comes nearly straight down");
    float top=-1e9f;
    for(int f=0;f<r[0].flight;++f){float q[3];PathAt(r[0],static_cast<float>(f),q);if(q[1]>top)top=q[1];}
    Check(top>r[0].to[1]+2000.0f,"ballistic: it climbs high first");

    n=PlanOf(Kind::cruise,At(0,0,3),r);
    bool clears=n==1;
    for(int f=0;f<r[0].flight && clears;++f) {
        float q[3];PathAt(r[0],static_cast<float>(f),q);
        const float g=Ground(q[0],q[2],0.0f);
        const float d=Flat(q,r[0].to);
        if(d>600.0f && Flat(q,r[0].from)>300.0f && q[1]<g+kCruiseOver*0.8f)clears=false;
    }
    Check(clears,"cruise: low over the ground, over the hill on its way");
    Check(n==1 && steep(r[0],0.6f),"cruise: it dives onto its point");
}

void Edge() {
    Round r[kMaxRounds];
    bool inside=true;
    const float lim=3000.0f-kEdgeRoom+30.0f;   // a launcher's jitter
    for(int k=0;k<kKinds;++k) {
        Setup s=At(0,-2700,5);   // near the world's -z edge; the heading +z: the launchers behind it, at the edge
        const int n=PlanOf(static_cast<Kind>(k),s,r);
        for(int i=0;i<n;++i)if((std::fabs(r[i].from[0])>lim || std::fabs(r[i].from[2])>lim))inside=false;
    }
    Check(inside,"the launchers stay inside the physics world near its edge");
}
}  // namespace

int main() {
    Counts();
    Replay();
    Curves();
    Shapes();
    Edge();
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n",failures);
    return failures ? 1 : 0;
}
