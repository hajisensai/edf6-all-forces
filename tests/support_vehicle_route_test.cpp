// Production dispatcher -> GroundNavigate -> pending route post -> NpcPostInput -> actual seat
// throttle. Only EDF object memory and terrain query results are fixtures; no route/driver stubs.
#define SUPPORT_ROUTE_NATIVE_TEST
#include "support_dispatch_test.cpp"
namespace crew {
bool NpcDriver(const unsigned char* vehicle) noexcept {
    if(!vehicle || !SeatCount(vehicle))return false;
    auto* seat=SeatAt(const_cast<unsigned char*>(vehicle),0);
    return SeatRider(seat)==Rider::other && !AnyPlayerIn(seat);
}
bool IsOnlineAuthority(const void*) noexcept {return true;}
bool ReadCommandUnit(const ObjRef&,const char*,const Command&,bool,CommandUnit*) noexcept {return false;}
bool wall=false;
bool bridge=false;
float MapRay(const float*,const float*,float*) noexcept {return wall ? 1.0f : -1.0f;}
float MapFloorRay(const float* from,const float* to,float* hit) noexcept {
    const float floor=bridge && from[1]>=6 && to[1]<=6 ? 6.0f : 0.0f;
    if(from[1]<floor || to[1]>floor)return -1;
    hit[0]=from[0];hit[1]=floor;hit[2]=from[2];return from[1]-floor;
}
Sea SeaAt(float,float,float*) noexcept {return Sea::land;}
}
#include "../src/npcpost.cpp"
#include "../src/ground_navigation.cpp"

int main() {
    using namespace crew;
    int checks=0;const auto check=[&](bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}};
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    check(image!=nullptr,"inert native image allocated");
    ResetSupportDispatch();ResetNpcPosts();made=releases=followed=0;testOnline=false;
    config=Config{};config.tankPostHold=6;
    auto vehicle=Make(true),driver=Make();
    auto* v=static_cast<unsigned char*>(const_cast<void*>(vehicle.obj));
    void* vtable[73]{};vtable[72]=image+0x661440;Put<void**>(v,0,vtable);
    Put<float>(v,kMatrix,1);Put<float>(v,kMatrix+40,1);Put<unsigned>(v,0x1AD0,1);
    Put<const void*>(seats[0],kSeatRider,driver.obj);Put<const void*>(seats[0],kSeatRiderCtrl,driver.ctrl);
    check(NpcDriver(v),"fixture is a real human rider, not a dummy/player");
    auto& d=deployments[0];d.used=true;d.assigned=true;d.id=1;d.born=fixtureMs;
    d.plan.catalogId=static_cast<unsigned>(GroundStart()+1);d.plan.count=2;d.plan.target[2]=24;
    d.plan.units[0].resourceId=kVehicle;d.plan.units[1].resourceId=kSoldier;d.plan.units[1].role=1;
    d.objects[0]=vehicle;d.objects[1]=driver;
    const auto step=[&]() {
        fixtureMs+=16;Put<float>(seats[0],0x2C0,0);Put<float>(seats[0],0x2C4,0);
        SupportDispatchTick();NpcPostInput(v);
    };
    step();check(At<float>(seats[0],0x2C4)==0,"route still planning: production driver stays parked instead of driving straight to destination");
    for(int i=0;i<200 && At<float>(seats[0],0x2C4)==0;++i)step();
    check(d.navigation.length>0 && posts[0].at[2]==4,"planner publishes its first actual 4 m route point");
    check(At<float>(seats[0],0x2C4)<0 && config.tankPostHold==6,
          "production NPC driver accelerates toward 4 m waypoint despite unchanged 6 m guard radius");
    check(posts[0].routeArrival==1 && d.navigation.profile.waypointRadius==1.5f,"driver and route advancement use the same explicit vehicle contract");
    Put<float>(v,kPosition+8,3.1f);step();
    check(posts[0].at[2]>=7.9f && At<float>(seats[0],0x2C4)<0,"near intermediate point advances route and keeps driving toward next verified point");
    wall=true;fixtureMs+=110;step();
    check(At<float>(seats[0],0x2C4)==0 && posts[0].at[2]==3.1f,"newly blocked next segment withdraws production driver throttle");
    wall=false;Put<float>(v,kPosition+8,20);step();
    check(!d.delivered && releases==0 && At<float>(seats[0],0x2C4)==0,"entering final radius requests parking but never unloads a moving vehicle");
    Put<float>(v,kPosition+8,20.2f);step();check(releases==0,"coasting vehicle keeps its real driver aboard");
    step();check(d.delivered && releases==1 && followed==1,"only observed stop completes empty delivery and returns its driver");
    // The audited native transform reconstructs the model root (near wheel bottom), not COM.
    // Do not guess a half-height offset or let a long downward ray choose another bridge level.
    const auto restartRoute=[&](float originY,float targetY) {
        d.delivered=false;d.started=true;d.navigation={};d.vehicleSampleAt=0;releases=0;
        Put<float>(v,kPosition+8,0);Put<float>(v,kPosition+4,originY);d.plan.target[1]=targetY;
        Put<float>(seats[0],0x2C0,0);Put<float>(seats[0],0x2C4,0);
        ResetNpcPosts();
    };
    restartRoute(0.65f,0);for(int i=0;i<50;++i)step();
    check(At<float>(seats[0],0x2C4)==0 && !d.navigation.length && !releases,
          "root outside the supported floor-step interval waits rather than inventing a COM offset");
    bridge=true;restartRoute(6.1f,6);
    for(int i=0;i<200 && At<float>(seats[0],0x2C4)==0;++i)step();
    check(d.navigation.length>0 && d.navigation.path[0].y==6 && At<float>(seats[0],0x2C4)<0,
          "vehicle just above bridge deck routes on that deck, never the ground underneath");
    restartRoute(0.1f,0);
    for(int i=0;i<200 && At<float>(seats[0],0x2C4)==0;++i)step();
    check(d.navigation.length>0 && d.navigation.path[0].y==0,
          "vehicle below bridge keeps the lower connected ground layer");
    restartRoute(6.8f,6);for(int i=0;i<50;++i)step();
    check(!d.navigation.length && At<float>(seats[0],0x2C4)==0 && !releases,
          "airborne vehicle is not declared grounded by projecting to the lower bridge or terrain");
    ResetSupportDispatch();ResetNpcPosts();VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("support_vehicle_route_test: %d checks passed\n",checks);
}
