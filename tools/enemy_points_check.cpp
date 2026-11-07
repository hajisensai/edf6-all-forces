// The gunner seats' shared enemy points (src/enemy_points.h) offline: for any vehicle, visiting the side's copy gives
// exactly the visits the live registry walk (heli.cpp ForEachEnemyOf / VisitEnemies) gives it -- the same objects and
// aim points in the same order, the vehicle itself left out -- also after some of them died since the copy was taken.
//   cmake --build build --target enemy_points_check && build\enemy_points_check.exe      (exit code 1 on a failure)
#include "../src/enemy_points.h"
#include <cstdio>
#include <limits>
#include <memory>
#include <random>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,long long a=0,long long b=0) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL: %s (%lld, %lld)\n",what,a,b);}
}
using namespace enemypoints;

struct Seen { const void* object; float aim[3]; };
bool Same(const std::vector<Seen>& a,const std::vector<Seen>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
        if(a[i].object!=b[i].object || std::memcmp(a[i].aim,b[i].aim,12)!=0)return false;
    return true;
}

// A registry in plain memory: a circular list of nodes (next at +0, target at +0x10) through a head node.
struct Registry {
    std::vector<std::unique_ptr<unsigned char[]>> objects,targets,nodes;
    unsigned char head[0x20]{};
    std::int32_t relation[kMaxTeam]{};
};

void Link(Registry& r) {
    unsigned char* prev=r.head;
    for(auto& n:r.nodes){edf::Put<unsigned char*>(prev,0,n.get());prev=n.get();}
    edf::Put<unsigned char*>(prev,0,r.head);
}

// `nodes` lock points of `objectCount` objects (several points each), every rule's way out drawn now and then: another
// target kind, not valid, not lockable, no object, dead, a team out of the table or not an enemy, an aim not finite.
void Make(Registry& r,std::mt19937& rng,int objectCount,int nodes) {
    std::uniform_int_distribution<int> pct(0,99);
    std::uniform_real_distribution<float> pos(-500.0f,500.0f);
    for(auto& rel:r.relation)rel=pct(rng)<45 ? kEnemyRelation : pct(rng)%3;
    for(int i=0;i<objectCount;++i) {
        auto o=std::make_unique<unsigned char[]>(0x400);
        std::memset(o.get(),0,0x400);
        o[edf::kDead]=pct(rng)<8;
        const int t=pct(rng);
        edf::Put<std::int32_t>(o.get(),edf::kTeam,t<3 ? -1 : t<6 ? kMaxTeam+t : static_cast<std::int32_t>(rng()%kMaxTeam));
        r.objects.push_back(std::move(o));
    }
    for(int i=0;i<nodes;++i) {
        auto t=std::make_unique<unsigned char[]>(0x40);
        std::memset(t.get(),0,0x40);
        t[0]=pct(rng)<4 ? 1 : 0;
        t[kTargetValid]=pct(rng)<93;
        t[kTargetLockable]=pct(rng)<90;
        const void* object=pct(rng)<3 ? nullptr : r.objects[rng()%r.objects.size()].get();
        edf::Put<const void*>(t.get(),kTargetObject,object);
        float aim[3]={pos(rng),pos(rng),pos(rng)};
        if(pct(rng)<3)aim[rng()%3]=pct(rng)<50 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
        std::memcpy(t.get()+kTargetAim,aim,12);
        auto n=std::make_unique<unsigned char[]>(0x20);
        std::memset(n.get(),0,0x20);
        edf::Put<unsigned char*>(n.get(),kNodeTarget,t.get());
        r.targets.push_back(std::move(t));
        r.nodes.push_back(std::move(n));
    }
    Link(r);
}

std::vector<Seen> Live(const Registry& r,const void* self) {
    std::vector<Seen> out;
    Walk(r.head,r.relation,self,[&](const void* o,const float* a){Seen s{o,{}};std::memcpy(s.aim,a,12);out.push_back(s);});
    return out;
}
std::vector<Seen> FromCopy(const std::vector<Point>& points,const void* self) {
    std::vector<Seen> out;
    Visit(points,self,[&](const void* o,const float* a){Seen s{o,{}};std::memcpy(s.aim,a,12);out.push_back(s);});
    return out;
}
}  // namespace

int main() {
    std::mt19937 rng(1107);
    int bad=0,badAfterDeaths=0,visits=0,selfSkipped=0,runs=0;
    for(int run=0;run<300;++run) {
        Registry r;
        Make(r,rng,1+static_cast<int>(rng()%200),static_cast<int>(rng()%1500));
        std::vector<Point> copy;
        Collect(r.head,r.relation,copy);
        // Every object as the vehicle asking, none, and one not in the registry at all.
        unsigned char stranger[0x400]{};
        std::vector<const void*> selves{nullptr,stranger};
        for(auto& o:r.objects)selves.push_back(o.get());
        for(const void* self:selves) {
            const auto live=Live(r,self),got=FromCopy(copy,self);
            bad+=!Same(live,got);
            visits+=static_cast<int>(live.size());
            selfSkipped+=self && Live(r,nullptr).size()!=live.size();
        }
        // Some die after the copy was taken: the copy still visits what the walk would now.
        for(auto& o:r.objects)if(rng()%5==0)o[edf::kDead]=1;
        for(const void* self:selves)badAfterDeaths+=!Same(Live(r,self),FromCopy(copy,self));
        ++runs;
    }
    std::printf("  %d registries: %d visits compared, %d asks that left their own vehicle out\n",runs,visits,selfSkipped);
    Check(bad==0,"the copy's visits equal the live walk's for every vehicle",bad);
    Check(badAfterDeaths==0,"and after some died since the copy",badAfterDeaths);
    Check(visits>10000 && selfSkipped>1000,"the registries exercise the filter and the vehicle's own points",visits,selfSkipped);
    // Nearest (the gunner seats' pick): the object, its distance and every usable() asked, as the nearest-within-reach
    // visitor over the live walk; also after some died since the copy. usable() refuses some objects (WastedFire).
    {
        int badPick=0,picked=0,asked=0;
        auto dist=[](const float* a,const float* b){
            const float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};return std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        };
        for(int run=0;run<200;++run) {
            Registry r;
            Make(r,rng,1+static_cast<int>(rng()%200),static_cast<int>(rng()%1500));
            std::vector<Point> copy;
            Collect(r.head,r.relation,copy);
            if(run%2)for(auto& o:r.objects)if(rng()%6==0)o[edf::kDead]=1;
            std::uniform_real_distribution<float> pos(-500.0f,500.0f),reachOf(0.0f,700.0f);
            for(int k=0;k<20;++k) {
                const void* self=r.objects[rng()%r.objects.size()].get();
                const float from[3]={pos(rng),pos(rng),pos(rng)};
                const float reach=k==0 ? 1e9f : reachOf(rng);
                const unsigned refuse=static_cast<unsigned>(rng()%5);
                std::vector<const void*> askedLive,askedCopy;
                const void* liveBest=nullptr;float liveD=1e30f;
                Walk(r.head,r.relation,self,[&](const void* o,const float* a){
                    const float d=dist(from,a);
                    if(d<=reach && d<liveD) {
                        askedLive.push_back(o);
                        if((reinterpret_cast<std::uintptr_t>(o)>>4)%5!=refuse){liveBest=o;liveD=d;}
                    }
                });
                const Pick got=Nearest(copy,self,from,reach,dist,[&](const void* o){
                    askedCopy.push_back(o);return (reinterpret_cast<std::uintptr_t>(o)>>4)%5!=refuse;
                });
                badPick+=got.object!=liveBest || std::memcmp(&got.dist,&liveD,4)!=0 || askedLive!=askedCopy;
                picked+=got.object!=nullptr;asked+=static_cast<int>(askedCopy.size());
            }
        }
        std::printf("  Nearest: %d picks, %d usable() asks\n",picked,asked);
        Check(badPick==0,"Nearest: the live visitor's pick, distance and usable() asks",badPick);
        Check(picked>1000 && asked>picked,"Nearest: picks made and refusals exercised",picked,asked);
    }
    // The walk's node cap (kMaxNodes): a longer list stops at the same node both ways.
    {
        Registry r;
        Make(r,rng,50,kMaxNodes+500);
        std::vector<Point> copy;
        Collect(r.head,r.relation,copy);
        Check(Same(Live(r,nullptr),FromCopy(copy,nullptr)),"past kMaxNodes: the same cut");
        // the cut is real: the last 500 nodes are never visited (all made enemy, alive and lockable)
        Registry tail;
        for(auto& rel:tail.relation)rel=kEnemyRelation;
        auto o=std::make_unique<unsigned char[]>(0x400);std::memset(o.get(),0,0x400);
        tail.objects.push_back(std::move(o));
        for(int i=0;i<kMaxNodes+10;++i) {
            auto t=std::make_unique<unsigned char[]>(0x40);std::memset(t.get(),0,0x40);
            t[kTargetValid]=t[kTargetLockable]=1;
            edf::Put<const void*>(t.get(),kTargetObject,tail.objects[0].get());
            auto n=std::make_unique<unsigned char[]>(0x20);std::memset(n.get(),0,0x20);
            edf::Put<unsigned char*>(n.get(),kNodeTarget,t.get());
            tail.targets.push_back(std::move(t));tail.nodes.push_back(std::move(n));
        }
        Link(tail);
        std::vector<Point> all;
        Collect(tail.head,tail.relation,all);
        Check(all.size()==static_cast<std::size_t>(kMaxNodes),"the copy holds kMaxNodes points of a longer list",
              static_cast<long long>(all.size()));
    }
    // The negative control: a copy that does not leave the asking vehicle out differs from the walk.
    {
        Registry r;
        Make(r,rng,20,400);
        std::vector<Point> copy;
        Collect(r.head,r.relation,copy);
        int differ=0;
        for(auto& o:r.objects)differ+=!Same(Live(r,o.get()),FromCopy(copy,nullptr));
        Check(differ>0,"the control: the copy unfiltered for the vehicle differs",differ);
    }
    std::printf("enemy_points_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
