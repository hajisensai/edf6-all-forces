#include "../src/support_entry.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace crew;using namespace support;
int checks=0;
void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
// m the point (x, z) lies past the ground's edge of `area` (negative: on the ground).
float PastGround(const PlayArea& area,const float* p) {
    float lo[2],hi[2];GroundEdge(area,lo,hi);
    const float out[4]={lo[0]-p[0],p[0]-hi[0],lo[1]-p[2],p[2]-hi[1]};
    float most=out[0];for(float o:out)most=o>most ? o : most;
    return most;
}
bool InSquare(const float* p,float half) { return std::fabs(p[0])<=half+0.01f && std::fabs(p[2])<=half+0.01f; }
}  // namespace

int main() {
    const auto clear=[](const float*,const float*){return true;};
    const auto ground=[](float,float,float,float& height){height=0;return true;};
    // The ground ends where the measure found it (playarea.h: the walls kVoidMargin inside it); off it a map ray finds nothing.
    const auto mapGround=[](const PlayArea& a){
        return [a](float x,float z,float,float& h){
            float lo[2],hi[2];GroundEdge(a,lo,hi);
            if(x<lo[0] || x>hi[0] || z<lo[1] || z>hi[1])return false;
            h=0;return true;
        };
    };
    const PlayArea area{{-1500,-900},{1500,900},true,0,true};
    const float target[3]={0,0,0},preferred[3]={0,0,1},observer[3]={0,0,-800};
    const Reach stock{2700,1000};   // BigWorld off: Havok +-3000 less kArrivalRoom; the stock far clip
    Route route;
    check(AirRoute(area,stock,target,observer,preferred,150,0,clear,mapGround(area),route)==Refusal::none,"an entry off the map");
    check(PastGround(area,route.from)>=kOffMap-0.01f,"the entry stands past the ground's edge, not on the map");
    check(InSquare(route.from,stock.half),"the entry inside the physical square");
    check(route.beyond && FlatDistance(route.from,observer)>=stock.draw+kDrawClear,"past the caller's draw distance");
    check(FlatDistance(route.from,target)>=kMinJourney,"arrival has visible journey");
    check(route.from[1]==150,"altitude above real ground");
    check(route.heading[0]*route.heading[0]+route.heading[2]*route.heading[2]>0.999f,"normalised heading");
    // Straight in: the heading points from the entry at the target.
    {
        const float dx=target[0]-route.from[0],dz=target[2]-route.from[2],d=std::sqrt(dx*dx+dz*dz);
        check(std::fabs(dx/d-route.heading[0])<1e-3f && std::fabs(dz/d-route.heading[2])<1e-3f,"the heading is the line to the target");
    }
    auto unknown=area;unknown.ground=false;
    check(AirRoute(unknown,stock,target,observer,preferred,150,0,clear,ground,route)==Refusal::noArea,"unknown map extent cannot spawn locally");
    auto blocked=[](const float*,const float*){return false;};
    check(AirRoute(area,stock,target,observer,preferred,150,0,blocked,ground,route)==Refusal::noEntry,"blocked corridors do not fall back to target offset");
    // The void off the map has no ground: no floor to keep over, not a refusal (the old entry on the map needed ground).
    auto voidMap=[](float,float,float,float&){return false;};
    check(AirRoute(area,stock,target,observer,preferred,150,0,clear,voidMap,route)==Refusal::none && route.from[1]==150,
          "the void off the map is no reason to refuse");
    auto tower=[](float,float,float,float& h){h=250;return true;};
    check(AirRoute(area,stock,target,observer,preferred,150,0,clear,tower,route)==Refusal::none && route.from[1]==400,"tall corridor terrain raises arrival");
    const float outside[3]={2000,0,0};
    check(AirRoute(area,stock,outside,observer,preferred,150,0,clear,ground,route)==Refusal::noEntry,"map pointer past the usable area cannot order support into the void");
    // The physical square holds no place past the draw distance (ViewDistance 4000 on the stock world, the caller at the
    // target): the farthest from the caller, still off the map, flagged.
    const Reach distant{2700,4000};
    const float atTarget[3]={0,0,0};
    check(AirRoute(area,distant,target,atTarget,preferred,150,0,clear,mapGround(area),route)==Refusal::none,"an entry with none past the draw distance");
    check(PastGround(area,route.from)>=kOffMap-0.01f && InSquare(route.from,distant.half),"still off the map, inside the square");
    check(!route.beyond && FlatDistance(route.from,atTarget)>2700.0f*1.41f-150.0f,"none past it: the farthest (a corner of the square)");
    // A square with no room past the ground: refused, never made on the map.
    const Reach cramped{1100,1000};
    check(AirRoute(area,cramped,target,observer,preferred,150,0,clear,mapGround(area),route)==Refusal::noEntry,
          "no room off the map inside the square: refused, not made on the map");
    // The 2026-10-10 13:17 call (catalog 32, the paratroop plane): the measured area x -1030..1508, z -1569..1269, the point
    // (-115,0,-509), the caller near it; BigWorld 6000 (the square +-5700), ViewDistance 3000. The old entry was
    // (-1000,209,-150), 180 m inside the ground and 894-941 m from the caller.
    {
        const PlayArea log{{-1030,-1569},{1508,1269},true,0,true};
        const float point[3]={-115,0,-509},caller[3]={-60,0,-420},east[3]={0.93f,0,-0.38f};
        const Reach big{5700,3000};
        const float turn=266.0f;   // the strike body's ferry turn (jet_flight.cpp FerryTurn at 98 m/s)
        check(AirRoute(log,big,point,caller,east,150,turn,clear,mapGround(log),route)==Refusal::none,"the log's call: an entry");
        check(PastGround(log,route.from)>=kOffMap-0.01f && route.beyond && FlatDistance(route.from,caller)>=3200.0f-0.5f,
              "the log's call: off the map and past the 3000 m draw distance");
        // Its line: entry, point and far end on one straight line, the far end off the map past the point.
        const float ax=point[0]-route.from[0],az=point[2]-route.from[2],bx=route.to[0]-point[0],bz=route.to[2]-point[2];
        check(std::fabs(ax*bz-az*bx)<1e-2f*std::sqrt((ax*ax+az*az)*(bx*bx+bz*bz)) && ax*bx+az*bz>0.0f,"entry, point and exit on one straight line");
        check(PastGround(log,route.to)>=kPassLeast-0.01f && InSquare(route.to,big.half),"the line's far end off the map, inside the square");
        PassLine line;
        check(MakePassLine(log,big,point,route.heading,turn,line),"its pass line");
        const float hiEnd[3]={point[0]+line.dir[0]*line.hi,0,point[2]+line.dir[2]*line.hi},loEnd[3]={point[0]+line.dir[0]*line.lo,0,point[2]+line.dir[2]*line.lo};
        check(line.lo<0 && line.hi>0 && PastGround(log,hiEnd)>=kPassLeast-0.01f && PastGround(log,loEnd)>=kPassLeast-0.01f,
              "both ends of the passes off the map");
        // Room to turn round past each end inside the square (its excursion is about its turn's diameter).
        check(InSquare(hiEnd,big.half-kTurnRoom*turn) && InSquare(loEnd,big.half-kTurnRoom*turn),"room to turn round inside the square");
        // The heading it was given: the line runs along it.
        check(line.dir[0]*route.heading[0]+line.dir[2]*route.heading[2]>0.999f,"the pass line along the heading it came in on");
    }
    // A pass needs room to turn round past both ends: none, no route for the plane (the strike jet takes it).
    {
        const PlayArea wide{{-2300,-2300},{2300,2300},true,0,true};
        check(AirRoute(wide,stock,target,observer,preferred,150,266.0f,clear,mapGround(wide),route)==Refusal::noEntry,
              "no room for the passes' turns off the map: no route for the plane");
        check(AirRoute(wide,{2650,1000},target,observer,preferred,150,0.0f,clear,mapGround(wide),route)==Refusal::none,
              "a call with no passes still comes in");
    }
    // Ground entries (2026-10-09): a stock map's edges lie 1100-1700 m from a central target, past the route planner's
    // +-1024 m square, so every edge was refused. Candidates are rings within its reach, and the field's edge where near.
    const PlayArea stockMap{{-1600,-1600},{1597,1597},true,0,true};
    const float centre[3]={71,24,455},caller[3]={71,24,455};
    const auto none=[](const float*){return false;};
    const auto entries=GroundEntryCandidates(stockMap,centre,caller,1000.0f,none);
    check(entries.count>=kGroundBearings,"a central target on a stock map has ground entry candidates");
    for(int i=0;i<entries.count;++i) {
        const float p[3]={entries.at[i][0],0,entries.at[i][1]};
        check(FlatDistance(p,centre)>=kMinJourney-0.5f && FlatDistance(p,caller)>=kObserverClear,"every entry: a journey, away from the caller");
        check(std::fabs(p[0]-centre[0])<256*kGroundPlanCell && std::fabs(p[2]-centre[2])<256*kGroundPlanCell,
              "every entry inside the planner's square round it");
        check(p[0]>=stockMap.lo[0]+kEntryInset-0.01f && p[0]<=stockMap.hi[0]-kEntryInset+0.01f && p[2]>=stockMap.lo[1]+kEntryInset-0.01f &&
              p[2]<=stockMap.hi[1]-kEntryInset+0.01f,"every entry inside the measured play area");
    }
    const float east[3]={900,0,0};const auto away=GroundEntryCandidates(stockMap,centre,east,1000.0f,none);
    const float first[3]={away.at[0][0],0,away.at[0][1]},second[3]={away.at[1][0],0,away.at[1][1]};
    check(away.count>0 && FlatDistance(first,east)>=FlatDistance(second,east),"within a ring the farthest from the caller is tried first");
    const PlayArea corner{{-700,-700},{700,700},true,0,true};const float edge[3]={650,0,650};
    const auto few=GroundEntryCandidates(corner,edge,nullptr,1000.0f,none);
    for(int i=0;i<few.count;++i)check(few.at[i][0]<=670 && few.at[i][1]<=670,"no entry past a near map edge");
    check(GroundEntryCandidates(unknown,centre,caller,1000.0f,none).count==0,"no measured area: no entries");
    // Out of the caller's sight first (2026-10-10: a vehicle made 800 m from the caller in plain sight): past its draw
    // distance, then hidden behind the map, the visible ones last.
    {
        const float west[3]={-600,0,455};
        const auto hiddenEast=[&](const float* p){return p[0]>centre[0]+100.0f;};   // a ridge east of the target
        const auto seen=GroundEntryCandidates(stockMap,centre,west,1000.0f,hiddenEast);
        check(seen.count>0,"candidates with a ridge");
        for(int i=1;i<seen.count;++i)check(seen.seen[i-1]<=seen.seen[i],"the ones out of sight before the visible ones");
        int unseen=0;
        for(int i=0;i<seen.count;++i) {
            const float p[3]={seen.at[i][0],0,seen.at[i][1]};
            const Seen want=FlatDistance(p,west)>=1000.0f+kDrawClear ? Seen::beyond : hiddenEast(p) ? Seen::hidden : Seen::visible;
            check(seen.seen[i]==want,"each candidate's sight as the caller has it");
            unseen+=want!=Seen::visible;
        }
        check(unseen>0 && seen.seen[0]!=Seen::visible,"an unseen entry is tried first");
        // The field's edge on a bearing, where it is within the rings' reach: a target near the east edge.
        const float nearEdge[3]={800,0,0};
        const auto edgeEntries=GroundEntryCandidates(stockMap,nearEdge,nullptr,1000.0f,none);
        bool onEdge=false;
        for(int i=0;i<edgeEntries.count;++i)onEdge=onEdge || std::fabs(edgeEntries.at[i][0]-(stockMap.hi[0]-kEntryInset))<0.5f;
        check(onEdge,"the field's edge is a candidate where the rings do not reach it");
    }
    // Air formation (2026-10-09: created in the air at the edge): line abreast, alternating sides, all at the route's
    // height, none behind the entry.
    const Route lead{{-1400,400,0},{1,0,0}};
    float slots[8][3];
    for(int i=0;i<8;++i)AirFormationSlot(lead,i,1.0f,slots[i]);
    check(slots[0][0]==-1400 && slots[0][1]==400 && slots[0][2]==0,"slot 0 is the lead at the entry");
    for(int i=1;i<8;++i) {
        check(slots[i][1]==400 && slots[i][0]==-1400 && std::fabs(slots[i][2])>=kFormationSide-0.01f,"every other slot abreast, same height, never behind");
        for(int k=0;k<i;++k)check(std::fabs(slots[i][2]-slots[k][2])>=kFormationSide-0.01f,"slots kFormationSide apart (no two aircraft made in one place)");
    }
    check(slots[1][2]*slots[2][2]<0,"the line alternates sides");
    float close[3];AirFormationSlot(lead,1,0.6f,close);
    check(std::fabs(close[2])<std::fabs(slots[1][2]),"helicopters fly closer");
    std::printf("support_entry_test: %d checks passed\n",checks);
}
