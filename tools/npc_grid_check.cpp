// The soldiers' frame grid (src/npc_grid.h) offline: every question asked through it gives the brute-force scan's own
// answer (npcai.cpp before 2026-11-08: npc_logic.h PickTarget, FriendNear, FriendsBut + ShotClear, Evade's first 64
// threats, GroupAt, FallBack's nearest), over thousands of random points: clusters, exact ties (repeated positions),
// points on cell edges, far outliers, odd points (NaN, infinite, past kFar), scopes and radii from 0 to past the whole
// grid, and the grid rebuilt over fewer and more points (objects gone and new: no index of the last build comes back).
//   cmake --build build --target npc_grid_check && build\npc_grid_check.exe      (exit code 1 on a failure)
#include "../src/npc_grid.h"
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,long long a=0,long long b=0) {
    ++cases;
    if(!ok){++failures;if(failures<40)std::printf("FAIL: %s (%lld, %lld)\n",what,a,b);}
}
struct P { float p[3]; float radius; };
std::mt19937 rng(20261108);
float U(float a,float b){ return std::uniform_real_distribution<float>(a,b)(rng); }
int I(int n){ return static_cast<int>(rng()%static_cast<unsigned>(n)); }

// A battle's points: clusters (squads, swarms), a spread, exact repeats (ties), cell edges, a few far ones.
std::vector<P> Battle(int n,float spread,bool outliers) {
    std::vector<P> v;v.reserve(static_cast<std::size_t>(n));
    float cx=U(-spread,spread),cz=U(-spread,spread);
    for(int i=0;i<n;++i) {
        P q{};
        const int kind=I(10);
        if(kind<4){ if(I(20)==0){cx=U(-spread,spread);cz=U(-spread,spread);} q.p[0]=cx+U(-15,15);q.p[2]=cz+U(-15,15); }
        else if(kind<7){q.p[0]=U(-spread,spread);q.p[2]=U(-spread,spread);}
        else if(kind<8 && !v.empty()){q=v[static_cast<std::size_t>(I(static_cast<int>(v.size())))];}   // the same spot: ties
        else if(kind<9){q.p[0]=16.0f*static_cast<float>(I(41)-20);q.p[2]=16.0f*static_cast<float>(I(41)-20);}   // on edges
        else {q.p[0]=U(-spread,spread);q.p[2]=U(-spread,spread);}
        if(outliers && I(200)==0){q.p[0]=U(-9e5f,9e5f);q.p[2]=U(-9e5f,9e5f);}
        if(kind!=7 || v.empty()){q.p[1]=U(-5,60);q.radius=I(3)==0 ? 4.0f : I(2) ? 0.8f : 1.5f;}
        v.push_back(q);
    }
    return v;
}
bool Usable(int i,unsigned salt){ return ((static_cast<unsigned>(i)*2654435761u)^salt)%7u!=0u; }

void Compare(const std::vector<P>& pts,const npc::grid::Grid& g,int queries,const char* label) {
    const int n=static_cast<int>(pts.size());
    const auto aim=[&](int k){ return pts[static_cast<std::size_t>(k)].p; };
    std::vector<npc::Friend> fr(pts.size());
    for(std::size_t i=0;i<pts.size();++i){fr[i].pos[0]=pts[i].p[0];fr[i].pos[1]=pts[i].p[1];fr[i].pos[2]=pts[i].p[2];fr[i].radius=pts[i].radius;}
    float maxRadius=0.0f;for(const P& q:pts)if(q.radius>maxRadius)maxRadius=q.radius;
    const float kInf=std::numeric_limits<float>::infinity(),kNan=std::numeric_limits<float>::quiet_NaN();
    std::vector<int> got;std::vector<npc::Friend> sub;
    int mismatches[6]={};
    for(int q=0;q<queries;++q) {
        // From a point's own spot (a soldier among them), near one, or anywhere.
        float eye[3],anchor[3];
        if(n && I(3)){const float* s=aim(I(n));eye[0]=s[0]+U(-3,3);eye[1]=s[1]+1.5f;eye[2]=s[2]+U(-3,3);}
        else{eye[0]=U(-3000,3000);eye[1]=U(0,50);eye[2]=U(-3000,3000);}
        if(I(2)){anchor[0]=eye[0];anchor[1]=eye[1];anchor[2]=eye[2];}
        else{anchor[0]=eye[0]+U(-80,80);anchor[1]=eye[1];anchor[2]=eye[2]+U(-80,80);}
        const float scopes[]={0.0f,U(1,60),U(40,400),U(300,3000),1e7f,kInf,kNan,-5.0f};
        const float reaches[]={0.0f,U(5,150),U(100,1000),1e9f,kInf};
        const float scope=scopes[I(8)],reach=reaches[I(5)];
        const int current=n && I(2) ? I(n) : -1;
        const unsigned salt=static_cast<unsigned>(rng());
        // The target.
        const auto isCurrent=[&](int k){ return k==current; };
        const auto usable=[&](int k){ return Usable(k,salt); };
        const int want=npc::PickTarget(n,aim,isCurrent,usable,eye,anchor,scope,reach);
        const int pick=npc::grid::PickTarget(g,n,aim,isCurrent,usable,eye,anchor,scope,reach);
        if(want!=pick){++mismatches[0];if(mismatches[0]<4)std::printf("  %s pick %d want %d (scope %g reach %g)\n",label,pick,want,static_cast<double>(scope),static_cast<double>(reach));}
        // The nearest over the ground.
        {
            int best=-1;float nd=1e30f;
            for(int i=0;i<n;++i){const float d=npc::Horiz(eye,aim(i));if(d<nd){nd=d;best=i;}}
            if(best!=npc::grid::NearestHoriz(g,n,aim,eye))++mismatches[1];
        }
        // A friend within r of a point (FriendNear), `self` left out.
        {
            const float r=I(5)==0 ? U(50,500) : U(0.5f,12.0f);
            const int self=n ? I(n) : -1;
            bool want2=false;
            for(int i=0;i<n;++i)if(i!=self && npc::Dist(fr[static_cast<std::size_t>(i)].pos,anchor)<r){want2=true;break;}
            bool near=false;
            g.Around(anchor,r,[&](int i){ if(i!=self && npc::Dist(fr[static_cast<std::size_t>(i)].pos,anchor)<r)near=true; });
            if(want2!=near)++mismatches[2];
        }
        // The threats in reach, the first 64 in index order (Evade), and the group round a point (GroupAt).
        {
            const float r=U(1,40);
            std::vector<int> scan;
            for(int i=0;i<n && static_cast<int>(scan.size())<64;++i)if(npc::Horiz(eye,aim(i))<r)scan.push_back(i);
            got.clear();
            g.Around(eye,r,[&](int i){ if(npc::Horiz(eye,aim(i))<r)got.push_back(i); });
            std::sort(got.begin(),got.end());
            if(got.size()>64)got.resize(64);
            if(got!=scan)++mismatches[3];
            int count=0,count2=0;
            for(int i=0;i<n;++i)if(npc::Dist(aim(i),eye)<=r)++count;
            g.Around(eye,r,[&](int i){ if(npc::Dist(aim(i),eye)<=r)++count2; });
            if(count!=count2)++mismatches[4];
        }
        // A shot: its line and its blast clear of every friend but the shooter (ShotOk / ShotSafe).
        {
            const int self=n ? I(n) : -1;
            float to[3];
            if(n && I(3)){const float* t=aim(I(n));to[0]=t[0];to[1]=t[1];to[2]=t[2];}
            else{const float len=I(4)==0 ? U(500,5000) : U(0,300),a=U(0,6.3f);to[0]=eye[0]+std::sin(a)*len;to[1]=eye[1]+U(-20,20);to[2]=eye[2]+std::cos(a)*len;}
            if(I(10)==0){to[0]=eye[0];to[2]=eye[2];}   // straight up or down
            const float blast=I(2) ? 0.0f : U(1,30);
            constexpr float kSpread=0.6f;
            sub.clear();
            for(int i=0;i<n;++i)if(i!=self)sub.push_back(fr[static_cast<std::size_t>(i)]);
            const bool want3=npc::ShotClear(eye,to,kSpread,blast,sub.data(),static_cast<int>(sub.size()));
            npc::grid::ShotCandidates(g,eye,to,maxRadius+kSpread,blast>0.0f ? blast*1.25f+maxRadius : 0.0f,got);
            sub.clear();
            for(int i:got)if(i!=self)sub.push_back(fr[static_cast<std::size_t>(i)]);
            const bool clear=npc::ShotClear(eye,to,kSpread,blast,sub.data(),static_cast<int>(sub.size()));
            if(want3!=clear)++mismatches[5];
            // Every friend the line or the blast touches is a candidate (the superset itself, not only the verdict).
            for(int i=0;i<n;++i) {
                if(i==self)continue;
                const npc::Friend& f=fr[static_cast<std::size_t>(i)];
                float t;
                const bool touches=(npc::SegmentDist(eye,to,f.pos,&t)<f.radius+kSpread && t>0.0f) ||
                                   (blast>0.0f && npc::Dist(to,f.pos)<blast*1.25f+f.radius);
                if(touches && !std::binary_search(got.begin(),got.end(),i)){++mismatches[5];break;}
            }
        }
    }
    static const char* const kWhat[]={"PickTarget","NearestHoriz","FriendNear","Evade's 64","GroupAt","ShotClear"};
    for(int k=0;k<6;++k) {
        char what[160];std::snprintf(what,sizeof(what),"%s: %s through the grid = the scan",label,kWhat[k]);
        Check(mismatches[k]==0,what,mismatches[k],queries);
    }
}
}  // namespace

int main() {
    npc::grid::Grid g;
    struct Case { const char* label; int n; float spread; bool outliers; float cell; };
    const Case kCases[]={{"crowd 4000 / 600 m",4000,600.0f,false,16.0f},{"crowd 3000 / 2 km, outliers",3000,2000.0f,true,16.0f},
                         {"few 7",7,300.0f,false,16.0f},{"one",1,10.0f,false,16.0f},{"empty",0,10.0f,false,16.0f},
                         {"dense 2000 / 40 m",2000,40.0f,false,16.0f},{"coarse cell 200",2500,1500.0f,false,200.0f},
                         {"fine cell 1",1500,200.0f,false,1.0f}};
    for(const Case& c:kCases) {
        const std::vector<P> pts=Battle(c.n,c.spread,c.outliers);
        g.Build(c.n,[&](int i){ return pts[static_cast<std::size_t>(i)].p; },c.cell);
        Check(g.Points()==c.n && g.Odd()==0,c.label,g.Points(),g.Odd());
        Compare(pts,g,c.n>100 ? 3000 : 600,c.label);
    }
    // Odd points (NaN, infinite, past kFar): kept apart, visited by every query; the pick and the nearest are the scan's.
    {
        std::vector<P> pts=Battle(1500,500.0f,false);
        const float kInf=std::numeric_limits<float>::infinity(),kNan=std::numeric_limits<float>::quiet_NaN();
        pts[10].p[0]=kNan;pts[200].p[2]=kInf;pts[700].p[0]=-kInf;pts[900].p[0]=2e6f;pts[0].p[2]=kNan;
        g.Build(static_cast<int>(pts.size()),[&](int i){ return pts[static_cast<std::size_t>(i)].p; },16.0f);
        Check(g.Odd()==5,"odd points kept apart",g.Odd(),5);
        Compare(pts,g,2000,"odd points");
    }
    // Rebuilt over fewer points (objects gone: no index of the last build is visited), then more (the arrays grow).
    {
        std::vector<P> pts=Battle(5000,800.0f,false);
        g.Build(5000,[&](int i){ return pts[static_cast<std::size_t>(i)].p; },16.0f);
        pts.resize(300);
        g.Build(300,[&](int i){ return pts[static_cast<std::size_t>(i)].p; },16.0f);
        int stale=0,seen=0;
        const float c[3]={0.0f,0.0f,0.0f};
        g.Around(c,1e9f,[&](int i){ ++seen;if(i>=300)++stale; });
        g.NearFirst(0.0f,0.0f,g.All(),[](float){ return false; },[&](int i){ ++seen;if(i>=300)++stale; });
        Check(stale==0 && seen==600,"rebuilt over fewer: only the new points",stale,seen);
        Compare(pts,g,1000,"rebuilt over fewer");
        pts=Battle(9000,1200.0f,true);
        g.Build(9000,[&](int i){ return pts[static_cast<std::size_t>(i)].p; },16.0f);
        Compare(pts,g,1500,"rebuilt over more");
        g.Clear();
        int after=0;
        g.Around(c,1e9f,[&](int){ ++after; });
        Check(after==0 && g.Points()==0,"cleared: nothing visited",after);
    }
    // Every point once: a box over everything, the rings over everything, each index exactly once.
    {
        const std::vector<P> pts=Battle(3000,900.0f,true);
        g.Build(3000,[&](int i){ return pts[static_cast<std::size_t>(i)].p; },16.0f);
        std::vector<int> box(3000,0),ring(3000,0);
        const float c[3]={U(-100,100),0.0f,U(-100,100)};
        g.Around(c,std::numeric_limits<float>::infinity(),[&](int i){ ++box[static_cast<std::size_t>(i)]; });
        g.NearFirst(c[0],c[2],g.All(),[](float){ return false; },[&](int i){ ++ring[static_cast<std::size_t>(i)]; });
        int bad=0;for(int i=0;i<3000;++i)bad+=box[static_cast<std::size_t>(i)]!=1 || ring[static_cast<std::size_t>(i)]!=1;
        Check(bad==0,"every point visited once by a whole box and by the whole rings",bad);
        // The rings nearest first: a ring's bound is never past a point it still gives.
        float lastLb=0.0f;int behind=0;
        g.NearFirst(c[0],c[2],g.All(),[&](float lb){ lastLb=lb;return false; },[&](int i){
            if(npc::Horiz(c,pts[static_cast<std::size_t>(i)].p)<lastLb)++behind; });
        Check(behind==0,"no point nearer than its ring's bound",behind);
    }
    // The cell count stays bounded by the points (a far outlier coarsens the cells instead of growing the arrays).
    {
        std::vector<P> pts(50);
        for(std::size_t i=0;i<pts.size();++i){pts[i].p[0]=static_cast<float>(i);pts[i].p[2]=0.0f;}
        pts[49].p[0]=9.9e5f;pts[49].p[2]=-9.9e5f;
        g.Build(50,[&](int i){ return pts[static_cast<std::size_t>(i)].p; },16.0f);
        Check(g.Cell()>=16.0f && g.Cell()<=1.0e6f,"an outlier: coarser cells",static_cast<long long>(g.Cell()));
        Compare(pts,g,500,"one far outlier");
    }
    std::printf("npc_grid_check: %d of %d cases passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
