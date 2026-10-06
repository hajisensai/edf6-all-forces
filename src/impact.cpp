// What a plane ran into (the user, 2026-10-06: the player's jet died 856 m over the ground on "blocked air: sent 161
// m/s, made 25", and the log never said what it hit). The jets' "blocked" (jet_flight.cpp Sense, playerjet.cpp
// Blocked) only sees the body held back; LogImpact, called there, names what is round the body then:
//  - the map: rays from the body along its way and the six axes, kImpactRay m out, through the map ray (terrain and
//    buildings, heli.cpp CastRay kMapLayer) and the building ray (the building layers alone, BuildingRay): the nearest
//    hit, a building when the building ray finds it as near, else terrain;
//  - the objects: every lock point the game registers (the lock-on list: units, vehicles, ships, whatever side;
//    VisitLockPoints) and the plugin's own jets (often not lockable, by their own side), the kImpactNearest nearest
//    within kImpactReach m: the vehicle's class, else the object's C++ class from its vtable's RTTI, its team, distance.
// What it cannot name: an object nobody may lock (a dropped weapon, a corpse, a static prop not of the map's own
// layers) and the SGO an object was made from (no field for it is known). An object counted is its lock point's
// distance, not its body's (a mothership's hull is far bigger than the point).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "edf/host.h"
#include "jet_internal.h"
#include "memory.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crew {
namespace {
constexpr float kImpactRay=40.0f,kImpactReach=250.0f;
constexpr int kImpactNearest=3;

struct Near { const void* object; float d; float at[3]; };
struct Gather { const void* self; const float* pos; Near best[kImpactNearest]; int n; };

void Keep(Gather& g,const void* object,const float* at) noexcept {
    if(!object || object==g.self)return;
    const float d[3]={at[0]-g.pos[0],at[1]-g.pos[1],at[2]-g.pos[2]};
    const float dist=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(dist>kImpactReach)return;
    for(int i=0;i<g.n;++i)if(g.best[i].object==object) {   // an object's nearest lock point
        if(dist<g.best[i].d){g.best[i].d=dist;std::memcpy(g.best[i].at,at,12);}
        return;
    }
    int slot=g.n<kImpactNearest ? g.n++ : -1;
    if(slot<0) {
        slot=0;
        for(int i=1;i<g.n;++i)if(g.best[i].d>g.best[slot].d)slot=i;
        if(g.best[slot].d<=dist)return;
    }
    g.best[slot]=Near{object,dist,{at[0],at[1],at[2]}};
}

void VisitPoint(void* ctx,const void* object,const float* at) noexcept { Keep(*static_cast<Gather*>(ctx),object,at); }

// The object's C++ class from its vtable's complete object locator (vtable -8; the type descriptor's image offset at
// +0xC, its name ".?AV<class>@@" at +0x10), the ".?AV" and "@@" cut off, into `out`. "?" when it cannot be read.
void ClassName(const void* object,char* out,std::size_t size) noexcept {
    std::snprintf(out,size,"?");
    if(!Readable(object,8))return;
    const auto vt=At<const unsigned char*>(object,0);
    if(vt<image+8 || vt>=image+edf::kImageSize)return;
    if(!Readable(vt-8,8))return;
    const auto col=*reinterpret_cast<const unsigned char* const*>(vt-8);
    if(!Readable(col,0x10))return;
    const std::uint32_t td=At<std::uint32_t>(col,0xC);
    if(!td || td+0x10+4>=edf::kImageSize)return;
    const char* name=reinterpret_cast<const char*>(image+td+0x10);
    if(std::strncmp(name,".?AV",4)==0)name+=4;
    std::size_t n=0;
    while(n+1<size && name[n] && !(name[n]=='@' && name[n+1]=='@') && image+td+0x10+4+n<image+edf::kImageSize)out[n]=name[n],++n;
    out[n]=0;
}

// The nearest map hit along the rays round `pos` (`dir` its way, unit): its distance (-1: none), and whether it is a
// building's.
float MapNear(const float* pos,const float* dir,bool* building,float* at) noexcept {
    const float ways[7][3]={{dir[0],dir[1],dir[2]},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    float best=-1.0f;
    *building=false;
    for(const auto& w:ways) {
        const float end[3]={pos[0]+w[0]*kImpactRay,pos[1]+w[1]*kImpactRay,pos[2]+w[2]*kImpactRay};
        float hit[3],bhit[3];
        const float d=MapRay(pos,end,hit);
        if(d<0.0f || (best>=0.0f && d>=best))continue;
        best=d;std::memcpy(at,hit,12);
        const float b=BuildingRay(pos,end,bhit);
        *building=b>=0.0f && b<=d+1.0f;
    }
    return best;
}
}  // namespace

void LogImpact(const char* who,const void* self,const float* pos,const float* way) noexcept {
    __try {
        float dir[3]={way[0],way[1],way[2]};
        const float l=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
        if(l>1e-3f)for(auto& c:dir)c/=l;
        else{dir[0]=0;dir[1]=0;dir[2]=1;}
        bool building=false;
        float hit[3]{};
        const float map=MapNear(pos,dir,&building,hit);
        const float clear=GroundClearance(pos);
        char ground[24];
        if(clear==kNoGround)std::snprintf(ground,sizeof(ground),"none under it");
        else std::snprintf(ground,sizeof(ground),"%.0f m",clear);
        if(map>=0.0f)
            Log("%s v=%p hit: %s %.1f m away at (%.0f,%.0f,%.0f); over the ground %s",who,self,building ? "a building" : "terrain",map,
                hit[0],hit[1],hit[2],ground);
        else Log("%s v=%p hit: no map within %.0f m (over the ground %s)",who,self,kImpactRay,ground);
        Gather g{self,pos,{},0};
        VisitLockPoints(&VisitPoint,&g);
        for(const auto& j:jet::jets)
            if(j.ref && j.Vehicle()!=self)Keep(g,j.Vehicle(),reinterpret_cast<const float*>(j.Vehicle()+kPosition));
        if(!g.n){Log("%s v=%p hit: no object within %.0f m",who,self,kImpactReach);return;}
        for(int i=0;i<g.n;++i) {
            const Near& n=g.best[i];
            char name[64];
            if(VehicleClassOf(n.object)>=0)std::snprintf(name,sizeof(name),"%s",VehicleClassName(n.object));
            else ClassName(n.object,name,sizeof(name));
            const std::int32_t team=Readable(n.object,kTeam+4) ? At<std::int32_t>(n.object,kTeam) : -1;
            Log("%s v=%p hit: near %p %s team %d, %.1f m (at %.0f,%.0f,%.0f)%s",who,self,n.object,name,static_cast<int>(team),n.d,
                n.at[0],n.at[1],n.at[2],jet::FindJet(static_cast<const unsigned char*>(n.object)) ? " (a plugin jet)" : "");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
}  // namespace crew
