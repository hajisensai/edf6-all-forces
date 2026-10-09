#include "../src/ground_navigation.h"
#include <cstdio>
#include <memory>
using namespace npc::navigation;
namespace {
int failures=0,cases=0;
void Check(bool yes,const char* name) {++cases;if(!yes){++failures;std::printf("FAIL %s\n",name);}}
struct World {
    bool wall=true,sealed=false,corner=false,gap=false,low=false,bridge=false,dynamic=false;
    int calls=0,allow=100000;
    Edge operator()(Point a,Point b,Point& out) {
        if(calls>=allow)return Edge::pending;
        ++calls;
        auto ground=[&](Point p,float step,float& y){
            if(gap && p.x>3 && p.x<6)return false;
            y=bridge ? (p.y>2 ? 5.0f : 0.0f) : 0.0f;
            return std::fabs(y-p.y)<=step;
        };
        auto clear=[&](Point p,Point q){
            const int n=30;
            for(int i=0;i<=n;++i) {
                const float t=static_cast<float>(i)/n,x=p.x+(q.x-p.x)*t,z=p.z+(q.z-p.z)*t,y=p.y+(q.y-p.y)*t;
                if(wall && x>=4 && x<=6 && (sealed || std::fabs(z)<4))return false;
                if(corner && z>=4 && z<=6 && x>4 && x<10)return false;
                if(low && x>3 && x<7 && y>1.1f)return false;
                if(dynamic && x>1 && x<3 && std::fabs(z)<2)return false;
            }
            return true;
        };
        return WalkEdge(a,b,out,{},ground,clear) ? Edge::open : Edge::blocked;
    }
};
Result Plan(State& s,World& w,Point from,Point to,Point& next,std::uint64_t& ms,int max=20000) {
    Result result=Result::pending;
    for(int i=0;i<max && result==Result::pending;++i) {ms+=16;result=Navigate(s,from,to,0.2f,ms,next,[&](Point a,Point b,Point& c){return w(a,b,c);});}
    return result;
}
void Routing() {
    auto s=std::make_unique<State>();World w;Point from{},to{12,0,0},next{};std::uint64_t ms=1;
    Check(Plan(*s,w,from,to,next,ms)==Result::moving,"wall has a route");
    bool detour=false,valid=true;
    for(int i=0;i<s->length;++i) {Point out{};valid &= w(from,s->path[i],out)==Edge::open;from=s->path[i];detour |= std::fabs(from.z)>4;}
    Check(valid&&detour&&Horizontal(from,to)<0.1f,"route goes around wall with body clearance");
    *s={};w.corner=true;from={};ms=1;
    Check(Plan(*s,w,from,to,next,ms)==Result::moving,"L corner can be routed");
    *s={};w.sealed=true;ms=1;
    Check(Plan(*s,w,from,to,next,ms)==Result::blocked,"infinite wall never falls back to direct motion");
    *s={};w={};w.wall=false;w.bridge=true;ms=1;
    Check(Plan(*s,w,from,{12,5,0},next,ms)==Result::blocked,"bridge above same XY cannot teleport between floors");
    *s={};w={};w.wall=false;w.gap=true;ms=1;
    Check(Plan(*s,w,from,to,next,ms)==Result::blocked,"unsupported gap is not traversable");
    *s={};w={};w.wall=false;ms=1;
    Check(Plan(*s,w,from,{4,0,0},next,ms)==Result::moving,"initial dynamic route exists");
    w.dynamic=true;ms+=101;
    const Result blocked=Navigate(*s,from,{4,0,0},0.2f,ms,next,[&](Point a,Point b,Point& c){return w(a,b,c);});
    Check(blocked!=Result::moving,"new obstacle cancels cached movement immediately on validation");
    Check(Plan(*s,w,from,{4,0,0},next,ms)==Result::moving,"dynamic obstacle replans around obstruction");
    *s={};w={};w.allow=0;ms=1;
    Check(Plan(*s,w,from,to,next,ms,1)==Result::pending && s->count==1,"budget exhaustion defers without inventing an edge");
    w.allow=100000;
    Check(Plan(*s,w,from,to,next,ms)==Result::moving,"budget deferred search resumes");
    w={};w.wall=false;w.low=true;Point out{};
    Check(w({2,0,0},{4,0,0},out)==Edge::blocked,"headroom blocks low ceiling");
    w={};w.wall=false;
    Check(w({0,0,0},{2,0,0},out)==Edge::open,"flat corridor accepted");
    *s={};ms=1;w={};w.wall=false;
    const auto r=Navigate(*s,from,to,0.2f,ms,next,[&](Point a,Point b,Point& c){return w(a,b,c);},{},2);
    Check(r==Result::pending&&w.calls==2,"per-call edge budget enforced");
    *s={};w={};w.wall=false;ms=1;
    Check(Plan(*s,w,from,{1,0,0},next,ms)==Result::moving,"short route available");
    const Result finalApproach=Navigate(*s,{0.7f,0,0},{1,0,0},0.1f,ms+16,next,
        [&](Point a,Point b,Point& c){return w(a,b,c);});
    Check(finalApproach==Result::moving && next.x==1.0f,"final waypoint uses arrival tolerance, not intermediate corner tolerance");
    const Result arrival=Navigate(*s,{0.95f,0,0},{1,0,0},0.1f,ms+32,next,
        [&](Point a,Point b,Point& c){return w(a,b,c);});
    Check(arrival==Result::arrived,"final approach can finish without planning forever");
}
// Support entries stand 650-950 m from their target (support_entry.h GroundEntryCandidates) and the support route
// profiles use 4 m cells. 2026-10-09 17:00 the user's tank call rejected all ten candidates one by one, ~40 s each,
// "no route to the target": a Euclidean estimate on the 8-way grid undercounts every off-axis route, so the search
// had to open the whole ellipse of cells whose estimate was under the real cost and ran out of nodes first, even
// across an empty flat field (before the fix 15 of these 21 failed, all but 0 and 45 deg).
struct Field {
    float wall=0.0f;   // half the length of a wall across the route at x = 325, 0: none
    Edge operator()(Point a,Point b,Point& out) const {
        if((a.x<325.0f)!=(b.x<325.0f) && std::fabs(a.z)<wall)return Edge::blocked;
        out=b;out.y=0.0f;return Edge::open;
    }
};
template<class S>
Result Far(S& s,const Field& f,Point to,Profile p) {
    Point next{};std::uint64_t ms=1;Result r=Result::pending;
    for(int i=0;i<40000 && r==Result::pending;++i){ms+=16;r=Navigate(s,{},to,2.0f,ms,next,f,p);}
    return r;
}
void LongRoutes() {
    Profile p;p.cell=4.0f;p.radius=4.0f;p.height=3.5f;p.waypointRadius=4.0f;
    // The exact estimate alone: a soldier's state (kNodes), the shortest route, in a straight walk of cells.
    for(float metres:{650.0f,800.0f,950.0f})for(float deg:{0.0f,10.0f,22.5f,33.0f,45.0f,67.5f,80.0f}) {
        auto s=std::make_unique<State>();
        const float a=deg*3.14159265f/180.0f;
        char name[96];std::snprintf(name,sizeof name,"open field: a %.0f m route at %.1f deg is found",metres,deg);
        Check(Far(*s,Field{},{std::cos(a)*metres,0,std::sin(a)*metres},p)==Result::moving && s->count<kNodes,name);
        Point at{};bool straight=true;
        for(int i=0;i<s->length;++i){straight=straight && Horizontal(at,s->path[i])<=p.cell*1.5f;at=s->path[i];}
        Check(straight && s->length>metres/(p.cell*1.5f),"the route steps cell by cell");
    }
    // A support route (RouteState, kRouteGreed) round a wall straight across its middle: 400 m long, then the
    // shortest-route search (greed 1) runs out of even kRouteNodes in front of it.
    Profile route=p;route.greed=kRouteGreed;
    auto s=std::make_unique<RouteState>();
    Check(Far(*s,Field{200.0f},{650,0,0},route)==Result::moving,"a support route round a 400 m wall is found");
    float length=0.0f;Point at{};bool round=false;
    for(int i=0;i<s->length;++i){length+=Horizontal(at,s->path[i]);at=s->path[i];round=round || std::fabs(at.z)>=200.0f;}
    Check(round && length<=650.0f*kRouteGreed,"it goes round the wall's end, within kRouteGreed of the shortest");
    s=std::make_unique<RouteState>();
    Check(Far(*s,Field{200.0f},{650,0,0},p)==Result::blocked,"negative control: the shortest-route search runs out there");
    s=std::make_unique<RouteState>();
    Check(Far(*s,Field{100000.0f},{650,0,0},route)==Result::blocked,"a wall with no end is still no route");
}
}
int main(){Routing();LongRoutes();std::printf("ground navigation: %d checks, %d failures\n",cases,failures);return failures ? 1 : 0;}
