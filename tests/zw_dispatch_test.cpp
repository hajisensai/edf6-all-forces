// The Vehicler's start vehicles' placement (src/zw_vehicle_dispatch_logic.h) and the table's lookups it plans with
// (src/zw_vehicle_catalog.h): pure, no game.
#include "zw_vehicle_dispatch_logic.h"
#include "zw_vehicle_catalog.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0;
void Check(bool ok,const char* what) {
    if(!ok){++failures;std::printf("FAIL: %s\n",what);}
}
using crew::zwplace::Footprint;
bool AnyOverlap(const Footprint* f,int count) {
    for(int i=0;i<count;++i)for(int j=i+1;j<count;++j)if(crew::zwplace::Overlap(f[i],f[j],crew::zwplace::kVehicleGap-0.01f))return true;
    return false;
}
// Plans one vehicle of `kind` for each of `count` players at `at` (pairs) facing (fx, fz), as the host does: one request
// after another, each with what was planned before and every player as a person.
int PlanAll(int kind,const float (*at)[2],int count,float fx,float fz,Footprint* out,bool (*clear)(const Footprint&)) {
    const auto& v=crew::kZwVehicles[kind];int planned=0;
    for(int p=0;p<count;++p) {
        Footprint f{};
        if(crew::zwplace::Place(at[p][0],at[p][1],fx,fz,v.halfWidth,v.halfLength,out,planned,at,count,clear,&f))out[planned++]=f;
    }
    return planned;
}
bool Open(const Footprint&) { return true; }
bool Closed(const Footprint&) { return false; }
// Only the left of the x = 0 line is ground (a cliff right of it).
bool LeftOnly(const Footprint& f) { return f.x+f.halfWidth<0.0f; }
}  // namespace

int main() {
    using namespace crew;
    using namespace crew::zwplace;
    // The table: kinds by row and resource, both ways, and nothing for anything else.
    for(int k=0;k<kZwVehicleCount;++k) {
        Check(ZwVehicleKindOfRow(kZwVehicles[k].row)==k,"a row's kind");
        Check(ZwVehicleKindOfResource(ZwVehicleResource(k))==k,"a resource's kind");
        Check(kZwVehicles[k].halfWidth>0 && kZwVehicles[k].halfLength>=kZwVehicles[k].halfWidth,"an envelope longer than wide");
        Check(ZwVehiclePrice(k)==kZwVehicles[k].logisticsCost && ZwVehiclePrice(k)<=kZwLogisticsGauge,"a price within the gauge");
    }
    Check(ZwVehicleKindOfResource(ZwVehicleResource(0)-1)<0 && ZwVehicleKindOfResource(ZwVehicleResource(kZwVehicleCount))<0,
          "no kind outside the resources");
    Check(ZwVehiclePrice(-1)==0 && ZwVehiclePrice(kZwVehicleCount)==0,"no price for no kind");

    // Overlap: two equal boxes side by side, touching / apart / turned.
    Footprint a{};a.halfWidth=2;a.halfLength=4;
    Footprint b=a;b.x=4.5f;
    Check(!Overlap(a,b,0.0f),"0.5 m apart: no overlap without a gap");
    Check(Overlap(a,b,2.0f),"0.5 m apart: within a 2 m gap");
    b.x=6.5f;Check(!Overlap(a,b,2.0f),"2.5 m apart: clear of a 2 m gap");
    b.x=0;b.z=7.0f;b.fx=1;b.fz=0;   // turned 90 degrees, its long side across a's end
    Check(Overlap(a,b,2.0f) && !Overlap(a,b,0.5f),"a turned box: 1 m past a's end");
    Check(Covers(a,0,3.5f,1.0f) && !Covers(a,3.5f,0,1.0f),"a person inside / beside a footprint");

    // The first spot: right of the player (right = (fz, -fx)), clear of the player by kSideGap.
    {
        const auto& v=kZwVehicles[2];Footprint f{};
        const float player[1][2]={{10.0f,20.0f}};
        Check(Place(10.0f,20.0f,0.0f,2.0f,v.halfWidth,v.halfLength,nullptr,0,player,1,Open,&f),"open ground: a spot");
        Check(std::fabs(f.x-(10.0f+v.halfWidth+kSideGap))<1e-4f && std::fabs(f.z-20.0f)<1e-4f && f.fz==1.0f && f.fx==0.0f,
              "beside the player on its right, facing its way (the heading made unit)");
        Check(!Covers(f,10.0f,20.0f,kPersonRadius),"the player is not under it");
        // Facing +x: right is -z.
        Check(Place(0,0,1,0,v.halfWidth,v.halfLength,nullptr,0,nullptr,0,Open,&f) && f.z<0 && std::fabs(f.x)<1e-4f,
              "facing +x the vehicle stands at -z");
    }
    // Four players two metres apart in a row, all facing the same way: four vehicles, none on another or on a player.
    for(int kind=0;kind<kZwVehicleCount;++kind) {
        const float row[4][2]={{0,0},{2,0},{4,0},{6,0}};Footprint planned[4]{};
        Check(PlanAll(kind,row,4,0,1,planned,Open)==4,"four players in a row: four vehicles");
        Check(!AnyOverlap(planned,4),"four players in a row: no two vehicles within the gap");
        for(int i=0;i<4;++i)for(int p=0;p<4;++p)Check(!Covers(planned[i],row[p][0],row[p][1],kPersonRadius),"no player under a vehicle");
    }
    // Four players in a square two metres a side, facing the same way, then all facing different ways.
    for(int kind=0;kind<kZwVehicleCount;++kind) {
        const float square[4][2]={{0,0},{2,0},{0,2},{2,2}};Footprint planned[4]{};
        Check(PlanAll(kind,square,4,0,1,planned,Open)==4 && !AnyOverlap(planned,4),"a square of four: four vehicles, apart");
        const auto& v=kZwVehicles[kind];int count=0;
        const float headings[4][2]={{0,1},{1,0},{0,-1},{-1,0}};
        for(int p=0;p<4;++p) {
            Footprint f{};
            if(Place(square[p][0],square[p][1],headings[p][0],headings[p][1],v.halfWidth,v.halfLength,planned,count,square,4,Open,&f))
                planned[count++]=f;
        }
        Check(count==4 && !AnyOverlap(planned,4),"a square of four facing four ways: four vehicles, apart");
    }
    // The ground decides: only the left is ground, the vehicle goes left; no ground at all, refused.
    {
        const auto& v=kZwVehicles[0];Footprint f{};
        Check(Place(0,0,0,1,v.halfWidth,v.halfLength,nullptr,0,nullptr,0,LeftOnly,&f) && f.x<0,"a cliff on the right: the left side");
        Check(!Place(0,0,0,1,v.halfWidth,v.halfLength,nullptr,0,nullptr,0,Closed,&f),"no clear ground: refused");
    }
    // Fully blocked: every candidate on a planned vehicle (a wall of them each side) or a person: refused, never overlapping.
    {
        const auto& v=kZwVehicles[2];
        Footprint wall[2]{};
        wall[0].halfWidth=wall[1].halfWidth=40.0f;wall[0].halfLength=wall[1].halfLength=v.halfLength;
        wall[0].x=1.0f+40.0f;wall[1].x=-1.0f-40.0f;
        Footprint f{};
        Check(!Place(0,0,0,1,v.halfWidth,v.halfLength,wall,2,nullptr,0,Open,&f),"walled in both sides: refused");
        float crowd[kCandidates][2]{};
        for(int i=0;i<kCandidates;++i)Candidate(0,0,0,1,v.halfWidth,i,crowd[i][0],crowd[i][1]);
        Check(!Place(0,0,0,1,v.halfWidth,v.halfLength,nullptr,0,crowd,kCandidates,Open,&f),"a person on every spot: refused");
    }
    // Nothing invented: no heading, a bad position, no envelope.
    {
        Footprint f{};
        Check(!Place(0,0,0,0,1,2,nullptr,0,nullptr,0,Open,&f),"no heading: refused");
        Check(!Place(NAN,0,0,1,1,2,nullptr,0,nullptr,0,Open,&f),"a bad position: refused");
        Check(!Place(0,0,0,1,0,2,nullptr,0,nullptr,0,Open,&f) && !Place(0,0,0,1,1,2,nullptr,0,nullptr,0,Open,nullptr),
              "no envelope / no output: refused");
    }
    if(failures){std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("zw_dispatch: all passed\n");
    return 0;
}
