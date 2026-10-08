// Execute the actual GroundNavigate world adapter. Only EDF scene boundaries are
// fixtures; water policy, budget, A*, cache and waypoint advancement are production.
#include "../src/crew.h"
#include "../src/heli.h"
#include "../src/ground_navigation.h"
#include <cstdio>
#include <memory>
namespace {
ULONGLONG frame=100;int queries=0,floors=0,waters=0,walls=0,cases=0,failures=0;
float floorHeight=0,surface=0;crew::Sea sea=crew::Sea::land;bool cornerWall=false;
void Check(bool yes,const char* what){++cases;if(!yes){++failures;std::printf("FAIL %s\n",what);}}
void Scene(float floor,crew::Sea water,float waterAt=0){++frame;queries=floors=waters=walls=0;floorHeight=floor;sea=water;surface=waterAt;cornerWall=false;}
void Next(){++frame;queries=0;}
}
namespace crew {
ULONGLONG GameFrame() noexcept{return frame;}
float MapFloorRay(const float* a,const float* b,float* hit) noexcept {
    ++queries;++floors;
    if(a[1]<floorHeight || b[1]>floorHeight)return -1;
    hit[0]=a[0];hit[1]=floorHeight;hit[2]=a[2];return a[1]-floorHeight;
}
float MapRay(const float* a,const float* b,float*) noexcept {
    ++queries;
    if(cornerWall)for(int i=0;i<=32;++i){
        const float t=static_cast<float>(i)/32.0f,x=a[0]+(b[0]-a[0])*t,z=a[2]+(b[2]-a[2])*t;
        if(x>3.3f && x<3.9f && z>1.0f && z<2.0f){++walls;return 0.5f;}
    }
    return -1;
}
Sea SeaAt(float,float,float* y) noexcept{++queries;++waters;*y=surface;return sea;}
}
using namespace npc::navigation;
namespace {
Result Advance(State& s,const float* from,const float* to,float* next,Profile p={},int most=200) {
    Result r=Result::pending;
    for(int i=0;i<most && r==Result::pending;++i){Next();r=crew::GroundNavigate(s,from,to,0.1f,frame*16,next,p);Check(queries<=4096,"shared world query budget");}
    return r;
}
void Water() {
    auto s=std::make_unique<State>();float from[3]={0,0,0},to[3]={2,0,0},next[3]{};
    Scene(0,crew::Sea::land);
    Check(Advance(*s,from,to,next)==Result::moving && floors>0 && waters>0,"production adapter queries dry ground and native water boundary");
    *s={};Scene(-5,crew::Sea::water,0);from[1]=to[1]=-5;
    Check(Advance(*s,from,to,next)==Result::blocked,"seabed is not accepted as dry terrain");
    *s={};Scene(0,crew::Sea::water,0.3f);from[1]=to[1]=0;
    Check(Advance(*s,from,to,next)==Result::moving,"shallow wading accepted by default profile");
    *s={};Profile vehicle{};vehicle.maxWaterDepth=0.1f;
    Check(Advance(*s,from,to,next,vehicle)==Result::blocked,"vehicle depth limit applies to same shallow water");
    *s={};Scene(-30,crew::Sea::land);from[1]=to[1]=-30;
    Check(Advance(*s,from,to,next)==Result::moving,"real dry underground result remains reachable below sea level");
    *s={};Scene(-30,crew::Sea::unknown);
    Check(Advance(*s,from,to,next,{},2)==Result::pending && s->count==1,"unknown water probe is deferred, not guessed dry underground");
    sea=crew::Sea::land;
    Check(Advance(*s,from,to,next)==Result::moving,"deferred water probe recovers without dropping route");
    Scene(-30,crew::Sea::water,0);*s={};
    Check(Advance(*s,from,from,next)==Result::blocked,"arrival check cannot certify a deep-water spawn point");
}
void Corners() {
    auto s=std::make_unique<State>();Profile vehicle{};vehicle.cell=4;vehicle.waypointRadius=1.5f;
    float from[3]={3.1f,0,0},to[3]={4,0,4},next[3]{};
    Scene(0,crew::Sea::land);Begin(*s,{0,0,0},{4,0,4},vehicle,frame*16);
    s->path[0]={4,0,0};s->path[1]={4,0,4};s->length=2;s->checked=true;s->checkedAt=frame*16;
    Check(crew::GroundNavigate(*s,from,to,0.1f,frame*16,next,vehicle)==Result::moving && next[2]==4,
        "vehicle inside 1.5m waypoint radius advances to next waypoint");
    *s={};Begin(*s,{0,0,0},{4,0,4},vehicle,frame*16);
    s->path[0]={4,0,0};s->path[1]={4,0,4};s->length=2;s->checked=true;s->checkedAt=frame*16;cornerWall=true;Next();
    Check(crew::GroundNavigate(*s,from,to,0.1f,frame*16,next,vehicle)!=Result::moving && walls>0,
        "advancing waypoint rechecks current position diagonal instead of cutting wall corner");
}
void Fairness() {
    auto states=std::make_unique<State[]>(32);Scene(0,crew::Sea::land);
    float from[3]={0,0,0},to[3]={100,0,0},next[3]{};
    for(int f=0;f<16;++f){Next();for(int i=0;i<32;++i)crew::GroundNavigate(states[i],from,to,0.1f,frame*16,next);Check(queries<=4096,"population query budget");}
    bool every=true;for(int i=0;i<32;++i)every &= states[i].count>1;
    Check(every,"budget rotation eventually serves every actor, including update-list tail");
}
void LongSearch() {
    auto s=std::make_unique<State>();Scene(0,crew::Sea::land);Profile profile;profile.cell=4;
    const float from[3]={0,0,0},to[3]={900,0,0};float next[3]{};
    const auto began=frame*16;
    Check(Advance(*s,from,to,next,profile,2000)==Result::moving && frame*16-began>1500,
        "900m complete route survives planning longer than the movement-stall interval");
    const int plannedNodes=s->count;frame+=100;queries=0;
    Check(crew::GroundNavigate(*s,from,to,0.1f,frame*16,next,profile)!=Result::moving && s->count<plannedNodes,
        "an actor actually stuck after route creation still replans");
}
}
int main(){Water();Corners();LongSearch();Fairness();std::printf("ground navigation runtime: %d checks, %d failures\n",cases,failures);return failures ? 1 : 0;}
