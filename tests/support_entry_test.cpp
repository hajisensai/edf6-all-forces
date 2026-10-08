#include "../src/support_entry.h"
#include <cstdio>
#include <cstdlib>

int main() {
    using namespace crew;using namespace support;
    int checks=0;
    const auto check=[&](bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}};
    const PlayArea area{{-1500,-900},{1500,900},true,0,true};
    const float target[3]={0,0,0},preferred[3]={0,0,1},observer[3]={0,0,-800};
    const auto clear=[](const float*,const float*){return true;};
    const auto ground=[](float,float,float,float& height){height=0;return true;};
    Route route;
    check(AirRoute(area,target,observer,preferred,150,clear,ground,route)==Refusal::none,"edge route exists");
    check(FlatDistance(route.from,target)>=kMinJourney,"arrival has visible journey");
    check(FlatDistance(route.from,observer)>=kObserverClear,"no arrival beside observer near edge");
    check(route.from[0]!=0 || route.from[2]!=-870,"near observer entry rejected");
    check(route.from[1]==150,"altitude above real ground");
    check(route.heading[0]*route.heading[0]+route.heading[2]*route.heading[2]>0.999f,"normalised heading");
    auto unknown=area;unknown.ground=false;
    check(AirRoute(unknown,target,observer,preferred,150,clear,ground,route)==Refusal::noArea,"unknown map extent cannot spawn locally");
    auto blocked=[](const float*,const float*){return false;};
    check(AirRoute(area,target,observer,preferred,150,blocked,ground,route)==Refusal::noEntry,"blocked corridors do not fall back to target offset");
    auto voidMap=[](float,float,float,float&){return false;};
    check(AirRoute(area,target,observer,preferred,150,clear,voidMap,route)==Refusal::noEntry,"missing terrain rejects entry");
    auto tower=[](float,float,float,float& h){h=250;return true;};
    check(AirRoute(area,target,observer,preferred,150,clear,tower,route)==Refusal::none && route.from[1]==400,"tall corridor terrain raises arrival");
    const PlayArea tiny{{-100,-100},{100,100},true,0,true};
    check(AirRoute(tiny,target,observer,preferred,150,clear,ground,route)==Refusal::noEntry,"no short journey on tiny arena");
    std::printf("support_entry_test: %d checks passed\n",checks);
}
