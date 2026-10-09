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
    const float outside[3]={2000,0,0};
    check(AirRoute(area,outside,observer,preferred,150,clear,ground,route)==Refusal::noEntry,"map pointer past the usable area cannot order support into the void");
    // Ground entries (2026-10-09): a stock map's edges lie 1100-1700 m from a central target, past the route planner's
    // +-1024 m square, so every edge was refused. Candidates are rings within its reach.
    const PlayArea stock{{-1600,-1600},{1597,1597},true,0,true};
    const float centre[3]={71,24,455},caller[3]={71,24,455};
    const auto entries=GroundEntryCandidates(stock,centre,caller);
    check(entries.count>=kGroundBearings,"a central target on a stock map has ground entry candidates");
    for(int i=0;i<entries.count;++i) {
        const float p[3]={entries.at[i][0],0,entries.at[i][1]};
        check(FlatDistance(p,centre)>=kMinJourney && FlatDistance(p,caller)>=kObserverClear,"every entry: a journey, out of the caller's sight");
        check(std::fabs(p[0]-centre[0])<256*kGroundPlanCell && std::fabs(p[2]-centre[2])<256*kGroundPlanCell,
              "every entry inside the planner's square round it");
        check(p[0]>=stock.lo[0]+kEntryInset && p[0]<=stock.hi[0]-kEntryInset && p[2]>=stock.lo[1]+kEntryInset && p[2]<=stock.hi[1]-kEntryInset,
              "every entry inside the measured play area");
    }
    const float east[3]={900,0,0};const auto away=GroundEntryCandidates(stock,centre,east);
    const float first[3]={away.at[0][0],0,away.at[0][1]},second[3]={away.at[1][0],0,away.at[1][1]};
    check(away.count>0 && FlatDistance(first,east)>=FlatDistance(second,east),"within a ring the farthest from the caller is tried first");
    const PlayArea corner{{-700,-700},{700,700},true,0,true};const float edge[3]={650,0,650};
    const auto few=GroundEntryCandidates(corner,edge,nullptr);
    for(int i=0;i<few.count;++i)check(few.at[i][0]<=670 && few.at[i][1]<=670,"no entry past a near map edge");
    check(GroundEntryCandidates(unknown,centre,caller).count==0,"no measured area: no entries");
    // Runway level rule (the landing strip's), against the old 0.5 m billiard table.
    const auto lane=[](float grade,float step){return [=](int i,float& g){g=grade*20.0f*static_cast<float>(i)+(i==6 ? step : 0.0f);return true;};};
    check(RunwayLaneLevel(12,0,lane(0.01f,0)),"a 1 % grade over 220 m is a runway");
    check(!RunwayLaneLevel(12,0,lane(0.05f,0)),"a 5 % hillside is not");
    check(!RunwayLaneLevel(12,0,lane(0,2.0f)),"a 2 m ledge is not");
    check(!RunwayLaneLevel(12,0,[](int,float&){return false;}),"no ground under a sample is not");
    std::printf("support_entry_test: %d checks passed\n",checks);
}
